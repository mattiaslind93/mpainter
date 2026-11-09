# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(bts_spsm_reader LANGUAGES C CXX)
  include(CTest)
endif()

# Independent worker-side library; no external vendor SDK, Qt, Blender or graph runtime.
# HDF5's config package accepts only its own minor series for versioned
# requests. Discover first, then enforce our minimum so 1.14 is not rejected
# in favor of a host h5cc while cross-compiling a 1.12-or-newer consumer.
set(BTS_HDF5_VERSION 1.14.6)
find_package(HDF5 COMPONENTS C)
if(NOT HDF5_FOUND OR HDF5_VERSION VERSION_LESS 1.12)
  # The reader calls H5Lget_info1 and its relatives, which arrived in 1.12.
  # Ubuntu 24.04 still packages 1.10, so a release build there used to stop
  # here with nothing built. Compile the pinned upstream source instead; it is
  # the same archive the macOS and Windows dependency trees already use, and a
  # new enough system HDF5 is still preferred and still used where it exists.
  message(STATUS "HDF5 ${HDF5_VERSION} is unusable here; building pinned HDF5 ${BTS_HDF5_VERSION}")
  if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
  endif()
  include(FetchContent)
  # HDF5 reads these as ordinary variables and would otherwise build its tools,
  # examples, language bindings and test suite into our build. BUILD_TESTING is
  # ours as well as theirs, so it goes back to what it was straight afterwards.
  set(BTS_BUILD_TESTING_WAS ${BUILD_TESTING})
  set(BUILD_TESTING OFF)
  set(BUILD_STATIC_LIBS ON)
  set(BUILD_SHARED_LIBS OFF)
  set(HDF5_BUILD_TOOLS OFF)
  set(HDF5_BUILD_EXAMPLES OFF)
  set(HDF5_BUILD_UTILS OFF)
  set(HDF5_BUILD_CPP_LIB OFF)
  set(HDF5_BUILD_FORTRAN OFF)
  set(HDF5_BUILD_HL_LIB OFF)
  set(HDF5_ENABLE_Z_LIB_SUPPORT OFF)
  set(HDF5_ENABLE_SZIP_SUPPORT OFF)
  set(HDF5_NO_PACKAGES ON)
  set(HDF5_EXTERNALLY_CONFIGURED ON)
  # HDF5 writes the compiler flags it was configured with into a C string in a
  # generated source file, without escaping them. Blender's build leaves
  # -fmacro-prefix-map="/path/"="" in the flags for every subdirectory of ours,
  # and those quotes close that string early: the path spills out as bare words
  # and the file stops compiling. The quotes carry no meaning to the compiler
  # here, so drop them for this subproject only and put ours back afterwards.
  set(BTS_C_FLAGS_WAS "${CMAKE_C_FLAGS}")
  set(BTS_CXX_FLAGS_WAS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "\"" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  string(REPLACE "\"" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  FetchContent_Declare(bts_hdf5
    URL https://github.com/HDFGroup/hdf5/archive/refs/tags/hdf5_${BTS_HDF5_VERSION}.tar.gz
    URL_HASH SHA256=09ee1c671a87401a5201c06106650f62badeea5a3b3941e9b1e2e1e08317357f)
  FetchContent_MakeAvailable(bts_hdf5)
  set(CMAKE_C_FLAGS "${BTS_C_FLAGS_WAS}")
  set(CMAKE_CXX_FLAGS "${BTS_CXX_FLAGS_WAS}")
  set(BUILD_TESTING ${BTS_BUILD_TESTING_WAS})
  set(HDF5_LIBRARIES hdf5-static)
  # hdf5.h includes H5FDsubfiling.h unconditionally, and that header sits in a
  # subdirectory of src rather than beside it; HDF5's own build adds the
  # subdirectory to its include path, so anything of ours that includes hdf5.h
  # needs it too.
  set(HDF5_INCLUDE_DIRS ${bts_hdf5_SOURCE_DIR}/src
    ${bts_hdf5_SOURCE_DIR}/src/H5FDsubfiling ${bts_hdf5_BINARY_DIR}/src)
endif()
find_package(Threads REQUIRED)
# Direct HDF5 consumers need its include paths and DLL import definitions as
# well as the library; keep those requirements available to embedded fixtures.
add_library(bts-hdf5 INTERFACE)
target_include_directories(bts-hdf5 INTERFACE ${HDF5_INCLUDE_DIRS})
target_link_libraries(bts-hdf5 INTERFACE ${HDF5_LIBRARIES})
add_library(bts-spsm-reader STATIC SpsmReader.cc)
target_compile_features(bts-spsm-reader PUBLIC cxx_std_20)
target_include_directories(bts-spsm-reader PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bts-spsm-reader PRIVATE bts-hdf5 Threads::Threads)

if(BUILD_TESTING)
  add_executable(bts-spsm-tests ../tests/spsm_reader_test.cc)
  target_link_libraries(bts-spsm-tests PRIVATE bts-spsm-reader bts-hdf5)
  add_test(NAME spsm-boundaries COMMAND bts-spsm-tests)
  add_test(NAME spsm-parallel-boundaries COMMAND bts-spsm-tests --parallel)
  set(BTS_SPSM_FIXTURE_DIR "" CACHE PATH "Optional directory containing the five user SPSM fixtures")
  if(BTS_SPSM_FIXTURE_DIR)
    add_test(NAME spsm-five-user-files COMMAND bts-spsm-tests ${BTS_SPSM_FIXTURE_DIR})
    add_test(NAME spsm-five-user-files-parallel COMMAND bts-spsm-tests --parallel ${BTS_SPSM_FIXTURE_DIR})
  endif()
endif()
