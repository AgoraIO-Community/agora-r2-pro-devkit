/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_PLATFORM_INTERNAL_H
#define RV1126B_PLATFORM_INTERNAL_H

#include "mybot_platform/platform.h"

/* Borrowed immutable configuration, valid after registration for process lifetime. */
const rv1126b_platform_config_t *rv1126b_platform_config(void);
const mybot_audio_capture_ops_t *rv1126b_capture_ops(void);
const mybot_audio_playback_ops_t *rv1126b_playback_ops(void);
const mybot_wifi_ops_t *rv1126b_network_ops(void);
const mybot_key_ops_t *rv1126b_key_ops(void);
#if MYBOT_ENABLE_VIDEO
const mybot_video_ops_t *rv1126b_video_ops(void);
#endif

#endif
