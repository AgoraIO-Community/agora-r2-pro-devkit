/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "factory_reset_internal.h"
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    (void)argv;
#ifdef RV1126B_BUILD_PROVENANCE
    extern const volatile char rv1126b_build_identity[];
    extern const volatile char rv1126b_build_source_identity[];
    (void)rv1126b_build_identity[0];
    (void)rv1126b_build_source_identity[0];
#endif
    if (argc != 1 || geteuid() != 0)
        return 1;
    umask(0077);
    static const factory_reset_paths_t paths = {
        .state = FACTORY_STATE_DIRECTORY,
        .data = "/userdata/mybot-rv1126b",
        .leases = "/run/mybot-network/leases",
        .config = "/etc/wpa_supplicant.conf",
        .remote_socket = "/run/wpa_supplicant/" FACTORY_STATION,
        .local_socket = "/run/mybot-rv1126b-service/reset-wpa",
        .app_lock = "/run/mybot-rv1126b.lock",
    };
    return factory_reset_run(&paths, getenv("RV1126B_WIFI_INTERFACE")) == 0 ? 0 : 1;
}
