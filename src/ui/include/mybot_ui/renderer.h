/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_RENDERER_H
#define RV1126B_UI_RENDERER_H
#include "mybot_ui/canvas.h"
#include "mybot_ui/expression.h"
#include "mybot_ui/settings.h"
#include <mybot/platform/mybot_lcd.h>
#include <mybot_ui/display_status.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*rv1126b_present_fn)(const uint32_t *canvas, const rv1126b_rect_t *area, void *user);

/* One process-wide view. Every function runs on its single owner thread.
 * Create returns 0/-1; present/user are borrowed until destroy. Content/status
 * updates are copied. The canvas is borrowed until destroy and cannot be freed
 * or modified by callers. Present consumes its borrowed rectangle/canvas during
 * the call; failures are exposed by failed(). Destroy tolerates partial create. */
int rv1126b_renderer_create(rv1126b_present_fn present, void *user, uint32_t now);

void rv1126b_renderer_update(const mybot_lcd_content_t *content,
                             const mybot_display_runtime_status_t *status, uint32_t now);

void rv1126b_renderer_settings(mybot_settings_page_t page);

void rv1126b_renderer_step(uint32_t now);

int rv1126b_renderer_expression(int expression);

const uint32_t *rv1126b_renderer_canvas(void);

int rv1126b_renderer_failed(void);

void rv1126b_renderer_destroy(void);

#ifdef __cplusplus
}
#endif

#endif
