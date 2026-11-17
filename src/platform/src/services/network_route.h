/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_NETWORK_ROUTE_H
#define RV1126B_NETWORK_ROUTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only generic route/address inspection. Returns 1 for a usable routed
 * IPv4 address, 0 while unavailable and -1 on inspection failure. NULL/empty
 * interface matches any interface. excluded_ipv4 is host byte order; zero adds
 * no exclusion. The Wi-Fi backend excludes its own 192.168.4.1 AP address and
 * separately verifies association, profile identity and fresh DHCP leases. */
int rv1126b_network_route_ready(const char *interface_name, uint32_t excluded_ipv4);

#ifdef __cplusplus
}
#endif

#endif
