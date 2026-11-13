/* SPDX-License-Identifier: Apache-2.0 */
#include "input_logic.h"
#include "check.h"
#include <linux/input.h>
#include <string.h>

static int touch(rv1126b_touch_t *input, unsigned type, unsigned code, int value, uint32_t now,
                 mybot_key_event_t *event) {
    return rv1126b_touch_event(input, type, code, value, now, MYBOT_STATE_READY, event);
}

int main(void) {
    mybot_key_event_t event;
    CHECK(rv1126b_key_translate(KEY_MENU, KEY_MENU, MYBOT_STATE_READY, &event) == 1);
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_START);
    CHECK(rv1126b_key_translate(KEY_MENU, KEY_MENU, MYBOT_STATE_IN_CONVERSATION, &event) == 1);
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    CHECK(!rv1126b_key_translate(KEY_MENU, KEY_MENU, MYBOT_STATE_PAIRING, &event));
    CHECK(!rv1126b_key_translate(KEY_ESC, KEY_MENU, MYBOT_STATE_READY, &event));
    CHECK(!rv1126b_key_translate(KEY_POWER, KEY_MENU, MYBOT_STATE_READY, &event));
    CHECK(rv1126b_key_translate(KEY_RIGHT, KEY_MENU, MYBOT_STATE_READY, &event));
    CHECK(event == MYBOT_KEY_EVENT_VOLUME_UP);
    CHECK(rv1126b_key_translate(KEY_LEFT, KEY_MENU, MYBOT_STATE_READY, &event));
    CHECK(event == MYBOT_KEY_EVENT_VOLUME_DOWN);
    for (uint32_t duration = 49; duration <= 1000; ++duration) {
        if (duration > 50 && duration < 999)
            continue;
        rv1126b_touch_t state = {0};
        CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 1, 100, &event));
        CHECK(!touch(&state, EV_ABS, ABS_MT_TRACKING_ID, 20, 100, &event));
        CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 100, &event));
        CHECK(!touch(&state, EV_ABS, ABS_MT_TRACKING_ID, -1, 100 + duration, &event));
        CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 0, 100 + duration, &event));
        CHECK(touch(&state, EV_SYN, SYN_REPORT, 0, 100 + duration, &event) ==
              (duration >= 50 && duration < 1000));
        CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 100 + duration, &event));
    }
    rv1126b_touch_t state = {0};
    CHECK(!touch(&state, EV_ABS, ABS_MT_TRACKING_ID, 42, UINT32_MAX - 20, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, UINT32_MAX - 20, &event));
    CHECK(!touch(&state, EV_ABS, ABS_MT_TRACKING_ID, -1, 50, &event));
    CHECK(touch(&state, EV_SYN, SYN_REPORT, 0, 50, &event));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 1, 100, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 100, &event));
    CHECK(!touch(&state, EV_SYN, SYN_DROPPED, 0, 110, &event));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 0, 200, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 200, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 210, &event));
    puts("Input state gating, touch boundaries, duplicate edges and dropped events: PASS");
    return 0;
}
