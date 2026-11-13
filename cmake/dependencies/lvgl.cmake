# SPDX-License-Identifier: Apache-2.0
include("${CMAKE_CURRENT_LIST_DIR}/lvgl-sources.cmake")
add_library(mybot_lvgl STATIC ${MYBOT_LVGL_SOURCES}
    "${PROJECT_SOURCE_DIR}/components/mybot_ui/src/lv_mem_clib.c")
target_include_directories(mybot_lvgl PUBLIC "${PROJECT_SOURCE_DIR}/third_party/lvgl"
    PRIVATE "${PROJECT_SOURCE_DIR}/third_party/lvgl/src"
    "${PROJECT_SOURCE_DIR}/components/mybot_ui")
target_compile_definitions(mybot_lvgl PUBLIC LV_KCONFIG_IGNORE=1
    LV_CONF_PATH="${PROJECT_SOURCE_DIR}/components/mybot_ui/lv_conf.h")
