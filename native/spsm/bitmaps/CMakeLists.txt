cmake_minimum_required(VERSION 3.20)
# The version built from source when the system has no lz4. Matches what the
# macOS dependency tree already compiles, so every platform ships the same lz4.
set(BTS_LZ4_VERSION 1.10.0)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(bts_spsm_bitmaps LANGUAGES C CXX)
  include(CTest)
  add_subdirectory(.. reader)
endif()
find_path(LZ4_INCLUDE_DIR lz4.h)
find_library(LZ4_LIBRARY NAMES lz4)
if(NOT LZ4_INCLUDE_DIR OR NOT LZ4_LIBRARY)
  # lz4 is the one dependency of ours that neither Blender's pinned libraries
  # nor every platform's package manager hands us, and a missing lz4.h used to
  # stop the whole configure with nothing built. Compile the pinned upstream
  # source instead: the block API this library uses lives entirely in lib/lz4.c,
  # so it costs a second and no system package.
  message(STATUS "lz4 not found on this system; building pinned lz4 ${BTS_LZ4_VERSION}")
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  include(FetchContent)
  FetchContent_Declare(bts_lz4
    URL https://github.com/lz4/lz4/archive/refs/tags/v${BTS_LZ4_VERSION}.tar.gz
    URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b)
  FetchContent_MakeAvailable(bts_lz4)
  add_library(bts-lz4 STATIC ${bts_lz4_SOURCE_DIR}/lib/lz4.c)
  set_target_properties(bts-lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_include_directories(bts-lz4 PUBLIC ${bts_lz4_SOURCE_DIR}/lib)
  set(LZ4_INCLUDE_DIR ${bts_lz4_SOURCE_DIR}/lib)
  set(LZ4_LIBRARY bts-lz4)
endif()
# Mirrors bts-hdf5 next door: a named target carrying whichever lz4 this build
# settled on, the system one or the pinned source, so another directory can
# borrow it without reaching for variables that only exist in this one.
add_library(bts-lz4-usage INTERFACE)
target_include_directories(bts-lz4-usage INTERFACE ${LZ4_INCLUDE_DIR})
target_link_libraries(bts-lz4-usage INTERFACE ${LZ4_LIBRARY})

add_library(bts-spsm-bitmaps STATIC SpsmBitmap.cc)
target_compile_features(bts-spsm-bitmaps PUBLIC cxx_std_20)
target_include_directories(bts-spsm-bitmaps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${LZ4_INCLUDE_DIR})
target_link_libraries(bts-spsm-bitmaps PUBLIC bts-spsm-reader PRIVATE ${LZ4_LIBRARY})
if(BUILD_TESTING)
  add_executable(bts-spsm-bitmap-tests ../../tests/spsm_bitmap_test.cc)
  target_include_directories(bts-spsm-bitmap-tests PRIVATE ${LZ4_INCLUDE_DIR})
  target_link_libraries(bts-spsm-bitmap-tests PRIVATE bts-spsm-bitmaps ${LZ4_LIBRARY} bts-hdf5)
  add_test(NAME spsm-bitmap-boundaries COMMAND bts-spsm-bitmap-tests)
  if(BTS_SPSM_FIXTURE_DIR)
    add_test(NAME spsm-eight-raw-images COMMAND bts-spsm-bitmap-tests ${BTS_SPSM_FIXTURE_DIR})
  endif()
endif()
