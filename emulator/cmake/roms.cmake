# ROM images assembled from this checkout's sources with cc65 (ca65/ld65),
# for whatever needs them: the tests, and apps/web, which embeds the BIOS.
#
# The backplane BIOS (backplane/bios), as its Makefile builds it: a full
# 512 KB ROM A image with the BIOS and Woz Monitor in the top block, the
# font in block 30 and the video demo in block 29 (the ROM's diagnostics in blocks 21-23,
# with oohw's modules built for the ROM, each block its own copy, the sound card's
# 6510 program in block 22, the flash update (flash.s) in the top block, and the flash's sums written by romsum.py),
# its ld65 -Ln label file, and its two chips' files, bios-even.rom and bios-odd.rom (romsplit.py). Target
# `backplane_bios`; files in EMU_BIOS_DIR.
# Without cc65 there is no target, and what needs it skips (tests) or
# refuses to configure (apps/web).

find_program(EMU_CA65 ca65)
find_program(EMU_LD65 ld65)
find_package(Python3 COMPONENTS Interpreter)

set(EMU_BIOS_SRC "${PROJECT_SOURCE_DIR}/../backplane/bios")
set(EMU_BIOS_DIR "${PROJECT_BINARY_DIR}/roms/bios")
set(EMU_OOHW_SRC "${PROJECT_SOURCE_DIR}/../backplane/oohw")
if(EMU_CA65 AND EMU_LD65 AND Python3_Interpreter_FOUND)
    file(MAKE_DIRECTORY "${EMU_BIOS_DIR}")
    set(objs "")
    file(GLOB demo_assets "${EMU_BIOS_SRC}/demo/*.bin")
    foreach(src bios wozmon video input apple sd demo splash flash diag21 diag22 diag23)
        add_custom_command(
                OUTPUT "${EMU_BIOS_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${EMU_BIOS_SRC}" -I "${EMU_OOHW_SRC}" --bin-include-dir "${EMU_BIOS_DIR}"
                        --bin-include-dir "${EMU_BIOS_SRC}" -o "${EMU_BIOS_DIR}/${src}.o" "${EMU_BIOS_SRC}/${src}.s"
                DEPENDS "${EMU_BIOS_SRC}/${src}.s" "${EMU_BIOS_SRC}/backplane.inc" "${EMU_BIOS_SRC}/font/vga8x16.bin"
                        "${EMU_BIOS_SRC}/diag.inc" "${EMU_BIOS_SRC}/diagcon.inc" "${EMU_BIOS_SRC}/diagtest.inc"
                        "${EMU_BIOS_SRC}/diagsnd.inc" "${EMU_OOHW_SRC}/oohw.inc"
                        ${demo_assets}
                VERBATIM)
        list(APPEND objs "${EMU_BIOS_DIR}/${src}.o")
    endforeach()
    # oohw's modules, for the ROM: each block its own copy (the Makefile's HWOBJS), block 23's under the plain
    # names, block 22's and 21's with their number in each name (OOHW_BLK, oohw.inc).
    foreach(pair "-:hwmem" "-:hwzp" "22:hwprobe" "22:hwclock" "22:hwfmt" "22:hwmem" "22:hwport" "22:hwzp"
                 "21:hwprobe" "21:hwclock" "21:hwfmt" "21:hwsdid" "21:hwzp")
        string(REPLACE ":" ";" parts "${pair}")
        list(GET parts 0 blk)
        list(GET parts 1 src)
        if(blk STREQUAL "-")
            set(obj "hw-${src}.o")
            set(blkdef "")
        else()
            set(obj "hw${blk}-${src}.o")
            set(blkdef "-DOOHW_BLK=${blk}")
        endif()
        add_custom_command(
                OUTPUT "${EMU_BIOS_DIR}/${obj}"
                COMMAND "${EMU_CA65}" -g -I "${EMU_BIOS_SRC}" -I "${EMU_OOHW_SRC}" -D OOHW_ROM ${blkdef}
                        -o "${EMU_BIOS_DIR}/${obj}" "${EMU_OOHW_SRC}/${src}.s"
                DEPENDS "${EMU_OOHW_SRC}/${src}.s" "${EMU_OOHW_SRC}/oohw.inc" "${EMU_BIOS_SRC}/backplane.inc"
                VERBATIM)
        list(APPEND objs "${EMU_BIOS_DIR}/${obj}")
    endforeach()
    add_custom_command(
            OUTPUT "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_DIR}/bios.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${EMU_BIOS_SRC}/bios.cfg" -o "${EMU_BIOS_DIR}/bios.rom.tmp"
                    -Ln "${EMU_BIOS_DIR}/bios.rom.lbl" ${objs}
            COMMAND "${Python3_EXECUTABLE}" -I "${EMU_BIOS_SRC}/romsum.py" "${EMU_BIOS_DIR}/bios.rom.tmp"
                    "${EMU_BIOS_DIR}/bios.rom.lbl"
            COMMAND "${CMAKE_COMMAND}" -E rename "${EMU_BIOS_DIR}/bios.rom.tmp" "${EMU_BIOS_DIR}/bios.rom"
            DEPENDS ${objs} "${EMU_BIOS_SRC}/bios.cfg" "${EMU_BIOS_SRC}/romsum.py"
            VERBATIM)
    # ROM A's two chips, a file each, as the Makefile's romsplit.py writes them: the even bytes and the odd.
    add_custom_command(
            OUTPUT "${EMU_BIOS_DIR}/bios-even.rom" "${EMU_BIOS_DIR}/bios-odd.rom"
            COMMAND "${Python3_EXECUTABLE}" -I "${EMU_BIOS_SRC}/romsplit.py" "${EMU_BIOS_DIR}/bios.rom"
                    "${EMU_BIOS_DIR}/bios-even.rom" "${EMU_BIOS_DIR}/bios-odd.rom"
            DEPENDS "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_SRC}/romsplit.py"
            VERBATIM)
    add_custom_target(backplane_bios ALL DEPENDS "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_DIR}/bios-even.rom"
            "${EMU_BIOS_DIR}/bios-odd.rom")
else()
    message(STATUS "emu: ca65/ld65 (or Python 3) not found — no backplane BIOS")
endif()
