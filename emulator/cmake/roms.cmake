# ROM images assembled from this checkout's sources with cc65 (ca65/ld65),
# for whatever needs them: the tests, and apps/web, which embeds the BIOS.
#
# The backplane BIOS (backplane/bios), as its Makefile builds it: a full
# 512 KB ROM A image with the BIOS and Woz Monitor in the top block, the
# font in block 30 and the video demo in block 29 (and the boot splash's
# players, splash/bootsnd.bin and splash/docsnd.bin, and its samples in blocks
# 24-28, splash/wave.bin, all committed; the ROM's diagnostics in blocks 21-23,
# with oohw's memory tests built for the ROM and the flash's sums written by
# romsum.py), and its ld65 -Ln label file. Target `backplane_bios`; files in EMU_BIOS_DIR.
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
    foreach(src bios wozmon video input apple sd demo splash diag21 diag22 diag23)
        add_custom_command(
                OUTPUT "${EMU_BIOS_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${EMU_BIOS_SRC}" -I "${EMU_OOHW_SRC}" --bin-include-dir "${EMU_BIOS_SRC}"
                        -o "${EMU_BIOS_DIR}/${src}.o" "${EMU_BIOS_SRC}/${src}.s"
                DEPENDS "${EMU_BIOS_SRC}/${src}.s" "${EMU_BIOS_SRC}/backplane.inc" "${EMU_BIOS_SRC}/font/vga8x16.bin"
                        "${EMU_BIOS_SRC}/diag.inc" "${EMU_BIOS_SRC}/diagcon.inc" "${EMU_OOHW_SRC}/oohw.inc"
                        ${demo_assets} "${EMU_BIOS_SRC}/splash/bootsnd.bin" "${EMU_BIOS_SRC}/splash/docsnd.bin"
                        "${EMU_BIOS_SRC}/splash/wave.bin"
                VERBATIM)
        list(APPEND objs "${EMU_BIOS_DIR}/${src}.o")
    endforeach()
    foreach(src hwmem hwzp) # oohw's, for the ROM: block 23's memory tests
        add_custom_command(
                OUTPUT "${EMU_BIOS_DIR}/hw-${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${EMU_BIOS_SRC}" -I "${EMU_OOHW_SRC}" -D OOHW_ROM
                        -o "${EMU_BIOS_DIR}/hw-${src}.o" "${EMU_OOHW_SRC}/${src}.s"
                DEPENDS "${EMU_OOHW_SRC}/${src}.s" "${EMU_OOHW_SRC}/oohw.inc" "${EMU_BIOS_SRC}/backplane.inc"
                VERBATIM)
        list(APPEND objs "${EMU_BIOS_DIR}/hw-${src}.o")
    endforeach()
    add_custom_command(
            OUTPUT "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_DIR}/bios.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${EMU_BIOS_SRC}/bios.cfg" -o "${EMU_BIOS_DIR}/bios.rom"
                    -Ln "${EMU_BIOS_DIR}/bios.rom.lbl" ${objs}
            COMMAND "${Python3_EXECUTABLE}" -I "${EMU_BIOS_SRC}/romsum.py" "${EMU_BIOS_DIR}/bios.rom"
            DEPENDS ${objs} "${EMU_BIOS_SRC}/bios.cfg" "${EMU_BIOS_SRC}/romsum.py"
            VERBATIM)
    add_custom_target(backplane_bios ALL DEPENDS "${EMU_BIOS_DIR}/bios.rom")
else()
    message(STATUS "emu: ca65/ld65 (or Python 3) not found — no backplane BIOS")
endif()
