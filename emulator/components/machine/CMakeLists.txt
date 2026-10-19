# machines/apple1.machine is compiled in as the app's default machine. Read
# here and re-configured when it changes, so the file stays the one source.
set(APPLE1_MACHINE_FILE "${PROJECT_SOURCE_DIR}/machines/apple1.machine")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${APPLE1_MACHINE_FILE}")
file(READ "${APPLE1_MACHINE_FILE}" APPLE1_MACHINE_TEXT)
# file(CONFIGURE) only rewrites the header when the text changed, so an
# unrelated reconfigure does not rebuild apple1.cpp.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/apple1_machine.inc"
     CONTENT "const char APPLE1_MACHINE[] = R\"MACHINE(${APPLE1_MACHINE_TEXT})MACHINE\";\n" @ONLY)

add_library(emu_machine STATIC apple1.cpp console.cpp loader.cpp machine.cpp)
target_include_directories(emu_machine PUBLIC include PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")
target_link_libraries(emu_machine PUBLIC emu_bus emu_devices PRIVATE emu_log)
emu_house_flags(emu_machine)
