/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOTSTATION_RV1126B_LVGL_UI_H
#define BOTSTATION_RV1126B_LVGL_UI_H

#include <mybot/platform/mybot_lcd.h>

typedef enum {
    RV1126B_UI_WAIT_NONE,
    RV1126B_UI_WAIT_NETWORK,
    RV1126B_UI_WAIT_TIME,
    RV1126B_UI_WAIT_SERVICES,
} rv1126b_ui_wait_t;

int rv1126b_lvgl_ui_start(void);
void rv1126b_lvgl_ui_set_wait(rv1126b_ui_wait_t stage);
void rv1126b_lvgl_ui_stop(void);
const mybot_lcd_ops_t *rv1126b_lvgl_lcd_ops(void);

#endif
