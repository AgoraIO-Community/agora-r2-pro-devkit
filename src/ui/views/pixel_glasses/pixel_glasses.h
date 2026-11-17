/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_PIXEL_GLASSES_H_
#define MYBOT_PIXEL_GLASSES_H_

#include "mybot_ui/expression.h"
#include <mybot/platform/mybot_lcd.h>
#include <mybot_ui/display_status.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called only by the UI worker that owns the view; calls are serialized.
 * Inputs are borrowed for this call; retained text is copied. No SDK state is
 * changed. Expression selection returns 0/-1 for accepted/invalid requests. */
void mybot_pixel_glasses_update(const mybot_lcd_content_t *content, const char *provisioning_ssid,
                                const mybot_display_runtime_status_t *status);

int mybot_pixel_glasses_show_expression(mybot_expression_t expression);

int mybot_pixel_glasses_replay_expression(mybot_expression_t expression);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_PIXEL_GLASSES_H_ */
