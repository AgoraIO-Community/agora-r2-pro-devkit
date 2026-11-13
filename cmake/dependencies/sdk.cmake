# SPDX-License-Identifier: Apache-2.0
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(FATAL_ERROR "Use the AArch64 toolchain, or RV1126B_HOST_ONLY=ON")
endif()
set(CONFIG_PLATFORM linux CACHE STRING "" FORCE)
foreach(feature MYBOT_BUILD_LINUX_PLATFORM MYBOT_BUILD_EXAMPLES MYBOT_BUILD_TESTS MYBOT_WAKE_WORDS MYBOT_ALLOW_INSECURE_HTTP)
    set(${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(MYBOT_CLOUD_AEC ON CACHE BOOL "" FORCE)
set(MYBOT_ENABLE_HTTPS ON CACHE BOOL "" FORCE)
set(MYBOT_AUDIO_PTIME_MS 20 CACHE STRING "" FORCE)
set(AGORA_SDK_DIR "${PROJECT_SOURCE_DIR}/third_party/rtsa/agora_sdk"
    CACHE PATH "Pinned RTSA SDK snapshot" FORCE)
set(AGORA_RTC_LIBRARY "${AGORA_SDK_DIR}/lib/aarch64/libagora-rtc-sdk.so"
    CACHE FILEPATH "Pinned AArch64 RTSA library" FORCE)
set(RV1126B_RUNTIME_MANIFEST "$ENV{RV1126B_RUNTIME_MANIFEST}" CACHE FILEPATH "Target runtime identity")
if(NOT EXISTS "${AGORA_RTC_LIBRARY}")
    message(FATAL_ERROR "Pinned RTSA SDK snapshot is missing from third_party/rtsa")
endif()
file(READ "${AGORA_RTC_LIBRARY}" rtsa_header LIMIT 20 HEX)
if(NOT rtsa_header MATCHES "^7f454c460201" OR NOT rtsa_header MATCHES "0300b700$")
    message(FATAL_ERROR "RTSA must be an ELF64 little-endian AArch64 shared object")
endif()
find_package(OpenSSL REQUIRED)
find_path(RV1126B_ALSA_INCLUDE alsa/asoundlib.h REQUIRED)
find_library(RV1126B_ALSA_LIBRARY asound REQUIRED)
find_path(RV1126B_DRM_INCLUDE xf86drm.h REQUIRED)
find_path(RV1126B_DRM_UAPI drm.h PATH_SUFFIXES libdrm REQUIRED)
find_library(RV1126B_DRM_LIBRARY drm REQUIRED)
set(AOSL_DIR "${PROJECT_SOURCE_DIR}/third_party/aosl"
    CACHE PATH "Pinned AOSL source snapshot" FORCE)
if(NOT EXISTS "${AOSL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Pinned AOSL source snapshot is missing")
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/third_party/mybot" "${PROJECT_BINARY_DIR}/third_party/mybot" EXCLUDE_FROM_ALL)

# Upstream Linux reference services stay in the immutable SDK snapshot.
add_library(mybot_linux_services STATIC
    "${PROJECT_SOURCE_DIR}/third_party/mybot/platforms/linux/linux_kv_store_file.c"
    "${PROJECT_SOURCE_DIR}/third_party/mybot/platforms/linux/linux_https_openssl.c"
    "${PROJECT_SOURCE_DIR}/third_party/mybot/platforms/linux/linux_announce_file.c"
    "${PROJECT_SOURCE_DIR}/third_party/mybot/platforms/linux/linux_lcd_console.c")
target_include_directories(mybot_linux_services PUBLIC "${PROJECT_SOURCE_DIR}/third_party/mybot/platforms/linux")
target_link_libraries(mybot_linux_services PRIVATE mybot::sdk OpenSSL::SSL OpenSSL::Crypto)
