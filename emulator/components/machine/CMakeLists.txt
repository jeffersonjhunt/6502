add_library(emu_machine STATIC apple1.cpp loader.cpp)
target_include_directories(emu_machine PUBLIC include)
target_link_libraries(emu_machine PUBLIC emu_bus PRIVATE emu_log)
emu_house_flags(emu_machine)
