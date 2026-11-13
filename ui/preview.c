/* SPDX-License-Identifier: Apache-2.0 */
#include "renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int save(const char *directory, const char *name, unsigned rotation) {
    unsigned width = rotation % 180 ? 480 : 640, height = rotation % 180 ? 640 : 480;
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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s EXISTING_OUTPUT_DIRECTORY\n", argv[0]);
        return 2;
    }
    if (rv1126b_renderer_create(NULL, NULL, 1) < 0)
        return 1;
    mybot_display_runtime_status_t status = {"ready", false};
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
    rv1126b_renderer_destroy();
    if (failed)
        perror("preview");
    else
        printf("Rendered 10 workflow pages, 23 expressions and physical orientation to %s\n",
               argv[1]);
    return failed ? 1 : 0;
}
