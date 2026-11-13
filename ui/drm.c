/* SPDX-License-Identifier: Apache-2.0 */
/* Resource management adapted from agora-r2-pro-devkit's generic DRM backend. */
#define _POSIX_C_SOURCE 200809L
#include "drm.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static struct {
    int fd;
    uint32_t connector, crtc, handle, framebuffer, pitch, width, height;
    unsigned rotation;
    size_t size;
    void *mapping;
    drmModeCrtc *saved_crtc;
    uint32_t *saved_connectors;
    int saved_count;
    bool modeset;
} s_drm = {.fd = -1};

static uint32_t find_crtc(const drmModeRes *resources, const drmModeConnector *connector) {
    for (int i = 0; i < connector->count_encoders; ++i) {
        drmModeEncoder *encoder = drmModeGetEncoder(s_drm.fd, connector->encoders[i]);
        if (!encoder)
            continue;
        uint32_t crtc = encoder->crtc_id;
        if (!crtc) {
            for (int j = 0; j < resources->count_crtcs && j < 32; ++j) {
                if (encoder->possible_crtcs & (UINT32_C(1) << j)) {
                    crtc = resources->crtcs[j];
                    break;
                }
            }
        }
        drmModeFreeEncoder(encoder);
        if (crtc)
            return crtc;
    }
    return 0;
}

static int select_mode(const drmModeRes *resources, drmModeModeInfo *mode) {
    unsigned width = s_drm.rotation % 180 ? 480 : 640;
    unsigned height = s_drm.rotation % 180 ? 640 : 480;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < resources->count_connectors; ++i) {
            drmModeConnector *connector = drmModeGetConnector(s_drm.fd, resources->connectors[i]);
            if (!connector)
                continue;
            uint32_t crtc = find_crtc(resources, connector);
            int selected = -1;
            if (connector->connection == DRM_MODE_CONNECTED && crtc &&
                (pass || connector->connector_type == DRM_MODE_CONNECTOR_DSI)) {
                for (int j = 0; j < connector->count_modes; ++j) {
                    if (connector->modes[j].hdisplay == width &&
                        connector->modes[j].vdisplay == height) {
                        selected = j;
                        if (connector->modes[j].type & DRM_MODE_TYPE_PREFERRED)
                            break;
                    }
                }
            }
            if (selected >= 0) {
                *mode = connector->modes[selected];
                s_drm.connector = connector->connector_id;
                s_drm.crtc = crtc;
                drmModeFreeConnector(connector);
                return 0;
            }
            drmModeFreeConnector(connector);
        }
    }
    fprintf(stderr, "DRM: no connected %ux%u mode for rotation %u\n", width, height,
            s_drm.rotation);
    return -1;
}

static int save_connectors(const drmModeRes *resources) {
    if (resources->count_connectors <= 0)
        return -1;
    s_drm.saved_connectors = calloc((size_t)resources->count_connectors, sizeof(uint32_t));
    if (!s_drm.saved_connectors)
        return -1;
    for (int i = 0; i < resources->count_connectors; ++i) {
        drmModeConnector *connector = drmModeGetConnector(s_drm.fd, resources->connectors[i]);
        if (!connector)
            return -1;
        drmModeEncoder *encoder =
            connector->encoder_id ? drmModeGetEncoder(s_drm.fd, connector->encoder_id) : NULL;
        if (connector->encoder_id && !encoder) {
            drmModeFreeConnector(connector);
            return -1;
        }
        if (encoder && encoder->crtc_id == s_drm.crtc)
            s_drm.saved_connectors[s_drm.saved_count++] = connector->connector_id;
        if (encoder)
            drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
    }
    return 0;
}

int rv1126b_drm_stop(void) {
    int result = 0;
    if (s_drm.modeset && s_drm.saved_crtc) {
        drmModeCrtc *old = s_drm.saved_crtc;
        bool enabled = old->mode_valid && s_drm.saved_count > 0;
        if (drmModeSetCrtc(s_drm.fd, old->crtc_id, enabled ? old->buffer_id : 0,
                           enabled ? old->x : 0, enabled ? old->y : 0,
                           enabled ? s_drm.saved_connectors : NULL, enabled ? s_drm.saved_count : 0,
                           enabled ? &old->mode : NULL) < 0) {
            fprintf(stderr, "DRM: cannot restore CRTC: %s\n", strerror(errno));
            result = -1;
        }
    }
    if (s_drm.framebuffer && drmModeRmFB(s_drm.fd, s_drm.framebuffer) < 0)
        result = -1;
    if (s_drm.mapping && munmap(s_drm.mapping, s_drm.size) < 0)
        result = -1;
    if (s_drm.handle) {
        struct drm_mode_destroy_dumb request = {.handle = s_drm.handle};
        if (drmIoctl(s_drm.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &request) < 0)
            result = -1;
    }
    if (s_drm.saved_crtc)
        drmModeFreeCrtc(s_drm.saved_crtc);
    if (s_drm.fd >= 0 && close(s_drm.fd) < 0)
        result = -1;
    free(s_drm.saved_connectors);
    memset(&s_drm, 0, sizeof(s_drm));
    s_drm.fd = -1;
    return result;
}

int rv1126b_drm_start(const char *device, unsigned rotation) {
    if (s_drm.fd >= 0 || !device || rotation > 270 || rotation % 90)
        return -1;
    s_drm.rotation = rotation;
    s_drm.fd = open(device, O_RDWR | O_CLOEXEC);
    if (s_drm.fd < 0)
        goto fail;
    if (drmSetMaster(s_drm.fd) < 0)
        goto fail;
    drmModeRes *resources = drmModeGetResources(s_drm.fd);
    if (!resources)
        goto fail;
    drmModeModeInfo mode;
    int selected = select_mode(resources, &mode);
    int saved = selected == 0 ? save_connectors(resources) : -1;
    drmModeFreeResources(resources);
    if (selected < 0 || saved < 0)
        goto fail;
    s_drm.width = mode.hdisplay;
    s_drm.height = mode.vdisplay;
    s_drm.saved_crtc = drmModeGetCrtc(s_drm.fd, s_drm.crtc);
    if (!s_drm.saved_crtc)
        goto fail;
    struct drm_mode_create_dumb create = {.width = s_drm.width, .height = s_drm.height, .bpp = 32};
    if (drmIoctl(s_drm.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        goto fail;
    s_drm.handle = create.handle;
    if (create.size > SIZE_MAX || create.pitch < s_drm.width * sizeof(uint32_t) ||
        create.size < (uint64_t)create.pitch * s_drm.height)
        goto fail;
    s_drm.size = (size_t)create.size;
    s_drm.pitch = create.pitch;
    uint32_t handles[4] = {create.handle}, pitches[4] = {create.pitch}, offsets[4] = {0};
    if (drmModeAddFB2(s_drm.fd, s_drm.width, s_drm.height, DRM_FORMAT_XRGB8888, handles, pitches,
                      offsets, &s_drm.framebuffer, 0) < 0)
        goto fail;
    struct drm_mode_map_dumb map = {.handle = create.handle};
    if (drmIoctl(s_drm.fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
        goto fail;
    void *mapping =
        mmap(NULL, s_drm.size, PROT_READ | PROT_WRITE, MAP_SHARED, s_drm.fd, map.offset);
    if (mapping == MAP_FAILED)
        goto fail;
    s_drm.mapping = mapping;
    memset(mapping, 0, s_drm.size);
    if (drmModeSetCrtc(s_drm.fd, s_drm.crtc, s_drm.framebuffer, 0, 0, &s_drm.connector, 1, &mode) <
        0)
        goto fail;
    s_drm.modeset = true;
    fprintf(stderr, "DRM: %s %ux%u, 640x480 native rendering, rotation=%u\n", device, s_drm.width,
            s_drm.height, rotation);
    return 0;
fail:
    fprintf(stderr,
            "DRM initialization failed: %s; stop the existing display service or use --no-ui\n",
            strerror(errno));
    (void)rv1126b_drm_stop();
    return -1;
}

int rv1126b_drm_present(const uint32_t *canvas, const rv1126b_rect_t *area, void *unused) {
    (void)unused;
    return rv1126b_pixels_blit(canvas, s_drm.mapping, s_drm.size, s_drm.pitch, s_drm.width,
                               s_drm.height, s_drm.rotation, area);
}
