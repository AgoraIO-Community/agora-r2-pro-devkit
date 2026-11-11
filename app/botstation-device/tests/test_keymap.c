/* SPDX-License-Identifier: Apache-2.0 */
#include "rv1126b_platform.h"

#include <linux/input-event-codes.h>
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void) {
    const mybot_state_t states[] = {
        MYBOT_STATE_STOPPED, MYBOT_STATE_WIFI_PROVISIONING, MYBOT_STATE_STARTING_SERVICES,
        MYBOT_STATE_READY, MYBOT_STATE_WIFI_DISCONNECTED, MYBOT_STATE_FAILED,
        MYBOT_STATE_STOPPING, MYBOT_STATE_IN_CONVERSATION, MYBOT_STATE_PAIRING,
    };
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        mybot_key_event_t event = MYBOT_KEY_EVENT_PAIR;
        int mapped = rv1126b_key_translate(KEY_PLAYPAUSE, KEY_PLAYPAUSE, states[i], &event);
        if (states[i] == MYBOT_STATE_READY) {
            CHECK(mapped == 1 && event == MYBOT_KEY_EVENT_CONVERSATION_START);
        } else if (states[i] == MYBOT_STATE_IN_CONVERSATION) {
            CHECK(mapped == 1 && event == MYBOT_KEY_EVENT_CONVERSATION_STOP);
        } else {
            CHECK(mapped == 0 && event == MYBOT_KEY_EVENT_PAIR);
        }
    }
    mybot_key_event_t event = MYBOT_KEY_EVENT_PAIR;
    CHECK(rv1126b_key_translate(KEY_VOLUMEUP, KEY_PLAYPAUSE, MYBOT_STATE_READY, &event) == 1);
    CHECK(event == MYBOT_KEY_EVENT_VOLUME_UP);
    CHECK(rv1126b_key_translate(KEY_VOLUMEDOWN, KEY_PLAYPAUSE, MYBOT_STATE_READY, &event) == 1);
    CHECK(event == MYBOT_KEY_EVENT_VOLUME_DOWN);
    CHECK(rv1126b_key_translate(KEY_A, KEY_PLAYPAUSE, MYBOT_STATE_READY, &event) == 0);
    CHECK(rv1126b_key_translate(KEY_PLAYPAUSE, KEY_PLAYPAUSE, MYBOT_STATE_READY, NULL) == 0);
    CHECK(rv1126b_key_translate(KEY_F1, KEY_F1, MYBOT_STATE_READY, &event) == 1);
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_START);
    puts("BotStation key mapping tests passed");
    return 0;
}
