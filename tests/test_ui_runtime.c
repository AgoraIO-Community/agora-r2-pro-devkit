/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_ui.h"
#include "renderer.h"
#include "check.h"
#include <pthread.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t test_lock = PTHREAD_MUTEX_INITIALIZER;
static int failure, started, stopped, renders;
static char last_code[16];
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
    (void)status;
    (void)now;
    pthread_mutex_lock(&test_lock);
    memcpy(last_code, content->pair_code, sizeof(last_code));
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

int main(void) {
    for (failure = 1; failure <= 2; ++failure) {
        CHECK(rv1126b_ui_start("mock", 270) < 0);
        CHECK(rv1126b_ui_stop() == 0);
    }
    failure = 0;
    for (int i = 0; i < 5; ++i) {
        CHECK(rv1126b_ui_start("mock", 270) == 0);
        CHECK(rv1126b_ui_start("mock", 270) < 0);
        void *ctx;
        CHECK(rv1126b_ui_lcd_ops()->init(&ctx) == 0);
        mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_PAIR_CODE, "000042", 0};
        CHECK(rv1126b_ui_lcd_ops()->render(ctx, &content) == 0);
        memset(content.pair_code, 'x', sizeof(content.pair_code));
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 1000000};
        bool copied = false;
        for (int attempt = 0; attempt < 1000 && !copied; ++attempt) {
            pthread_mutex_lock(&test_lock);
            copied = strcmp(last_code, "000042") == 0;
            pthread_mutex_unlock(&test_lock);
            if (!copied)
                nanosleep(&pause, NULL);
        }
        CHECK(copied);
        rv1126b_ui_lcd_ops()->destroy(ctx);
        CHECK(rv1126b_ui_stop() == 0);
        CHECK(rv1126b_ui_stop() == 0);
    }
    CHECK(started == stopped && renders > 0);
    puts("UI failed startup cleanup, copied snapshots and repeated worker shutdown: PASS");
    return 0;
}
