/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_H
#define RV1126B_WIFI_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RV1126B_WIFI_WAITING,
    RV1126B_WIFI_CONNECTING,
    RV1126B_WIFI_AP_READY,
    RV1126B_WIFI_VERIFYING,
    RV1126B_WIFI_ONLINE,
    RV1126B_WIFI_ERROR,
    /* The SDK must stop before the product attempts recovery or opens an AP. */
    RV1126B_WIFI_RECOVERY_REQUIRED
} rv1126b_wifi_state_t;

typedef struct {
    bool enabled;
    bool force_ap;
    const char *interface_name; /* Empty selects the first wireless interface. */
    const char *storage_directory;
    const char *runtime_directory;
    const char *dhcp_script;
} rv1126b_wifi_config_t;

typedef struct {
    rv1126b_wifi_state_t state;
    bool ap_active;
    char ssid[33];
    char error[160];
} rv1126b_wifi_status_t;

/* Product-owned network lifecycle, independent of SDK connectivity listeners.
 * Start after storage/instance locks, before mybot_start(). Already usable
 * networking is retained. No daemon is started when provisioning is disabled. */
int rv1126b_wifi_start(const rv1126b_wifi_config_t *config);
/* The application must stop the SDK completely before requesting provisioning. */
void rv1126b_wifi_request_provisioning(void);
/* Acknowledge a lost route after stopping the SDK; retry saved profiles first. */
void rv1126b_wifi_request_recovery(void);
/* Copies a bounded status snapshot; no credentials are exposed. */
void rv1126b_wifi_status(rv1126b_wifi_status_t *status);
/* Cancels connection attempts, closes the portal and joins all owned workers. */
void rv1126b_wifi_stop(void);

#ifdef __cplusplus
}
#endif
#endif
