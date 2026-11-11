/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_platform.h"
#include "linux_platform_adapters.h"

#include <linux/input.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_changed = PTHREAD_COND_INITIALIZER;
static int s_key_events;
static int s_network_events;

static void on_key(mybot_key_event_t event, void *user_data) {
    CHECK(user_data == &s_key_events);
    CHECK(event == MYBOT_KEY_EVENT_PAIR);
    pthread_mutex_lock(&s_lock);
    ++s_key_events;
    pthread_cond_signal(&s_changed);
    pthread_mutex_unlock(&s_lock);
}

static void on_network(mybot_wifi_event_t event, void *user_data) {
    CHECK(user_data == &s_network_events);
    CHECK(event == MYBOT_WIFI_EVENT_STA_CONNECTED ||
          event == MYBOT_WIFI_EVENT_STA_DISCONNECTED || event == MYBOT_WIFI_EVENT_FAILED);
    pthread_mutex_lock(&s_lock);
    ++s_network_events;
    pthread_cond_signal(&s_changed);
    pthread_mutex_unlock(&s_lock);
}

static void wait_event(int *count) {
    struct timespec deadline;
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&s_lock);
    while (*count == 0) {
        CHECK(pthread_cond_timedwait(&s_changed, &s_lock, &deadline) == 0);
    }
    pthread_mutex_unlock(&s_lock);
}

static void test_keys(void) {
    int input[2];
    CHECK(pipe(input) == 0);
    int saved_stdin = dup(STDIN_FILENO);
    CHECK(saved_stdin >= 0);
    CHECK(dup2(input[0], STDIN_FILENO) >= 0);
    const mybot_key_ops_t *keys = rv1126b_key_ops();
    void *ctx = NULL;
    CHECK(keys->init(&ctx, on_key, &s_key_events) == 0);
    CHECK(write(input[1], "p", 1) == 1);
    wait_event(&s_key_events);
    keys->destroy(ctx);
    CHECK(s_key_events == 1);
    CHECK(write(input[1], "p", 1) == 1);
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 200000000};
    nanosleep(&pause, NULL);
    CHECK(s_key_events == 1);
    CHECK(dup2(saved_stdin, STDIN_FILENO) >= 0);
    close(saved_stdin);
    close(input[0]);
    close(input[1]);
}

static void test_audio(void) {
    const mybot_audio_capture_ops_t *capture = rv1126b_capture_ops();
    const mybot_audio_playback_ops_t *playback = rv1126b_playback_ops();
    int16_t pcm[960] = {0};
    void *ctx = NULL;
    CHECK(capture->init(&ctx, 48000, 1, 16) < 0);
    CHECK(ctx == NULL);
    CHECK(capture->init(&ctx, 16000, 1, 16) == 0);
    CHECK(capture->start(ctx) == 0);
    CHECK(capture->read(ctx, pcm, 960) == 960);
    CHECK(capture->read(ctx, pcm, 320) == 320);
    CHECK(capture->stop(ctx) == 0);
    CHECK(capture->read(ctx, pcm, 960) == 0);
    CHECK(capture->start(ctx) == 0);
    CHECK(capture->read(ctx, pcm, 960) == 960);
    CHECK(capture->stop(ctx) == 0);
    capture->destroy(ctx);
    ctx = NULL;
    CHECK(playback->init(&ctx, 16000, 2, 16) < 0);
    CHECK(playback->init(&ctx, 16000, 1, 16) == 0);
    CHECK(playback->start(ctx) == 0);
    CHECK(playback->write(ctx, pcm, 960) == 960);
    CHECK(playback->stop(ctx) == 0);
    CHECK(playback->write(ctx, pcm, 960) == 0);
    playback->destroy(ctx);
}

static void test_kv(void) {
    char parent[] = "/tmp/botstation-test-XXXXXX";
    CHECK(mkdtemp(parent) != NULL);
    char store[128];
    CHECK(snprintf(store, sizeof(store), "%s/store", parent) < (int)sizeof(store));
    CHECK(setenv("MYBOT_KV_STORE_DIR", store, 1) == 0);
    const mybot_kv_store_ops_t *kv = linux_kv_store_platform_file_ops();
    void *ctx = NULL;
    CHECK(kv->init(&ctx) == 0);
    const char data[] = "test-credential";
    char output[sizeof(data)];
    size_t len = 999;
    CHECK(kv->get(ctx, "missing", output, sizeof(output), &len) == MYBOT_ERR_NOT_FOUND);
    CHECK(len == 999);
    CHECK(kv->set(ctx, "token", data, sizeof(data)) == 0);
    CHECK(kv->get(ctx, "token", output, sizeof(output) - 1, &len) < 0);
    CHECK(len == 999);
    CHECK(kv->get(ctx, "token", output, sizeof(output), &len) == 0);
    CHECK(len == sizeof(data) && memcmp(output, data, len) == 0);
    CHECK(kv->set(ctx, "../escape", data, sizeof(data)) < 0);
    kv->destroy(ctx);
    ctx = NULL;
    CHECK(kv->init(&ctx) == 0);
    CHECK(kv->get(ctx, "token", output, sizeof(output), &len) == 0);
    CHECK(len == sizeof(data) && memcmp(output, data, len) == 0);
    CHECK(kv->erase(ctx, "token") == 0);
    CHECK(kv->erase(ctx, "token") == 0);
    kv->destroy(ctx);
    CHECK(kv->init(&ctx) == 0);
    CHECK(kv->get(ctx, "token", output, sizeof(output), &len) == MYBOT_ERR_NOT_FOUND);
    kv->destroy(ctx);
    CHECK(rmdir(store) == 0);
    CHECK(rmdir(parent) == 0);
}

int main(void) {
    const rv1126b_platform_config_t config = {
        .capture_device = "null", .playback_device = "null",
        .network_interface = "", .input_device = "", .talk_key = KEY_PLAYPAUSE,
    };
    CHECK(rv1126b_platform_register(&config) == 0);
    CHECK(rv1126b_platform_register(&config) < 0);
    test_keys();
    test_audio();
    test_kv();
    const mybot_wifi_ops_t *network = rv1126b_network_ops();
    void *ctx = NULL;
    CHECK(network->init(&ctx, "test-device", on_network, &s_network_events) == 0);
    wait_event(&s_network_events);
    network->destroy(ctx);
    int events = s_network_events;
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 200000000};
    nanosleep(&pause, NULL);
    CHECK(s_network_events == events);
    puts("BotStation adapter tests passed");
    return 0;
}
