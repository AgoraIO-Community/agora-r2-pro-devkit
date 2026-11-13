/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_RUNTIME_GUARD_H
#define RV1126B_RUNTIME_GUARD_H

#include <stdbool.h>

/* Returns an owned locked FD, -2 for a missing read-only lock, -1 on failure. */
int rv1126b_runtime_lock(const char *path, bool create);
/* Returns 1 for the legacy service, 0 if absent, -1 if /proc cannot be read. */
int rv1126b_legacy_service_running(void);
/* Enters an owned private absolute directory. Creates it only after /userdata
 * mount checks; refuses symlinks or group/other permissions. Returns 0/-1. */
int rv1126b_prepare_storage(const char *path);

#endif
