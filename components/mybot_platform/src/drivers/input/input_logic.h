/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_INPUT_LOGIC_H
#define RV1126B_INPUT_LOGIC_H
#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>

typedef struct {
    bool button_seen, button_down, down, dropped;
    bool slots[16];
    int slot;
    uint32_t pressed_at;
} rv1126b_touch_t;
/* Returns one semantic event at most. Repeated key events and long touches are ignored. */
int rv1126b_key_translate(unsigned code, unsigned talk_key, mybot_state_t state,
                          mybot_key_event_t *event);
int rv1126b_touch_event(rv1126b_touch_t *touch, unsigned type, unsigned code, int value,
                        uint32_t now, mybot_state_t state, mybot_key_event_t *event);
#endif
