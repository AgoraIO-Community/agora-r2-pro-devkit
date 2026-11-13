/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PLATFORM_DISPLAY_STATUS_H_
#define MYBOT_PLATFORM_DISPLAY_STATUS_H_

#include <stdbool.h>

#define MYBOT_DISPLAY_STATE_CAPACITY 32

typedef struct {
    char state[MYBOT_DISPLAY_STATE_CAPACITY];
    bool in_conversation;
} mybot_display_runtime_status_t;

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MYBOT_CORES3_PIXEL_UI) && MYBOT_CORES3_PIXEL_UI
/* Copies a presentation-only runtime snapshot; never reads or changes SDK state.
 * Repeated values are a no-op. Returns -1 if the display is unavailable. */
int mybot_display_set_runtime_status(const char *state, bool in_conversation);
#else
static inline int mybot_display_set_runtime_status(const char *state, bool in_conversation) {
    (void)state;
    (void)in_conversation;
    return 0;
}
#endif

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_PLATFORM_DISPLAY_STATUS_H_ */
