# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.18)
project(mybot_rv1126b VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RV1126B_HOST_ONLY "Build native UI previews and adapter tests without target SDK" OFF)
option(RV1126B_BUILD_TESTS "Build native tests" ${RV1126B_HOST_ONLY})
option(RV1126B_ENABLE_UBSAN "Instrument native host tests with undefined-behavior sanitizer" OFF)
if(RV1126B_ENABLE_UBSAN)
    if(NOT RV1126B_HOST_ONLY)
        message(FATAL_ERROR "Use RV1126B_ENABLE_UBSAN only with the native host test build")
    endif()
    add_compile_options(-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined -fno-sanitize-recover=all)
endif()
option(MYBOT_ENABLE_VIDEO "Include optional V4L2/MPP video uplink" OFF)
set(RV1126B_LANGUAGE "zh-CN" CACHE STRING "UI build language: zh-CN or en-US")
set_property(CACHE RV1126B_LANGUAGE PROPERTY STRINGS zh-CN en-US)
if(RV1126B_LANGUAGE STREQUAL "zh-CN")
    set(RV1126B_CHINESE 1)
elseif(RV1126B_LANGUAGE STREQUAL "en-US")
    set(RV1126B_CHINESE 0)
else()
    message(FATAL_ERROR "RV1126B_LANGUAGE must be zh-CN or en-US")
endif()

find_package(Threads REQUIRED)
file(GLOB_RECURSE RV1126B_LVGL_SOURCES CONFIGURE_DEPENDS "third_party/lvgl/src/*.c")
add_library(rv1126b_lvgl STATIC ${RV1126B_LVGL_SOURCES} ui/lv_mem_clib.c)
target_include_directories(rv1126b_lvgl PUBLIC third_party/lvgl third_party/lvgl/src ui/include)
target_compile_definitions(rv1126b_lvgl PUBLIC LV_KCONFIG_IGNORE=1
    LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ui/include/lv_conf.h")
add_library(rv1126b_view STATIC ui/view/pixel_glasses_view.cc ui/view/pixel_expressions.c
    ui/view/pixel_fonts.c ui/view/lvgl_fonts.c ui/view/pair_font.c ui/renderer.c ui/pixels.c)
target_compile_definitions(rv1126b_view PUBLIC
    CONFIG_MYBOT_LANGUAGE_ZH_CN=${RV1126B_CHINESE} CONFIG_MYBOT_LVGL_UI_ANIMATIONS=1)
target_include_directories(rv1126b_view PUBLIC ui ui/include third_party/mybot/include)
target_compile_options(rv1126b_view PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rv1126b_view PUBLIC rv1126b_lvgl m)

if(RV1126B_HOST_ONLY)
    add_executable(mybot-ui-preview ui/preview.c)
    target_link_libraries(mybot-ui-preview PRIVATE rv1126b_view)
    if(RV1126B_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
    return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Use the AArch64 toolchain for the device app, or RV1126B_HOST_ONLY=ON")
endif()
set(CONFIG_PLATFORM linux CACHE STRING "" FORCE)
set(MYBOT_BUILD_LINUX_PLATFORM OFF CACHE BOOL "" FORCE)
set(MYBOT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MYBOT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MYBOT_WAKE_WORDS OFF CACHE BOOL "" FORCE)
set(MYBOT_CLOUD_AEC ON CACHE BOOL "" FORCE)
set(MYBOT_ENABLE_HTTPS ON CACHE BOOL "" FORCE)
set(MYBOT_ALLOW_INSECURE_HTTP OFF CACHE BOOL "" FORCE)
set(MYBOT_AUDIO_PTIME_MS 20 CACHE STRING "" FORCE)
set(AGORA_SDK_DIR "$ENV{AGORA_SDK_DIR}" CACHE PATH "Target RTSA SDK")
set(AGORA_RTC_LIBRARY "$ENV{AGORA_RTC_LIBRARY}" CACHE FILEPATH "AArch64 RTSA shared library")
if(NOT EXISTS "${AGORA_RTC_LIBRARY}")
    message(FATAL_ERROR "Set AGORA_SDK_DIR and AGORA_RTC_LIBRARY to the matching target package")
endif()
file(READ "${AGORA_RTC_LIBRARY}" RV1126B_RTSA_HEADER LIMIT 20 HEX)
if(NOT RV1126B_RTSA_HEADER MATCHES "^7f454c460201" OR
   NOT RV1126B_RTSA_HEADER MATCHES "0300b700$")
    message(FATAL_ERROR "The RTSA library must be an ELF64 little-endian AArch64 shared object")
endif()

find_package(OpenSSL REQUIRED)
find_path(RV1126B_ALSA_INCLUDE alsa/asoundlib.h REQUIRED)
find_library(RV1126B_ALSA_LIBRARY asound REQUIRED)
find_path(RV1126B_DRM_INCLUDE xf86drm.h REQUIRED)
find_path(RV1126B_DRM_UAPI drm.h PATH_SUFFIXES libdrm REQUIRED)
find_library(RV1126B_DRM_LIBRARY drm REQUIRED)
add_subdirectory(third_party/mybot EXCLUDE_FROM_ALL)

add_library(rv1126b_platform STATIC platforms/rv1126b/rv1126b_platform.c
    platforms/rv1126b/rv1126b_audio.c platforms/rv1126b/rv1126b_network.c
    platforms/rv1126b/rv1126b_key.c platforms/rv1126b/input_logic.c
    third_party/mybot/platforms/linux/linux_kv_store_file.c
    third_party/mybot/platforms/linux/linux_https_openssl.c
    third_party/mybot/platforms/linux/linux_announce_file.c
    third_party/mybot/platforms/linux/linux_lcd_console.c
    ui/runtime.c ui/drm.c)
target_include_directories(rv1126b_platform PUBLIC platforms/rv1126b ui/include
    PRIVATE ui third_party/mybot/platforms/linux "${RV1126B_ALSA_INCLUDE}"
    "${RV1126B_DRM_INCLUDE}" "${RV1126B_DRM_UAPI}")
target_compile_options(rv1126b_platform PRIVATE -Wall -Wextra -Werror)
target_link_libraries(rv1126b_platform PUBLIC mybot::sdk rv1126b_view
    PRIVATE "${RV1126B_ALSA_LIBRARY}" "${RV1126B_DRM_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
if(MYBOT_ENABLE_VIDEO)
    find_path(RV1126B_MPP_INCLUDE rk_mpi.h PATH_SUFFIXES rockchip REQUIRED)
    find_library(RV1126B_MPP_LIBRARY rockchip_mpp REQUIRED)
    target_sources(rv1126b_platform PRIVATE platforms/rv1126b/rv1126b_video.c)
    target_include_directories(rv1126b_platform PRIVATE "${RV1126B_MPP_INCLUDE}")
    target_link_libraries(rv1126b_platform PRIVATE "${RV1126B_MPP_LIBRARY}")
endif()
add_executable(mybot-rv1126b app/main.c app/device_identity.c)
target_compile_options(mybot-rv1126b PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(mybot-rv1126b PRIVATE RV1126B_VERSION="${PROJECT_VERSION}"
    RV1126B_LOCALE="${RV1126B_LANGUAGE}")
target_link_libraries(mybot-rv1126b PRIVATE rv1126b_platform OpenSSL::Crypto)
set_target_properties(mybot-rv1126b PROPERTIES INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_WITH_INSTALL_RPATH TRUE)
install(TARGETS mybot-rv1126b RUNTIME DESTINATION bin)
install(FILES "${AGORA_RTC_LIBRARY}" DESTINATION lib)
install(DIRECTORY third_party/mybot/assets/locales DESTINATION assets)
