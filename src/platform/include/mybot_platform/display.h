/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_H
#define RV1126B_UI_H
#include <stdbool.h>
#include <mybot/mybot.h>
#include <mybot/platform/mybot_lcd.h>
#include <mybot_ui/settings.h>
#include <mybot_ui/display_status.h>

#ifdef __cplusplus
extern "C" {
#endif
/* One process-wide display service. Start/stop belong to the application owner;
 * status updates may come from SDK threads and are copied under the worker lock.
 * Only the display worker calls LVGL or owns DRM resources. */
/* Borrows drm_device until stop. Waits for startup; returns 0/-1. */
int rv1126b_ui_start(const char *drm_device, unsigned rotation);

/* Joins the worker and restores saved outputs. Repeated stop is safe; 0/-1. */
int rv1126b_ui_stop(void);

/* Joins the worker and disables its CRTC for accepted system poweroff. Repeated
 * stop is safe; returns 0/-1 and preserves genuine display cleanup failures. */
int rv1126b_ui_stop_for_poweroff(void);

void rv1126b_ui_set_state(mybot_state_t state);

/* Copies bounded content during this call; caller keeps ownership. */
void rv1126b_ui_show(const mybot_lcd_content_t *content);

/* Show actual AP guidance only after the portal has started. NULL clears it. */
void rv1126b_ui_provisioning(const char *ssid);

/* Copies a setup failure flag; settings remain available for an explicit retry. */
void rv1126b_ui_wifi_failed(bool failed);

/* Copies the typed product startup clock gate.
 * Readiness means the supported clock range, not proof of NTP synchronization.
 * Invalid values are ignored. */
void rv1126b_ui_time_status(mybot_display_clock_state_t status);

/* Copies a product-owned menu state; only the display worker touches LVGL. */
void rv1126b_ui_settings(mybot_settings_page_t page);

/* Selects an existing expression; -1 restores the state-driven policy. */
void rv1126b_ui_demo_expression(int expression);

/* Thread-safe query of startup/render/cleanup failure. */
int rv1126b_ui_failed(void);

/* Returns an immutable, process-lifetime SDK LCD operations table. */
const mybot_lcd_ops_t *rv1126b_ui_lcd_ops(void);

#ifdef __cplusplus
}
#endif

#endif
