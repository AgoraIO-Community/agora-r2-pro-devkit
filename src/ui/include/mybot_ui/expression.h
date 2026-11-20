/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_EXPRESSION_H_
#define MYBOT_EXPRESSION_H_

/* Shared by the renderer and the display owner's expression timing policy. */
#define MYBOT_EXPRESSION_TRANSITION_MS 250U

#ifdef __cplusplus
extern "C" {
#endif

/* Stable identifiers for the artwork displayed by the renderer. */
typedef enum {
    MYBOT_EXPRESSION_NEUTRAL = 0,
    MYBOT_EXPRESSION_GREETING,
    MYBOT_EXPRESSION_HAPPY,
    MYBOT_EXPRESSION_EXCITED,
    MYBOT_EXPRESSION_CURIOUS,
    MYBOT_EXPRESSION_CONFUSED,
    MYBOT_EXPRESSION_THINKING,
    MYBOT_EXPRESSION_LISTENING,
    MYBOT_EXPRESSION_SURPRISED,
    MYBOT_EXPRESSION_SHY,
    MYBOT_EXPRESSION_PROUD,
    MYBOT_EXPRESSION_LOVE,
    MYBOT_EXPRESSION_SLEEPY,
    MYBOT_EXPRESSION_BORED,
    MYBOT_EXPRESSION_SAD,
    MYBOT_EXPRESSION_PLEADING,
    MYBOT_EXPRESSION_APOLOGY,
    MYBOT_EXPRESSION_WORRIED,
    MYBOT_EXPRESSION_ANNOYED,
    MYBOT_EXPRESSION_REFUSE,
    MYBOT_EXPRESSION_PROCESSING,
    MYBOT_EXPRESSION_ERROR,
    MYBOT_EXPRESSION_SLEEP,
    MYBOT_EXPRESSION_COUNT,
} mybot_expression_t;

/* Stable uppercase expression name, or NULL for an invalid ID. The returned
 * storage is immutable and valid for the process lifetime. */
const char *mybot_expression_name(mybot_expression_t expression);

/* Match a complete name ignoring ASCII letter case. Return 0 and set the
 * output on success, or -1 for an unknown name or a NULL argument. */
int mybot_expression_from_name(const char *name, mybot_expression_t *expression);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_EXPRESSION_H_ */
