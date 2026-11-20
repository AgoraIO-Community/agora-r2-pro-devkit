/* SPDX-License-Identifier: Apache-2.0 */
#include "check.h"
#include "wifi_service.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fake's complete state is mutex-protected: test controls race with the
 * service worker deliberately, without introducing C data races themselves. */
typedef struct {
    pthread_mutex_t mutex;
    unsigned initialized, ap_starts, ap_stops, connections, saves, portal_starts, portal_stops,
        scans;
    unsigned ready_calls, probe_spacing, loads, invalid_profiles;
    uint64_t last_probe;
    wifi_service_t *probe_status;
    unsigned committed_checks, ap, init_result, route_on_init, ap_ok, portal_ok, connection_ok,
        routed, concurrent;
    unsigned block_connect, block_save, save_ok, reload_after_save, committed_ready, committed_ok;
    unsigned block_portal_stop, wrong_thread, owner_set, ready_mode;
    unsigned system_station, station_checks;
    pthread_t owner;
    wifi_credentials_t disk;
    wifi_credential_t attempted[32];
    unsigned timeouts[32];
} fake_state_t;
static unsigned get(fake_state_t *s, size_t offset) {
    pthread_mutex_lock(&s->mutex);
    const unsigned result = *(unsigned *)((char *)s + offset);
    pthread_mutex_unlock(&s->mutex);
    return result;
}
static void put(fake_state_t *s, size_t offset, unsigned value) {
    pthread_mutex_lock(&s->mutex);
    *(unsigned *)((char *)s + offset) = value;
    pthread_mutex_unlock(&s->mutex);
}
#define GET(s, field) get((s), offsetof(fake_state_t, field))
#define SET(s, field, value) put((s), offsetof(fake_state_t, field), (value))
static void owned(fake_state_t *s) {
    pthread_mutex_lock(&s->mutex);
    if (!s->owner_set) {
        s->owner = pthread_self();
        s->owner_set = 1;
    } else if (!pthread_equal(s->owner, pthread_self()))
        s->wrong_thread = 1;
    pthread_mutex_unlock(&s->mutex);
}
static void increment(fake_state_t *s, size_t offset) {
    owned(s);
    pthread_mutex_lock(&s->mutex);
    ++*(unsigned *)((char *)s + offset);
    pthread_mutex_unlock(&s->mutex);
}
#define INCREMENT(s, field) increment((s), offsetof(fake_state_t, field))
static wifi_backend_init_result_t fake_initialize(void *context, char *error) {
    fake_state_t *s = context;
    INCREMENT(s, initialized);
    const wifi_backend_init_result_t result = GET(s, init_result);
    if (result == WIFI_BACKEND_READY && GET(s, route_on_init))
        SET(s, routed, 1);
    if (result == WIFI_BACKEND_RETRY)
        wifi_error(error, "Waiting for wireless interface");
    else
        wifi_error(error, result == WIFI_BACKEND_READY ? "" : "Missing AP tools");
    return result;
}

static const char *fake_ssid(void *context) {
    (void)context;
    return "mybot-test";
}
static bool fake_start_ap(void *context, char *error) {
    fake_state_t *s = context;
    INCREMENT(s, ap_starts);
    const bool ok = GET(s, ap_ok) != 0;
    SET(s, ap, ok);
    wifi_error(error, ok ? "" : "Access point failure");
    return ok;
}
static void fake_stop_ap(void *context) {
    fake_state_t *s = context;
    INCREMENT(s, ap_stops);
    SET(s, ap, 0);
}
static bool fake_ap_running(void *context) {
    return GET((fake_state_t *)context, ap) != 0;
}
static bool fake_concurrent(void *context) {
    return GET((fake_state_t *)context, concurrent) != 0;
}
static bool fake_online(void *context) {
    return GET((fake_state_t *)context, routed) != 0;
}
static bool fake_scan(void *context, wifi_scan_t *scan, char *error) {
    fake_state_t *s = context;
    (void)error;
    INCREMENT(s, scans);
    *scan = (wifi_scan_t){1, {{"test-network", -50, 3, 6}}};
    return true;
}
static bool fake_load(void *context, wifi_credentials_t *profiles, char *error) {
    fake_state_t *s = context;
    INCREMENT(s, loads);
    if (GET(s, invalid_profiles)) {
        wifi_error(error, "Malformed system Wi-Fi configuration");
        return false;
    }
    pthread_mutex_lock(&s->mutex);
    *profiles = s->disk;
    pthread_mutex_unlock(&s->mutex);
    return true;
}
static bool fake_save(void *context, const wifi_credentials_t *profiles, char *error) {
    fake_state_t *s = context;
    INCREMENT(s, saves);
    while (GET(s, block_save))
        wifi_sleep_ms(1);
    if (!GET(s, save_ok)) {
        wifi_error(error, "Cannot persist network");
        return false;
    }
    pthread_mutex_lock(&s->mutex);
    s->disk = *profiles;
    if (s->reload_after_save)
        s->committed_ready = 0;
    pthread_mutex_unlock(&s->mutex);
    return true;
}
static bool fake_wait_committed(void *context, const wifi_credential_t *profile, unsigned timeout,
                                const wifi_cancel_t *cancel, char *error) {
    fake_state_t *s = context;
    (void)profile;
    INCREMENT(s, committed_checks);
    const uint64_t deadline = wifi_now_ms() + timeout;
    while (GET(s, committed_ok) && (!GET(s, committed_ready) || !GET(s, routed)) &&
           !wifi_cancelled(cancel) && wifi_now_ms() < deadline)
        wifi_sleep_ms(1);
    if (wifi_cancelled(cancel)) {
        wifi_error(error, "Cancelled");
        return false;
    }
    const bool ok = GET(s, committed_ok) && GET(s, committed_ready) && GET(s, routed);
    wifi_error(error, ok ? "" : "Committed network did not settle");
    return ok;
}
static bool fake_connect(void *context, const wifi_credential_t *profile, unsigned timeout,
                         const wifi_cancel_t *cancel, char *error) {
    fake_state_t *s = context;
    INCREMENT(s, connections);
    pthread_mutex_lock(&s->mutex);
    if (!s->concurrent)
        s->ap = 0;
    if (s->connections <= 32) {
        s->attempted[s->connections - 1] = *profile;
        s->timeouts[s->connections - 1] = timeout;
    }
    pthread_mutex_unlock(&s->mutex);
    const uint64_t deadline = wifi_now_ms() + timeout;
    while (GET(s, block_connect) && !wifi_cancelled(cancel) && wifi_now_ms() < deadline)
        wifi_sleep_ms(1);
    if (wifi_cancelled(cancel)) {
        wifi_error(error, "Cancelled");
        return false;
    }
    const bool ok = GET(s, connection_ok) != 0;
    wifi_error(error, ok ? "" : "Connection failed");
    return ok;
}
static bool fake_portal_start(void *context, const wifi_portal_handler_t *handler, char *error) {
    fake_state_t *s = context;
    CHECK(handler && handler->ops && handler->context);
    INCREMENT(s, portal_starts);
    const bool ok = GET(s, portal_ok) != 0;
    wifi_error(error, ok ? "" : "HTTP/DNS bind failed");
    return ok;
}
static void fake_portal_stop(void *context) {
    fake_state_t *s = context;
    INCREMENT(s, portal_stops);
    while (GET(s, block_portal_stop))
        wifi_sleep_ms(1);
}
static void fake_destroy(void *context) {
    (void)context;
}

static bool fake_system_station(void *context) {
    fake_state_t *s = context;
    INCREMENT(s, station_checks);
    return GET(s, system_station) != 0;
}

static const wifi_backend_ops_t backend_ops = {
    fake_initialize,     fake_ssid,    fake_start_ap, fake_stop_ap,       fake_ap_running,
    fake_concurrent,     fake_online,  fake_scan,     fake_load,          fake_save,
    fake_wait_committed, fake_connect, fake_destroy,  fake_system_station};
static const wifi_portal_ops_t portal_ops = {fake_portal_start, fake_portal_stop, fake_destroy};
static bool fake_ready(void *context) {
    fake_state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    ++s->ready_calls;
    uint64_t now = wifi_now_ms();
    s->probe_spacing = (unsigned)(now - s->last_probe);
    s->last_probe = now;
    if (s->probe_status) {
        rv1126b_wifi_status_t snapshot;
        wifi_service_snapshot(s->probe_status, &snapshot);
    }
    const bool result = s->routed && (s->ready_mode || (s->connections > 0 && s->connection_ok));
    pthread_mutex_unlock(&s->mutex);
    return result;
}
static wifi_service_timing_t timing(void) {
    return (wifi_service_timing_t){.connect_budget_ms = 60,
                                   .attempt_ms = 30,
                                   .commit_budget_ms = 30,
                                   .submit_grace_ms = 5,
                                   .finish_grace_ms = 20,
                                   .success_hold_ms = 1000,
                                   .scan_interval_ms = 1000,
                                   .offline_grace_ms = 20,
                                   .route_interval_ms = 10,
                                   .init_budget_ms = 1000,
                                   .init_retry_ms = 10,
                                   .station_grace_ms = 80};
}

static fake_state_t *new_state(void) {
    fake_state_t *s = calloc(1, sizeof(*s));
    CHECK(s && !pthread_mutex_init(&s->mutex, NULL));
    s->init_result = WIFI_BACKEND_READY;
    s->ap_ok = s->portal_ok = s->routed = s->concurrent = 1;
    s->save_ok = s->committed_ready = s->committed_ok = 1;
    return s;
}
static wifi_service_t *make_service(fake_state_t *s, bool enabled, bool force_ap, bool ready,
                                    const wifi_service_timing_t *deadlines) {
    SET(s, ready_mode, ready);
    wifi_backend_t backend = {&backend_ops, s};
    wifi_portal_t portal = {&portal_ops, s};
    const wifi_service_config_t config = {enabled, force_ap};
    const wifi_service_timing_t defaults = timing();
    wifi_service_t *service = wifi_service_create(&config, &backend, &portal, fake_ready, s,
                                                  deadlines ? deadlines : &defaults);
    CHECK(service && !backend.ops && !portal.ops);
    return service;
}
static void close_service(wifi_service_t *service, fake_state_t *s) {
    wifi_service_destroy(service);
    CHECK(!GET(s, wrong_thread));
    CHECK(!pthread_mutex_destroy(&s->mutex));
    free(s);
}
static wifi_credential_t profile(const char *ssid) {
    wifi_credential_t result = {0};
    CHECK(wifi_copy(result.ssid, sizeof(result.ssid), ssid));
    memset(result.psk, 'a', 64);
    return result;
}
static void add_profile(fake_state_t *s, const char *ssid) {
    pthread_mutex_lock(&s->mutex);
    CHECK(s->disk.count < WIFI_MAX_NETWORKS);
    s->disk.items[s->disk.count++] = profile(ssid);
    pthread_mutex_unlock(&s->mutex);
}
static rv1126b_wifi_status_t snapshot(wifi_service_t *s) {
    rv1126b_wifi_status_t result;
    wifi_service_snapshot(s, &result);
    return result;
}
static wifi_portal_status_t status(wifi_service_t *s) {
    wifi_portal_status_t result;
    wifi_service_status(s, &result);
    return result;
}
static wifi_saved_names_t names(wifi_service_t *s) {
    wifi_saved_names_t result;
    wifi_service_saved_networks(s, &result);
    return result;
}
static bool has_networks(wifi_service_t *s) {
    wifi_scan_t scan;
    wifi_service_networks(s, &scan);
    return scan.count != 0;
}
static bool portal_state(wifi_service_t *s, const char *state) {
    const wifi_portal_status_t result = status(s);
    return !strcmp(result.state, state);
}
static bool first_name(wifi_service_t *s, const char *ssid) {
    const wifi_saved_names_t result = names(s);
    return result.count && !strcmp(result.items[0], ssid);
}
#define AWAIT(expression)                                                                          \
    do {                                                                                           \
        const uint64_t await_deadline = wifi_now_ms() + 1500;                                      \
        while (!(expression) && wifi_now_ms() < await_deadline)                                    \
            wifi_sleep_ms(1);                                                                      \
        CHECK(expression);                                                                         \
    } while (0)
static void wait_ap(wifi_service_t *service) {
    AWAIT(snapshot(service).state == RV1126B_WIFI_AP_READY);
    const rv1126b_wifi_status_t value = snapshot(service);
    CHECK(value.ap_active && !strcmp(value.ssid, "mybot-test"));
}
static void test_disabled_and_existing_network(void) {
    fake_state_t *s = new_state();
    wifi_service_t *service = make_service(s, false, false, true, NULL);
    CHECK(wifi_service_start(service));
    CHECK(!wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    wifi_service_request_provisioning(service);
    wifi_service_stop(service);
    CHECK(!GET(s, initialized) && !GET(s, ap_starts) && !GET(s, portal_starts));
    CHECK(!GET(s, ap_stops) && !GET(s, portal_stops));
    close_service(service, s);

    s = new_state();
    SET(s, init_result, WIFI_BACKEND_FAILED);
    service = make_service(s, true, false, true, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(GET(s, initialized) == 1);
    SET(s, init_result, WIFI_BACKEND_READY);
    wifi_service_request_provisioning(service);
    CHECK(snapshot(service).state != RV1126B_WIFI_ONLINE);
    wait_ap(service);
    CHECK(GET(s, initialized) == 2);
    close_service(service, s);
}

static void test_boot_uses_route_acquired_during_initialization(void) {
    for (unsigned route_on_init = 0; route_on_init <= 1; ++route_on_init) {
        fake_state_t *s = new_state();
        add_profile(s, "first");
        add_profile(s, "second");
        SET(s, routed, 0);
        SET(s, init_result, WIFI_BACKEND_RETRY);
        SET(s, route_on_init, route_on_init);
        wifi_service_t *service = make_service(s, true, false, true, NULL);
        CHECK(wifi_service_start(service));
        AWAIT(GET(s, initialized) >= 2);
        CHECK(!GET(s, loads));
        const rv1126b_wifi_status_t waiting = snapshot(service);
        CHECK(waiting.state == RV1126B_WIFI_WAITING && !waiting.error[0] && !waiting.ap_active);
        if (route_on_init)
            SET(s, init_result, WIFI_BACKEND_READY);
        else
            SET(s, routed, 1);
        AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
        CHECK(GET(s, loads) == route_on_init);
        CHECK(!GET(s, scans) && !GET(s, connections) && !GET(s, ap_starts) && !GET(s, saves));
        SET(s, routed, 0);
        AWAIT(snapshot(service).state == RV1126B_WIFI_RECOVERY_REQUIRED);
        close_service(service, s);
    }
}

static void test_transient_init_timeout_and_stop(void) {
    fake_state_t *s = new_state();
    SET(s, init_result, WIFI_BACKEND_RETRY);
    SET(s, routed, 0);
    wifi_service_timing_t deadlines = timing();
    deadlines.init_budget_ms = 80;
    wifi_service_t *service = make_service(s, true, false, true, &deadlines);
    const uint64_t started = wifi_now_ms();
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ERROR);
    CHECK(wifi_now_ms() - started >= deadlines.init_budget_ms && GET(s, initialized) >= 2);
    CHECK(strstr(snapshot(service).error, "Waiting for wireless interface"));
    CHECK(!GET(s, connections) && !GET(s, ap_starts));
    SET(s, routed, 1);
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(!snapshot(service).error[0]);
    SET(s, routed, 0);
    AWAIT(snapshot(service).state == RV1126B_WIFI_RECOVERY_REQUIRED);
    close_service(service, s);

    s = new_state();
    SET(s, init_result, WIFI_BACKEND_RETRY);
    deadlines.init_budget_ms = 10000;
    deadlines.init_retry_ms = 5000;
    service = make_service(s, true, false, false, &deadlines);
    CHECK(wifi_service_start(service));
    AWAIT(GET(s, initialized) == 1);
    const uint64_t stopping = wifi_now_ms();
    wifi_service_stop(service);
    CHECK(wifi_now_ms() - stopping < 500 && GET(s, initialized) == 1);
    CHECK(snapshot(service).state == RV1126B_WIFI_WAITING);
    close_service(service, s);
}

static void test_boot_allows_system_station_to_obtain_route(void) {
    for (unsigned external_route = 0; external_route <= 1; ++external_route) {
        fake_state_t *s = new_state();
        add_profile(s, "first");
        add_profile(s, "second");
        SET(s, system_station, 1);
        SET(s, routed, 0);
        wifi_service_timing_t deadlines = timing();
        if (external_route)
            deadlines.station_grace_ms = 1000;
        wifi_service_t *service = make_service(s, true, false, true, &deadlines);
        const uint64_t started = wifi_now_ms();
        CHECK(wifi_service_start(service));
        if (external_route) {
            AWAIT(GET(s, station_checks) > 0);
            CHECK(snapshot(service).state == RV1126B_WIFI_WAITING);
            SET(s, routed, 1);
            AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
            CHECK(!GET(s, scans) && !GET(s, connections) && !GET(s, ap_starts));
        } else {
            wait_ap(service);
            CHECK(wifi_now_ms() - started >= deadlines.station_grace_ms);
            CHECK(GET(s, connections) == 2);
        }
        CHECK(!GET(s, saves));
        close_service(service, s);
    }
}

static void test_forced_provisioning_waits_for_backend_despite_existing_route(void) {
    fake_state_t *s = new_state();
    add_profile(s, "retained");
    SET(s, init_result, WIFI_BACKEND_RETRY);
    wifi_service_t *service = make_service(s, true, true, true, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(GET(s, initialized) >= 2);
    CHECK(snapshot(service).state == RV1126B_WIFI_WAITING && !GET(s, ap_starts));
    SET(s, init_result, WIFI_BACKEND_READY);
    wait_ap(service);
    CHECK(!GET(s, connections) && GET(s, portal_starts) == 1 && first_name(service, "retained"));
    close_service(service, s);
}

static void test_online_route_sampling(void) {
    fake_state_t *s = new_state();
    wifi_service_timing_t deadlines = timing();
    deadlines.route_interval_ms = 100;
    wifi_service_t *service = make_service(s, true, false, true, &deadlines);
    s->probe_status = service;
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE && GET(s, ready_calls) >= 2);
    AWAIT(GET(s, ready_calls) >= 3);
    CHECK(GET(s, probe_spacing) >= deadlines.route_interval_ms);
    SET(s, routed, 0);
    AWAIT(snapshot(service).state == RV1126B_WIFI_RECOVERY_REQUIRED);
    close_service(service, s);
}

static void test_offline_recovery_requires_sdk_acknowledgement(void) {
    fake_state_t *s = new_state();
    add_profile(s, "saved");
    wifi_service_t *service = make_service(s, true, false, true, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(names(service).count == 1);
    SET(s, routed, 0);
    AWAIT(snapshot(service).state == RV1126B_WIFI_RECOVERY_REQUIRED);
    CHECK(!GET(s, ap_starts) && !GET(s, connections));
    wifi_service_request_recovery(service);
    wait_ap(service);
    CHECK(GET(s, connections) == 1 && GET(s, ap_starts) == 1);
    SET(s, routed, 1);
    SET(s, connection_ok, 1);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "saved", "password123", error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_VERIFYING);
    CHECK(wifi_service_finish(service, error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    SET(s, routed, 0);
    AWAIT(snapshot(service).state == RV1126B_WIFI_RECOVERY_REQUIRED);
    const unsigned starts = GET(s, ap_starts);
    SET(s, routed, 1);
    wifi_service_request_recovery(service);
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(GET(s, ap_starts) == starts);
    close_service(service, s);
}
static void test_ap_and_portal_rollback(void) {
    for (unsigned portal_failure = 0; portal_failure < 2; ++portal_failure) {
        fake_state_t *s = new_state();
        SET(s, ap_ok, portal_failure);
        SET(s, portal_ok, 0);
        wifi_service_t *service = make_service(s, true, false, false, NULL);
        CHECK(wifi_service_start(service));
        AWAIT(snapshot(service).state == RV1126B_WIFI_ERROR);
        CHECK(!snapshot(service).ap_active);
        CHECK(GET(s, portal_starts) == portal_failure);
        CHECK(status(service).error_code == WIFI_PORTAL_ERROR_AP);
        CHECK(GET(s, ap_stops) >= 1 && !GET(s, ap));
        close_service(service, s);
    }
    /* C backends report failures through return values, never exceptions. */
    fake_state_t *s = new_state();
    SET(s, init_result, WIFI_BACKEND_FAILED);
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ERROR);
    CHECK(!snapshot(service).ap_active);
    CHECK(GET(s, initialized) == 1 && !strcmp(snapshot(service).error, "Missing AP tools"));
    close_service(service, s);

    s = new_state();
    SET(s, invalid_profiles, 1);
    service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ERROR);
    CHECK(GET(s, loads) == 1 &&
          !strcmp(snapshot(service).error, "Malformed system Wi-Fi configuration"));
    close_service(service, s);
}

static void test_ap_exit_withdraws_guidance_before_cleanup_and_allows_retry(void) {
    fake_state_t *s = new_state();
    add_profile(s, "retained");
    wifi_service_timing_t deadlines = timing();
    deadlines.scan_interval_ms = 60000;
    wifi_service_t *service = make_service(s, true, true, false, &deadlines);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    AWAIT(GET(s, scans) == 1);
    SET(s, block_portal_stop, 1);
    SET(s, ap, 0);
    AWAIT(GET(s, portal_stops) == 1);
    const rv1126b_wifi_status_t failed = snapshot(service);
    const wifi_portal_status_t portal = status(service);
    CHECK(failed.state == RV1126B_WIFI_ERROR && !failed.ap_active && !failed.ssid[0]);
    CHECK(!strcmp(portal.state, "error") && !portal.busy && !portal.ssid[0]);
    CHECK(failed.error[0] && !strcmp(failed.error, portal.error));
    CHECK(portal.error_code == WIFI_PORTAL_ERROR_AP);
    CHECK(GET(s, ap_stops) == 0 && GET(s, scans) == 1);
    CHECK(first_name(service, "retained") && GET(s, saves) == 0);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(!wifi_service_submit(service, "new", "password123", error));
    CHECK(!wifi_service_saved_action(service, "delete", 0, "retained", error));
    CHECK(!wifi_service_finish(service, error));
    SET(s, block_portal_stop, 0);
    AWAIT(GET(s, ap_stops) == 1);
    wifi_sleep_ms(350);
    CHECK(GET(s, ap_starts) == 1 && snapshot(service).state == RV1126B_WIFI_ERROR);

    SET(s, ap_ok, 0);
    wifi_service_request_provisioning(service);
    AWAIT(GET(s, ap_starts) == 2 && snapshot(service).state == RV1126B_WIFI_ERROR);
    wifi_sleep_ms(350);
    CHECK(GET(s, ap_starts) == 2 && !snapshot(service).ap_active);
    SET(s, ap_ok, 1);
    wifi_service_request_provisioning(service);
    wait_ap(service);
    CHECK(GET(s, ap_starts) == 3 && GET(s, portal_starts) == 2);
    CHECK(first_name(service, "retained") && GET(s, saves) == 0);
    close_service(service, s);
}

static void test_single_interface_connect_and_verify_do_not_require_ap(void) {
    fake_state_t *s = new_state();
    SET(s, concurrent, 0);
    SET(s, connection_ok, 1);
    SET(s, block_connect, 1);
    wifi_service_timing_t deadlines = timing();
    deadlines.connect_budget_ms = deadlines.attempt_ms = 1000;
    deadlines.success_hold_ms = 5000;
    wifi_service_t *service = make_service(s, true, true, false, &deadlines);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(GET(s, connections) == 1 && !snapshot(service).ap_active);
    wifi_sleep_ms(350);
    CHECK(snapshot(service).state == RV1126B_WIFI_CONNECTING && GET(s, ap_stops) == 0);
    SET(s, block_connect, 0);
    AWAIT(snapshot(service).state == RV1126B_WIFI_VERIFYING);
    wifi_sleep_ms(350);
    CHECK(snapshot(service).state == RV1126B_WIFI_VERIFYING && !snapshot(service).ap_active);
    CHECK(GET(s, ap_stops) == 0 && GET(s, ap_starts) == 1);
    CHECK(wifi_service_finish(service, error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    close_service(service, s);
}

static void test_saved_attempts_and_deadline(void) {
    fake_state_t *s = new_state();
    add_profile(s, "one");
    add_profile(s, "two");
    add_profile(s, "three");
    SET(s, block_connect, 1);
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    const uint64_t before = wifi_now_ms();
    CHECK(wifi_service_start(service));
    wait_ap(service);
    CHECK(GET(s, connections) == 3 && wifi_now_ms() - before < 250);
    CHECK(!GET(s, saves));
    close_service(service, s);

    s = new_state();
    add_profile(s, "known");
    SET(s, connection_ok, 1);
    service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(GET(s, connections) == 1 && !GET(s, ap_starts) && !GET(s, portal_starts));
    close_service(service, s);
}
static void test_candidate_failure_then_success_and_grace(void) {
    fake_state_t *s = new_state();
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    AWAIT(has_networks(service));
    wifi_scan_t scan;
    wifi_service_networks(service, &scan);
    CHECK(scan.count == 1 && !strcmp(scan.items[0].ssid, "test-network"));
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(!wifi_service_submit(service, "test", "short", error));
    CHECK(!GET(s, connections));
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(GET(s, connections) == 2);
    wait_ap(service);
    CHECK(portal_state(service, "error") && !GET(s, saves));
    CHECK(status(service).error_code == WIFI_PORTAL_ERROR_CONNECT);
    SET(s, connection_ok, 1);
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(portal_state(service, "connected"));
    CHECK(snapshot(service).state == RV1126B_WIFI_VERIFYING && snapshot(service).ap_active);
    CHECK(GET(s, saves) == 1);
    CHECK(!wifi_service_submit(service, "other", "password123", error));
    pthread_mutex_lock(&s->mutex);
    CHECK(s->disk.count == 1 && !strcmp(s->disk.items[0].ssid, "test"));
    CHECK(strlen(s->disk.items[0].psk) == 64 && strcmp(s->disk.items[0].psk, "password123"));
    pthread_mutex_unlock(&s->mutex);
    const uint64_t before = wifi_now_ms();
    CHECK(wifi_service_finish(service, error));
    CHECK(snapshot(service).ap_active);
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(wifi_now_ms() - before >= 20 && !snapshot(service).ap_active);
    wifi_service_request_provisioning(service);
    CHECK(snapshot(service).state != RV1126B_WIFI_ONLINE);
    wait_ap(service);
    CHECK(portal_state(service, "ap") && !status(service).error[0]);
    CHECK(!wifi_service_finish(service, error));
    close_service(service, s);
}
static void test_requires_routed_ip_and_single_interface_recovery(void) {
    fake_state_t *s = new_state();
    SET(s, concurrent, 0);
    SET(s, connection_ok, 1);
    SET(s, routed, 0);
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(GET(s, connections) == 2);
    wait_ap(service);
    CHECK(GET(s, ap_starts) == 2 && GET(s, portal_starts) == 1 && !GET(s, saves));
    SET(s, routed, 1);
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(portal_state(service, "connected"));
    CHECK(!snapshot(service).ap_active);
    close_service(service, s);
}
static void test_saved_mutations_and_mru(void) {
    fake_state_t *s = new_state();
    for (unsigned i = 0; i < 10; ++i) {
        char ssid[20];
        snprintf(ssid, sizeof(ssid), "network-%u", i);
        add_profile(s, ssid);
    }
    wifi_service_t *service = make_service(s, true, true, true, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    CHECK(!GET(s, connections));
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(!wifi_service_saved_action(service, "delete", 10, "missing", error));
    CHECK(!wifi_service_saved_action(service, "unknown", 0, "network-0", error));
    SET(s, block_save, 1);
    CHECK(wifi_service_saved_action(service, "set_default", 4, "network-4", error));
    AWAIT(GET(s, saves) == 1);
    CHECK(status(service).busy && first_name(service, "network-0"));
    CHECK(!wifi_service_saved_action(service, "delete", 0, "network-0", error));
    CHECK(!wifi_service_submit(service, "new", "password123", error));
    SET(s, block_save, 0);
    AWAIT(first_name(service, "network-4"));
    CHECK(!status(service).busy && portal_state(service, "ap"));
    CHECK(!wifi_service_saved_action(service, "delete", 0, "network-0", error));
    CHECK(!wifi_service_saved_action(service, "set_default", 0, "network-0", error));
    CHECK(first_name(service, "network-4") && names(service).count == 10 && GET(s, saves) == 1);
    CHECK(wifi_service_saved_action(service, "delete", 0, "network-4", error));
    AWAIT(names(service).count == 9);
    SET(s, connection_ok, 1);
    CHECK(wifi_service_submit(service, "new", "password123", error));
    AWAIT(portal_state(service, "connected"));
    CHECK(names(service).count == 10 && first_name(service, "new"));
    CHECK(wifi_service_finish(service, error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    wifi_service_request_provisioning(service);
    CHECK(snapshot(service).state != RV1126B_WIFI_ONLINE);
    wait_ap(service);
    CHECK(wifi_service_submit(service, "network-7", "password456", error));
    AWAIT(portal_state(service, "connected"));
    const wifi_saved_names_t saved = names(service);
    unsigned matches = 0;
    for (size_t i = 0; i < saved.count; ++i)
        matches += !strcmp(saved.items[i], "network-7");
    CHECK(saved.count == 10 && !strcmp(saved.items[0], "network-7") && matches == 1);
    close_service(service, s);
}
static void test_stop_cancels_and_auto_finish(void) {
    fake_state_t *s = new_state();
    SET(s, block_connect, 1);
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(GET(s, connections) == 1);
    const uint64_t before = wifi_now_ms();
    wifi_service_stop(service);
    CHECK(wifi_now_ms() - before < 100 && !snapshot(service).ap_active && !GET(s, saves));
    CHECK(!wifi_service_submit(service, "test", "password123", error));
    CHECK(!wifi_service_saved_action(service, "delete", 0, "test", error));
    close_service(service, s);

    s = new_state();
    SET(s, connection_ok, 1);
    wifi_service_timing_t deadlines = timing();
    deadlines.success_hold_ms = 30;
    service = make_service(s, true, false, false, &deadlines);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(!snapshot(service).ap_active);
    close_service(service, s);
}
static void test_manual_request_during_close_and_late_finish(void) {
    fake_state_t *s = new_state();
    SET(s, connection_ok, 1);
    wifi_service_t *service = make_service(s, true, false, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(portal_state(service, "connected"));
    SET(s, block_portal_stop, 1);
    CHECK(wifi_service_finish(service, error));
    AWAIT(GET(s, portal_stops) == 1);
    wifi_service_request_provisioning(service);
    CHECK(snapshot(service).state == RV1126B_WIFI_WAITING);
    SET(s, block_portal_stop, 0);
    const uint64_t deadline = wifi_now_ms() + 1500;
    while (snapshot(service).state != RV1126B_WIFI_AP_READY && wifi_now_ms() < deadline) {
        CHECK(snapshot(service).state != RV1126B_WIFI_ONLINE);
        wifi_sleep_ms(1);
    }
    wait_ap(service);
    close_service(service, s);

    s = new_state();
    SET(s, connection_ok, 1);
    wifi_service_timing_t deadlines = timing();
    deadlines.success_hold_ms = 20;
    deadlines.finish_grace_ms = 40;
    service = make_service(s, true, false, false, &deadlines);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(portal_state(service, "connected"));
    const uint64_t before = wifi_now_ms();
    CHECK(wifi_service_finish(service, error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(wifi_now_ms() - before >= 40);
    close_service(service, s);
}
static void test_failed_persistence_does_not_change_cache(void) {
    fake_state_t *s = new_state();
    add_profile(s, "old");
    SET(s, save_ok, 0);
    wifi_service_t *service = make_service(s, true, true, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    SET(s, block_save, 1);
    CHECK(wifi_service_saved_action(service, "delete", 0, "old", error));
    AWAIT(GET(s, saves) == 1);
    CHECK(status(service).busy && names(service).count == 1);
    SET(s, block_save, 0);
    AWAIT(!status(service).busy);
    CHECK(portal_state(service, "error") && status(service).error[0]);
    CHECK(status(service).error_code == WIFI_PORTAL_ERROR_SAVED_UPDATE);
    const rv1126b_wifi_status_t public_status = snapshot(service);
    const wifi_portal_status_t portal_status = status(service);
    CHECK(!strcmp(public_status.error, portal_status.error) && names(service).count == 1);
    SET(s, connection_ok, 1);
    CHECK(wifi_service_submit(service, "new", "password123", error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_AP_READY && portal_state(service, "error"));
    CHECK(snapshot(service).ap_active && first_name(service, "old") && status(service).error[0]);
    CHECK(status(service).error_code == WIFI_PORTAL_ERROR_SAVE);
    CHECK(!wifi_service_finish(service, error));
    SET(s, save_ok, 1);
    CHECK(wifi_service_submit(service, "new", "password123", error));
    AWAIT(portal_state(service, "connected"));
    CHECK(first_name(service, "new") && !status(service).error[0]);
    CHECK(status(service).error_code == WIFI_PORTAL_ERROR_NONE);
    CHECK(wifi_service_finish(service, error));
    close_service(service, s);
}
static void test_profile_reload_settles_before_phone_success(void) {
    fake_state_t *s = new_state();
    SET(s, connection_ok, 1);
    SET(s, reload_after_save, 1);
    wifi_service_timing_t deadlines = timing();
    deadlines.commit_budget_ms = 500;
    wifi_service_t *service = make_service(s, true, false, false, &deadlines);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "test", "password123", error));
    AWAIT(GET(s, committed_checks) == 1);
    CHECK(GET(s, routed) && portal_state(service, "connecting") && status(service).busy);
    CHECK(snapshot(service).state == RV1126B_WIFI_CONNECTING && snapshot(service).ap_active);
    CHECK(names(service).count == 1 && first_name(service, "test"));
    CHECK(!wifi_service_finish(service, error));
    SET(s, routed, 0);
    wifi_sleep_ms(10);
    CHECK(portal_state(service, "connecting"));
    SET(s, routed, 1);
    wifi_sleep_ms(10);
    CHECK(portal_state(service, "connecting"));
    SET(s, committed_ready, 1);
    AWAIT(portal_state(service, "connected"));
    CHECK(snapshot(service).state == RV1126B_WIFI_VERIFYING);
    CHECK(GET(s, saves) == 1 && GET(s, committed_checks) == 1);
    CHECK(wifi_service_finish(service, error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_ONLINE);
    CHECK(!snapshot(service).ap_active);
    close_service(service, s);
}
static void test_profile_reload_timeout_retains_committed_cache_and_cancels(void) {
    fake_state_t *s = new_state();
    add_profile(s, "old");
    SET(s, connection_ok, 1);
    SET(s, reload_after_save, 1);
    wifi_service_t *service = make_service(s, true, true, false, NULL);
    CHECK(wifi_service_start(service));
    wait_ap(service);
    char error[WIFI_ERROR_CAPACITY] = "";
    CHECK(wifi_service_submit(service, "new", "password123", error));
    AWAIT(snapshot(service).state == RV1126B_WIFI_AP_READY && portal_state(service, "error"));
    CHECK(snapshot(service).ap_active && !status(service).busy && first_name(service, "new"));
    CHECK(GET(s, saves) == 1 && GET(s, committed_checks) == 1);
    CHECK(status(service).error_code == WIFI_PORTAL_ERROR_COMMIT);
    pthread_mutex_lock(&s->mutex);
    CHECK(!strcmp(s->disk.items[0].ssid, "new"));
    pthread_mutex_unlock(&s->mutex);
    CHECK(!wifi_service_finish(service, error));
    CHECK(wifi_service_submit(service, "new", "password123", error));
    AWAIT(GET(s, committed_checks) == 2);
    const uint64_t before = wifi_now_ms();
    wifi_service_stop(service);
    CHECK(wifi_now_ms() - before < 100 && !snapshot(service).ap_active &&
          first_name(service, "new"));
    close_service(service, s);
}
static void test_public_lifecycle(void) {
    CHECK(rv1126b_wifi_start(NULL) == -1);
    const rv1126b_wifi_config_t disabled = {0};
    CHECK(rv1126b_wifi_start(&disabled) == 0);
    CHECK(rv1126b_wifi_start(&disabled) == -1);
    rv1126b_wifi_request_provisioning();
    rv1126b_wifi_request_recovery();
    rv1126b_wifi_status(NULL);
    rv1126b_wifi_stop();
    rv1126b_wifi_stop();
    rv1126b_wifi_status_t stopped;
    rv1126b_wifi_status(&stopped);
    CHECK(stopped.state == RV1126B_WIFI_WAITING && !stopped.ap_active);
}
int main(void) {
    test_disabled_and_existing_network();
    test_boot_uses_route_acquired_during_initialization();
    test_transient_init_timeout_and_stop();
    test_boot_allows_system_station_to_obtain_route();
    test_forced_provisioning_waits_for_backend_despite_existing_route();
    test_online_route_sampling();
    test_offline_recovery_requires_sdk_acknowledgement();
    test_ap_and_portal_rollback();
    test_ap_exit_withdraws_guidance_before_cleanup_and_allows_retry();
    test_single_interface_connect_and_verify_do_not_require_ap();
    test_saved_attempts_and_deadline();
    test_candidate_failure_then_success_and_grace();
    test_requires_routed_ip_and_single_interface_recovery();
    test_saved_mutations_and_mru();
    test_stop_cancels_and_auto_finish();
    test_failed_persistence_does_not_change_cache();
    test_profile_reload_settles_before_phone_success();
    test_profile_reload_timeout_retains_committed_cache_and_cancels();
    test_manual_request_during_close_and_late_finish();
    test_public_lifecycle();
    return 0;
}
