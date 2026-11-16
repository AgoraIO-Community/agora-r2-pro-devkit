/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_INPUT_LOGIC_H
#define RV1126B_INPUT_LOGIC_H
#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>

typedef struct {
    bool button_seen, button_down, down, dropped;
    bool long_sent, long_pending;
    bool slots[16];
    int slot;
    uint32_t pressed_at;
} rv1126b_touch_t;
/* Returns one SDK semantic event at most; provisioning is a separate product request. */
int rv1126b_key_translate(unsigned code, unsigned talk_key, mybot_state_t state,
                          mybot_key_event_t *event);
int rv1126b_touch_event(rv1126b_touch_t *touch, unsigned type, unsigned code, int value,
                        uint32_t now, mybot_state_t state, mybot_key_event_t *event);
/* Product-level provisioning gesture, deliberately separate from SDK key events. */
int rv1126b_touch_provisioning_request(rv1126b_touch_t *touch, uint32_t now);
#endif
