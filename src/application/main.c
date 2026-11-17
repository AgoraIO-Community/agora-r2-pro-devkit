/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include "device_identity.h"
#include "config.h"
#include "runtime_guard.h"
#include "service_status.h"
#include "network_gate.h"
#include "board_config.h"
#include "build_identity.h"
#include "mybot_platform/platform.h"
#include "mybot_platform/display.h"
#include "mybot_platform/controls.h"
#include "mybot_platform/power.h"
#include "mybot_platform/wifi.h"
#include "mybot_platform/volume.h"
#include "mybot_platform/prompt.h"
#include "mybot_platform/time_sync.h"
#include "mybot_ui/expression.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t s_exit;

static void publish_status(const char *state) {
    static bool warned;
    if (rv1126b_service_status(state) < 0 && !warned) {
        fprintf(stderr, "Cannot publish private application status\n");
        warned = true;
    }
}

static const char *sdk_status(mybot_state_t state) {
    switch (state) {
    case MYBOT_STATE_READY:
        return "ready";
    case MYBOT_STATE_PAIRING:
        return "pairing";
    case MYBOT_STATE_IN_CONVERSATION:
        return "conversation";
    case MYBOT_STATE_WIFI_DISCONNECTED:
        return "offline";
    case MYBOT_STATE_FAILED:
        return "failed";
    default:
        return "starting_services";
    }
}

static void signal_handler(int signal_number) {
    (void)signal_number;
    s_exit = 1;
}

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int copy_field(char *output, size_t size, const char *input) {
    if (strlen(input) >= size)
        return -1;
    memcpy(output, input, strlen(input) + 1);
    return 0;
}

static void stop_sdk(void) {
    mybot_stop();
    if (mybot_get_state() != MYBOT_STATE_STOPPED) {
        fprintf(stderr, "SDK shutdown incomplete; retrying cleanup\n");
        mybot_stop();
        if (mybot_get_state() != MYBOT_STATE_STOPPED) {
            fprintf(stderr,
                    "SDK cleanup incomplete; terminating without explicit resource cleanup\n");
            _Exit(EXIT_FAILURE);
        }
    }
}

/* The supervisor owns the system transition; this process owns orderly cleanup. */
static bool accept_poweroff(void) {
    if (!rv1126b_key_poweroff_requested())
        return false;
    if (rv1126b_poweroff_request() < 0) {
        fprintf(stderr, "Cannot schedule device poweroff: %s\n", strerror(errno));
        rv1126b_input_settings_page(MYBOT_SETTINGS_POWEROFF_ERROR);
        return false;
    }
    publish_status("powering_off");
    /* Allow the UI worker to present the final state before resource teardown. */
    const struct timespec present = {.tv_sec = 0, .tv_nsec = 150000000};
    nanosleep(&present, NULL);
    return true;
}

int main(int argc, char **argv) {
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    (void)rv1126b_build_identity[0];
    (void)rv1126b_build_source_identity[0];
    rv1126b_app_config_t options;
    int parsed = rv1126b_app_config_parse(argc, argv, &options);
    if (parsed != 0)
        return parsed > 0 ? 0 : 2;
    rv1126b_platform_config_t platform = options.platform;
    const char *server = options.server;
    const char *device_id = options.device_id;
    const char *identity_interface = options.identity_interface;
    const char *drm = options.drm_device;
    unsigned int rotation = options.rotation, seconds = options.seconds;
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
    int failed = 0;
    bool sdk_started = false, ui_started = false, wifi_started = false, volume_started = false;
    bool input_started = false;
    rv1126b_time_sync_t *time_sync = NULL;
    char dhcp_script[PATH_MAX];
    if (!demo) {
        char executable[PATH_MAX], assets[PATH_MAX];
        ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0 || (size_t)length >= sizeof(executable) - 1) {
            failed = 1;
            goto cleanup;
        }
        executable[length] = '\0';
        char *slash = strrchr(executable, '/');
        if (!slash) {
            failed = 1;
            goto cleanup;
        }
        *slash = '\0';
        if (snprintf(assets, sizeof(assets), "%s/../assets", executable) >= (int)sizeof(assets) ||
            setenv("MYBOT_ASSETS_DIR", assets, 0) < 0 ||
            setenv("MYBOT_KV_STORE_DIR", "/userdata/mybot-rv1126b", 0) < 0 ||
            setenv("MYBOT_LOCALE", RV1126B_LOCALE, 0) < 0 || getenv("MYBOT_ASSETS_DIR")[0] != '/' ||
            rv1126b_prepare_storage(getenv("MYBOT_KV_STORE_DIR")) < 0) {
            perror("Cannot prepare private runtime directory");
            failed = 1;
            goto cleanup;
        }
        fprintf(stdout, "mybot-rv1126b %s: id=%s, locale=%s, ptime=%d ms, video=%d\n",
                RV1126B_VERSION, device_id, RV1126B_LOCALE, MYBOT_AUDIO_PTIME_MS,
                MYBOT_ENABLE_VIDEO);
        if (rv1126b_volume_prepare(getenv("MYBOT_KV_STORE_DIR")) < 0) {
            fprintf(stderr, "Cannot restore private playback volume\n");
            failed = 1;
            goto cleanup;
        }
        volume_started = true;
        const rv1126b_time_sync_config_t time_config = {
            .timeout_seconds = options.time_sync_timeout,
            .helper_path = options.time_sync_helper,
        };
        time_sync = rv1126b_time_sync_create(&time_config);
        if (!time_sync) {
            fprintf(stderr, "Cannot initialize system time readiness guard\n");
            failed = 1;
            goto cleanup;
        }
        publish_status("starting");
    }
    if (platform.enable_ui) {
        if (rv1126b_ui_start(drm, rotation) < 0) {
            failed = 1;
            goto cleanup;
        }
        ui_started = true;
    }
    uint64_t started = monotonic_ms();
    if (!started) {
        failed = 1;
        goto cleanup;
    }
    uint64_t deadline = seconds ? started + (uint64_t)seconds * 1000 : UINT64_MAX;
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 100000000};
    if (demo) {
        const mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_READY, {0}, 0};
        rv1126b_ui_set_state(MYBOT_STATE_READY);
        rv1126b_ui_show(&content);
        int last = -1;
        while (!s_exit && monotonic_ms() < deadline && !rv1126b_ui_failed()) {
            int expression = (int)(((monotonic_ms() - started) / 2000) % MYBOT_EXPRESSION_COUNT);
            if (expression != last) {
                rv1126b_ui_demo_expression(expression);
                fprintf(stdout, "UI demo: %s\n",
                        mybot_expression_name((mybot_expression_t)expression));
                last = expression;
            }
            nanosleep(&interval, NULL);
        }
        failed = rv1126b_ui_failed();
        goto cleanup;
    }
    if (rv1126b_platform_register(&platform) < 0) {
        fprintf(stderr, "Invalid platform configuration\n");
        failed = 1;
        goto cleanup;
    }
    if (rv1126b_input_start(&platform, rotation) < 0) {
        fprintf(stderr, "Cannot start product input service\n");
        failed = 1;
        goto cleanup;
    }
    input_started = true;
    if (snprintf(dhcp_script, sizeof(dhcp_script), "%s/wifi/udhcpc.script",
                 getenv("MYBOT_ASSETS_DIR")) >= (int)sizeof(dhcp_script)) {
        failed = 1;
        goto cleanup;
    }
    const char *wifi_runtime = getenv("MYBOT_WIFI_RUNTIME_DIR");
    const rv1126b_wifi_config_t wifi = {
        .enabled = options.wifi_provisioning,
        .interface_name = options.wifi_interface,
        .storage_directory = getenv("MYBOT_KV_STORE_DIR"),
        .runtime_directory = wifi_runtime ? wifi_runtime : "/run/mybot-rv1126b-wifi",
        .dhcp_script = dhcp_script,
    };
    if (rv1126b_wifi_start(&wifi) < 0) {
        fprintf(stderr, "Cannot start Wi-Fi control service\n");
        failed = 1;
        goto cleanup;
    }
    wifi_started = true;
    bool require_provisioning = false;
    for (;;) {
        const mybot_lcd_content_t waiting = {MYBOT_LCD_SCREEN_WIFI_PROVISIONING, {0}, 0};
        rv1126b_ui_set_state(MYBOT_STATE_WIFI_PROVISIONING);
        rv1126b_ui_show(&waiting);
        rv1126b_ui_time_status(0);
        rv1126b_ui_wifi_failed(false);
        int last_wifi_state = -1;
        /* One announcement per setup session, including single-radio retries
         * that temporarily stop and then restore the same AP. */
        bool prompt_announced = false;
        int last_time_state = -1;
        rv1126b_network_gate_t network_gate = {0};
        fprintf(stdout, "Waiting for usable IPv4 routing and system time\n");
        while (!s_exit && monotonic_ms() < deadline) {
            if (rv1126b_key_exit_requested() || accept_poweroff())
                goto cleanup;
            if (rv1126b_key_wifi_requested()) {
                if (options.wifi_provisioning) {
                    rv1126b_wifi_status_t current = {0};
                    rv1126b_wifi_status(&current);
                    if (!current.ap_active) {
                        require_provisioning = true;
                        prompt_announced = false;
                        rv1126b_prompt_stop();
                        (void)rv1126b_time_sync_poll(time_sync, false);
                        rv1126b_wifi_request_provisioning();
                    }
                } else {
                    fprintf(stderr, "Wi-Fi provisioning is disabled by configuration\n");
                }
            }
            rv1126b_wifi_status_t status = {0};
            rv1126b_wifi_status(&status);
            if (status.ap_active && !prompt_announced) {
                prompt_announced = true;
                if (rv1126b_prompt_start(getenv("MYBOT_ASSETS_DIR"), getenv("MYBOT_LOCALE")) < 0)
                    fprintf(stderr, "Cannot start Wi-Fi setup voice guidance\n");
            } else if (!status.ap_active) {
                rv1126b_prompt_stop();
            }
            if ((int)status.state != last_wifi_state) {
                fprintf(stdout, "Wi-Fi state=%d%s%s\n", status.state, status.error[0] ? ": " : "",
                        status.error);
                if (status.ap_active)
                    fprintf(stdout, "Connect to hotspot %s and open http://192.168.4.1\n",
                            status.ssid);
                last_wifi_state = (int)status.state;
            }
            rv1126b_ui_provisioning(status.ap_active ? status.ssid : NULL);
            const bool wifi_failed = status.state == RV1126B_WIFI_ERROR && !status.ap_active;
            rv1126b_ui_wifi_failed(wifi_failed);
            int ready = rv1126b_network_ready(platform.network_interface);
            if (ready < 0 || (platform.enable_ui && rv1126b_ui_failed())) {
                failed = 1;
                goto cleanup;
            }
            /* A setup fault keeps the settings retry available in this process.
             * The gate still requires successful manually requested setup. */
            rv1126b_network_gate_action_t network_action =
                rv1126b_network_gate_poll(&network_gate, options.wifi_provisioning,
                                          require_provisioning, sdk_started, ready, &status);
            if (network_action == RV1126B_NETWORK_GATE_RECOVER) {
                /* No SDK is running in this loop. A route lost while closing
                 * setup still needs the same recovery acknowledgement as a
                 * loss during normal operation. */
                fprintf(stdout, "Retrying Wi-Fi before SDK startup\n");
                rv1126b_wifi_request_recovery();
            }
            bool network_ready = network_action == RV1126B_NETWORK_GATE_READY;
            rv1126b_time_sync_state_t time_state = rv1126b_time_sync_poll(time_sync, network_ready);
            if ((int)time_state != last_time_state) {
                fprintf(stdout, "Time readiness state=%d%s%s\n", time_state,
                        rv1126b_time_sync_error(time_sync)[0] ? ": " : "",
                        rv1126b_time_sync_error(time_sync));
                last_time_state = (int)time_state;
            }
            rv1126b_ui_time_status(time_state == RV1126B_TIME_WAIT_CLOCK ? 1 : 0);
            publish_status(status.ap_active                        ? "ap"
                           : wifi_failed                           ? "wifi_error"
                           : time_state == RV1126B_TIME_WAIT_CLOCK ? "waiting_clock"
                                                                   : "waiting_network");
            if (time_state == RV1126B_TIME_FAILED || time_state == RV1126B_TIME_TIMEOUT) {
                fprintf(stderr,
                        "Cannot start services: %s\n"
                        "Check the system time service or configure --time-sync-helper; "
                        "then restart.\n",
                        rv1126b_time_sync_error(time_sync));
                rv1126b_prompt_stop();
                rv1126b_time_sync_destroy(time_sync);
                time_sync = NULL;
                rv1126b_ui_time_status(2);
                publish_status("failed");
                /* Give the screen time to show the fault while honoring exit
                 * signals and an explicitly bounded verification run. */
                for (unsigned int hold = 0; hold < 20 && platform.enable_ui && !s_exit &&
                                            monotonic_ms() < deadline && !rv1126b_ui_failed();
                     ++hold) {
                    if (rv1126b_key_exit_requested() || accept_poweroff())
                        goto cleanup;
                    nanosleep(&interval, NULL);
                }
                failed = 1;
                goto cleanup;
            }
            if (network_ready && time_state == RV1126B_TIME_READY)
                break;
            nanosleep(&interval, NULL);
        }
        if (s_exit || monotonic_ms() >= deadline)
            goto cleanup;
        rv1126b_ui_provisioning(NULL);
        rv1126b_ui_wifi_failed(false);
        rv1126b_ui_time_status(0);
        rv1126b_prompt_stop();
        if (mybot_start(&config) < 0) {
            fprintf(stderr, "MyBot startup failed\n");
            failed = 1;
            goto cleanup;
        }
        sdk_started = true;
        bool reprovision = false;
        bool recover_network = false;
        mybot_state_t previous = MYBOT_STATE_STOPPED;
        while (!s_exit && monotonic_ms() < deadline && mybot_is_running()) {
            if (rv1126b_key_exit_requested() || accept_poweroff())
                goto cleanup;
            if (rv1126b_key_wifi_requested()) {
                if (options.wifi_provisioning) {
                    reprovision = true;
                    break;
                }
                fprintf(stderr, "Wi-Fi provisioning is disabled by configuration\n");
            }
            rv1126b_wifi_status_t network_status = {0};
            rv1126b_wifi_status(&network_status);
            if (options.wifi_provisioning &&
                network_status.state == RV1126B_WIFI_RECOVERY_REQUIRED) {
                recover_network = true;
                break;
            }
            mybot_state_t state = mybot_get_state();
            if (state != previous) {
                fprintf(stdout, "SDK state=%d\n", state);
                rv1126b_ui_set_state(state);
                publish_status(sdk_status(state));
                previous = state;
            }
            if (state == MYBOT_STATE_FAILED || (platform.enable_ui && rv1126b_ui_failed())) {
                failed = 1;
                goto cleanup;
            }
            nanosleep(&interval, NULL);
        }
        bool input_exit = rv1126b_key_exit_requested();
        if (!reprovision && options.wifi_provisioning && !input_exit)
            reprovision = rv1126b_key_wifi_requested() != 0;
        if (!reprovision && options.wifi_provisioning && !input_exit &&
            rv1126b_network_ready(platform.network_interface) == 0)
            recover_network = true;
        stop_sdk();
        sdk_started = false;
        if (s_exit || monotonic_ms() >= deadline || input_exit)
            goto cleanup;
        if (!reprovision && !recover_network) {
            failed = 1;
            goto cleanup;
        }
        require_provisioning = true;
        if (reprovision)
            rv1126b_wifi_request_provisioning();
        else
            rv1126b_wifi_request_recovery();
    }
cleanup:
    if (!demo)
        publish_status("stopping");
    rv1126b_prompt_stop();
    if (sdk_started)
        stop_sdk();
    rv1126b_time_sync_destroy(time_sync);
    if (wifi_started)
        rv1126b_wifi_stop();
    if (volume_started)
        rv1126b_volume_shutdown();
    if (input_started && rv1126b_input_stop() < 0)
        failed = 1;
    if (ui_started && rv1126b_ui_stop() < 0)
        failed = 1;
    if (!demo)
        publish_status(failed ? "failed" : "stopped");
    close(lock);
    if (legacy_lock >= 0)
        close(legacy_lock);
    return failed ? 1 : 0;
invalid:
    fprintf(stderr, "Invalid or oversized option; run --help\n");
    return 2;
}
