# SPDX-License-Identifier: Apache-2.0
add_library(mybot_ui STATIC src/renderer.c
    views/pixel_glasses/pixel_glasses_view.cc
    views/pixel_glasses/pixel_expressions.c
    fonts/generated/pixel_fonts.c fonts/generated/lvgl_fonts.c fonts/generated/pair_font.c)
add_library(mybot::ui ALIAS mybot_ui)
target_include_directories(mybot_ui PRIVATE src views/pixel_glasses)
target_compile_definitions(mybot_ui PUBLIC CONFIG_MYBOT_LANGUAGE_ZH_CN=${RV1126B_CHINESE}
    CONFIG_MYBOT_LVGL_UI_ANIMATIONS=1)
target_compile_options(mybot_ui PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mybot_ui PUBLIC mybot_ui_contract PRIVATE mybot_lvgl m)
