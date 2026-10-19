# Host tests, built by the top-level project (EMU_BUILD_TESTS, on in the
# `dev` preset):
#
#   python3 tools/fetch_fixtures.py        # optional: Dormann + Harte fixtures
#   cmake --preset dev && cmake --build --preset dev && ctest --preset dev
#
# Since Phase 2 the code under test gets the same flags as the tests: the
# full warning set, -Werror, and in `dev` ASan/UBSan. Phase 0's split, with
# the core exempt and its headers marked SYSTEM, is gone along with
# easylogging++ (whose CHECK macros used to shadow harness.h's).
#
# Suites with fixtures exit 77 when those are absent, which CTest reports as
# Skipped rather than Passed.

set(EMU_FIXTURE_DIR "${CMAKE_CURRENT_LIST_DIR}/fixtures")

function(emu_add_test_exe name)
    add_executable(${name} "${name}.cpp")
    emu_house_flags(${name})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    target_compile_definitions(${name} PRIVATE EMU_FIXTURE_DIR="${EMU_FIXTURE_DIR}")
    target_link_libraries(${name} PRIVATE emu_debug emu_cpu6502 emu_machine emu_devices emu_bus emu_log)
endfunction()

function(emu_add_test name)
    emu_add_test_exe(${name})
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
endfunction()

emu_add_test(test_cpu_smoke)
emu_add_test_exe(test_dormann)
foreach(run "6502_functional_test 6502" "6502_functional_test w65c02s" "65C02_extended_opcodes_test w65c02s")
    separate_arguments(args UNIX_COMMAND "${run}")
    list(GET args 0 image)
    list(GET args 1 cpu)
    add_test(NAME test_dormann_${image}_${cpu} COMMAND test_dormann ${args})
    # ~30M instructions each; under ASan that is well past CTest's default.
    set_tests_properties(test_dormann_${image}_${cpu} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
endforeach()
# One runner, one CTest test per CPU model.
emu_add_test_exe(test_harte)
foreach(cpu 6502 wdc65c02)
    add_test(NAME test_harte_${cpu} COMMAND test_harte ${cpu})
    set_tests_properties(test_harte_${cpu} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
emu_add_test(test_pia)
emu_add_test(test_decoder)
emu_add_test(test_machine)
emu_add_test(test_loader)
emu_add_test(test_rom_boot)
emu_add_test(test_disasm)
emu_add_test(test_debugger)


# --- Apple-1 ROMs ------------------------------------------------------------
#
# Assembled here from apple1/src, into the build tree, so the boot tests run
# against the sources in this checkout rather than whatever .rom happens to be
# lying in apple1/src. Mirrors apple1/src/Makefile, including the ld65 -Ln
# label files (<rom>.lbl), with every label since ca65 gets -g. Without cc65
# the ROM tests skip.

set(APPLE1_SRC "${PROJECT_SOURCE_DIR}/../apple1/src")
set(ROM_DIR "${CMAKE_CURRENT_BINARY_DIR}/roms")
find_program(EMU_CA65 ca65)
find_program(EMU_LD65 ld65)

if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${ROM_DIR}")
    foreach(src wozmon applesoft io ibasic)
        add_custom_command(
                OUTPUT "${ROM_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${APPLE1_SRC}" -o "${ROM_DIR}/${src}.o" "${APPLE1_SRC}/${src}.s"
                DEPENDS "${APPLE1_SRC}/${src}.s" "${APPLE1_SRC}/zeropage.s" "${APPLE1_SRC}/macros.s"
                VERBATIM)
    endforeach()
    add_custom_command(
            OUTPUT "${ROM_DIR}/applesoft.rom" "${ROM_DIR}/applesoft.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${APPLE1_SRC}/apple1.cfg" -o "${ROM_DIR}/applesoft.rom" -Ln "${ROM_DIR}/applesoft.rom.lbl"
                    "${ROM_DIR}/applesoft.o" "${ROM_DIR}/io.o" "${ROM_DIR}/wozmon.o"
            DEPENDS "${ROM_DIR}/applesoft.o" "${ROM_DIR}/io.o" "${ROM_DIR}/wozmon.o" "${APPLE1_SRC}/apple1.cfg"
            VERBATIM)
    add_custom_command(
            OUTPUT "${ROM_DIR}/ibasic.rom" "${ROM_DIR}/ibasic.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${APPLE1_SRC}/apple1.cfg" -o "${ROM_DIR}/ibasic.rom" -Ln "${ROM_DIR}/ibasic.rom.lbl"
                    "${ROM_DIR}/ibasic.o" "${ROM_DIR}/wozmon.o"
            DEPENDS "${ROM_DIR}/ibasic.o" "${ROM_DIR}/wozmon.o" "${APPLE1_SRC}/apple1.cfg"
            VERBATIM)
    add_custom_target(apple1_roms ALL DEPENDS "${ROM_DIR}/applesoft.rom" "${ROM_DIR}/ibasic.rom")
else()
    message(STATUS "emu: ca65/ld65 not found — Apple-1 ROM tests will skip")
endif()

target_compile_definitions(test_disasm PRIVATE EMU_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}")
if(EMU_CA65 AND EMU_LD65)
    target_compile_definitions(test_disasm PRIVATE EMU_CA65="${EMU_CA65}" EMU_LD65="${EMU_LD65}")
endif()

target_compile_definitions(test_rom_boot PRIVATE
        EMU_IBASIC_ROM="${ROM_DIR}/ibasic.rom"
        EMU_APPLESOFT_ROM="${ROM_DIR}/applesoft.rom")

# --- the app itself ----------------------------------------------------------
#
# The real `emulator` binary, in a pty. In the `dev` preset it has ASan, so
# LeakSanitizer turns a leak at exit into a failure.

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME test_app_tty
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_app_tty.py"
                     $<TARGET_FILE:emulator> "${ROM_DIR}/ibasic.rom")
    set_tests_properties(test_app_tty PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME test_app_debug
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_app_debug.py"
                     $<TARGET_FILE:emulator> "${ROM_DIR}/ibasic.rom" "${ROM_DIR}/ibasic.rom.lbl"
                     "${PROJECT_SOURCE_DIR}/tools/trace_diff.py" "${PROJECT_SOURCE_DIR}/machines")
    set_tests_properties(test_app_debug PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME test_trace_diff
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_trace_diff.py"
                     "${PROJECT_SOURCE_DIR}/tools/trace_diff.py")
endif()
