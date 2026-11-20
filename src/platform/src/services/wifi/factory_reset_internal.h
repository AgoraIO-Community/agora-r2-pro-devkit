/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_FACTORY_RESET_INTERNAL_H
#define RV1126B_FACTORY_RESET_INTERNAL_H

#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FACTORY_STATE_DIRECTORY "/userdata/mybot"
#define FACTORY_SETUP_FILE "factory-reset.setup"
#define FACTORY_PENDING_FILE "factory-reset.pending"

#define FACTORY_STATION "wlan0"

static inline bool factory_station_supported(const char *station) {
    return !station || !*station || !strcmp(station, FACTORY_STATION);
}

/* Opens an owned mode-0700 directory without following symlinks in its path.
 * Optional directories may return -1 with errno=ENOENT. */
int factory_private_directory(const char *path);

/* Returns 1/0 for present/absent and -1 for an unsafe entry or I/O error. */
int factory_file_status(int directory, const char *name, bool marker);

/* Empty private markers survive power loss once the file and directory sync. */
int factory_create_marker(int directory, const char *name);

int factory_remove_file(int directory, const char *name, bool marker);

typedef struct {
    const char *state;
    const char *data;
    const char *leases;
    const char *config;
    const char *remote_socket;
    const char *local_socket;
    const char *app_lock;
} factory_reset_paths_t;

/* Invoked by the service wrapper before application startup. The internal
 * path set is fixed in production and substituted only by host tests. */
int factory_reset_run(const factory_reset_paths_t *paths, const char *station);

#ifdef __cplusplus
}
#endif
#endif
