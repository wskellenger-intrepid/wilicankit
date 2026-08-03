# Standalone host-test tree: no Pico SDK, no pico_sdk_init(). Pure-logic
# modules from the repo root are compiled directly against the host C
# compiler so tests run natively on Windows/Linux with no hardware.
cmake_minimum_required(VERSION 3.13)
project(wilicankit_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

add_executable(test_can_pack
    test_can_pack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../can_model.c)
target_include_directories(test_can_pack PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR})
# can_model.c's platform/diag.h (RTT logging) needs the bsp submodule; skip it on host.
target_compile_definitions(test_can_pack PRIVATE HOST_TEST)
add_test(NAME can_pack COMMAND test_can_pack)

# storage_json.c depends on cJSON; fetched here too since this is a separate
# standalone CMake project from the root (see tools/test.ps1).
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
)
FetchContent_MakeAvailable(cjson)

add_executable(test_storage_json
    test_storage_json.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../storage_json.c)
target_include_directories(test_storage_json PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${cjson_SOURCE_DIR})
target_link_libraries(test_storage_json cjson)
add_test(NAME storage_json COMMAND test_storage_json)
