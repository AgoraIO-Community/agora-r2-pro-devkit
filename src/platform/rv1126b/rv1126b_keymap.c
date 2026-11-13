/* SPDX-License-Identifier: Apache-2.0 */
#include "rv1126b_platform.h"

#include <linux/input-event-codes.h>
#include <stddef.h>

int rv1126b_key_translate(unsigned int code, unsigned int talk_key, mybot_state_t state,
                          mybot_key_event_t *event) {
    if (!event) {
        return 0;
    }
    if (code == talk_key) {
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
    if (code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) {
        *event = code == KEY_VOLUMEUP ? MYBOT_KEY_EVENT_VOLUME_UP : MYBOT_KEY_EVENT_VOLUME_DOWN;
        return 1;
    }
    return 0;
}
