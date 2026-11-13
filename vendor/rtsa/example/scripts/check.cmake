string(TOUPPER "${CMAKE_BUILD_TYPE}" _toolchain_check_build_type)
set(_toolchain_check_link_libraries "agora-rtc-sdk")

set(_toolchain_check_cmake_flags
    "-DLINK_LIBRARIES=${_toolchain_check_link_libraries}"
    "-DLINK_DIRECTORIES=${CMAKE_SOURCE_DIR}/../agora_sdk/lib/${MACHINE}"
)

if(CMAKE_BUILD_TYPE)
  list(APPEND _toolchain_check_cmake_flags
      "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif()

if(_toolchain_check_build_type STREQUAL "ASAN")
  set(_toolchain_check_exe_linker_flags
      "${CMAKE_EXE_LINKER_FLAGS_ASAN} -fsanitize=address")
  set(_toolchain_check_shared_linker_flags
      "${CMAKE_SHARED_LINKER_FLAGS_ASAN} -fsanitize=address")
  if(ANDROID)
    string(APPEND _toolchain_check_exe_linker_flags " -llog")
    string(APPEND _toolchain_check_shared_linker_flags " -llog")
  endif()

  # try_compile runs before env.cmake, so pass ASAN flags explicitly to the
  # temporary sub-build that links against the already-instrumented SDK.
  list(APPEND _toolchain_check_cmake_flags
      "-DCMAKE_C_FLAGS_ASAN=${CMAKE_C_FLAGS_ASAN} -fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
      "-DCMAKE_CXX_FLAGS_ASAN=${CMAKE_CXX_FLAGS_ASAN} -fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
      "-DCMAKE_EXE_LINKER_FLAGS_ASAN=${_toolchain_check_exe_linker_flags}"
      "-DCMAKE_SHARED_LINKER_FLAGS_ASAN=${_toolchain_check_shared_linker_flags}")
endif()

try_compile(TOOLCHAIN_CHECK /tmp/$ENV{USER}-CMakeTest ${CMAKE_SOURCE_DIR}/scripts/.test.c
        CMAKE_FLAGS ${_toolchain_check_cmake_flags}
        OUTPUT_VARIABLE OUTPUT_CHECK
    )
if(NOT TOOLCHAIN_CHECK)
  message("${OUTPUT_CHECK}")
  message(FATAL_ERROR "\nTOOLCHAIN ERROR! Modify the toolchain script for your cross compiler:\n${CMAKE_TOOLCHAIN_FILE}\n")
endif()
