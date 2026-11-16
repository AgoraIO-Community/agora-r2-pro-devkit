/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_PROMPT_H
#define RV1126B_PROMPT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Play locales/{zh-CN,en-US}/wificonfig.pcm on a product worker. Call only
 * after stopping the SDK and making the AP/portal ready. This starts no file or
 * PCM I/O on the caller. An existing worker returns -1/EBUSY, even after EOF;
 * call stop to join it before starting another prompt. Playback failures are
 * logged and do not change provisioning state. */
int rv1126b_prompt_start(const char *assets_directory, const char *locale);
/* Cancels and joins the sole worker. All PCM/file ownership is released before
 * returning, so the SDK can safely open playback. Safe to call repeatedly. */
void rv1126b_prompt_stop(void);
/* A nonblocking snapshot; false includes normal EOF, error and cancellation. */
bool rv1126b_prompt_active(void);

#ifdef __cplusplus
}
#endif
#endif
