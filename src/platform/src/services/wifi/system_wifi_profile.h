/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_SYSTEM_WIFI_PROFILE_H
#define RV1126B_SYSTEM_WIFI_PROFILE_H

#include "wifi_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

bool wifi_parse_system_profiles(const char *text, wifi_credentials_t *profiles, char *error);

bool wifi_read_system_profiles(const char *path, wifi_credentials_t *profiles, char *error);

bool wifi_commit_system_profiles(const char *path, const wifi_credentials_t *profiles,
                                 bool (*reconfigure)(void *context), void *context, char *error);

typedef struct {
    char generation[129], bssid[18], ipv4[16], ssid[WIFI_SSID_CAPACITY];
} wifi_dhcpcd_lease_t;

bool wifi_parse_dhcpcd_lease(const char *text, wifi_dhcpcd_lease_t *lease);

bool wifi_read_dhcpcd_lease(const char *directory, const char *interface_name,
                            wifi_dhcpcd_lease_t *lease);

bool wifi_dhcpcd_lease_matches(const wifi_dhcpcd_lease_t *lease, const char *previous_generation,
                               const char *status, const char *const *addresses, size_t count);

#ifdef __cplusplus
}
#endif

#endif
