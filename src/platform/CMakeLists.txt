# SPDX-License-Identifier: Apache-2.0
set(display_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/display")
add_library(mybot_pixels STATIC src/drivers/display/pixels.c)
target_link_libraries(mybot_pixels PUBLIC mybot_ui_contract)
target_compile_options(mybot_pixels PRIVATE -Wall -Wextra -Werror)
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
    PRIVATE mybot_linux_services "${RV1126B_ALSA_LIBRARY}" Threads::Threads)
if(MYBOT_ENABLE_VIDEO)
    find_path(RV1126B_MPP_INCLUDE rk_mpi.h PATH_SUFFIXES rockchip REQUIRED)
    find_library(RV1126B_MPP_LIBRARY rockchip_mpp REQUIRED)
    target_sources(mybot_platform PRIVATE src/drivers/video/v4l2_mpp_video.c)
    target_include_directories(mybot_platform PRIVATE "${RV1126B_MPP_INCLUDE}")
    target_link_libraries(mybot_platform PRIVATE "${RV1126B_MPP_LIBRARY}")
endif()
