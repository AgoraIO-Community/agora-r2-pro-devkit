# SPDX-License-Identifier: Apache-2.0
add_executable(mybot-ui-preview main.c)
set_target_properties(mybot-ui-preview PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(mybot-ui-preview PRIVATE "${PROJECT_SOURCE_DIR}/components/mybot_platform/src/drivers/display")
target_link_libraries(mybot-ui-preview PRIVATE mybot_ui mybot_pixels)
