# SPDX-License-Identifier: Apache-2.0
if(MYBOT_ENABLE_VIDEO)
    set(RV1126B_VIDEO_JSON true)
    set(RV1126B_VIDEO_NUMBER 1)
else()
    set(RV1126B_VIDEO_JSON false)
    set(RV1126B_VIDEO_NUMBER 0)
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" build_type_upper)
set(RV1126B_EFFECTIVE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${build_type_upper}}")
set(RV1126B_EFFECTIVE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${build_type_upper}}")
set(RV1126B_EFFECTIVE_LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${CMAKE_EXE_LINKER_FLAGS_${build_type_upper}}")
foreach(value RV1126B_EFFECTIVE_C_FLAGS RV1126B_EFFECTIVE_CXX_FLAGS RV1126B_EFFECTIVE_LINK_FLAGS)
    string(REPLACE "\\" "\\\\" ${value} "${${value}}")
    string(REPLACE "\"" "\\\"" ${value} "${${value}}")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/cmake/build-config.json.in" "${PROJECT_BINARY_DIR}/build-config.json" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/cmake/build_identity.c.in" "${CMAKE_CURRENT_BINARY_DIR}/build_identity.c" @ONLY)
configure_file("${PROJECT_SOURCE_DIR}/cmake/build_identity.h.in" "${CMAKE_CURRENT_BINARY_DIR}/build_identity.h" @ONLY)
add_executable(mybot-rv1126b main.c config.c runtime_guard.c device_identity.c
    "${CMAKE_CURRENT_BINARY_DIR}/build_identity.c" "${PROJECT_BINARY_DIR}/build_source_identity.c")
set_source_files_properties("${PROJECT_BINARY_DIR}/build_source_identity.c" PROPERTIES GENERATED TRUE)
target_include_directories(mybot-rv1126b PRIVATE "${RV1126B_BOARD_INCLUDE}" "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(mybot-rv1126b PRIVATE RV1126B_VERSION="${PROJECT_VERSION}" RV1126B_LOCALE="${RV1126B_LANGUAGE}")
target_compile_options(mybot-rv1126b PRIVATE -Wall -Wextra -Werror)
target_link_libraries(mybot-rv1126b PRIVATE mybot_platform OpenSSL::Crypto)
set_target_properties(mybot-rv1126b PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
    INSTALL_RPATH "$ORIGIN/../lib" BUILD_WITH_INSTALL_RPATH TRUE)
add_custom_target(mybot-build-inputs
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-manifest.py"
        --build-dir "${PROJECT_BINARY_DIR}" --prepare
    BYPRODUCTS "${PROJECT_BINARY_DIR}/build_source_identity.c" VERBATIM)
add_dependencies(mybot-rv1126b mybot-build-inputs)
get_filename_component(rtsa_library_directory "${AGORA_RTC_LIBRARY}" DIRECTORY)
add_custom_target(mybot-artifacts ALL
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check-target-elf.py"
        "$<TARGET_FILE:mybot-rv1126b>" --library-root "${RV1126B_STAGING}"
        --library-root "${rtsa_library_directory}"
        --output "${PROJECT_BINARY_DIR}/abi-report.json"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-manifest.py" --build-dir "${PROJECT_BINARY_DIR}"
    DEPENDS mybot-rv1126b VERBATIM)
install(TARGETS mybot-rv1126b RUNTIME DESTINATION bin)
install(FILES "${AGORA_RTC_LIBRARY}" DESTINATION lib)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/mybot/assets/locales" DESTINATION assets)
