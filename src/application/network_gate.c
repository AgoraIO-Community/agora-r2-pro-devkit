/* SPDX-License-Identifier: Apache-2.0 */
#include "network_gate.h"

rv1126b_network_gate_action_t rv1126b_network_gate_poll(rv1126b_network_gate_t *gate,
                                                        bool provisioning_enabled,
                                                        bool require_provisioning, bool sdk_started,
                                                        int routed,
                                                        const rv1126b_wifi_status_t *status) {
    if (!gate || !status)
        return RV1126B_NETWORK_GATE_WAIT;
    if (status->state != RV1126B_WIFI_RECOVERY_REQUIRED)
        gate->recovery_requested = false;
    if (provisioning_enabled && !sdk_started && !status->ap_active &&
        status->state == RV1126B_WIFI_RECOVERY_REQUIRED) {
        if (!gate->recovery_requested) {
            gate->recovery_requested = true;
            return RV1126B_NETWORK_GATE_RECOVER;
        }
        return RV1126B_NETWORK_GATE_WAIT;
    }
    const bool configured =
        !provisioning_enabled || status->state == RV1126B_WIFI_ONLINE ||
        (!require_provisioning && status->state == RV1126B_WIFI_ERROR && !status->ap_active);
    return routed > 0 && configured && !status->ap_active ? RV1126B_NETWORK_GATE_READY
                                                          : RV1126B_NETWORK_GATE_WAIT;
}
