/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "device_identity.h"
#include "config.h"
#include "runtime_guard.h"
#include "service_status.h"
#include "controller.h"
#include "board_config.h"
#include "build_identity.h"
#include "mybot_platform/platform.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t s_exit;

static void signal_handler(int signal_number) {
    (void)signal_number;
    s_exit = 1;
}

static int copy_field(char *output, size_t size, const char *input) {
    if (strlen(input) >= size)
        return -1;
    memcpy(output, input, strlen(input) + 1);
    return 0;
}

int main(int argc, char **argv) {
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)rv1126b_build_identity[0];
    (void)rv1126b_build_source_identity[0];
    rv1126b_app_config_t options;
    int parsed = rv1126b_app_config_parse(argc, argv, &options);
    if (parsed != 0)
        return parsed > 0 ? 0 : 2;
    const char *server = options.server;
    const char *device_id = options.device_id;
    const char *identity_interface = options.identity_interface;
    bool demo = options.demo;
    mybot_config_t config = {0};
    char generated_id[33];
    if (!demo) {
        if (strncmp(server, "https://", 8) || !server[8] || strlen(device_id) > 32)
            goto invalid;
        if (!device_id[0]) {
            int result;
            if (identity_interface[0])
                result = rv1126b_identity_from_mac(identity_interface, generated_id,
                                                   sizeof(generated_id));
            else {
                FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
                if (!cpuinfo) {
                    perror("CPU identity");
                    return 1;
                }
                result = rv1126b_identity_from_serial(cpuinfo, generated_id, sizeof(generated_id));
                if (fclose(cpuinfo) != 0)
                    result = -1;
            }
            if (result < 0) {
                fprintf(stderr, "Cannot derive stable identity; supply --device-id or a permanent "
                                "--identity-interface\n");
                return 1;
            }
            device_id = generated_id;
        }
        if (copy_field(config.server_base, sizeof(config.server_base), server) < 0 ||
            copy_field(config.device_id, sizeof(config.device_id), device_id) < 0 ||
            copy_field(config.firmware_ver, sizeof(config.firmware_ver), RV1126B_VERSION) < 0 ||
            copy_field(config.hw_model, sizeof(config.hw_model), RV1126B_BOARD_MODEL) < 0)
            goto invalid;
    }
    struct sigaction action = {0};
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0)
        return 1;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, NULL) < 0)
        return 1;
    umask(0077);
    if (rv1126b_legacy_service_running() != 0) {
        fprintf(stderr, "Existing botstation service is running, or /proc cannot be checked. Stop "
                        "that service before starting this application.\n");
        return 1;
    }
    int legacy_lock = rv1126b_runtime_lock("/run/botstation-device.lock", false);
    if (legacy_lock == -1) {
        fprintf(stderr, "Existing botstation service holds its lock (or it cannot be checked). "
                        "Stop it before starting this application; it owns the same hardware.\n");
        return 1;
    }
    int lock = rv1126b_runtime_lock("/run/mybot-rv1126b.lock", true);
    if (lock < 0) {
        fprintf(stderr,
                "Cannot acquire /run/mybot-rv1126b.lock; another instance may be running\n");
        if (legacy_lock >= 0)
            close(legacy_lock);
        return 1;
    }
    int result = 1;
    if (!demo) {
        char executable[PATH_MAX], assets[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0 || (size_t)length >= sizeof(executable) - 1) {
            goto environment_failed;
        }
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (!slash) {
            goto environment_failed;
        }
        *slash = '\0';
        if (snprintf(assets, sizeof(assets), "%s/../assets", executable) >= (int)sizeof(assets) ||
            setenv("MYBOT_ASSETS_DIR", assets, 0) < 0 ||
            setenv("MYBOT_KV_STORE_DIR", "/userdata/mybot-rv1126b", 0) < 0 ||
            setenv("MYBOT_LOCALE", RV1126B_LOCALE, 0) < 0 || getenv("MYBOT_ASSETS_DIR")[0] != '/' ||
            rv1126b_prepare_storage(getenv("MYBOT_KV_STORE_DIR")) < 0) {
            perror("Cannot prepare private runtime directory");
            goto environment_failed;
        }
        fprintf(stdout, "mybot-rv1126b %s: id=%s, locale=%s, ptime=%d ms, video=%d\n",
                RV1126B_VERSION, device_id, RV1126B_LOCALE, MYBOT_AUDIO_PTIME_MS,
                MYBOT_ENABLE_VIDEO);
    }
    result = rv1126b_controller_run(&options, &config, &s_exit);
    goto finished;
environment_failed:
    (void)rv1126b_service_status(RV1126B_APP_STOPPING);
    (void)rv1126b_service_status(RV1126B_APP_FAILED);
finished:
    close(lock);
    if (legacy_lock >= 0)
        close(legacy_lock);
    return result;
invalid:
    fprintf(stderr, "Invalid or oversized option; run --help\n");
    return 2;
}
