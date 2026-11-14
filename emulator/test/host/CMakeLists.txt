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

# A suite's CTest entries: EMU_RUNS_<suite>, those that run its cases (the suite's own name, or its parts
# when split), and EMU_ENTRIES_<suite>, those and the split's check. The labels (below, "Labels") go on
# every entry of a suite.
function(emu_entry suite test)
    set_property(GLOBAL APPEND PROPERTY EMU_RUNS_${suite} ${test})
    set_property(GLOBAL APPEND PROPERTY EMU_ENTRIES_${suite} ${test})
endfunction()

# set_tests_properties on the entries that run a suite's cases, split or not.
function(emu_suite_properties suite)
    get_property(runs GLOBAL PROPERTY EMU_RUNS_${suite})
    if(NOT runs)
        message(FATAL_ERROR "emu: emu_suite_properties(${suite}): no such suite")
    endif()
    set_tests_properties(${runs} PROPERTIES ${ARGN})
endfunction()

# A long suite is split (hw/testing.md, item 6) by set(EMU_SPLIT_<name> N) before its emu_add_test: one
# binary, N CTest entries <name>_1ofN ... <name>_NofN, each taking every Nth case (OOE_TEST_SHARD=K/N,
# harness.h), and <name>_cases, check_split.py holding the entries to every case of the binary's. And
# set(EMU_SPLIT_ALONE_<name> case...) gives each of those cases an entry of its own, <name>_<case>
# (OOE_TEST_CASES), the N shares taking the rest (OOE_TEST_EXCEPT): for a case far longer than the others.
# Every entry sets all four selection variables (empty is unset), so one exported by hand changes nothing.
set(EMU_TEST_ENV_CLEAR "OOE_TEST_LIST=" "OOE_TEST_SHARD=" "OOE_TEST_CASES=" "OOE_TEST_EXCEPT=")
function(emu_add_test name)
    emu_add_test_exe(${name})
    if(DEFINED EMU_SPLIT_${name})
        set(n ${EMU_SPLIT_${name}})
        set(alone ${EMU_SPLIT_ALONE_${name}})
        string(JOIN "," except ${alone})
        foreach(k RANGE 1 ${n})
            add_test(NAME ${name}_${k}of${n} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(${name}_${k}of${n} PROPERTIES
                    ENVIRONMENT "OOE_TEST_LIST=;OOE_TEST_CASES=;OOE_TEST_EXCEPT=${except};OOE_TEST_SHARD=${k}/${n}"
                    SKIP_RETURN_CODE 77 TIMEOUT 120)
            emu_entry(${name} ${name}_${k}of${n})
        endforeach()
        foreach(c ${alone})
            add_test(NAME ${name}_${c} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(${name}_${c} PROPERTIES
                    ENVIRONMENT "OOE_TEST_LIST=;OOE_TEST_SHARD=;OOE_TEST_EXCEPT=;OOE_TEST_CASES=${c}"
                    SKIP_RETURN_CODE 77 TIMEOUT 120)
            emu_entry(${name} ${name}_${c})
        endforeach()
        add_test(NAME ${name}_cases
                 COMMAND "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_LIST_DIR}/check_split.py"
                         "${CMAKE_BINARY_DIR}" $<TARGET_FILE:${name}>)
        set_tests_properties(${name}_cases PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
        set_property(GLOBAL APPEND PROPERTY EMU_ENTRIES_${name} ${name}_cases)
    else()
        add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "${EMU_TEST_ENV_CLEAR}"
                SKIP_RETURN_CODE 77 TIMEOUT 120) # a hang fails, not stalls
        emu_entry(${name} ${name})
    endif()
endfunction()

# --- WebAssembly (the `web` preset) -----------------------------------------
#
# The same core compiled by Emscripten and run under node (the toolchain's
# CMAKE_CROSSCOMPILING_EMULATOR), reading the build tree through NODERAWFS:
# the suites that need no fixture download, Python or subprocess, so both
# sides of the native/WASM fork run the same tests. Then apps/web's own.

if(EMSCRIPTEN)
    foreach(name test_cpu_smoke test_pia test_via test_acia test_decoder test_machine test_loader test_debugger
                 test_backplane test_dma test_console_card test_video test_video_rev2 test_usb test_sound test_sound_pcm test_sound_mix test_sound_mp3 test_sound_eq test_es5503 test_sound_doc test_sid test_sid_audio test_sid_file test_apple test_bios test_session
                 test_disk_image test_disk2 test_sd test_sd_fat test_bios_sd test_oodos_card test_ds3231 test_rtc test_joyports)
        emu_add_test(${name})
        # The tests put 64 KB buses on the stack; Emscripten's default stack is 64 KB in all.
        target_link_options(${name} PRIVATE -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB)
    endforeach()
    target_compile_definitions(test_backplane PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    target_compile_definitions(test_video_rev2 PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    target_compile_definitions(test_rtc PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
    target_compile_definitions(test_joyports PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
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
    target_compile_definitions(test_bios PRIVATE EMU_BIOS_DIR_SPLASH="${PROJECT_SOURCE_DIR}/../backplane/bios/splash")
    target_compile_definitions(test_sound_mp3 PRIVATE EMU_MP3_DIR="${CMAKE_CURRENT_LIST_DIR}/mp3")
    target_link_libraries(test_sound_mp3 PRIVATE emu_minimp3)
    target_compile_definitions(test_sound_eq PRIVATE EMU_MP3_DIR="${CMAKE_CURRENT_LIST_DIR}/mp3")
    target_link_libraries(test_sound_eq PRIVATE emu_minimp3)
    return()
endif()

find_package(Python3 COMPONENTS Interpreter) # the Python suites, and a split suite's check

# The long suites, split (hw/testing.md, item 6): over ~45 s alone in the fast build (2026-11-11).
set(EMU_SPLIT_test_oodos_disk2_media 4)
set(EMU_SPLIT_test_oodos_disk2 3)
set(EMU_SPLIT_test_oodos_utils_dir 3)
set(EMU_SPLIT_test_oodos_utils_files 2)
# test_session under the sanitizers (dev, alone, 2026-11-11): 106 s of its 120, 50 of them one case's.
set(EMU_SPLIT_test_session 2)
set(EMU_SPLIT_ALONE_test_session the_speaker_and_the_sids_mix_into_the_same_stream)

emu_add_test(test_cpu_smoke)
emu_add_test_exe(test_dormann)
foreach(run "6502_functional_test 6502" "6502_functional_test w65c02s" "65C02_extended_opcodes_test w65c02s")
    separate_arguments(args UNIX_COMMAND "${run}")
    list(GET args 0 image)
    list(GET args 1 cpu)
    add_test(NAME test_dormann_${image}_${cpu} COMMAND test_dormann ${args})
    emu_entry(test_dormann test_dormann_${image}_${cpu})
    # ~30M instructions each; under ASan that is well past CTest's default.
    set_tests_properties(test_dormann_${image}_${cpu} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
endforeach()
# One runner, one CTest test per CPU model.
emu_add_test_exe(test_harte)
foreach(cpu 6502 wdc65c02)
    add_test(NAME test_harte_${cpu} COMMAND test_harte ${cpu})
    emu_entry(test_harte test_harte_${cpu})
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
emu_add_test(test_joyports)
target_compile_definitions(test_joyports PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
emu_add_test(test_bios)
set_tests_properties(test_bios PROPERTIES TIMEOUT 600) # about 290 s alone under ASan (2026-11-07), more 4 at once
emu_add_test(test_dma)
emu_add_test(test_console_card)
emu_add_test(test_video)
emu_add_test(test_video_rev2) # revision 2 (hw/video-rev2.md): card memory, the aperture, the port
target_compile_definitions(test_video_rev2 PRIVATE EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
emu_add_test(test_usb)
emu_add_test(test_sound)
emu_add_test(test_sound_pcm)
emu_add_test(test_sound_mix)
emu_add_test(test_sound_mp3) # our own MP3 files (test/host/mp3, tools/make_mp3_fixtures.sh), against minimp3 itself
target_compile_definitions(test_sound_mp3 PRIVATE EMU_MP3_DIR="${CMAKE_CURRENT_LIST_DIR}/mp3")
target_link_libraries(test_sound_mp3 PRIVATE emu_minimp3)
emu_add_test(test_sound_eq) # the EQ on an MP3 against minimp3's frames too
target_compile_definitions(test_sound_eq PRIVATE EMU_MP3_DIR="${CMAKE_CURRENT_LIST_DIR}/mp3")
target_link_libraries(test_sound_eq PRIVATE emu_minimp3)
emu_add_test(test_es5503) # the DOC alone (hw/sound-rev2.md step 6)
emu_add_test(test_sound_doc) # the DOC on the card (step 7)
emu_add_test(test_sid)
emu_add_test(test_sid_audio)
emu_add_test(test_sid_file)
emu_add_test(test_sid_corpus) # a sample of the owner's tunes, under fixtures/SIDs; skips without them
target_compile_definitions(test_sid_corpus PRIVATE EMU_SID_KNOWN_FAILURES="${CMAKE_CURRENT_LIST_DIR}/sid_known_failures.txt")
emu_add_test(test_apple)
emu_add_test(test_disk_image)
emu_add_test(test_disk2)
emu_add_test(test_disk2_native) # native mode (phase 6a): 6502 code on the backplane, no fixtures
emu_add_test(test_disk2_prom) # native mode's disks on the owner's sequencer PROM (fixtures/apple2e): exits 77 without it
emu_add_test(test_session)
emu_add_test(test_sd)
emu_add_test(test_sd_fat)
emu_add_test(test_bios_sd)
emu_add_test(test_bios_diag) # the ROM's diagnostics (hw/diag.md, plan 2): D3a's core and BIOS 0.14's entries
set_tests_properties(test_bios_diag PROPERTIES TIMEOUT 900)
target_compile_definitions(test_bios_diag PRIVATE
        EMU_BIOS_ROM="${EMU_BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
emu_add_test(test_oodos_card)
set_tests_properties(test_bios_sd PROPERTIES TIMEOUT 300) # the boot at power-on waits out the 5 s splash: ~115 s alone under ASan
set_tests_properties(test_oodos_card PROPERTIES TIMEOUT 1800) # each case boots the card through the 5 s splash: about 710 s alone under ASan (2026-11-05)
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
        EMU_BIOS_DIR_SPLASH="${PROJECT_SOURCE_DIR}/../backplane/bios/splash"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_session PRIVATE
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_disk2_native PRIVATE
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
# and its test system, and oodos's Makefile HELLO.PRL and the utilities, for
# test_oodos_boot, which checks the committed card's copies against them (and reads OOLDR's
# labels). ctest stays hermetic with no skips (hw/oodos.md, "Testing"), so
# the tools are required: configuring fails without them.

emu_add_test(test_oodos_sys)
set_tests_properties(test_oodos_sys PROPERTIES TIMEOUT 1800) # dozens of boots and minutes of machine time under ASan: about 600 s alone, more under load
emu_add_test(test_oodos_ccp)
set_tests_properties(test_oodos_ccp PROPERTIES TIMEOUT 1800) # the CCP's cases (phase 4a), test_oodos_sys's machinery
emu_add_test(test_oodos_rsx)
set_tests_properties(test_oodos_rsx PROPERTIES TIMEOUT 1800) # the loader and the RSXs (phase 5a), test_oodos_ccp's machinery
# Phase 4b's utilities (oodos/utils, `make -C oodos utils`): a suite each group, test_oodos_ccp's
# machinery. A new suite is its name here and its .cpp.
set(OODOS_UTILS_SUITES test_oodos_apple test_oodos_disk2 test_oodos_disk2_media test_oodos_disk2_noprom test_oodos_dos33 test_oodos_dos33_iie test_oodos_err10 test_oodos_utils test_oodos_utils_set test_oodos_utils_files test_oodos_utils_dir test_oodos_utils_sys test_oodos_utils_rom test_oodos_utils_pip test_oodos_utils_rsx test_oodos_utils_submit test_oodos_clock test_oodos_clock_disk2 test_oodos_input test_oodos_sound test_oodos_playsid test_oodos_video test_oodos_load test_oodos_gfx test_oodos_ui test_oodos_pb test_oodos_mu test_oodos_c test_oodos_sysinfo)
foreach(name ${OODOS_UTILS_SUITES})
    emu_add_test(${name})
    emu_suite_properties(${name} TIMEOUT 1800) # boots and command lines under ASan, as test_oodos_ccp
endforeach()
emu_suite_properties(test_oodos_utils_dir TIMEOUT 3600) # its page and sheet sweep (some 25 listings): ~1,460 s at load 2, more in a parallel run
emu_suite_properties(test_oodos_disk2_media TIMEOUT 5400) # 24 cases of Disk II time under ASan: alone 2,050-2,457 s at c11e7e7 and 2,356 s at e4aa5ca (2026-11-09), so the host's spread, not a change
emu_suite_properties(test_oodos_utils_files TIMEOUT 3600) # 1,291-1,305 s in the full tier under ASan (2026-11-09): 72% of 1,800, with the host's spread of about 30%
emu_suite_properties(test_oodos_disk2 TIMEOUT 5400) # the Disk II's cases under ASan: 1,724-1,733 s in the full tier (2026-11-09), too close to 1,800
set(OODOS_SRC "${PROJECT_SOURCE_DIR}/../oodos")
set(OODOS_SYS_SRC "${OODOS_SRC}/sys")
set(OODOS_SYS_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_sys")
set(OODOS_LDR_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_ldr")
set(OODOS_PRL_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_prl")
set(OODOS_UTILS_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_utils")
find_package(Python3 COMPONENTS Interpreter)
find_program(EMU_MAKE make)
find_program(EMU_CC65 cc65)
if(NOT (EMU_CA65 AND EMU_LD65 AND EMU_CC65 AND Python3_FOUND AND EMU_MAKE))
    message(FATAL_ERROR "emu: the OODOS tests need cc65 (cc65, ca65, ld65), python3 and make")
endif()
file(GLOB oodos_includes CONFIGURE_DEPENDS "${OODOS_SRC}/include/*.inc")
file(GLOB oodos_deps CONFIGURE_DEPENDS
        "${OODOS_SYS_SRC}/*.s" "${OODOS_SYS_SRC}/*.inc" "${OODOS_SYS_SRC}/*.cfg" "${OODOS_SYS_SRC}/*.py"
        "${OODOS_SYS_SRC}/Makefile" "${OODOS_SYS_SRC}/test/*.s" "${OODOS_SYS_SRC}/test/*.inc"
        "${OODOS_SRC}/ldr/*.s" "${OODOS_SRC}/ldr/*.cfg" "${OODOS_SRC}/ldr/Makefile" "${OODOS_SRC}/Makefile"
        "${OODOS_SRC}/cfg/*.cfg" "${OODOS_SRC}/tools/*.py"
        "${OODOS_SRC}/examples/*.s" "${EMU_BIOS_SRC}/backplane.inc")
list(APPEND oodos_deps ${oodos_includes})
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
# The utilities: every oodos/utils/*.s there is (the Makefile's wildcard), so a stamp stands for them.
# With them, OOLIB L4's: GXTEST (oodos/test/gx, the library's test program, not a utility) and the assets
# test_oodos_gfx shows with VIEW, from PNGs check_mkasset.py writes (never the owner's); L5's PBTEST
# (oodos/test/pb, the sound library's, for test_oodos_pb); and L6b's MUTEST (oodos/test/mu, the music
# library's, for test_oodos_mu, with the 6510 driver it takes in, lib/mudrv). And SIDOVL with its overlays
# SIDPLUG (the library's .sid, its zero page SIDOVL's) and SIDPLUGZ (without, refused), oodos/test/ovl, for
# test_oodos_playsid. And CTEST, the C target's test program (oodos/test/c, Plan C1), for test_oodos_c, with
# a copy in bigstack/ linked with a C stack too big for the TPA. And UITEST (oodos/test/ui), OOUI's test
# program, for test_oodos_ui: four programs from its one source (UITEST, UITWID, UITBAR, UITPICK), each an
# ordinary .COM linked with OOUI's program part (ooui.lib) and oolib.lib (test/ui/uiprog.cfg), and
# OOUI.BNK, the banked library they load (U3a), with its labels (ooui-bnk.lbl); and UITMIS, UITEST with
# UISTATE off a page (test/ui/uimis.cfg), for ui_load's UX_STATE. And UIDEMO (oodos/demo), the demo card's
# OOUI program, linked the same way (demo/uidemo.cfg), with its labels, for test_oodos_ui and test_oodos_boot.
file(GLOB oodos_utils_deps CONFIGURE_DEPENDS "${OODOS_SRC}/utils/*.s" "${OODOS_SRC}/rsx/*.s" "${OODOS_SRC}/lib/*.s"
     "${OODOS_SRC}/lib/*.inc" "${OODOS_SRC}/lib/mudrv/*" "${OODOS_SRC}/test/gx/*.s" "${OODOS_SRC}/test/pb/*.s"
     "${OODOS_SRC}/test/mu/*.s" "${OODOS_SRC}/test/ovl/*.s" "${OODOS_SRC}/test/check_mkasset.py"
     "${OODOS_SRC}/tools/libzp.py" "${OODOS_SRC}/c/*.s" "${OODOS_SRC}/test/c/*.c" "${OODOS_SRC}/include/oodos.h"
     "${OODOS_SRC}/ui/*.s" "${OODOS_SRC}/ui/*.inc" "${OODOS_SRC}/test/ui/*.s" "${OODOS_SRC}/test/ui/*.cfg"
     "${OODOS_SRC}/uiprog/*.s" "${OODOS_SRC}/cfg/ooui-bnk.cfg" "${OODOS_SRC}/tools/mkbank.py" "${OODOS_SRC}/tools/rules.py"
     "${OODOS_SRC}/demo/*.s" "${OODOS_SRC}/demo/*.cfg"
     "${OODOS_SRC}/../backplane/oohw/*.s" "${OODOS_SRC}/../backplane/oohw/*.inc") # oohw.lib, SYSINFO's
add_custom_command(
        OUTPUT "${OODOS_UTILS_DIR}/utils.stamp"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" utils "BUILD=${OODOS_UTILS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/gx "OOLIB_USERS=playsid view gxtest"
                "AFLAGS=-g -I include -I sys/test" "BUILD=${OODOS_UTILS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
                "${OODOS_UTILS_DIR}/gxtest.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/pb "OOLIB_USERS=playsid view pbtest"
                "AFLAGS=-g -I include -I sys/test" "BUILD=${OODOS_UTILS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
                "${OODOS_UTILS_DIR}/pbtest.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/mu "OOLIB_USERS=playsid view mutest"
                "AFLAGS=-g -I include -I sys/test" "BUILD=${OODOS_UTILS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
                "${OODOS_UTILS_DIR}/mutest.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/ovl "OOLIB_USERS=playsid view sidovl" OVL_LIB=sidplug
                OVL_CALLER=sidovl OVL_ZP=SID_ZP "AFLAGS=-g -I include -I sys/test" "BUILD=${OODOS_UTILS_DIR}"
                "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/sidovl.com" "${OODOS_UTILS_DIR}/sidplug.prl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/ovl "OOLIB_USERS=playsid view sidovl" OVL_LIB=sidplugz
                OVL_CALLER=sidovl "AFLAGS=-g -I include -I sys/test" "BUILD=${OODOS_UTILS_DIR}"
                "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/sidplugz.prl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/ui "OOUI_USERS=uitest uitwid uitbar uitpick"
                "AFLAGS=-g -I include -I sys/test -I test/ui" "BUILD=${OODOS_UTILS_DIR}"
                "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/uitest.com" "${OODOS_UTILS_DIR}/uitwid.com"
                "${OODOS_UTILS_DIR}/uitbar.com" "${OODOS_UTILS_DIR}/uitpick.com" "${OODOS_UTILS_DIR}/OOUI.BNK"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/ui OOUI_USERS=uitmis OOUI_CFG=test/ui/uimis.cfg
                "AFLAGS=-g -I include -I sys/test -I test/ui" "BUILD=${OODOS_UTILS_DIR}"
                "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/uitmis.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=demo OOUI_USERS=uidemo OOUI_CFG=demo/uidemo.cfg
                "BUILD=${OODOS_UTILS_DIR}" "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/uidemo.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/c CPROGRAMS=ctest "BUILD=${OODOS_UTILS_DIR}"
                "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/ctest.com"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SRC}" SRC=test/c CPROGRAMS=ctest "BUILD=${OODOS_UTILS_DIR}/bigstack"
                "CLDFLAGS=-D __STACKSIZE__=0xB000" "PYTHON=${Python3_EXECUTABLE} -I" "${OODOS_UTILS_DIR}/bigstack/ctest.com"
        COMMAND "${Python3_EXECUTABLE}" -I -B "${OODOS_SRC}/test/check_mkasset.py" --write "${OODOS_UTILS_DIR}/assets"
        COMMAND "${CMAKE_COMMAND}" -E touch "${OODOS_UTILS_DIR}/utils.stamp"
        DEPENDS ${oodos_deps} ${oodos_utils_deps}
        VERBATIM)
add_custom_target(oodos_sys ALL DEPENDS "${OODOS_SYS_DIR}/OODOS.SYS")
# OOLIB L1's debug build (`make DEBUG=1`: the hooks' cycles counted), with the test programs, for
# test_oodos_clock_debug.
set(OODOS_SYS_DEBUG_DIR "${CMAKE_CURRENT_BINARY_DIR}/oodos_sys_debug")
add_custom_command(
        OUTPUT "${OODOS_SYS_DEBUG_DIR}/OODOS.SYS" "${OODOS_SYS_DEBUG_DIR}/oodos.lbl"
        COMMAND "${EMU_MAKE}" -s -C "${OODOS_SYS_SRC}" DEBUG=1 "BUILD=${OODOS_SYS_DEBUG_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        DEPENDS ${oodos_deps}
        VERBATIM)
add_custom_target(oodos_sys_debug ALL DEPENDS "${OODOS_SYS_DEBUG_DIR}/OODOS.SYS")
emu_add_test(test_oodos_clock_debug)
set_tests_properties(test_oodos_clock_debug PROPERTIES TIMEOUT 600) # one boot and a few seconds of machine time, under ASan
add_dependencies(test_oodos_clock_debug oodos_sys_debug backplane_bios)
target_compile_definitions(test_oodos_clock_debug PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DEBUG_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
add_custom_target(oodos_utils ALL DEPENDS "${OODOS_UTILS_DIR}/utils.stamp")
target_compile_definitions(test_oodos_sound PRIVATE EMU_MP3_DIR="${CMAKE_CURRENT_LIST_DIR}/mp3") # OOLIB L3's MP3 acceptance case
# OOUI U1's golden frames: a hash of each scene, generated by the emulator from OOUI's code (hw/ooui.md, "Emulator and testing").
target_compile_definitions(test_oodos_ui PRIVATE EMU_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
# D1's golden reports (hw/diag.md): SYSINFO's output, its measurements taken out (test_oodos_sysinfo.cpp).
target_compile_definitions(test_oodos_sysinfo PRIVATE EMU_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/golden")
set_tests_properties(test_oodos_ui PROPERTIES SKIP_RETURN_CODE 77) # OOUI_GOLDEN=update: skipped, never passed
target_link_libraries(test_oodos_sound PRIVATE emu_minimp3)
add_custom_target(oodos_ldr ALL DEPENDS "${OODOS_LDR_DIR}/BOOT.BIN" "${OODOS_PRL_DIR}/hello.prl")
add_dependencies(test_disk2_native backplane_bios)
add_dependencies(test_oodos_sys oodos_sys backplane_bios)
add_dependencies(test_oodos_ccp oodos_sys backplane_bios)
add_dependencies(test_oodos_rsx oodos_sys backplane_bios)
foreach(name ${OODOS_UTILS_SUITES})
    add_dependencies(${name} oodos_sys oodos_utils backplane_bios)
    target_compile_definitions(${name} PRIVATE
            EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
            EMU_OODOS_UTILS_DIR="${OODOS_UTILS_DIR}"
            EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
            EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
            EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
            EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
endforeach()
add_dependencies(test_oodos_boot oodos_sys oodos_ldr oodos_utils backplane_bios)
# test_oodos_dos33 (6d): dos33.py makes, lists and checks its DOS 3.3 images, in a scratch dir of its own.
# test_oodos_dos33_iie: and DOS 3.3 itself, the owner's disk in the emulated IIe.
foreach(name test_oodos_dos33 test_oodos_dos33_iie)
    target_compile_definitions(${name} PRIVATE
            EMU_OODOS_SRC="${OODOS_SRC}"
            EMU_PYTHON="${Python3_EXECUTABLE}"
            EMU_DOS33_WORK="${CMAKE_CURRENT_BINARY_DIR}/dos33_work")
endforeach()
target_compile_definitions(test_oodos_sys PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_ccp PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_rsx PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
target_compile_definitions(test_oodos_boot PRIVATE
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_OODOS_LDR_DIR="${OODOS_LDR_DIR}"
        EMU_OODOS_PRL_DIR="${OODOS_PRL_DIR}"
        EMU_OODOS_UTILS_DIR="${OODOS_UTILS_DIR}"
        EMU_OODOS_UTILS_SRC="${OODOS_SRC}/utils"
        EMU_OODOS_CARD_DIR="${OODOS_SRC}/card")

# --- the books (docs/) ------------------------------------------------------
#
# test_docs: docs/tools/check_docs.py, the books held to oodos.inc and
# backplane.inc (entries, the unwritten list, anchors, links, listings, the
# indexes); test_docs_check: its own cases, each check made to fail on a
# broken copy of the tree. test_docs_examples: each docs/examples program
# (ca65's NAME.s or C's NAME.c), built by docs/examples/Makefile through oodos/Makefile's rules, run from
# the CCP (oodos_rig.h's CcpRig) and its output compared with NAME.out.

get_filename_component(DOCS_SRC "${PROJECT_SOURCE_DIR}/../docs" ABSOLUTE)
add_test(NAME test_docs COMMAND "${Python3_EXECUTABLE}" -I -B "${DOCS_SRC}/tools/check_docs.py")
add_test(NAME test_docs_check COMMAND "${Python3_EXECUTABLE}" -I -B "${DOCS_SRC}/tools/test_check_docs.py")
set_tests_properties(test_docs test_docs_check PROPERTIES TIMEOUT 120)
emu_add_test(test_docs_examples)
set_tests_properties(test_docs_examples PROPERTIES TIMEOUT 600) # a boot and a command a program, under ASan
set(DOCS_EXAMPLES_DIR "${CMAKE_CURRENT_BINARY_DIR}/docs_examples")
file(GLOB docs_examples CONFIGURE_DEPENDS "${DOCS_SRC}/examples/*.s" "${DOCS_SRC}/examples/*.c")
file(GLOB oodos_c_lib CONFIGURE_DEPENDS "${OODOS_SRC}/c/*.s")
set(docs_example_coms)
foreach(src ${docs_examples})
    get_filename_component(name "${src}" NAME_WE)
    list(APPEND docs_example_coms "${DOCS_EXAMPLES_DIR}/${name}.com")
endforeach()
add_custom_command(
        OUTPUT ${docs_example_coms}
        COMMAND "${EMU_MAKE}" -s -C "${DOCS_SRC}/examples" "BUILD=${DOCS_EXAMPLES_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        DEPENDS ${docs_examples} ${oodos_includes} "${DOCS_SRC}/examples/Makefile" "${OODOS_SRC}/Makefile"
                "${OODOS_SRC}/cfg/com.cfg" "${OODOS_SRC}/tools/rules.py" "${OODOS_SRC}/cfg/c.cfg"
                "${OODOS_SRC}/include/oodos.h" "${OODOS_SRC}/tools/oodosh.py" ${oodos_c_lib}
        VERBATIM)
add_custom_target(docs_examples ALL DEPENDS ${docs_example_coms})
add_dependencies(test_docs_examples docs_examples oodos_sys backplane_bios)
target_compile_definitions(test_docs_examples PRIVATE
        EMU_DOCS_EXAMPLES_SRC="${DOCS_SRC}/examples"
        EMU_DOCS_EXAMPLES_BUILD="${DOCS_EXAMPLES_DIR}"
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")

# --- GW-BASIC (hw/oodos.md, "The plan, phase 7") -----------------------------
#
# oodos/gwbasic's Makefile builds GWBASIC.COM and GWBASIC.OVL (the resident
# core and the blocks, through the size gate and the rules gate) into the
# build tree, and checks the translation ledger. test_gwbasic_core runs them
# from OODOS's CCP (oodos_rig.h).

get_filename_component(GWBASIC_SRC "${PROJECT_SOURCE_DIR}/../oodos/gwbasic" ABSOLUTE)
set(GWBASIC_DIR "${CMAKE_CURRENT_BINARY_DIR}/gwbasic")
file(GLOB_RECURSE gwbasic_sources CONFIGURE_DEPENDS "${GWBASIC_SRC}/*.s" "${GWBASIC_SRC}/*.inc"
        "${GWBASIC_SRC}/*.cfg" "${GWBASIC_SRC}/tools/*.py")
list(APPEND gwbasic_sources "${GWBASIC_SRC}/ledger.txt" "${GWBASIC_SRC}/srcnames.txt") # the ledger's check
add_custom_command(
        OUTPUT "${GWBASIC_DIR}/GWBASIC.COM" "${GWBASIC_DIR}/GWBASIC.OVL" "${GWBASIC_DIR}/gwbasic.lbl"
               "${GWBASIC_DIR}/ledger.ok"
        COMMAND "${EMU_MAKE}" -s -C "${GWBASIC_SRC}" "BUILD=${GWBASIC_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
        DEPENDS ${gwbasic_sources} "${GWBASIC_SRC}/Makefile" ${oodos_includes} "${OODOS_SRC}/tools/rules.py"
        VERBATIM)
add_custom_target(gwbasic ALL DEPENDS "${GWBASIC_DIR}/GWBASIC.COM")
emu_add_test(test_gwbasic_core)
set_tests_properties(test_gwbasic_core PROPERTIES TIMEOUT 600)
add_dependencies(test_gwbasic_core gwbasic oodos_sys backplane_bios)
target_compile_definitions(test_gwbasic_core PRIVATE
        EMU_GWBASIC_BUILD="${GWBASIC_DIR}"
        EMU_GWBASIC_PROGRAMS="${CMAKE_CURRENT_LIST_DIR}/gwbasic"
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")
# test_gwbasic_stmts: 7b's statements (block P3), the same way.
emu_add_test(test_gwbasic_stmts)
set_tests_properties(test_gwbasic_stmts PROPERTIES TIMEOUT 600)
add_dependencies(test_gwbasic_stmts gwbasic oodos_sys backplane_bios)
target_compile_definitions(test_gwbasic_stmts PRIVATE
        EMU_GWBASIC_BUILD="${GWBASIC_DIR}"
        EMU_GWBASIC_PROGRAMS="${CMAKE_CURRENT_LIST_DIR}/gwbasic"
        EMU_OODOS_SYS_DIR="${OODOS_SYS_DIR}"
        EMU_SD_DIR="${CMAKE_CURRENT_LIST_DIR}/sd"
        EMU_OODOS_DISKDEFS="${PROJECT_SOURCE_DIR}/../oodos/diskdefs"
        EMU_BIOS_ROM="${BIOS_DIR}/bios.rom"
        EMU_MACHINE_DIR="${PROJECT_SOURCE_DIR}/machines")

# test_gwbasic_math: the math (res/ and p1/) against oodos/gwbasic/test/mbf.py, bit for bit, on the
# W65C02S core alone: the Makefile's flat test image (every module in one image, a far call a JSR;
# test/flat.cfg) and run6502 (test/run6502.cpp), built here, run by test/check.py. No device, and no
# OODOS: about 5 s.
add_executable(gwbasic_run6502 "${GWBASIC_SRC}/test/run6502.cpp")
target_link_libraries(gwbasic_run6502 PRIVATE emu_cpu6502 emu_log)
add_custom_command(
        OUTPUT "${GWBASIC_DIR}/flat.bin" "${GWBASIC_DIR}/flat.map" "${GWBASIC_DIR}/flat.lbl"
        COMMAND "${EMU_MAKE}" -s -C "${GWBASIC_SRC}" "BUILD=${GWBASIC_DIR}" "PYTHON=${Python3_EXECUTABLE} -I"
                "${GWBASIC_DIR}/flat.bin"
        DEPENDS ${gwbasic_sources} "${GWBASIC_SRC}/Makefile" ${oodos_includes}
        VERBATIM)
add_custom_target(gwbasic_flat ALL DEPENDS "${GWBASIC_DIR}/flat.bin" gwbasic_run6502)
add_dependencies(gwbasic_flat gwbasic) # one make at a time in GWBASIC_DIR
add_test(NAME test_gwbasic_math
         COMMAND "${Python3_EXECUTABLE}" -I -B "${GWBASIC_SRC}/test/check.py"
                 --runner $<TARGET_FILE:gwbasic_run6502> --image "${GWBASIC_DIR}/flat.bin")
set_tests_properties(test_gwbasic_math PROPERTIES TIMEOUT 300)
# test_gwbasic_programs: oodos/gwbasic/test/programs/*.bas on the same flat image, through NEWSTT and
# the stand-in PRINT, each against PC-BASIC's output (NAME.out) or the 1983 source's where PC-BASIC
# differs (NAME.differ): the variables, arrays and strings (test/programs.py).
add_test(NAME test_gwbasic_programs
         COMMAND "${Python3_EXECUTABLE}" -I -B "${GWBASIC_SRC}/test/programs.py"
                 --runner $<TARGET_FILE:gwbasic_run6502> --image "${GWBASIC_DIR}/flat.bin")
set_tests_properties(test_gwbasic_programs PROPERTIES TIMEOUT 300)

# --- OODOS: the BDOS's model test --------------------------------------------
#
# oodos/test/: the model of CP/M 3's file system, the generator and the
# comparison (Python), and DRIVER.COM, which runs a script through the BDOS
# (oodos/test/driver's Makefile, through the rules gate). test_oodos_model_py
# is the model's own tests; test_oodos_model runs DRIVER.COM with the model
# as its BDOS, and under OODOS. OODOS_MODEL_BDOS (on) also compares OODOS's
# own BDOS with the model (hw/oodos.md, "The BDOS, phase 3"), on eight seeds
# (about 6 minutes with the rest under ASan); more seeds by hand (oodos/README.md).

option(OODOS_MODEL_BDOS "test_oodos_model: compare OODOS's BDOS with the model" ON)
emu_add_test(test_oodos_model)
set_tests_properties(test_oodos_model PROPERTIES TIMEOUT 1200) # eight seeds of BDOS calls under ASan: about 6 minutes
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

# --- OOLIB's music driver ---------------------------------------------------
#
# oodos/lib/mudrv/mudrv.s, for the card's 6510 (hw/oolib.md, L6a): assembled
# here so test_mudrv runs it on the card. Without cc65 it skips.

emu_add_test(test_mudrv)
set(MUDRV_SRC "${PROJECT_SOURCE_DIR}/../oodos/lib/mudrv")
set(MUDRV_DIR "${CMAKE_CURRENT_BINARY_DIR}/mudrv")
if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${MUDRV_DIR}")
    add_custom_command(
            OUTPUT "${MUDRV_DIR}/mudrv.bin"
            COMMAND "${EMU_CA65}" -g -o "${MUDRV_DIR}/mudrv.o" "${MUDRV_SRC}/mudrv.s"
            COMMAND "${EMU_LD65}" -C "${MUDRV_SRC}/mudrv.cfg" -o "${MUDRV_DIR}/mudrv.bin" "${MUDRV_DIR}/mudrv.o"
            DEPENDS "${MUDRV_SRC}/mudrv.s" "${MUDRV_SRC}/mudrv.cfg"
            VERBATIM)
    add_custom_target(mudrv ALL DEPENDS "${MUDRV_DIR}/mudrv.bin")
    add_dependencies(test_mudrv mudrv)
endif()
target_compile_definitions(test_mudrv PRIVATE EMU_MUDRV="${MUDRV_DIR}/mudrv.bin")

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

# --- the map's own check -----------------------------------------------------
#
# tools/test_map.py check: every path git tracks is in tools/test-map.txt, and the map's labels are the
# suites' (below). Python3 and git are needed; without git (a source tarball) it skips.

find_program(EMU_GIT git)
if(EMU_GIT)
    add_test(NAME test_test_map
             COMMAND "${Python3_EXECUTABLE}" -I -B "${PROJECT_SOURCE_DIR}/../tools/test_map.py" check
                     --build "${CMAKE_BINARY_DIR}")
    set_tests_properties(test_test_map PROPERTIES TIMEOUT 120)
endif()

# --- Labels ------------------------------------------------------------------
#
# What each suite covers (hw/testing.md, items 1-4), for `ctest -L` and tools/check.sh changed, which picks
# the suites a branch's paths choose through tools/test-map.txt. One place: a suite and its labels a line.
# The parts: cpu, bus, machine (the core: a change there runs everything), debug, app (apps/emu), a label a
# device (acia, bridge, console, disk2, dma, es5503, iie, joy, lcd, pia, rtc, sd, sid, sound, usb, via,
# video), backplane (the suite builds machines/backplane.machine, so runs every card on it), and the
# software a suite runs: bios, oodos, apple (the owner's Apple IIe ROMs and disks), apple1,
# eater, docs, and tools (the tiers' own scripts). And one that is a kind, not a part: emu, the emulator's own
# suites, the ones `full` and `changed` run under the sanitizers (never an OODOS, BIOS, Apple-mode or docs
# suite). The device labels were checked against coverage (tools/test_coverage.py, item 7): rerun it when a
# device or a suite is added. Every CTest entry must get a label: configuring fails on one that hasn't.
#
# A split suite's entries all get its labels, and its _cases check `split` too.

set(EMU_TEST_LABELS
    test_cpu_smoke              "emu cpu"
    test_dormann                "emu cpu"
    test_harte                  "emu cpu"
    test_pia                    "emu pia"
    test_via                    "emu via"
    test_acia                   "emu acia"
    test_lcd                    "emu lcd via"
    test_decoder                "emu bus pia via"
    test_machine                "emu machine apple1 acia bridge console dma lcd pia via"
    test_loader                 "emu machine debug"
    test_rom_boot               "emu apple1 pia"
    test_eater                  "emu eater lcd via acia"
    test_disasm                 "emu debug"
    test_debugger               "emu debug pia"
    test_backplane              "emu bridge pia backplane"
    test_ds3231                 "emu rtc"
    test_rtc                    "emu rtc bridge debug backplane"
    test_joyports               "emu joy via acia bridge debug backplane lcd"
    test_bios                   "bios bridge console video sound es5503 sid usb dma sd backplane"
    test_bios_diag              "bios bridge console video sound es5503 sid usb dma sd rtc backplane"
    test_dma                    "emu dma bridge"
    test_console_card           "emu console dma bridge"
    test_video                  "emu video bridge"
    test_video_rev2             "emu video bridge dma debug backplane"
    test_usb                    "emu usb dma bridge"
    test_sound                  "emu sound dma bridge"
    test_sound_pcm              "emu sound dma bridge"
    test_sound_mix              "emu sound"
    test_sound_mp3              "emu sound dma bridge"
    test_sound_eq               "emu sound"
    test_es5503                 "emu es5503"
    test_sound_doc              "emu sound es5503 dma bridge"
    test_sid                    "emu sid sound"
    test_sid_audio              "emu sid sound"
    test_sid_file               "emu sid sound"
    test_sid_corpus             "emu sid sound"
    test_apple                  "emu iie bridge usb"
    test_disk_image             "emu disk2"
    test_disk2                  "emu disk2"
    test_disk2_native           "emu disk2 bridge bios backplane"
    test_disk2_prom             "emu disk2"
    test_session                "emu machine bios bridge console video sound sid es5503 usb dma iie disk2 rtc backplane debug"
    test_sd                     "emu sd dma bios backplane"
    test_sd_fat                 "emu sd"
    test_bios_sd                "bios sd backplane debug"
    test_oodos_card             "oodos bios sd backplane debug"
    test_oodos_boot             "oodos bios sd backplane debug"
    test_oodos_e2e              "oodos bios sd backplane debug"
    test_iie                    "apple iie bios usb video disk2 backplane"
    test_pacing                 "emu app"
    test_oodos_sys              "oodos bios sd usb backplane"
    test_oodos_ccp              "oodos bios sd backplane"
    test_oodos_rsx              "oodos bios sd backplane"
    test_oodos_apple            "oodos bios sd backplane"
    test_oodos_disk2            "oodos bios sd disk2 backplane"
    test_oodos_disk2_media      "oodos bios sd disk2 backplane"
    test_oodos_disk2_noprom     "oodos bios sd disk2 backplane"
    test_oodos_dos33            "oodos bios sd disk2 backplane"
    test_oodos_dos33_iie        "oodos apple bios sd disk2 iie usb backplane"
    test_oodos_err10            "oodos bios sd disk2 backplane"
    test_oodos_utils            "oodos bios sd backplane"
    test_oodos_utils_set        "oodos bios sd backplane"
    test_oodos_utils_files      "oodos bios sd backplane"
    test_oodos_utils_dir        "oodos bios sd backplane"
    test_oodos_utils_sys        "oodos bios sd rtc backplane"
    test_oodos_utils_rom        "oodos bios sd backplane"
    test_oodos_utils_pip        "oodos bios sd disk2 backplane"
    test_oodos_utils_rsx        "oodos bios sd backplane"
    test_oodos_utils_submit     "oodos bios sd backplane"
    test_oodos_clock            "oodos bios sd backplane"
    test_oodos_clock_disk2      "oodos bios sd disk2 backplane"
    test_oodos_clock_debug      "oodos bios sd backplane"
    test_oodos_input            "oodos bios sd usb joy backplane"
    test_oodos_sound            "oodos bios sd sound sid es5503 dma backplane"
    test_oodos_playsid          "oodos bios sd sound sid backplane"
    test_oodos_video            "oodos bios sd video dma backplane"
    test_oodos_load             "oodos bios sd dma sound backplane"
    test_oodos_gfx              "oodos bios sd video dma backplane"
    test_oodos_ui               "oodos bios sd video dma usb backplane"
    test_oodos_pb               "oodos bios sd sound dma backplane"
    test_oodos_mu               "oodos bios sd sound sid backplane"
    test_oodos_c                "oodos bios sd backplane"
    test_oodos_sysinfo          "oodos bios sd video sound usb rtc console backplane"
    test_oodos_model            "oodos bios sd backplane"
    test_oodos_model_py         "oodos"
    test_docs                   "docs"
    test_docs_check             "docs"
    test_docs_examples          "docs oodos bios sd backplane"
    test_gwbasic_core           "gwbasic oodos bios sd backplane"
    test_gwbasic_stmts          "gwbasic oodos bios sd backplane"
    test_gwbasic_math           "gwbasic oodos"
    test_gwbasic_programs       "gwbasic oodos"
    test_sound_driver           "emu sound bridge"
    test_mudrv                  "oodos sound sid bridge"
    test_app_tty                "emu app apple1 debug pia"
    test_app_debug              "emu app apple1 debug backplane pia"
    test_app_eater              "emu app eater acia debug lcd via"
    test_fetch_fixtures         "tools"
    test_trace_diff             "debug"
    test_test_map               "tools")

list(LENGTH EMU_TEST_LABELS n)
math(EXPR last "${n} - 1")
set(labelled)
foreach(i RANGE 0 ${last} 2)
    math(EXPR j "${i} + 1")
    list(GET EMU_TEST_LABELS ${i} suite)
    list(GET EMU_TEST_LABELS ${j} labels)
    separate_arguments(labels UNIX_COMMAND "${labels}")
    get_property(entries GLOBAL PROPERTY EMU_ENTRIES_${suite})
    if(NOT entries)
        if(NOT TEST ${suite})
            if(suite STREQUAL "test_test_map" AND NOT EMU_GIT)
                continue()
            endif()
            message(FATAL_ERROR "emu: labels for ${suite}, which is no suite")
        endif()
        set(entries ${suite})
    endif()
    set_tests_properties(${entries} PROPERTIES LABELS "${labels}")
    if(TEST ${suite}_cases)
        set_tests_properties(${suite}_cases PROPERTIES LABELS "${labels};split")
    endif()
    list(APPEND labelled ${entries})
endforeach()
get_property(all_tests DIRECTORY PROPERTY TESTS)
foreach(t ${all_tests})
    if(NOT t IN_LIST labelled)
        message(FATAL_ERROR "emu: ${t} has no labels: give its suite a line in EMU_TEST_LABELS (\"Labels\")")
    endif()
endforeach()
add_dependencies(test_bios_diag backplane_bios)
