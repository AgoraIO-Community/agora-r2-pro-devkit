/* SPDX-License-Identifier: Apache-2.0 */
#include "pixels.h"
#include "check.h"
#include <string.h>

#define SENTINEL UINT32_C(0xa7a7a7a7)
static uint32_t source_pixel(unsigned x, unsigned y) {
    return UINT32_C(0xff000000) | ((uint32_t)y << 10) | x;
}

/* Independent inverse transform: check each physical pixel against its source. */
static uint32_t expected(unsigned x, unsigned y, unsigned rotation, const rv1126b_rect_t *area) {
    unsigned sx = x, sy = y;
    switch (rotation) {
    case 90:
        sx = y;
        sy = RV1126B_UI_HEIGHT - 1 - x;
        break;
    case 180:
        sx = RV1126B_UI_WIDTH - 1 - x;
        sy = RV1126B_UI_HEIGHT - 1 - y;
        break;
    case 270:
        sx = RV1126B_UI_WIDTH - 1 - y;
        sy = x;
        break;
    }
    if ((int)sx < area->x1 || (int)sx > area->x2 || (int)sy < area->y1 || (int)sy > area->y2)
        return SENTINEL;
    return source_pixel(sx, sy);
}

static void check_area(const uint32_t *canvas, const rv1126b_rect_t *area) {
    for (unsigned rotation = 0; rotation < 360; rotation += 90) {
        unsigned width = rotation % 180 ? 480 : 640, height = rotation % 180 ? 640 : 480;
        for (unsigned padding = 0; padding <= 28; padding += 28) {
            size_t pitch = width * 4 + padding, size = pitch * height + 31;
            unsigned char *frame = malloc(size);
            CHECK(frame);
            memset(frame, 0xa7, size);
            CHECK(rv1126b_pixels_blit(canvas, frame, size, pitch, width, height, rotation, area) ==
                  0);
            size_t changed = 0;
            for (unsigned y = 0; y < height; ++y) {
                const uint32_t *row = (const uint32_t *)(frame + y * pitch);
                for (unsigned x = 0; x < width; ++x) {
                    CHECK(row[x] == expected(x, y, rotation, area));
                    changed += row[x] != SENTINEL;
                }
                for (size_t x = width * 4; x < pitch; ++x)
                    CHECK(frame[y * pitch + x] == 0xa7);
            }
            CHECK(changed == (size_t)(area->x2 - area->x1 + 1) * (size_t)(area->y2 - area->y1 + 1));
            for (size_t i = pitch * height; i < size; ++i)
                CHECK(frame[i] == 0xa7);
            free(frame);
        }
    }
}

int main(void) {
    uint32_t *canvas = malloc(RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT * sizeof(*canvas));
    CHECK(canvas);
    for (unsigned y = 0; y < RV1126B_UI_HEIGHT; ++y)
        for (unsigned x = 0; x < RV1126B_UI_WIDTH; ++x)
            canvas[y * RV1126B_UI_WIDTH + x] = source_pixel(x, y);
    const rv1126b_rect_t areas[] = {{0, 0, 639, 479}, {17, 23, 53, 41}, {0, 0, 0, 0},
                                    {639, 0, 639, 0}, {0, 479, 0, 479}, {639, 479, 639, 479}};
    for (unsigned i = 0; i < sizeof(areas) / sizeof(areas[0]); ++i)
        check_area(canvas, &areas[i]);

    size_t size = 640 * 480 * 4;
    unsigned char *frame = malloc(size);
    CHECK(frame);
    memset(frame, 0xa7, size);
    const rv1126b_rect_t bad[] = {{-1, 0, 0, 0},    {0, -1, 0, 0}, {0, 0, 640, 479},
                                  {0, 0, 639, 480}, {5, 0, 4, 0},  {0, 6, 0, 5}};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 640, 480, 0, &bad[i]) < 0);
    CHECK(rv1126b_pixels_blit(NULL, frame, size, 640 * 4, 640, 480, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, NULL, size, 640 * 4, 640, 480, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 640, 480, 0, NULL) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size - 1, 640 * 4, 640, 480, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4 - 1, 640, 480, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, SIZE_MAX, SIZE_MAX, 640, 480, 0, &areas[0]) < 0);
    const unsigned invalid_rotations[] = {45, 360, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_rotations) / sizeof(invalid_rotations[0]); ++i)
        CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 640, 480, invalid_rotations[i],
                                  &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 0, 480, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 640, 0, 0, &areas[0]) < 0);
    CHECK(rv1126b_pixels_blit(canvas, frame, size, 640 * 4, 640, 480, 90, &areas[0]) < 0);
    for (size_t i = 0; i < size; ++i)
        CHECK(frame[i] == 0xa7);
    free(frame);
    free(canvas);
    puts("Native pixels: inverse rotation, dirty rectangles, row padding and invalid calls: PASS");
    return 0;
}
