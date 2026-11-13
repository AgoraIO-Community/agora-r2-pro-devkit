/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_ui/renderer.h"
#include "expression_policy.h"
#include "lvgl_view.h"
#include "pixel_glasses.h"
#include "ui_config.h"

#include <stdlib.h>
#include <string.h>

static struct {
    lv_display_t *display;
    uint32_t *draw;
    uint32_t *canvas;
    rv1126b_present_fn present;
    void *user;
    mybot_expression_policy_t policy;
    int override_expression;
    bool failed;
    bool initialized;
} s_renderer;
static uint32_t s_tick;

static uint32_t tick(void) {
    return s_tick;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    rv1126b_rect_t rect = {area->x1, area->y1, area->x2, area->y2};
    if (rect.x1 < 0 || rect.y1 < 0 || rect.x2 >= RV1126B_UI_WIDTH || rect.y2 >= RV1126B_UI_HEIGHT ||
        rect.x2 < rect.x1 || rect.y2 < rect.y1) {
        s_renderer.failed = true;
    } else {
        size_t row_bytes = (size_t)(rect.x2 - rect.x1 + 1) * sizeof(uint32_t);
        size_t stride = lv_draw_buf_width_to_stride((uint32_t)(rect.x2 - rect.x1 + 1),
                                                    LV_COLOR_FORMAT_XRGB8888);
        for (int y = rect.y1; y <= rect.y2; ++y) {
            memcpy(s_renderer.canvas + (size_t)y * RV1126B_UI_WIDTH + rect.x1, pixels, row_bytes);
            pixels += stride;
        }
        if (s_renderer.present &&
            s_renderer.present(s_renderer.canvas, &rect, s_renderer.user) < 0) {
            s_renderer.failed = true;
        }
    }
    lv_display_flush_ready(display);
}

int rv1126b_renderer_create(rv1126b_present_fn present, void *user, uint32_t now) {
    if (s_renderer.initialized)
        return -1;
    s_tick = now;
    lv_init();
    s_renderer.initialized = true;
    s_renderer.override_expression = -1;
    s_renderer.present = present;
    s_renderer.user = user;
    lv_tick_set_cb(tick);
    s_renderer.canvas = calloc(RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT, sizeof(uint32_t));
    s_renderer.draw = calloc(RV1126B_UI_WIDTH * 32, sizeof(uint32_t));
    s_renderer.display = lv_display_create(RV1126B_UI_WIDTH, RV1126B_UI_HEIGHT);
    if (!s_renderer.canvas || !s_renderer.draw || !s_renderer.display)
        goto fail;
    lv_display_set_color_format(s_renderer.display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(s_renderer.display, s_renderer.draw, NULL,
                           RV1126B_UI_WIDTH * 32 * sizeof(uint32_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_renderer.display, flush);
    mybot_expression_policy_init(&s_renderer.policy);
    if (mybot_lvgl_view_create(s_renderer.display) < 0)
        goto fail;
    return 0;
fail:
    rv1126b_renderer_destroy();
    return -1;
}

void rv1126b_renderer_update(const mybot_lcd_content_t *content,
                             const mybot_display_runtime_status_t *status, uint32_t now) {
    if (!s_renderer.initialized || !content)
        return;
    s_tick = now;
    mybot_expression_policy_update(&s_renderer.policy, content, now);
    mybot_pixel_glasses_update(content, NULL, status);
}

void rv1126b_renderer_step(uint32_t now) {
    if (!s_renderer.initialized)
        return;
    s_tick = now;
    mybot_expression_t expression;
    bool replay;
    const uint32_t transition =
        CONFIG_MYBOT_LVGL_UI_ANIMATIONS ? MYBOT_EXPRESSION_TRANSITION_MS : 0;
    if (s_renderer.override_expression >= 0) {
        (void)mybot_pixel_glasses_show_expression(
            (mybot_expression_t)s_renderer.override_expression);
    } else if (mybot_expression_policy_select(&s_renderer.policy, now, transition, &expression,
                                              &replay)) {
        int result = replay ? mybot_pixel_glasses_replay_expression(expression)
                            : mybot_pixel_glasses_show_expression(expression);
        if (result == 0)
            mybot_expression_policy_displayed(&s_renderer.policy, now);
    }
    (void)lv_timer_handler();
}

int rv1126b_renderer_expression(int expression) {
    if (expression < -1 || expression >= MYBOT_EXPRESSION_COUNT)
        return -1;
    s_renderer.override_expression = expression;
    return 0;
}

const uint32_t *rv1126b_renderer_canvas(void) {
    if (!s_renderer.display)
        return NULL;
    lv_refr_now(s_renderer.display);
    return s_renderer.canvas;
}

int rv1126b_renderer_failed(void) {
    return s_renderer.failed ? 1 : 0;
}

void rv1126b_renderer_destroy(void) {
    if (!s_renderer.initialized)
        return;
    mybot_lvgl_view_destroy();
    if (s_renderer.display)
        lv_display_delete(s_renderer.display);
    free(s_renderer.draw);
    free(s_renderer.canvas);
    lv_deinit();
    memset(&s_renderer, 0, sizeof(s_renderer));
}
