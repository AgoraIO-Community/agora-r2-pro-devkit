/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "wifi_service.h"
#include "../network_route.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AP_HEALTH_INTERVAL_MS 250

typedef enum { ACTION_NONE, ACTION_CONNECT, ACTION_DELETE, ACTION_SET_DEFAULT } action_kind_t;
typedef struct {
    action_kind_t kind;
    wifi_credential_t credential;
    unsigned index;
} action_t;
struct wifi_service {
    wifi_service_config_t config;
    wifi_service_timing_t timing;
    wifi_backend_t backend;
    wifi_portal_t portal;
    wifi_portal_handler_t handler;
    bool (*ready)(void *);
    void *ready_context;
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t worker;
    bool worker_started, stop, cancel_attempt;
    rv1126b_wifi_status_t public_status;
    wifi_portal_status_t portal_status;
    wifi_scan_t networks;
    wifi_credentials_t saved;
    action_t pending;
    bool busy, manual, recovery, initialized, portal_started, finish_requested, connected;
    uint64_t pending_at, finish_at, connected_at, next_scan, next_health, offline_since;
};

static void copy_text(char *out, size_t capacity, const char *text) {
    if (capacity)
        snprintf(out, capacity, "%s", text ? text : "");
}

static const char *failure(const char *error, const char *fallback) {
    return error[0] ? error : fallback;
}

bool wifi_routed_ipv4_ready(const char *interface_name) {
    return rv1126b_network_route_ready(interface_name, 0) > 0;
}

static bool stopping(wifi_service_t *s) {
    pthread_mutex_lock(&s->mutex);
    const bool result = s->stop;
    pthread_mutex_unlock(&s->mutex);
    return result;
}

static bool cancelled(void *context) {
    wifi_service_t *s = context;
    pthread_mutex_lock(&s->mutex);
    const bool result = s->stop || s->cancel_attempt;
    pthread_mutex_unlock(&s->mutex);
    return result;
}

/* Caller holds the mutex. Linux's condition variable uses CLOCK_MONOTONIC;
 * macOS exposes the same relative wait semantics through its native extension. */
static void wait_until(wifi_service_t *s, uint64_t deadline) {
    const uint64_t now = wifi_now_ms();
    if (deadline <= now)
        return;
    struct timespec ts;
#if defined(__APPLE__)
    const uint64_t remaining = deadline - now;
    ts.tv_sec = (time_t)(remaining / 1000);
    ts.tv_nsec = (long)(remaining % 1000) * 1000000L;
    pthread_cond_timedwait_relative_np(&s->wake, &s->mutex, &ts);
#else
    ts.tv_sec = (time_t)(deadline / 1000);
    ts.tv_nsec = (long)(deadline % 1000) * 1000000L;
    pthread_cond_timedwait(&s->wake, &s->mutex, &ts);
#endif
}

static void set_state(wifi_service_t *s, rv1126b_wifi_state_t state, const char *portal_state,
                      const char *error, wifi_portal_error_t error_code) {
    pthread_mutex_lock(&s->mutex);
    /* A pending SDK-stop acknowledgement owns the next transition. */
    if (!(s->manual && state == RV1126B_WIFI_ONLINE)) {
        s->public_status.state = state;
        copy_text(s->public_status.error, sizeof(s->public_status.error), error);
        copy_text(s->portal_status.state, sizeof(s->portal_status.state), portal_state);
        copy_text(s->portal_status.error, sizeof(s->portal_status.error), error);
        s->portal_status.error_code = error_code;
    }
    pthread_mutex_unlock(&s->mutex);
}

void wifi_service_snapshot(wifi_service_t *s, rv1126b_wifi_status_t *status) {
    if (!status)
        return;
    memset(status, 0, sizeof(*status));
    status->state = RV1126B_WIFI_WAITING;
    if (!s)
        return;
    pthread_mutex_lock(&s->mutex);
    *status = s->public_status;
    pthread_mutex_unlock(&s->mutex);
}

void wifi_service_status(wifi_service_t *s, wifi_portal_status_t *status) {
    if (!status)
        return;
    memset(status, 0, sizeof(*status));
    if (!s)
        return;
    pthread_mutex_lock(&s->mutex);
    *status = s->portal_status;
    status->busy = s->busy;
    pthread_mutex_unlock(&s->mutex);
}

void wifi_service_networks(wifi_service_t *s, wifi_scan_t *scan) {
    if (!scan)
        return;
    memset(scan, 0, sizeof(*scan));
    if (!s)
        return;
    pthread_mutex_lock(&s->mutex);
    *scan = s->networks;
    pthread_mutex_unlock(&s->mutex);
}

void wifi_service_saved_networks(wifi_service_t *s, wifi_saved_names_t *names) {
    if (!names)
        return;
    memset(names, 0, sizeof(*names));
    if (!s)
        return;
    pthread_mutex_lock(&s->mutex);
    names->count = s->saved.count;
    for (size_t i = 0; i < names->count; ++i)
        copy_text(names->items[i], sizeof(names->items[i]), s->saved.items[i].ssid);
    pthread_mutex_unlock(&s->mutex);
}

static void request(wifi_service_t *s, bool recovery) {
    if (!s || !s->config.enabled)
        return;
    pthread_mutex_lock(&s->mutex);
    if (!s->stop) {
        s->manual = true;
        s->recovery = recovery;
        s->cancel_attempt = true;
        s->public_status.state = RV1126B_WIFI_WAITING;
        s->public_status.error[0] = '\0';
        copy_text(s->portal_status.state, sizeof(s->portal_status.state), "connecting");
        s->portal_status.error[0] = '\0';
        s->portal_status.error_code = WIFI_PORTAL_ERROR_NONE;
        s->connected = false;
        s->finish_requested = false;
        memset(&s->pending, 0, sizeof(s->pending));
        s->busy = true;
        pthread_cond_broadcast(&s->wake);
    }
    pthread_mutex_unlock(&s->mutex);
}

void wifi_service_request_provisioning(wifi_service_t *s) {
    request(s, false);
}

void wifi_service_request_recovery(wifi_service_t *s) {
    request(s, true);
}

bool wifi_service_submit(wifi_service_t *s, const char *ssid, const char *password, char *error) {
    wifi_credential_t credential;
    if (!s || !wifi_make_credential(ssid, password, &credential, error))
        return false;
    pthread_mutex_lock(&s->mutex);
    if (s->stop || s->manual || !s->portal_started || s->connected || s->busy) {
        pthread_mutex_unlock(&s->mutex);
        wifi_error(error, "Wi-Fi control is busy");
        return false;
    }
    s->pending.kind = ACTION_CONNECT;
    s->pending.credential = credential;
    s->pending_at = wifi_now_ms() + s->timing.submit_grace_ms;
    s->busy = true;
    s->public_status.state = RV1126B_WIFI_CONNECTING;
    s->public_status.error[0] = '\0';
    copy_text(s->portal_status.state, sizeof(s->portal_status.state), "connecting");
    s->portal_status.error[0] = '\0';
    s->portal_status.error_code = WIFI_PORTAL_ERROR_NONE;
    pthread_cond_broadcast(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    return true;
}

bool wifi_service_saved_action(wifi_service_t *s, const char *action, unsigned index,
                               const char *expected_ssid, char *error) {
    if (!s || !action || (strcmp(action, "delete") && strcmp(action, "set_default"))) {
        wifi_error(error, "Unknown saved network action");
        return false;
    }
    pthread_mutex_lock(&s->mutex);
    const char *problem = NULL;
    if (s->stop || s->manual || !s->portal_started || s->connected || s->busy)
        problem = "Wi-Fi control is busy";
    else if (index >= s->saved.count)
        problem = "Saved network does not exist";
    else if (!expected_ssid || strcmp(s->saved.items[index].ssid, expected_ssid))
        problem = "Saved network list changed; refresh and retry";
    if (problem) {
        pthread_mutex_unlock(&s->mutex);
        wifi_error(error, problem);
        return false;
    }
    s->pending.kind = strcmp(action, "delete") == 0 ? ACTION_DELETE : ACTION_SET_DEFAULT;
    s->pending.index = index;
    s->busy = true;
    pthread_cond_broadcast(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    return true;
}

bool wifi_service_finish(wifi_service_t *s, char *error) {
    if (!s) {
        wifi_error(error, "Wi-Fi is not connected");
        return false;
    }
    pthread_mutex_lock(&s->mutex);
    if (s->stop || !s->connected) {
        pthread_mutex_unlock(&s->mutex);
        wifi_error(error, "Wi-Fi is not connected");
        return false;
    }
    if (!s->finish_requested) {
        s->finish_requested = true;
        s->finish_at = wifi_now_ms() + s->timing.finish_grace_ms;
    }
    pthread_cond_broadcast(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    return true;
}

static void handler_status(void *s, wifi_portal_status_t *out) {
    wifi_service_status(s, out);
}

static void handler_networks(void *s, wifi_scan_t *out) {
    wifi_service_networks(s, out);
}

static void handler_saved(void *s, wifi_saved_names_t *out) {
    wifi_service_saved_networks(s, out);
}

static bool handler_submit(void *s, const char *ssid, const char *password, char *error) {
    return wifi_service_submit(s, ssid, password, error);
}

static bool handler_action(void *s, const char *action, unsigned index, const char *ssid,
                           char *error) {
    return wifi_service_saved_action(s, action, index, ssid, error);
}

static bool handler_finish(void *s, char *error) {
    return wifi_service_finish(s, error);
}

static const wifi_portal_handler_ops_t handler_ops = {handler_status, handler_networks,
                                                      handler_saved,  handler_submit,
                                                      handler_action, handler_finish};
static bool initialize(wifi_service_t *s) {
    if (s->initialized)
        return true;
    char error[WIFI_ERROR_CAPACITY] = "";
    if (!s->backend.ops->initialize(s->backend.context, error)) {
        set_state(s, RV1126B_WIFI_ERROR, "error", failure(error, "Wi-Fi backend is unavailable"),
                  WIFI_PORTAL_ERROR_AP);
        return false;
    }
    s->initialized = true;
    wifi_credentials_t profiles = {0};
    s->backend.ops->load(s->backend.context, &profiles, error);
    if (profiles.count > WIFI_MAX_NETWORKS)
        profiles.count = WIFI_MAX_NETWORKS;
    const bool concurrent = s->backend.ops->concurrent_ap(s->backend.context);
    pthread_mutex_lock(&s->mutex);
    s->saved = profiles;
    s->portal_status.concurrent = concurrent;
    pthread_mutex_unlock(&s->mutex);
    return true;
}

static void ap_failed(wifi_service_t *s, const char *error) {
    /* Withdraw guidance before joins/driver cleanup, which can take time. */
    pthread_mutex_lock(&s->mutex);
    s->portal_started = false;
    s->public_status.ap_active = false;
    s->public_status.ssid[0] = '\0';
    s->public_status.state = RV1126B_WIFI_ERROR;
    copy_text(s->public_status.error, sizeof(s->public_status.error), error);
    s->portal_status.ssid[0] = '\0';
    copy_text(s->portal_status.state, sizeof(s->portal_status.state), "error");
    copy_text(s->portal_status.error, sizeof(s->portal_status.error), error);
    s->portal_status.error_code = WIFI_PORTAL_ERROR_AP;
    s->busy = false;
    s->connected = false;
    s->finish_requested = false;
    memset(&s->pending, 0, sizeof(s->pending));
    pthread_mutex_unlock(&s->mutex);
    s->portal.ops->stop(s->portal.context);
    s->backend.ops->stop_ap(s->backend.context);
}

static bool enter_ap(wifi_service_t *s, const char *previous_error,
                     wifi_portal_error_t previous_code) {
    if (cancelled(s))
        return false;
    char error[WIFI_ERROR_CAPACITY] = "";
    if (!s->backend.ops->start_ap(s->backend.context, error) ||
        !s->backend.ops->ap_running(s->backend.context)) {
        ap_failed(s, failure(error, "Wi-Fi access point did not start"));
        return false;
    }
    pthread_mutex_lock(&s->mutex);
    const bool already_started = s->portal_started;
    pthread_mutex_unlock(&s->mutex);
    if (!already_started && !s->portal.ops->start(s->portal.context, &s->handler, error)) {
        ap_failed(s, failure(error, "Wi-Fi setup portal did not start"));
        return false;
    }
    const char *ssid = s->backend.ops->ap_ssid(s->backend.context);
    const bool concurrent = s->backend.ops->concurrent_ap(s->backend.context);
    pthread_mutex_lock(&s->mutex);
    s->portal_started = true;
    s->public_status.ap_active = true;
    copy_text(s->public_status.ssid, sizeof(s->public_status.ssid), ssid);
    copy_text(s->portal_status.ssid, sizeof(s->portal_status.ssid), ssid);
    s->portal_status.concurrent = concurrent;
    s->busy = false;
    s->connected = false;
    s->finish_requested = false;
    pthread_mutex_unlock(&s->mutex);
    set_state(s, RV1126B_WIFI_AP_READY, previous_error && previous_error[0] ? "error" : "ap",
              previous_error, previous_code);
    s->next_scan = wifi_now_ms();
    s->next_health = wifi_now_ms() + AP_HEALTH_INTERVAL_MS;
    return true;
}

static bool try_saved(wifi_service_t *s) {
    pthread_mutex_lock(&s->mutex);
    const wifi_credentials_t credentials = s->saved;
    pthread_mutex_unlock(&s->mutex);
    const uint64_t deadline = wifi_now_ms() + s->timing.connect_budget_ms;
    const wifi_cancel_t cancel = {cancelled, s};
    for (size_t i = 0; i < credentials.count && !cancelled(s); ++i) {
        const uint64_t now = wifi_now_ms();
        if (now >= deadline)
            break;
        unsigned timeout = (unsigned)((deadline - now) / (credentials.count - i));
        if (!timeout)
            timeout = 1;
        if (timeout > s->timing.attempt_ms)
            timeout = s->timing.attempt_ms;
        set_state(s, RV1126B_WIFI_CONNECTING, "connecting", "", WIFI_PORTAL_ERROR_NONE);
        char error[WIFI_ERROR_CAPACITY] = "";
        if (s->backend.ops->connect(s->backend.context, &credentials.items[i], timeout, &cancel,
                                    error) &&
            !cancelled(s) && s->backend.ops->online(s->backend.context)) {
            set_state(s, RV1126B_WIFI_ONLINE, "connected", "", WIFI_PORTAL_ERROR_NONE);
            return true;
        }
    }
    return false;
}

static void remove_profile(wifi_credentials_t *profiles, size_t index) {
    if (index >= profiles->count)
        return;
    memmove(&profiles->items[index], &profiles->items[index + 1],
            (profiles->count - index - 1) * sizeof(profiles->items[0]));
    memset(&profiles->items[--profiles->count], 0, sizeof(profiles->items[0]));
}

static void connect_candidate(wifi_service_t *s, const wifi_credential_t *credential) {
    const uint64_t deadline = wifi_now_ms() + s->timing.connect_budget_ms;
    const wifi_cancel_t cancel = {cancelled, s};
    char error[WIFI_ERROR_CAPACITY] = "";
    bool connected = false;
    const bool concurrent = s->backend.ops->concurrent_ap(s->backend.context);
    pthread_mutex_lock(&s->mutex);
    if (!concurrent)
        s->public_status.ap_active = false;
    pthread_mutex_unlock(&s->mutex);
    for (unsigned attempt = 0; attempt < 2 && !cancelled(s); ++attempt) {
        const uint64_t now = wifi_now_ms();
        if (now >= deadline)
            break;
        unsigned timeout = (unsigned)(deadline - now);
        if (timeout > s->timing.attempt_ms)
            timeout = s->timing.attempt_ms;
        if (s->backend.ops->connect(s->backend.context, credential, timeout, &cancel, error) &&
            !cancelled(s) && s->backend.ops->online(s->backend.context)) {
            connected = true;
            break;
        }
    }
    if (cancelled(s))
        return;
    if (!connected) {
        enter_ap(s, failure(error, "Wi-Fi connection failed; check the network and password"),
                 WIFI_PORTAL_ERROR_CONNECT);
        return;
    }
    pthread_mutex_lock(&s->mutex);
    wifi_credentials_t updated = s->saved;
    pthread_mutex_unlock(&s->mutex);
    for (size_t i = 0; i < updated.count;)
        if (!strcmp(updated.items[i].ssid, credential->ssid))
            remove_profile(&updated, i);
        else
            ++i;
    if (updated.count == WIFI_MAX_NETWORKS)
        remove_profile(&updated, updated.count - 1);
    memmove(&updated.items[1], &updated.items[0], updated.count * sizeof(updated.items[0]));
    updated.items[0] = *credential;
    ++updated.count;
    error[0] = '\0';
    if (!s->backend.ops->save(s->backend.context, &updated, error)) {
        enter_ap(s, failure(error, "Network could not be saved; please retry"),
                 WIFI_PORTAL_ERROR_SAVE);
        return;
    }
    /* Persistence already committed even if the subsequent reload times out. */
    pthread_mutex_lock(&s->mutex);
    s->saved = updated;
    pthread_mutex_unlock(&s->mutex);
    if (cancelled(s))
        return;
    error[0] = '\0';
    const bool committed = s->backend.ops->wait_committed(
        s->backend.context, credential, s->timing.commit_budget_ms, &cancel, error);
    if (cancelled(s))
        return;
    if (!committed) {
        enter_ap(s, failure(error, "Saved network is not ready; please retry"),
                 WIFI_PORTAL_ERROR_COMMIT);
        return;
    }
    const bool ap_active = s->backend.ops->ap_running(s->backend.context);
    pthread_mutex_lock(&s->mutex);
    s->public_status.ap_active = ap_active;
    s->connected = true;
    s->busy = false;
    s->connected_at = wifi_now_ms();
    s->finish_requested = false;
    pthread_mutex_unlock(&s->mutex);
    set_state(s, RV1126B_WIFI_VERIFYING, "connected", "", WIFI_PORTAL_ERROR_NONE);
}

static void mutate_saved(wifi_service_t *s, const action_t *action) {
    pthread_mutex_lock(&s->mutex);
    wifi_credentials_t updated = s->saved;
    pthread_mutex_unlock(&s->mutex);
    if (action->index >= updated.count) {
        pthread_mutex_lock(&s->mutex);
        s->busy = false;
        pthread_mutex_unlock(&s->mutex);
        return;
    }
    const wifi_credential_t credential = updated.items[action->index];
    remove_profile(&updated, action->index);
    if (action->kind == ACTION_SET_DEFAULT) {
        memmove(&updated.items[1], &updated.items[0], updated.count * sizeof(updated.items[0]));
        updated.items[0] = credential;
        ++updated.count;
    }
    char error[WIFI_ERROR_CAPACITY] = "";
    const bool saved = s->backend.ops->save(s->backend.context, &updated, error);
    const char *result = saved ? "" : failure(error, "Saved networks could not be updated");
    pthread_mutex_lock(&s->mutex);
    if (saved)
        s->saved = updated;
    s->public_status.state = RV1126B_WIFI_AP_READY;
    copy_text(s->public_status.error, sizeof(s->public_status.error), result);
    copy_text(s->portal_status.state, sizeof(s->portal_status.state), saved ? "ap" : "error");
    copy_text(s->portal_status.error, sizeof(s->portal_status.error), result);
    s->portal_status.error_code = saved ? WIFI_PORTAL_ERROR_NONE : WIFI_PORTAL_ERROR_SAVED_UPDATE;
    s->busy = false;
    pthread_mutex_unlock(&s->mutex);
}

static void refresh_scan(wifi_service_t *s) {
    char error[WIFI_ERROR_CAPACITY] = "";
    wifi_scan_t scan = {0};
    s->backend.ops->scan(s->backend.context, &scan, error);
    if (scan.count > WIFI_MAX_ACCESS_POINTS)
        scan.count = WIFI_MAX_ACCESS_POINTS;
    pthread_mutex_lock(&s->mutex);
    s->networks = scan;
    pthread_mutex_unlock(&s->mutex);
    s->next_scan = wifi_now_ms() + s->timing.scan_interval_ms;
}

static bool ready(wifi_service_t *s) {
    return s->ready && s->ready(s->ready_context);
}

static void control(wifi_service_t *s) {
    if (!s->config.enabled) {
        while (!stopping(s)) {
            set_state(s, ready(s) ? RV1126B_WIFI_ONLINE : RV1126B_WIFI_WAITING, "ap", "",
                      WIFI_PORTAL_ERROR_NONE);
            pthread_mutex_lock(&s->mutex);
            if (!s->stop)
                wait_until(s, wifi_now_ms() + 250);
            pthread_mutex_unlock(&s->mutex);
        }
        return;
    }
    if (!s->config.force_ap && ready(s)) {
        /* Load profiles even when the externally managed route is already up. */
        initialize(s);
        set_state(s, RV1126B_WIFI_ONLINE, "connected", "", WIFI_PORTAL_ERROR_NONE);
    } else if (initialize(s) && (s->config.force_ap || !try_saved(s))) {
        enter_ap(s, "", WIFI_PORTAL_ERROR_NONE);
    }
    while (!stopping(s)) {
        action_t action = {0};
        bool manual = false, recovery = false, finish = false, scan = false, health = false;
        pthread_mutex_lock(&s->mutex);
        const uint64_t now = wifi_now_ms();
        if (s->manual) {
            s->manual = false;
            manual = true;
            recovery = s->recovery;
            s->recovery = false;
            memset(&s->pending, 0, sizeof(s->pending));
            s->busy = false;
            s->connected = false;
            s->finish_requested = false;
            s->portal_status.error[0] = '\0';
            s->portal_status.error_code = WIFI_PORTAL_ERROR_NONE;
            s->public_status.error[0] = '\0';
            s->cancel_attempt = false;
        } else if (s->pending.kind != ACTION_NONE) {
            if (s->pending.kind == ACTION_CONNECT && now < s->pending_at) {
                wait_until(s, s->pending_at);
                pthread_mutex_unlock(&s->mutex);
                continue;
            }
            action = s->pending;
            memset(&s->pending, 0, sizeof(s->pending));
        } else if (s->connected &&
                   (s->finish_requested ? now >= s->finish_at
                                        : now >= s->connected_at + s->timing.success_hold_ms)) {
            s->connected = false;
            finish = true;
        } else if (s->portal_started && s->public_status.ap_active && !s->connected && !s->busy &&
                   now >= s->next_health) {
            health = true;
        } else if (s->portal_started && !s->connected && !s->busy && now >= s->next_scan) {
            scan = true;
        } else {
            if (s->public_status.state == RV1126B_WIFI_ONLINE && !s->portal_started) {
                if (ready(s))
                    s->offline_since = 0;
                else if (!s->offline_since)
                    s->offline_since = now;
                else if (now - s->offline_since >= s->timing.offline_grace_ms) {
                    s->public_status.state = RV1126B_WIFI_RECOVERY_REQUIRED;
                    copy_text(s->public_status.error, sizeof(s->public_status.error),
                              "Network lost; waiting for SDK shutdown");
                    copy_text(s->portal_status.state, sizeof(s->portal_status.state), "connecting");
                }
            }
            if (!s->stop)
                wait_until(s, now + 50);
            pthread_mutex_unlock(&s->mutex);
            continue;
        }
        pthread_mutex_unlock(&s->mutex);
        if (manual) {
            s->offline_since = 0;
            if (initialize(s) && (!recovery || !try_saved(s)))
                enter_ap(s, "", WIFI_PORTAL_ERROR_NONE);
        } else if (action.kind == ACTION_CONNECT)
            connect_candidate(s, &action.credential);
        else if (action.kind != ACTION_NONE)
            mutate_saved(s, &action);
        else if (finish) {
            s->portal.ops->stop(s->portal.context);
            s->backend.ops->stop_ap(s->backend.context);
            pthread_mutex_lock(&s->mutex);
            s->portal_started = false;
            s->public_status.ap_active = false;
            s->busy = false;
            pthread_mutex_unlock(&s->mutex);
            set_state(s, RV1126B_WIFI_ONLINE, "connected", "", WIFI_PORTAL_ERROR_NONE);
        } else if (health) {
            s->next_health = wifi_now_ms() + AP_HEALTH_INTERVAL_MS;
            if (!s->backend.ops->ap_running(s->backend.context))
                ap_failed(s, "Wi-Fi access point stopped; open settings and retry Wi-Fi setup");
        } else if (scan)
            refresh_scan(s);
    }
}

static void *run(void *context) {
    wifi_service_t *s = context;
    control(s);
    if (s->config.enabled && s->initialized) {
        s->portal.ops->stop(s->portal.context);
        s->backend.ops->stop_ap(s->backend.context);
    }
    pthread_mutex_lock(&s->mutex);
    s->portal_started = false;
    s->public_status.ap_active = false;
    s->busy = false;
    memset(&s->pending, 0, sizeof(s->pending));
    pthread_mutex_unlock(&s->mutex);
    return NULL;
}

static bool backend_valid(const wifi_backend_t *b) {
    return b && b->ops && b->ops->initialize && b->ops->ap_ssid && b->ops->start_ap &&
           b->ops->stop_ap && b->ops->ap_running && b->ops->concurrent_ap && b->ops->online &&
           b->ops->scan && b->ops->load && b->ops->save && b->ops->wait_committed &&
           b->ops->connect;
}

wifi_service_t *wifi_service_create(const wifi_service_config_t *config, wifi_backend_t *backend,
                                    wifi_portal_t *portal, bool (*is_ready)(void *),
                                    void *ready_context, const wifi_service_timing_t *timing) {
    const wifi_service_config_t defaults = {true, false};
    const wifi_service_config_t selected = config ? *config : defaults;
    if (selected.enabled && (!backend_valid(backend) || !portal || !portal->ops ||
                             !portal->ops->start || !portal->ops->stop))
        return NULL;
    wifi_service_t *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->config = selected;
    const wifi_service_timing_t default_timing = {30000, 15000, 30000, 200, 200, 5000, 15000, 3000};
    s->timing = timing ? *timing : default_timing;
    s->ready = is_ready;
    s->ready_context = ready_context;
    s->public_status.state = RV1126B_WIFI_WAITING;
    copy_text(s->portal_status.state, sizeof(s->portal_status.state), "ap");
    if (pthread_mutex_init(&s->mutex, NULL)) {
        free(s);
        return NULL;
    }
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes)) {
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return NULL;
    }
#if !defined(__APPLE__)
    if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC)) {
        pthread_condattr_destroy(&attributes);
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return NULL;
    }
#endif
    const int result = pthread_cond_init(&s->wake, &attributes);
    pthread_condattr_destroy(&attributes);
    if (result) {
        pthread_mutex_destroy(&s->mutex);
        free(s);
        return NULL;
    }
    if (backend) {
        s->backend = *backend;
        memset(backend, 0, sizeof(*backend));
    }
    if (portal) {
        s->portal = *portal;
        memset(portal, 0, sizeof(*portal));
    }
    s->handler.ops = &handler_ops;
    s->handler.context = s;
    return s;
}

bool wifi_service_start(wifi_service_t *s) {
    if (!s)
        return false;
    pthread_mutex_lock(&s->mutex);
    if (s->worker_started || s->stop) {
        pthread_mutex_unlock(&s->mutex);
        return false;
    }
    const int result = pthread_create(&s->worker, NULL, run, s);
    if (!result)
        s->worker_started = true;
    pthread_mutex_unlock(&s->mutex);
    if (result)
        set_state(s, RV1126B_WIFI_ERROR, "error", "Cannot start Wi-Fi control worker",
                  WIFI_PORTAL_ERROR_AP);
    return result == 0;
}

void wifi_service_stop(wifi_service_t *s) {
    if (!s)
        return;
    pthread_mutex_lock(&s->mutex);
    s->stop = true;
    s->cancel_attempt = true;
    const bool join = s->worker_started;
    pthread_cond_broadcast(&s->wake);
    pthread_mutex_unlock(&s->mutex);
    if (join) {
        pthread_join(s->worker, NULL);
        pthread_mutex_lock(&s->mutex);
        s->worker_started = false;
        pthread_mutex_unlock(&s->mutex);
    }
}

void wifi_service_destroy(wifi_service_t *s) {
    if (!s)
        return;
    wifi_service_stop(s);
    wifi_portal_destroy(&s->portal);
    wifi_backend_destroy(&s->backend);
    pthread_cond_destroy(&s->wake);
    pthread_mutex_destroy(&s->mutex);
    memset(s, 0, sizeof(*s));
    free(s);
}

static pthread_mutex_t service_mutex = PTHREAD_MUTEX_INITIALIZER;
static wifi_service_t *service;
static char *service_interface;
static bool system_ready(void *context) {
    return wifi_routed_ipv4_ready(context);
}

int rv1126b_wifi_start(const rv1126b_wifi_config_t *config) {
    if (!config)
        return -1;
    pthread_mutex_lock(&service_mutex);
    if (service) {
        pthread_mutex_unlock(&service_mutex);
        return -1;
    }
    char *interface = strdup(config->interface_name ? config->interface_name : "");
    wifi_backend_t backend = {0};
    wifi_portal_t portal = {0};
    char error[WIFI_ERROR_CAPACITY] = "";
    const wifi_backend_config_t backend_config = {config->interface_name,
                                                  config->storage_directory,
                                                  config->runtime_directory,
                                                  config->dhcp_script,
                                                  NULL,
                                                  NULL};
    bool success = interface != NULL;
    if (success && config->enabled)
        success = wifi_linux_backend_create(&backend_config, &backend, error) &&
                  wifi_portal_create(NULL, &portal, error);
    const wifi_service_config_t service_config = {config->enabled, config->force_ap};
    wifi_service_t *candidate = success ? wifi_service_create(&service_config, &backend, &portal,
                                                              system_ready, interface, NULL)
                                        : NULL;
    success = candidate && wifi_service_start(candidate);
    if (success) {
        service = candidate;
        service_interface = interface;
    } else {
        wifi_service_destroy(candidate);
        wifi_backend_destroy(&backend);
        wifi_portal_destroy(&portal);
        free(interface);
    }
    pthread_mutex_unlock(&service_mutex);
    return success ? 0 : -1;
}

void rv1126b_wifi_request_provisioning(void) {
    pthread_mutex_lock(&service_mutex);
    wifi_service_request_provisioning(service);
    pthread_mutex_unlock(&service_mutex);
}

void rv1126b_wifi_request_recovery(void) {
    pthread_mutex_lock(&service_mutex);
    wifi_service_request_recovery(service);
    pthread_mutex_unlock(&service_mutex);
}

void rv1126b_wifi_status(rv1126b_wifi_status_t *status) {
    pthread_mutex_lock(&service_mutex);
    wifi_service_snapshot(service, status);
    pthread_mutex_unlock(&service_mutex);
}

void rv1126b_wifi_stop(void) {
    pthread_mutex_lock(&service_mutex);
    wifi_service_destroy(service);
    service = NULL;
    free(service_interface);
    service_interface = NULL;
    pthread_mutex_unlock(&service_mutex);
}
