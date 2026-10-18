add_library(emu_log STATIC emu_log.cpp)
target_include_directories(emu_log PUBLIC include)
emu_house_flags(emu_log)
