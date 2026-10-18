add_library(emu_cpu6502 STATIC cpu6502.cpp)
target_include_directories(emu_cpu6502 PUBLIC include)
target_link_libraries(emu_cpu6502 PUBLIC emu_bus PRIVATE emu_log)
emu_house_flags(emu_cpu6502)
