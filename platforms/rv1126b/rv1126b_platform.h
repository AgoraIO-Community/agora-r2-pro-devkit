/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_RV1126B_PLATFORM_H
#define MYBOT_RV1126B_PLATFORM_H

#include <mybot/mybot.h>
#include <mybot/platform/mybot_platform.h>

/* Strings are borrowed for the process lifetime, before registration. */
typedef struct {
    const char *capture_device;
    const char *playback_device;
    const char *network_interface;
    const char *input_device;
    const char *touch_device;
    unsigned int talk_key;
    bool enable_ui;
} rv1126b_platform_config_t;

const rv1126b_platform_config_t *rv1126b_platform_config(void);
int rv1126b_platform_register(const rv1126b_platform_config_t *config);
int rv1126b_network_ready(const char *interface_name);
const mybot_audio_capture_ops_t *rv1126b_capture_ops(void);
const mybot_audio_playback_ops_t *rv1126b_playback_ops(void);
const mybot_wifi_ops_t *rv1126b_network_ops(void);
const mybot_key_ops_t *rv1126b_key_ops(void);
int rv1126b_key_exit_requested(void);
#if MYBOT_ENABLE_VIDEO
const mybot_video_ops_t *rv1126b_video_ops(void);
#endif

#endif
