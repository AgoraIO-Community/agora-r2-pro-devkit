/* SPDX-License-Identifier: Apache-2.0 */
/*
 * THESIS: Make every BotStation workflow state legible without a phone or console.
 * OWN-WORLD: Ink-dark field, cool surfaces, expressive pixel eyes and state signals.
 * STORY: The device shows whether it is connecting, ready, listening or replying.
 * FIRST VIEWPORT: Brand and state above a face, pairing code or service symbol.
 * FORM: A persistent status scene with expressive pixel eyes and restrained activity motion.
 */
#define _POSIX_C_SOURCE 200809L
#include "rv1126b_lvgl_ui.h"

#include "linux_platform_adapters.h"

#include <api/aosl_log.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef BOTSTATION_ENABLE_LVGL_UI
#include <lvgl/lvgl.h>
#include <lv_drivers/display/drm.h>
#include <math.h>
#if defined(LV_USE_FREETYPE) && LV_USE_FREETYPE
#include <lvgl/src/extra/libs/freetype/lv_freetype.h>
#define BOTSTATION_LVGL_FREETYPE 1
#endif

typedef struct {
    const char *title_zh;
    const char *title_en;
    const char *detail_zh;
    const char *detail_en;
    const char *icon;
    uint32_t accent;
    bool show_eyes;
} screen_presentation_t;

typedef struct {
    lv_obj_t *line;
    lv_obj_t *dot;
    lv_point_t points[9];
    lv_coord_t center_x;
    lv_coord_t center_y;
    lv_coord_t width;
    lv_coord_t height;
    lv_coord_t dot_size;
    bool line_arc;
    bool line_configured;
} eye_t;

typedef enum {
    EYE_MODE_NEUTRAL,
    EYE_MODE_LISTENING,
    EYE_MODE_THINKING,
} eye_mode_t;

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_started_cond = PTHREAD_COND_INITIALIZER;
static pthread_t s_thread;
static bool s_started;
static bool s_stop;
static bool s_startup_done;
static int s_startup_result;
static mybot_lcd_content_t s_content = {.screen = MYBOT_LCD_SCREEN_STARTING};
static uint32_t s_content_revision;
static rv1126b_ui_wait_t s_wait_stage;
static uint32_t s_applied_revision = UINT32_MAX;
static eye_t s_eyes[2];
static lv_obj_t *s_brand;
static lv_obj_t *s_status;
static lv_obj_t *s_title;
static lv_obj_t *s_detail;
static lv_obj_t *s_icon;
static lv_obj_t *s_pair_code;
static lv_obj_t *s_activity[5];
static lv_font_t *s_chinese_font;
static lv_font_t *s_chinese_small_font;
static bool s_chinese_font_ready;
static bool s_eyes_visible;
static eye_mode_t s_eye_mode;
static uint32_t s_eye_mode_since;
static uint32_t s_accent;
static uint32_t s_activity_phase;

static const screen_presentation_t s_presentations[MYBOT_LCD_SCREEN_COUNT] = {
    [MYBOT_LCD_SCREEN_STARTING] = {
        "正在启动", "Starting", "正在初始化设备", "Initializing device",
        LV_SYMBOL_POWER, 0x69B5FF, false,
    },
    [MYBOT_LCD_SCREEN_WIFI_PROVISIONING] = {
        "等待网络", "Waiting for network", "请使用产品配网方式连接网络",
        "Use product Wi-Fi setup to connect", LV_SYMBOL_WIFI, 0xFFCC7C, false,
    },
    [MYBOT_LCD_SCREEN_WIFI_DISCONNECTED] = {
        "网络已断开", "Network disconnected", "正在等待网络恢复",
        "Waiting for the network to recover", LV_SYMBOL_WIFI, 0xFF8E91, false,
    },
    [MYBOT_LCD_SCREEN_STARTING_SERVICES] = {
        "连接服务中", "Connecting", "正在连接 BotStation 服务",
        "Connecting to BotStation services", LV_SYMBOL_REFRESH, 0x69B5FF, false,
    },
    [MYBOT_LCD_SCREEN_PAIRING] = {
        "设备配对中", "Pairing device", "请在控制台完成设备绑定",
        "Finish device pairing in the console", LV_SYMBOL_SETTINGS, 0xFFCC7C, false,
    },
    [MYBOT_LCD_SCREEN_PAIR_CODE] = {
        "输入配对码", "Pairing code", "请在网页控制台输入此配对码",
        "Enter this code in the web console", LV_SYMBOL_SETTINGS, 0xFFCC7C, false,
    },
    [MYBOT_LCD_SCREEN_READY] = {
        "准备就绪", "Ready", "设备已准备好进行语音对话",
        "Ready for a voice conversation", LV_SYMBOL_OK, 0x69DBA8, true,
    },
    [MYBOT_LCD_SCREEN_IN_CONVERSATION] = {
        "对话中", "In conversation", "正在进行语音对话",
        "Voice conversation is active", LV_SYMBOL_AUDIO, 0x69B5FF, true,
    },
    [MYBOT_LCD_SCREEN_FAILED] = {
        "启动失败", "Startup failed", "检查网络和设备配置后重启",
        "Check setup, then restart", LV_SYMBOL_WARNING, 0xFF8E91, false,
    },
    [MYBOT_LCD_SCREEN_STOPPING] = {
        "正在关闭", "Stopping", "正在安全结束设备服务",
        "Safely shutting down device services", LV_SYMBOL_POWER, 0xA5B2C7, false,
    },
};

static const lv_font_t *ui_label_font(void) {
#if LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#elif LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *ui_display_font(void) {
#if LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#elif LV_FONT_MONTSERRAT_20
    return &lv_font_montserrat_20;
#elif LV_FONT_MONTSERRAT_14
    return &lv_font_montserrat_14;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const char *screen_status(mybot_lcd_screen_t screen, uint32_t indicators) {
    if (screen == MYBOT_LCD_SCREEN_IN_CONVERSATION) {
        if (indicators & MYBOT_LCD_INDICATOR_LISTENING) {
            return s_chinese_font_ready ? "聆听" : "LISTENING";
        }
        if (indicators & MYBOT_LCD_INDICATOR_THINKING) {
            return s_chinese_font_ready ? "思考" : "THINKING";
        }
        if (indicators & MYBOT_LCD_INDICATOR_SPEAKING) {
            return s_chinese_font_ready ? "回复" : "SPEAKING";
        }
    }
    if (s_chinese_font_ready) {
        static const char *const labels[] = {
            "启动", "网络", "离线", "连接", "配对", "配对码",
            "就绪", "对话", "异常", "退出",
        };
        return labels[screen];
    }
    static const char *const labels[] = {
        "START", "NETWORK", "OFFLINE", "CONNECTING", "PAIRING",
        "PAIR CODE", "READY", "TALKING", "ERROR", "STOPPING",
    };
    return labels[screen];
}

static void set_round_shape(lv_obj_t *object, lv_color_t color) {
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(object, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_circle(lv_obj_t *parent, lv_coord_t size, lv_color_t color) {
    lv_obj_t *circle = lv_obj_create(parent);
    if (!circle) {
        return NULL;
    }
    lv_obj_set_size(circle, size, size);
    set_round_shape(circle, color);
    return circle;
}

static lv_obj_t *create_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height, uint32_t color,
                              lv_text_align_t alignment) {
    if (!parent) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        return NULL;
    }
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, alignment, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, ui_label_font(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static int create_eye(eye_t *eye, lv_coord_t center_x, lv_coord_t center_y,
                      lv_coord_t width, lv_coord_t height) {
    eye->center_x = center_x;
    eye->center_y = center_y;
    eye->width = width;
    eye->height = height;
    eye->dot_size = height * 36 / 100;
    if (eye->dot_size < 14) {
        eye->dot_size = 14;
    }
    eye->line = lv_line_create(lv_scr_act());
    eye->dot = create_circle(lv_scr_act(), eye->dot_size, lv_color_hex(0xF1F1EE));
    if (!eye->line || !eye->dot) {
        return -1;
    }
    lv_obj_set_style_line_color(eye->line, lv_color_hex(0xF1F1EE), LV_PART_MAIN);
    lv_coord_t stroke_width = height * 18 / 100;
    lv_obj_set_style_line_width(eye->line, stroke_width, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(eye->line, true, LV_PART_MAIN);
    lv_obj_set_style_line_opa(eye->line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(eye->line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(eye->dot, LV_OBJ_FLAG_HIDDEN);
    return 0;
}

static bool create_chinese_fonts(void) {
#ifdef BOTSTATION_LVGL_FREETYPE
    static const char *font_path =
        "/usr/share/fonts/source-han-sans-cn/SourceHanSansCN-Regular.otf";
    if (!lv_freetype_init(2, 2, 16384)) {
        AOSL_LOG_WRN("LVGL UI: FreeType initialization failed; using English labels");
        return false;
    }
    lv_ft_info_t title_info = {
        .name = font_path,
        .weight = 28,
        .style = FT_FONT_STYLE_NORMAL,
    };
    lv_ft_info_t detail_info = {
        .name = font_path,
        .weight = 18,
        .style = FT_FONT_STYLE_NORMAL,
    };
    if (!lv_ft_font_init(&title_info) || !lv_ft_font_init(&detail_info)) {
        if (title_info.font) {
            lv_ft_font_destroy(title_info.font);
        }
        if (detail_info.font) {
            lv_ft_font_destroy(detail_info.font);
        }
        lv_freetype_destroy();
        AOSL_LOG_WRN("LVGL UI: Chinese font unavailable at %s; using English labels",
                     font_path);
        return false;
    }
    s_chinese_font = title_info.font;
    s_chinese_small_font = detail_info.font;
    s_chinese_font_ready = true;
    return true;
#else
    return false;
#endif
}

static void set_eye_visible(eye_t *eye, bool visible);

static int create_scene(void) {
    lv_disp_t *disp = lv_disp_get_default();
    lv_coord_t screen_width = lv_disp_get_hor_res(disp);
    lv_coord_t screen_height = lv_disp_get_ver_res(disp);
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x080D18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t margin = screen_width * 6 / 100;
    lv_coord_t gap = screen_width * 5 / 100;
    lv_coord_t eye_width = screen_width * 9 / 100;
    lv_coord_t eye_height = screen_height * 10 / 100;
    lv_coord_t eye_spacing = screen_width * 31 / 100;
    lv_coord_t eye_center_y = screen_height * 44 / 100;

    lv_obj_t *header = lv_obj_create(screen);
    if (!header) {
        AOSL_LOG_ERR("LVGL UI: cannot create workflow header");
        return -1;
    }
    lv_obj_set_pos(header, margin, screen_height * 4 / 100);
    lv_obj_set_size(header, screen_width - 2 * margin, screen_height * 7 / 100);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x111D2B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 14, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_brand = create_label(header, 14, 5, screen_width * 38 / 100,
                           screen_height * 6 / 100, 0xEDF2FA, LV_TEXT_ALIGN_LEFT);
    s_status = create_label(header, screen_width * 48 / 100, 5,
                            screen_width * 41 / 100, screen_height * 6 / 100,
                            0x69DBA8, LV_TEXT_ALIGN_RIGHT);
    s_title = create_label(screen, margin, screen_height * 15 / 100,
                           screen_width - 2 * margin, screen_height * 7 / 100,
                           0xEDF2FA, LV_TEXT_ALIGN_CENTER);
    s_detail = create_label(screen, margin, screen_height * 22 / 100,
                            screen_width - 2 * margin, screen_height * 8 / 100,
                            0xA5B2C7, LV_TEXT_ALIGN_CENTER);
    s_icon = create_label(screen, screen_width * 25 / 100, screen_height * 32 / 100,
                          screen_width * 50 / 100, screen_height * 20 / 100,
                          0x69B5FF, LV_TEXT_ALIGN_CENTER);
    s_pair_code = create_label(screen, margin, screen_height * 43 / 100,
                               screen_width - 2 * margin, screen_height * 10 / 100,
                               0xFFCC7C, LV_TEXT_ALIGN_CENTER);
    for (unsigned i = 0; i < 5; ++i) {
        s_activity[i] = create_circle(screen, 10, lv_color_hex(0x69B5FF));
        if (s_activity[i]) {
            lv_obj_set_pos(s_activity[i], screen_width / 2 - 38 + (lv_coord_t)i * 19,
                           screen_height * 78 / 100);
            lv_obj_set_style_radius(s_activity[i], 4, LV_PART_MAIN);
            lv_obj_add_flag(s_activity[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!header || !s_brand || !s_status || !s_title || !s_detail || !s_icon || !s_pair_code) {
        AOSL_LOG_ERR("LVGL UI: cannot create workflow display objects");
        return -1;
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (!s_activity[i]) {
            AOSL_LOG_ERR("LVGL UI: cannot create conversation activity indicator");
            return -1;
        }
    }
    lv_obj_set_style_text_font(s_icon, ui_display_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_icon, screen_height * 7 / 100, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_pair_code, ui_display_font(), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(s_pair_code, 3, LV_PART_MAIN);

    if (create_eye(&s_eyes[0], screen_width / 2 - eye_spacing / 2,
                   eye_center_y, eye_width, eye_height) < 0 ||
        create_eye(&s_eyes[1], screen_width / 2 + eye_spacing / 2,
                   eye_center_y, eye_width, eye_height) < 0) {
        AOSL_LOG_ERR("LVGL UI: cannot create animated eyes");
        return -1;
    }
    set_eye_visible(&s_eyes[0], false);
    set_eye_visible(&s_eyes[1], false);
    (void)create_chinese_fonts();
    if (s_chinese_font_ready) {
        lv_obj_set_style_text_font(s_title, s_chinese_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_detail, s_chinese_small_font, LV_PART_MAIN);
        lv_obj_set_style_text_font(s_status, s_chinese_small_font, LV_PART_MAIN);
    }
    lv_label_set_text(s_brand, "BOTSTATION");
    lv_label_set_text(s_status, "STARTING");
    lv_label_set_text(s_title, "");
    lv_label_set_text(s_detail, "");
    lv_label_set_text(s_icon, "");
    lv_label_set_text(s_pair_code, "");
    return 0;
}

static void set_eye_visible(eye_t *eye, bool visible) {
    if (visible) {
        lv_obj_clear_flag(eye->line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(eye->dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(eye->line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(eye->dot, LV_OBJ_FLAG_HIDDEN);
    }
    s_eyes_visible = visible;
}

static void apply_content(const mybot_lcd_content_t *content, uint32_t revision,
                          rv1126b_ui_wait_t wait_stage) {
    if (revision == s_applied_revision ||
        content->screen < MYBOT_LCD_SCREEN_STARTING ||
        content->screen >= MYBOT_LCD_SCREEN_COUNT) {
        return;
    }

    const screen_presentation_t *presentation = &s_presentations[content->screen];
    const char *title = s_chinese_font_ready ? presentation->title_zh
                                             : presentation->title_en;
    const char *detail = s_chinese_font_ready ? presentation->detail_zh
                                              : presentation->detail_en;
    const char *status = screen_status(content->screen, content->indicators);
    uint32_t accent = presentation->accent;
    bool animate_activity = false;
    switch (wait_stage) {
    case RV1126B_UI_WAIT_NETWORK:
        title = s_chinese_font_ready ? "等待网络连接" : "Waiting for network";
        detail = s_chinese_font_ready ? "等待获取 IPv4 地址和默认路由"
                                     : "Waiting for an IPv4 address and default route";
        break;
    case RV1126B_UI_WAIT_TIME:
        title = s_chinese_font_ready ? "等待系统时间校准" : "Waiting for clock correction";
        detail = s_chinese_font_ready ? "网络已就绪，请检查网络校时服务"
                                     : "Network ready; check the network time service";
        status = s_chinese_font_ready ? "校时" : "CLOCK";
        break;
    case RV1126B_UI_WAIT_SERVICES:
        title = s_chinese_font_ready ? "正在启动设备服务" : "Starting device services";
        detail = s_chinese_font_ready ? "网络和时间检查通过，正在初始化 SDK"
                                     : "Network and clock checks passed; starting SDK";
        break;
    case RV1126B_UI_WAIT_NONE:
        break;
    }
    if (content->screen == MYBOT_LCD_SCREEN_IN_CONVERSATION) {
        if (content->indicators & MYBOT_LCD_INDICATOR_LISTENING) {
            title = s_chinese_font_ready ? "聆听中" : "Listening";
            status = s_chinese_font_ready ? "聆听中" : "LISTENING";
            accent = 0x69DBA8;
            animate_activity = true;
        } else if (content->indicators & MYBOT_LCD_INDICATOR_THINKING) {
            title = s_chinese_font_ready ? "思考中" : "Thinking";
            status = s_chinese_font_ready ? "思考中" : "THINKING";
            accent = 0xFFCC7C;
            animate_activity = true;
        } else if (content->indicators & MYBOT_LCD_INDICATOR_SPEAKING) {
            title = s_chinese_font_ready ? "正在回复" : "Speaking";
            status = s_chinese_font_ready ? "正在回复" : "SPEAKING";
            accent = 0x69B5FF;
            animate_activity = true;
        }
        if (content->indicators & MYBOT_LCD_INDICATOR_VP_REGISTERED) {
            detail = s_chinese_font_ready ? "声纹已注册" : "Voiceprint registered";
        }
    }
    eye_mode_t eye_mode = EYE_MODE_NEUTRAL;
    if (content->screen == MYBOT_LCD_SCREEN_IN_CONVERSATION) {
        if (content->indicators & MYBOT_LCD_INDICATOR_LISTENING) {
            eye_mode = EYE_MODE_LISTENING;
        } else if (content->indicators & MYBOT_LCD_INDICATOR_THINKING) {
            eye_mode = EYE_MODE_THINKING;
        }
    }
    if (eye_mode != s_eye_mode) {
        s_eye_mode = eye_mode;
        s_eye_mode_since = lv_tick_get();
    }

    lv_label_set_text(s_title, title);
    lv_label_set_text(s_detail, detail);
    lv_label_set_text(s_status, status);
    lv_obj_set_style_text_color(s_title, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_icon, lv_color_hex(accent), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_pair_code, lv_color_hex(accent), LV_PART_MAIN);
    for (unsigned i = 0; i < 5; ++i) {
        lv_obj_set_style_bg_color(s_activity[i], lv_color_hex(accent), LV_PART_MAIN);
        if (animate_activity) {
            lv_obj_clear_flag(s_activity[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_activity[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (content->screen == MYBOT_LCD_SCREEN_PAIR_CODE) {
        lv_label_set_text(s_icon, "");
        lv_label_set_text(s_pair_code, content->pair_code);
        lv_obj_add_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pair_code, LV_OBJ_FLAG_HIDDEN);
        set_eye_visible(&s_eyes[0], false);
        set_eye_visible(&s_eyes[1], false);
    } else if (presentation->show_eyes) {
        lv_obj_add_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pair_code, LV_OBJ_FLAG_HIDDEN);
        set_eye_visible(&s_eyes[0], true);
        set_eye_visible(&s_eyes[1], true);
    } else {
        lv_label_set_text(s_icon, presentation->icon);
        lv_obj_clear_flag(s_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pair_code, LV_OBJ_FLAG_HIDDEN);
        set_eye_visible(&s_eyes[0], false);
        set_eye_visible(&s_eyes[1], false);
    }
    s_accent = accent;
    s_activity_phase = 0;
    s_applied_revision = revision;
}

static void update_eye(eye_t *eye, bool arc, bool dot,
                       lv_coord_t offset_x, lv_coord_t offset_y, lv_color_t color) {
    if (dot) {
        lv_obj_add_flag(eye->line, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(eye->dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(eye->dot, color, LV_PART_MAIN);
        lv_coord_t dot_size = eye->dot_size;
        lv_obj_set_size(eye->dot, dot_size, dot_size);
        lv_obj_set_pos(eye->dot,
                       eye->center_x - dot_size / 2 + offset_x,
                       eye->center_y - dot_size / 2 + offset_y);
        return;
    }

    lv_obj_add_flag(eye->dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(eye->line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_line_color(eye->line, color, LV_PART_MAIN);
    if (!eye->line_configured || eye->line_arc != arc) {
        for (unsigned i = 0; i < 9; ++i) {
            lv_coord_t x = eye->width * (lv_coord_t)i / 8;
            lv_coord_t y = eye->height / 2;
            if (arc) {
                lv_coord_t rise = eye->height * (lv_coord_t)i *
                    (8 - (lv_coord_t)i) / 16;
                y = eye->height - rise;
            } else {
                x = eye->width * 12 / 100 + eye->width * 76 / 100 *
                    (lv_coord_t)i / 8;
            }
            eye->points[i].x = x;
            eye->points[i].y = y;
        }
        lv_line_set_points(eye->line, eye->points, 9);
        eye->line_arc = arc;
        eye->line_configured = true;
    }
    lv_obj_set_pos(eye->line, eye->center_x - eye->width / 2 + offset_x,
                   eye->center_y - eye->height / 2 + offset_y);
}

static void animate_eyes(lv_timer_t *timer) {
    (void)timer;
    uint32_t now = lv_tick_get();

    mybot_lcd_content_t content;
    uint32_t revision;
    pthread_mutex_lock(&s_lock);
    content = s_content;
    revision = s_content_revision;
    rv1126b_ui_wait_t wait_stage = s_wait_stage;
    pthread_mutex_unlock(&s_lock);
    apply_content(&content, revision, wait_stage);

    if (content.screen == MYBOT_LCD_SCREEN_IN_CONVERSATION &&
        (content.indicators & MYBOT_LCD_INDICATOR_SERVER_STATE_MASK)) {
        static const uint8_t heights[6][5] = {
            {4, 8, 14, 8, 4}, {6, 12, 8, 14, 6}, {10, 6, 12, 6, 10},
            {14, 8, 4, 10, 14}, {8, 14, 10, 4, 8}, {6, 10, 14, 10, 6},
        };
        s_activity_phase = (s_activity_phase + 1) % 6;
        for (unsigned i = 0; i < 5; ++i) {
            lv_coord_t height = heights[s_activity_phase][i];
            lv_obj_set_size(s_activity[i], 8, height);
            lv_obj_set_pos(s_activity[i], lv_disp_get_hor_res(lv_disp_get_default()) / 2 -
                                          44 + (lv_coord_t)i * 20,
                           lv_disp_get_ver_res(lv_disp_get_default()) * 78 / 100 -
                               height / 2);
            lv_obj_set_style_bg_color(s_activity[i], lv_color_hex(s_accent), LV_PART_MAIN);
        }
    }

    if (s_eyes_visible) {
        bool thinking = s_eye_mode == EYE_MODE_THINKING;
        bool processing = thinking && (uint32_t)(now - s_eye_mode_since) >= 3000;
        bool listening = s_eye_mode == EYE_MODE_LISTENING;
        bool left_dot = listening || processing;
        bool right_dot = listening || thinking;
        bool left_arc = !listening && !thinking;
        bool right_arc = !listening && !thinking;
        float phase = (float)now / (processing ? 1200.0f : 1400.0f);
        lv_coord_t drift_x = thinking
            ? (lv_coord_t)(sinf(phase) * (processing ? 10.0f : 5.0f)) : 0;
        lv_coord_t orbit_y = processing ? (lv_coord_t)(cosf(phase) * 6.0f) : 0;
        lv_coord_t look_y = thinking && !processing ? -s_eyes[1].height / 8 : 0;
        lv_color_t eye_color = thinking ? lv_color_hex(0x8FCBDC)
                                        : lv_color_hex(0xF1F1EE);
        update_eye(&s_eyes[0], left_arc, left_dot,
                   -drift_x, processing ? orbit_y : 0, eye_color);
        update_eye(&s_eyes[1], right_arc, right_dot,
                   drift_x, processing ? -orbit_y : look_y, eye_color);
    }
}

static void publish_startup(int result) {
    pthread_mutex_lock(&s_lock);
    s_startup_result = result;
    s_startup_done = true;
    pthread_cond_broadcast(&s_started_cond);
    pthread_mutex_unlock(&s_lock);
}

static void destroy_scene(void) {
    if (lv_scr_act()) {
        lv_obj_clean(lv_scr_act());
    }
#ifdef BOTSTATION_LVGL_FREETYPE
    if (s_chinese_font_ready) {
        lv_ft_font_destroy(s_chinese_font);
        lv_ft_font_destroy(s_chinese_small_font);
        lv_freetype_destroy();
    }
#endif
    s_chinese_font = NULL;
    s_chinese_small_font = NULL;
    s_chinese_font_ready = false;
    s_eyes_visible = false;
}

static void *ui_worker(void *opaque) {
    (void)opaque;
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        AOSL_LOG_ERR("LVGL UI: cannot open /dev/dri/card0: %s", strerror(errno));
        publish_startup(-1);
        return NULL;
    }
    close(fd);

    lv_init();
    drm_disp_drv_init(0, 0, 270);
    lv_disp_t *disp = lv_disp_get_default();
    if (!disp || lv_disp_get_hor_res(disp) <= 0 ||
        lv_disp_get_ver_res(disp) <= 0) {
        AOSL_LOG_ERR("LVGL UI: DRM display driver returned an invalid resolution");
        drm_disp_drv_deinit();
        lv_deinit();
        publish_startup(-1);
        return NULL;
    }
    if (create_scene() < 0) {
        destroy_scene();
        drm_disp_drv_deinit();
        lv_deinit();
        publish_startup(-1);
        return NULL;
    }
    if (!lv_timer_create(animate_eyes, 30, NULL)) {
        AOSL_LOG_ERR("LVGL UI: cannot create eye animation timer");
        destroy_scene();
        drm_disp_drv_deinit();
        lv_deinit();
        publish_startup(-1);
        return NULL;
    }

    AOSL_LOG_NTC("LVGL DRM workflow UI ready: %dx%d",
                 lv_disp_get_hor_res(disp), lv_disp_get_ver_res(disp));
    publish_startup(0);
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 5000000};
    for (;;) {
        pthread_mutex_lock(&s_lock);
        bool stop = s_stop;
        pthread_mutex_unlock(&s_lock);
        if (stop) {
            break;
        }
        (void)lv_timer_handler();
        nanosleep(&pause, NULL);
    }

    destroy_scene();
    drm_disp_drv_deinit();
    lv_deinit();
    return NULL;
}

static int ui_lcd_init(void **out_context) {
    if (!out_context) {
        return -1;
    }
    pthread_mutex_lock(&s_lock);
    bool started = s_started && s_startup_done && s_startup_result == 0;
    pthread_mutex_unlock(&s_lock);
    if (!started) {
        AOSL_LOG_ERR("LVGL UI is unavailable while initializing LCD adapter");
        return -1;
    }
    *out_context = &s_content;
    return 0;
}

static int ui_lcd_render(void *context, const mybot_lcd_content_t *content) {
    if (context != &s_content || !content ||
        content->screen < MYBOT_LCD_SCREEN_STARTING ||
        content->screen >= MYBOT_LCD_SCREEN_COUNT) {
        AOSL_LOG_ERR("LVGL UI received invalid LCD content");
        return -1;
    }
    pthread_mutex_lock(&s_lock);
    s_content = *content;
    s_wait_stage = RV1126B_UI_WAIT_NONE;
    s_content.pair_code[MYBOT_LCD_PAIR_CODE_CAPACITY - 1] = '\0';
    ++s_content_revision;
    pthread_mutex_unlock(&s_lock);
    AOSL_LOG_NTC("[LCD] screen=%d rendered by LVGL DRM", (int)content->screen);
    return 0;
}

static void ui_lcd_destroy(void *context) {
    if (context != &s_content) {
        AOSL_LOG_ERR("LVGL UI received invalid LCD context during destroy");
    }
}

void rv1126b_lvgl_ui_set_wait(rv1126b_ui_wait_t stage) {
    if (stage < RV1126B_UI_WAIT_NONE || stage > RV1126B_UI_WAIT_SERVICES) {
        AOSL_LOG_ERR("LVGL UI received invalid wait stage: %d", (int)stage);
        return;
    }
    pthread_mutex_lock(&s_lock);
    s_wait_stage = stage;
    memset(&s_content, 0, sizeof(s_content));
    s_content.screen = stage == RV1126B_UI_WAIT_NETWORK
        ? MYBOT_LCD_SCREEN_WIFI_PROVISIONING : MYBOT_LCD_SCREEN_STARTING_SERVICES;
    ++s_content_revision;
    pthread_mutex_unlock(&s_lock);
}

int rv1126b_lvgl_ui_start(void) {
    pthread_mutex_lock(&s_lock);
    if (s_started) {
        pthread_mutex_unlock(&s_lock);
        AOSL_LOG_ERR("LVGL UI start requested more than once");
        return -1;
    }
    s_stop = false;
    s_startup_done = false;
    s_startup_result = -1;
    memset(&s_content, 0, sizeof(s_content));
    s_content.screen = MYBOT_LCD_SCREEN_STARTING;
    s_wait_stage = RV1126B_UI_WAIT_NONE;
    s_content_revision = 1;
    s_applied_revision = UINT32_MAX;
    int err = pthread_create(&s_thread, NULL, ui_worker, NULL);
    if (err != 0) {
        pthread_mutex_unlock(&s_lock);
        AOSL_LOG_ERR("LVGL UI thread: %s", strerror(err));
        return -1;
    }
    s_started = true;
    while (!s_startup_done) {
        err = pthread_cond_wait(&s_started_cond, &s_lock);
        if (err != 0) {
            s_stop = true;
            pthread_mutex_unlock(&s_lock);
            AOSL_LOG_ERR("LVGL UI startup wait: %s", strerror(err));
            pthread_join(s_thread, NULL);
            pthread_mutex_lock(&s_lock);
            s_started = false;
            pthread_mutex_unlock(&s_lock);
            return -1;
        }
    }
    int result = s_startup_result;
    pthread_mutex_unlock(&s_lock);
    if (result < 0) {
        pthread_join(s_thread, NULL);
        pthread_mutex_lock(&s_lock);
        s_started = false;
        pthread_mutex_unlock(&s_lock);
        return -1;
    }
    return 0;
}

void rv1126b_lvgl_ui_stop(void) {
    pthread_mutex_lock(&s_lock);
    if (!s_started) {
        pthread_mutex_unlock(&s_lock);
        return;
    }
    s_stop = true;
    pthread_mutex_unlock(&s_lock);
    int err = pthread_join(s_thread, NULL);
    if (err != 0) {
        AOSL_LOG_ERR("cannot join LVGL UI thread: %s", strerror(err));
        abort();
    }
    pthread_mutex_lock(&s_lock);
    s_started = false;
    s_startup_done = false;
    pthread_mutex_unlock(&s_lock);
}

const mybot_lcd_ops_t *rv1126b_lvgl_lcd_ops(void) {
    static const mybot_lcd_ops_t ops = {
        .init = ui_lcd_init,
        .render = ui_lcd_render,
        .destroy = ui_lcd_destroy,
    };
    return &ops;
}

#else

void rv1126b_lvgl_ui_set_wait(rv1126b_ui_wait_t stage) {
    (void)stage;
}

int rv1126b_lvgl_ui_start(void) {
    return 0;
}

void rv1126b_lvgl_ui_stop(void) {}

const mybot_lcd_ops_t *rv1126b_lvgl_lcd_ops(void) {
    return linux_lcd_platform_console_ops();
}

#endif
