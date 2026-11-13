/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
static int native_pipe2(int descriptors[2], int flags) {
    (void)flags;
    if (pipe(descriptors))
        return -1;
    for (int i = 0; i < 2; ++i) {
        if (fcntl(descriptors[i], F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(descriptors[i], F_SETFL, O_NONBLOCK) < 0) {
            close(descriptors[0]);
            close(descriptors[1]);
            return -1;
        }
    }
    return 0;
}
#define pipe2 native_pipe2
#include "../../components/mybot_platform/src/drivers/input/evdev_input.c"
#include "check.h"

static const rv1126b_platform_config_t config = {
    .input_device = "stdin",
    .touch_device = "none",
    .talk_key = KEY_MENU,
};
const rv1126b_platform_config_t *rv1126b_platform_config(void) {
    return &config;
}
mybot_state_t mybot_get_state(void) {
    return MYBOT_STATE_READY;
}
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool callback_entered, release_callback, destroyed;
static unsigned calls;
static void handler(mybot_key_event_t event, void *user) {
    CHECK(user == &calls && event == MYBOT_KEY_EVENT_PAIR);
    pthread_mutex_lock(&lock);
    ++calls;
    callback_entered = true;
    pthread_cond_broadcast(&changed);
    while (!release_callback)
        pthread_cond_wait(&changed, &lock);
    pthread_mutex_unlock(&lock);
}
static void *destroy_worker(void *ctx) {
    rv1126b_key_ops()->destroy(ctx);
    pthread_mutex_lock(&lock);
    destroyed = true;
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void) {
    int input[2];
    CHECK(pipe(input) == 0);
    int saved = dup(STDIN_FILENO);
    CHECK(saved >= 0 && dup2(input[0], STDIN_FILENO) >= 0);
    for (int i = 0; i < 5; ++i) {
        void *ctx = NULL;
        CHECK(rv1126b_key_ops()->init(&ctx, handler, &calls) == 0);
        uint32_t started = now_ms();
        rv1126b_key_ops()->destroy(ctx);
        CHECK((uint32_t)(now_ms() - started) < 1000);
    }
    void *ctx = NULL;
    CHECK(rv1126b_key_ops()->init(&ctx, handler, &calls) == 0);
    CHECK(write(input[1], "p", 1) == 1);
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&lock);
    while (!callback_entered)
        CHECK(pthread_cond_timedwait(&changed, &lock, &deadline) == 0);
    pthread_mutex_unlock(&lock);
    pthread_t stopping;
    CHECK(pthread_create(&stopping, NULL, destroy_worker, ctx) == 0);
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 30000000};
    nanosleep(&pause, NULL);
    pthread_mutex_lock(&lock);
    CHECK(!destroyed);
    release_callback = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    CHECK(pthread_join(stopping, NULL) == 0);
    CHECK(destroyed && calls == 1);
    CHECK(write(input[1], "p", 1) == 1);
    nanosleep(&pause, NULL);
    CHECK(calls == 1);
    CHECK(dup2(saved, STDIN_FILENO) >= 0);
    close(saved);
    close(input[0]);
    close(input[1]);
    puts("Input idle shutdown, repeated lifecycle and in-flight callback join: PASS");
    return 0;
}
