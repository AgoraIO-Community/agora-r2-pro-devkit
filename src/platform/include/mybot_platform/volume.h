/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_VOLUME_H
#define RV1126B_VOLUME_H

#include <mybot/platform/mybot_audio.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Prepare product-owned volume before opening the SDK. The private directory
 * must belong to the current user and have mode 0700. Missing/corrupt state
 * defaults to 100 (unity gain); corrupt records produce a warning. */
int rv1126b_volume_prepare(const char *private_storage_directory);
/* SDK attachment and destruction retain the product-owned level. */
const mybot_audio_volume_ops_t *rv1126b_volume_ops(void);
/* Returns one nonblocking 0..100 snapshot. Before prepare/after shutdown the
 * fallback is 100, preserving playback's original unity gain. */
int rv1126b_volume_level(void);
/* Apply the product level once at the final playback boundary. Supports in-place
 * processing; separate input remains unmodified. No allocation or file I/O. */
void rv1126b_volume_scale(int16_t *output, const int16_t *input, size_t samples);
/* Call only after SDK and product audio users have stopped. */
void rv1126b_volume_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
