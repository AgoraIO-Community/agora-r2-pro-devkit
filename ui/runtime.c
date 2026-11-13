/* SPDX-License-Identifier: Apache-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_ui.h"
#include "drm.h"
#include "renderer.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    pthread_t thread;
    bool started, stopping, accepting, pending, startup_done, failed;
    const char *device;
    unsigned rotation;
    int expression;
    mybot_lcd_content_t latest;
    mybot_display_runtime_status_t status;
} s_ui = {.lock = PTHREAD_MUTEX_INITIALIZER, .ready = PTHREAD_COND_INITIALIZER};

static uint32_t now_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return 0;
    return (uint32_t)((uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static void *ui_worker(void *unused) {
    (void)unused;
    int result = rv1126b_drm_start(s_ui.device, s_ui.rotation);
    if (result == 0)
        result = rv1126b_renderer_create(rv1126b_drm_present, NULL, now_ms());
    pthread_mutex_lock(&s_ui.lock);
    s_ui.failed = result < 0;
    s_ui.accepting = result == 0;
    s_ui.startup_done = true;
    pthread_cond_signal(&s_ui.ready);
    pthread_mutex_unlock(&s_ui.lock);
    const struct timespec interval = {.tv_sec = 0, .tv_nsec = 10000000};
    while (result == 0) {
        pthread_mutex_lock(&s_ui.lock);
        bool stop = s_ui.stopping;
        bool update = s_ui.pending;
        mybot_lcd_content_t content = s_ui.latest;
        mybot_display_runtime_status_t status = s_ui.status;
        int expression = s_ui.expression;
        s_ui.pending = false;
        pthread_mutex_unlock(&s_ui.lock);
        if (stop)
            break;
        uint32_t now = now_ms();
        if (update)
            rv1126b_renderer_update(&content, &status, now);
        (void)rv1126b_renderer_expression(expression);
        rv1126b_renderer_step(now);
        if (rv1126b_renderer_failed()) {
            fprintf(stderr, "UI framebuffer update failed\n");
            result = -1;
            break;
        }
        nanosleep(&interval, NULL);
    }
    rv1126b_renderer_destroy();
    if (rv1126b_drm_stop() < 0)
        result = -1;
    pthread_mutex_lock(&s_ui.lock);
    s_ui.accepting = false;
    s_ui.failed = s_ui.failed || result < 0;
    pthread_mutex_unlock(&s_ui.lock);
    return NULL;
}

int rv1126b_ui_start(const char *device, unsigned rotation) {
    pthread_mutex_lock(&s_ui.lock);
    if (s_ui.started || !device || !device[0]) {
        pthread_mutex_unlock(&s_ui.lock);
        return -1;
    }
    s_ui.device = device;
    s_ui.rotation = rotation;
    s_ui.stopping = s_ui.startup_done = s_ui.failed = false;
    s_ui.expression = -1;
    s_ui.latest = (mybot_lcd_content_t){MYBOT_LCD_SCREEN_STARTING, {0}, 0};
    s_ui.status = (mybot_display_runtime_status_t){"starting", false};
    s_ui.pending = true;
    if (pthread_create(&s_ui.thread, NULL, ui_worker, NULL) != 0) {
        pthread_mutex_unlock(&s_ui.lock);
        return -1;
    }
    s_ui.started = true;
    while (!s_ui.startup_done)
        pthread_cond_wait(&s_ui.ready, &s_ui.lock);
    bool failed = s_ui.failed;
    pthread_mutex_unlock(&s_ui.lock);
    if (failed)
        (void)rv1126b_ui_stop();
    return failed ? -1 : 0;
}

int rv1126b_ui_stop(void) {
    pthread_mutex_lock(&s_ui.lock);
    bool started = s_ui.started;
    s_ui.stopping = true;
    s_ui.accepting = false;
    pthread_mutex_unlock(&s_ui.lock);
    if (!started)
        return 0;
    if (pthread_join(s_ui.thread, NULL) != 0)
        return -1;
    pthread_mutex_lock(&s_ui.lock);
    s_ui.started = false;
    bool failed = s_ui.failed;
    pthread_mutex_unlock(&s_ui.lock);
    return failed ? -1 : 0;
}

void rv1126b_ui_show(const mybot_lcd_content_t *content) {
    if (!content)
        return;
    pthread_mutex_lock(&s_ui.lock);
    if (s_ui.accepting) {
        s_ui.latest = *content;
        s_ui.pending = true;
    }
    pthread_mutex_unlock(&s_ui.lock);
}

void rv1126b_ui_set_state(mybot_state_t state) {
    const char *name = "unknown";
    switch (state) {
    case MYBOT_STATE_STOPPED:
        name = "stopped";
        break;
    case MYBOT_STATE_WIFI_PROVISIONING:
        name = "waiting_network";
        break;
    case MYBOT_STATE_STARTING_SERVICES:
        name = "starting_services";
        break;
    case MYBOT_STATE_READY:
        name = "ready";
        break;
    case MYBOT_STATE_WIFI_DISCONNECTED:
        name = "offline";
        break;
    case MYBOT_STATE_FAILED:
        name = "failed";
        break;
    case MYBOT_STATE_STOPPING:
        name = "stopping";
        break;
    case MYBOT_STATE_IN_CONVERSATION:
        name = "in_conversation";
        break;
    case MYBOT_STATE_PAIRING:
        name = "pairing";
        break;
    }
    pthread_mutex_lock(&s_ui.lock);
    snprintf(s_ui.status.state, sizeof(s_ui.status.state), "%s", name);
    s_ui.status.in_conversation = state == MYBOT_STATE_IN_CONVERSATION;
    s_ui.pending = true;
    pthread_mutex_unlock(&s_ui.lock);
}

void rv1126b_ui_demo_expression(int expression) {
    pthread_mutex_lock(&s_ui.lock);
    s_ui.expression = expression;
    pthread_mutex_unlock(&s_ui.lock);
}

int rv1126b_ui_failed(void) {
    pthread_mutex_lock(&s_ui.lock);
    bool failed = s_ui.failed;
    pthread_mutex_unlock(&s_ui.lock);
    return failed ? 1 : 0;
}

static int lcd_init(void **out) {
    pthread_mutex_lock(&s_ui.lock);
    bool accepting = s_ui.accepting;
    pthread_mutex_unlock(&s_ui.lock);
    if (!out || !accepting)
        return -1;
    *out = &s_ui;
    return 0;
}

static int lcd_render(void *context, const mybot_lcd_content_t *content) {
    (void)context;
    rv1126b_ui_show(content);
    return rv1126b_ui_failed() ? -1 : 0;
}

static void lcd_destroy(void *context) {
    (void)context;
    /* The application owns the display through network wait and SDK shutdown. */
    const mybot_lcd_content_t content = {MYBOT_LCD_SCREEN_STOPPING, {0}, 0};
    rv1126b_ui_show(&content);
}

const mybot_lcd_ops_t *rv1126b_ui_lcd_ops(void) {
    static const mybot_lcd_ops_t ops = {
        .init = lcd_init, .render = lcd_render, .destroy = lcd_destroy};
    return &ops;
}
