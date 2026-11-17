/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_LVGL_VIEW_H_
#define MYBOT_LVGL_VIEW_H_

#include "lvgl.h"
#include <mybot/platform/mybot_lcd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One UI worker serializes every call; the view creates no threads. The
 * display and its active screen outlive the view. Create returns 0/-1 and owns
 * one LVGL timer, removed before widgets on destroy. Update borrows content for
 * this call and copies retained text. Destroy is safe after partial creation. */
int mybot_lvgl_view_create(lv_display_t *display);

int mybot_lvgl_view_create_sized(lv_display_t *display, int width, int height);

void mybot_lvgl_view_update(const mybot_lcd_content_t *content);

void mybot_lvgl_view_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_LVGL_VIEW_H_ */
