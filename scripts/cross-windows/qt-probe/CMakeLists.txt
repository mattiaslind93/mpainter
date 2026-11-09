# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.31)
project(mpainter_windows_qt_probe LANGUAGES CXX)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Widgets)
add_executable(qt-probe main.cc)
target_link_libraries(qt-probe PRIVATE Qt6::Widgets)
