/* SPDX-License-Identifier: Apache-2.0 */
#include "pixels.h"

int rv1126b_pixels_blit(const uint32_t *canvas, void *destination, size_t size, size_t pitch,
                        unsigned width, unsigned height, unsigned rotation,
                        const rv1126b_rect_t *area) {
    const unsigned logical_width = RV1126B_UI_WIDTH;
    const unsigned logical_height = RV1126B_UI_HEIGHT;
    if (!canvas || !destination || !area || area->x1 < 0 || area->y1 < 0 || area->x2 < area->x1 ||
        area->y2 < area->y1 || area->x2 >= RV1126B_UI_WIDTH || area->y2 >= RV1126B_UI_HEIGHT ||
        rotation % 90 || rotation > 270 ||
        width != (rotation % 180 ? logical_height : logical_width) ||
        height != (rotation % 180 ? logical_width : logical_height) ||
        pitch < width * sizeof(uint32_t) || pitch % sizeof(uint32_t) || pitch > SIZE_MAX / height ||
        size < pitch * height) {
        return -1;
    }
    /* Keep framebuffer stores adjacent within each physical row. The mmap may
     * use write-combining memory, so column-wise stores are especially costly. */
    if (rotation == 90 || rotation == 270) {
        for (int x = area->x1; x <= area->x2; ++x) {
            unsigned py = rotation == 90 ? (unsigned)x : logical_width - 1 - (unsigned)x;
            uint32_t *row = (uint32_t *)((unsigned char *)destination + py * pitch);
            for (int y = area->y1; y <= area->y2; ++y) {
                unsigned px = rotation == 90 ? logical_height - 1 - (unsigned)y : (unsigned)y;
                row[px] = canvas[(size_t)y * RV1126B_UI_WIDTH + (size_t)x];
            }
        }
    } else {
        for (int y = area->y1; y <= area->y2; ++y) {
            unsigned py = rotation == 180 ? logical_height - 1 - (unsigned)y : (unsigned)y;
            uint32_t *row = (uint32_t *)((unsigned char *)destination + py * pitch);
            for (int x = area->x1; x <= area->x2; ++x) {
                unsigned px = rotation == 180 ? logical_width - 1 - (unsigned)x : (unsigned)x;
                row[px] = canvas[(size_t)y * RV1126B_UI_WIDTH + (size_t)x];
            }
        }
    }
    return 0;
}
