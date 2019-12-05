cmake_minimum_required(VERSION 3.8)

find_package(PythonInterp 3.7 REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_BUILD_TYPE RelWithDebInfo)

project(afl37)
add_subdirectory(pybind11)
pybind11_add_module(afl37 afl37.cpp)
