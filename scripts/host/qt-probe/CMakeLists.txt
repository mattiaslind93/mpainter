# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.25)
project(qt_gpu_environment_probe LANGUAGES CXX)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Widgets OpenGLWidgets)
add_executable(qt-gpu-probe main.cc)
target_link_libraries(qt-gpu-probe PRIVATE Qt6::Widgets Qt6::OpenGLWidgets)
target_compile_features(qt-gpu-probe PRIVATE cxx_std_20)
