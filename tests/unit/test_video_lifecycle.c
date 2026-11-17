/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#include "video_lifecycle.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

static bool fail_worker_creation;

static int fixture_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*entry)(void *), void *argument);

#define pthread_create fixture_create
#include "video_lifecycle.c"
#undef pthread_create

static int fixture_create(pthread_t *thread, const pthread_attr_t *attributes,
                          void *(*entry)(void *), void *argument) {
    return fail_worker_creation ? EAGAIN : pthread_create(thread, attributes, entry, argument);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    rv1126b_video_lifecycle_t lifecycle;
    pthread_t caller;
    unsigned int prepares, steps, releases, callbacks, status_changes;
    rv1126b_video_state_t last_state;
    unsigned int startup_failures, capture_failures, cleanup_failures;
    bool hardware, block_prepare, prepare_entered, block_callback, callback_entered;
    bool stop_done;
    int stop_result;
} fixture_t;

static void pause_ms(unsigned int milliseconds) {
    const struct timespec delay = {.tv_sec = milliseconds / 1000,
                                   .tv_nsec = (long)(milliseconds % 1000) * 1000000};
    nanosleep(&delay, NULL);
}

static long long milliseconds(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int prepare(void *argument) {
    fixture_t *fixture = argument;
    pthread_mutex_lock(&fixture->mutex);
    assert(!fixture->hardware);
    fixture->hardware = true;
    ++fixture->prepares;
    fixture->prepare_entered = true;
    pthread_cond_broadcast(&fixture->condition);
    while (fixture->block_prepare)
        pthread_cond_wait(&fixture->condition, &fixture->mutex);
    bool failed = fixture->startup_failures > 0;
    if (failed)
        --fixture->startup_failures;
    pthread_mutex_unlock(&fixture->mutex);
    return failed ? -1 : 0;
}

static int step(void *argument) {
    fixture_t *fixture = argument;
    assert(!pthread_equal(fixture->caller, pthread_self()));
    pthread_mutex_lock(&fixture->mutex);
    assert(fixture->hardware);
    ++fixture->steps;
    bool failed = fixture->capture_failures > 0;
    if (failed)
        --fixture->capture_failures;
    else {
        ++fixture->callbacks;
        fixture->callback_entered = true;
        pthread_cond_broadcast(&fixture->condition);
        while (fixture->block_callback)
            pthread_cond_wait(&fixture->condition, &fixture->mutex);
    }
    pthread_mutex_unlock(&fixture->mutex);
    pause_ms(1);
    return failed ? -1 : 0;
}

static int release(void *argument) {
    fixture_t *fixture = argument;
    pthread_mutex_lock(&fixture->mutex);
    ++fixture->releases;
    bool failed = fixture->cleanup_failures > 0;
    if (failed)
        --fixture->cleanup_failures;
    else
        fixture->hardware = false;
    pthread_mutex_unlock(&fixture->mutex);
    return failed ? -1 : 0;
}

static void changed(void *argument, const rv1126b_video_status_t *status) {
    fixture_t *fixture = argument;
    rv1126b_video_status_t snapshot;
    /* A copied status callback may query health; no lifecycle lock is held. */
    rv1126b_video_lifecycle_status(&fixture->lifecycle, &snapshot);
    pthread_mutex_lock(&fixture->mutex);
    ++fixture->status_changes;
    fixture->last_state = status->state;
    pthread_mutex_unlock(&fixture->mutex);
}

static void init(fixture_t *fixture, unsigned int retry_ms) {
    *fixture = (fixture_t){0};
    assert(pthread_mutex_init(&fixture->mutex, NULL) == 0);
    assert(pthread_cond_init(&fixture->condition, NULL) == 0);
    fixture->caller = pthread_self();
    const rv1126b_video_lifecycle_ops_t ops = {
        .prepare = prepare, .step = step, .release = release, .changed = changed};
    assert(rv1126b_video_lifecycle_init(&fixture->lifecycle, &ops, fixture, retry_ms) == 0);
}

static rv1126b_video_status_t wait_state(fixture_t *fixture, rv1126b_video_state_t state,
                                         unsigned int attempts, bool exited) {
    rv1126b_video_status_t status;
    for (unsigned int elapsed = 0; elapsed < 3000; ++elapsed) {
        rv1126b_video_lifecycle_status(&fixture->lifecycle, &status);
        if (status.state == state && status.attempts == attempts && status.exited == exited)
            return status;
        pause_ms(1);
    }
    fprintf(stderr, "state=%d attempts=%u exited=%d; expected state=%d attempts=%u exited=%d\n",
            status.state, status.attempts, status.exited, state, attempts, exited);
    assert(0);
    return status;
}

static void destroy(fixture_t *fixture) {
    assert(rv1126b_video_lifecycle_stop(&fixture->lifecycle) == 0);
    assert(!fixture->hardware);
    rv1126b_video_lifecycle_destroy(&fixture->lifecycle);
    assert(pthread_cond_destroy(&fixture->condition) == 0);
    assert(pthread_mutex_destroy(&fixture->mutex) == 0);
}

static void test_creation_failure_is_reported_and_owns_no_worker(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fail_worker_creation = true;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == -1);
    rv1126b_video_status_t status;
    rv1126b_video_lifecycle_status(&fixture.lifecycle, &status);
    assert(status.state == RV1126B_VIDEO_FAILED && status.fault == RV1126B_VIDEO_FAULT_STARTUP);
    assert(status.exited && !status.joinable && !status.cleanup_pending);
    assert(fixture.status_changes == 1 && fixture.last_state == RV1126B_VIDEO_FAILED);
    assert(fixture.prepares == 0 && fixture.steps == 0 && fixture.releases == 0);
    fail_worker_creation = false;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    wait_state(&fixture, RV1126B_VIDEO_RUNNING, 1, false);
    destroy(&fixture);
}

static void test_prompt_start_and_duplicate_start(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.block_prepare = true;
    long long started = milliseconds();
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    assert(milliseconds() - started < 250);
    pthread_mutex_lock(&fixture.mutex);
    while (!fixture.prepare_entered)
        pthread_cond_wait(&fixture.condition, &fixture.mutex);
    pthread_mutex_unlock(&fixture.mutex);
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.prepares == 1 && fixture.callbacks == 0);
    fixture.block_prepare = false;
    pthread_cond_broadcast(&fixture.condition);
    pthread_mutex_unlock(&fixture.mutex);
    wait_state(&fixture, RV1126B_VIDEO_RUNNING, 1, false);
    destroy(&fixture);
}

static void test_startup_recovery_without_new_start(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.startup_failures = 2;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    wait_state(&fixture, RV1126B_VIDEO_RUNNING, 3, false);
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.prepares == 3 && fixture.releases == 2);
    pthread_mutex_unlock(&fixture.mutex);
    destroy(&fixture);
}

static void test_startup_exhaustion_is_joinable(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.startup_failures = 9;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    rv1126b_video_status_t status = wait_state(&fixture, RV1126B_VIDEO_FAILED, 3, true);
    assert(status.joinable && !status.cleanup_pending &&
           status.fault == RV1126B_VIDEO_FAULT_STARTUP);
    pthread_mutex_lock(&fixture.mutex);
    assert(fixture.prepares == 3 && fixture.releases == 3 && fixture.callbacks == 0);
    pthread_mutex_unlock(&fixture.mutex);
    assert(rv1126b_video_lifecycle_stop(&fixture.lifecycle) == 0);
    rv1126b_video_lifecycle_status(&fixture.lifecycle, &status);
    assert(!status.joinable && status.exited && status.state == RV1126B_VIDEO_STOPPED);
    destroy(&fixture);
}

static void test_capture_recovery_and_repeated_start_stop(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.capture_failures = 1;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    wait_state(&fixture, RV1126B_VIDEO_RUNNING, 2, false);
    assert(rv1126b_video_lifecycle_stop(&fixture.lifecycle) == 0);
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    wait_state(&fixture, RV1126B_VIDEO_RUNNING, 1, false);
    destroy(&fixture);
}

static void test_cleanup_failure_prevents_retry_and_retains_resources(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.startup_failures = 1;
    fixture.cleanup_failures = 2;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    rv1126b_video_status_t status = wait_state(&fixture, RV1126B_VIDEO_FAILED, 1, true);
    assert(status.fault == RV1126B_VIDEO_FAULT_CLEANUP && status.cleanup_pending &&
           status.joinable);
    assert(rv1126b_video_lifecycle_stop(&fixture.lifecycle) == -1);
    rv1126b_video_lifecycle_status(&fixture.lifecycle, &status);
    assert(!status.joinable && status.cleanup_pending && fixture.hardware);
    assert(fixture.prepares == 1);
    assert(rv1126b_video_lifecycle_stop(&fixture.lifecycle) == 0);
    assert(!fixture.hardware);
    destroy(&fixture);
}

static void *stop_worker(void *argument) {
    fixture_t *fixture = argument;
    int result = rv1126b_video_lifecycle_stop(&fixture->lifecycle);
    pthread_mutex_lock(&fixture->mutex);
    fixture->stop_result = result;
    fixture->stop_done = true;
    pthread_mutex_unlock(&fixture->mutex);
    return NULL;
}

static void test_stop_waits_for_callback(void) {
    fixture_t fixture;
    init(&fixture, 1);
    fixture.block_callback = true;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    pthread_mutex_lock(&fixture.mutex);
    while (!fixture.callback_entered)
        pthread_cond_wait(&fixture.condition, &fixture.mutex);
    pthread_mutex_unlock(&fixture.mutex);
    pthread_t stopper;
    assert(pthread_create(&stopper, NULL, stop_worker, &fixture) == 0);
    pause_ms(20);
    pthread_mutex_lock(&fixture.mutex);
    assert(!fixture.stop_done);
    fixture.block_callback = false;
    pthread_cond_broadcast(&fixture.condition);
    pthread_mutex_unlock(&fixture.mutex);
    assert(pthread_join(stopper, NULL) == 0);
    assert(fixture.stop_done && fixture.stop_result == 0);
    unsigned int callbacks = fixture.callbacks;
    pause_ms(20);
    assert(fixture.callbacks == callbacks);
    destroy(&fixture);
}

static void test_stop_wakes_retry_backoff(void) {
    fixture_t fixture;
    init(&fixture, 5000);
    fixture.startup_failures = 1;
    assert(rv1126b_video_lifecycle_start(&fixture.lifecycle) == 0);
    wait_state(&fixture, RV1126B_VIDEO_RETRYING, 1, false);
    long long started = milliseconds();
    assert(rv1126b_video_lifecycle_stop(&fixture.lifecycle) == 0);
    assert(milliseconds() - started < 250);
    assert(fixture.prepares == 1);
    destroy(&fixture);
}

int main(void) {
    test_creation_failure_is_reported_and_owns_no_worker();
    test_prompt_start_and_duplicate_start();
    test_startup_recovery_without_new_start();
    test_startup_exhaustion_is_joinable();
    test_capture_recovery_and_repeated_start_stop();
    test_cleanup_failure_prevents_retry_and_retains_resources();
    test_stop_waits_for_callback();
    test_stop_wakes_retry_backoff();
    puts("Video lifecycle fault-injection tests passed (8 scenarios)");
    return 0;
}
