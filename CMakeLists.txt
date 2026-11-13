cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BOTSTATION_VERSION LIMIT_COUNT 1)
if(NOT BOTSTATION_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([-+][0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "VERSION must contain one semantic version")
endif()

project(botstation_device VERSION "${BOTSTATION_VERSION}" LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "botstation-device requires a Linux target/toolchain")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CONFIG_PLATFORM linux CACHE STRING "" FORCE)
set(MYBOT_BUILD_LINUX_PLATFORM OFF CACHE BOOL "" FORCE)
set(MYBOT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MYBOT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MYBOT_WAKE_WORDS OFF CACHE BOOL "" FORCE)
set(MYBOT_CLOUD_AEC ON CACHE BOOL "RV1126B has no device-side AEC" FORCE)
option(MYBOT_ENABLE_VIDEO "Enable MIPI camera encoded video uplink" ON)
set(MYBOT_ENABLE_HTTPS ON CACHE BOOL "" FORCE)
set(MYBOT_ALLOW_INSECURE_HTTP OFF CACHE BOOL "" FORCE)
set(MYBOT_ENABLE_ASAN OFF CACHE BOOL "" FORCE)
set(MYBOT_ENABLE_UBSAN OFF CACHE BOOL "" FORCE)

set(AGORA_SDK_DIR
    "${PROJECT_SOURCE_DIR}/third_party/agora-rtsa/agora_rtsa_sdk/agora_sdk"
    CACHE PATH "Target Agora RTSA package, including build metadata")
set(AGORA_RTC_LIBRARY "${AGORA_SDK_DIR}/lib/aarch64/libagora-rtc-sdk.so"
    CACHE FILEPATH "Target libagora-rtc-sdk.so")
set(MYBOT_AUDIO_PTIME_MS "20" CACHE STRING "Must match selected RTSA timer metadata")
set(BOTSTATION_RTSA_AUDIO_CODEC "G722" CACHE STRING
    "Audio codec required by the linked mybot SDK")
if(NOT BOTSTATION_RTSA_AUDIO_CODEC STREQUAL "G722")
    message(FATAL_ERROR
        "The pinned mybot SDK currently selects G722; update the SDK integration "
        "together with BOTSTATION_RTSA_AUDIO_CODEC")
endif()
if(NOT EXISTS "${AGORA_SDK_DIR}/include/agora_rtc_api.h" OR
   NOT EXISTS "${AGORA_RTC_LIBRARY}")
    message(FATAL_ERROR
        "Supply an authorized Agora RTSA package for the target ABI using "
        "AGORA_SDK_DIR and AGORA_RTC_LIBRARY; no vendor package is bundled")
endif()
set(BOTSTATION_MYBOT_SOURCE_DIR
    "${PROJECT_SOURCE_DIR}/third_party/agent-studio-device-firmware")
if(NOT EXISTS "${BOTSTATION_MYBOT_SOURCE_DIR}/third_party/aosl/CMakeLists.txt")
    message(FATAL_ERROR "Missing pinned AOSL source in the agent-studio SDK snapshot")
endif()

file(READ "${AGORA_RTC_LIBRARY}" RTSA_ELF_HEADER LIMIT 20 HEX)
string(LENGTH "${RTSA_ELF_HEADER}" RTSA_HEADER_LENGTH)
if(RTSA_HEADER_LENGTH LESS 40 OR NOT RTSA_ELF_HEADER MATCHES "^7f454c460201")
    message(FATAL_ERROR "RTSA must be a little-endian ELF64 shared library")
endif()
string(SUBSTRING "${RTSA_ELF_HEADER}" 32 4 RTSA_ELF_TYPE)
string(SUBSTRING "${RTSA_ELF_HEADER}" 36 4 RTSA_ELF_MACHINE)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" BOTSTATION_PROCESSOR)
if(BOTSTATION_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(RTSA_EXPECTED_MACHINE "b700")
elseif(BOTSTATION_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(RTSA_EXPECTED_MACHINE "3e00")
else()
    message(FATAL_ERROR "Only AArch64 boards and x86_64 Linux host tests are supported")
endif()
if(NOT RTSA_ELF_TYPE STREQUAL "0300" OR
   NOT RTSA_ELF_MACHINE STREQUAL RTSA_EXPECTED_MACHINE)
    message(FATAL_ERROR "RTSA shared library architecture does not match ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(BOTSTATION_CODEC_METADATA_FOUND FALSE)
set(BOTSTATION_G722_CONFIG FALSE)
foreach(BOTSTATION_RTSA_CONFIG
        "${AGORA_SDK_DIR}/.config"
        "${AGORA_SDK_DIR}/include/global_config.cmake")
    if(EXISTS "${BOTSTATION_RTSA_CONFIG}")
        file(STRINGS "${BOTSTATION_RTSA_CONFIG}" BOTSTATION_CODEC_LINES
             REGEX "CONFIG_AUDIO_CODEC(_OPUS|_G711|_G722)?")
        if(BOTSTATION_CODEC_LINES)
            set(BOTSTATION_CODEC_METADATA_FOUND TRUE)
        endif()
        foreach(BOTSTATION_CODEC_LINE IN LISTS BOTSTATION_CODEC_LINES)
            if(BOTSTATION_CODEC_LINE MATCHES
               "CONFIG_AUDIO_CODEC_G722[^A-Za-z0-9]*(y|Y|on|ON|true|TRUE)")
                set(BOTSTATION_G722_CONFIG TRUE)
            endif()
        endforeach()
    endif()
endforeach()
if(BOTSTATION_CODEC_METADATA_FOUND AND NOT BOTSTATION_G722_CONFIG)
    message(FATAL_ERROR
        "The RTSA package does not enable G722, but the pinned mybot SDK requests "
        "G722. Supply a matching package or update both the SDK codec integration "
        "and BOTSTATION_RTSA_AUDIO_CODEC")
elseif(NOT BOTSTATION_CODEC_METADATA_FOUND)
    message(WARNING
        "RTSA codec metadata is unavailable; G722 support cannot be verified")
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h)
find_library(ALSA_LIBRARY asound)
if(NOT ALSA_INCLUDE_DIR OR NOT ALSA_LIBRARY)
    message(FATAL_ERROR "ALSA development headers/library are required in the target sysroot")
endif()
option(BOTSTATION_ENABLE_LVGL_UI "Build the LVGL DRM workflow display" OFF)
if(BOTSTATION_ENABLE_LVGL_UI)
    find_path(LVGL_INCLUDE_DIR NAMES lvgl/lvgl.h)
    find_path(LVGL_DRIVERS_INCLUDE_DIR NAMES lv_drivers/display/drm.h
        HINTS "${CMAKE_SYSROOT}/usr/include/lvgl")
    find_library(LVGL_LIBRARY NAMES lvgl)
    find_library(LVGL_DRIVERS_LIBRARY NAMES lv_drivers)
    find_library(DRM_LIBRARY NAMES drm)
    if(NOT LVGL_INCLUDE_DIR OR NOT LVGL_DRIVERS_INCLUDE_DIR OR NOT LVGL_LIBRARY OR
       NOT LVGL_DRIVERS_LIBRARY OR NOT DRM_LIBRARY)
        message(FATAL_ERROR "LVGL v8, lv_drivers DRM and libdrm are required for the workflow UI")
    endif()
endif()
add_subdirectory("${BOTSTATION_MYBOT_SOURCE_DIR}"
    "${CMAKE_BINARY_DIR}/agent-studio-device-firmware" EXCLUDE_FROM_ALL)

set(MYBOT_LINUX_DIR "${BOTSTATION_MYBOT_SOURCE_DIR}/platforms/linux")
set(RV1126B_PLATFORM_DIR "${PROJECT_SOURCE_DIR}/src/platform/rv1126b")
add_library(botstation_platform STATIC
    src/platform/rv1126b/rv1126b_platform.c
    src/platform/rv1126b/rv1126b_audio.c
    src/platform/rv1126b/rv1126b_network.c
    src/platform/rv1126b/rv1126b_key.c
    src/platform/rv1126b/rv1126b_keymap.c
    src/platform/rv1126b/rv1126b_lvgl_ui.c
    "${MYBOT_LINUX_DIR}/linux_kv_store_file.c"
    "${MYBOT_LINUX_DIR}/linux_https_openssl.c"
    "${MYBOT_LINUX_DIR}/linux_announce_file.c"
    "${MYBOT_LINUX_DIR}/linux_lcd_console.c"
)
target_include_directories(botstation_platform
    PUBLIC "${RV1126B_PLATFORM_DIR}"
    PRIVATE "${MYBOT_LINUX_DIR}" "${ALSA_INCLUDE_DIR}"
)
target_compile_definitions(botstation_platform PRIVATE MYBOT_LINUX_HTTPS_OPENSSL=1)
if(MYBOT_ENABLE_VIDEO)
    find_path(MPP_INCLUDE_DIR rk_mpi.h PATH_SUFFIXES rockchip)
    find_library(MPP_LIBRARY rockchip_mpp)
    if(NOT MPP_INCLUDE_DIR OR NOT MPP_LIBRARY)
        message(FATAL_ERROR "Video requires Rockchip MPP development headers/library")
    endif()
    target_sources(botstation_platform PRIVATE src/platform/rv1126b/rv1126b_video.c)
    target_include_directories(botstation_platform PRIVATE "${MPP_INCLUDE_DIR}")
    target_link_libraries(botstation_platform PRIVATE "${MPP_LIBRARY}")
endif()
if(BOTSTATION_ENABLE_LVGL_UI)
    target_include_directories(botstation_platform PRIVATE
        "${LVGL_INCLUDE_DIR}" "${LVGL_DRIVERS_INCLUDE_DIR}"
        "${LVGL_DRIVERS_INCLUDE_DIR}/lv_drivers")
    target_compile_definitions(botstation_platform PRIVATE
        BOTSTATION_ENABLE_LVGL_UI=1 LV_CONF_INCLUDE_SIMPLE=1 USE_DRM=1)
    target_link_libraries(botstation_platform PRIVATE
        "${LVGL_LIBRARY}" "${LVGL_DRIVERS_LIBRARY}" "${DRM_LIBRARY}" m)
endif()
target_compile_options(botstation_platform PRIVATE -Wall -Wextra)
target_link_libraries(botstation_platform PUBLIC mybot::sdk
    PRIVATE "${ALSA_LIBRARY}" OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

set(BOTSTATION_GIT_REVISION "unknown")
if(EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git -C "${PROJECT_SOURCE_DIR}" rev-parse --short=12 HEAD
        RESULT_VARIABLE BOTSTATION_GIT_RESULT
        OUTPUT_VARIABLE BOTSTATION_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT BOTSTATION_GIT_RESULT EQUAL 0)
        set(BOTSTATION_GIT_REVISION "unknown")
    endif()
endif()
configure_file(
    "${PROJECT_SOURCE_DIR}/src/botstation-device/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY)

add_executable(botstation-device
    src/botstation-device/main.c
    src/botstation-device/device_config.c)
target_include_directories(botstation-device PRIVATE
    "${PROJECT_SOURCE_DIR}/src/botstation-device"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_options(botstation-device PRIVATE -Wall -Wextra)
string(TIMESTAMP BOTSTATION_BUILD_EPOCH "%s" UTC)
math(EXPR BOTSTATION_MIN_WALL_TIME "${BOTSTATION_BUILD_EPOCH} / 86400 * 86400")
# Reproducible SDK builds may supply a SOURCE_DATE_EPOCH predating this integration.
if(BOTSTATION_MIN_WALL_TIME LESS 1767225600)
    set(BOTSTATION_MIN_WALL_TIME 1767225600) # 2026-01-01 UTC
endif()
target_compile_definitions(botstation-device PRIVATE
    BOTSTATION_MIN_WALL_TIME=${BOTSTATION_MIN_WALL_TIME})
target_link_libraries(botstation-device PRIVATE botstation_platform)
set_target_properties(botstation-device PROPERTIES INSTALL_RPATH "/usr/lib")

include(GNUInstallDirs)
set(BOTSTATION_INSTALL_SYSCONFDIR "/etc" CACHE PATH "Product system configuration directory")
configure_file(
    "${PROJECT_SOURCE_DIR}/boards/rv1126b-r2pro/config/botstation-device.conf.in"
    "${CMAKE_BINARY_DIR}/etc/botstation-device.conf"
    @ONLY)
install(TARGETS botstation-device RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${AGORA_RTC_LIBRARY}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "${BOTSTATION_MYBOT_SOURCE_DIR}/assets/locales"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/botstation")
install(FILES "${CMAKE_BINARY_DIR}/etc/botstation-device.conf"
    DESTINATION "${BOTSTATION_INSTALL_SYSCONFDIR}")
install(FILES boards/rv1126b-r2pro/config/90-botstation.conf
    DESTINATION "${BOTSTATION_INSTALL_SYSCONFDIR}/alsa/conf.d")
install(PROGRAMS boards/rv1126b-r2pro/config/S99botstation-device
    DESTINATION "${BOTSTATION_INSTALL_SYSCONFDIR}/init.d")
install(FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/botstation-device")

option(BOTSTATION_BUILD_TESTS "Build adapter tests on Linux" OFF)
if(BOTSTATION_BUILD_TESTS)
    enable_testing()
    if(MYBOT_ENABLE_VIDEO)
        add_executable(botstation-video-test tests/platform/rv1126b/test_video.c)
        target_include_directories(botstation-video-test PRIVATE
            "${RV1126B_PLATFORM_DIR}" "${MPP_INCLUDE_DIR}")
        target_compile_options(botstation-video-test PRIVATE -UNDEBUG)
        target_link_libraries(botstation-video-test PRIVATE
            mybot::sdk Threads::Threads "${MPP_LIBRARY}")
        add_test(NAME botstation-video COMMAND botstation-video-test)
        set_tests_properties(botstation-video PROPERTIES TIMEOUT 15)
    endif()
    add_executable(botstation-config-test
        tests/unit/test_config.c src/botstation-device/device_config.c)
    target_include_directories(botstation-config-test PRIVATE
        "${PROJECT_SOURCE_DIR}/src/botstation-device")
    add_test(NAME botstation-config COMMAND botstation-config-test)
    add_executable(botstation-platform-test tests/platform/rv1126b/test_platform.c)
    target_include_directories(botstation-platform-test PRIVATE
        "${MYBOT_LINUX_DIR}" "${ALSA_INCLUDE_DIR}")
    target_link_libraries(botstation-platform-test PRIVATE botstation_platform)
    add_test(NAME botstation-platform COMMAND botstation-platform-test)
    add_executable(botstation-auto-start-test tests/platform/rv1126b/test_auto_start.c)
    target_link_libraries(botstation-auto-start-test PRIVATE botstation_platform)
    target_link_options(botstation-auto-start-test PRIVATE
        -Wl,--wrap=mybot_get_state -Wl,--wrap=rv1126b_platform_config)
    add_test(NAME botstation-auto-start COMMAND botstation-auto-start-test)
    add_executable(botstation-audio-io-test tests/platform/rv1126b/test_audio_io.c)
    target_include_directories(botstation-audio-io-test PRIVATE "${ALSA_INCLUDE_DIR}")
    target_link_libraries(botstation-audio-io-test PRIVATE botstation_platform)
    target_link_options(botstation-audio-io-test PRIVATE
        -Wl,--wrap=snd_pcm_readi -Wl,--wrap=snd_pcm_writei)
    add_test(NAME botstation-audio-io COMMAND botstation-audio-io-test)
    add_executable(botstation-keymap-test
        tests/unit/test_keymap.c src/platform/rv1126b/rv1126b_keymap.c)
    target_include_directories(botstation-keymap-test PRIVATE
        "${RV1126B_PLATFORM_DIR}" "${BOTSTATION_MYBOT_SOURCE_DIR}/include")
    add_test(NAME botstation-keymap COMMAND botstation-keymap-test)
    add_test(NAME botstation-help COMMAND botstation-device --help)
    add_test(NAME botstation-version COMMAND botstation-device --version)
    add_test(NAME botstation-invalid-args COMMAND botstation-device --unknown)
    set_tests_properties(botstation-invalid-args PROPERTIES WILL_FAIL TRUE)
endif()
