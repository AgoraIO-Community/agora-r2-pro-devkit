/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static mybot_state_t s_state = MYBOT_STATE_PAIRING;
static unsigned s_events;

const rv1126b_platform_config_t *__wrap_rv1126b_platform_config(void) {
    static const rv1126b_platform_config_t config = {.input_device = "none"};
    return &config;
}

mybot_state_t __wrap_mybot_get_state(void) {
    pthread_mutex_lock(&s_lock);
    mybot_state_t state = s_state;
    pthread_mutex_unlock(&s_lock);
    return state;
}

static void on_key(mybot_key_event_t event, void *user_data) {
    CHECK(user_data == &s_events);
    CHECK(event == MYBOT_KEY_EVENT_CONVERSATION_START);
    pthread_mutex_lock(&s_lock);
    ++s_events;
    pthread_cond_signal(&s_changed);
    pthread_mutex_unlock(&s_lock);
}

static void set_state(mybot_state_t state) {
    pthread_mutex_lock(&s_lock);
    s_state = state;
    pthread_mutex_unlock(&s_lock);
}

static void expect_no_new_events(unsigned expected) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 300000000};
    nanosleep(&pause, NULL);
    pthread_mutex_lock(&s_lock);
    CHECK(s_events == expected);
    pthread_mutex_unlock(&s_lock);
}

int main(void) {
    const mybot_key_ops_t *keys = rv1126b_key_ops();
    void *ctx = NULL;
    CHECK(keys->init(&ctx, on_key, &s_events) == 0);
    expect_no_new_events(0);
    set_state(MYBOT_STATE_READY);
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&s_lock);
    while (s_events == 0) {
        CHECK(pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == 0);
    }
    CHECK(s_events == 1);
    pthread_mutex_unlock(&s_lock);
    expect_no_new_events(1);
    set_state(MYBOT_STATE_IN_CONVERSATION);
    expect_no_new_events(1);
    set_state(MYBOT_STATE_READY);
    expect_no_new_events(1);
    set_state(MYBOT_STATE_WIFI_DISCONNECTED);
    expect_no_new_events(1);
    set_state(MYBOT_STATE_READY);
    expect_no_new_events(1);
    keys->destroy(ctx);
    CHECK(keys->init(&ctx, on_key, &s_events) == 0);
    expect_no_new_events(1);
    keys->destroy(ctx);
    puts("BotStation automatic conversation start tests passed");
    return 0;
}
