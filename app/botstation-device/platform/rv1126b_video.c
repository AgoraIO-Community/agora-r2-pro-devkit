/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <mybot/platform/mybot_video.h>
#include <api/aosl_log.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <rk_mpi.h>
#include <mpp_buffer.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_venc_cfg.h>
#include <rk_venc_rc.h>

#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
#define VIDEO_FPS 3
#define VIDEO_MIN_BPS 100000
#define VIDEO_MAX_BPS 1000000
#define VIDEO_BUFFERS 4
#define VIDEO_RAW_BYTES (VIDEO_WIDTH * VIDEO_HEIGHT * 3 / 2)
#define VIDEO_PAYLOAD_PERCENT 80

typedef struct {
    void *address;
    size_t length;
} capture_buffer_t;

typedef struct {
    mybot_video_frame_handler_t handler;
    void *user_data;
    pthread_mutex_t mutex;
    pthread_t thread;
    bool thread_started, stopping, streaming, buffers_requested;
    int fd;
    enum v4l2_buf_type type;
    unsigned int buffer_count, pitch;
    capture_buffer_t buffers[VIDEO_BUFFERS];
    MppCtx encoder;
    MppApi *api;
    MppEncCfg cfg;
    MppBufferGroup group;
    MppBuffer raw, encoded;
    MppFrame frame;
    MppPacket packet;
    uint32_t target_bps;
    int64_t last_send_ms, stats_start_ms;
    size_t last_send_bytes;
    uint64_t encoded_bytes, submitted_bytes;
    unsigned int encoded_frames, submitted_frames, budget_drops;
    bool snapshot_attempted;
} video_context_t;

static int checked(MPP_RET result, const char *operation) {
    if (result != MPP_OK)
        AOSL_LOG_ERR("camera: %s failed (%d)", operation, result);
    return result == MPP_OK ? 0 : -1;
}

static int camera_ioctl(int fd, unsigned long request, void *arg) {
    int ret;
    do {
        ret = ioctl(fd, request, arg);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

static int capture_ioctl(video_context_t *ctx, unsigned long request, void *arg,
                         const char *operation) {
    if (camera_ioctl(ctx->fd, request, arg) == 0)
        return 0;
    AOSL_LOG_ERR("camera: %s: %s", operation, strerror(errno));
    return -1;
}

static bool is_stopping(video_context_t *ctx) {
    pthread_mutex_lock(&ctx->mutex);
    bool stopping = ctx->stopping;
    pthread_mutex_unlock(&ctx->mutex);
    return stopping;
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        AOSL_LOG_ERR("camera: clock_gettime: %s", strerror(errno));
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Select the ISP processed output, never guess /dev/video0 or a sensor ID. */
static int find_camera(char *device, size_t size) {
    const char *configured = getenv("BOTSTATION_VIDEO_DEVICE");
    if (configured && configured[0]) {
        int len = snprintf(device, size, "%s", configured);
        if (len < 0 || (size_t)len >= size) {
            AOSL_LOG_ERR("camera: BOTSTATION_VIDEO_DEVICE is too long");
            return -1;
        }
        return 0;
    }
    glob_t nodes = {0};
    int ret = glob("/sys/class/video4linux/video*/name", 0, NULL, &nodes);
    if (ret != 0) {
        AOSL_LOG_ERR("camera: cannot enumerate V4L2 nodes (%d)", ret);
        globfree(&nodes);
        return -1;
    }
    unsigned int matches = 0;
    for (size_t i = 0; i < nodes.gl_pathc; ++i) {
        FILE *file = fopen(nodes.gl_pathv[i], "r");
        char name[80];
        if (!file) {
            AOSL_LOG_ERR("camera: read %s: %s", nodes.gl_pathv[i], strerror(errno));
            globfree(&nodes);
            return -1;
        }
        bool mainpath = false;
        if (fgets(name, sizeof(name), file)) {
            name[strcspn(name, "\r\n")] = '\0';
            mainpath = strcmp(name, "rkisp_mainpath") == 0;
        } else {
            AOSL_LOG_ERR("camera: empty/unreadable V4L2 name: %s", nodes.gl_pathv[i]);
            fclose(file);
            globfree(&nodes);
            return -1;
        }
        fclose(file);
        if (mainpath) {
            const char *node = nodes.gl_pathv[i] + strlen("/sys/class/video4linux/");
            size_t length = strcspn(node, "/");
            snprintf(device, size, "/dev/%.*s", (int)length, node);
            ++matches;
        }
    }
    globfree(&nodes);
    if (matches != 1) {
        AOSL_LOG_ERR("camera: found %u rkisp_mainpath nodes; set BOTSTATION_VIDEO_DEVICE",
                     matches);
        return -1;
    }
    return 0;
}

static void buffer_args(video_context_t *ctx, struct v4l2_buffer *buffer,
                        struct v4l2_plane *plane) {
    memset(buffer, 0, sizeof(*buffer));
    memset(plane, 0, sizeof(*plane));
    buffer->type = ctx->type;
    buffer->memory = V4L2_MEMORY_MMAP;
    if (ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        buffer->m.planes = plane;
        buffer->length = 1;
    }
}

static int prepare_capture(video_context_t *ctx) {
    char device[256];
    struct v4l2_capability cap = {0};
    struct v4l2_format format = {0};
    struct stat statbuf;
    if (find_camera(device, sizeof(device)))
        return -1;
    ctx->fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (ctx->fd < 0) {
        AOSL_LOG_ERR("camera: open %s: %s", device, strerror(errno));
        return -1;
    }
    if (fstat(ctx->fd, &statbuf) || !S_ISCHR(statbuf.st_mode)) {
        AOSL_LOG_ERR("camera: %s is not a V4L2 character device", device);
        return -1;
    }
    if (capture_ioctl(ctx, VIDIOC_QUERYCAP, &cap, "query capabilities"))
        return -1;
    uint32_t caps = cap.capabilities & V4L2_CAP_DEVICE_CAPS ?
        cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_STREAMING) ||
        !(caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE))) {
        AOSL_LOG_ERR("camera: %s does not support streaming capture", device);
        return -1;
    }
    ctx->type = caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE ?
        V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.type = ctx->type;
    if (ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        format.fmt.pix_mp.width = VIDEO_WIDTH;
        format.fmt.pix_mp.height = VIDEO_HEIGHT;
        format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
        format.fmt.pix_mp.field = V4L2_FIELD_NONE;
        format.fmt.pix_mp.num_planes = 1;
    } else {
        format.fmt.pix.width = VIDEO_WIDTH;
        format.fmt.pix.height = VIDEO_HEIGHT;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
        format.fmt.pix.field = V4L2_FIELD_NONE;
    }
    if (capture_ioctl(ctx, VIDIOC_S_FMT, &format, "set NV12 format"))
        return -1;
    unsigned int width, height, fourcc, planes = 1;
    if (ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        width = format.fmt.pix_mp.width;
        height = format.fmt.pix_mp.height;
        fourcc = format.fmt.pix_mp.pixelformat;
        planes = format.fmt.pix_mp.num_planes;
        ctx->pitch = format.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        width = format.fmt.pix.width;
        height = format.fmt.pix.height;
        fourcc = format.fmt.pix.pixelformat;
        ctx->pitch = format.fmt.pix.bytesperline;
    }
    if (width != VIDEO_WIDTH || height != VIDEO_HEIGHT || fourcc != V4L2_PIX_FMT_NV12 ||
        planes != 1 || ctx->pitch < VIDEO_WIDTH || ctx->pitch > 16384) {
        AOSL_LOG_ERR("camera: unsupported negotiated layout %ux%u fourcc=%#x planes=%u pitch=%u",
                     width, height, fourcc, planes, ctx->pitch);
        return -1;
    }
    struct v4l2_requestbuffers request = {
        .count = VIDEO_BUFFERS, .type = ctx->type, .memory = V4L2_MEMORY_MMAP
    };
    if (capture_ioctl(ctx, VIDIOC_REQBUFS, &request, "request capture buffers"))
        return -1;
    ctx->buffers_requested = true;
    if (request.count < 2 || request.count > VIDEO_BUFFERS) {
        AOSL_LOG_ERR("camera: invalid buffer count %u", request.count);
        return -1;
    }
    ctx->buffer_count = request.count;
    for (unsigned int i = 0; i < ctx->buffer_count; ++i) {
        struct v4l2_buffer buffer;
        struct v4l2_plane plane;
        buffer_args(ctx, &buffer, &plane);
        buffer.index = i;
        if (capture_ioctl(ctx, VIDIOC_QUERYBUF, &buffer, "query capture buffer"))
            return -1;
        size_t length = ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ?
            plane.length : buffer.length;
        off_t offset = ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ?
            plane.m.mem_offset : buffer.m.offset;
        if (length < (size_t)ctx->pitch * VIDEO_HEIGHT * 3 / 2) {
            AOSL_LOG_ERR("camera: capture buffer is too small (%zu)", length);
            return -1;
        }
        void *address = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, offset);
        if (address == MAP_FAILED) {
            AOSL_LOG_ERR("camera: mmap: %s", strerror(errno));
            return -1;
        }
        ctx->buffers[i].address = address;
        ctx->buffers[i].length = length;
        if (capture_ioctl(ctx, VIDIOC_QBUF, &buffer, "queue capture buffer"))
            return -1;
    }
    AOSL_LOG_NTC("camera: %s NV12 %ux%u pitch=%u; ISP owned by rkaiq_3A_server",
                 device, width, height, ctx->pitch);
    return 0;
}

static int config_set(video_context_t *ctx, const char *key, RK_S32 value) {
    return checked(mpp_enc_cfg_set_s32(ctx->cfg, key, value), key);
}

static int apply_bitrate(video_context_t *ctx, uint32_t bps) {
    bps = bps * VIDEO_PAYLOAD_PERCENT / 100;
    /* MPP needs a nonzero target even while the network budget pauses sending. */
    if (bps < VIDEO_MIN_BPS)
        bps = VIDEO_MIN_BPS;
    if (config_set(ctx, "rc:bps_target", bps) ||
        config_set(ctx, "rc:bps_min", bps * 15 / 16) ||
        config_set(ctx, "rc:bps_max", bps * 17 / 16))
        return -1;
    return checked(ctx->api->control(ctx->encoder, MPP_ENC_SET_CFG, ctx->cfg), "apply encoder config");
}

static int prepare_encoder(video_context_t *ctx) {
    if (checked(mpp_create(&ctx->encoder, &ctx->api), "create MPP encoder"))
        return -1;
    MppPollType timeout = 500;
    if (checked(ctx->api->control(ctx->encoder, MPP_SET_INPUT_TIMEOUT, &timeout), "input timeout") ||
        checked(ctx->api->control(ctx->encoder, MPP_SET_OUTPUT_TIMEOUT, &timeout), "output timeout") ||
        checked(mpp_init(ctx->encoder, MPP_CTX_ENC, MPP_VIDEO_CodingMJPEG), "init MJPEG") ||
        checked(mpp_enc_cfg_init(&ctx->cfg), "init encoder config") ||
        checked(ctx->api->control(ctx->encoder, MPP_ENC_GET_CFG, ctx->cfg), "get encoder config"))
        return -1;
    const struct { const char *key; RK_S32 value; } settings[] = {
        {"codec:type", MPP_VIDEO_CodingMJPEG},
        {"prep:width", VIDEO_WIDTH}, {"prep:height", VIDEO_HEIGHT},
        {"prep:hor_stride", VIDEO_WIDTH}, {"prep:ver_stride", VIDEO_HEIGHT},
        {"prep:rotation", MPP_ENC_ROT_180}, {"prep:mirroring", 0},
        {"prep:format", MPP_FMT_YUV420SP}, {"rc:mode", MPP_ENC_RC_MODE_CBR},
        {"rc:fps_in_flex", 0}, {"rc:fps_in_num", VIDEO_FPS}, {"rc:fps_in_denom", 1},
        {"rc:fps_out_flex", 0}, {"rc:fps_out_num", VIDEO_FPS}, {"rc:fps_out_denom", 1},
        {"jpeg:q_factor", 60}, {"jpeg:qf_min", 5}, {"jpeg:qf_max", 80}
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i)
        if (config_set(ctx, settings[i].key, settings[i].value))
            return -1;
    pthread_mutex_lock(&ctx->mutex);
    uint32_t bps = ctx->target_bps;
    pthread_mutex_unlock(&ctx->mutex);
    if (apply_bitrate(ctx, bps) ||
        checked(mpp_buffer_group_get_internal(&ctx->group,
                  MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_CACHABLE), "allocate buffer group") ||
        checked(mpp_buffer_get(ctx->group, &ctx->raw, VIDEO_RAW_BYTES), "allocate raw buffer") ||
        checked(mpp_buffer_get(ctx->group, &ctx->encoded, VIDEO_RAW_BYTES), "allocate JPEG buffer"))
        return -1;
    return 0;
}

static int copy_nv12(video_context_t *ctx, const struct v4l2_buffer *buffer,
                     const struct v4l2_plane *plane) {
    if (buffer->index >= ctx->buffer_count || (buffer->flags & V4L2_BUF_FLAG_ERROR)) {
        AOSL_LOG_ERR("camera: invalid capture buffer index/flags");
        return -1;
    }
    const capture_buffer_t *mapped = &ctx->buffers[buffer->index];
    size_t used = ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ?
        plane->bytesused : buffer->bytesused;
    size_t offset = ctx->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane->data_offset : 0;
    size_t needed = (size_t)ctx->pitch * VIDEO_HEIGHT * 3 / 2;
    if (used > mapped->length || offset > used || needed > used - offset) {
        AOSL_LOG_ERR("camera: incomplete NV12 buffer used=%zu offset=%zu required=%zu",
                     used, offset, needed);
        return -1;
    }
    unsigned char *destination = mpp_buffer_get_ptr(ctx->raw);
    if (!destination) {
        AOSL_LOG_ERR("camera: MPP raw buffer is not mapped");
        return -1;
    }
    if (checked(mpp_buffer_sync_begin(ctx->raw), "begin raw buffer CPU access"))
        return -1;
    const unsigned char *source = (const unsigned char *)mapped->address + offset;
    for (unsigned int row = 0; row < VIDEO_HEIGHT * 3 / 2; ++row)
        memcpy(destination + (size_t)row * VIDEO_WIDTH,
               source + (size_t)row * ctx->pitch, VIDEO_WIDTH);
    return checked(mpp_buffer_sync_end(ctx->raw), "end raw buffer CPU access");
}

/* MJPEG frames are independent: skip rather than queue beyond the BWE budget. */
static bool budget_allows(video_context_t *ctx, int64_t now, size_t bytes, uint32_t bps) {
    uint32_t payload_bps = bps * VIDEO_PAYLOAD_PERCENT / 100;
    if (!payload_bps || bytes > payload_bps / 8)
        return false;
    uint64_t interval = ((uint64_t)ctx->last_send_bytes * 8000 + payload_bps - 1) / payload_bps;
    return ctx->last_send_ms < 0 ||
        (now >= ctx->last_send_ms && (uint64_t)(now - ctx->last_send_ms) >= interval);
}

static void log_video_stats(video_context_t *ctx, int64_t now, uint32_t bps) {
    int64_t elapsed = now - ctx->stats_start_ms;
    if (elapsed < 2000)
        return;
    AOSL_LOG_NTC("camera: target=%u payload_budget=%u encoded_bps=%llu submitted_bps=%llu "
                 "encoded_frames=%u submitted_frames=%u budget_drops=%u interval_ms=%lld",
                 bps, bps * VIDEO_PAYLOAD_PERCENT / 100,
                 (unsigned long long)(ctx->encoded_bytes * 8000 / elapsed),
                 (unsigned long long)(ctx->submitted_bytes * 8000 / elapsed),
                 ctx->encoded_frames, ctx->submitted_frames, ctx->budget_drops,
                 (long long)elapsed);
    ctx->stats_start_ms = now;
    ctx->encoded_bytes = ctx->submitted_bytes = 0;
    ctx->encoded_frames = ctx->submitted_frames = ctx->budget_drops = 0;
}

static int save_snapshot(const unsigned char *data, size_t length, const char *path) {
    char temporary[512];
    int count = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path);
    if (count < 0 || (size_t)count >= sizeof(temporary)) {
        AOSL_LOG_ERR("camera: snapshot path too long");
        return -1;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) {
        AOSL_LOG_ERR("camera: create snapshot %s: %s", path, strerror(errno));
        return -1;
    }
    int error = 0;
    size_t written = 0;
    while (written < length) {
        ssize_t bytes = write(fd, data + written, length - written);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0) {
            error = bytes < 0 ? errno : EIO;
            break;
        }
        written += (size_t)bytes;
    }
    if (!error && fsync(fd) < 0)
        error = errno;
    if (close(fd) < 0 && !error)
        error = errno;
    if (!error && rename(temporary, path) < 0)
        error = errno;
    if (error) {
        AOSL_LOG_ERR("camera: save snapshot %s: %s", path, strerror(error));
        if (unlink(temporary) < 0)
            AOSL_LOG_ERR("camera: remove snapshot temporary file: %s", strerror(errno));
        return -1;
    }
    char *separator = strrchr(temporary, '/');
    if (separator)
        separator[separator == temporary ? 1 : 0] = '\0';
    int directory = open(separator ? temporary : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
        AOSL_LOG_ERR("camera: open snapshot directory for sync: %s", strerror(errno));
        return -1;
    }
    if (fsync(directory) < 0)
        error = errno;
    if (close(directory) < 0 && !error)
        error = errno;
    if (error) {
        AOSL_LOG_ERR("camera: sync snapshot directory %s: %s", path, strerror(error));
        return -1;
    }
    AOSL_LOG_NTC("camera: snapshot saved to %s (%zu bytes, encoder rotation=180, mirror=0)",
                 path, length);
    return 0;
}

static int encode_frame(video_context_t *ctx, int64_t pts) {
    if (checked(mpp_frame_init(&ctx->frame), "init raw frame"))
        return -1;
    MppFrame frame = ctx->frame;
    mpp_frame_set_width(frame, VIDEO_WIDTH);
    mpp_frame_set_height(frame, VIDEO_HEIGHT);
    mpp_frame_set_hor_stride(frame, VIDEO_WIDTH);
    mpp_frame_set_ver_stride(frame, VIDEO_HEIGHT);
    mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
    mpp_frame_set_pts(frame, pts * 1000);
    mpp_frame_set_buffer(frame, ctx->raw);
    if (checked(mpp_packet_init_with_buffer(&ctx->packet, ctx->encoded), "init JPEG packet")) {
        return -1;
    }
    mpp_packet_set_length(ctx->packet, 0);
    int ret = checked(mpp_meta_set_packet(mpp_frame_get_meta(frame), KEY_OUTPUT_PACKET,
                                          ctx->packet), "attach JPEG packet");
    if (!ret)
        ret = checked(ctx->api->encode_put_frame(ctx->encoder, frame), "submit frame");
    /* A timed-out submission may still own this frame until encoder teardown. */
    if (ret)
        return -1;
    MppPacket packet = NULL;
    if (checked(ctx->api->encode_get_packet(ctx->encoder, &packet), "receive JPEG"))
        return -1;
    if (!packet || packet != ctx->packet) {
        AOSL_LOG_ERR("camera: unexpected output packet");
        if (packet)
            mpp_packet_deinit(&packet);
        return -1;
    }
    const unsigned char *data = mpp_packet_get_pos(packet);
    size_t length = mpp_packet_get_length(packet);
    bool valid = data && length >= 4 && length <= MYBOT_VIDEO_MAX_FRAME_BYTES &&
        length <= VIDEO_RAW_BYTES && data[0] == 0xff && data[1] == 0xd8 &&
        data[length - 2] == 0xff && data[length - 1] == 0xd9 &&
        (!mpp_packet_is_partition(packet) || mpp_packet_is_eoi(packet));
    if (valid && !is_stopping(ctx)) {
        if (!ctx->snapshot_attempted) {
            ctx->snapshot_attempted = true;
            (void)save_snapshot(data, length, "/userdata/botstation-camera.jpg");
        }
        int64_t now = monotonic_ms();
        if (now < 0)
            valid = false;
        else {
            pthread_mutex_lock(&ctx->mutex);
            uint32_t bps = ctx->target_bps;
            pthread_mutex_unlock(&ctx->mutex);
            ctx->encoded_bytes += length;
            ++ctx->encoded_frames;
            if (budget_allows(ctx, now, length, bps)) {
                mybot_video_frame_t encoded = {data, length, MYBOT_VIDEO_CODEC_JPEG};
                bool first = ctx->last_send_ms < 0;
                ctx->last_send_ms = now;
                ctx->last_send_bytes = length;
                if (ctx->handler(&encoded, ctx->user_data))
                    AOSL_LOG_WRN("camera: SDK rejected JPEG");
                else {
                    ctx->submitted_bytes += length;
                    ++ctx->submitted_frames;
                    if (first)
                        AOSL_LOG_NTC("camera: first JPEG accepted by SDK (%zu bytes, 640x480, max %d fps)",
                                     length, VIDEO_FPS);
                }
            } else
                ++ctx->budget_drops;
            log_video_stats(ctx, now, bps);
        }
    } else if (!valid) {
        AOSL_LOG_ERR("camera: encoder returned an invalid/incomplete JPEG (%zu bytes)", length);
    }
    mpp_frame_deinit(&ctx->frame);
    mpp_packet_deinit(&ctx->packet);
    return valid ? 0 : -1;
}

/* Only the worker owns hardware; stop joins it before retrying failed cleanup. */
static int release_pipeline(video_context_t *ctx) {
    if (ctx->streaming) {
        if (capture_ioctl(ctx, VIDIOC_STREAMOFF, &ctx->type, "stop capture"))
            return -1;
        ctx->streaming = false;
    }
    if (ctx->encoder) {
        if (checked(mpp_destroy(ctx->encoder), "destroy encoder"))
            return -1;
        ctx->encoder = NULL;
    }
    if (ctx->frame && checked(mpp_frame_deinit(&ctx->frame), "release raw frame"))
        return -1;
    if (ctx->packet && checked(mpp_packet_deinit(&ctx->packet), "release JPEG packet"))
        return -1;
    if (ctx->cfg) {
        if (checked(mpp_enc_cfg_deinit(ctx->cfg), "release encoder config"))
            return -1;
        ctx->cfg = NULL;
    }
    if (ctx->raw) {
        if (checked(mpp_buffer_put(ctx->raw), "release raw buffer"))
            return -1;
        ctx->raw = NULL;
    }
    if (ctx->encoded) {
        if (checked(mpp_buffer_put(ctx->encoded), "release encoded buffer"))
            return -1;
        ctx->encoded = NULL;
    }
    if (ctx->group) {
        if (checked(mpp_buffer_group_put(ctx->group), "release buffer group"))
            return -1;
        ctx->group = NULL;
    }
    for (unsigned int i = 0; i < ctx->buffer_count; ++i) {
        if (ctx->buffers[i].address) {
            if (munmap(ctx->buffers[i].address, ctx->buffers[i].length)) {
                AOSL_LOG_ERR("camera: munmap: %s", strerror(errno));
                return -1;
            }
            ctx->buffers[i].address = NULL;
        }
    }
    ctx->buffer_count = 0;
    if (ctx->buffers_requested) {
        struct v4l2_requestbuffers request = {.type = ctx->type, .memory = V4L2_MEMORY_MMAP};
        if (capture_ioctl(ctx, VIDIOC_REQBUFS, &request, "release capture buffers"))
            return -1;
        ctx->buffers_requested = false;
    }
    if (ctx->fd >= 0) {
        int fd = ctx->fd;
        ctx->fd = -1;
        if (close(fd)) {
            AOSL_LOG_ERR("camera: close: %s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static void *video_worker(void *arg) {
    video_context_t *ctx = arg;
    if (prepare_capture(ctx) || is_stopping(ctx) || prepare_encoder(ctx))
        goto done;
    if (capture_ioctl(ctx, VIDIOC_STREAMON, &ctx->type, "start capture"))
        goto done;
    ctx->streaming = true;
    int64_t last_frame = -1, last_received = monotonic_ms();
    uint32_t applied_bps = 0;
    ctx->stats_start_ms = last_received;
    while (!is_stopping(ctx) && last_received >= 0) {
        struct pollfd fd = {.fd = ctx->fd, .events = POLLIN};
        int ret = poll(&fd, 1, 200);
        if (ret < 0 && errno == EINTR)
            continue;
        if (ret < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            AOSL_LOG_ERR("camera: capture poll failed ret=%d revents=%#x errno=%d",
                         ret, fd.revents, errno);
            break;
        }
        int64_t now = monotonic_ms();
        if (now < 0)
            break;
        if (!ret) {
            if (now - last_received >= 5000) {
                AOSL_LOG_ERR("camera: no frames for 5 seconds; check sensor/media links/3A service");
                break;
            }
            continue;
        }
        struct v4l2_buffer buffer;
        struct v4l2_plane plane;
        buffer_args(ctx, &buffer, &plane);
        if (camera_ioctl(ctx->fd, VIDIOC_DQBUF, &buffer)) {
            if (errno == EAGAIN && now - last_received < 5000)
                continue;
            AOSL_LOG_ERR("camera: dequeue: %s", strerror(errno));
            break;
        }
        last_received = now;
        bool selected = last_frame < 0 || now - last_frame >= (1000 + VIDEO_FPS - 1) / VIDEO_FPS;
        ret = selected ? copy_nv12(ctx, &buffer, &plane) : 0;
        if (capture_ioctl(ctx, VIDIOC_QBUF, &buffer, "requeue capture buffer") || ret)
            break;
        if (!selected || is_stopping(ctx))
            continue;
        pthread_mutex_lock(&ctx->mutex);
        uint32_t bps = ctx->target_bps;
        pthread_mutex_unlock(&ctx->mutex);
        if (bps != applied_bps) {
            if (apply_bitrate(ctx, bps))
                break;
            applied_bps = bps;
        }
        if (encode_frame(ctx, now))
            break;
        last_frame = now;
    }
done:
    release_pipeline(ctx);
    return NULL;
}

static int video_init(void **out, mybot_video_frame_handler_t handler, void *user_data) {
    if (!out || !handler) {
        AOSL_LOG_ERR("camera: invalid source initialization");
        return -1;
    }
    *out = NULL;
    video_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        AOSL_LOG_ERR("camera: context allocation failed");
        return -1;
    }
    int ret = pthread_mutex_init(&ctx->mutex, NULL);
    if (ret) {
        AOSL_LOG_ERR("camera: mutex init failed (%d)", ret);
        free(ctx);
        return -1;
    }
    ctx->fd = -1;
    ctx->handler = handler;
    ctx->user_data = user_data;
    ctx->target_bps = (VIDEO_MIN_BPS + VIDEO_MAX_BPS) / 2;
    ctx->last_send_ms = -1;
    *out = ctx;
    return 0;
}

static int video_start(void *arg) {
    video_context_t *ctx = arg;
    if (ctx->thread_started)
        return 0;
    if (release_pipeline(ctx))
        return -1;
    ctx->last_send_ms = -1;
    ctx->last_send_bytes = 0;
    ctx->snapshot_attempted = false;
    ctx->encoded_bytes = ctx->submitted_bytes = 0;
    ctx->encoded_frames = ctx->submitted_frames = ctx->budget_drops = 0;
    pthread_mutex_lock(&ctx->mutex);
    ctx->stopping = false;
    pthread_mutex_unlock(&ctx->mutex);
    int ret = pthread_create(&ctx->thread, NULL, video_worker, ctx);
    if (ret) {
        AOSL_LOG_ERR("camera: worker creation failed (%d)", ret);
        return -1;
    }
    ctx->thread_started = true;
    return 0;
}

static int video_stop(void *arg) {
    video_context_t *ctx = arg;
    pthread_mutex_lock(&ctx->mutex);
    ctx->stopping = true;
    pthread_mutex_unlock(&ctx->mutex);
    if (ctx->thread_started) {
        int ret = pthread_join(ctx->thread, NULL);
        if (ret) {
            AOSL_LOG_ERR("camera: worker join failed (%d)", ret);
            return -1;
        }
        ctx->thread_started = false;
    }
    return release_pipeline(ctx);
}

static void video_bitrate(void *arg, uint32_t bps) {
    video_context_t *ctx = arg;
    if (bps > VIDEO_MAX_BPS)
        bps = VIDEO_MAX_BPS;
    pthread_mutex_lock(&ctx->mutex);
    ctx->target_bps = bps;
    pthread_mutex_unlock(&ctx->mutex);
}

static void video_destroy(void *arg) {
    video_context_t *ctx = arg;
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx);
}

const mybot_video_ops_t *rv1126b_video_ops(void) {
    static const mybot_video_ops_t ops = {
        .min_bps = VIDEO_MIN_BPS, .max_bps = VIDEO_MAX_BPS,
        .init = video_init, .start = video_start, .stop = video_stop,
        .on_target_bitrate_changed = video_bitrate, .destroy = video_destroy,
    };
    return &ops;
}
