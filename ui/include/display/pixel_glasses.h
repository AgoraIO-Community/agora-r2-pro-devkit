/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_PIXEL_GLASSES_H_
#define MYBOT_PIXEL_GLASSES_H_

#include "display/pixel_expression.h"
#include <mybot/platform/mybot_lcd.h>
#include <mybot_platform/display_status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called only by the display owner with the LVGL port locked. All inputs are
 * borrowed for this call; the view copies the strings it retains. */
void mybot_pixel_glasses_update(const mybot_lcd_content_t *content, const char *provisioning_ssid,
                                const mybot_display_runtime_status_t *status);
int mybot_pixel_glasses_show_expression(mybot_expression_t expression);
int mybot_pixel_glasses_replay_expression(mybot_expression_t expression);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_PIXEL_GLASSES_H_ */
