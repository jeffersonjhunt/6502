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

# Dormann runs ~30M instructions; under ASan that is well past CTest's default.
set_tests_properties(test_dormann PROPERTIES TIMEOUT 900)
