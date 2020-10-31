cmake_minimum_required(VERSION 3.5)

project(prepare_cc CXX)

set(CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(prepare_cc main.cpp analyze_include.cpp generate_header_blocks.cpp compile_commands_processor.cpp)
set_property(TARGET prepare_cc PROPERTY CXX_STANDARD 17)
