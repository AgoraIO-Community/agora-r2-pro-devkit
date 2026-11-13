/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_PIXELS_H
#define RV1126B_UI_PIXELS_H
#include <stddef.h>
#include <stdint.h>
#define RV1126B_UI_WIDTH 640
#define RV1126B_UI_HEIGHT 480
typedef struct {
    int x1, y1, x2, y2;
} rv1126b_rect_t;
/* Copies a native-resolution dirty rectangle into an XRGB8888 framebuffer.
 * Rotation is clockwise in degrees; pitch includes any destination row padding. */
int rv1126b_pixels_blit(const uint32_t *canvas, void *destination, size_t size, size_t pitch,
                        unsigned width, unsigned height, unsigned rotation,
                        const rv1126b_rect_t *area);
#endif
