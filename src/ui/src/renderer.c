/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_ui/renderer.h"
#include "expression_policy.h"
#include "lvgl_view.h"
#include "pixel_glasses.h"
#include "ui_config.h"

#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(mybot_lvgl_font_40);
LV_FONT_DECLARE(mybot_terminal_font_18);

#if CONFIG_MYBOT_LANGUAGE_ZH_CN
#define RESET_TITLE "恢复出厂设置"
#define RESET_QUESTION "恢复出厂设置？"
#define RESET_CONFIRM "确认恢复"
#define RESET_WARNING "清除已存 Wi-Fi 和本机绑定\n音量恢复为 70\n之后需重新配网和配对"
#define RESET_PROGRESS "正在恢复设置"
#define RESET_FAILURE "未能开始恢复"
#define RESET_RETRY "请返回设置后重试"
#define SETTINGS_TITLE "设置"
#define SETTINGS_WIFI "设备配网"
#define SETTINGS_POWEROFF "关机"
#define SETTINGS_BACK "返回"
#define SETTINGS_CANCEL "取消"
#define SETTINGS_CONFIRM "确认关机"
#define SETTINGS_QUESTION "确认关机？"
#define SETTINGS_WARNING "设备将停止运行\n确认关闭设备？"
#define SETTINGS_PROGRESS "正在关机"
#define SETTINGS_WAIT "请稍候"
#define SETTINGS_FAILURE "关机失败"
#define SETTINGS_RETRY "设备仍在运行\n请重试或检查系统"
#else
#define RESET_TITLE "Factory reset"
#define RESET_QUESTION "Factory reset?"
#define RESET_CONFIRM "Reset device"
#define RESET_WARNING                                                                              \
    "Forget saved Wi-Fi and\nclear local pairing.\nSet volume to 70.\nThen set up and pair again."
#define RESET_PROGRESS "Resetting device"
#define RESET_FAILURE "Reset could not start"
#define RESET_RETRY "Go back and try again."
#define SETTINGS_TITLE "Settings"
#define SETTINGS_WIFI "Wi-Fi setup"
#define SETTINGS_POWEROFF "Power off"
#define SETTINGS_BACK "Back"
#define SETTINGS_CANCEL "Cancel"
#define SETTINGS_CONFIRM "Power off"
#define SETTINGS_QUESTION "Power off?"
#define SETTINGS_WARNING "Current calls will end.\nPower off this device?"
#define SETTINGS_PROGRESS "Powering off"
#define SETTINGS_WAIT "Please wait"
#define SETTINGS_FAILURE "Power off failed"
#define SETTINGS_RETRY "The device is still on.\nPlease try again."
#endif

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
    lv_obj_t *settings_gear;
    lv_obj_t *settings_panel;
    lv_obj_t *settings_title;
    lv_obj_t *settings_detail;
    lv_obj_t *settings_controls[MYBOT_SETTINGS_CONTROL_COUNT];
    mybot_settings_page_t settings_page;
} s_renderer;
static uint32_t s_tick;

static lv_obj_t *settings_panel(lv_obj_t *parent, int x, int y, int width, int height,
                                uint32_t color) {
    if (!parent)
        return NULL;
    lv_obj_t *panel = lv_obj_create(parent);
    if (!panel)
        return NULL;
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *settings_label(lv_obj_t *parent, int x, int y, int width, int height,
                                const char *text, const lv_font_t *font, uint32_t color) {
    if (!parent)
        return NULL;
    lv_obj_t *label = lv_label_create(parent);
    if (!label)
        return NULL;
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(label, 4, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text_static(label, text);
    return label;
}

static bool settings_button(mybot_settings_control_t control, const char *text, uint32_t color) {
    mybot_settings_rect_t bounds = mybot_settings_bounds(control);
    lv_obj_t *button = settings_panel(s_renderer.settings_panel, bounds.x, bounds.y, bounds.width,
                                      bounds.height, 0x10191b);
    if (!button)
        return false;
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_radius(button, 8, 0);
    if (!settings_label(button, 0, (bounds.height - 4 - mybot_lvgl_font_40.line_height) / 2,
                        bounds.width - 4, mybot_lvgl_font_40.line_height, text, &mybot_lvgl_font_40,
                        color))
        return false;
    s_renderer.settings_controls[control] = button;
    return true;
}

static bool settings_create(void) {
    lv_obj_t *screen = lv_display_get_screen_active(s_renderer.display);
    const mybot_settings_rect_t bounds = mybot_settings_bounds(MYBOT_SETTINGS_GEAR);
    s_renderer.settings_gear =
        settings_panel(screen, bounds.x, bounds.y, bounds.width, bounds.height, 0x0d1619);
    if (!s_renderer.settings_gear)
        return false;
    lv_obj_set_style_border_width(s_renderer.settings_gear, 1, 0);
    lv_obj_set_style_border_color(s_renderer.settings_gear, lv_color_hex(0x37565b), 0);
    lv_obj_set_style_radius(s_renderer.settings_gear, 10, 0);
    static const lv_point_precise_t outline[] = {
        {23, 8},  {31, 8},  {32, 14}, {36, 16}, {41, 13}, {46, 19}, {42, 24}, {42, 30}, {46, 35},
        {41, 41}, {36, 38}, {32, 40}, {31, 46}, {23, 46}, {22, 40}, {18, 38}, {13, 41}, {8, 35},
        {12, 30}, {12, 24}, {8, 19},  {13, 13}, {18, 16}, {22, 14}, {23, 8}};
    lv_obj_t *line = lv_line_create(s_renderer.settings_gear);
    if (!line)
        return false;
    lv_line_set_points(line, outline, sizeof(outline) / sizeof(outline[0]));
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0x91e7d0), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_t *hub = settings_panel(s_renderer.settings_gear, 19, 19, 16, 16, 0x0d1619);
    if (!hub)
        return false;
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hub, 3, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(0x91e7d0), 0);
    s_renderer.settings_panel =
        settings_panel(screen, 0, 0, RV1126B_UI_WIDTH, RV1126B_UI_HEIGHT, 0x050708);
    if (!s_renderer.settings_panel ||
        !settings_label(s_renderer.settings_panel, 64, 28, 512, 26, "MYBOT / SETTINGS",
                        &mybot_terminal_font_18, 0x72b6aa))
        return false;
    s_renderer.settings_title = settings_label(s_renderer.settings_panel, 48, 76, 544, 56,
                                               SETTINGS_TITLE, &mybot_lvgl_font_40, 0xe8f4ee);
    s_renderer.settings_detail = settings_label(s_renderer.settings_panel, 48, 160, 544, 228, "",
                                                &mybot_lvgl_font_40, 0xabc1ba);
    if (!s_renderer.settings_title || !s_renderer.settings_detail ||
        !settings_button(MYBOT_SETTINGS_WIFI, SETTINGS_WIFI, 0x91e7d0) ||
        !settings_button(MYBOT_SETTINGS_POWEROFF, SETTINGS_POWEROFF, 0xee9b8c) ||
        !settings_button(MYBOT_SETTINGS_BACK, SETTINGS_BACK, 0xabc1ba) ||
        !settings_button(MYBOT_SETTINGS_CANCEL, SETTINGS_CANCEL, 0xabc1ba) ||
        !settings_button(MYBOT_SETTINGS_CONFIRM, SETTINGS_CONFIRM, 0xee9b8c) ||
        !settings_button(MYBOT_SETTINGS_FACTORY_RESET, RESET_TITLE, 0xee9b8c) ||
        !settings_button(MYBOT_SETTINGS_RESET_CONFIRM, RESET_CONFIRM, 0xee9b8c))
        return false;
    lv_obj_add_flag(s_renderer.settings_panel, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void rv1126b_renderer_settings(mybot_settings_page_t page) {
    if (!s_renderer.initialized || !s_renderer.settings_panel)
        return;
    if (page < MYBOT_SETTINGS_CLOSED || page > MYBOT_SETTINGS_RESET_ERROR)
        page = MYBOT_SETTINGS_CLOSED;
    if (page == s_renderer.settings_page)
        return;
    s_renderer.settings_page = page;
    if (page == MYBOT_SETTINGS_CLOSED) {
        lv_obj_add_flag(s_renderer.settings_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_gear, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_renderer.settings_gear, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_renderer.settings_panel, LV_OBJ_FLAG_HIDDEN);
    for (int control = MYBOT_SETTINGS_WIFI; control < MYBOT_SETTINGS_CONTROL_COUNT; ++control)
        lv_obj_add_flag(s_renderer.settings_controls[control], LV_OBJ_FLAG_HIDDEN);
    const char *title = SETTINGS_TITLE;
    const char *detail = "";
    if (page == MYBOT_SETTINGS_MENU) {
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_WIFI], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_POWEROFF],
                           LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_FACTORY_RESET],
                           LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_BACK], LV_OBJ_FLAG_HIDDEN);
    } else if (page == MYBOT_SETTINGS_CONFIRM_POWEROFF) {
        title = SETTINGS_QUESTION;
        detail = SETTINGS_WARNING;
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_CANCEL], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_CONFIRM],
                           LV_OBJ_FLAG_HIDDEN);
    } else if (page == MYBOT_SETTINGS_POWERING_OFF) {
        title = SETTINGS_PROGRESS;
        detail = SETTINGS_WAIT;
    } else if (page == MYBOT_SETTINGS_CONFIRM_RESET) {
        title = RESET_QUESTION;
        detail = RESET_WARNING;
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_BACK], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_RESET_CONFIRM],
                           LV_OBJ_FLAG_HIDDEN);
    } else if (page == MYBOT_SETTINGS_RESETTING) {
        title = RESET_PROGRESS;
        detail = SETTINGS_WAIT;
    } else {
        title = page == MYBOT_SETTINGS_RESET_ERROR ? RESET_FAILURE : SETTINGS_FAILURE;
        detail = page == MYBOT_SETTINGS_RESET_ERROR ? RESET_RETRY : SETTINGS_RETRY;
        lv_obj_remove_flag(s_renderer.settings_controls[MYBOT_SETTINGS_BACK], LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_static(s_renderer.settings_title, title);
    lv_label_set_text_static(s_renderer.settings_detail, detail);
}

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
    if (!settings_create())
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
    mybot_pixel_glasses_update(content, status ? status->provisioning_ssid : NULL, status);
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
