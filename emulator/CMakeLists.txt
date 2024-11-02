cmake_minimum_required(VERSION 3.16)

project(emulator)

find_package(fmt REQUIRED)

# The compiled library code is here
add_subdirectory(src)

# The executable code is here
add_subdirectory(apps)
