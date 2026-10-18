add_executable(emulator main.cpp emulator.cpp terminal.cpp)
target_link_libraries(emulator PRIVATE emu_cpu6502 emu_machine emu_log)
emu_house_flags(emulator)
