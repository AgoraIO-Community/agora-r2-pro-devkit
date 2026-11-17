# SPDX-License-Identifier: Apache-2.0
set(display_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/display")
add_library(mybot_pixels STATIC src/drivers/display/pixels.c)
target_link_libraries(mybot_pixels PUBLIC mybot_ui_contract)
target_compile_options(mybot_pixels PRIVATE -Wall -Wextra -Werror)
add_library(mybot_volume STATIC src/services/audio_volume.c)
target_include_directories(mybot_volume PUBLIC include)
target_link_libraries(mybot_volume PUBLIC mybot::public_headers Threads::Threads)
target_compile_options(mybot_volume PRIVATE -Wall -Wextra -Werror)
add_library(mybot_prompt STATIC src/services/audio_prompt.c)
target_include_directories(mybot_prompt PUBLIC include)
target_link_libraries(mybot_prompt PUBLIC mybot::public_headers Threads::Threads)
target_compile_options(mybot_prompt PRIVATE -Wall -Wextra -Werror)
add_library(mybot_time_sync STATIC src/services/time_sync.c)
target_include_directories(mybot_time_sync PUBLIC include)
target_compile_options(mybot_time_sync PRIVATE -Wall -Wextra -Werror)
add_library(mybot_network_route STATIC src/services/network_route.c)
target_compile_options(mybot_network_route PRIVATE -Wall -Wextra -Werror)
find_package(OpenSSL REQUIRED)
set(wifi_portal_header "${CMAKE_CURRENT_BINARY_DIR}/generated/wifi/wifi_portal_page.h")
add_custom_command(OUTPUT "${wifi_portal_header}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/embed-wifi-portal.py"
        --input "${CMAKE_CURRENT_SOURCE_DIR}/src/services/wifi/portal.html"
        --output "${wifi_portal_header}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${wifi_portal_header}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/services/wifi/portal.html"
        "${PROJECT_SOURCE_DIR}/scripts/embed-wifi-portal.py"
    VERBATIM)
add_library(mybot_wifi STATIC "${wifi_portal_header}" src/services/wifi/wifi_common.c
    src/services/wifi/wifi_backend.c src/services/wifi/wifi_credentials.c
    src/services/wifi/wifi_process.c src/services/wifi/wifi_radio.c
    src/services/wifi/wifi_wpa_control.c src/services/wifi/wifi_portal.c
    src/services/wifi/wifi_service.c src/services/wifi/system_wifi_profile.c)
set_target_properties(mybot_wifi PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
target_include_directories(mybot_wifi PUBLIC include PRIVATE src/services/wifi
    "${CMAKE_CURRENT_BINARY_DIR}/generated/wifi")
target_link_libraries(mybot_wifi PUBLIC Threads::Threads OpenSSL::Crypto PRIVATE mybot_network_route)
target_compile_options(mybot_wifi PRIVATE -Wall -Wextra -Werror)
add_library(mybot_power STATIC src/services/power.c)
target_include_directories(mybot_power PUBLIC include)
target_compile_options(mybot_power PRIVATE -Wall -Wextra -Werror)
add_executable(mybot-supervisor src/services/supervisor.c)
target_include_directories(mybot-supervisor PRIVATE include)
target_compile_options(mybot-supervisor PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(mybot-supervisor PRIVATE RV1126B_VERSION="${PROJECT_VERSION}")
set_target_properties(mybot-supervisor PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(RV1126B_HOST_ONLY)
    return()
endif()

add_library(mybot_linux_display STATIC src/services/display_runtime.c src/drivers/display/drm.c)
target_include_directories(mybot_linux_display PUBLIC include PRIVATE "${display_dir}"
    "${RV1126B_DRM_INCLUDE}" "${RV1126B_DRM_UAPI}")
target_link_libraries(mybot_linux_display PUBLIC mybot_ui_contract
    PRIVATE mybot_ui mybot_pixels "${RV1126B_DRM_LIBRARY}" Threads::Threads)
target_compile_options(mybot_linux_display PRIVATE -Wall -Wextra -Werror)

add_library(mybot_platform STATIC src/platform.c src/drivers/audio/alsa_audio.c
    src/drivers/input/evdev_input.c src/drivers/input/input_logic.c src/services/network_monitor.c)
target_include_directories(mybot_platform PUBLIC include PRIVATE src/internal
    "${RV1126B_BOARD_INCLUDE}" "${RV1126B_ALSA_INCLUDE}")
target_compile_options(mybot_platform PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mybot_platform PUBLIC mybot::sdk mybot_linux_display
    PRIVATE mybot_linux_services mybot_wifi mybot_volume mybot_prompt mybot_time_sync mybot_power mybot_network_route "${RV1126B_ALSA_LIBRARY}" Threads::Threads)
if(MYBOT_ENABLE_VIDEO)
    find_path(RV1126B_MPP_INCLUDE rk_mpi.h PATH_SUFFIXES rockchip REQUIRED)
    find_library(RV1126B_MPP_LIBRARY rockchip_mpp REQUIRED)
    target_sources(mybot_platform PRIVATE src/drivers/video/v4l2_mpp_video.c
        src/drivers/video/video_lifecycle.c)
    target_include_directories(mybot_platform PRIVATE "${RV1126B_MPP_INCLUDE}")
    target_link_libraries(mybot_platform PRIVATE "${RV1126B_MPP_LIBRARY}")
endif()
