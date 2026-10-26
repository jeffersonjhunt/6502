add_library(emu_devices STATIC acia6551.cpp apple_io.cpp backplane_bridge.cpp console_card.cpp disk2_card.cpp disk_image.cpp dma_controller.cpp hd44780.cpp memory_devices.cpp pia6821.cpp ps2_keyboard.cpp sid.cpp sid_file.cpp sd_card.cpp sd_media.cpp sound_card.cpp usb_card.cpp via6522.cpp video_card.cpp)
target_include_directories(emu_devices PUBLIC include)
target_link_libraries(emu_devices PUBLIC emu_bus emu_cpu6502 PRIVATE emu_log)
emu_house_flags(emu_devices)
