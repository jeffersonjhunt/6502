# Host test build for the 6502 core — no terminal, no ROMs, no hardware.
#
#   python3 tools/fetch_fixtures.py        # optional: Dormann + Harte fixtures
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
#
# Phase 0 (docs/EMULATOR_PLAN.md §4) measures the core exactly as tagged
# `beta`, so it is compiled from its existing sources with its existing
# includes. Test code gets the house warning set and -Werror; the core does
# not yet, because Phase 0 does not change it and a wall of -Wconversion
# warnings from code we are about to rewrite would turn this build into a
# Phase 2 work list. Phase 2 moves the core under the same flags.
#
# Fixture-driven suites exit 77 when their fixtures are absent, which CTest
# reports as Skipped rather than Passed: a machine without them shows that it
# did not run them.

cmake_minimum_required(VERSION 3.16)
project(emu_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Sanitizers by default, on the core as well as the tests: UB in an emulator
# shows up as an instruction that is right on one compiler and wrong on the
# next, which is exactly what a conformance suite should catch.
option(EMU_SANITIZE "Build host tests with ASan/UBSan" ON)
if(EMU_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(fmt REQUIRED)

set(EMU_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(EMU_FIXTURE_DIR "${CMAKE_CURRENT_LIST_DIR}/fixtures")

# The core as it stands: cpu.cpp plus the easylogging++ it logs through.
# ELPP_NO_DEFAULT_LOG_FILE stops easylogging creating logs/myeasylog.log in the
# build tree; it has to match across every TU that includes logging.h, hence
# PUBLIC. Its headers are SYSTEM for the same reason the core skips -Werror:
# bus.h alone trips -Wconversion, and that is Phase 2's to fix.
#
# ELPP_NO_CHECK_MACROS is not optional. Without it easylogging++ defines its
# own CHECK and CHECK_EQ, silently replacing harness.h's (SYSTEM swallows the
# redefinition warning), and with logging off every assertion becomes a no-op:
# the smoke suite passed against a deliberately broken core until this was
# found. test_support.h refuses to compile without it.
add_library(emu_core STATIC
        "${EMU_ROOT}/src/cpu.cpp"
        "${EMU_ROOT}/src/memory.cpp"
        "${EMU_ROOT}/src/loader.cpp"
        "${EMU_ROOT}/src/apple1.cpp"
        "${EMU_ROOT}/src/display.cpp"
        "${EMU_ROOT}/src/keyboard.cpp"
        "${EMU_ROOT}/src/logging.cpp")
target_include_directories(emu_core SYSTEM PUBLIC "${EMU_ROOT}/include")
target_compile_definitions(emu_core PUBLIC ELPP_NO_DEFAULT_LOG_FILE ELPP_NO_CHECK_MACROS)
target_link_libraries(emu_core PUBLIC fmt::fmt)

# OBJECT, not STATIC: nothing references quiet_logging.o by symbol (the core
# only reaches el::base::elStorage through it), so an archive member would be
# dropped by the linker whenever the core happens to come first.
add_library(emu_test_support OBJECT quiet_logging.cpp)
target_link_libraries(emu_test_support PUBLIC emu_core)

enable_testing()

function(emu_add_test name)
    add_executable(${name} "${name}.cpp")
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(${name} PRIVATE EMU_FIXTURE_DIR="${EMU_FIXTURE_DIR}")
    target_link_libraries(${name} PRIVATE emu_test_support)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

emu_add_test(test_cpu_smoke)
emu_add_test(test_dormann)
emu_add_test(test_harte)
emu_add_test(test_memory)
emu_add_test(test_loader)
emu_add_test(test_rom_boot)

# --- Apple-1 ROMs ------------------------------------------------------------
#
# Assembled here from apple1/src, into the build tree, so the boot tests run
# against the sources in this checkout rather than whatever .rom happens to be
# lying in apple1/src. Mirrors apple1/src/Makefile. Without cc65 the ROM tests
# skip.

set(APPLE1_SRC "${EMU_ROOT}/../apple1/src")
set(ROM_DIR "${CMAKE_CURRENT_BINARY_DIR}/roms")
find_program(EMU_CA65 ca65)
find_program(EMU_LD65 ld65)

if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${ROM_DIR}")
    foreach(src wozmon applesoft io ibasic)
        add_custom_command(
                OUTPUT "${ROM_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -I "${APPLE1_SRC}" -o "${ROM_DIR}/${src}.o" "${APPLE1_SRC}/${src}.s"
                DEPENDS "${APPLE1_SRC}/${src}.s" "${APPLE1_SRC}/zeropage.s" "${APPLE1_SRC}/macros.s"
                VERBATIM)
    endforeach()
    add_custom_command(
            OUTPUT "${ROM_DIR}/applesoft.rom"
            COMMAND "${EMU_LD65}" -C "${APPLE1_SRC}/apple1.cfg" -o "${ROM_DIR}/applesoft.rom"
                    "${ROM_DIR}/applesoft.o" "${ROM_DIR}/io.o" "${ROM_DIR}/wozmon.o"
            DEPENDS "${ROM_DIR}/applesoft.o" "${ROM_DIR}/io.o" "${ROM_DIR}/wozmon.o" "${APPLE1_SRC}/apple1.cfg"
            VERBATIM)
    add_custom_command(
            OUTPUT "${ROM_DIR}/ibasic.rom"
            COMMAND "${EMU_LD65}" -C "${APPLE1_SRC}/apple1.cfg" -o "${ROM_DIR}/ibasic.rom"
                    "${ROM_DIR}/ibasic.o" "${ROM_DIR}/wozmon.o"
            DEPENDS "${ROM_DIR}/ibasic.o" "${ROM_DIR}/wozmon.o" "${APPLE1_SRC}/apple1.cfg"
            VERBATIM)
    add_custom_target(apple1_roms ALL DEPENDS "${ROM_DIR}/applesoft.rom" "${ROM_DIR}/ibasic.rom")
else()
    message(STATUS "emu: ca65/ld65 not found — Apple-1 ROM tests will skip")
endif()

target_compile_definitions(test_rom_boot PRIVATE
        EMU_IBASIC_ROM="${ROM_DIR}/ibasic.rom"
        EMU_APPLESOFT_ROM="${ROM_DIR}/applesoft.rom")

# --- the app itself ----------------------------------------------------------
#
# Built here as well as by emulator/CMakeLists.txt so it gets the sanitizers:
# test_app_tty relies on LeakSanitizer to turn a leak at exit into a failure.

add_executable(emulator_app "${EMU_ROOT}/apps/emulator.cpp")
target_include_directories(emulator_app PRIVATE "${EMU_ROOT}/apps")
target_link_libraries(emulator_app PRIVATE emu_core)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME test_app_tty
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_app_tty.py"
                     $<TARGET_FILE:emulator_app> "${EMU_ROOT}/logging.cfg" "${ROM_DIR}/ibasic.rom")
    set_tests_properties(test_app_tty PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

# Dormann runs ~30M instructions; under ASan that is well past CTest's default.
set_tests_properties(test_dormann PROPERTIES TIMEOUT 900)
