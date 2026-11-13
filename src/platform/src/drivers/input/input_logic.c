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

int rv1126b_touch_event(rv1126b_touch_t *touch, unsigned type, unsigned code, int value,
                        uint32_t now, mybot_state_t state, mybot_key_event_t *event) {
    if (!touch)
        return 0;
    if (type == EV_SYN && code == SYN_DROPPED) {
        memset(touch, 0, sizeof(*touch));
        touch->dropped = true;
        return 0;
    }
    if (touch->dropped) {
        if (type == EV_SYN && code == SYN_REPORT)
            touch->dropped = false;
        return 0;
    }
    if (type == EV_KEY && code == BTN_TOUCH) {
        touch->button_seen = true;
        touch->button_down = value != 0;
    } else if (type == EV_ABS && code == ABS_MT_SLOT) {
        touch->slot = value >= 0 && value < 16 ? value : -1;
    } else if (type == EV_ABS && code == ABS_MT_TRACKING_ID && touch->slot >= 0) {
        touch->slots[touch->slot] = value >= 0;
    } else if (type == EV_SYN && code == SYN_REPORT) {
        bool down = touch->button_down;
        if (!touch->button_seen) {
            for (size_t i = 0; i < sizeof(touch->slots) / sizeof(touch->slots[0]); ++i)
                down = down || touch->slots[i];
        }
        if (down && !touch->down)
            touch->pressed_at = now;
        bool released = !down && touch->down;
        touch->down = down;
        uint32_t duration = now - touch->pressed_at;
        if (released && duration >= 50 && duration < 1000)
            return talk(state, event);
    }
    return 0;
}
