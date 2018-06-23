cmake_minimum_required(VERSION 3.7)
project(UDP_Port_Scan_Server)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES main.cpp)
add_executable(UDP_Port_Scan_Server ${SOURCE_FILES})