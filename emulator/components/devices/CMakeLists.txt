add_library(emu_devices STATIC memory_devices.cpp pia6821.cpp)
target_include_directories(emu_devices PUBLIC include)
target_link_libraries(emu_devices PUBLIC emu_bus PRIVATE emu_log)
emu_house_flags(emu_devices)
