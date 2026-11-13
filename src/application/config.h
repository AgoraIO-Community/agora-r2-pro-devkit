/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_APP_CONFIG_H
#define RV1126B_APP_CONFIG_H

#include "mybot_platform/platform.h"

typedef struct {
    rv1126b_platform_config_t platform;
    const char *server;
    const char *device_id;
    const char *identity_interface;
    const char *drm_device;
    unsigned int rotation;
    unsigned int seconds;
    bool demo;
} rv1126b_app_config_t;

/* Borrows argv/environment strings for process lifetime. No I/O except help/error
 * output. Returns 0 to run, 1 after --help/--version, -1 for invalid arguments. */
int rv1126b_app_config_parse(int argc, char **argv, rv1126b_app_config_t *out);

#endif
