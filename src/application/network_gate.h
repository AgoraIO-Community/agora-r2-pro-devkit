/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_NETWORK_GATE_H
#define RV1126B_NETWORK_GATE_H

#include "mybot_platform/wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool recovery_requested;
} rv1126b_network_gate_t;

typedef enum {
    RV1126B_NETWORK_GATE_WAIT,
    RV1126B_NETWORK_GATE_READY,
    RV1126B_NETWORK_GATE_RECOVER
} rv1126b_network_gate_action_t;

/* Called while the application waits to start the SDK. RECOVER authorizes one
 * acknowledgement of route loss only after the SDK has stopped. */
rv1126b_network_gate_action_t rv1126b_network_gate_poll(rv1126b_network_gate_t *gate,
                                                        bool provisioning_enabled,
                                                        bool require_provisioning, bool sdk_started,
                                                        int routed,
                                                        const rv1126b_wifi_status_t *status);

#ifdef __cplusplus
}
#endif
#endif
