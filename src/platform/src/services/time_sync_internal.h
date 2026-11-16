/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_TIME_SYNC_INTERNAL_H
#define RV1126B_TIME_SYNC_INTERNAL_H

#include "mybot_platform/time_sync.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* Private deterministic clock/process seam; production uses the default backend.
 * helper_exited observes without reaping so its PID cannot be reused before
 * cancel_helper signals the owned process group. reap_helper returns 1/0/-1. */
typedef struct {
    int (*realtime)(void *context, int64_t *seconds);
    int (*monotonic)(void *context, uint64_t *milliseconds);
    int (*service_running)(void *context);
    int (*spawn_helper)(void *context, const char *path, pid_t *pid);
    int (*helper_exited)(void *context, pid_t pid);
    int (*cancel_helper)(void *context, pid_t pid);
    int (*reap_helper)(void *context, pid_t pid, bool block);
} rv1126b_time_sync_backend_t;

rv1126b_time_sync_t *
rv1126b_time_sync_create_with_backend(const rv1126b_time_sync_config_t *config,
                                      const rv1126b_time_sync_backend_t *backend, void *context);
const rv1126b_time_sync_backend_t *rv1126b_time_sync_default_backend(void);
/* cmdline is the bounded NUL-separated /proc/PID/cmdline payload. */
bool rv1126b_time_sync_process_is_service(const char *comm, const char *cmdline, size_t length);

#endif
