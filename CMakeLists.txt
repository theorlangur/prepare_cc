cmake_minimum_required(VERSION 3.5)

project(prepare_cc CXX)

set(CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC main.cpp analyze_include.cpp generate_header_blocks.cpp compile_commands_processor.cpp log.cpp indexer_preparator.cpp)
set(HDR analyze_include.h generate_header_blocks.h compile_commands_processor.h log.h indexer_preparator.h)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads REQUIRED)
add_executable(prepare_cc ${SRC} ${HDR})
target_link_libraries(prepare_cc PRIVATE Threads::Threads)
set_property(TARGET prepare_cc PROPERTY CXX_STANDARD 17)
