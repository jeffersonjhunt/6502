add_library(emu_debug STATIC bus_trace.cpp debugger.cpp disasm.cpp symbols.cpp)
target_include_directories(emu_debug PUBLIC include)
target_link_libraries(emu_debug PUBLIC emu_cpu6502 emu_bus PRIVATE emu_log)
emu_house_flags(emu_debug)
