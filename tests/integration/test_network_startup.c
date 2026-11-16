/* SPDX-License-Identifier: Apache-2.0 */
#include "check.h"
#include "network_gate.h"
#include "wifi_service.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pthread_mutex_t mutex;
    bool routed, ap, lose_route_on_ap_stop;
    unsigned connections, ap_starts;
    wifi_credentials_t saved;
} state_t;
static bool initialize(void *context, char *error) {
    (void)context;
    wifi_error(error, "");
    return true;
}
static const char *ap_ssid(void *context) {
    (void)context;
    return "startup-test";
}
static bool start_ap(void *context, char *error) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    ++s->ap_starts;
    s->ap = true;
    pthread_mutex_unlock(&s->mutex);
    wifi_error(error, "");
    return true;
}
static void stop_ap(void *context) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    if (s->ap && s->lose_route_on_ap_stop)
        s->routed = false;
    s->ap = false;
    pthread_mutex_unlock(&s->mutex);
}
static bool ap_running(void *context) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    bool value = s->ap;
    pthread_mutex_unlock(&s->mutex);
    return value;
}
static bool concurrent(void *context) {
    (void)context;
    return true;
}
static bool online(void *context) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    bool value = s->routed;
    pthread_mutex_unlock(&s->mutex);
    return value;
}
static bool scan(void *context, wifi_scan_t *out, char *error) {
    (void)context;
    *out = (wifi_scan_t){0};
    wifi_error(error, "");
    return true;
}
static bool load(void *context, wifi_credentials_t *out, char *error) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    *out = s->saved;
    pthread_mutex_unlock(&s->mutex);
    wifi_error(error, "");
    return true;
}
static bool save(void *context, const wifi_credentials_t *profiles, char *error) {
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    s->saved = *profiles;
    pthread_mutex_unlock(&s->mutex);
    wifi_error(error, "");
    return true;
}
static bool wait_committed(void *context, const wifi_credential_t *credential, unsigned timeout,
                           const wifi_cancel_t *cancel, char *error) {
    (void)credential;
    (void)timeout;
    wifi_error(error, "");
    return !wifi_cancelled(cancel) && online(context);
}
static bool connect_network(void *context, const wifi_credential_t *credential, unsigned timeout,
                            const wifi_cancel_t *cancel, char *error) {
    (void)credential;
    (void)timeout;
    bool routed = !wifi_cancelled(cancel);
    state_t *s = context;
    pthread_mutex_lock(&s->mutex);
    ++s->connections;
    s->routed = routed;
    pthread_mutex_unlock(&s->mutex);
    wifi_error(error, "");
    return routed;
}
static void no_destroy(void *context) {
    (void)context;
}
static const wifi_backend_ops_t backend_ops = {
    initialize, ap_ssid, start_ap, stop_ap,        ap_running,      concurrent, online,
    scan,       load,    save,     wait_committed, connect_network, no_destroy};
static bool portal_start(void *context, const wifi_portal_handler_t *handler, char *error) {
    (void)context;
    (void)handler;
    wifi_error(error, "");
    return true;
}
static const wifi_portal_ops_t portal_ops = {portal_start, no_destroy, no_destroy};
static void await_state(wifi_service_t *service, rv1126b_wifi_state_t expected) {
    uint64_t deadline = wifi_now_ms() + 2000;
    rv1126b_wifi_status_t status;
    do {
        wifi_service_snapshot(service, &status);
        if (status.state == expected)
            return;
        wifi_sleep_ms(1);
    } while (wifi_now_ms() < deadline);
    CHECK(status.state == expected);
}
static void recovery_after_portal_closes(void) {
    state_t state = {
        .mutex = PTHREAD_MUTEX_INITIALIZER, .routed = true, .lose_route_on_ap_stop = true};
    wifi_backend_t backend = {&backend_ops, &state};
    wifi_portal_t portal = {&portal_ops, NULL};
    const wifi_service_config_t config = {.enabled = true, .force_ap = true};
    const wifi_service_timing_t timing = {.connect_budget_ms = 30000,
                                          .attempt_ms = 15000,
                                          .commit_budget_ms = 30000,
                                          .submit_grace_ms = 1,
                                          .finish_grace_ms = 1,
                                          .success_hold_ms = 5000,
                                          .scan_interval_ms = 15000,
                                          .offline_grace_ms = 10};
    wifi_service_t *service =
        wifi_service_create(&config, &backend, &portal, online, &state, &timing);
    CHECK(service && !backend.ops && !portal.ops && wifi_service_start(service));
    await_state(service, RV1126B_WIFI_AP_READY);
    char error[WIFI_ERROR_CAPACITY];
    CHECK(wifi_service_submit(service, "test-network", "password123", error));
    await_state(service, RV1126B_WIFI_VERIFYING);
    CHECK(wifi_service_finish(service, error));
    await_state(service, RV1126B_WIFI_RECOVERY_REQUIRED);
    rv1126b_wifi_status_t status;
    wifi_service_snapshot(service, &status);
    CHECK(!status.ap_active);
    /* A recovered physical route does not clear the latched recovery handshake. */
    pthread_mutex_lock(&state.mutex);
    state.routed = true;
    pthread_mutex_unlock(&state.mutex);
    rv1126b_network_gate_t gate = {0};
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
          RV1126B_NETWORK_GATE_RECOVER);
    wifi_service_request_recovery(service);
    await_state(service, RV1126B_WIFI_ONLINE);
    wifi_service_snapshot(service, &status);
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
          RV1126B_NETWORK_GATE_READY);
    pthread_mutex_lock(&state.mutex);
    CHECK(state.connections == 2 && state.ap_starts == 1);
    pthread_mutex_unlock(&state.mutex);
    wifi_service_destroy(service);
    CHECK(!pthread_mutex_destroy(&state.mutex));
}
static void acknowledgement_guards(void) {
    rv1126b_network_gate_t gate = {0};
    rv1126b_wifi_status_t status = {.state = RV1126B_WIFI_RECOVERY_REQUIRED};
    CHECK(rv1126b_network_gate_poll(&gate, true, true, true, 1, &status) ==
          RV1126B_NETWORK_GATE_WAIT);
    status.ap_active = true;
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
          RV1126B_NETWORK_GATE_WAIT);
    status.ap_active = false;
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
          RV1126B_NETWORK_GATE_RECOVER);
    for (unsigned i = 0; i < 10; ++i)
        CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
              RV1126B_NETWORK_GATE_WAIT);
    status.state = RV1126B_WIFI_WAITING;
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 1, &status) ==
          RV1126B_NETWORK_GATE_WAIT);
    status.state = RV1126B_WIFI_RECOVERY_REQUIRED;
    CHECK(rv1126b_network_gate_poll(&gate, true, true, false, 0, &status) ==
          RV1126B_NETWORK_GATE_RECOVER);
    gate = (rv1126b_network_gate_t){0};
    CHECK(rv1126b_network_gate_poll(&gate, false, false, false, 1, &status) ==
          RV1126B_NETWORK_GATE_READY);
    CHECK(!gate.recovery_requested);
}
int main(void) {
    recovery_after_portal_closes();
    acknowledgement_guards();
    puts("C startup network recovery after portal close and SDK/AP guards: PASS");
    return 0;
}
