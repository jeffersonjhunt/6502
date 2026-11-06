add_library(emu_devices STATIC acia6551.cpp apple_io.cpp backplane_bridge.cpp console_card.cpp ds3231.cpp disk2_card.cpp es5503.cpp disk_image.cpp fat_volume.cpp dma_controller.cpp hd44780.cpp joystick_ports.cpp memory_devices.cpp pia6821.cpp ps2_keyboard.cpp sid.cpp sid_file.cpp sd_card.cpp sd_media.cpp sound_card.cpp sound_dsp.cpp usb_card.cpp via6522.cpp video_card.cpp)
target_include_directories(emu_devices PUBLIC include)
target_link_libraries(emu_devices PUBLIC emu_bus emu_cpu6502 PRIVATE emu_log)
emu_house_flags(emu_devices)
# The sound's floats the same on every target: no fused multiply-adds, which g++ makes on aarch64 at -O2 and
# clang within expressions, and WebAssembly and -O0 never do (test_sound_mix's golden hash is one value). That
# holds given the same libm (std::pow, std::sin, std::exp): glibc and Emscripten's agree today; another may not.
# sound_dsp.cpp (the EQ and the vis page) uses no libm but floor, fabs and ldexp, which are exact.
set_source_files_properties(sid.cpp sound_card.cpp sound_dsp.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# minimp3 (third_party/minimp3, CC0), the sound card's MP3 codec: built as it comes, with the house flags but
# not -Wconversion, whose int-to-byte narrowings in its own code are its authors' to answer for. Clang's
# -Wshorten-64-to-32 is on for it explicitly (WebAssembly's build): it has none of those.
add_library(emu_minimp3 STATIC minimp3.cpp)
target_include_directories(emu_minimp3 PUBLIC ${PROJECT_SOURCE_DIR}/third_party/minimp3)
target_compile_options(emu_minimp3 PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Werror -ffp-contract=off
        -ftrivial-auto-var-init=zero) # its scratch on the stack read past the data in bad frames: zero, not stale
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(emu_minimp3 PRIVATE -Wshorten-64-to-32)
endif()
target_link_libraries(emu_devices PRIVATE emu_minimp3)
