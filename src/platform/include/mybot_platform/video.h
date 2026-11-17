/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_VIDEO_H
#define RV1126B_VIDEO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Video-enabled builds only. Thread-safe, copied diagnostic flag for the single
 * hardware source. True after finite recovery attempts are exhausted or cleanup
 * fails; the product owner must stop the SDK before restarting the application.
 * This does not access SDK state or transfer ownership of camera resources. */
bool rv1126b_video_failed(void);

#ifdef __cplusplus
}
#endif

#endif
