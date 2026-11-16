/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_AUDIO_H
#define RV1126B_AUDIO_H

#include <mybot/platform/mybot_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Product prompts and the SDK must hold playback ownership exclusively. */
const mybot_audio_playback_ops_t *rv1126b_playback_ops(void);
/* Finish accepted prompt PCM before stop drops the device queue. Nonblocking
 * ALSA calls are retried for at most timeout_ms (1..1000); returns 0 or -1.
 * The caller owns the context throughout; start again before further writes. */
int rv1126b_playback_drain(void *context, unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif
#endif
