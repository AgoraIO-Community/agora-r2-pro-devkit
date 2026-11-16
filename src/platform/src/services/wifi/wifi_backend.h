/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_BACKEND_H
#define RV1126B_WIFI_BACKEND_H

#include "wifi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *interface_name, *storage_directory, *runtime_directory, *dhcp_script;
    const char *system_wpa_config, *dhcpcd_lease_directory;
} wifi_backend_config_t;

typedef struct {
    bool (*initialize)(void *context, char *error);
    const char *(*ap_ssid)(void *context);
    bool (*start_ap)(void *context, char *error);
    void (*stop_ap)(void *context);
    bool (*ap_running)(void *context);
    bool (*concurrent_ap)(void *context);
    bool (*online)(void *context);
    bool (*scan)(void *context, wifi_scan_t *scan, char *error);
    bool (*load)(void *context, wifi_credentials_t *profiles, char *error);
    bool (*save)(void *context, const wifi_credentials_t *profiles, char *error);
    bool (*wait_committed)(void *context, const wifi_credential_t *credential, unsigned timeout_ms,
                           const wifi_cancel_t *cancel, char *error);
    bool (*connect)(void *context, const wifi_credential_t *credential, unsigned timeout_ms,
                    const wifi_cancel_t *cancel, char *error);
    void (*destroy)(void *context);
} wifi_backend_ops_t;

typedef struct {
    const wifi_backend_ops_t *ops;
    void *context;
} wifi_backend_t;

/* Copies config strings. Non-NULL error buffers have WIFI_ERROR_CAPACITY bytes. */
bool wifi_linux_backend_create(const wifi_backend_config_t *config, wifi_backend_t *backend,
                               char *error);

void wifi_backend_destroy(wifi_backend_t *backend);

bool wifi_make_credential(const char *ssid, const char *password, wifi_credential_t *credential,
                          char *error);

#ifdef __cplusplus
}
#endif

#endif
