/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_WIFI_PORTAL_H
#define RV1126B_WIFI_PORTAL_H

#include "wifi_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_PORTAL_ERROR_NONE,
    WIFI_PORTAL_ERROR_CONNECT,
    WIFI_PORTAL_ERROR_SAVE,
    WIFI_PORTAL_ERROR_COMMIT,
    WIFI_PORTAL_ERROR_AP,
    WIFI_PORTAL_ERROR_SAVED_UPDATE
} wifi_portal_error_t;

typedef struct {
    char state[16], error[WIFI_ERROR_CAPACITY], ssid[WIFI_SSID_CAPACITY];
    bool concurrent, busy;
    wifi_portal_error_t error_code;
} wifi_portal_status_t;

typedef struct {
    void (*status)(void *context, wifi_portal_status_t *status);
    void (*networks)(void *context, wifi_scan_t *scan);
    void (*saved_networks)(void *context, wifi_saved_names_t *names);
    bool (*submit)(void *context, const char *ssid, const char *password, char *error);
    bool (*saved_action)(void *context, const char *action, unsigned index,
                         const char *expected_ssid, char *error);
    bool (*finish)(void *context, char *error);
} wifi_portal_handler_ops_t;

typedef struct {
    const wifi_portal_handler_ops_t *ops;
    void *context;
} wifi_portal_handler_t;

typedef struct {
    const char *address;
    unsigned short http_port, dns_port;
} wifi_portal_config_t;

typedef struct {
    bool (*start)(void *context, const wifi_portal_handler_t *handler, char *error);
    void (*stop)(void *context);
    void (*destroy)(void *context);
} wifi_portal_ops_t;

typedef struct {
    const wifi_portal_ops_t *ops;
    void *context;
} wifi_portal_t;

/* NULL config selects 192.168.4.1:80/53. Handler stays alive until stop returns. */
bool wifi_portal_create(const wifi_portal_config_t *config, wifi_portal_t *portal, char *error);

void wifi_portal_destroy(wifi_portal_t *portal);

#ifdef __cplusplus
}
#endif

#endif
