/* SPDX-License-Identifier: Apache-2.0 */
#include "input_logic.h"
#include "check.h"
#include <linux/input.h>
#include <string.h>

static int touch(rv1126b_touch_t *input, unsigned type, unsigned code, int value, uint32_t now,
                 mybot_key_event_t *event) {
    return rv1126b_touch_event(input, type, code, value, now, MYBOT_STATE_READY, event);
}

static void click(rv1126b_touch_t *input, int x, int y, uint32_t now) {
    mybot_key_event_t event;
    CHECK(!touch(input, EV_ABS, ABS_X, x, now, &event));
    CHECK(!touch(input, EV_ABS, ABS_Y, y, now, &event));
    CHECK(!touch(input, EV_KEY, BTN_TOUCH, 1, now, &event));
    CHECK(!touch(input, EV_SYN, SYN_REPORT, 0, now, &event));
    CHECK(!touch(input, EV_KEY, BTN_TOUCH, 0, now + 100, &event));
    CHECK(!touch(input, EV_SYN, SYN_REPORT, 0, now + 100, &event));
}

static void check_positions(void) {
    rv1126b_touch_t input = {0};
    int x, y;
    const unsigned rotations[] = {0, 90, 180, 270};
    const int first_x[] = {0, 0, 639, 639};
    const int first_y[] = {0, 479, 479, 0};
    for (size_t i = 0; i < sizeof(rotations) / sizeof(rotations[0]); ++i) {
        rv1126b_touch_configure(&input, 10, 490, -10, 630, rotations[i]);
        CHECK(rv1126b_touch_position(&input, 10, -10, &x, &y));
        CHECK(x == first_x[i] && y == first_y[i]);
        CHECK(rv1126b_touch_position(&input, 490, 630, &x, &y));
        CHECK(x == 639 - first_x[i] && y == 479 - first_y[i]);
        CHECK(!rv1126b_touch_position(&input, 491, 630, &x, &y));
        CHECK(!rv1126b_touch_position(&input, 10, -11, &x, &y));
    }
    rv1126b_touch_configure(&input, 0, 480, 0, 640, 270);
    CHECK(rv1126b_touch_position(&input, 44, 40, &x, &y));
    CHECK(mybot_settings_contains(MYBOT_SETTINGS_GEAR, x, y));
    rv1126b_touch_configure(&input, 0, 0, 0, 640, 270);
    CHECK(!rv1126b_touch_position(&input, 0, 0, &x, &y));
}

static void check_settings(void) {
    rv1126b_touch_t input = {0};
    rv1126b_touch_configure(&input, 0, 639, 0, 479, 0);
    click(&input, 596, 44, 100);
    CHECK(input.page == MYBOT_SETTINGS_MENU && input.page_changed);
    CHECK(!rv1126b_touch_wifi_requested(&input));
    click(&input, 200, 270, 1000);
    CHECK(input.page == MYBOT_SETTINGS_CONFIRM_POWEROFF && !input.poweroff_pending);
    click(&input, 200, 330, 2000);
    CHECK(input.page == MYBOT_SETTINGS_MENU && !input.poweroff_pending);
    click(&input, 200, 270, 3000);
    click(&input, 400, 330, 4000);
    CHECK(input.page == MYBOT_SETTINGS_POWERING_OFF && input.poweroff_pending);
    input.poweroff_pending = false;
    click(&input, 596, 44, 5000);
    CHECK(input.page == MYBOT_SETTINGS_POWERING_OFF && !input.poweroff_pending);
    rv1126b_touch_settings_page(&input, MYBOT_SETTINGS_POWEROFF_ERROR);
    mybot_settings_rect_t back = mybot_settings_bounds(MYBOT_SETTINGS_BACK);
    click(&input, back.x + back.width / 2, back.y + back.height / 2, 6000);
    CHECK(input.page == MYBOT_SETTINGS_MENU);
    click(&input, 200, 180, 7000);
    CHECK(input.page == MYBOT_SETTINGS_CLOSED);
    CHECK(rv1126b_touch_wifi_requested(&input));
    CHECK(!rv1126b_touch_wifi_requested(&input));
    click(&input, 596, 44, 8000);
    click(&input, 20, 20, 9000);
    CHECK(input.page == MYBOT_SETTINGS_CLOSED);

    mybot_key_event_t event;
    /* Holding the gear cannot enter AP setup or dispatch a conversation. */
    CHECK(!touch(&input, EV_ABS, ABS_X, 596, 10000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_Y, 44, 10000, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 10000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 10000, &event));
    CHECK(!rv1126b_touch_wifi_requested(&input));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 14000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 14000, &event));
    CHECK(input.page == MYBOT_SETTINGS_CLOSED);
    /* Invalid coordinates must not click a retained button position. */
    click(&input, 640, 44, 15000);
    CHECK(input.page == MYBOT_SETTINGS_CLOSED);
    CHECK(!rv1126b_touch_wifi_requested(&input));

    /* Without UI coordinate mapping the gear region remains a normal talk tap. */
    memset(&input, 0, sizeof(input));
    CHECK(!touch(&input, EV_ABS, ABS_X, 596, 21000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_Y, 44, 21000, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 21000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 21000, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 21100, &event));
    CHECK(touch(&input, EV_SYN, SYN_REPORT, 0, 21100, &event));
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_START && input.page == MYBOT_SETTINGS_CLOSED);
}

static void check_multitouch(void) {
    rv1126b_touch_t input = {0};
    rv1126b_touch_configure(&input, 0, 639, 0, 479, 0);
    mybot_key_event_t event;
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, 1, 100, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_X, 596, 100, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_Y, 44, 100, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 100, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_SLOT, 1, 120, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, 2, 120, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_X, 600, 120, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_Y, 40, 120, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 120, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, -1, 150, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_SLOT, 0, 150, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, -1, 150, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 200, &event));
    CHECK(input.page == MYBOT_SETTINGS_CLOSED);
    CHECK(!rv1126b_touch_wifi_requested(&input));
    /* The next single contact may use a different slot, without stale coordinates. */
    CHECK(!touch(&input, EV_ABS, ABS_MT_SLOT, 3, 6000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, 3, 6000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_X, 596, 6000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_POSITION_Y, 44, 6000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 6000, &event));
    CHECK(!touch(&input, EV_ABS, ABS_MT_TRACKING_ID, -1, 6100, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 6100, &event));
    CHECK(input.page == MYBOT_SETTINGS_MENU);
    /* Dropped frames cancel a confirmation, and held contacts cannot create a click. */
    rv1126b_touch_settings_page(&input, MYBOT_SETTINGS_CONFIRM_POWEROFF);
    CHECK(!touch(&input, EV_SYN, SYN_DROPPED, 0, 7000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 7001, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 7002, &event));
    CHECK(!touch(&input, EV_ABS, ABS_X, 400, 7002, &event));
    CHECK(!touch(&input, EV_ABS, ABS_Y, 330, 7002, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 7002, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 7100, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 7100, &event));
    CHECK(!input.poweroff_pending && input.page == MYBOT_SETTINGS_CONFIRM_POWEROFF);
}

static void check_ignored_holds(void) {
    const uint32_t durations[] = {1000, 2999, 3000, 10000};
    for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); ++i) {
        rv1126b_touch_t input = {0};
        mybot_key_event_t event;
        rv1126b_touch_configure(&input, 0, 639, 0, 479, 0);
        CHECK(!touch(&input, EV_ABS, ABS_X, 200, 100, &event));
        CHECK(!touch(&input, EV_ABS, ABS_Y, 180, 100, &event));
        CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 100, &event));
        CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 100, &event));
        CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 100 + durations[i], &event));
        CHECK(!rv1126b_touch_wifi_requested(&input));
        /* Movement and repeated reports during a long contact stay inert. */
        CHECK(!touch(&input, EV_ABS, ABS_X, 596, 100 + durations[i], &event));
        CHECK(!touch(&input, EV_ABS, ABS_Y, 44, 100 + durations[i], &event));
        CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 100 + durations[i], &event));
        CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 100 + durations[i], &event));
        CHECK(!rv1126b_touch_wifi_requested(&input));
        CHECK(input.page == MYBOT_SETTINGS_CLOSED);

        rv1126b_touch_settings_page(&input, MYBOT_SETTINGS_MENU);
        CHECK(!touch(&input, EV_ABS, ABS_X, 200, 20000, &event));
        CHECK(!touch(&input, EV_ABS, ABS_Y, 180, 20000, &event));
        CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 20000, &event));
        CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 20000, &event));
        CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 20000 + durations[i], &event));
        CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 20000 + durations[i], &event));
        CHECK(!rv1126b_touch_wifi_requested(&input));
        CHECK(input.page == MYBOT_SETTINGS_MENU);
    }
    rv1126b_touch_t input = {0};
    mybot_key_event_t event;
    rv1126b_touch_configure(&input, 0, 639, 0, 479, 0);
    rv1126b_touch_settings_page(&input, MYBOT_SETTINGS_MENU);
    CHECK(!touch(&input, EV_ABS, ABS_X, 200, 100, &event));
    CHECK(!touch(&input, EV_ABS, ABS_Y, 180, 100, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 100, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 100, &event));
    CHECK(!touch(&input, EV_ABS, ABS_X, 300, 150, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 200, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 200, &event));
    CHECK(!rv1126b_touch_wifi_requested(&input));
    CHECK(input.page == MYBOT_SETTINGS_MENU);
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 1, 1000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 1000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_DROPPED, 0, 1100, &event));
    CHECK(!touch(&input, EV_KEY, BTN_TOUCH, 0, 5000, &event));
    CHECK(!touch(&input, EV_SYN, SYN_REPORT, 0, 5000, &event));
    CHECK(!rv1126b_touch_wifi_requested(&input));
    CHECK(input.page == MYBOT_SETTINGS_MENU);
    click(&input, 200, 180, 6000);
    CHECK(rv1126b_touch_wifi_requested(&input));
    CHECK(!rv1126b_touch_wifi_requested(&input));
}

int main(void) {
    check_positions();
    check_settings();
    check_multitouch();
    check_ignored_holds();
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
    memset(&state, 0, sizeof(state));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 1, 100, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 100, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 3100, &event));
    CHECK(!rv1126b_touch_wifi_requested(&state));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 0, 3300, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 3300, &event));
    CHECK(!rv1126b_touch_wifi_requested(&state));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 1, 4000, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 4000, &event));
    CHECK(!touch(&state, EV_KEY, BTN_TOUCH, 0, 7100, &event));
    CHECK(!touch(&state, EV_SYN, SYN_REPORT, 0, 7100, &event));
    CHECK(!rv1126b_touch_wifi_requested(&state));
    CHECK(!touch(&state, EV_SYN, SYN_DROPPED, 0, 8000, &event));
    CHECK(!rv1126b_touch_wifi_requested(&state));
    puts("Input state gating, touch boundaries, duplicate edges and dropped events: PASS");
    return 0;
}
