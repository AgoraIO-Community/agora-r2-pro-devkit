/* SPDX-License-Identifier: Apache-2.0 */
#include "input_logic.h"
#include <linux/input.h>
#include <string.h>

static int talk(mybot_state_t state, mybot_key_event_t *event) {
    if (!event)
        return 0;
    if (state == MYBOT_STATE_READY) {
        *event = MYBOT_KEY_EVENT_CONVERSATION_START;
        return 1;
    }
    if (state == MYBOT_STATE_IN_CONVERSATION) {
        *event = MYBOT_KEY_EVENT_CONVERSATION_STOP;
        return 1;
    }
    return 0;
}

int rv1126b_key_translate(unsigned code, unsigned talk_key, mybot_state_t state,
                          mybot_key_event_t *event) {
    if (!event)
        return 0;
    if (code == talk_key)
        return talk(state, event);
    if (code == KEY_VOLUMEUP || code == KEY_RIGHT) {
        *event = MYBOT_KEY_EVENT_VOLUME_UP;
        return 1;
    }
    if (code == KEY_VOLUMEDOWN || code == KEY_LEFT) {
        *event = MYBOT_KEY_EVENT_VOLUME_DOWN;
        return 1;
    }
    return 0;
}

void rv1126b_touch_configure(rv1126b_touch_t *touch, int min_x, int max_x, int min_y, int max_y,
                             unsigned rotation) {
    if (!touch)
        return;
    touch->min_x = min_x;
    touch->max_x = max_x;
    touch->min_y = min_y;
    touch->max_y = max_y;
    touch->rotation = rotation;
    touch->mapped = max_x > min_x && max_y > min_y && rotation <= 270 && rotation % 90 == 0;
}

int rv1126b_touch_position(const rv1126b_touch_t *touch, int raw_x, int raw_y, int *x, int *y) {
    if (!touch || !x || !y || !touch->mapped || raw_x < touch->min_x || raw_x > touch->max_x ||
        raw_y < touch->min_y || raw_y > touch->max_y)
        return 0;
    int width = touch->rotation % 180 ? 480 : 640;
    int height = touch->rotation % 180 ? 640 : 480;
    int px = (int)(((int64_t)raw_x - touch->min_x) * (width - 1) /
                   ((int64_t)touch->max_x - touch->min_x));
    int py = (int)(((int64_t)raw_y - touch->min_y) * (height - 1) /
                   ((int64_t)touch->max_y - touch->min_y));
    switch (touch->rotation) {
    case 90:
        *x = py;
        *y = 479 - px;
        break;
    case 180:
        *x = 639 - px;
        *y = 479 - py;
        break;
    case 270:
        *x = 639 - py;
        *y = px;
        break;
    default:
        *x = px;
        *y = py;
        break;
    }
    return 1;
}

void rv1126b_touch_settings_page(rv1126b_touch_t *touch, mybot_settings_page_t page) {
    if (!touch || touch->page == page)
        return;
    touch->page = page;
    if (touch->down)
        touch->blocked = true;
}

static int hit_control(mybot_settings_page_t page, int x, int y) {
    static const mybot_settings_control_t menu[] = {MYBOT_SETTINGS_WIFI, MYBOT_SETTINGS_POWEROFF,
                                                    MYBOT_SETTINGS_BACK};
    static const mybot_settings_control_t confirm[] = {MYBOT_SETTINGS_CANCEL,
                                                       MYBOT_SETTINGS_CONFIRM};
    if (page == MYBOT_SETTINGS_CLOSED)
        return mybot_settings_contains(MYBOT_SETTINGS_GEAR, x, y) ? MYBOT_SETTINGS_GEAR : -1;
    const mybot_settings_control_t *controls = menu;
    size_t count = sizeof(menu) / sizeof(menu[0]);
    if (page == MYBOT_SETTINGS_CONFIRM_POWEROFF) {
        controls = confirm;
        count = sizeof(confirm) / sizeof(confirm[0]);
    } else if (page == MYBOT_SETTINGS_POWEROFF_ERROR) {
        return mybot_settings_contains(MYBOT_SETTINGS_BACK, x, y) ? MYBOT_SETTINGS_BACK : -1;
    } else if (page != MYBOT_SETTINGS_MENU) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i)
        if (mybot_settings_contains(controls[i], x, y))
            return controls[i];
    return -1;
}

static void settings_click(rv1126b_touch_t *touch, int control) {
    mybot_settings_page_t page = touch->page;
    switch (page) {
    case MYBOT_SETTINGS_CLOSED:
        if (control == MYBOT_SETTINGS_GEAR)
            page = MYBOT_SETTINGS_MENU;
        break;
    case MYBOT_SETTINGS_MENU:
        if (control == MYBOT_SETTINGS_WIFI) {
            touch->wifi_pending = true;
            page = MYBOT_SETTINGS_CLOSED;
        } else if (control == MYBOT_SETTINGS_POWEROFF) {
            page = MYBOT_SETTINGS_CONFIRM_POWEROFF;
        } else {
            page = MYBOT_SETTINGS_CLOSED;
        }
        break;
    case MYBOT_SETTINGS_CONFIRM_POWEROFF:
        if (control == MYBOT_SETTINGS_CONFIRM) {
            touch->poweroff_pending = true;
            page = MYBOT_SETTINGS_POWERING_OFF;
        } else {
            page = MYBOT_SETTINGS_MENU;
        }
        break;
    case MYBOT_SETTINGS_POWEROFF_ERROR:
        page = MYBOT_SETTINGS_MENU;
        break;
    case MYBOT_SETTINGS_POWERING_OFF:
        break;
    }
    if (touch->page != page) {
        touch->page = page;
        touch->page_changed = true;
    }
}

static int current_position(rv1126b_touch_t *touch, int *x, int *y) {
    if (touch->mt_seen) {
        for (size_t i = 0; i < sizeof(touch->slots) / sizeof(touch->slots[0]); ++i) {
            rv1126b_touch_slot_t *slot = &touch->slots[i];
            if (slot->active)
                return slot->x_valid && slot->y_valid &&
                       rv1126b_touch_position(touch, slot->x, slot->y, x, y);
        }
        /* Release reports retain this gesture's last contact coordinates. */
        int index = touch->down ? touch->contact_slot : touch->slot;
        if (index >= 0 && index < 16) {
            rv1126b_touch_slot_t *slot = &touch->slots[index];
            if (slot->x_valid && slot->y_valid)
                return rv1126b_touch_position(touch, slot->x, slot->y, x, y);
        }
    }
    return touch->single_x_valid && touch->single_y_valid &&
           rv1126b_touch_position(touch, touch->single_x, touch->single_y, x, y);
}

static void reset_after_drop(rv1126b_touch_t *touch) {
    rv1126b_touch_t saved = *touch;
    memset(touch, 0, sizeof(*touch));
    rv1126b_touch_configure(touch, saved.min_x, saved.max_x, saved.min_y, saved.max_y,
                            saved.rotation);
    touch->page = saved.page;
    touch->dropped = true;
    touch->await_release = true;
}

int rv1126b_touch_event(rv1126b_touch_t *touch, unsigned type, unsigned code, int value,
                        uint32_t now, mybot_state_t state, mybot_key_event_t *event) {
    if (!touch)
        return 0;
    if (type == EV_SYN && code == SYN_DROPPED) {
        reset_after_drop(touch);
        return 0;
    }
    if (touch->dropped) {
        if ((type == EV_KEY && code == BTN_TOUCH && value == 0) ||
            (type == EV_ABS && code == ABS_MT_TRACKING_ID && value < 0))
            touch->release_seen = true;
        if (type == EV_SYN && code == SYN_REPORT) {
            touch->dropped = false;
            touch->await_release = !touch->release_seen;
        }
        return 0;
    }
    if (type == EV_KEY && code == BTN_TOUCH) {
        touch->button_seen = true;
        touch->button_down = value != 0;
        touch->release_seen = touch->release_seen || value == 0;
    } else if (type == EV_ABS && code == ABS_MT_SLOT) {
        touch->slot = value >= 0 && value < 16 ? value : -1;
        if (touch->slot < 0)
            touch->invalid_contact = true;
    } else if (type == EV_ABS && code == ABS_MT_TRACKING_ID && touch->slot >= 0) {
        rv1126b_touch_slot_t *slot = &touch->slots[touch->slot];
        if (value >= 0 && slot->active && slot->tracking_id != value)
            touch->invalid_contact = true;
        /* Protocol B retains slot coordinates across contacts. Linux filters
         * unchanged ABS values, so a new tracking ID may contain only one axis
         * or neither. New slots and dropped-event resets remain uninitialized. */
        slot->active = value >= 0;
        slot->tracking_id = value;
        if (slot->active)
            for (int i = 0; i < 16; ++i)
                if (i != touch->slot && touch->slots[i].active)
                    touch->invalid_contact = true;
        touch->release_seen = touch->release_seen || value < 0;
    } else if (type == EV_ABS && (code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y) &&
               touch->slot >= 0) {
        touch->mt_seen = true;
        rv1126b_touch_slot_t *slot = &touch->slots[touch->slot];
        if (code == ABS_MT_POSITION_X) {
            slot->x = value;
            slot->x_valid = true;
        } else {
            slot->y = value;
            slot->y_valid = true;
        }
    } else if (type == EV_ABS && code == ABS_X) {
        touch->single_x = value;
        touch->single_x_valid = true;
    } else if (type == EV_ABS && code == ABS_Y) {
        touch->single_y = value;
        touch->single_y_valid = true;
    } else if (type == EV_SYN && code == SYN_REPORT) {
        unsigned contacts = 0;
        for (size_t i = 0; i < sizeof(touch->slots) / sizeof(touch->slots[0]); ++i)
            contacts += touch->slots[i].active;
        bool down = touch->button_seen ? touch->button_down : contacts != 0;
        if (touch->await_release) {
            if (!down && touch->release_seen)
                touch->await_release = false;
            return 0;
        }
        int x = 0, y = 0;
        bool located = current_position(touch, &x, &y);
        if (down && !touch->down) {
            touch->pressed_at = now;
            touch->long_sent = false;
            touch->long_pending = false;
            touch->blocked = touch->invalid_contact;
            touch->pressed_x = x;
            touch->pressed_y = y;
            touch->contact_slot = touch->slot;
            for (int i = 0; i < 16; ++i)
                if (touch->slots[i].active) {
                    touch->contact_slot = i;
                    break;
                }
            touch->pressed_control = located ? hit_control(touch->page, x, y) : -1;
            touch->settings_owned = touch->page != MYBOT_SETTINGS_CLOSED ||
                                    touch->pressed_control == MYBOT_SETTINGS_GEAR;
            if (touch->settings_owned && !located)
                touch->blocked = true;
        }
        if (contacts > 1 || touch->invalid_contact || (touch->mapped && !located))
            touch->blocked = true;
        if (!down)
            touch->invalid_contact = false;
        if (touch->settings_owned && located &&
            (x - touch->pressed_x > 24 || touch->pressed_x - x > 24 || y - touch->pressed_y > 24 ||
             touch->pressed_y - y > 24))
            touch->blocked = true;
        bool released = !down && touch->down;
        touch->down = down;
        uint32_t duration = now - touch->pressed_at;
        if (!released || touch->blocked)
            return 0;
        if (touch->settings_owned) {
            if (duration >= 50 && duration < 1000 && located &&
                hit_control(touch->page, x, y) == touch->pressed_control)
                settings_click(touch, touch->pressed_control);
            return 0;
        }
        if (duration >= 3000 && !touch->long_sent)
            touch->long_pending = true;
        if (duration >= 50 && duration < 1000)
            return talk(state, event);
    }
    return 0;
}

int rv1126b_touch_provisioning_request(rv1126b_touch_t *touch, uint32_t now) {
    if (!touch)
        return 0;
    if (touch->wifi_pending) {
        touch->wifi_pending = false;
        return 1;
    }
    if (touch->dropped || touch->await_release || touch->long_sent || touch->settings_owned ||
        touch->blocked)
        return 0;
    if (touch->long_pending || (touch->down && (uint32_t)(now - touch->pressed_at) >= 3000)) {
        touch->long_pending = false;
        touch->long_sent = true;
        return 1;
    }
    return 0;
}
