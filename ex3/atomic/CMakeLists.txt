cmake_minimum_required(VERSION 3.15)
project(atomic)

set(CMAKE_CXX_STANDARD 14)

add_executable(atomic atomicdemo.cpp atomicdemo_multivar.cpp)