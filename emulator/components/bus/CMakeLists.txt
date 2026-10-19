add_library(emu_bus STATIC decoder.cpp)
target_include_directories(emu_bus PUBLIC include)
target_link_libraries(emu_bus PRIVATE emu_log)
emu_house_flags(emu_bus)
