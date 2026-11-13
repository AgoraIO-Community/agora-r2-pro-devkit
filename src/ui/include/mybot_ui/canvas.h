/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_CANVAS_H
#define RV1126B_UI_CANVAS_H

#include <stddef.h>
#include <stdint.h>

/* Native logical canvas. Physical rotation belongs to the display backend. */
#define RV1126B_UI_WIDTH 640
#define RV1126B_UI_HEIGHT 480

typedef struct {
    int x1, y1, x2, y2;
} rv1126b_rect_t;

#endif
