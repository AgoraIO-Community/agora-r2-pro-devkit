cmake_minimum_required(VERSION 3.16)
project(botstation_device VERSION 0.1.0 LANGUAGES C)

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
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/agora-rtsa/agora_rtsa_sdk/agora_sdk"
    CACHE PATH "Target Agora RTSA package, including build metadata")
set(AGORA_RTC_LIBRARY "${AGORA_SDK_DIR}/lib/aarch64/libagora-rtc-sdk.so"
    CACHE FILEPATH "Target libagora-rtc-sdk.so")
set(MYBOT_AUDIO_PTIME_MS "20" CACHE STRING "Must match selected RTSA timer metadata")
if(NOT EXISTS "${AGORA_SDK_DIR}/include/agora_rtc_api.h" OR
   NOT EXISTS "${AGORA_RTC_LIBRARY}")
    message(FATAL_ERROR
        "Supply an authorized Agora RTSA package for the target ABI using "
        "AGORA_SDK_DIR and AGORA_RTC_LIBRARY; no vendor package is bundled")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mybot/third_party/aosl/CMakeLists.txt")
    message(FATAL_ERROR "Missing pinned AOSL source; initialize the SDK dependency")
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
add_subdirectory(third_party/mybot EXCLUDE_FROM_ALL)

set(MYBOT_LINUX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mybot/platforms/linux")
add_library(botstation_platform STATIC
    platform/rv1126b_platform.c
    platform/rv1126b_audio.c
    platform/rv1126b_network.c
    platform/rv1126b_key.c
    platform/rv1126b_keymap.c
    platform/rv1126b_lvgl_ui.c
    "${MYBOT_LINUX_DIR}/linux_kv_store_file.c"
    "${MYBOT_LINUX_DIR}/linux_https_openssl.c"
    "${MYBOT_LINUX_DIR}/linux_announce_file.c"
    "${MYBOT_LINUX_DIR}/linux_lcd_console.c"
)
target_include_directories(botstation_platform
    PUBLIC platform
    PRIVATE "${MYBOT_LINUX_DIR}" "${ALSA_INCLUDE_DIR}"
)
target_compile_definitions(botstation_platform PRIVATE MYBOT_LINUX_HTTPS_OPENSSL=1)
if(MYBOT_ENABLE_VIDEO)
    find_path(MPP_INCLUDE_DIR rk_mpi.h PATH_SUFFIXES rockchip)
    find_library(MPP_LIBRARY rockchip_mpp)
    if(NOT MPP_INCLUDE_DIR OR NOT MPP_LIBRARY)
        message(FATAL_ERROR "Video requires Rockchip MPP development headers/library")
    endif()
    target_sources(botstation_platform PRIVATE platform/rv1126b_video.c)
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

add_executable(botstation-device main.c device_config.c)
target_compile_options(botstation-device PRIVATE -Wall -Wextra)
target_compile_definitions(botstation-device PRIVATE BOTSTATION_VERSION="${PROJECT_VERSION}")
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
install(TARGETS botstation-device RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY third_party/mybot/assets/locales
    DESTINATION "${CMAKE_INSTALL_DATADIR}/botstation")

option(BOTSTATION_BUILD_TESTS "Build adapter tests on Linux" OFF)
if(BOTSTATION_BUILD_TESTS)
    enable_testing()
    if(MYBOT_ENABLE_VIDEO)
        add_executable(botstation-video-test tests/test_video.c)
        target_include_directories(botstation-video-test PRIVATE "${MPP_INCLUDE_DIR}")
        target_compile_options(botstation-video-test PRIVATE -UNDEBUG)
        target_link_libraries(botstation-video-test PRIVATE mybot::sdk Threads::Threads "${MPP_LIBRARY}")
        add_test(NAME botstation-video COMMAND botstation-video-test)
        set_tests_properties(botstation-video PROPERTIES TIMEOUT 15)
    endif()
    add_executable(botstation-config-test tests/test_config.c device_config.c)
    target_include_directories(botstation-config-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME botstation-config COMMAND botstation-config-test)
    add_executable(botstation-platform-test tests/test_platform.c)
    target_include_directories(botstation-platform-test PRIVATE "${ALSA_INCLUDE_DIR}" "${MYBOT_LINUX_DIR}")
    target_link_libraries(botstation-platform-test PRIVATE botstation_platform)
    add_test(NAME botstation-platform COMMAND botstation-platform-test)
    add_executable(botstation-auto-start-test tests/test_auto_start.c)
    target_link_libraries(botstation-auto-start-test PRIVATE botstation_platform)
    target_link_options(botstation-auto-start-test PRIVATE
        -Wl,--wrap=mybot_get_state -Wl,--wrap=rv1126b_platform_config)
    add_test(NAME botstation-auto-start COMMAND botstation-auto-start-test)
    add_executable(botstation-audio-io-test tests/test_audio_io.c)
    target_include_directories(botstation-audio-io-test PRIVATE "${ALSA_INCLUDE_DIR}")
    target_link_libraries(botstation-audio-io-test PRIVATE botstation_platform)
    target_link_options(botstation-audio-io-test PRIVATE
        -Wl,--wrap=snd_pcm_readi -Wl,--wrap=snd_pcm_writei)
    add_test(NAME botstation-audio-io COMMAND botstation-audio-io-test)
    add_executable(botstation-keymap-test tests/test_keymap.c platform/rv1126b_keymap.c)
    target_include_directories(botstation-keymap-test PRIVATE platform third_party/mybot/include)
    add_test(NAME botstation-keymap COMMAND botstation-keymap-test)
    add_test(NAME botstation-help COMMAND botstation-device --help)
    add_test(NAME botstation-invalid-args COMMAND botstation-device --unknown)
    set_tests_properties(botstation-invalid-args PROPERTIES WILL_FAIL TRUE)
endif()
