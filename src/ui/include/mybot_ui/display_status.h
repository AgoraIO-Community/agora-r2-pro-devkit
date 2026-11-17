/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_DISPLAY_STATUS_H_
#define MYBOT_UI_DISPLAY_STATUS_H_

#include <stdbool.h>

#define MYBOT_DISPLAY_STATE_CAPACITY 32

typedef enum {
    MYBOT_DISPLAY_CLOCK_INACTIVE,
    MYBOT_DISPLAY_CLOCK_WAITING,
    MYBOT_DISPLAY_CLOCK_FAILED
} mybot_display_clock_state_t;

typedef struct {
    char state[MYBOT_DISPLAY_STATE_CAPACITY];
    bool in_conversation;
    char provisioning_ssid[33];
    /* Product-owned setup failure; renderer shows fixed, local retry guidance. */
    bool wifi_failed;
    /* Product startup gate; readiness does not assert NTP synchronization. */
    mybot_display_clock_state_t clock_state;
} mybot_display_runtime_status_t;

#endif /* MYBOT_UI_DISPLAY_STATUS_H_ */
