/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_ui/renderer.h"
#include "pixels.h"
#include "check.h"
#include "lvgl.h"
#include "src/misc/lv_text_private.h"
#include <string.h>

LV_FONT_DECLARE(mybot_lvgl_font_40);
LV_FONT_DECLARE(mybot_terminal_font_18);
LV_FONT_DECLARE(mybot_pair_font_64);
static int s_fail;
static uint32_t *s_frame;

static lv_obj_t *find_label(lv_obj_t *object, const char *text) {
    if (lv_obj_check_type(object, &lv_label_class) && !strcmp(lv_label_get_text(object), text))
        return object;
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i) {
        lv_obj_t *found = find_label(lv_obj_get_child(object, i), text);
        if (found)
            return found;
    }
    return NULL;
}

static void check_visible_labels(lv_obj_t *object) {
    if (!lv_obj_is_visible(object))
        return;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *text = lv_label_get_text(object);
        const lv_font_t *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
        lv_point_t needed;
        bool wraps = lv_label_get_long_mode(object) == LV_LABEL_LONG_WRAP;
        lv_text_get_size(
            &needed, text, font, lv_obj_get_style_text_letter_space(object, LV_PART_MAIN),
            lv_obj_get_style_text_line_space(object, LV_PART_MAIN),
            wraps ? lv_obj_get_content_width(object) : LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        CHECK(needed.x <= lv_obj_get_content_width(object));
        CHECK(needed.y <= lv_obj_get_content_height(object));
        lv_area_t box;
        lv_obj_get_coords(object, &box);
        CHECK(box.x1 >= 0 && box.y1 >= 0 && box.x2 < RV1126B_UI_WIDTH &&
              box.y2 < RV1126B_UI_HEIGHT);
        for (uint32_t index = 0; text[index];) {
            uint32_t character = lv_text_encoded_next(text, &index);
            if (character == '\n')
                continue;
            lv_font_glyph_dsc_t glyph;
            CHECK(lv_font_get_glyph_dsc(font, &glyph, character, 0));
            CHECK(!glyph.is_placeholder);
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
        check_visible_labels(lv_obj_get_child(object, i));
}

/* Whole-frame nearest-neighbor doubling yields a 1.0 score for one alignment.
 * Real output from the renderer must contain independent native text pixels. */
static void check_native_text(lv_obj_t *label) {
    lv_area_t box;
    lv_obj_get_coords(label, &box);
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    CHECK(font == &mybot_terminal_font_18 && font->line_height >= 24);
    lv_point_t needed;
    lv_text_get_size(&needed, lv_label_get_text(label), font, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    double worst = 0;
    for (int oy = 0; oy < 2; ++oy)
        for (int ox = 0; ox < 2; ++ox) {
            unsigned occupied = 0, uniform = 0;
            for (int y = box.y1 + oy; y + 1 < box.y1 + needed.y; y += 2)
                for (int x = box.x1 + ox; x + 1 < box.x1 + needed.x; x += 2) {
                    uint32_t a = s_frame[y * RV1126B_UI_WIDTH + x] & 0xffffff;
                    uint32_t b = s_frame[y * RV1126B_UI_WIDTH + x + 1] & 0xffffff;
                    uint32_t c = s_frame[(y + 1) * RV1126B_UI_WIDTH + x] & 0xffffff;
                    uint32_t d = s_frame[(y + 1) * RV1126B_UI_WIDTH + x + 1] & 0xffffff;
                    if (a == 0x050708 && b == a && c == a && d == a)
                        continue;
                    ++occupied;
                    uniform += a == b && a == c && a == d;
                }
            CHECK(occupied > 20);
            double ratio = (double)uniform / occupied;
            if (ratio > worst)
                worst = ratio;
        }
    printf("Native text maximum uniform 2x2 block ratio: %.3f\n", worst);
    CHECK(worst < 0.9);
}
static void check_no_unimplemented_portal(lv_obj_t *object) {
    if (lv_obj_check_type(object, &lv_label_class))
        CHECK(!strstr(lv_label_get_text(object), "192.168.4.1"));
    for (unsigned i = 0; i < lv_obj_get_child_count(object); ++i)
        check_no_unimplemented_portal(lv_obj_get_child(object, i));
}
static int present(const uint32_t *canvas, const rv1126b_rect_t *area, void *user) {
    CHECK(canvas && area && user == &s_fail);
    return s_fail ? -1
                  : rv1126b_pixels_blit(canvas, s_frame,
                                        RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT * sizeof(uint32_t),
                                        RV1126B_UI_WIDTH * sizeof(uint32_t), RV1126B_UI_WIDTH,
                                        RV1126B_UI_HEIGHT, 0, area);
}
static uint64_t hash(const uint32_t *pixels) {
    uint64_t value = UINT64_C(1469598103934665603);
    for (unsigned i = 0; i < RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT; ++i)
        value = (value ^ pixels[i]) * UINT64_C(1099511628211);
    return value;
}
int main(void) {
    s_frame = calloc(RV1126B_UI_WIDTH * RV1126B_UI_HEIGHT, sizeof(uint32_t));
    CHECK(s_frame);
    lv_font_glyph_dsc_t native_glyph;
    CHECK(lv_font_get_glyph_dsc(&mybot_terminal_font_18, &native_glyph, 'M', 0));
    CHECK(native_glyph.box_h >= 12);
    const uint32_t chinese[] = {0x7b49, 0x5f85, 0x7f51, 0x7edc, 0x6b63, 0x5728, 0x8fde, 0x63a5};
    for (unsigned i = 0; i < sizeof(chinese) / sizeof(chinese[0]); ++i) {
        lv_font_glyph_dsc_t glyph;
        CHECK(lv_font_get_glyph_dsc(&mybot_lvgl_font_40, &glyph, chinese[i], 0));
        CHECK(!glyph.is_placeholder);
    }
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        uint32_t now = 1;
        CHECK(rv1126b_renderer_create(present, &s_fail, now) == 0);
        CHECK(rv1126b_renderer_create(present, &s_fail, now) < 0);
        mybot_display_runtime_status_t status = {"in_conversation", true};
        for (int screen = 0; screen < MYBOT_LCD_SCREEN_COUNT; ++screen) {
            mybot_lcd_content_t content = {(mybot_lcd_screen_t)screen, "000042", 0};
            rv1126b_renderer_update(&content, &status, now);
            rv1126b_renderer_step(now += 500);
            CHECK(rv1126b_renderer_canvas());
            check_no_unimplemented_portal(lv_display_get_screen_active(lv_display_get_default()));
            check_visible_labels(lv_display_get_screen_active(lv_display_get_default()));
            if (screen == MYBOT_LCD_SCREEN_PAIR_CODE) {
                lv_obj_t *code =
                    find_label(lv_display_get_screen_active(lv_display_get_default()), "000042");
                CHECK(code &&
                      lv_obj_get_style_text_font(code, LV_PART_MAIN) == &mybot_pair_font_64);
            }
        }
        const mybot_lcd_content_t conversation = {MYBOT_LCD_SCREEN_IN_CONVERSATION, {0}, 0};
        rv1126b_renderer_update(&conversation, &status, now);
        rv1126b_renderer_step(now += 1000);
        CHECK(rv1126b_renderer_canvas());
        lv_obj_t *long_state = find_label(lv_display_get_screen_active(lv_display_get_default()),
                                          "> state: in_conversation | vp:pending");
        CHECK(long_state && lv_obj_is_visible(long_state));
        check_visible_labels(lv_display_get_screen_active(lv_display_get_default()));
        lv_point_t width;
        lv_text_get_size(&width, "> MyBot / RV1126B", &mybot_terminal_font_18, 0, 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_NONE);
        CHECK(width.x <= 419);
        const mybot_lcd_content_t ready = {MYBOT_LCD_SCREEN_READY, {0}, 0};
        rv1126b_renderer_update(&ready, &status, now);
        rv1126b_renderer_step(now += 500);
        rv1126b_renderer_step(now += 500);
        CHECK(rv1126b_renderer_canvas());
        lv_obj_t *brand =
            find_label(lv_display_get_screen_active(lv_display_get_default()), "> MyBot / RV1126B");
        CHECK(brand && lv_obj_is_visible(brand));
        check_native_text(brand);
        uint64_t hashes[MYBOT_EXPRESSION_COUNT];
        unsigned distinct = 0;
        for (int expression = 0; expression < MYBOT_EXPRESSION_COUNT; ++expression) {
            CHECK(rv1126b_renderer_expression(expression) == 0);
            rv1126b_renderer_step(now += 500);
            rv1126b_renderer_step(now += 500);
            hashes[expression] = hash(rv1126b_renderer_canvas());
            check_visible_labels(lv_display_get_screen_active(lv_display_get_default()));
            bool unique = true;
            for (int old = 0; old < expression; ++old)
                unique = unique && hashes[old] != hashes[expression];
            distinct += unique;
        }
        CHECK(distinct >= 20);
        CHECK(!rv1126b_renderer_failed());
        s_fail = 1;
        rv1126b_renderer_update(&(mybot_lcd_content_t){MYBOT_LCD_SCREEN_PAIR_CODE, "135790", 0},
                                &status, now);
        CHECK(rv1126b_renderer_canvas());
        CHECK(rv1126b_renderer_failed());
        s_fail = 0;
        rv1126b_renderer_destroy();
        rv1126b_renderer_destroy();
    }
    free(s_frame);
    puts("Real LVGL: 10 pages, 23 expressions, Chinese glyphs, label width and teardown: PASS");
    return 0;
}
