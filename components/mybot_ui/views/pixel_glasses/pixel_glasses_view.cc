/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Project Contributors */
/* Pixel-glasses geometry and process-skin colors follow the product simulator's
 * src/expressions.js and src/reference-style.js. The renderer owns Linux view
 * state and LVGL objects; the display adapter owns panel and DMA buffers. */
#include "lvgl_view.h"
#include "pixel_glasses.h"
#include "pixel_expression_shapes.h"
#include "mybot_ui/canvas.h"
#include "ui_config.h"

#include <cstdlib>

#include <cstdio>
#include <cstring>

LV_FONT_DECLARE(mybot_lvgl_font_40);
LV_FONT_DECLARE(mybot_pair_font_64);
LV_FONT_DECLARE(mybot_terminal_font_18);
LV_FONT_DECLARE(mybot_terminal_font_20);

namespace {

constexpr int kArtworkWidth = 1024;
constexpr int kArtworkHeight = 896;
constexpr unsigned kFrameParts = 13;
constexpr size_t kSsidCapacity = 33;
constexpr size_t kStateLineCapacity =
    MYBOT_DISPLAY_STATE_CAPACITY + sizeof("> state: ") + sizeof(" | vp:pending") - 2;
constexpr uint32_t kSupportedIndicators =
    MYBOT_LCD_INDICATOR_VP_REGISTERED | MYBOT_LCD_INDICATOR_SERVER_STATE_MASK;
constexpr uint32_t kTransitionMs = MYBOT_EXPRESSION_TRANSITION_MS;
constexpr uint32_t kTransitionTickMs = 50;
constexpr uint32_t kCursorMs = 500;
constexpr uint32_t kBackground = 0x050708;
constexpr uint32_t kNeutral = 0xf1f1ee;
constexpr uint32_t kGreeting = 0xf1ca79;
constexpr uint32_t kTerminal = 0x639589;
constexpr uint32_t kMuted = 0xa0a6a3;
constexpr uint32_t kError = 0xe57876;

struct Presentation {
    const char *title;
    const char *notice;
    uint32_t ink;
};

#if CONFIG_MYBOT_LANGUAGE_ZH_CN
constexpr Presentation kScreens[] = {
    {"启动中", "正在初始化", kNeutral},
    {"等待网络", "正在连接网络", kGreeting},
    {"网络已断开", "正在重新连接", kError},
    {"连接服务中", "正在连接服务", kNeutral},
    {"等待配对", "正在获取配对码", kGreeting},
    {"配对码", "在网页控制台输入配对码", kGreeting},
    {"", "", kNeutral},
    {"", "", kGreeting},
    {"启动失败", "请重启后重试", kError},
    {"正在退出", "正在结束服务", kMuted},
};
#else
constexpr Presentation kScreens[] = {
    {"Starting", "Please wait", kNeutral},
    {"Waiting for network", "Connect the device to a network", kGreeting},
    {"Wi-Fi disconnected", "Reconnecting to Wi-Fi", kError},
    {"Connecting", "Connecting to services", kNeutral},
    {"Pairing", "Getting pairing code", kGreeting},
    {"Pairing code", "Enter code in web console", kGreeting},
    {"", "", kNeutral},
    {"", "", kGreeting},
    {"Startup failed", "Restart to retry", kError},
    {"Stopping", "Closing services", kMuted},
};
#endif

static_assert(sizeof(kScreens) / sizeof(kScreens[0]) == MYBOT_LCD_SCREEN_COUNT,
              "Every SDK workflow screen needs a presentation");

struct View {
    lv_obj_t *root;
    lv_obj_t *artwork;
    lv_obj_t *cursor;
    lv_obj_t *signature;
    lv_obj_t *actor;
    lv_obj_t *terminal[4];
    lv_obj_t *face;
    lv_obj_t *frame[kFrameParts];
    pixel_shapes::Bank banks[2];
    lv_obj_t *guide;
    lv_obj_t *divider;
    lv_obj_t *title;
    lv_obj_t *code;
    lv_obj_t *detail;
    lv_obj_t *notice;
    lv_timer_t *timer;
    mybot_lcd_content_t previous;
    mybot_display_runtime_status_t runtime_status;
    char provisioning_ssid[kSsidCapacity];
    char pair_code[MYBOT_LCD_PAIR_CODE_CAPACITY];
    char mood[32];
    char state_line[kStateLineCapacity];
    int artwork_width;
    int artwork_height;
    bool has_content;
    bool face_visible;
    bool transitioning;
    uint32_t transition_started;
    uint32_t current_ink;
    uint32_t current_terminal;
    mybot_expression_t expression;
    unsigned active_bank;
    bool previous_face;
};

/* LVGL retains pointers into these banks and label buffers until widget deletion. */
View *s_view;

int scale(int value) {
    return (value * s_view->artwork_width + kArtworkWidth / 2) / kArtworkWidth;
}

int positive_scale(int value) {
    const int result = scale(value);
    return result > 0 ? result : 1;
}

void set_hidden(lv_obj_t *object, bool hidden) {
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == hidden) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_text(lv_obj_t *label, const char *text) {
    if (lv_label_get_text(label) != text) {
        lv_label_set_text_static(label, text);
    }
}

void prepare_object(lv_obj_t *object, int x, int y, int width, int height) {
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object,
                       static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
}

lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int width, int height, uint32_t ink,
                     lv_opa_t opacity = LV_OPA_COVER) {
    lv_obj_t *object = lv_obj_create(parent);
    if (object) {
        prepare_object(object, x, y, width, height);
        lv_obj_set_style_bg_color(object, lv_color_hex(ink), 0);
        lv_obj_set_style_bg_opa(object, opacity, 0);
    }
    return object;
}

lv_obj_t *make_artwork_panel(lv_obj_t *parent, int x, int y, int width, int height, uint32_t ink,
                             lv_opa_t opacity = LV_OPA_COVER) {
    /* Preserve the original two-pixel outline grid while text uses native pixels.
     * Independent native rounding can otherwise leave one-pixel gaps between
     * adjacent stepped frame rectangles. */
    const auto grid = [](int value) {
        return 2 * ((value * (s_view->artwork_width / 2) + kArtworkWidth / 2) / kArtworkWidth);
    };
    const int grid_width = grid(width), grid_height = grid(height);
    return make_panel(parent, grid(x), grid(y), grid_width > 0 ? grid_width : 2,
                      grid_height > 0 ? grid_height : 2, ink, opacity);
}

lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width, int height, const lv_font_t *font,
                     uint32_t ink) {
    lv_obj_t *object = lv_label_create(parent);
    if (object) {
        int label_height = positive_scale(height);
        if (label_height < font->line_height) {
            label_height = font->line_height;
        }
        prepare_object(object, scale(x), scale(y), positive_scale(width), label_height);
        lv_obj_set_style_text_font(object, font, 0);
        lv_obj_set_style_text_color(object, lv_color_hex(ink), 0);
        lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
        lv_label_set_text_static(object, "");
    }
    return object;
}

uint32_t blend_color(uint32_t from, uint32_t to, unsigned progress) {
    uint32_t result = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const unsigned channel = (((from >> shift) & 255u) * (255u - progress) +
                                  ((to >> shift) & 255u) * progress + 127u) /
                                 255u;
        result |= channel << shift;
    }
    return result;
}

void paint_frame(uint32_t ink, uint32_t terminal) {
    const lv_color_t color = lv_color_hex(ink);
    if (ink != s_view->current_ink) {
        for (auto *part : s_view->frame) {
            lv_obj_set_style_bg_color(part, color, 0);
        }
        lv_obj_set_style_bg_color(s_view->cursor, color, 0);
        lv_obj_set_style_text_color(s_view->signature, color, 0);
        s_view->current_ink = ink;
    }
    if (terminal != s_view->current_terminal) {
        for (auto *label : s_view->terminal) {
            lv_obj_set_style_text_color(label, lv_color_hex(terminal), 0);
        }
        s_view->current_terminal = terminal;
    }
}

void update_terminal(mybot_expression_t expression) {
    const char *name = mybot_expression_name(expression);
    constexpr char prefix[] = "> mood: ";
    std::memcpy(s_view->mood, prefix, sizeof(prefix) - 1);
    unsigned out = sizeof(prefix) - 1;
    while (*name && out < sizeof(s_view->mood) - 1) {
        const char letter = *name++;
        s_view->mood[out++] = letter >= 'A' && letter <= 'Z' ? letter + 'a' - 'A' : letter;
    }
    s_view->mood[out] = '\0';
    /* The owned buffer address is stable; explicitly refresh its changed bytes. */
    lv_label_set_text_static(s_view->terminal[1], s_view->mood);
}

void update_runtime_terminal(const mybot_lcd_content_t &content,
                             const mybot_display_runtime_status_t &status) {
    const bool conversation =
        status.in_conversation && content.screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
    const char *state = status.state;
    const char *voiceprint = "";
    if (conversation) {
        /* A malformed multi-phase mask has a deterministic display priority. */
        if (content.indicators & MYBOT_LCD_INDICATOR_SPEAKING) {
            state = "speaking";
        } else if (content.indicators & MYBOT_LCD_INDICATOR_THINKING) {
            state = "thinking";
        } else if (content.indicators & MYBOT_LCD_INDICATOR_LISTENING) {
            state = "listening";
        } else {
            state = "in_conversation";
        }
        voiceprint =
            content.indicators & MYBOT_LCD_INDICATOR_VP_REGISTERED ? " | vp:ok" : " | vp:pending";
    }
    char text[kStateLineCapacity]{};
    std::snprintf(text, sizeof(text), "> state: %s%s", state, voiceprint);
    if (std::strcmp(text, s_view->state_line) != 0) {
        std::memcpy(s_view->state_line, text, sizeof(s_view->state_line));
        /* Static labels retain the address; explicitly invalidate changed bytes. */
        lv_label_set_text_static(s_view->terminal[2], s_view->state_line);
    }
}

void render_expression(uint32_t now) {
    unsigned progress = 255;
#if CONFIG_MYBOT_LVGL_UI_ANIMATIONS
    if (s_view->transitioning) {
        const uint32_t elapsed = now - s_view->transition_started;
        progress = elapsed >= kTransitionMs ? 255u : elapsed * 255u / kTransitionMs;
    }
#endif
    auto &current = s_view->banks[s_view->active_bank];
    const auto &config = pixel_shapes::kConfigs[s_view->expression];
    const unsigned brightness =
        pixel_shapes::brightness(s_view->expression, now, s_view->transition_started);
    unsigned alpha = progress * brightness / 1000u;
    if (alpha > 255)
        alpha = 255;
    pixel_shapes::paint(current, now, static_cast<uint8_t>(alpha), brightness,
                        static_cast<uint8_t>(progress));
    const unsigned shade = brightness > 1000 ? 255 : brightness * 255u / 1000u;
    const uint32_t target_ink = blend_color(kBackground, config.ink, shade);
    const uint32_t target_terminal = blend_color(kBackground, kTerminal, shade);
    uint32_t old_ink = kBackground, old_terminal = kBackground;
    auto &previous = s_view->banks[1 - s_view->active_bank];
    if (progress < 255 && s_view->previous_face) {
        const unsigned previous_brightness =
            pixel_shapes::brightness(previous.expression, now, previous.changed_at);
        unsigned previous_alpha = (255u - progress) * previous_brightness / 1000u;
        if (previous_alpha > 255)
            previous_alpha = 255;
        pixel_shapes::paint(previous, now, static_cast<uint8_t>(previous_alpha),
                            previous_brightness, static_cast<uint8_t>(255u - progress));
        const unsigned previous_shade =
            previous_brightness > 1000 ? 255 : previous_brightness * 255u / 1000u;
        old_ink = blend_color(kBackground, pixel_shapes::kConfigs[previous.expression].ink,
                              previous_shade);
        old_terminal = blend_color(kBackground, kTerminal, previous_shade);
    }
    paint_frame(blend_color(old_ink, target_ink, progress),
                blend_color(old_terminal, target_terminal, progress));
    if (progress == 255) {
        s_view->transitioning = false;
        set_hidden(previous.root, true);
    }
}

void update_timer() {
#if CONFIG_MYBOT_LVGL_UI_ANIMATIONS
    if (!s_view->face_visible) {
        lv_timer_pause(s_view->timer);
        return;
    }
    const uint32_t now = lv_tick_get();
    const bool flash = (pixel_shapes::kConfigs[s_view->expression].motion & pixel_shapes::Flash) &&
                       (uint32_t)(now - s_view->transition_started) < 90;
    const uint32_t period =
        s_view->transitioning || pixel_shapes::animated(s_view->expression) || flash
            ? kTransitionTickMs
            : kCursorMs - now % kCursorMs;
    lv_timer_set_period(s_view->timer, period);
    if (lv_timer_get_paused(s_view->timer)) {
        lv_timer_reset(s_view->timer);
        lv_timer_resume(s_view->timer);
    }
#else
    lv_timer_pause(s_view->timer);
#endif
}

void timer_callback(lv_timer_t *) {
#if CONFIG_MYBOT_LVGL_UI_ANIMATIONS
    const uint32_t now = lv_tick_get();
    if (s_view->transitioning || pixel_shapes::animated(s_view->expression))
        render_expression(now);
    set_hidden(s_view->cursor, (now / kCursorMs) % 2 != 0);
#endif
    update_timer();
}

void show_face(mybot_expression_t expression) {
    const bool was_visible = s_view->face_visible;
    set_hidden(s_view->guide, true);
    set_hidden(s_view->face, false);
    set_hidden(s_view->cursor, false);
    s_view->face_visible = true;
    s_view->previous_face = was_visible;
    s_view->active_bank = 1 - s_view->active_bank;
    auto &bank = s_view->banks[s_view->active_bank];
    pixel_shapes::configure(bank, expression);
    bank.changed_at = lv_tick_get();
    s_view->expression = expression;
    s_view->transition_started = bank.changed_at;
    update_terminal(expression);
    lv_obj_set_y(s_view->actor, scale(pixel_shapes::kConfigs[expression].offset_y));
    set_hidden(bank.root, false);
    /* Keep current shapes above previous shapes without a transform/layer. */
    lv_obj_move_to_index(bank.root, -1);
    set_hidden(s_view->banks[1 - s_view->active_bank].root, !was_visible);
#if CONFIG_MYBOT_LVGL_UI_ANIMATIONS
    s_view->transitioning = true;
#else
    s_view->transitioning = false;
#endif
    render_expression(lv_tick_get());
    update_timer();
}

void show_guide(const Presentation &presentation) {
    s_view->face_visible = false;
    s_view->transitioning = false;
    lv_obj_set_y(s_view->actor, 0);
    update_terminal(MYBOT_EXPRESSION_NEUTRAL);
    set_hidden(s_view->face, true);
    set_hidden(s_view->cursor, true);
    set_hidden(s_view->guide, false);
    set_hidden(s_view->code, true);
    set_hidden(s_view->detail, true);
    set_text(s_view->title, presentation.title);
    set_text(s_view->notice, presentation.notice);
    lv_obj_set_style_text_color(s_view->title, lv_color_hex(presentation.ink), 0);
    lv_obj_set_style_text_color(s_view->signature, lv_color_hex(presentation.ink), 0);
    lv_obj_set_style_bg_color(s_view->divider, lv_color_hex(presentation.ink), 0);
    /* Guide chrome can change independently while the hidden face retains its
     * old color. Force a complete primitive repaint when returning to a face. */
    s_view->current_ink = 0xffffffffu;
    for (auto *label : s_view->terminal) {
        lv_obj_set_style_text_color(label, lv_color_hex(kTerminal), 0);
    }
    s_view->current_terminal = kTerminal;
    update_timer();
}

bool create_face() {
    s_view->face = make_panel(s_view->actor, 0, 0, s_view->artwork_width, s_view->artwork_height,
                              kBackground, LV_OPA_TRANSP);
    if (!s_view->face) {
        return false;
    }
    unsigned part = 0;
    for (unsigned eye = 0; eye < 2; ++eye) {
        const int x = eye == 0 ? 214 : 534;
        s_view->frame[part++] = make_artwork_panel(s_view->face, x + 16, 354, 245, 11, kNeutral);
        s_view->frame[part++] = make_artwork_panel(s_view->face, x + 5, 365, 11, 204, kNeutral);
        s_view->frame[part++] = make_artwork_panel(s_view->face, x + 261, 365, 11, 204, kNeutral);
        s_view->frame[part++] = make_artwork_panel(s_view->face, x + 16, 569, 245, 11, kNeutral);
        s_view->frame[part++] = make_artwork_panel(s_view->face, x + 28, 581, 220, 5, kNeutral);
    }
    s_view->frame[part++] = make_artwork_panel(s_view->face, 486, 392, 48, 12, kNeutral);
    s_view->frame[part++] = make_artwork_panel(s_view->face, 204, 377, 12, 13, kNeutral);
    s_view->frame[part++] = make_artwork_panel(s_view->face, 806, 377, 12, 13, kNeutral);
    for (auto *object : s_view->frame) {
        if (!object) {
            return false;
        }
    }
    for (auto &bank : s_view->banks) {
        if (!pixel_shapes::create(bank, s_view->face, s_view->artwork_width,
                                  s_view->artwork_height))
            return false;
    }
    return true;
}

bool create_chrome() {
    for (unsigned i = 0; i < 3; ++i) {
        const int radius = i == 1 ? 19 : 13;
        lv_obj_t *dot =
            make_artwork_panel(s_view->artwork, 390 + static_cast<int>(i) * 122 - radius,
                               90 - radius, radius * 2, radius * 2, i == 1 ? 0x151923 : 0x595d5f);
        if (!dot) {
            return false;
        }
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    }
    s_view->actor = make_panel(s_view->artwork, 0, 0, s_view->artwork_width, s_view->artwork_height,
                               kBackground, LV_OPA_TRANSP);
    if (!s_view->actor)
        return false;
    /* Rasterize text at output resolution. Keep the original physical row
     * spacing and leave room for the larger font's actual advance widths. */
    const lv_font_t *terminal_font = &mybot_terminal_font_18;
    const lv_font_t *signature_font = &mybot_terminal_font_20;
    const int terminal_top = scale(108);
    const int scaled_step = scale(46);
    const int row_step =
        scaled_step > terminal_font->line_height ? scaled_step : terminal_font->line_height + 1;
    for (unsigned line = 0; line < 4; ++line) {
        s_view->terminal[line] =
            make_label(s_view->actor, 104, 108, 744, 44, terminal_font, kTerminal);
        if (!s_view->terminal[line])
            return false;
        lv_obj_set_y(s_view->terminal[line], terminal_top + static_cast<int>(line) * row_step);
        lv_obj_set_height(s_view->terminal[line], terminal_font->line_height);
        lv_obj_set_style_text_align(s_view->terminal[line], LV_TEXT_ALIGN_LEFT, 0);
        lv_label_set_long_mode(s_view->terminal[line], LV_LABEL_LONG_CLIP);
    }
    s_view->signature = make_label(s_view->actor, 795, 112, 145, 40, signature_font, kNeutral);
    /* Place the terminal cursor from the actual fixed-width font advance. */
    lv_point_t extent{};
    lv_text_get_size(&extent, "> agora / realtime", terminal_font, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    const int cursor_height = positive_scale(24);
    const int cursor_top =
        terminal_top + 3 * row_step + (terminal_font->line_height - cursor_height) / 2;
    s_view->cursor = make_panel(s_view->actor, scale(104) + extent.x + scale(8), cursor_top,
                                positive_scale(13), cursor_height, kNeutral);
    if (!s_view->signature || !s_view->cursor) {
        return false;
    }
    lv_obj_set_style_text_align(s_view->signature, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text_static(s_view->terminal[0], "> MyBot / RV1126B");
    lv_label_set_text_static(s_view->terminal[3], "> agora / realtime");
    update_terminal(MYBOT_EXPRESSION_NEUTRAL);
    lv_label_set_text_static(s_view->signature, "</>");
    return true;
}

bool create_guide() {
    s_view->guide = make_panel(s_view->artwork, 0, 0, s_view->artwork_width, s_view->artwork_height,
                               kBackground, LV_OPA_TRANSP);
    if (!s_view->guide) {
        return false;
    }
    s_view->divider = make_artwork_panel(s_view->guide, 104, 292, 816, 2, kNeutral);
    s_view->title = make_label(s_view->guide, 72, 350, 880, 116, &mybot_lvgl_font_40, kNeutral);
    s_view->code = make_label(s_view->guide, 72, 478, 880, 124, &mybot_pair_font_64, kGreeting);
    s_view->detail = make_label(s_view->guide, 72, 478, 880, 130, &mybot_lvgl_font_40, kNeutral);
    s_view->notice = make_label(s_view->guide, 72, 660, 880, 130, &mybot_lvgl_font_40, kMuted);
    /* Operational copy and a 32-byte SSID can wrap to two 26-pixel lines on
     * the smaller panel; source-scaled height alone would clip the second. */
    const int two_lines = mybot_lvgl_font_40.line_height * 2;
    if (s_view->detail && lv_obj_get_height(s_view->detail) < two_lines)
        lv_obj_set_height(s_view->detail, two_lines);
    if (s_view->notice && lv_obj_get_height(s_view->notice) < two_lines)
        lv_obj_set_height(s_view->notice, two_lines);
    return s_view->divider && s_view->title && s_view->code && s_view->detail && s_view->notice;
}

} // namespace

int mybot_lvgl_view_create(lv_display_t *display) {
    if (!display || s_view) {
        return -1;
    }
    const int width = lv_display_get_horizontal_resolution(display);
    const int height = lv_display_get_vertical_resolution(display);
    if (width != RV1126B_UI_WIDTH || height != RV1126B_UI_HEIGHT) {
        return -1;
    }
    s_view = static_cast<View *>(std::calloc(1, sizeof(View)));
    if (!s_view) {
        return -1;
    }
    /* The 640x480 view uses a 576x504 artwork at (32,-12), matching the
     * CoreS3 composition. Only empty artwork margins are cropped. Geometry
     * remains in integer pixels; the DRM adapter owns physical rotation. */
    s_view->artwork_width = width * 9 / 10;
    s_view->artwork_height = s_view->artwork_width * kArtworkHeight / kArtworkWidth;
    s_view->current_ink = kNeutral;
    s_view->root =
        make_panel(lv_display_get_screen_active(display), 0, 0, width, height, kBackground);
    if (!s_view->root) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    s_view->artwork = make_panel(s_view->root, (width - s_view->artwork_width) / 2,
                                 (height - s_view->artwork_height) / 2, s_view->artwork_width,
                                 s_view->artwork_height, kBackground, LV_OPA_TRANSP);
    if (!s_view->artwork || !create_chrome() || !create_face() || !create_guide()) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    s_view->timer = lv_timer_create(timer_callback, kTransitionTickMs, nullptr);
    if (!s_view->timer) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    lv_timer_pause(s_view->timer);
    const mybot_lcd_content_t initial = {MYBOT_LCD_SCREEN_STARTING, {}, 0};
    mybot_pixel_glasses_update(&initial, nullptr, nullptr);
    return 0;
}

int mybot_lvgl_view_create_sized(lv_display_t *display, int width, int height) {
    if (!display || width != lv_display_get_horizontal_resolution(display) ||
        height != lv_display_get_vertical_resolution(display))
        return -1;
    return mybot_lvgl_view_create(display);
}

void mybot_lvgl_view_update(const mybot_lcd_content_t *content) {
    mybot_pixel_glasses_update(content, nullptr, nullptr);
}

void mybot_pixel_glasses_update(const mybot_lcd_content_t *content, const char *ssid,
                                const mybot_display_runtime_status_t *status) {
    if (!s_view || !s_view->root || !content || content->screen < MYBOT_LCD_SCREEN_STARTING ||
        content->screen >= MYBOT_LCD_SCREEN_COUNT) {
        return;
    }
    mybot_lcd_content_t normalized{};
    normalized.screen = content->screen;
    normalized.indicators = content->indicators & kSupportedIndicators;
    mybot_display_runtime_status_t copied_status{};
    if (status) {
        for (size_t i = 0; i < sizeof(copied_status.state) - 1 && status->state[i]; ++i) {
            copied_status.state[i] = status->state[i];
        }
        copied_status.in_conversation = status->in_conversation;
    }
    if (!copied_status.state[0]) {
        std::memcpy(copied_status.state, "stopped", sizeof("stopped"));
    }
    const bool pairing_code = content->screen == MYBOT_LCD_SCREEN_PAIR_CODE;
    const bool provisioning = content->screen == MYBOT_LCD_SCREEN_WIFI_PROVISIONING;
    if (pairing_code) {
        std::memcpy(normalized.pair_code, content->pair_code, sizeof(normalized.pair_code) - 1);
    }
    char copied_ssid[kSsidCapacity]{};
    if (provisioning && ssid) {
        for (size_t i = 0; i < sizeof(copied_ssid) - 1 && ssid[i]; ++i) {
            copied_ssid[i] = ssid[i];
        }
    }
    const bool workflow_changed =
        !s_view->has_content || normalized.screen != s_view->previous.screen ||
        std::strcmp(normalized.pair_code, s_view->previous.pair_code) != 0 ||
        std::strcmp(copied_ssid, s_view->provisioning_ssid) != 0;
    const bool status_changed =
        !s_view->has_content || normalized.indicators != s_view->previous.indicators ||
        copied_status.in_conversation != s_view->runtime_status.in_conversation ||
        std::strcmp(copied_status.state, s_view->runtime_status.state) != 0;
    if (!workflow_changed && !status_changed) {
        return;
    }
    update_runtime_terminal(normalized, copied_status);
    /* The display owner chooses face expressions after updating this snapshot.
     * Workflow/status changes never insert an intermediate default face or
     * restart either expression clock. Guidance still replaces a visible face. */
    if (workflow_changed && content->screen != MYBOT_LCD_SCREEN_READY &&
        content->screen != MYBOT_LCD_SCREEN_IN_CONVERSATION) {
        show_guide(kScreens[content->screen]);
        if (provisioning) {
            std::memcpy(s_view->provisioning_ssid, copied_ssid, sizeof(s_view->provisioning_ssid));
            lv_label_set_text_static(s_view->detail, s_view->provisioning_ssid);
            set_hidden(s_view->detail, !copied_ssid[0]);
        }
        if (pairing_code) {
            /* Keep six-digit codes, including leading zeroes, at native 64 px. */
            std::memcpy(s_view->pair_code, normalized.pair_code, sizeof(s_view->pair_code));
            lv_label_set_text_static(s_view->code, s_view->pair_code);
            set_hidden(s_view->code, false);
        }
    }
    s_view->previous = normalized;
    s_view->runtime_status = copied_status;
    std::memcpy(s_view->provisioning_ssid, copied_ssid, sizeof(s_view->provisioning_ssid));
    s_view->has_content = true;
}

int mybot_pixel_glasses_show_expression(mybot_expression_t expression) {
    if (!s_view || !s_view->root || !s_view->has_content ||
        (s_view->previous.screen != MYBOT_LCD_SCREEN_READY &&
         s_view->previous.screen != MYBOT_LCD_SCREEN_IN_CONVERSATION) ||
        expression < MYBOT_EXPRESSION_NEUTRAL || expression >= MYBOT_EXPRESSION_COUNT)
        return -1;
    if (!s_view->face_visible || expression != s_view->expression)
        show_face(expression);
    return 0;
}

int mybot_pixel_glasses_replay_expression(mybot_expression_t expression) {
    if (!s_view || !s_view->root || !s_view->has_content ||
        (s_view->previous.screen != MYBOT_LCD_SCREEN_READY &&
         s_view->previous.screen != MYBOT_LCD_SCREEN_IN_CONVERSATION) ||
        expression < MYBOT_EXPRESSION_NEUTRAL || expression >= MYBOT_EXPRESSION_COUNT)
        return -1;
    show_face(expression);
    return 0;
}

void mybot_lvgl_view_destroy(void) {
    /* The display owner holds the LVGL lock, excluding timers and drawing while
     * retained points, event userdata and static-label buffers are released. */
    if (!s_view) {
        return;
    }
    if (s_view->timer) {
        lv_timer_delete(s_view->timer);
    }
    if (s_view->root) {
        lv_obj_delete(s_view->root);
    }
    std::free(s_view);
    s_view = nullptr;
}
