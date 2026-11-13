cmake_minimum_required(VERSION 2.4...3.5)
project(hello_rdt)

include_directories(${UTILITY} ${THIRD_PARTY})
aux_source_directory(${UTILITY} COMMON_FILES)

add_executable(hello_rdt hello_rdt.c ${COMMON_FILES})
target_link_libraries(hello_rdt agora-rtc-sdk ${LIBS})

add_executable(hello_rdt_multi hello_rdt_multi.c ${COMMON_FILES})
target_link_libraries(hello_rdt_multi agora-rtc-sdk ${LIBS})
