# SPDX-License-Identifier: Apache-2.0
# Public SDK types are sufficient for host rendering; RTSA is not linked.
add_library(mybot_public_headers INTERFACE)
add_library(mybot::public_headers ALIAS mybot_public_headers)
target_include_directories(mybot_public_headers INTERFACE "${PROJECT_SOURCE_DIR}/third_party/mybot/include")

add_library(mybot_ui_contract INTERFACE)
target_include_directories(mybot_ui_contract INTERFACE "${PROJECT_SOURCE_DIR}/components/mybot_ui/include")
target_link_libraries(mybot_ui_contract INTERFACE mybot::public_headers)
