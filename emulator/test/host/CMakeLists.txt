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

# --- WebAssembly (the `web` preset) -----------------------------------------
#
# The same core compiled by Emscripten and run under node (the toolchain's
# CMAKE_CROSSCOMPILING_EMULATOR), reading the build tree through NODERAWFS:
# the suites that need no fixture download, Python or subprocess, so both
# sides of the native/WASM fork run the same tests. Then apps/web's own.

if(EMSCRIPTEN)
    foreach(name test_cpu_smoke test_pia test_via test_acia test_decoder test_machine test_loader test_debugger
                 test_backplane test_dma test_console_card test_video test_usb test_sound test_sid test_sid_audio test_sid_file test_apple test_bios test_session
                 test_disk_image test_disk2 test_sd test_sd_fat test_bios_sd test_oodos_card test_ds3231 test_rtc)
        emu_add_test(${name})
        # The tests put 64 KB buses on the stack; Emscripten's default stack is 64 KB in all.
        target_link_options(${name} PRIVATE -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB)
    endforeach()
    target_compile_definitions(test_backplane PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    target_compile_definitions(test_rtc PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    target_compile_definitions(test_sd_fat PRIVATE EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd")
    target_compile_definitions(test_bios_sd PRIVATE EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd")
    target_compile_definitions(test_oodos_card PRIVATE
            EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
            EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs")
    foreach(name test_bios test_session test_sd test_bios_sd test_oodos_card)
        target_compile_definitions(${name} PRIVATE
                EMU_BIOS_ROM="${EMU_BIOS_DIR}/bios.rom"
                EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    endforeach()
    return()
endif()

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
emu_add_test(test_ds3231)
emu_add_test(test_rtc)
target_compile_definitions(test_rtc PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
emu_add_test(test_bios)
set_tests_properties(test_bios PROPERTIES TIMEOUT 300) # about 140 s of BIOS runs under ASan
emu_add_test(test_dma)
emu_add_test(test_console_card)
emu_add_test(test_video)
emu_add_test(test_usb)
emu_add_test(test_sound)
emu_add_test(test_sid)
emu_add_test(test_sid_audio)
emu_add_test(test_sid_file)
emu_add_test(test_sid_corpus) # the owner's tunes, in fixtures/SIDs; skips without them
emu_add_test(test_apple)
emu_add_test(test_disk_image)
emu_add_test(test_disk2)
emu_add_test(test_session)
emu_add_test(test_sd)
emu_add_test(test_sd_fat)
emu_add_test(test_bios_sd)
emu_add_test(test_oodos_card)
emu_add_test(test_oodos_boot)
set_tests_properties(test_oodos_boot PROPERTIES TIMEOUT 600) # dozens of boots from 17-33 MB cards, under ASan
emu_add_test(test_oodos_e2e)
set_tests_properties(test_oodos_e2e PROPERTIES TIMEOUT 600) # a few boots of the whole card, under ASan
emu_add_test(test_iie)
set_tests_properties(test_iie PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200) # minutes of Apple time: boots and disks, under ASan
target_compile_definitions(test_backplane PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
emu_add_test(test_pacing) # the terminal app's arithmetic, header-only
target_include_directories(test_pacing PRIVATE "${PROJECT_SOURCE_DIR}/apps/emu")


# --- Apple-1 ROMs ------------------------------------------------------------
#
# Assembled here from apple1/src, into the build tree, so the boot tests run
# against the sources in this checkout rather than whatever .rom happens to be
# lying in apple1/src. Mirrors apple1/src/Makefile, including the ld65 -Ln
# label files (<rom>.lbl), with every label since ca65 gets -g. Without cc65
# the ROM tests skip.

set(APPLE1_SRC "${PROJECT_SOURCE_DIR}/../apple1/src")
set(ROM_DIR "${CMAKE_CURRENT_BINARY_DIR}/roms")

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
# Assembled by cmake/roms.cmake (apps/web embeds it too). Without cc65
# test_bios and test_session skip. test_iie also needs the owner's Apple IIe
# ROMs in fixtures/apple2e, and skips without them.

set(BIOS_DIR "${EMU_BIOS_DIR}")
target_compile_definitions(test_bios PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_session PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_sd_fat PRIVATE EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd")
target_compile_definitions(test_bios_sd PRIVATE
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_card PRIVATE
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_boot PRIVATE
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_e2e PRIVATE
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_sd PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_iie PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")

# --- OODOS: the system, the loader, the card's programs ------------------------
#
# oodos/sys's Makefile builds OODOS.SYS and its test programs (ca65, ld65,
# python3) into the build tree, for test_oodos_sys; oodos/ldr's builds OOLDR
# and its test system, and oodos's Makefile HELLO.PRL, for test_oodos_boot,
# which checks the committed card's copies against them (and reads OOLDR's
# labels). ctest stays hermetic with no skips (hw/oodos.md, "Testing"), so
# the tools are required: configuring fails without them.

emu_add_test(test_oodos_sys)
set_tests_properties(test_oodos_sys PROPERTIES TIMEOUT 600) # a dozen boots, seconds of machine time, under ASan
set(OODOS_SRC "${PROJECT_SOURCE_DIR}/../oodos")
set(OODOS_SYS_SRC "${OODOS_SRC}/sys")
set(OODOS_SYS_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_sys")
set(OODOS_LDR_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_ldr")
set(OODOS_PRL_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_prl")
find_package(Python3 COMPONENTS Interpreter)
find_program(EMU_MAKE make)
if(NOT (EMU_CA65 AND EMU_LD65 AND Python3_FOUND AND EMU_MAKE))
    message(FATAL_ERROR "emu: the OODOS tests need cc65 (ca65, ld65), python3 and make")
endif()
file(GLOB oodos_deps CONFIGURE_DEPENDS
        "${OODOS_SYS_SRC}/*.s" "${OODOS_SYS_SRC}/*.inc" "${OODOS_SYS_SRC}/*.cfg" "${OODOS_SYS_SRC}/*.py"
        "${OODOS_SYS_SRC}/Makefile" "${OODOS_SYS_SRC}/test/*.s" "${OODOS_SYS_SRC}/test/*.inc"
        "${OODOS_SRC}/ldr/*.s" "${OODOS_SRC}/ldr/*.cfg" "${OODOS_SRC}/ldr/Makefile" "${OODOS_SRC}/Makefile"
        "${OODOS_SRC}/include/*.inc" "${OODOS_SRC}/cfg/*.cfg" "${OODOS_SRC}/tools/*.py"
        "${OODOS_SRC}/examples/*.s" "${EMU_BIOS_SRC}/backplane.inc")
add_custom_command(
        OUTPUT "${OODOS_SYS_DIR}/OODOS.SYS" "${OODOS_SYS_DIR}/oodos.lbl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SYS_SRC}" "BUILD=${OODOS_SYS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        DEPENDS ${oodos_deps}
        VERBATIM)
add_custom_command(
        OUTPUT "${OODOS_LDR_DIR}/BOOT.BIN" "${OODOS_LDR_DIR}/OODOS.SYS" "${OODOS_LDR_DIR}/ooldr.lbl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}/ldr" "BUILD=${OODOS_LDR_DIR}"
        DEPENDS ${oodos_deps}
        VERBATIM)
add_custom_command(
        OUTPUT "${OODOS_PRL_DIR}/hello.prl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" "BUILD=${OODOS_PRL_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
                "${OODOS_PRL_DIR}/hello.prl"
        DEPENDS ${oodos_deps}
        VERBATIM)
add_custom_target(oodos_sys ALL DEPENDS "${OODOS_SYS_DIR}/OODOS.SYS")
add_custom_target(oodos_ldr ALL DEPENDS "${OODOS_LDR_DIR}/BOOT.BIN" "${OODOS_PRL_DIR}/hello.prl")
add_dependencies(test_oodos_sys oodos_sys backplane_bios)
add_dependencies(test_oodos_boot oodos_sys oodos_ldr backplane_bios)
target_compile_definitions(test_oodos_sys PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_boot PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_OODOS_LDR_DIR="${OODOS_LDR_DIR}"
        EMU_OODOS_PRL_DIR="${OODOS_PRL_DIR}"
        EMU_OODOS_CARD_DIR="${OODOS_SRC}/card")

# --- OODOS: the BDOS's model test --------------------------------------------
#
# oodos/test/: the model of CP/M 3's file system, the generator and the
# comparison (Python), and DRIVER.COM, which runs a script through the BDOS
# (oodos/test/driver's Makefile, through the rules gate). test_oodos_model_py
# is the model's own tests; test_oodos_model runs DRIVER.COM with the model
# as its BDOS, and under OODOS. OODOS_MODEL_BDOS (on) also compares OODOS's
# own BDOS with the model (hw/oodos.md, "The BDOS, phase 3"), on eight seeds
# (about 7 minutes with the rest under ASan); more seeds by hand (oodos/README.md).

option(OODOS_MODEL_BDOS "test_oodos_model: compare OODOS's BDOS with the model" ON)
emu_add_test(test_oodos_model)
set_tests_properties(test_oodos_model PROPERTIES TIMEOUT 1200) # eight seeds of BDOS calls under ASan: about 7 minutes
set(OODOS_MODEL_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_model")
file(MAKE_DIRECTORY "${OODOS_MODEL_DIR}")
add_custom_command(
        OUTPUT "${OODOS_MODEL_DIR}/driver.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}/test/driver" "BUILD=${OODOS_MODEL_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        DEPENDS "${OODOS_SRC}/test/driver/driver.s" "${OODOS_SRC}/test/driver/Makefile"
                "${OODOS_SRC}/include/oodos.inc" "${OODOS_SRC}/cfg/com.cfg" "${OODOS_SRC}/tools/rules.py"
        VERBATIM)
add_custom_target(oodos_driver ALL DEPENDS "${OODOS_MODEL_DIR}/driver.com")
add_dependencies(test_oodos_model oodos_driver oodos_sys backplane_bios)
target_compile_definitions(test_oodos_model PRIVATE
        EMU_OODOS_SRC="${OODOS_SRC}"
        EMU_OODOS_DRIVER="${OODOS_MODEL_DIR}/driver.com"
        EMU_OODOS_MODEL_DIR="${OODOS_MODEL_DIR}"
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_OODOS_MODEL_BDOS=$<BOOL:${OODOS_MODEL_BDOS}>
        EMU_PYTHON="${Python3_EXECUTABLE}"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
add_test(NAME test_oodos_model_py COMMAND "${Python3_EXECUTABLE}" -I "${OODOS_SRC}/test/test_model.py")
set_tests_properties(test_oodos_model_py PROPERTIES TIMEOUT 600)

# --- the sound card's test driver --------------------------------------------
#
# test/host/sound/driver.s, for the card's 6510: assembled here so
# test_sound_driver runs a real program on the card. Without cc65 it skips.

emu_add_test(test_sound_driver)
set(SOUND_SRC "${CMAKE_CURRENT_LIST_DIR}/sound")
set(SOUND_DIR "${CMAKE_CURRENT_BINARY_DIR}/sound")
if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${SOUND_DIR}")
    add_custom_command(
            OUTPUT "${SOUND_DIR}/driver.bin"
            COMMAND "${EMU_CA65}" -g -o "${SOUND_DIR}/driver.o" "${SOUND_SRC}/driver.s"
            COMMAND "${EMU_LD65}" -C "${SOUND_SRC}/driver.cfg" -o "${SOUND_DIR}/driver.bin" "${SOUND_DIR}/driver.o"
            DEPENDS "${SOUND_SRC}/driver.s" "${SOUND_SRC}/driver.cfg"
            VERBATIM)
    add_custom_target(sound_driver ALL DEPENDS "${SOUND_DIR}/driver.bin")
    add_dependencies(test_sound_driver sound_driver)
endif()
target_compile_definitions(test_sound_driver PRIVATE EMU_SOUND_DRIVER="${SOUND_DIR}/driver.bin")

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
    add_test(NAME test_fetch_fixtures # a fake download into a scratch directory: never fixtures/
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_fetch_fixtures.py"
                     "${PROJECT_SOURCE_DIR}/tools/fetch_fixtures.py")
    add_test(NAME test_trace_diff
             COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_LIST_DIR}/test_trace_diff.py"
                     "${PROJECT_SOURCE_DIR}/tools/trace_diff.py")
endif()
