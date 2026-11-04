cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(bts_spsm_reader LANGUAGES C CXX)
  include(CTest)
endif()

# Independent worker-side library; no external vendor SDK, Qt, Blender or graph runtime.
# HDF5's config package accepts only its own minor series for versioned
# requests. Discover first, then enforce our minimum so 1.14 is not rejected
# in favor of a host h5cc while cross-compiling a 1.12-or-newer consumer.
find_package(HDF5 REQUIRED COMPONENTS C)
if(HDF5_VERSION VERSION_LESS 1.12)
  message(FATAL_ERROR "The SPSM reader requires HDF5 1.12 or newer")
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
