/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_SERVICE_STATUS_H
#define RV1126B_SERVICE_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RV1126B_APP_STARTING,
    RV1126B_APP_READY,
    RV1126B_APP_PAIRING,
    RV1126B_APP_CONVERSATION,
    RV1126B_APP_OFFLINE,
    RV1126B_APP_FAILED,
    RV1126B_APP_STARTING_SERVICES,
    RV1126B_APP_POWERING_OFF,
    RV1126B_APP_WAITING_NETWORK,
    RV1126B_APP_WAITING_CLOCK,
    RV1126B_APP_AP,
    RV1126B_APP_WIFI_ERROR,
    RV1126B_APP_STOPPING,
    RV1126B_APP_STOPPED,
    RV1126B_APP_STATE_COUNT
} rv1126b_app_state_t;

/* Stable application.status wire values; not supervisor or SDK states. NULL for
 * an invalid enum. Returned strings are immutable for the process lifetime. */
const char *rv1126b_app_state_name(rv1126b_app_state_t state);

/* Product-thread-only publication. Unset MYBOT_STATUS_PATH disables all I/O.
 * The parent must be an owned private directory. No device identity or secrets
 * are included. Failure preserves the previous complete record. */
int rv1126b_service_status(rv1126b_app_state_t state);

#ifdef __cplusplus
}
#endif

#endif
