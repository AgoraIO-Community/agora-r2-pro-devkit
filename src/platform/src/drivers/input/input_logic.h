/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_INPUT_LOGIC_H
#define RV1126B_INPUT_LOGIC_H

#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>
#include <mybot_ui/settings.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active, x_valid, y_valid;
    int x, y, tracking_id;
} rv1126b_touch_slot_t;

typedef struct {
    bool button_seen, button_down, down, dropped, await_release, release_seen;
    bool long_sent, long_pending, blocked, settings_owned, page_changed;
    bool single_x_valid, single_y_valid, mt_seen, mapped, invalid_contact;
    bool wifi_pending, poweroff_pending;
    int single_x, single_y, slot, pressed_control, contact_slot;
    int min_x, max_x, min_y, max_y, pressed_x, pressed_y;
    unsigned rotation;
    uint32_t pressed_at;
    rv1126b_touch_slot_t slots[16];
    mybot_settings_page_t page;
} rv1126b_touch_t;

/* Returns one SDK semantic event at most; product requests remain separate. */
int rv1126b_key_translate(unsigned code, unsigned talk_key, mybot_state_t state,
                          mybot_key_event_t *event);

void rv1126b_touch_configure(rv1126b_touch_t *touch, int min_x, int max_x, int min_y, int max_y,
                             unsigned rotation);

/* Converts raw physical panel coordinates into the renderer's 640x480 space. */
int rv1126b_touch_position(const rv1126b_touch_t *touch, int raw_x, int raw_y, int *x, int *y);

void rv1126b_touch_settings_page(rv1126b_touch_t *touch, mybot_settings_page_t page);

int rv1126b_touch_event(rv1126b_touch_t *touch, unsigned type, unsigned code, int value,
                        uint32_t now, mybot_state_t state, mybot_key_event_t *event);

/* Product-level provisioning gesture, deliberately separate from SDK key events. */
int rv1126b_touch_provisioning_request(rv1126b_touch_t *touch, uint32_t now);

#ifdef __cplusplus
}
#endif

#endif
