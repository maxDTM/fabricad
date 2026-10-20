# SPDX-License-Identifier: LGPL-2.1-or-later
# Stand-alone measurement tool for docs/comparison (not part of the FabriCAD build).
#   cmake -S docs/comparison/tools -B <scratch>/stlcheck-build -DCMAKE_BUILD_TYPE=Release && cmake --build <scratch>/stlcheck-build -j2
cmake_minimum_required(VERSION 3.20)
project(stlcheck CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(OpenCASCADE REQUIRED)
find_package(Threads REQUIRED)
add_executable(stlcheck stlcheck.cpp)
target_include_directories(stlcheck PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(stlcheck PRIVATE TKDESTEP TKXSBase TKBRep TKTopAlgo TKGeomBase TKG3d TKMath TKernel Threads::Threads)
