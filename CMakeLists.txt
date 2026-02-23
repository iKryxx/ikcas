cmake_minimum_required(VERSION 3.31)
project(ikcas VERSION 0.1.2 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(IKCAS_VERSION="${PROJECT_VERSION}")
add_compile_definitions(IKCAS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR})
add_compile_definitions(IKCAS_VERSION_MINOR=${PROJECT_VERSION_MINOR})
add_compile_definitions(IKCAS_VERSION_PATCH=${PROJECT_VERSION_PATCH})


find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED notcurses)

file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h")

add_executable(ikcas ${SOURCES} ${HEADERS})
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/inc"
        "${CMAKE_CURRENT_BINARY_DIR}"
        "${NOTCURSES_INCLUDE_DIRS}"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic -Werror     
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    _POSIX_C_SOURCE=200809L
    _XOPEN_SOURCE=700
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${NOTCURSES_LIBRARIES}
    m
)

