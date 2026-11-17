/* SPDX-License-Identifier: Apache-2.0 */
#ifndef RV1126B_VIDEO_LIFECYCLE_H
#define RV1126B_VIDEO_LIFECYCLE_H

#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RV1126B_VIDEO_STOPPED,
    RV1126B_VIDEO_STARTING,
    RV1126B_VIDEO_RUNNING,
    RV1126B_VIDEO_RETRYING,
    RV1126B_VIDEO_FAILED
} rv1126b_video_state_t;

typedef enum {
    RV1126B_VIDEO_FAULT_NONE,
    RV1126B_VIDEO_FAULT_STARTUP,
    RV1126B_VIDEO_FAULT_CAPTURE,
    RV1126B_VIDEO_FAULT_CLEANUP
} rv1126b_video_fault_t;

typedef struct {
    rv1126b_video_state_t state;
    rv1126b_video_fault_t fault;
    unsigned int attempts;
    bool joinable;
    bool exited;
    bool cleanup_pending;
} rv1126b_video_status_t;

/* All hardware callbacks run on the video worker, except release() may be
 * retried by stop() after a successful join. prepare() emits no frames; step()
 * performs one bounded capture/encode operation. release() must be idempotent
 * and retain resources whose release failed. changed() receives only copied
 * diagnostic state and must not call start(), stop(), or destroy(). */
typedef struct {
    int (*prepare)(void *context);
    int (*step)(void *context);
    int (*release)(void *context);
    void (*changed)(void *context, const rv1126b_video_status_t *status);
} rv1126b_video_lifecycle_ops_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t wake;
    pthread_t thread;
    rv1126b_video_lifecycle_ops_t ops;
    void *context;
    rv1126b_video_status_t status;
    bool stopping;
    unsigned int retry_ms;
} rv1126b_video_lifecycle_t;

int rv1126b_video_lifecycle_init(rv1126b_video_lifecycle_t *lifecycle,
                                 const rv1126b_video_lifecycle_ops_t *ops, void *context,
                                 unsigned int retry_ms);

/* Serial SDK-owner operation. Accepts the worker promptly without calling frame
 * handlers on this thread. Hardware startup and up to three total attempts are
 * asynchronous and visible in status(). A failed start never leaves a worker.
 * A successfully accepted worker remains joinable even after failure/exit. */
int rv1126b_video_lifecycle_start(rv1126b_video_lifecycle_t *lifecycle);

/* Serial SDK-owner operation. Wakes backoff, joins in-flight callbacks, then
 * retries failed cleanup. Returns -1 while resources must be retained. */
int rv1126b_video_lifecycle_stop(rv1126b_video_lifecycle_t *lifecycle);

bool rv1126b_video_lifecycle_stopping(rv1126b_video_lifecycle_t *lifecycle);

void rv1126b_video_lifecycle_status(rv1126b_video_lifecycle_t *lifecycle,
                                    rv1126b_video_status_t *status);

/* Only after stop() succeeds. The caller owns and frees the enclosing object. */
void rv1126b_video_lifecycle_destroy(rv1126b_video_lifecycle_t *lifecycle);

#ifdef __cplusplus
}
#endif

#endif
