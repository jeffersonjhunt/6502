# ROM images assembled from this checkout's sources with cc65 (ca65/ld65),
# for whatever needs them: the tests, and apps/web, which embeds the BIOS.
#
# The backplane BIOS (backplane/bios), as its Makefile builds it: a full
# 512 KB ROM A image with the BIOS and Woz Monitor in the top block, the
# font in block 30 and the video demo in block 29, and its ld65 -Ln label file. Target `backplane_bios`; files in EMU_BIOS_DIR.
# Without cc65 there is no target, and what needs it skips (tests) or
# refuses to configure (apps/web).

find_program(EMU_CA65 ca65)
find_program(EMU_LD65 ld65)

set(EMU_BIOS_SRC "${PROJECT_SOURCE_DIR}/../backplane/bios")
set(EMU_BIOS_DIR "${PROJECT_BINARY_DIR}/roms/bios")
if(EMU_CA65 AND EMU_LD65)
    file(MAKE_DIRECTORY "${EMU_BIOS_DIR}")
    set(objs "")
    file(GLOB demo_assets "${EMU_BIOS_SRC}/demo/*.bin")
    foreach(src bios wozmon video input demo)
        add_custom_command(
                OUTPUT "${EMU_BIOS_DIR}/${src}.o"
                COMMAND "${EMU_CA65}" -g -I "${EMU_BIOS_SRC}" --bin-include-dir "${EMU_BIOS_SRC}"
                        -o "${EMU_BIOS_DIR}/${src}.o" "${EMU_BIOS_SRC}/${src}.s"
                DEPENDS "${EMU_BIOS_SRC}/${src}.s" "${EMU_BIOS_SRC}/backplane.inc" "${EMU_BIOS_SRC}/font/vga8x16.bin"
                        ${demo_assets}
                VERBATIM)
        list(APPEND objs "${EMU_BIOS_DIR}/${src}.o")
    endforeach()
    add_custom_command(
            OUTPUT "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_DIR}/bios.rom.lbl"
            COMMAND "${EMU_LD65}" -C "${EMU_BIOS_SRC}/bios.cfg" -o "${EMU_BIOS_DIR}/bios.rom"
                    -Ln "${EMU_BIOS_DIR}/bios.rom.lbl" ${objs}
            DEPENDS ${objs} "${EMU_BIOS_SRC}/bios.cfg"
            VERBATIM)
    add_custom_target(backplane_bios ALL DEPENDS "${EMU_BIOS_DIR}/bios.rom")
else()
    message(STATUS "emu: ca65/ld65 not found — no backplane BIOS")
endif()
