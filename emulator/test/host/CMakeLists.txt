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
    target_link_libraries(${name} PRIVATE emu_session emu_debug emu_cpu6502 emu_machine emu_devices emu_bus emu_log)
endfunction()

function(emu_add_test name)
    emu_add_test_exe(${name})
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120) # a hang fails, not stalls
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
emu_add_test(test_via)
emu_add_test(test_acia)
emu_add_test(test_lcd)
emu_add_test(test_decoder)
emu_add_test(test_machine)
emu_add_test(test_loader)
emu_add_test(test_rom_boot)
emu_add_test(test_eater)
emu_add_test(test_disasm)
emu_add_test(test_debugger)
emu_add_test(test_backplane)
emu_add_test(test_bios)
emu_add_test(test_dma)
emu_add_test(test_console_card)
emu_add_test(test_session)
target_compile_definitions(test_backplane PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")


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

# --- the backplane BIOS -----------------------------------------------------
#
# Assembled from backplane/bios as its Makefile does, into the build tree:
# a full 512 KB ROM A image with the BIOS and Woz Monitor in the top block.
# Without cc65 test_bios skips.

set(BIOS_SRC "${PROJECT_SOURCE_DIR}/../backplane/bios")
set(BIOS_DIR "${CMAKE_CURRENT_BINARY_DIR}/bios")
if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${BIOS_DIR}")
    foreach(src bios wozmon)
        add_custom_command(
                OUTPUT "${BIOS_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${BIOS_SRC}" -o "${BIOS_DIR}/${src}.o" "${BIOS_SRC}/${src}.s"
                DEPENDS "${BIOS_SRC}/${src}.s" "${BIOS_SRC}/backplane.inc"
                VERBATIM)
    endforeach()
    add_custom_command(
            OUTPUT "${BIOS_DIR}/bios.rom" "${BIOS_DIR}/bios.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${BIOS_SRC}/bios.cfg" -o "${BIOS_DIR}/bios.rom" -Ln "${BIOS_DIR}/bios.rom.lbl"
                    "${BIOS_DIR}/bios.o" "${BIOS_DIR}/wozmon.o"
            DEPENDS "${BIOS_DIR}/bios.o" "${BIOS_DIR}/wozmon.o" "${BIOS_SRC}/bios.cfg"
            VERBATIM)
    add_custom_target(backplane_bios ALL DEPENDS "${BIOS_DIR}/bios.rom")
else()
    message(STATUS "emu: ca65/ld65 not found — backplane BIOS tests will skip")
endif()

target_compile_definitions(test_bios PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_session PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")

# --- Ben Eater's ROMs -------------------------------------------------------
#
# Assembled from eater/src with vasm, as eater/Makefile does (same flags), so
# the Eater machines run the programs written for the board. vasm is looked
# for where that Makefile expects it. Without it test_eater skips.

set(EATER_SRC "${PROJECT_SOURCE_DIR}/../eater/src")
set(EATER_ROM_DIR "${CMAKE_CURRENT_BINARY_DIR}/eater")
find_program(EMU_VASM vasm6502_oldstyle HINTS "$ENV{HOME}/.local/bin/vbcc/bin")
if(EMU_VASM)
    file(MAKE_DIRECTORY "${EATER_ROM_DIR}")
    set(eater_roms)
    foreach(prog blink hello-world hello-world-4pin hello-world-final keyboard wozmon)
        add_custom_command(
                OUTPUT "${EATER_ROM_DIR}/${prog}.rom"
                COMMAND "${EMU_VASM}" -Fbin -dotdir -wdc02 -quiet -o "${EATER_ROM_DIR}/${prog}.rom"
                        "${EATER_SRC}/${prog}.s"
                DEPENDS "${EATER_SRC}/${prog}.s"
                VERBATIM)
        list(APPEND eater_roms "${EATER_ROM_DIR}/${prog}.rom")
    endforeach()
    add_custom_target(eater_roms ALL DEPENDS ${eater_roms})
else()
    message(STATUS "emu: vasm6502_oldstyle not found — Eater ROM tests will skip")
endif()

target_compile_definitions(test_eater PRIVATE
        EMU_EATER_ROM_DIR="${EATER_ROM_DIR}"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")

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
    add_test(NAME test_app_eater
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_app_eater.py"
                     $<TARGET_FILE:emulator> "${PROJECT_SOURCE_DIR}/machines" "${EATER_ROM_DIR}")
    set_tests_properties(test_app_eater PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME test_trace_diff
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_trace_diff.py"
                     "${PROJECT_SOURCE_DIR}/tools/trace_diff.py")
endif()
