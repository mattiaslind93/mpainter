# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.31)
project(mpainter_windows_toolchain_probe LANGUAGES C CXX)
add_executable(toolchain-probe ../probe.cc)
target_compile_features(toolchain-probe PRIVATE cxx_std_20)
