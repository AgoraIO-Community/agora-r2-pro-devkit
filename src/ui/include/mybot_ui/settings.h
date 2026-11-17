/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_SETTINGS_H
#define MYBOT_UI_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MYBOT_SETTINGS_CLOSED,
    MYBOT_SETTINGS_MENU,
    MYBOT_SETTINGS_CONFIRM_POWEROFF,
    MYBOT_SETTINGS_POWERING_OFF,
    MYBOT_SETTINGS_POWEROFF_ERROR
} mybot_settings_page_t;

typedef enum {
    MYBOT_SETTINGS_GEAR,
    MYBOT_SETTINGS_WIFI,
    MYBOT_SETTINGS_POWEROFF,
    MYBOT_SETTINGS_BACK,
    MYBOT_SETTINGS_CANCEL,
    MYBOT_SETTINGS_CONFIRM
} mybot_settings_control_t;

typedef struct {
    int x, y, width, height;
} mybot_settings_rect_t;

/* One geometry contract for native 640x480 rendering and touch hit testing. */
static inline mybot_settings_rect_t mybot_settings_bounds(mybot_settings_control_t control) {
    static const mybot_settings_rect_t rectangles[] = {{568, 16, 56, 56},   {112, 152, 416, 72},
                                                       {112, 240, 416, 72}, {16, 16, 128, 56},
                                                       {96, 300, 208, 64},  {336, 300, 208, 64}};
    const mybot_settings_rect_t empty = {0, 0, 0, 0};
    return (unsigned)control < sizeof(rectangles) / sizeof(rectangles[0]) ? rectangles[control]
                                                                          : empty;
}

static inline bool mybot_settings_contains(mybot_settings_control_t control, int x, int y) {
    mybot_settings_rect_t rectangle = mybot_settings_bounds(control);
    return x >= rectangle.x && y >= rectangle.y && x < rectangle.x + rectangle.width &&
           y < rectangle.y + rectangle.height;
}

#ifdef __cplusplus
}
#endif

#endif
