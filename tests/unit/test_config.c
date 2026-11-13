/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "check.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
    CHECK(unsetenv("RV1126B_SERVER") == 0);
    CHECK(unsetenv("RV1126B_INPUT_DEVICE") == 0);
    rv1126b_app_config_t config;
    char *defaults[] = {"mybot", NULL};
    CHECK(rv1126b_app_config_parse(1, defaults, &config) == 0);
    CHECK(!strcmp(config.server, "https://botstation.shengwang.cn/api"));
    CHECK(config.rotation == 270 && config.seconds == 0 && config.platform.talk_key == 139);
    CHECK(!config.demo && config.platform.enable_ui);
    CHECK(setenv("RV1126B_SERVER", "https://environment.example/api", 1) == 0);
    char *override[] = {"mybot",         "--server", "https://cli.example/api", "--rotation", "90",
                        "--run-seconds", "120",      "--input-device",          "none",       NULL};
    CHECK(rv1126b_app_config_parse(9, override, &config) == 0);
    CHECK(!strcmp(config.server, "https://cli.example/api") && config.rotation == 90);
    CHECK(config.seconds == 120 && !strcmp(config.platform.input_device, "none"));
    char *bad_rotation[] = {"mybot", "--rotation", "45", NULL};
    CHECK(rv1126b_app_config_parse(3, bad_rotation, &config) < 0);
    char *bad_time[] = {"mybot", "--run-seconds", "-1", NULL};
    CHECK(rv1126b_app_config_parse(3, bad_time, &config) < 0);
    char *bad_key[] = {"mybot", "--talk-key", "105", NULL};
    CHECK(rv1126b_app_config_parse(3, bad_key, &config) < 0);
    char *bad_demo[] = {"mybot", "--no-ui", "--ui-demo", NULL};
    CHECK(rv1126b_app_config_parse(3, bad_demo, &config) < 0);
    char *version[] = {"mybot", "--version", NULL};
    CHECK(rv1126b_app_config_parse(2, version, &config) == 1);
    char *help[] = {"mybot", "--help", NULL};
    CHECK(rv1126b_app_config_parse(2, help, &config) == 1);
    CHECK(rv1126b_app_config_parse(1, defaults, &config) == 0);
    CHECK(!strcmp(config.server, "https://environment.example/api"));
    puts("Application defaults, CLI precedence, invalid inputs and informational exits: PASS");
    return 0;
}
