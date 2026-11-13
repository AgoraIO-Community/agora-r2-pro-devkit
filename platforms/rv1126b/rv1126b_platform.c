/* SPDX-License-Identifier: Apache-2.0 */
#define _DEFAULT_SOURCE
#include "rv1126b_platform.h"
#include "linux_platform_adapters.h"
#include "rv1126b_ui.h"

#include <linux/input.h>
#include <net/if.h>
#include <string.h>

static rv1126b_platform_config_t s_config;
static bool s_registered;

const rv1126b_platform_config_t *rv1126b_platform_config(void) {
    return &s_config;
}

int rv1126b_platform_register(const rv1126b_platform_config_t *config) {
    if (s_registered || !config || !config->capture_device || !config->capture_device[0] ||
        !config->playback_device || !config->playback_device[0] || !config->network_interface ||
        strlen(config->network_interface) >= IFNAMSIZ || !config->input_device ||
        !config->touch_device || config->talk_key == KEY_RESERVED || config->talk_key > KEY_MAX ||
        config->talk_key == KEY_VOLUMEUP || config->talk_key == KEY_VOLUMEDOWN ||
        config->talk_key == KEY_LEFT || config->talk_key == KEY_RIGHT) {
        return -1;
    }
    s_config = *config;
    const mybot_platform_descriptor_t platform = {
        .wifi = rv1126b_network_ops(),
        .kv_store = linux_kv_store_platform_file_ops(),
        .key = rv1126b_key_ops(),
        .audio_capture = rv1126b_capture_ops(),
        .audio_playback = rv1126b_playback_ops(),
        .https = linux_https_platform_openssl_ops(),
        .lcd = config->enable_ui ? rv1126b_ui_lcd_ops() : linux_lcd_platform_console_ops(),
        .announce = linux_announce_platform_file_ops(),
#if MYBOT_ENABLE_VIDEO
        .video = rv1126b_video_ops(),
#endif
    };
    if (mybot_platform_register(&platform) < 0) {
        memset(&s_config, 0, sizeof(s_config));
        return -1;
    }
    s_registered = true;
    return 0;
}
