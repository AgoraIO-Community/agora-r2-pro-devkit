/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_RENDERER_H
#define RV1126B_UI_RENDERER_H
#include "pixels.h"
#include "display/pixel_expression.h"
#include <mybot/platform/mybot_lcd.h>
#include <mybot_platform/display_status.h>

typedef int (*rv1126b_present_fn)(const uint32_t *canvas, const rv1126b_rect_t *area, void *user);
/* One process-wide view. Every function is called by its single owner thread. */
int rv1126b_renderer_create(rv1126b_present_fn present, void *user, uint32_t now);
void rv1126b_renderer_update(const mybot_lcd_content_t *content,
                             const mybot_display_runtime_status_t *status, uint32_t now);
void rv1126b_renderer_step(uint32_t now);
int rv1126b_renderer_expression(int expression);
const uint32_t *rv1126b_renderer_canvas(void);
int rv1126b_renderer_failed(void);
void rv1126b_renderer_destroy(void);
#endif
