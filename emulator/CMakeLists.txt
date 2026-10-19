# 6502 emulator: CPU core, Apple-1 machine, terminal front end, host tests.
#
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
#
# One project builds everything with the house flags (pinled's: the full
# warning set as errors, C++17). The `dev` preset adds ASan/UBSan, which the
# tests rely on (test_app_tty uses LeakSanitizer to catch leaks at exit);
# `release` is the optimised build to actually run.

cmake_minimum_required(VERSION 3.18) # file(CONFIGURE)
project(emu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(EMU_SANITIZE "Build with ASan/UBSan" OFF)
option(EMU_TRACE "Compile in per-instruction and per-access tracing (EMU_LOGV)" OFF)
option(EMU_BUILD_TESTS "Build the host tests" ON)

if(EMU_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Applied per target rather than globally, so nothing third-party ever has to
# pass it (there is none today; easylogging++ and fmt went in Phase 2).
function(emu_house_flags target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror)
    if(EMU_TRACE)
        target_compile_definitions(${target} PRIVATE EMU_TRACE)
    endif()
endfunction()

add_subdirectory(components/emu_log)
add_subdirectory(components/bus)
add_subdirectory(components/cpu6502)
add_subdirectory(components/devices)
add_subdirectory(components/machine)
add_subdirectory(components/debug)
add_subdirectory(apps/emu)

if(EMU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test/host)
endif()
