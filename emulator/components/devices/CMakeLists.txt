add_library(emu_devices STATIC acia6551.cpp backplane_bridge.cpp console_card.cpp dma_controller.cpp hd44780.cpp memory_devices.cpp pia6821.cpp ps2_keyboard.cpp via6522.cpp video_card.cpp)
target_include_directories(emu_devices PUBLIC include)
target_link_libraries(emu_devices PUBLIC emu_bus PRIVATE emu_log)
emu_house_flags(emu_devices)
