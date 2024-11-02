add_executable(emulator emulator.cpp)

target_link_libraries(emulator PRIVATE fmt::fmt)

target_compile_features(emulator PRIVATE cxx_std_17)

target_link_libraries(emulator PRIVATE mos6502)
