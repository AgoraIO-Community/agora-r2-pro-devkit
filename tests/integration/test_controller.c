/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "controller.h"
#include "service_status.h"
#include "check.h"
#include <errno.h>
#include <string.h>
#include <time.h>

static uint64_t fixture_now;

static int controller_clock(clockid_t id, struct timespec *now) {
    CHECK(id == CLOCK_MONOTONIC);
    now->tv_sec = (time_t)(fixture_now / 1000);
    now->tv_nsec = (long)(fixture_now % 1000) * 1000000;
    return 0;
}

static int controller_pause(const struct timespec *duration, struct timespec *remaining) {
    (void)remaining;
    fixture_now += (uint64_t)duration->tv_sec * 1000 + duration->tv_nsec / 1000000;
    return 0;
}

#define clock_gettime controller_clock
#define nanosleep controller_pause
#include "../../src/application/controller.c"
#undef clock_gettime
#undef nanosleep

typedef enum {
    NORMAL,
    MANUAL,
    MANUAL_RETRY,
    RECOVERY,
    CLOCK_FAILURE,
    VIDEO_FAILURE,
    INPUT_FAILURE,
    SDK_START_FAILURE,
    SDK_START_FAILURE_CLEAN,
    POWEROFF,
    POWEROFF_REJECTED,
    POWEROFF_DISPLAY_FAILURE,
    POWEROFF_WAITING,
    RESET_WAITING,
    RESET_ACTIVE,
    RESET_REJECTED,
    FIRST_SETUP,
    FIRST_SETUP_ERROR,
    DEMO,
    OFFLINE_DEADLINE
} scenario_t;

enum {
    VOLUME_START = 1,
    TIME_START,
    UI_START,
    PLATFORM_REGISTER,
    INPUT_START,
    WIFI_START,
    SDK_START,
    SDK_STOP,
    AP_REQUEST,
    RECOVERY_REQUEST,
    PROMPT_START,
    PROMPT_STOP,
    WIFI_STOP,
    TIME_STOP,
    VOLUME_STOP,
    INPUT_STOP,
    UI_STOP,
    UI_STOP_FOR_POWEROFF,
    POWER_REQUEST,
    RESET_REQUEST,
    SETUP_FINISHED
};

typedef struct {
    scenario_t scenario;
    unsigned int events[128], count, starts, stops, wifi_requests, recoveries;
    unsigned int active_checks, ap_polls, clock_failed, wifi_failed, prompts, status_calls;
    bool reset_rejected;
    bool sdk_active, prompt_active, manual_sent, retry_sent, error_seen, ap_seen, power_sent;
    rv1126b_app_state_t last_status;
    unsigned int statuses[RV1126B_APP_STATE_COUNT];
} fixture_t;

static fixture_t fixture;

struct rv1126b_time_sync {
    int unused;
};

static struct rv1126b_time_sync clock_context;

static void record(unsigned int event) {
    CHECK(fixture.count < sizeof(fixture.events) / sizeof(fixture.events[0]));
    fixture.events[fixture.count++] = event;
}

static int position(unsigned int event) {
    for (unsigned int index = 0; index < fixture.count; ++index)
        if (fixture.events[index] == event)
            return (int)index;
    return -1;
}

int rv1126b_service_status(rv1126b_app_state_t state) {
    CHECK((unsigned int)state < RV1126B_APP_STATE_COUNT);
    fixture.last_status = state;
    ++fixture.statuses[state];
    ++fixture.status_calls;
    fixture.error_seen = fixture.error_seen || state == RV1126B_APP_WIFI_ERROR;
    return 0;
}

int rv1126b_volume_prepare(const char *directory) {
    CHECK(directory && !strcmp(directory, "/fixture/storage"));
    record(VOLUME_START);
    return 0;
}

void rv1126b_volume_shutdown(void) {
    CHECK(!fixture.sdk_active && !fixture.prompt_active);
    record(VOLUME_STOP);
}

rv1126b_time_sync_t *rv1126b_time_sync_create(const rv1126b_time_sync_config_t *config) {
    CHECK(config && config->timeout_seconds == 30);
    record(TIME_START);
    return &clock_context;
}

rv1126b_time_sync_state_t rv1126b_time_sync_poll(rv1126b_time_sync_t *context, bool ready) {
    CHECK(context == &clock_context);
    if (!ready)
        return RV1126B_TIME_WAIT_NETWORK;
    return fixture.scenario == CLOCK_FAILURE ? RV1126B_TIME_FAILED : RV1126B_TIME_READY;
}

const char *rv1126b_time_sync_error(const rv1126b_time_sync_t *context) {
    CHECK(context == &clock_context);
    return "synthetic clock fault";
}

void rv1126b_time_sync_destroy(rv1126b_time_sync_t *context) {
    if (context) {
        CHECK(context == &clock_context && !fixture.sdk_active);
        record(TIME_STOP);
    }
}

int rv1126b_ui_start(const char *device, unsigned int rotation) {
    CHECK(!strcmp(device, "fixture") && rotation == 270);
    record(UI_START);
    return 0;
}

int rv1126b_ui_stop(void) {
    record(UI_STOP);
    return 0;
}

int rv1126b_ui_stop_for_poweroff(void) {
    record(UI_STOP_FOR_POWEROFF);
    return fixture.scenario == POWEROFF_DISPLAY_FAILURE ? -1 : 0;
}

void rv1126b_ui_set_state(mybot_state_t state) {
    (void)state;
}

void rv1126b_ui_show(const mybot_lcd_content_t *content) {
    CHECK(content);
}

void rv1126b_ui_time_status(mybot_display_clock_state_t state) {
    CHECK((unsigned int)state <= MYBOT_DISPLAY_CLOCK_FAILED);
    fixture.clock_failed += state == MYBOT_DISPLAY_CLOCK_FAILED;
}

void rv1126b_ui_wifi_failed(bool failed) {
    fixture.wifi_failed += failed;
}

void rv1126b_ui_provisioning(const char *ssid) {
    if (ssid)
        CHECK(!strcmp(ssid, "fixture-ap"));
}

int rv1126b_ui_failed(void) {
    return 0;
}

void rv1126b_ui_demo_expression(int expression) {
    CHECK(expression >= 0 && expression < MYBOT_EXPRESSION_COUNT);
}

const char *mybot_expression_name(mybot_expression_t expression) {
    (void)expression;
    return "synthetic";
}

int rv1126b_platform_register(const rv1126b_platform_config_t *config) {
    CHECK(config);
    record(PLATFORM_REGISTER);
    return 0;
}

int rv1126b_input_start(const rv1126b_platform_config_t *config, unsigned int rotation) {
    CHECK(config && rotation == 270);
    record(INPUT_START);
    return fixture.scenario == INPUT_FAILURE ? -1 : 0;
}

int rv1126b_input_stop(void) {
    record(INPUT_STOP);
    return 0;
}

void rv1126b_input_settings_page(mybot_settings_page_t page) {
    if (page == MYBOT_SETTINGS_RESET_ERROR)
        fixture.reset_rejected = true;
    else
        CHECK(page == MYBOT_SETTINGS_POWEROFF_ERROR);
}

int rv1126b_key_exit_requested(void) {
    if (!fixture.sdk_active)
        return 0;
    ++fixture.active_checks;
    if (fixture.scenario == NORMAL || fixture.starts >= 2)
        return fixture.active_checks >= 2;
    return 0;
}

int rv1126b_key_wifi_requested(void) {
    if ((fixture.scenario == MANUAL || fixture.scenario == MANUAL_RETRY) && fixture.sdk_active &&
        fixture.starts == 1 && !fixture.manual_sent) {
        fixture.manual_sent = true;
        return 1;
    }
    if (fixture.scenario == MANUAL_RETRY && fixture.error_seen && !fixture.retry_sent) {
        CHECK(!fixture.sdk_active && fixture.starts == 1);
        fixture.retry_sent = true;
        return 1;
    }
    return 0;
}

int rv1126b_key_factory_reset_requested(void) {
    return position(RESET_REQUEST) < 0 &&
           (fixture.scenario == RESET_WAITING ||
            (fixture.sdk_active &&
             (fixture.scenario == RESET_ACTIVE || fixture.scenario == RESET_REJECTED)));
}

int rv1126b_factory_reset_request(void) {
    record(RESET_REQUEST);
    return fixture.scenario == RESET_REJECTED ? -1 : 0;
}

int rv1126b_factory_setup_pending(void) {
    return fixture.scenario == FIRST_SETUP || fixture.scenario == FIRST_SETUP_ERROR;
}

int rv1126b_factory_setup_complete(void) {
    CHECK(fixture.ap_seen && !fixture.sdk_active);
    record(SETUP_FINISHED);
    return 0;
}

int rv1126b_key_poweroff_requested(void) {
    bool active = fixture.scenario == POWEROFF || fixture.scenario == POWEROFF_REJECTED ||
                  fixture.scenario == POWEROFF_DISPLAY_FAILURE;
    return !fixture.power_sent &&
           ((active && fixture.sdk_active) || fixture.scenario == POWEROFF_WAITING);
}

int rv1126b_poweroff_request(void) {
    CHECK(!fixture.power_sent);
    fixture.power_sent = true;
    record(POWER_REQUEST);
    return fixture.scenario == POWEROFF_REJECTED ? -1 : 0;
}

int rv1126b_network_ready(const char *interface_name) {
    CHECK(interface_name && !strcmp(interface_name, "fixture0"));
    return fixture.scenario == OFFLINE_DEADLINE ? 0 : 1;
}

int rv1126b_wifi_start(const rv1126b_wifi_config_t *config) {
    CHECK(config && config->enabled &&
          !strcmp(config->dhcp_script, "/fixture/assets/wifi/udhcpc.script"));
    CHECK(config->force_ap ==
          (fixture.scenario == FIRST_SETUP || fixture.scenario == FIRST_SETUP_ERROR));
    record(WIFI_START);
    return 0;
}

void rv1126b_wifi_stop(void) {
    CHECK(!fixture.sdk_active && !fixture.prompt_active);
    record(WIFI_STOP);
}

void rv1126b_wifi_request_provisioning(void) {
    CHECK(!fixture.sdk_active);
    ++fixture.wifi_requests;
    fixture.ap_polls = 0;
    record(AP_REQUEST);
}

void rv1126b_wifi_request_recovery(void) {
    CHECK(!fixture.sdk_active);
    ++fixture.recoveries;
    record(RECOVERY_REQUEST);
}

void rv1126b_wifi_status(rv1126b_wifi_status_t *status) {
    *status = (rv1126b_wifi_status_t){.state = RV1126B_WIFI_ONLINE};
    if (fixture.scenario == FIRST_SETUP_ERROR)
        status->state = RV1126B_WIFI_ERROR;
    else if (fixture.scenario == FIRST_SETUP && !fixture.ap_seen) {
        status->state = RV1126B_WIFI_AP_READY;
        status->ap_active = true;
        strcpy(status->ssid, "fixture-ap");
        fixture.ap_seen = true;
    } else if (fixture.scenario == OFFLINE_DEADLINE)
        status->state = RV1126B_WIFI_WAITING;
    else if (fixture.scenario == RECOVERY && fixture.sdk_active && fixture.starts == 1)
        status->state = RV1126B_WIFI_RECOVERY_REQUIRED;
    else if (fixture.scenario == MANUAL_RETRY && fixture.wifi_requests == 1)
        status->state = RV1126B_WIFI_ERROR;
    else if (fixture.wifi_requests && !fixture.sdk_active && fixture.ap_polls++ == 0) {
        status->state = RV1126B_WIFI_AP_READY;
        status->ap_active = true;
        strcpy(status->ssid, "fixture-ap");
        fixture.ap_seen = true;
    }
}

int rv1126b_prompt_start(const char *assets, const char *locale) {
    CHECK(!fixture.sdk_active && !fixture.prompt_active && assets && locale);
    fixture.prompt_active = true;
    ++fixture.prompts;
    record(PROMPT_START);
    return 0;
}

void rv1126b_prompt_stop(void) {
    if (fixture.prompt_active) {
        fixture.prompt_active = false;
        record(PROMPT_STOP);
    }
}

int mybot_start(const mybot_config_t *config) {
    CHECK(config && !fixture.sdk_active && !fixture.prompt_active);
    fixture.sdk_active = true;
    fixture.active_checks = 0;
    ++fixture.starts;
    record(SDK_START);
    if (fixture.scenario == SDK_START_FAILURE_CLEAN)
        fixture.sdk_active = false;
    return fixture.scenario == SDK_START_FAILURE || fixture.scenario == SDK_START_FAILURE_CLEAN ? -1
                                                                                                : 0;
}

void mybot_stop(void) {
    /* The public SDK permits stop after an already-cleaned failed start. */
    CHECK(fixture.starts > 0);
    fixture.sdk_active = false;
    ++fixture.stops;
    record(SDK_STOP);
}

bool mybot_is_running(void) {
    return fixture.sdk_active;
}

mybot_state_t mybot_get_state(void) {
    if (!fixture.sdk_active)
        return MYBOT_STATE_STOPPED;
    return fixture.scenario == SDK_START_FAILURE ? MYBOT_STATE_FAILED : MYBOT_STATE_READY;
}

bool rv1126b_video_failed(void) {
    return fixture.scenario == VIDEO_FAILURE;
}

static void run(scenario_t scenario, int expected) {
    fixture = (fixture_t){.scenario = scenario};
    fixture_now = 1000;
    volatile sig_atomic_t stop = 0;
    const rv1126b_app_config_t options = {
        .platform = {.enable_ui = scenario != CLOCK_FAILURE, .network_interface = "fixture0"},
        .drm_device = "fixture",
        .rotation = 270,
        .seconds = 2,
        .demo = scenario == DEMO,
        .wifi_provisioning = true,
        .wifi_interface = "fixture0",
        .time_sync_timeout = 30,
    };
    const mybot_config_t sdk = {0};
    CHECK(rv1126b_controller_run(&options, &sdk, &stop) == expected);
    CHECK(!fixture.sdk_active && !fixture.prompt_active);
    if (scenario != DEMO) {
        CHECK(fixture.last_status == (expected ? RV1126B_APP_FAILED : RV1126B_APP_STOPPED));
        CHECK(position(VOLUME_START) < position(TIME_START));
        CHECK(position(TIME_STOP) < position(VOLUME_STOP));
    }
    if (fixture.stops && position(WIFI_STOP) >= 0)
        CHECK(position(SDK_STOP) < position(WIFI_STOP));
    if (scenario == NORMAL) {
        CHECK(fixture.starts == 1 && fixture.stops == 1);
        CHECK(position(UI_START) < position(INPUT_START) &&
              position(INPUT_START) < position(WIFI_START));
        CHECK(position(WIFI_START) < position(SDK_START));
        CHECK(fixture.statuses[RV1126B_APP_READY]);
    } else if (scenario == MANUAL || scenario == MANUAL_RETRY) {
        CHECK(fixture.starts == 2 && fixture.stops == 2 && fixture.ap_seen && fixture.prompts == 1);
        CHECK(position(SDK_STOP) < position(AP_REQUEST));
        CHECK(fixture.statuses[RV1126B_APP_AP]);
        if (scenario == MANUAL_RETRY)
            CHECK(fixture.wifi_requests == 2 && fixture.wifi_failed && fixture.error_seen);
    } else if (scenario == RECOVERY) {
        CHECK(fixture.starts == 2 && fixture.stops == 2 && fixture.recoveries == 1);
        CHECK(position(SDK_STOP) < position(RECOVERY_REQUEST));
    } else if (scenario == CLOCK_FAILURE) {
        CHECK(fixture.starts == 0 && fixture.clock_failed && position(UI_START) < 0);
    } else if (scenario == VIDEO_FAILURE) {
        CHECK(fixture.starts == 1 && fixture.stops == 1);
    } else if (scenario == INPUT_FAILURE) {
        CHECK(fixture.starts == 0 && position(WIFI_START) < 0 && position(INPUT_STOP) < 0);
        CHECK(position(UI_STOP) >= 0 && position(VOLUME_STOP) >= 0);
    } else if (scenario == SDK_START_FAILURE || scenario == SDK_START_FAILURE_CLEAN) {
        CHECK(fixture.starts == 1 && fixture.stops == 1);
        CHECK(!fixture.statuses[RV1126B_APP_READY]);
        CHECK(position(SDK_STOP) < position(TIME_STOP));
        CHECK(position(SDK_STOP) < position(INPUT_STOP));
        CHECK(position(SDK_STOP) < position(UI_STOP));
    } else if (scenario == POWEROFF || scenario == POWEROFF_DISPLAY_FAILURE ||
               scenario == POWEROFF_WAITING) {
        CHECK(fixture.power_sent && fixture.statuses[RV1126B_APP_POWERING_OFF]);
        CHECK(position(UI_STOP_FOR_POWEROFF) >= 0 && position(UI_STOP) < 0);
        CHECK(position(POWER_REQUEST) < position(UI_STOP_FOR_POWEROFF));
        if (scenario == POWEROFF_WAITING)
            CHECK(fixture.starts == 0 && fixture.stops == 0);
        else
            CHECK(position(POWER_REQUEST) < position(SDK_STOP));
    } else if (scenario == POWEROFF_REJECTED) {
        CHECK(fixture.power_sent && !fixture.statuses[RV1126B_APP_POWERING_OFF]);
        CHECK(position(UI_STOP_FOR_POWEROFF) < 0 && position(UI_STOP) >= 0);
    } else if (scenario == RESET_ACTIVE || scenario == RESET_WAITING) {
        CHECK(position(RESET_REQUEST) >= 0 && !fixture.power_sent);
        CHECK(fixture.statuses[RV1126B_APP_FACTORY_RESET]);
        CHECK(position(UI_STOP_FOR_POWEROFF) > position(RESET_REQUEST) && position(UI_STOP) < 0);
        if (scenario == RESET_ACTIVE)
            CHECK(position(SDK_STOP) > position(RESET_REQUEST));
        else
            CHECK(!fixture.starts);
    } else if (scenario == RESET_REJECTED) {
        CHECK(fixture.reset_rejected && position(RESET_REQUEST) >= 0);
        CHECK(position(UI_STOP_FOR_POWEROFF) < 0 && !fixture.statuses[RV1126B_APP_FACTORY_RESET]);
    } else if (scenario == FIRST_SETUP) {
        CHECK(fixture.ap_seen && position(SETUP_FINISHED) > position(WIFI_START));
        CHECK(position(SDK_START) > position(SETUP_FINISHED));
    } else if (scenario == FIRST_SETUP_ERROR) {
        CHECK(!fixture.starts && position(SETUP_FINISHED) < 0 && fixture.wifi_failed);
    } else if (scenario == DEMO) {
        CHECK(fixture.status_calls == 0 && fixture.starts == 0 && position(UI_STOP) >= 0);
        CHECK(position(VOLUME_START) < 0 && position(INPUT_START) < 0);
    } else if (scenario == OFFLINE_DEADLINE) {
        CHECK(fixture.starts == 0 && fixture.statuses[RV1126B_APP_WAITING_NETWORK]);
        CHECK(fixture_now >= 3000);
    }
}

int main(void) {
    CHECK(setenv("MYBOT_KV_STORE_DIR", "/fixture/storage", 1) == 0);
    CHECK(setenv("MYBOT_ASSETS_DIR", "/fixture/assets", 1) == 0);
    CHECK(setenv("MYBOT_LOCALE", "zh-CN", 1) == 0);
    CHECK(setenv("MYBOT_SUPERVISOR_RUNTIME", "/fixture/supervisor", 1) == 0);
    for (scenario_t scenario = NORMAL; scenario <= OFFLINE_DEADLINE; ++scenario)
        run(scenario, scenario == CLOCK_FAILURE || scenario == VIDEO_FAILURE ||
                          scenario == INPUT_FAILURE || scenario == SDK_START_FAILURE ||
                          scenario == SDK_START_FAILURE_CLEAN ||
                          scenario == POWEROFF_DISPLAY_FAILURE);
    CHECK(sdk_status(MYBOT_STATE_PAIRING) == RV1126B_APP_PAIRING);
    CHECK(sdk_status(MYBOT_STATE_IN_CONVERSATION) == RV1126B_APP_CONVERSATION);
    CHECK(sdk_status(MYBOT_STATE_WIFI_DISCONNECTED) == RV1126B_APP_OFFLINE);
    puts("Product lifecycle ordering, setup retry/recovery, failure cleanup and demo: PASS");
    return 0;
}
