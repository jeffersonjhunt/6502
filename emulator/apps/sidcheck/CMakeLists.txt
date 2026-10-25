add_executable(sidcheck main.cpp)
target_link_libraries(sidcheck PRIVATE emu_debug emu_devices)
emu_house_flags(sidcheck)
