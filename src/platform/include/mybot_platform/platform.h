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

/* Registers one complete SDK platform. Configuration strings must outlive it.
 * Returns 0 on success, -1 for invalid configuration or duplicate registration. */
int rv1126b_platform_register(const rv1126b_platform_config_t *config);
/* Checks routed IPv4 availability without configuring the operating system.
 * Returns 1 when ready, 0 while unavailable, -1 on an inspection failure. */
int rv1126b_network_ready(const char *interface_name);
/* True after foreground input requested application shutdown. */
int rv1126b_key_exit_requested(void);

#endif
