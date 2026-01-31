cmake_minimum_required(VERSION 3.10)
project(lab_4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_BUILD_TYPE Debug)

find_package(Threads REQUIRED)

add_executable(emulator emulator.cpp)
target_link_libraries(emulator Threads::Threads)

add_executable(sensor_logger main.cpp)
target_link_libraries(sensor_logger Threads::Threads)