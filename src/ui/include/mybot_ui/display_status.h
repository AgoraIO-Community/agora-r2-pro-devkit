/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_DISPLAY_STATUS_H_
#define MYBOT_UI_DISPLAY_STATUS_H_

#include <stdbool.h>

#define MYBOT_DISPLAY_STATE_CAPACITY 32

typedef struct {
    char state[MYBOT_DISPLAY_STATE_CAPACITY];
    bool in_conversation;
    char provisioning_ssid[33];
    /* Product-owned setup failure; renderer shows fixed, local retry guidance. */
    bool wifi_failed;
    /* Product startup clock gate: 0 inactive, 1 waiting, 2 failed. */
    int clock_state;
} mybot_display_runtime_status_t;

#endif /* MYBOT_UI_DISPLAY_STATUS_H_ */
