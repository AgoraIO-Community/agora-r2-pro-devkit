/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_H
#define RV1126B_UI_H
#include <mybot/mybot.h>
#include <mybot/platform/mybot_lcd.h>
/* One process-wide display service. Start/stop belong to the application owner;
 * status updates may come from SDK threads and are copied under the worker lock.
 * Only the display worker calls LVGL or owns DRM resources. */
/* Borrows drm_device until stop. Waits for startup; returns 0/-1. */
int rv1126b_ui_start(const char *drm_device, unsigned rotation);
/* Joins the worker and restores saved outputs. Repeated stop is safe; 0/-1. */
int rv1126b_ui_stop(void);
void rv1126b_ui_set_state(mybot_state_t state);
/* Copies bounded content during this call; caller keeps ownership. */
void rv1126b_ui_show(const mybot_lcd_content_t *content);
/* Selects an existing expression; -1 restores the state-driven policy. */
void rv1126b_ui_demo_expression(int expression);
/* Thread-safe query of startup/render/cleanup failure. */
int rv1126b_ui_failed(void);
/* Returns an immutable, process-lifetime SDK LCD operations table. */
const mybot_lcd_ops_t *rv1126b_ui_lcd_ops(void);
#endif
