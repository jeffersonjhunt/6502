# The WebUI's core: a machine run a budget of cycles at a time (session.h).
# No terminal, clock or Emscripten code, so it builds and tests on the host.
add_library(emu_session STATIC session.cpp)
target_include_directories(emu_session PUBLIC include)
target_link_libraries(emu_session PUBLIC emu_machine emu_devices emu_debug emu_cpu6502 PRIVATE emu_log)
emu_house_flags(emu_session)
