/* SPDX-License-Identifier: Apache-2.0 */
/* The imported LVGL production subset omits malloc backends. Linux uses libc. */
#include "lvgl.h"
#include <stdlib.h>
#include <string.h>

void lv_mem_init(void) {
}
void lv_mem_deinit(void) {
}
lv_mem_pool_t lv_mem_add_pool(void *memory, size_t bytes) {
    (void)memory;
    (void)bytes;
    return NULL;
}
void lv_mem_remove_pool(lv_mem_pool_t pool) {
    (void)pool;
}
void *lv_malloc_core(size_t bytes) {
    return malloc(bytes);
}
void *lv_realloc_core(void *memory, size_t bytes) {
    return realloc(memory, bytes);
}
void lv_free_core(void *memory) {
    free(memory);
}
void lv_mem_monitor_core(lv_mem_monitor_t *monitor) {
    /* libc exposes no portable allocator statistics; no heap-size claim is made. */
    memset(monitor, 0, sizeof(*monitor));
}
lv_result_t lv_mem_test_core(void) {
    return LV_RESULT_OK;
}
