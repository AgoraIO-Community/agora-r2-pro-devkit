/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "check.h"
#include <stdlib.h>
#include <string.h>

int main(void) {
    CHECK(unsetenv("RV1126B_SERVER") == 0);
    CHECK(unsetenv("RV1126B_INPUT_DEVICE") == 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_TIMEOUT") == 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_HELPER") == 0);
    rv1126b_app_config_t config;
    char *defaults[] = {"mybot", NULL};
    CHECK(rv1126b_app_config_parse(1, defaults, &config) == 0);
    CHECK(!strcmp(config.server, "https://botstation.shengwang.cn/api"));
    CHECK(config.rotation == 270 && config.seconds == 0 && config.platform.talk_key == 139);
    CHECK(!config.demo && config.platform.enable_ui);
    CHECK(config.time_sync_timeout == 30 && !config.time_sync_helper[0]);
    CHECK(setenv("RV1126B_TIME_SYNC_TIMEOUT", "60", 1) == 0);
    CHECK(setenv("RV1126B_TIME_SYNC_HELPER", "/usr/local/bin/sync-time", 1) == 0);
    CHECK(rv1126b_app_config_parse(1, defaults, &config) == 0);
    CHECK(config.time_sync_timeout == 60);
    CHECK(!strcmp(config.time_sync_helper, "/usr/local/bin/sync-time"));
    char *time_options[] = {"mybot", "--time-sync-timeout", "10", "--time-sync-helper", "", NULL};
    CHECK(rv1126b_app_config_parse(5, time_options, &config) == 0);
    CHECK(config.time_sync_timeout == 10 && !config.time_sync_helper[0]);
    const char *bad_timeouts[] = {"0", "301", "-1", "1s", "", "999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad_timeouts) / sizeof(*bad_timeouts); ++i) {
        char *bad_timeout[] = {"mybot", "--time-sync-timeout", (char *)bad_timeouts[i], NULL};
        CHECK(rv1126b_app_config_parse(3, bad_timeout, &config) < 0);
    }
    char *relative_helper[] = {"mybot", "--time-sync-helper", "assets/time/sync-busybox.sh", NULL};
    CHECK(rv1126b_app_config_parse(3, relative_helper, &config) < 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_TIMEOUT") == 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_HELPER") == 0);
    CHECK(config.wifi_provisioning && !config.force_wifi_provisioning && !config.wifi_interface[0]);
    char *provision[] = {"mybot", "--wifi-provision", "--wifi-interface", "wlan0", NULL};
    CHECK(rv1126b_app_config_parse(4, provision, &config) == 0);
    CHECK(config.force_wifi_provisioning && !strcmp(config.wifi_interface, "wlan0"));
    char *monitor[] = {"mybot", "--no-wifi-provisioning", NULL};
    CHECK(rv1126b_app_config_parse(2, monitor, &config) == 0 && !config.wifi_provisioning);
    char *conflict[] = {"mybot", "--wifi-provision", "--no-wifi-provisioning", NULL};
    CHECK(rv1126b_app_config_parse(3, conflict, &config) < 0);
    char *bad_interface[] = {"mybot", "--wifi-interface", "../../wlan0", NULL};
    CHECK(rv1126b_app_config_parse(3, bad_interface, &config) < 0);
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
    CHECK(setenv("RV1126B_TIME_SYNC_TIMEOUT", "bad", 1) == 0);
    CHECK(setenv("RV1126B_TIME_SYNC_HELPER", "relative", 1) == 0);
    CHECK(rv1126b_app_config_parse(2, help, &config) == 1);
    CHECK(rv1126b_app_config_parse(2, version, &config) == 1);
    CHECK(rv1126b_app_config_parse(1, defaults, &config) < 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_TIMEOUT") == 0);
    CHECK(unsetenv("RV1126B_TIME_SYNC_HELPER") == 0);
    CHECK(rv1126b_app_config_parse(1, defaults, &config) == 0);
    CHECK(!strcmp(config.server, "https://environment.example/api"));
    puts("Application defaults, CLI precedence, invalid inputs and informational exits: PASS");
    return 0;
}
