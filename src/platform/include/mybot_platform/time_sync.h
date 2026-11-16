/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_TIME_SYNC_H
#define RV1126B_TIME_SYNC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rv1126b_time_sync rv1126b_time_sync_t;

typedef struct {
    unsigned int timeout_seconds; /* 1..300; NULL config uses 30. */
    const char *helper_path;      /* Empty defaults to waiting for OS time. */
} rv1126b_time_sync_config_t;

typedef enum {
    RV1126B_TIME_WAIT_NETWORK,
    RV1126B_TIME_WAIT_CLOCK,
    RV1126B_TIME_READY,
    RV1126B_TIME_TIMEOUT,
    RV1126B_TIME_FAILED
} rv1126b_time_sync_state_t;

/* Product-owned clock plausibility gate, independent of SDK lifecycle. A ready
 * clock is >= 2026-01-01 UTC; this does not prove precise NTP synchronization.
 * No helper is inspected or started while the clock is already plausible. */
rv1126b_time_sync_t *rv1126b_time_sync_create(const rv1126b_time_sync_config_t *config);
/* Call from one product thread about every 100 ms. Offline time does not consume
 * the monotonic deadline. An explicit absolute helper runs without arguments,
 * once per online window, only when no existing time service is found. Polling
 * never waits for helper execution; cancelled ownership is reaped on later polls. */
rv1126b_time_sync_state_t rv1126b_time_sync_poll(rv1126b_time_sync_t *sync, bool network_ready);
/* An actionable, non-sensitive diagnostic valid until the next poll. */
const char *rv1126b_time_sync_error(const rv1126b_time_sync_t *sync);
/* Cancels/reaps only the helper process group created by this instance. */
void rv1126b_time_sync_destroy(rv1126b_time_sync_t *sync);

#ifdef __cplusplus
}
#endif
#endif
