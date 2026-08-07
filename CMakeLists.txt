# wilicankit — general-purpose, user-configurable CAN device driver/GUI app.
# Standalone repo: wilibsp (board support package) and LVGL are git
# submodules (see .gitmodules) — `git clone --recurse-submodules` or
# `git submodule update --init --recursive` before configuring.

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.20)

# agentio: remote input injection + screen capture for agent E2E verification.
# Consumed by wilibsp/bsp/CMakeLists.txt; must be declared before that
# add_subdirectory() call below (wilibsp's own top-level CMakeLists.txt is
# not used here — this repo drives the board configure itself).
option(FW2_AGENTIO "Build the agentio remote-control/capture harness" ON)

# RP2350B board header (PICO_RP2350A=0 -> 48 GPIO, 16MB flash).
# Do NOT pass -DPICO_BOARD on the command line — that overrides this.
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/wilibsp/bsp/boards")

include(pico_sdk_import.cmake)
project(wilicankit C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(wilibsp/bsp)
add_subdirectory(wilibsp/libs/onewili)

# LVGL and cJSON deps. LVGL/cJSON's own demos/examples/ThorVG/test/utils
# targets are disabled: we only need the widget/core (resp. parse/build)
# library. cJSON is used by storage_json.c (config save/load) — kept to a
# static lib with its own strict-compiler-flags disabled, since those target
# host GCC/Clang, not arm-none-eabi.
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/lvgl)
# LVGL's own FatFs driver (src/libs/fsdrv/lv_fs_fatfs.c, LV_USE_FS_FATFS in
# lv_conf.h) #includes "ff.h" directly. lvgl is a standalone submodule
# target with no dependency on freewili2_bsp (which is the only other target
# that normally exposes this dir), so it needs its own include path.
target_include_directories(lvgl PRIVATE ${CMAKE_SOURCE_DIR}/wilibsp/bsp/third_party/fatfs)

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

add_executable(wilicankit
    main.c
    lvgl_port.c
    lv_psram_pool.c
    ui_icons.c
    ui_background.c
    can_model.c
    ow_link.c
    can_link.c
    device_leds.c
    app_state.c
    ui_common.c
    ui_signals.c
    ui_messages.c
    ui_transmit.c
    ui_controls.c
    ui_monitor.c
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

# PSRAM-resident build. The display's SD app loader routes purely on the first
# UF2 block's target address: anything in the flash window makes it erase and
# reprogram the display's flash, destroying the persistent FW2Display
# bootloader. Staging at 0x11000000 instead routes to the PSRAM loader
# (freewilimain runPsramAppFromSD), which never touches flash. The RAM window
# is not an option: its ceiling is BL_STAGE_BYTES (192 KiB) and this app's
# .text+.data is well over twice that.
#
# no_flash is the base binary type because it is the only one that emits no
# flash-targeted blocks at all; the linker_overrides/ directory then moves
# .text/.rodata into PSRAM and stores .data there, which stock no_flash cannot
# do (it aliases PSRAM_STORE to RAM).
pico_set_binary_type(wilicankit no_flash)
pico_add_linker_script_override_path(wilicankit ${CMAKE_CURRENT_LIST_DIR}/linker_overrides)
target_compile_definitions(wilicankit PRIVATE
    # no_flash normally loads .data in place and so skips crt0's copy; here the
    # load address is in PSRAM and the run address in RAM, so the copy is real.
    PICO_CRT0_NO_DATA_COPY=0
    # The PSRAM loader's stub hands us PSRAM already configured. Re-running the
    # SDK's setup would re-time the QMI window this app is executing from.
    PICO_RUNTIME_SKIP_INIT_PSRAM=1
)

# Not pico_add_extra_outputs(): its picotool uf2 step fails with "entry point
# is not in mapped part of file", because picotool only maps the flash and
# SRAM windows. tools/elf2uf2_psram.py walks the program headers instead.
pico_add_dis_output(wilicankit)
pico_add_map_output(wilicankit)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET wilicankit POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/elf2uf2_psram.py
            $<TARGET_FILE:wilicankit> ${CMAKE_CURRENT_BINARY_DIR}/wilicankit.uf2
    VERBATIM)