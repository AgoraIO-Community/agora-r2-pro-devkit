/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_EXPRESSION_POLICY_H_
#define MYBOT_EXPRESSION_POLICY_H_

#include "display/pixel_expression.h"
#include <mybot/platform/mybot_lcd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Display policy only. The LCD snapshot lock serializes all accesses; no SDK
 * lifecycle, RTM parsing, hardware control, allocation or timer is owned here. */
typedef enum {
    MYBOT_EXPRESSION_SOURCE_STATE = 0,
    MYBOT_EXPRESSION_SOURCE_OPENING,
} mybot_expression_source_t;

typedef struct {
    mybot_lcd_screen_t screen;
    uint32_t phase;
    uint32_t phase_since;
    mybot_expression_source_t source;
    mybot_expression_t foreground;
    uint32_t foreground_since;
    bool foreground_pending;
} mybot_expression_policy_t;

static inline void mybot_expression_policy_init(mybot_expression_policy_t *policy) {
    memset(policy, 0, sizeof(*policy));
    policy->screen = MYBOT_LCD_SCREEN_STARTING;
}

static inline bool mybot_expression_policy_clear(mybot_expression_policy_t *policy) {
    bool changed = policy->source != MYBOT_EXPRESSION_SOURCE_STATE;
    policy->source = MYBOT_EXPRESSION_SOURCE_STATE;
    policy->foreground_pending = false;
    policy->foreground_since = 0;
    return changed;
}

/* Used at SDK detach/reset: retain the workflow page, but discard all state
 * and deadlines borrowed from the ended conversation. */
static inline void mybot_expression_policy_cancel(mybot_expression_policy_t *policy) {
    (void)mybot_expression_policy_clear(policy);
    policy->phase = 0;
    policy->phase_since = 0;
}

static inline void mybot_expression_policy_update(mybot_expression_policy_t *policy,
                                                  const mybot_lcd_content_t *content,
                                                  uint32_t now) {
    bool conversation = content->screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
    if (policy->screen != content->screen) {
        (void)mybot_expression_policy_clear(policy);
        if (conversation) {
            policy->source = MYBOT_EXPRESSION_SOURCE_OPENING;
            policy->foreground = MYBOT_EXPRESSION_SURPRISED;
            policy->foreground_pending = true;
        }
    }
    policy->screen = content->screen;
    uint32_t phase = 0;
    if (conversation) {
        /* Match the terminal's deterministic priority for a malformed mask. */
        if (content->indicators & MYBOT_LCD_INDICATOR_SPEAKING)
            phase = MYBOT_LCD_INDICATOR_SPEAKING;
        else if (content->indicators & MYBOT_LCD_INDICATOR_THINKING)
            phase = MYBOT_LCD_INDICATOR_THINKING;
        else if (content->indicators & MYBOT_LCD_INDICATOR_LISTENING)
            phase = MYBOT_LCD_INDICATOR_LISTENING;
    }
    if (phase != policy->phase) {
        policy->phase = phase;
        policy->phase_since = now;
    }
}

/* The UI owner calls this every bounded tick, including without a new render.
 * Pending foreground clocks start only after successful display submission.
 * The 300 ms opener hold excludes its entering transition. */
static inline bool mybot_expression_policy_select(mybot_expression_policy_t *policy, uint32_t now,
                                                  uint32_t transition_ms,
                                                  mybot_expression_t *expression, bool *replay) {
    *replay = false;
    if (policy->screen != MYBOT_LCD_SCREEN_READY &&
        policy->screen != MYBOT_LCD_SCREEN_IN_CONVERSATION)
        return false;
    if (policy->source == MYBOT_EXPRESSION_SOURCE_OPENING) {
        if (!policy->foreground_pending &&
            (uint32_t)(now - policy->foreground_since) >= 300U + transition_ms)
            (void)mybot_expression_policy_clear(policy);
    }
    if (policy->source != MYBOT_EXPRESSION_SOURCE_STATE) {
        *expression = policy->foreground;
        *replay = policy->foreground_pending;
    } else if (policy->phase == MYBOT_LCD_INDICATOR_LISTENING) {
        *expression = MYBOT_EXPRESSION_LISTENING;
    } else if (policy->phase == MYBOT_LCD_INDICATOR_THINKING) {
        *expression = (uint32_t)(now - policy->phase_since) >= 3000U ? MYBOT_EXPRESSION_PROCESSING
                                                                     : MYBOT_EXPRESSION_THINKING;
    } else {
        *expression = MYBOT_EXPRESSION_NEUTRAL;
    }
    return true;
}

static inline void mybot_expression_policy_displayed(mybot_expression_policy_t *policy,
                                                     uint32_t now) {
    if (policy->foreground_pending) {
        policy->foreground_since = now;
        policy->foreground_pending = false;
    }
}

static inline const char *mybot_expression_source_name(mybot_expression_source_t source) {
    switch (source) {
    case MYBOT_EXPRESSION_SOURCE_OPENING:
        return "conversation_start";
    default:
        return "state";
    }
}

#endif /* MYBOT_EXPRESSION_POLICY_H_ */
