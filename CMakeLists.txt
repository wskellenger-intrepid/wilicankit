# wilicankit — general-purpose, user-configurable CAN device driver/GUI app.
# LVGL and cJSON are fetched from source (this repo's first FetchContent-based
# deps; all other deps are harvested source — see AGENTS.md). LVGL's
# demos/examples and internal ThorVG are disabled: we only need the
# widget/core library. cJSON is used by storage_json.c (config save/load) —
# kept to a static lib with its own test/utils/strict-compiler-flags targets
# disabled, since those target host GCC/Clang, not arm-none-eabi.
include(FetchContent)

set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.3.0
)
FetchContent_MakeAvailable(lvgl)
# LVGL's own FatFs driver (src/libs/fsdrv/lv_fs_fatfs.c, LV_USE_FS_FATFS in
# lv_conf.h) #includes "ff.h" directly. lvgl is a standalone FetchContent
# target with no dependency on freewili2_bsp (which is the only other target
# that normally exposes this dir), so it needs its own include path.
target_include_directories(lvgl PRIVATE ${CMAKE_SOURCE_DIR}/bsp/third_party/fatfs)

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

add_executable(wilicankit
    main.c
    lvgl_port.c
    lv_psram_pool.c
    ui_icons.c
    can_model.c
    can_link.c
    app_state.c
    ui_common.c
    ui_signals.c
    ui_messages.c
    ui_transmit.c
    ui_controls.c
    ui_shell.c
    storage.c
    storage_json.c
    ui_config.c
)
target_include_directories(wilicankit PRIVATE ${cjson_SOURCE_DIR})
target_link_libraries(wilicankit freewili2_bsp onewili_fwgui lvgl cjson)
# Default PICO_STACK_SIZE is 0x800 (2 KB, see toggleled) — tight for an LVGL
# GUI with forms/dropdowns/snprintf-heavy code. crt0.S is an INTERFACE source
# (compiled per-target, not a prebuilt lib), so this define only affects this
# app. The core0 stack lives in a small dedicated SCRATCH_Y bank (NOT the
# main 512 KB SRAM used for .data/.bss) that is only 4 KB total — 0x1000 is
# the max this app can request without a custom linker script; UI code
# should still avoid large stack-local buffers (prefer `static`) as a rule.
target_compile_definitions(wilicankit PRIVATE PICO_STACK_SIZE=0x1000)
pico_set_binary_type(wilicankit copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(wilicankit)             # .uf2 / .bin / .map
