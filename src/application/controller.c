/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "controller.h"
#include "network_gate.h"
#include "service_status.h"
#include "mybot_platform/display.h"
#include "mybot_platform/factory_reset.h"
#include "mybot_platform/controls.h"
#include "mybot_platform/power.h"
#include "mybot_platform/supervisor.h"
#include "mybot_platform/wifi.h"
#include "mybot_platform/volume.h"
#include "mybot_platform/prompt.h"
#include "mybot_platform/time_sync.h"
#if MYBOT_ENABLE_VIDEO
#include "mybot_platform/video.h"
#endif
#include "mybot_ui/expression.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void publish_status(rv1126b_app_state_t state) {
    static bool warned;
    if (rv1126b_service_status(state) < 0 && !warned) {
        fprintf(stderr, "Cannot publish private application status\n");
        warned = true;
    }
}

static rv1126b_app_state_t sdk_status(mybot_state_t state) {
    switch (state) {
#define RV1126B_SDK_STATE(sdk, service, label)                                                     \
    case sdk:                                                                                      \
        return service;
#include "mybot_platform/sdk_states.def"
#undef RV1126B_SDK_STATE
    }
    return RV1126B_APP_STARTING_SERVICES;
}

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void stop_sdk(void) {
    mybot_stop();
    if (mybot_get_state() != MYBOT_STATE_STOPPED) {
        fprintf(stderr, "SDK shutdown incomplete; retrying cleanup\n");
        mybot_stop();
        if (mybot_get_state() != MYBOT_STATE_STOPPED) {
            fprintf(stderr,
                    "SDK cleanup incomplete; terminating without explicit resource cleanup\n");
            _Exit(RV1126B_EXIT_UNCLEAN);
        }
    }
}

/* The supervisor owns the system transition; this process owns orderly cleanup. */
static bool accept_system_action(bool *release_display) {
    if (rv1126b_key_factory_reset_requested()) {
        if (rv1126b_factory_reset_request() < 0) {
            fprintf(stderr, "Factory reset request rejected: %s\n", strerror(errno));
            rv1126b_input_settings_page(MYBOT_SETTINGS_RESET_ERROR);
            return false;
        }
        *release_display = true;
        publish_status(RV1126B_APP_FACTORY_RESET);
        return true;
    }
    if (!rv1126b_key_poweroff_requested())
        return false;
    if (rv1126b_poweroff_request() < 0) {
        fprintf(stderr, "Cannot schedule device poweroff: %s\n", strerror(errno));
        rv1126b_input_settings_page(MYBOT_SETTINGS_POWEROFF_ERROR);
        return false;
    }
    *release_display = true;
    publish_status(RV1126B_APP_POWERING_OFF);
    /* Allow the UI worker to present the final state before resource teardown. */
    const struct timespec present = {.tv_sec = 0, .tv_nsec = 150000000};
    nanosleep(&present, NULL);
    return true;
}

typedef struct {
    const rv1126b_app_config_t *options;
    volatile sig_atomic_t *exit_requested;
    uint64_t deadline;
    rv1126b_time_sync_t *time_sync;
    bool setup_pending, sdk_needs_stop, release_display;
} controller_run_t;

typedef enum {
    SESSION_EXIT,
    SESSION_FAILED,
    SESSION_READY,
    SESSION_PROVISION,
    SESSION_RECOVER
} session_result_t;

static const struct timespec interval = {.tv_nsec = 100000000};

static bool within_deadline(const controller_run_t *run) {
    return !*run->exit_requested && monotonic_ms() < run->deadline;
}

static session_result_t wait_for_startup(controller_run_t *run, bool require_provisioning) {
    const mybot_lcd_content_t waiting = {MYBOT_LCD_SCREEN_WIFI_PROVISIONING, {0}, 0};
    rv1126b_ui_set_state(MYBOT_STATE_WIFI_PROVISIONING);
    rv1126b_ui_show(&waiting);
    rv1126b_ui_time_status(MYBOT_DISPLAY_CLOCK_INACTIVE);
    rv1126b_ui_wifi_failed(false);
    int last_wifi_state = -1;
    /* One announcement per setup session, including single-radio retries
     * that temporarily stop and then restore the same AP. */
    bool prompt_announced = false;
    int last_time_state = -1;
    rv1126b_network_gate_t network_gate = {0};
    fprintf(stdout, "Waiting for usable IPv4 routing and system time\n");
    while (within_deadline(run)) {
        if (rv1126b_key_exit_requested() || accept_system_action(&run->release_display))
            return SESSION_EXIT;
        if (rv1126b_key_wifi_requested()) {
            if (run->options->wifi_provisioning) {
                rv1126b_wifi_status_t current = {0};
                rv1126b_wifi_status(&current);
                if (!current.ap_active) {
                    require_provisioning = true;
                    prompt_announced = false;
                    rv1126b_prompt_stop();
                    (void)rv1126b_time_sync_poll(run->time_sync, false);
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
                fprintf(stdout, "Connect to hotspot %s and open http://192.168.4.1\n", status.ssid);
            last_wifi_state = (int)status.state;
        }
        rv1126b_ui_provisioning(status.ap_active ? status.ssid : NULL);
        const bool wifi_failed = status.state == RV1126B_WIFI_ERROR && !status.ap_active;
        rv1126b_ui_wifi_failed(wifi_failed);
        int ready = rv1126b_network_ready(run->options->platform.network_interface);
        if (ready < 0 || (run->options->platform.enable_ui && rv1126b_ui_failed())) {
            return SESSION_FAILED;
        }
        /* A setup fault keeps the settings retry available in this process.
         * The gate still requires successful manually requested setup. */
        rv1126b_network_gate_action_t network_action =
            rv1126b_network_gate_poll(&network_gate, run->options->wifi_provisioning,
                                      require_provisioning, false, ready, &status);
        if (network_action == RV1126B_NETWORK_GATE_RECOVER) {
            /* No SDK is running in this loop. A route lost while closing
             * setup still needs the same recovery acknowledgement as a
             * loss during normal operation. */
            fprintf(stdout, "Retrying Wi-Fi before SDK startup\n");
            rv1126b_wifi_request_recovery();
        }
        bool network_ready = network_action == RV1126B_NETWORK_GATE_READY;
        if (run->setup_pending && network_ready && status.state == RV1126B_WIFI_ONLINE) {
            if (rv1126b_factory_setup_complete() < 0) {
                fprintf(stderr, "Cannot record completed factory network setup\n");
                return SESSION_FAILED;
            }
            run->setup_pending = false;
        }
        rv1126b_time_sync_state_t time_state =
            rv1126b_time_sync_poll(run->time_sync, network_ready);
        if ((int)time_state != last_time_state) {
            fprintf(stdout, "Time readiness state=%d%s%s\n", time_state,
                    rv1126b_time_sync_error(run->time_sync)[0] ? ": " : "",
                    rv1126b_time_sync_error(run->time_sync));
            last_time_state = (int)time_state;
        }
        bool waiting_clock =
            time_state == RV1126B_TIME_WAIT_CLOCK || time_state == RV1126B_TIME_TIMEOUT;
        rv1126b_ui_time_status(waiting_clock ? MYBOT_DISPLAY_CLOCK_WAITING
                                             : MYBOT_DISPLAY_CLOCK_INACTIVE);
        publish_status(status.ap_active ? RV1126B_APP_AP
                       : wifi_failed    ? RV1126B_APP_WIFI_ERROR
                       : waiting_clock  ? RV1126B_APP_WAITING_CLOCK
                                        : RV1126B_APP_WAITING_NETWORK);
        if (time_state == RV1126B_TIME_FAILED) {
            fprintf(stderr,
                    "Cannot start services: %s\n"
                    "Check the system time service or configure --time-sync-helper; "
                    "then restart.\n",
                    rv1126b_time_sync_error(run->time_sync));
            rv1126b_prompt_stop();
            rv1126b_time_sync_destroy(run->time_sync);
            run->time_sync = NULL;
            rv1126b_ui_time_status(MYBOT_DISPLAY_CLOCK_FAILED);
            publish_status(RV1126B_APP_FAILED);
            /* Give the screen time to show the fault while honoring exit
             * signals and an explicitly bounded verification run. */
            for (unsigned int hold = 0; hold < 20 && run->options->platform.enable_ui &&
                                        within_deadline(run) && !rv1126b_ui_failed();
                 ++hold) {
                if (rv1126b_key_exit_requested() || accept_system_action(&run->release_display))
                    return SESSION_EXIT;
                nanosleep(&interval, NULL);
            }
            return SESSION_FAILED;
        }
        if (network_ready && time_state == RV1126B_TIME_READY)
            break;
        nanosleep(&interval, NULL);
    }
    if (!within_deadline(run))
        return SESSION_EXIT;
    rv1126b_ui_provisioning(NULL);
    rv1126b_ui_wifi_failed(false);
    rv1126b_ui_time_status(MYBOT_DISPLAY_CLOCK_INACTIVE);
    rv1126b_prompt_stop();
    return SESSION_READY;
}

static session_result_t run_session(controller_run_t *run, const mybot_config_t *config) {
    /* stop() is idempotent after failed startup and retries cleanup that
     * could not finish during the startup attempt. */
    run->sdk_needs_stop = true;
    if (mybot_start(config) < 0) {
        fprintf(stderr, "MyBot startup failed\n");
        return SESSION_FAILED;
    }
    bool reprovision = false;
    bool recover_network = false;
    mybot_state_t previous = MYBOT_STATE_STOPPED;
    while (within_deadline(run) && mybot_is_running()) {
        if (rv1126b_key_exit_requested() || accept_system_action(&run->release_display))
            return SESSION_EXIT;
        if (rv1126b_key_wifi_requested()) {
            if (run->options->wifi_provisioning) {
                reprovision = true;
                break;
            }
            fprintf(stderr, "Wi-Fi provisioning is disabled by configuration\n");
        }
        rv1126b_wifi_status_t network_status = {0};
        rv1126b_wifi_status(&network_status);
        if (run->options->wifi_provisioning &&
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
#if MYBOT_ENABLE_VIDEO
        if (rv1126b_video_failed()) {
            fprintf(stderr, "Video recovery exhausted; stopping the SDK before restart\n");
            return SESSION_FAILED;
        }
#endif
        if (state == MYBOT_STATE_FAILED ||
            (run->options->platform.enable_ui && rv1126b_ui_failed())) {
            return SESSION_FAILED;
        }
        nanosleep(&interval, NULL);
    }
    bool input_exit = rv1126b_key_exit_requested();
    if (!reprovision && run->options->wifi_provisioning && !input_exit)
        reprovision = rv1126b_key_wifi_requested() != 0;
    if (!reprovision && run->options->wifi_provisioning && !input_exit &&
        rv1126b_network_ready(run->options->platform.network_interface) == 0)
        recover_network = true;
    stop_sdk();
    run->sdk_needs_stop = false;
    if (!within_deadline(run) || input_exit)
        return SESSION_EXIT;
    if (!reprovision && !recover_network) {
        return SESSION_FAILED;
    }
    return reprovision ? SESSION_PROVISION : SESSION_RECOVER;
}

int rv1126b_controller_run(const rv1126b_app_config_t *settings, const mybot_config_t *config,
                           volatile sig_atomic_t *exit_requested) {
    if (!settings || !config || !exit_requested)
        return 1;
    const rv1126b_app_config_t options = *settings;
    const rv1126b_platform_config_t platform = options.platform;
    const bool demo = options.demo;
    const char *drm = options.drm_device;
    const unsigned int rotation = options.rotation, seconds = options.seconds;
    int failed = 0;
    bool ui_started = false, wifi_started = false, volume_started = false;
    bool input_started = false;
    controller_run_t run = {.options = &options, .exit_requested = exit_requested};
    char dhcp_script[PATH_MAX];
    if (!demo) {
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
        run.time_sync = rv1126b_time_sync_create(&time_config);
        if (!run.time_sync) {
            fprintf(stderr, "Cannot initialize system time readiness guard\n");
            failed = 1;
            goto cleanup;
        }
        publish_status(RV1126B_APP_STARTING);
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
    run.deadline = seconds ? started + (uint64_t)seconds * 1000 : UINT64_MAX;
    if (demo) {
        const mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_READY, {0}, 0};
        rv1126b_ui_set_state(MYBOT_STATE_READY);
        rv1126b_ui_show(&content);
        int last = -1;
        while (within_deadline(&run) && !rv1126b_ui_failed()) {
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
    int setup_pending = getenv("MYBOT_SUPERVISOR_RUNTIME") ? rv1126b_factory_setup_pending() : 0;
    if (setup_pending < 0 || (setup_pending && !options.wifi_provisioning)) {
        fprintf(stderr, "Factory setup requires a valid marker and enabled Wi-Fi provisioning\n");
        failed = 1;
        goto cleanup;
    }
    const rv1126b_wifi_config_t wifi = {
        .force_ap = setup_pending != 0,
        .enabled = options.wifi_provisioning,
        .interface_name = options.wifi_interface,
        .storage_directory = getenv("MYBOT_KV_STORE_DIR"),
        .runtime_directory = wifi_runtime ? wifi_runtime : "/run/mybot-rv1126b-wifi",
        .dhcp_script = dhcp_script,
        .route_interface = platform.network_interface,
    };
    if (rv1126b_wifi_start(&wifi) < 0) {
        fprintf(stderr, "Cannot start Wi-Fi control service\n");
        failed = 1;
        goto cleanup;
    }
    wifi_started = true;
    run.setup_pending = setup_pending != 0;
    bool require_provisioning = run.setup_pending;
    for (;;) {
        session_result_t result = wait_for_startup(&run, require_provisioning);
        if (result == SESSION_READY)
            result = run_session(&run, config);
        if (result == SESSION_PROVISION || result == SESSION_RECOVER) {
            require_provisioning = true;
            if (result == SESSION_PROVISION)
                rv1126b_wifi_request_provisioning();
            else
                rv1126b_wifi_request_recovery();
            continue;
        }
        failed = result == SESSION_FAILED;
        break;
    }
cleanup:
    if (!demo)
        publish_status(RV1126B_APP_STOPPING);
    rv1126b_prompt_stop();
    if (run.sdk_needs_stop)
        stop_sdk();
    rv1126b_time_sync_destroy(run.time_sync);
    if (wifi_started)
        rv1126b_wifi_stop();
    if (volume_started)
        rv1126b_volume_shutdown();
    if (input_started && rv1126b_input_stop() < 0)
        failed = 1;
    if (ui_started) {
        int result = run.release_display ? rv1126b_ui_stop_for_poweroff() : rv1126b_ui_stop();
        if (result < 0)
            failed = 1;
    }
    if (!demo)
        publish_status(failed ? RV1126B_APP_FAILED : RV1126B_APP_STOPPED);
    return failed ? 1 : 0;
}
