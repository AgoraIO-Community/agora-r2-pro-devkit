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
    MYBOT_SETTINGS_POWEROFF_ERROR,
    MYBOT_SETTINGS_CONFIRM_RESET,
    MYBOT_SETTINGS_RESETTING,
    MYBOT_SETTINGS_RESET_ERROR
} mybot_settings_page_t;

typedef enum {
    MYBOT_SETTINGS_GEAR,
    MYBOT_SETTINGS_WIFI,
    MYBOT_SETTINGS_POWEROFF,
    MYBOT_SETTINGS_BACK,
    MYBOT_SETTINGS_CANCEL,
    MYBOT_SETTINGS_CONFIRM,
    MYBOT_SETTINGS_FACTORY_RESET,
    MYBOT_SETTINGS_RESET_CONFIRM,
    MYBOT_SETTINGS_CONTROL_COUNT
} mybot_settings_control_t;

/* Controls visible to the renderer and available for touch on each page. */
typedef struct {
    unsigned int count;
    mybot_settings_control_t controls[4];
} mybot_settings_controls_t;

static inline const mybot_settings_controls_t *mybot_settings_controls(mybot_settings_page_t page) {
    /* Indexed by mybot_settings_page_t. Progress pages accept no input. */
    static const mybot_settings_controls_t pages[] = {
        {1, {MYBOT_SETTINGS_GEAR}},
        {4,
         {MYBOT_SETTINGS_WIFI, MYBOT_SETTINGS_POWEROFF, MYBOT_SETTINGS_FACTORY_RESET,
          MYBOT_SETTINGS_BACK}},
        {2, {MYBOT_SETTINGS_CANCEL, MYBOT_SETTINGS_CONFIRM}},
        {0, {MYBOT_SETTINGS_CONTROL_COUNT}},
        {1, {MYBOT_SETTINGS_BACK}},
        {2, {MYBOT_SETTINGS_BACK, MYBOT_SETTINGS_RESET_CONFIRM}},
        {0, {MYBOT_SETTINGS_CONTROL_COUNT}},
        {1, {MYBOT_SETTINGS_BACK}},
    };
    return (unsigned)page < sizeof(pages) / sizeof(pages[0]) ? &pages[page] : 0;
}

typedef struct {
    int x, y, width, height;
} mybot_settings_rect_t;

/* One geometry contract for native 640x480 rendering and touch hit testing. */
static inline mybot_settings_rect_t mybot_settings_bounds(mybot_settings_control_t control) {
    static const mybot_settings_rect_t rectangles[MYBOT_SETTINGS_CONTROL_COUNT] = {
        {568, 16, 56, 56},   /* gear */
        {112, 152, 416, 72}, /* Wi-Fi */
        {112, 240, 416, 72}, /* power off */
        {16, 16, 128, 56},   /* back / cancel reset */
        {96, 300, 208, 64},  /* cancel power off */
        {336, 300, 208, 64}, /* confirm power off */
        {112, 328, 416, 72}, /* factory reset */
        {160, 408, 320, 64}  /* confirm reset, outside the menu entry's bounds */
    };
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
