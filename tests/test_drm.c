/* SPDX-License-Identifier: Apache-2.0 */
/* Fault injection adapted from the RV1126B reference. No device is opened. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
int test_open(const char *path, int flags, ...);
int test_close(int fd);
void *test_mmap(void *address, size_t length, int prot, int flags, int fd, off_t offset);
int test_munmap(void *address, size_t length);
#define open test_open
#define close test_close
#define mmap test_mmap
#define munmap test_munmap
#define drmSetMaster test_master
#define drmIoctl test_ioctl
#define drmModeGetResources test_resources
#define drmModeFreeResources test_free_resources
#define drmModeGetConnector test_connector
#define drmModeFreeConnector test_free_connector
#define drmModeGetEncoder test_encoder
#define drmModeFreeEncoder test_free_encoder
#define drmModeGetCrtc test_crtc
#define drmModeFreeCrtc test_free_crtc
#define drmModeAddFB2 test_add_fb
#define drmModeRmFB test_rm_fb
#define drmModeSetCrtc test_set_crtc
#include "../ui/drm.c"
#include "check.h"

enum fault {
    NONE,
    OPEN,
    MASTER,
    RESOURCES,
    CONNECTOR,
    ENCODER,
    CRTC,
    CREATE,
    ADD_FB,
    MAP,
    MMAP,
    MODESET
};
static enum fault fault;
static int fds, objects, handles, buffers, maps, restores, outputs;
static uint32_t connectors[] = {10, 11}, crtcs[] = {20};
static drmModeModeInfo mode = {.hdisplay = 480, .vdisplay = 640, .type = DRM_MODE_TYPE_PREFERRED};
#define PITCH (480U * 4U + 16U)
#define SIZE (PITCH * 640U)

int test_open(const char *path, int flags, ...) {
    (void)flags;
    CHECK(!strcmp(path, "mock"));
    if (fault == OPEN)
        return -1;
    ++fds;
    return 42;
}
int test_close(int fd) {
    CHECK(fd == 42 && fds == 1);
    --fds;
    return 0;
}
int test_master(int fd) {
    CHECK(fd == 42);
    return fault == MASTER ? -1 : 0;
}
static void *object(size_t size) {
    void *p = calloc(1, size);
    CHECK(p);
    ++objects;
    return p;
}
static void release(void *p) {
    CHECK(p && objects > 0);
    --objects;
    free(p);
}
drmModeResPtr test_resources(int fd) {
    CHECK(fd == 42);
    if (fault == RESOURCES)
        return NULL;
    drmModeRes *p = object(sizeof(*p));
    p->count_connectors = 2;
    p->connectors = connectors;
    p->count_crtcs = 1;
    p->crtcs = crtcs;
    return p;
}
void test_free_resources(drmModeResPtr p) {
    release(p);
}
drmModeConnectorPtr test_connector(int fd, uint32_t id) {
    CHECK(fd == 42);
    if (fault == CONNECTOR)
        return NULL;
    drmModeConnector *p = object(sizeof(*p));
    p->connector_id = id;
    p->connection = DRM_MODE_CONNECTED;
    p->connector_type = DRM_MODE_CONNECTOR_DSI;
    p->count_modes = 1;
    p->modes = &mode;
    p->count_encoders = 1;
    p->encoders = crtcs;
    p->encoder_id = outputs && (id == 10 || outputs == 2) ? 20 : 0;
    return p;
}
void test_free_connector(drmModeConnectorPtr p) {
    release(p);
}
drmModeEncoderPtr test_encoder(int fd, uint32_t id) {
    CHECK(fd == 42 && id == 20);
    if (fault == ENCODER)
        return NULL;
    drmModeEncoder *p = object(sizeof(*p));
    p->crtc_id = 20;
    p->possible_crtcs = 1;
    return p;
}
void test_free_encoder(drmModeEncoderPtr p) {
    release(p);
}
drmModeCrtcPtr test_crtc(int fd, uint32_t id) {
    CHECK(fd == 42 && id == 20);
    if (fault == CRTC)
        return NULL;
    drmModeCrtc *p = object(sizeof(*p));
    p->crtc_id = 20;
    p->buffer_id = 55;
    p->mode_valid = outputs != 0;
    return p;
}
void test_free_crtc(drmModeCrtcPtr p) {
    release(p);
}
int test_ioctl(int fd, unsigned long request, void *arg) {
    CHECK(fd == 42);
    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        if (fault == CREATE)
            return -1;
        struct drm_mode_create_dumb *p = arg;
        p->handle = 7;
        p->pitch = PITCH;
        p->size = SIZE;
        ++handles;
    } else if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        if (fault == MAP)
            return -1;
        ((struct drm_mode_map_dumb *)arg)->offset = 0;
    } else if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        CHECK(handles == 1 && ((struct drm_mode_destroy_dumb *)arg)->handle == 7);
        --handles;
    } else
        CHECK(0);
    return 0;
}
int test_add_fb(int fd, uint32_t width, uint32_t height, uint32_t format, const uint32_t h[4],
                const uint32_t pitches[4], const uint32_t offsets[4], uint32_t *id,
                uint32_t flags) {
    (void)offsets;
    (void)flags;
    CHECK(fd == 42 && width == 480 && height == 640 && format == DRM_FORMAT_XRGB8888);
    CHECK(h[0] == 7 && pitches[0] == PITCH);
    if (fault == ADD_FB)
        return -1;
    *id = 123;
    ++buffers;
    return 0;
}
int test_rm_fb(int fd, uint32_t id) {
    CHECK(fd == 42 && id == 123 && buffers == 1);
    --buffers;
    return 0;
}
void *test_mmap(void *address, size_t size, int prot, int flags, int fd, off_t offset) {
    (void)address;
    (void)prot;
    (void)flags;
    (void)offset;
    CHECK(fd == 42 && size == SIZE);
    if (fault == MMAP)
        return MAP_FAILED;
    ++maps;
    return malloc(size);
}
int test_munmap(void *address, size_t size) {
    CHECK(maps == 1 && size == SIZE);
    --maps;
    free(address);
    return 0;
}
int test_set_crtc(int fd, uint32_t crtc, uint32_t fb, uint32_t x, uint32_t y, uint32_t *ids,
                  int count, drmModeModeInfoPtr selected) {
    (void)x;
    (void)y;
    CHECK(fd == 42 && crtc == 20);
    if (fb == 123) {
        CHECK(count == 1 && ids[0] == 10 && selected);
        return fault == MODESET ? -1 : 0;
    }
    CHECK(fb == (outputs ? 55U : 0U) && count == outputs);
    if (count) {
        CHECK(ids[0] == 10 && selected);
        if (count == 2)
            CHECK(ids[1] == 11);
    } else
        CHECK(!ids && !selected);
    ++restores;
    return 0;
}
static void released(void) {
    CHECK(!fds && !objects && !handles && !buffers && !maps && s_drm.fd == -1);
}
int main(void) {
    for (fault = OPEN; fault <= MODESET; ++fault) {
        outputs = 2;
        restores = 0;
        CHECK(rv1126b_drm_start("mock", 270) < 0);
        released();
        CHECK(restores == 0);
        CHECK(rv1126b_drm_stop() == 0);
        released();
    }
    fault = NONE;
    for (outputs = 0; outputs <= 2; ++outputs) {
        restores = 0;
        CHECK(rv1126b_drm_start("mock", 270) == 0);
        CHECK(rv1126b_drm_start("mock", 270) < 0);
        uint32_t *canvas = calloc(RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT, sizeof(uint32_t));
        CHECK(canvas);
        canvas[0] = 0xabcdef;
        const rv1126b_rect_t area = {0, 0, 0, 0};
        CHECK(rv1126b_drm_present(canvas, &area, NULL) == 0);
        uint32_t *row = (uint32_t *)((unsigned char *)s_drm.mapping + 639 * PITCH);
        CHECK(row[0] == 0xabcdef && row[1] == 0 && row[2] == 0);
        CHECK(rv1126b_drm_stop() == 0);
        CHECK(restores == 1);
        released();
        free(canvas);
    }
    puts("DRM failure cleanup, padded framebuffer and original CRTC/output restoration: PASS");
    return 0;
}
