/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOTSTATION_RV1126B_PLATFORM_H
#define BOTSTATION_RV1126B_PLATFORM_H

#include <mybot/mybot.h>
#include <mybot/platform/mybot_platform.h>

/* Configure before registration; these strings remain valid for the process lifetime. */
typedef struct {
    const char *capture_device;
    const char *playback_device;
    const char *network_interface;
    const char *input_device;
    unsigned int talk_key;
} rv1126b_platform_config_t;

const rv1126b_platform_config_t *rv1126b_platform_config(void);
int rv1126b_platform_register(const rv1126b_platform_config_t *config);
int rv1126b_network_ready(const char *interface_name);
int rv1126b_key_translate(unsigned int code, unsigned int talk_key, mybot_state_t state,
                        mybot_key_event_t *event);

const mybot_audio_capture_ops_t *rv1126b_capture_ops(void);
const mybot_audio_playback_ops_t *rv1126b_playback_ops(void);
const mybot_wifi_ops_t *rv1126b_network_ops(void);
const mybot_key_ops_t *rv1126b_key_ops(void);
#if MYBOT_ENABLE_VIDEO
const mybot_video_ops_t *rv1126b_video_ops(void);
#endif
int rv1126b_key_exit_requested(void);

#endif
