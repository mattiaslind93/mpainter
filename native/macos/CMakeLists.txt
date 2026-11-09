# Included inside Blender's creator graph, only by an explicit diagnostic build.
if(NOT APPLE OR NOT WITH_METAL_BACKEND OR NOT WITH_HEADLESS)
  message(FATAL_ERROR "The Metal probe requires Apple, WITH_METAL_BACKEND and WITH_HEADLESS")
endif()
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Gui)
# Qt 6.8's WrapOpenGL adds AGL even for QImage-only clients. New Apple SDKs
# omit its link stub; our code uses no AGL symbols. QtGui's own dylib dependency
# remains intact and must still be satisfied by macOS at runtime.
if(NOT EXISTS "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/AGL.framework")
  get_target_property(_mpainter_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  list(FILTER _mpainter_opengl_links EXCLUDE REGEX "(^|/| )AGL([.]framework|$)")
  set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${_mpainter_opengl_links}")
endif()
add_executable(mpainter-metal-probe ${SRC} ${BTS_HOST_SOURCE_DIR}/../macos/MetalProbe.mm
  ${BTS_HOST_SOURCE_DIR}/../macos/MetalRhiViewport.mm
  ${BTS_HOST_SOURCE_DIR}/../macos/MetalContextOwner.mm
  ${BTS_HOST_SOURCE_DIR}/PaintPixels.cc)
target_compile_features(mpainter-metal-probe PRIVATE cxx_std_20)
target_compile_definitions(mpainter-metal-probe PRIVATE WITH_BTS_HOST QT_NO_KEYWORDS)
target_link_libraries(mpainter-metal-probe PRIVATE ${LIB} Qt6::Gui Qt6::GuiPrivate)
if(WITH_CPU_CHECK)
  target_link_libraries(mpainter-metal-probe PRIVATE blender_cpu_check)
endif()
add_dependencies(mpainter-metal-probe makesdna)
blender_target_include_dirs(mpainter-metal-probe ${INC}
  ${BTS_HOST_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/intern/ghost
  ${CMAKE_SOURCE_DIR}/intern/ghost/intern
  ${CMAKE_SOURCE_DIR}/source/blender/gpu
  ${CMAKE_SOURCE_DIR}/source/blender/gpu/intern
  ${CMAKE_SOURCE_DIR}/source/blender/gpu/metal
  ${CMAKE_SOURCE_DIR}/source/blender/draw
)
setup_platform_linker_flags(mpainter-metal-probe)
setup_platform_linker_libs(mpainter-metal-probe)
setup_platform_linker_symbol_hiding(mpainter-metal-probe)
# QtGui exports its own FreeType symbols. Force Blender's complete static copy
# into this executable and keep its API private, avoiding a mixed FT_Library
# whose driver list comes from Qt (which cannot decode Blender's WOFF2 fonts).
target_link_options(mpainter-metal-probe PRIVATE
  "LINKER:-force_load,${FREETYPE_LIBRARY_RELEASE}"
  "LINKER:-unexported_symbol,_FT_*")
# Deliberately not mPainter.app: this target is a port diagnostic, not the editor.

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Widgets)
add_executable(mpainter-macos-bvh-cpu ${BTS_HOST_SOURCE_DIR}/../tests/gpu_bake_bvh_test.cc)
target_include_directories(mpainter-macos-bvh-cpu PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(mpainter-macos-bvh-cpu PRIVATE cxx_std_20)
add_executable(mpainter-macos-theme-cpu
  ${BTS_HOST_SOURCE_DIR}/../tests/theme_test.cc ${BTS_HOST_SOURCE_DIR}/Theme.cc)
target_include_directories(mpainter-macos-theme-cpu PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(mpainter-macos-theme-cpu PRIVATE cxx_std_20)
target_compile_options(mpainter-macos-theme-cpu PRIVATE -UNDEBUG)
target_link_libraries(mpainter-macos-theme-cpu PRIVATE Qt6::Widgets)

add_executable(mpainter-macos-erase-cpu
  ${BTS_HOST_SOURCE_DIR}/../tests/erase_pixels_test.cc ${BTS_HOST_SOURCE_DIR}/PaintPixels.cc)
add_executable(mpainter-macos-storage-cpu ${BTS_HOST_SOURCE_DIR}/../tests/mesh_map_storage_test.cc)
foreach(target mpainter-macos-erase-cpu mpainter-macos-storage-cpu)
  target_include_directories(${target} PRIVATE ${BTS_HOST_SOURCE_DIR})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE Qt6::Gui)
endforeach()

add_executable(mpainter-macos-mask-cpu
  ${BTS_HOST_SOURCE_DIR}/../tests/layer_mask_test.cc ${BTS_HOST_SOURCE_DIR}/PaintPixels.cc)
add_executable(mpainter-macos-export-cpu ${BTS_HOST_SOURCE_DIR}/../tests/export_packing_test.cc)
add_executable(mpainter-macos-save-safety-cpu
  ${BTS_HOST_SOURCE_DIR}/../tests/project_save_safety_test.cc ${BTS_HOST_SOURCE_DIR}/PaintPixels.cc)
foreach(target mpainter-macos-mask-cpu mpainter-macos-export-cpu mpainter-macos-save-safety-cpu)
  target_include_directories(${target} PRIVATE ${BTS_HOST_SOURCE_DIR})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_link_libraries(${target} PRIVATE Qt6::Widgets)
endforeach()

# Blender 5.2 forcibly resets its global deployment target to 11.2. The official
# Qt 6.8.3 arm64 frameworks have LC_BUILD_VERSION minos 12.0. Raise only these Qt
# executables; do not force a rebuild or change the baseline of Blender libraries.
foreach(target mpainter-metal-probe mpainter-macos-bvh-cpu mpainter-macos-theme-cpu
    mpainter-macos-erase-cpu mpainter-macos-storage-cpu mpainter-macos-mask-cpu
    mpainter-macos-export-cpu mpainter-macos-save-safety-cpu)
  target_compile_options(${target} PRIVATE -mmacosx-version-min=12.0)
  if(NOT target STREQUAL "mpainter-metal-probe")
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
  target_link_options(${target} PRIVATE -mmacosx-version-min=12.0)
endforeach()
