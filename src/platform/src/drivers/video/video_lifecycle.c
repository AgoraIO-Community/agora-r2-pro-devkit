/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "video_lifecycle.h"

#include <errno.h>
#include <string.h>
#include <time.h>

#define VIDEO_ATTEMPTS 3

static void report(rv1126b_video_lifecycle_t *lifecycle) {
    if (lifecycle->ops.changed) {
        rv1126b_video_status_t status;
        rv1126b_video_lifecycle_status(lifecycle, &status);
        lifecycle->ops.changed(lifecycle->context, &status);
    }
}

static void transition(rv1126b_video_lifecycle_t *lifecycle, rv1126b_video_state_t state,
                       rv1126b_video_fault_t fault, unsigned int attempt, bool pending) {
    pthread_mutex_lock(&lifecycle->mutex);
    lifecycle->status.state = state;
    lifecycle->status.fault = fault;
    lifecycle->status.attempts = attempt;
    lifecycle->status.cleanup_pending = pending;
    pthread_mutex_unlock(&lifecycle->mutex);
    report(lifecycle);
}

bool rv1126b_video_lifecycle_stopping(rv1126b_video_lifecycle_t *lifecycle) {
    pthread_mutex_lock(&lifecycle->mutex);
    bool stopping = lifecycle->stopping;
    pthread_mutex_unlock(&lifecycle->mutex);
    return stopping;
}

static int backoff(rv1126b_video_lifecycle_t *lifecycle, unsigned int attempt) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return -1;
    const unsigned long long deadline = (unsigned long long)now.tv_sec * 1000 +
                                        now.tv_nsec / 1000000 + lifecycle->retry_ms * attempt;
    pthread_mutex_lock(&lifecycle->mutex);
    int result = 0;
    while (!lifecycle->stopping) {
        if (clock_gettime(CLOCK_MONOTONIC, &now)) {
            result = -1;
            break;
        }
        unsigned long long current = (unsigned long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (current >= deadline)
            break;
        struct timespec until;
#if defined(__APPLE__)
        const unsigned long long remaining = deadline - current;
        until.tv_sec = (time_t)(remaining / 1000);
        until.tv_nsec = (long)(remaining % 1000) * 1000000;
        int waited =
            pthread_cond_timedwait_relative_np(&lifecycle->wake, &lifecycle->mutex, &until);
#else
        until.tv_sec = (time_t)(deadline / 1000);
        until.tv_nsec = (long)(deadline % 1000) * 1000000;
        int waited = pthread_cond_timedwait(&lifecycle->wake, &lifecycle->mutex, &until);
#endif
        if (waited && waited != ETIMEDOUT) {
            result = -1;
            break;
        }
    }
    bool stopping = lifecycle->stopping;
    pthread_mutex_unlock(&lifecycle->mutex);
    return stopping ? 1 : result;
}

static void *worker(void *argument) {
    rv1126b_video_lifecycle_t *lifecycle = argument;
    rv1126b_video_fault_t fault = RV1126B_VIDEO_FAULT_NONE;
    unsigned int attempt = 0;
    bool pending = false;
    for (attempt = 1; attempt <= VIDEO_ATTEMPTS; ++attempt) {
        if (rv1126b_video_lifecycle_stopping(lifecycle))
            break;
        pending = true;
        transition(lifecycle, RV1126B_VIDEO_STARTING, RV1126B_VIDEO_FAULT_NONE, attempt, pending);
        int result = lifecycle->ops.prepare(lifecycle->context);
        fault = result ? RV1126B_VIDEO_FAULT_STARTUP : RV1126B_VIDEO_FAULT_NONE;
        if (!result && !rv1126b_video_lifecycle_stopping(lifecycle)) {
            transition(lifecycle, RV1126B_VIDEO_RUNNING, fault, attempt, pending);
            while (!rv1126b_video_lifecycle_stopping(lifecycle)) {
                if (lifecycle->ops.step(lifecycle->context)) {
                    fault = RV1126B_VIDEO_FAULT_CAPTURE;
                    break;
                }
            }
        }
        if (lifecycle->ops.release(lifecycle->context)) {
            fault = RV1126B_VIDEO_FAULT_CLEANUP;
            break;
        }
        pending = false;
        if (rv1126b_video_lifecycle_stopping(lifecycle) || attempt == VIDEO_ATTEMPTS)
            break;
        transition(lifecycle, RV1126B_VIDEO_RETRYING, fault, attempt, pending);
        if (backoff(lifecycle, attempt))
            break;
    }
    bool stopped = rv1126b_video_lifecycle_stopping(lifecycle) && !pending;
    pthread_mutex_lock(&lifecycle->mutex);
    lifecycle->status.state = stopped ? RV1126B_VIDEO_STOPPED : RV1126B_VIDEO_FAILED;
    lifecycle->status.fault = stopped ? RV1126B_VIDEO_FAULT_NONE : fault;
    lifecycle->status.attempts = attempt > VIDEO_ATTEMPTS ? VIDEO_ATTEMPTS : attempt;
    lifecycle->status.cleanup_pending = pending;
    lifecycle->status.exited = true;
    pthread_mutex_unlock(&lifecycle->mutex);
    report(lifecycle);
    return NULL;
}

int rv1126b_video_lifecycle_init(rv1126b_video_lifecycle_t *lifecycle,
                                 const rv1126b_video_lifecycle_ops_t *ops, void *context,
                                 unsigned int retry_ms) {
    if (!lifecycle || !ops || !ops->prepare || !ops->step || !ops->release || retry_ms > 10000)
        return -1;
    memset(lifecycle, 0, sizeof(*lifecycle));
    if (pthread_mutex_init(&lifecycle->mutex, NULL))
        return -1;
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes)) {
        pthread_mutex_destroy(&lifecycle->mutex);
        return -1;
    }
#if !defined(__APPLE__)
    if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC)) {
        pthread_condattr_destroy(&attributes);
        pthread_mutex_destroy(&lifecycle->mutex);
        return -1;
    }
#endif
    const int result = pthread_cond_init(&lifecycle->wake, &attributes);
    pthread_condattr_destroy(&attributes);
    if (result) {
        pthread_mutex_destroy(&lifecycle->mutex);
        return -1;
    }
    lifecycle->ops = *ops;
    lifecycle->context = context;
    lifecycle->retry_ms = retry_ms;
    lifecycle->status.state = RV1126B_VIDEO_STOPPED;
    lifecycle->status.exited = true;
    return 0;
}

int rv1126b_video_lifecycle_start(rv1126b_video_lifecycle_t *lifecycle) {
    rv1126b_video_status_t status;
    rv1126b_video_lifecycle_status(lifecycle, &status);
    if (status.joinable && !status.exited)
        return 0;
    if ((status.joinable || status.cleanup_pending) && rv1126b_video_lifecycle_stop(lifecycle))
        return -1;
    pthread_mutex_lock(&lifecycle->mutex);
    lifecycle->stopping = false;
    lifecycle->status = (rv1126b_video_status_t){.state = RV1126B_VIDEO_STARTING};
    int result = pthread_create(&lifecycle->thread, NULL, worker, lifecycle);
    if (!result)
        lifecycle->status.joinable = true;
    else {
        lifecycle->status.state = RV1126B_VIDEO_FAILED;
        lifecycle->status.fault = RV1126B_VIDEO_FAULT_STARTUP;
        lifecycle->status.exited = true;
    }
    pthread_mutex_unlock(&lifecycle->mutex);
    if (result)
        report(lifecycle);
    return result ? -1 : 0;
}

int rv1126b_video_lifecycle_stop(rv1126b_video_lifecycle_t *lifecycle) {
    pthread_mutex_lock(&lifecycle->mutex);
    lifecycle->stopping = true;
    pthread_cond_broadcast(&lifecycle->wake);
    bool joinable = lifecycle->status.joinable;
    pthread_mutex_unlock(&lifecycle->mutex);
    if (joinable) {
        if (pthread_join(lifecycle->thread, NULL))
            return -1;
        pthread_mutex_lock(&lifecycle->mutex);
        lifecycle->status.joinable = false;
        pthread_mutex_unlock(&lifecycle->mutex);
    }
    rv1126b_video_status_t status;
    rv1126b_video_lifecycle_status(lifecycle, &status);
    if (status.cleanup_pending && lifecycle->ops.release(lifecycle->context))
        return -1;
    transition(lifecycle, RV1126B_VIDEO_STOPPED, RV1126B_VIDEO_FAULT_NONE, status.attempts, false);
    return 0;
}

void rv1126b_video_lifecycle_status(rv1126b_video_lifecycle_t *lifecycle,
                                    rv1126b_video_status_t *status) {
    pthread_mutex_lock(&lifecycle->mutex);
    *status = lifecycle->status;
    pthread_mutex_unlock(&lifecycle->mutex);
}

void rv1126b_video_lifecycle_destroy(rv1126b_video_lifecycle_t *lifecycle) {
    pthread_cond_destroy(&lifecycle->wake);
    pthread_mutex_destroy(&lifecycle->mutex);
}
