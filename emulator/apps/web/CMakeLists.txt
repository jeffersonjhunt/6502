# The WebUI's core in WebAssembly: Session (components/session) behind
# embind (bindings.cpp), as an ES module, oo1.mjs + oo1.wasm, that runs in
# a browser or node. Built with the `web` preset, in the emsdk image
# (Dockerfile, tools/web_build.sh).
#
# The backplane machine and BIOS are built into the module's file system,
# so it boots with no fetches: /machines/backplane.machine, /roms/bios.rom
# and /roms/bios.rom.lbl. The BIOS needs cc65, so without it this refuses
# to configure rather than build a module with nothing to run.

if(NOT TARGET backplane_bios)
    message(FATAL_ERROR "apps/web embeds the backplane BIOS, which needs ca65/ld65 (cc65)")
endif()

add_executable(oo1 bindings.cpp)
set_target_properties(oo1 PROPERTIES SUFFIX ".mjs")
target_link_libraries(oo1 PRIVATE emu_session emu_log)
emu_house_flags(oo1)
add_dependencies(oo1 backplane_bios)
target_link_options(oo1 PRIVATE
        -lembind
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createOO1
        -sENVIRONMENT=web,node
        -sALLOW_MEMORY_GROWTH=1
        -sEMBIND_STD_STRING_IS_UTF8=0
        -sEXPORTED_RUNTIME_METHODS=FS
        "--embed-file=${PROJECT_SOURCE_DIR}/machines/backplane.machine@/machines/backplane.machine"
        "--embed-file=${EMU_BIOS_DIR}/bios.rom@/roms/bios.rom"
        "--embed-file=${EMU_BIOS_DIR}/bios.rom.lbl@/roms/bios.rom.lbl")
# Relink when an embedded file changes.
set_property(TARGET oo1 APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/machines/backplane.machine" "${EMU_BIOS_DIR}/bios.rom" "${EMU_BIOS_DIR}/bios.rom.lbl")

if(EMU_BUILD_TESTS)
    add_test(NAME web_boot
             COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} "${CMAKE_CURRENT_LIST_DIR}/test/boot.mjs" $<TARGET_FILE:oo1>)
    set_tests_properties(web_boot PROPERTIES TIMEOUT 120)
endif()
