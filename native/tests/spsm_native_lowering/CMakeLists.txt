cmake_minimum_required(VERSION 3.20)
project(bts_spsm_native_lowering LANGUAGES C CXX)
include(CTest)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL)
add_subdirectory(../../spsm reader)
add_subdirectory(../../spsm/semantic semantic)
add_library(bts-spsm-native-lowering STATIC ../../host/SpsmMaterialImport.cc
  ../../host/PaintPixels.cc ../../host/StrokeMetadata.cc
  ../../host/StrokeHistory.cc ../../host/SurfacePathIO.cc)
target_link_libraries(bts-spsm-native-lowering PUBLIC bts-spsm-semantic Qt6::Widgets Qt6::OpenGL)
target_include_directories(bts-spsm-native-lowering PUBLIC ../../host)
add_executable(bts-spsm-native-lowering-tests ../spsm_native_lowering_test.cc)
target_link_libraries(bts-spsm-native-lowering-tests PRIVATE bts-spsm-native-lowering)
add_test(NAME spsm-native-lowering-boundaries COMMAND bts-spsm-native-lowering-tests)
if(BTS_SPSM_FIXTURE_DIR)
 add_test(NAME spsm-native-lowering-five-files COMMAND bts-spsm-native-lowering-tests ${BTS_SPSM_FIXTURE_DIR})
endif()
set(BTS_NATIVE_EXPORT_LIBRARY "" CACHE FILEPATH "Optional matching production libbts-export-config.a for real apply/codec proof")
if(BTS_NATIVE_EXPORT_LIBRARY)
 find_package(ZLIB REQUIRED)
 add_executable(bts-spsm-native-apply-tests ../spsm_native_apply_test.cc)
 target_link_libraries(bts-spsm-native-apply-tests PRIVATE bts-spsm-native-lowering ${BTS_NATIVE_EXPORT_LIBRARY} ZLIB::ZLIB)
 if(BTS_SPSM_FIXTURE_DIR)
  add_test(NAME spsm-native-apply-five-files COMMAND bts-spsm-native-apply-tests ${BTS_SPSM_FIXTURE_DIR})
 endif()
endif()

# Native import defaults are exercised without a Blender/GPU runtime.
add_executable(bts-spsm-bitmap-options-tests ../spsm_bitmap_options_test.cc)
target_link_libraries(bts-spsm-bitmap-options-tests PRIVATE Qt6::Widgets)
target_include_directories(bts-spsm-bitmap-options-tests PRIVATE ../../host)
add_test(NAME spsm-native-import-defaults COMMAND bts-spsm-bitmap-options-tests)
set_tests_properties(spsm-native-import-defaults PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Independent spatial-filter math is diagnostic-only; it never enters the live
# document evaluator or the CPU export path.
add_executable(bts-spatial-filter-reference-tests ../diagnostic_spatial_filter_test.cc)
target_compile_features(bts-spatial-filter-reference-tests PRIVATE cxx_std_20)
target_include_directories(bts-spatial-filter-reference-tests PRIVATE ../../host)
target_link_libraries(bts-spatial-filter-reference-tests PRIVATE Qt6::Core)
add_test(NAME spatial-filter-reference COMMAND bts-spatial-filter-reference-tests)
add_executable(bts-spatial-export-guard-tests ../native_export_guard_test.cc)
target_link_libraries(bts-spatial-export-guard-tests PRIVATE bts-spsm-native-lowering)
add_test(NAME spatial-export-guard COMMAND bts-spatial-export-guard-tests)

set(BTS_SPMSK_FIXTURE_DIR "" CACHE PATH "Optional directory containing the fourteen user DataSmartMask files")
if(BTS_SPMSK_FIXTURE_DIR)
 file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/empty-material-fixtures")
 add_test(NAME spmsk-native-fourteen-files COMMAND bts-spsm-native-lowering-tests
  "${CMAKE_CURRENT_BINARY_DIR}/empty-material-fixtures" "${BTS_SPMSK_FIXTURE_DIR}")
endif()

add_subdirectory(../../spsm/bitmaps bitmaps)
add_executable(bts-spmsk-embedded-bitmap-tests ../spmsk_embedded_bitmap_test.cc)
target_include_directories(bts-spmsk-embedded-bitmap-tests PRIVATE
  $<TARGET_PROPERTY:bts-spsm-reader,INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:bts-spsm-bitmaps,INCLUDE_DIRECTORIES>)
target_link_libraries(bts-spmsk-embedded-bitmap-tests PRIVATE bts-spsm-native-lowering bts-spsm-bitmaps bts-hdf5)
add_test(NAME spmsk-embedded-image-boundary COMMAND bts-spmsk-embedded-bitmap-tests)
