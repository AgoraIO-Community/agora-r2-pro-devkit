/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#define mpp_buffer_get_ptr_with_caller test_buffer_ptr
#define mpp_buffer_sync_begin_f test_sync_begin
#define mpp_buffer_sync_end_f test_sync_end
#define mpp_packet_init_with_buffer test_packet_init
#define ioctl test_ioctl
#include "rv1126b_video.c"

static unsigned char raw[VIDEO_RAW_BYTES];
static int streamoff_failures, sync_failure;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool entered, allow_return, stop_entered, stop_done;
static int stop_result;
static unsigned char jpeg[] = {0xff, 0xd8, 1, 2, 0xff, 0xd9};
static MppPacket output_packet;
static unsigned int frames_received;
static bool invalid_jpeg, submission_failure;

MPP_RET test_packet_init(MppPacket *packet, MppBuffer buffer) {
    (void)buffer;
    return mpp_packet_init(packet, jpeg, sizeof(jpeg));
}

static MPP_RET put_frame(MppCtx ctx, MppFrame frame) {
    (void)ctx;
    assert(mpp_frame_get_width(frame) == VIDEO_WIDTH);
    assert(mpp_frame_get_height(frame) == VIDEO_HEIGHT);
    assert(mpp_frame_get_fmt(frame) == MPP_FMT_YUV420SP);
    assert(mpp_meta_get_packet(mpp_frame_get_meta(frame), KEY_OUTPUT_PACKET, &output_packet) ==
           MPP_OK);
    return submission_failure ? MPP_ERR_TIMEOUT : MPP_OK;
}

static MPP_RET get_packet(MppCtx ctx, MppPacket *packet) {
    (void)ctx;
    mpp_packet_set_length(output_packet, invalid_jpeg ? sizeof(jpeg) - 1 : sizeof(jpeg));
    *packet = output_packet;
    return MPP_OK;
}

static int count_frame(const mybot_video_frame_t *frame, void *data) {
    (void)data;
    assert(frame->codec == MYBOT_VIDEO_CODEC_JPEG);
    assert(frame->len == sizeof(jpeg));
    assert(memcmp(frame->data, jpeg, sizeof(jpeg)) == 0);
    ++frames_received;
    return 0;
}

void *test_buffer_ptr(MppBuffer buffer, const char *caller) {
    (void)buffer;
    (void)caller;
    return raw;
}

MPP_RET test_sync_begin(MppBuffer buffer, RK_S32 ro, const char *caller) {
    (void)buffer;
    (void)ro;
    (void)caller;
    return sync_failure ? MPP_NOK : MPP_OK;
}

MPP_RET test_sync_end(MppBuffer buffer, RK_S32 ro, const char *caller) {
    return test_sync_begin(buffer, ro, caller);
}

int test_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    assert(request == VIDIOC_STREAMOFF);
    if (streamoff_failures) {
        --streamoff_failures;
        errno = EIO;
        return -1;
    }
    return 0;
}

static int handler(const mybot_video_frame_t *frame, void *data) {
    (void)frame;
    (void)data;
    pthread_mutex_lock(&gate);
    entered = true;
    pthread_cond_broadcast(&changed);
    while (!allow_return)
        pthread_cond_wait(&changed, &gate);
    pthread_mutex_unlock(&gate);
    return 0;
}

static void wait_for(bool *flag) {
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;
    while (!*flag)
        assert(pthread_cond_timedwait(&changed, &gate, &deadline) == 0);
}

static void *fake_worker(void *arg) {
    video_context_t *ctx = arg;
    mybot_video_frame_t frame = {raw, sizeof(raw), MYBOT_VIDEO_CODEC_JPEG};
    ctx->handler(&frame, ctx->user_data);
    return NULL;
}

static void *stop_worker(void *arg) {
    pthread_mutex_lock(&gate);
    stop_entered = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    int result = video_stop(arg);
    pthread_mutex_lock(&gate);
    stop_result = result;
    stop_done = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    return NULL;
}

int main(void) {
    void *opaque = NULL;
    assert(video_init(NULL, handler, NULL) == -1);
    assert(video_init(&opaque, NULL, NULL) == -1);
    assert(video_init(&opaque, handler, NULL) == 0);
    video_context_t *ctx = opaque;
    assert(ctx->fd == -1 && !ctx->thread_started);
    video_bitrate(ctx, 0);
    assert(ctx->target_bps == 0);
    video_bitrate(ctx, 50000);
    assert(ctx->target_bps == 50000);
    video_bitrate(ctx, UINT32_MAX);
    assert(ctx->target_bps == VIDEO_MAX_BPS);
    assert(video_stop(ctx) == 0);

    assert(!budget_allows(ctx, 0, 1000, 0));
    assert(budget_allows(ctx, 0, 10000, 100000));
    assert(!budget_allows(ctx, 0, 10001, 100000));
    ctx->last_send_ms = 0;
    ctx->last_send_bytes = 1000;
    assert(!budget_allows(ctx, 99, 1000, 100000));
    assert(budget_allows(ctx, 100, 1000, 100000));
    assert(!budget_allows(ctx, 199, 1000, 50000));
    assert(budget_allows(ctx, 200, 1000, 50000));
    assert(!budget_allows(ctx, 0, 1, 1));
    /* No credit accumulates while idle, and reductions immediately stretch spacing. */
    ctx->last_send_ms = 10000;
    assert(!budget_allows(ctx, 10099, 1000, 100000));
    ctx->last_send_ms = -1;

    ctx->pitch = VIDEO_WIDTH + 32;
    size_t bytes = (size_t)ctx->pitch * VIDEO_HEIGHT * 3 / 2;
    unsigned char *source = malloc(bytes + 16);
    assert(source);
    for (unsigned int row = 0; row < VIDEO_HEIGHT * 3 / 2; ++row) {
        memset(source + 16 + (size_t)row * ctx->pitch, row & 255, VIDEO_WIDTH);
        memset(source + 16 + (size_t)row * ctx->pitch + VIDEO_WIDTH, 0xcc, 32);
    }
    ctx->buffer_count = 1;
    ctx->buffers[0].address = source;
    ctx->buffers[0].length = bytes + 16;
    ctx->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    struct v4l2_buffer buffer;
    struct v4l2_plane plane;
    buffer_args(ctx, &buffer, &plane);
    assert(buffer.m.planes == &plane && buffer.length == 1);
    plane.bytesused = bytes + 16;
    plane.data_offset = 16;
    assert(copy_nv12(ctx, &buffer, &plane) == 0);
    for (unsigned int row = 0; row < VIDEO_HEIGHT * 3 / 2; ++row)
        for (unsigned int x = 0; x < VIDEO_WIDTH; ++x)
            assert(raw[(size_t)row * VIDEO_WIDTH + x] == (row & 255));
    plane.bytesused--;
    assert(copy_nv12(ctx, &buffer, &plane) == -1);
    plane.bytesused = bytes + 16;
    plane.data_offset = bytes + 17;
    assert(copy_nv12(ctx, &buffer, &plane) == -1);
    plane.data_offset = 16;
    buffer.index = 1;
    assert(copy_nv12(ctx, &buffer, &plane) == -1);
    buffer.index = 0;
    buffer.flags = V4L2_BUF_FLAG_ERROR;
    assert(copy_nv12(ctx, &buffer, &plane) == -1);
    buffer.flags = 0;
    sync_failure = 1;
    assert(copy_nv12(ctx, &buffer, &plane) == -1);
    sync_failure = 0;
    ctx->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_args(ctx, &buffer, &plane);
    assert(buffer.m.planes == NULL);
    ctx->buffers[0].address = source + 16;
    ctx->buffers[0].length = bytes;
    buffer.bytesused = bytes;
    assert(copy_nv12(ctx, &buffer, &plane) == 0);
    ctx->buffers[0].address = NULL;
    ctx->buffer_count = 0;
    free(source);

    /* Failed cleanup preserves ownership so stop can retry. */
    streamoff_failures = 1;
    ctx->streaming = true;
    assert(video_stop(ctx) == -1 && ctx->streaming);
    assert(video_stop(ctx) == 0 && !ctx->streaming);

    MppApi api = {.encode_put_frame = put_frame, .encode_get_packet = get_packet};
    char directory[] = "/tmp/botstation-snapshot-XXXXXX";
    assert(mkdtemp(directory));
    char snapshot[256];
    assert(snprintf(snapshot, sizeof(snapshot), "%s/camera.jpg", directory) > 0);
    assert(save_snapshot(jpeg, sizeof(jpeg), snapshot) == 0);
    FILE *saved = fopen(snapshot, "rb");
    assert(saved);
    unsigned char saved_bytes[sizeof(jpeg) + 1];
    assert(fread(saved_bytes, 1, sizeof(saved_bytes), saved) == sizeof(jpeg));
    assert(memcmp(saved_bytes, jpeg, sizeof(jpeg)) == 0);
    assert(fclose(saved) == 0);
    assert(save_snapshot(jpeg, 4, snapshot) == 0);
    struct stat saved_stat;
    assert(stat(snapshot, &saved_stat) == 0 && saved_stat.st_size == 4);
    assert((saved_stat.st_mode & 0777) == 0600);
    assert(unlink(snapshot) == 0 && rmdir(directory) == 0);
    assert(save_snapshot(jpeg, sizeof(jpeg), snapshot) == -1);
    ctx->snapshot_attempted = true;
    ctx->api = &api;
    ctx->handler = count_frame;
    ctx->stopping = false;
    assert(encode_frame(ctx, 123) == 0 && frames_received == 1 && ctx->packet == NULL);
    invalid_jpeg = true;
    assert(encode_frame(ctx, 124) == -1 && frames_received == 1 && ctx->packet == NULL);
    invalid_jpeg = false;
    video_bitrate(ctx, 0);
    assert(encode_frame(ctx, 125) == 0 && frames_received == 1);
    video_bitrate(ctx, VIDEO_MAX_BPS);
    ctx->stopping = true;
    assert(encode_frame(ctx, 125) == 0 && frames_received == 1);
    submission_failure = true;
    assert(encode_frame(ctx, 126) == -1 && ctx->frame && ctx->packet);
    assert(release_pipeline(ctx) == 0 && !ctx->frame && !ctx->packet);
    submission_failure = false;
    ctx->handler = handler;

    assert(pthread_create(&ctx->thread, NULL, fake_worker, ctx) == 0);
    ctx->thread_started = true;
    pthread_mutex_lock(&gate);
    wait_for(&entered);
    pthread_mutex_unlock(&gate);
    pthread_t stopper;
    assert(pthread_create(&stopper, NULL, stop_worker, ctx) == 0);
    pthread_mutex_lock(&gate);
    wait_for(&stop_entered);
    assert(!stop_done);
    allow_return = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&gate);
    assert(pthread_join(stopper, NULL) == 0);
    assert(stop_done && stop_result == 0 && !ctx->thread_started);

    char device[256];
    assert(setenv("BOTSTATION_VIDEO_DEVICE", "/dev/botstation-test-missing", 1) == 0);
    assert(find_camera(device, sizeof(device)) == 0);
    assert(strcmp(device, "/dev/botstation-test-missing") == 0);
    assert(prepare_capture(ctx) == -1 && ctx->fd == -1);
    assert(video_stop(ctx) == 0);
    unsetenv("BOTSTATION_VIDEO_DEVICE");
    video_destroy(ctx);
    return 0;
}
