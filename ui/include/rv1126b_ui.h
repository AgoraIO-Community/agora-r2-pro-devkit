/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_UI_H
#define RV1126B_UI_H
#include <mybot/mybot.h>
#include <mybot/platform/mybot_lcd.h>
int rv1126b_ui_start(const char *drm_device, unsigned rotation);
int rv1126b_ui_stop(void);
void rv1126b_ui_set_state(mybot_state_t state);
void rv1126b_ui_show(const mybot_lcd_content_t *content);
void rv1126b_ui_demo_expression(int expression);
int rv1126b_ui_failed(void);
const mybot_lcd_ops_t *rv1126b_ui_lcd_ops(void);
#endif
