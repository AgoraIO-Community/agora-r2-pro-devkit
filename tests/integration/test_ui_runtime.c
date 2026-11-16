/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "mybot_platform/display.h"
#include "mybot_ui/renderer.h"
#include "check.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static int failure, started, stopped, renders, last_clock_state;
static char last_code[16];
static char last_ssid[33];
int rv1126b_drm_start(const char *path, unsigned rotation) {
    CHECK(!strcmp(path, "mock") && rotation == 270);
    ++started;
    return failure == 1 ? -1 : 0;
}
int rv1126b_drm_stop(void) {
    ++stopped;
    return 0;
}
int rv1126b_drm_present(const uint32_t *canvas, const rv1126b_rect_t *area, void *user) {
    (void)canvas;
    (void)area;
    (void)user;
    return 0;
}
int rv1126b_renderer_create(rv1126b_present_fn present, void *user, uint32_t now) {
    (void)user;
    (void)now;
    CHECK(present);
    return failure == 2 ? -1 : 0;
}
void rv1126b_renderer_destroy(void) {
}
void rv1126b_renderer_update(const mybot_lcd_content_t *content,
                             const mybot_display_runtime_status_t *status, uint32_t now) {
    (void)now;
    pthread_mutex_lock(&test_lock);
    memcpy(last_code, content->pair_code, sizeof(last_code));
    memcpy(last_ssid, status->provisioning_ssid, sizeof(last_ssid));
    last_clock_state = status->clock_state;
    ++renders;
    pthread_mutex_unlock(&test_lock);
}
void rv1126b_renderer_step(uint32_t now) {
    (void)now;
}
int rv1126b_renderer_expression(int expression) {
    (void)expression;
    return 0;
}
int rv1126b_renderer_failed(void) {
    return 0;
}

static void wait_for_clock(int expected, int previous_renders) {
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
    bool observed = false;
    for (int attempt = 0; attempt < 1000 && !observed; ++attempt) {
        pthread_mutex_lock(&test_lock);
        observed = last_clock_state == expected && renders > previous_renders;
        pthread_mutex_unlock(&test_lock);
        if (!observed)
            nanosleep(&pause, NULL);
    }
    CHECK(observed);
}

static int rendered(void) {
    pthread_mutex_lock(&test_lock);
    int count = renders;
    pthread_mutex_unlock(&test_lock);
    return count;
}

int main(void) {
    for (failure = 1; failure <= 2; ++failure) {
        CHECK(rv1126b_ui_start("mock", 270) < 0);
        CHECK(rv1126b_ui_stop() == 0);
    }
    failure = 0;
    for (int i = 0; i < 5; ++i) {
        int before_start = rendered();
        CHECK(rv1126b_ui_start("mock", 270) == 0);
        wait_for_clock(0, before_start);
        CHECK(rv1126b_ui_start("mock", 270) < 0);
        void *ctx;
        CHECK(rv1126b_ui_lcd_ops()->init(&ctx) == 0);
        char ssid[] = "mybot-abcd";
        rv1126b_ui_provisioning(ssid);
        memset(ssid, 'x', sizeof(ssid) - 1);
        mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_PAIR_CODE, "000042", 0};
        CHECK(rv1126b_ui_lcd_ops()->render(ctx, &content) == 0);
        memset(content.pair_code, 'x', sizeof(content.pair_code));
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
        bool copied = false;
        for (int attempt = 0; attempt < 1000 && !copied; ++attempt) {
            pthread_mutex_lock(&test_lock);
            copied = strcmp(last_code, "000042") == 0 && strcmp(last_ssid, "mybot-abcd") == 0;
            pthread_mutex_unlock(&test_lock);
            if (!copied)
                nanosleep(&pause, NULL);
        }
        CHECK(copied);
        int before = rendered();
        rv1126b_ui_time_status(1);
        wait_for_clock(1, before);
        before = rendered();
        rv1126b_ui_time_status(2);
        wait_for_clock(2, before);
        before = rendered();
        rv1126b_ui_time_status(-1);
        rv1126b_ui_time_status(3);
        rv1126b_ui_show(&content);
        wait_for_clock(2, before);
        before = rendered();
        rv1126b_ui_time_status(0);
        wait_for_clock(0, before);
        /* Stop with an active gate; the next display lifecycle resets it. */
        before = rendered();
        rv1126b_ui_time_status(2);
        wait_for_clock(2, before);
        rv1126b_ui_lcd_ops()->destroy(ctx);
        CHECK(rv1126b_ui_stop() == 0);
        CHECK(rv1126b_ui_stop() == 0);
    }
    CHECK(started == stopped && renders > 0);
    puts("UI copied snapshots, product clock gate and repeated worker shutdown: PASS");
    return 0;
}
