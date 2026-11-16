/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_SERVICE_H
#define RV1126B_WIFI_SERVICE_H

#include "mybot_platform/wifi.h"
#include "wifi_backend.h"
#include "wifi_portal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_service wifi_service_t;

typedef struct {
    bool enabled;
    bool force_ap;
} wifi_service_config_t;

typedef struct {
    unsigned connect_budget_ms, attempt_ms, commit_budget_ms;
    unsigned submit_grace_ms, finish_grace_ms, success_hold_ms;
    unsigned scan_interval_ms, offline_grace_ms;
} wifi_service_timing_t;

/* Private injectable control surface. The callback/context stay valid until
 * destroy returns. NULL config/timing select enabled/default deadlines.
 * On success ownership of backend and portal transfers and their handles clear;
 * on failure ownership remains with the caller. Disabled mode permits NULLs. */
wifi_service_t *wifi_service_create(const wifi_service_config_t *config, wifi_backend_t *backend,
                                    wifi_portal_t *portal, bool (*ready)(void *),
                                    void *ready_context, const wifi_service_timing_t *timing);

bool wifi_service_start(wifi_service_t *service);

void wifi_service_stop(wifi_service_t *service);

void wifi_service_destroy(wifi_service_t *service);

void wifi_service_request_provisioning(wifi_service_t *service);

void wifi_service_request_recovery(wifi_service_t *service);

void wifi_service_snapshot(wifi_service_t *service, rv1126b_wifi_status_t *status);

void wifi_service_status(wifi_service_t *service, wifi_portal_status_t *status);

void wifi_service_networks(wifi_service_t *service, wifi_scan_t *scan);

void wifi_service_saved_networks(wifi_service_t *service, wifi_saved_names_t *names);

bool wifi_service_submit(wifi_service_t *service, const char *ssid, const char *password,
                         char *error);

bool wifi_service_saved_action(wifi_service_t *service, const char *action, unsigned index,
                               const char *expected_ssid, char *error);

bool wifi_service_finish(wifi_service_t *service, char *error);

bool wifi_routed_ipv4_ready(const char *interface_name);

#ifdef __cplusplus
}
#endif

#endif
