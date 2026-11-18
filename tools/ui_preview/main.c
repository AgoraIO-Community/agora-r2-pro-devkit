/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_ui/renderer.h"
#include "pixels.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int save(const char *directory, const char *name, unsigned rotation) {
    unsigned width = rotation % 180 ? RV1126B_UI_HEIGHT : RV1126B_UI_WIDTH,
             height = rotation % 180 ? RV1126B_UI_WIDTH : RV1126B_UI_HEIGHT;
    size_t size = (size_t)width * height * sizeof(uint32_t);
    uint32_t *output = malloc(size);
    unsigned char *row = malloc((size_t)width * 3);
    char path[1024];
    const rv1126b_rect_t full = {0, 0, RV1126B_UI_WIDTH - 1, RV1126B_UI_HEIGHT - 1};
    if (!output || !row ||
        snprintf(path, sizeof(path), "%s/%s.ppm", directory, name) >= (int)sizeof(path)) {
        free(output);
        free(row);
        return -1;
    }
    if (rv1126b_pixels_blit(rv1126b_renderer_canvas(), output, size, width * 4, width, height,
                            rotation, &full) < 0) {
        free(output);
        free(row);
        return -1;
    }
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(output);
        free(row);
        return -1;
    }
    int failed = fprintf(file, "P6\n%u %u\n255\n", width, height) < 0;
    for (unsigned y = 0; y < height && !failed; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t pixel = output[(size_t)y * width + x];
            row[x * 3] = (unsigned char)(pixel >> 16);
            row[x * 3 + 1] = (unsigned char)(pixel >> 8);
            row[x * 3 + 2] = (unsigned char)pixel;
        }
        failed = fwrite(row, 3, width, file) != width;
    }
    if (fclose(file))
        failed = 1;
    free(output);
    free(row);
    return failed ? -1 : 0;
}

typedef struct {
    const char *name;
    mybot_lcd_screen_t screen;
    const char *state;
    const char *ssid;
    mybot_display_clock_state_t clock;
    uint32_t indicators;
    mybot_settings_page_t settings;
} manual_scene_t;

static int render_manual(const char *directory) {
    /* Synthetic examples only: no device identities, credentials or events. */
    static const manual_scene_t scenes[] = {
        {"starting", MYBOT_LCD_SCREEN_STARTING, "starting", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_CLOSED},
        {"waiting-network", MYBOT_LCD_SCREEN_WIFI_PROVISIONING, "waiting_network", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, 0, MYBOT_SETTINGS_CLOSED},
        {"connecting-services", MYBOT_LCD_SCREEN_STARTING_SERVICES, "starting_services", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, 0, MYBOT_SETTINGS_CLOSED},
        {"ready", MYBOT_LCD_SCREEN_READY, "ready", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_CLOSED},
        {"ap-hotspot", MYBOT_LCD_SCREEN_WIFI_PROVISIONING, "waiting_network", "mybot-a1b2",
         MYBOT_DISPLAY_CLOCK_INACTIVE, 0, MYBOT_SETTINGS_CLOSED},
        {"pairing-wait", MYBOT_LCD_SCREEN_PAIRING, "pairing", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_CLOSED},
        {"pairing-code", MYBOT_LCD_SCREEN_PAIR_CODE, "pairing", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE,
         0, MYBOT_SETTINGS_CLOSED},
        {"conversation-listening", MYBOT_LCD_SCREEN_IN_CONVERSATION, "in_conversation", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, MYBOT_LCD_INDICATOR_LISTENING, MYBOT_SETTINGS_CLOSED},
        {"conversation-thinking", MYBOT_LCD_SCREEN_IN_CONVERSATION, "in_conversation", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, MYBOT_LCD_INDICATOR_THINKING, MYBOT_SETTINGS_CLOSED},
        {"conversation-speaking", MYBOT_LCD_SCREEN_IN_CONVERSATION, "in_conversation", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, MYBOT_LCD_INDICATOR_SPEAKING, MYBOT_SETTINGS_CLOSED},
        {"settings-menu", MYBOT_LCD_SCREEN_READY, "ready", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_MENU},
        {"settings-confirm", MYBOT_LCD_SCREEN_READY, "ready", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_CONFIRM_POWEROFF},
        {"settings-reset-confirm", MYBOT_LCD_SCREEN_READY, "ready", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, 0, MYBOT_SETTINGS_CONFIRM_RESET},
        {"settings-powering-off", MYBOT_LCD_SCREEN_READY, "ready", NULL,
         MYBOT_DISPLAY_CLOCK_INACTIVE, 0, MYBOT_SETTINGS_POWERING_OFF},
        {"settings-error", MYBOT_LCD_SCREEN_READY, "ready", NULL, MYBOT_DISPLAY_CLOCK_INACTIVE, 0,
         MYBOT_SETTINGS_POWEROFF_ERROR},
        {"clock-error", MYBOT_LCD_SCREEN_STARTING_SERVICES, "starting_services", NULL,
         MYBOT_DISPLAY_CLOCK_FAILED, 0, MYBOT_SETTINGS_CLOSED},
    };
    for (size_t i = 0; i < sizeof(scenes) / sizeof(scenes[0]); ++i) {
        const manual_scene_t *scene = &scenes[i];
        if (rv1126b_renderer_create(NULL, NULL, 1) < 0)
            return 1;
        mybot_display_runtime_status_t status = {0};
        mybot_lcd_content_t content = {scene->screen, "000042", scene->indicators};
        snprintf(status.state, sizeof(status.state), "%s", scene->state);
        if (scene->ssid)
            snprintf(status.provisioning_ssid, sizeof(status.provisioning_ssid), "%s", scene->ssid);
        status.in_conversation = scene->screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
        status.clock_state = scene->clock;
        rv1126b_renderer_update(&content, &status, 100);
        rv1126b_renderer_settings(scene->settings);
        for (uint32_t now = 400; now <= 1600; now += 300)
            rv1126b_renderer_step(now);
        const int failed = save(directory, scene->name, 0) < 0;
        rv1126b_renderer_destroy();
        if (failed) {
            perror("manual preview");
            return 1;
        }
    }
#if CONFIG_MYBOT_LANGUAGE_ZH_CN
    const char *language = "zh-CN";
#else
    const char *language = "en-US";
#endif
    printf("Rendered %zu manual scenes; language=%s; output=%s\n",
           sizeof(scenes) / sizeof(scenes[0]), language, directory);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--manual"))
        return render_manual(argv[2]);
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [--manual] EXISTING_OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (rv1126b_renderer_create(NULL, NULL, 1) < 0)
        return 1;
    mybot_display_runtime_status_t status = {.state = "ready", .in_conversation = false};
    uint32_t now = 100;
    int failed = 0;
    for (int i = 0; i < MYBOT_LCD_SCREEN_COUNT && !failed; ++i) {
        mybot_lcd_content_t content = {(mybot_lcd_screen_t)i, "000042", 0};
        const char *states[] = {"starting", "waiting_network", "offline", "starting_services",
                                "pairing",  "pairing",         "ready",   "in_conversation",
                                "failed",   "stopping"};
        snprintf(status.state, sizeof(status.state), "%s", states[i]);
        status.in_conversation = i == MYBOT_LCD_SCREEN_IN_CONVERSATION;
        char name[32];
        snprintf(name, sizeof(name), "screen-%02d", i);
        rv1126b_renderer_update(&content, &status, now);
        rv1126b_renderer_step(now += 300);
        failed = save(argv[1], name, 0) < 0;
    }
    const mybot_lcd_content_t ready = {MYBOT_LCD_SCREEN_READY, {0}, 0};
    snprintf(status.state, sizeof(status.state), "%s", "ready");
    status.in_conversation = false;
    rv1126b_renderer_update(&ready, &status, now);
    for (int i = 0; i < MYBOT_EXPRESSION_COUNT && !failed; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "expression-%s", mybot_expression_name((mybot_expression_t)i));
        rv1126b_renderer_expression(i);
        rv1126b_renderer_step(now += 500);
        rv1126b_renderer_step(now += 500);
        failed = save(argv[1], name, 0) < 0;
    }
    rv1126b_renderer_expression(MYBOT_EXPRESSION_NEUTRAL);
    rv1126b_renderer_step(now += 1000);
    rv1126b_renderer_step(now += 1000);
    if (!failed)
        failed = save(argv[1], "physical-270", 270) < 0;
    const char *settings_names[] = {
        "settings-menu",          "settings-confirm",   "settings-powering-off", "settings-error",
        "settings-reset-confirm", "settings-resetting", "settings-reset-error"};
    for (int page = MYBOT_SETTINGS_MENU; page <= MYBOT_SETTINGS_RESET_ERROR && !failed; ++page) {
        rv1126b_renderer_settings((mybot_settings_page_t)page);
        rv1126b_renderer_step(now += 1000);
        failed = save(argv[1], settings_names[page - 1], 0) < 0;
    }
    rv1126b_renderer_destroy();
    if (failed)
        perror("preview");
    else
        printf(
            "Rendered 10 workflow pages, 23 expressions, 7 settings pages and orientation to %s\n",
            argv[1]);
    return failed ? 1 : 0;
}
