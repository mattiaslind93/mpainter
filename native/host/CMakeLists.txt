# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Widgets OpenGLWidgets Network)
find_package(assimp CONFIG REQUIRED)

qt6_add_resources(BTS_FILE_BROWSER_RESOURCES ${BTS_HOST_SOURCE_DIR}/../../assets/file-browser/file-browser.qrc)
set(BTS_FILE_BROWSER_WIDGET_SOURCES ${BTS_HOST_SOURCE_DIR}/ui/dialogs/FileDialogs.cc ${BTS_HOST_SOURCE_DIR}/blender/FileBrowserIcons.cc ${BTS_HOST_SOURCE_DIR}/blender/FileBrowserController.cc ${BTS_FILE_BROWSER_RESOURCES})
qt6_add_resources(BTS_BRANDING_RESOURCES ${BTS_HOST_SOURCE_DIR}/../../assets/branding/branding.qrc)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BTS_HOST_SOURCE_DIR}/../../pyproject.toml)
file(STRINGS ${BTS_HOST_SOURCE_DIR}/../../pyproject.toml BTS_PRODUCT_VERSION_LINE REGEX "^version = ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+(-[A-Za-z0-9.-]+)?" BTS_PRODUCT_VERSION "${BTS_PRODUCT_VERSION_LINE}")
# The standalone Blender target retains its resources; this host has its own
# icon and VERSIONINFO in packaging/mpainter.rc.
set(BTS_CREATOR_SOURCES ${SRC})
list(REMOVE_ITEM BTS_CREATOR_SOURCES "${CMAKE_SOURCE_DIR}/release/windows/icons/winblender.rc")
add_executable(bts-host ${BTS_CREATOR_SOURCES}
  ${BTS_BRANDING_RESOURCES}
  ${BTS_HOST_SOURCE_DIR}/app/Branding.cc
  ${BTS_HOST_SOURCE_DIR}/ui/theme/Theme.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/BakeSourceCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ViewportHeadlessDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/TextureImportCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/TextureImportDemo.cc
  ${BTS_HOST_SOURCE_DIR}/io/TextureLoadController.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ViewportIdleDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/AutomationPlanCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/python/AutomationExportPlan.cc
  ${BTS_HOST_SOURCE_DIR}/python/AutomationJobs.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/AutomationDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ExtensionCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/layers/LayerApi.cc
  ${BTS_HOST_SOURCE_DIR}/blender/LinearColorTransform.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/LayerApiDemo.cc
  ${BTS_HOST_SOURCE_DIR}/layers/ChannelConfiguration.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/HardSurfaceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ChannelConfigurationDemo.cc
  ${BTS_HOST_SOURCE_DIR}/ui/dialogs/ChannelConfigurationDialog.cc
  ${BTS_HOST_SOURCE_DIR}/python/PythonUi.cc
  ${BTS_HOST_SOURCE_DIR}/python/PythonConsole.cc
  ${BTS_FILE_BROWSER_WIDGET_SOURCES}
  ${BTS_HOST_SOURCE_DIR}/blender/FileBrowserNativeThumbnails.cc
  ${BTS_HOST_SOURCE_DIR}/blender/QtSharedContext.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/RenderModeDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/FlattenExport.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/FlattenDemo.cc
  ${BTS_HOST_SOURCE_DIR}/layers/FlattenLayer.cc
  ${BTS_HOST_SOURCE_DIR}/blender/FlattenLayerDialog.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/DerivedExportDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/DerivedExportCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/InputSpaceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/SubsurfaceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SubsurfacePreviewIO.cc
  ${BTS_HOST_SOURCE_DIR}/viewport/RenderController.cc
  ${BTS_HOST_SOURCE_DIR}/blender/DisplayPostEffects.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/DisplayPostPanel.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/DisplayPostDemo.cc
  ${BTS_HOST_SOURCE_DIR}/viewport/ShaderController.cc
  ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrPanel.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ShaderDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/InspectionDemo.cc
  ${BTS_HOST_SOURCE_DIR}/diagnostics/ResourceMonitor.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ResourceMonitorDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/AutoBakeDemo.cc
  ${BTS_HOST_SOURCE_DIR}/baking/AutoBakeEngine.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/SkewDemo.cc
  ${BTS_HOST_SOURCE_DIR}/baking/SkewController.cc
  ${BTS_HOST_SOURCE_DIR}/baking/SkewPanel.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SkewGeometry.cc
  ${BTS_HOST_SOURCE_DIR}/baking/SkewBakeEngine.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SkewIO.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ResidencyDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ViewportResidency.cc
  ${BTS_HOST_SOURCE_DIR}/blender/GpuLayerPreview.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/PerformanceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/AePersistenceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/PropertyPerformanceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/StrokeSplineDemo.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/WarpPanel.cc
  ${BTS_HOST_SOURCE_DIR}/ui/widgets/WarpEditor.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/WarpGeometryDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/WarpDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ImageSequenceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/SkewBakeCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ClippingDemo.cc
  ${BTS_HOST_SOURCE_DIR}/app/Preferences.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/FillChannelsDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/SpatialModeDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/PlanarDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/FilledPathDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/RibbonDemo.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/RibbonPanel.cc
  ${BTS_HOST_SOURCE_DIR}/ui/widgets/OverlayScroll.cc
  ${BTS_HOST_SOURCE_DIR}/ui/widgets/PanelScrolling.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ScrollDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/AlembicDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/JpegExportCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ExportDepthCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/RequestedFeaturesProof.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ExportCpuDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/SignedDisplacementDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/DisplacementDemo.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/DisplacementPanel.cc
  ${BTS_HOST_SOURCE_DIR}/blender/DisplacementPreview.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeCompositeJob.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeCyclesImages.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ColorPicker.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/ColorPickerDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/LightingDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/BakeFixDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/LayerEditDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/GuiFixDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_ui_artist_test.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/TabletPressureDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/TabletPerformanceDemo.cc
  ${BTS_HOST_SOURCE_DIR}/painting/StrokeReproject.cc
  ${BTS_HOST_SOURCE_DIR}/blender/StrokeIO.cc
  ${BTS_HOST_SOURCE_DIR}/io/TextureSetReassignment.cc
  ${BTS_HOST_SOURCE_DIR}/persistence/ProjectTemplate.cc
  ${BTS_HOST_SOURCE_DIR}/persistence/ProjectConfiguration.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/UnitsPanel.cc
  ${BTS_HOST_SOURCE_DIR}/fill/PhysicalScaleSource.cc
  ${BTS_HOST_SOURCE_DIR}/io/UsdScene.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/UsdScenePanel.cc
  ${BTS_HOST_SOURCE_DIR}/blender/BlendAssetLibrary.cc
  ${BTS_HOST_SOURCE_DIR}/assets/BlendAssetBrowser.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialPreview.cc
  ${BTS_HOST_SOURCE_DIR}/assets/SmartMaskThumbnailJobs.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialPreviewImages.cc
  ${BTS_HOST_SOURCE_DIR}/blender/BlendMaterial.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialPanel.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialPanelTest.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialInspectionDemo.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialGpuProgramTest.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialStackTest.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialGpuLibrary.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ShaderTreeCache.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialGpuProgram.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialGpuCompiler.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialFragmentBumpTest.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialFragmentGpuCompiler.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeFragmentGeometry.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialFragmentGpuTest.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialGpuPrototype.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialContext.cc
  ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialImagePanel.cc
  ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialImageJobs.cc
  ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialImageUi.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImageBindings.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImageVariant.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialSourceFiles.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialResource.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialArchive.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialDecodedImage.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImages.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImageUpload.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImageResource.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialImageArchive.cc
  ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialSourceLoader.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialHostQueueDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialSharedGpuImages.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialGraph.cc
  ${BTS_HOST_SOURCE_DIR}/ui/dialogs/BlendMaterialDialog.cc
  ${BTS_HOST_SOURCE_DIR}/blender/BlendLibrary.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/BlendLibraryPanel.cc
  ${BTS_HOST_SOURCE_DIR}/io/SceneBridge.cc
  ${BTS_HOST_SOURCE_DIR}/blender/AutoUnwrap.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshReplacement.cc
  ${BTS_HOST_SOURCE_DIR}/persistence/Recovery.cc
  ${BTS_HOST_SOURCE_DIR}/io/ExportPadding.cc
  ${BTS_HOST_SOURCE_DIR}/blender/TextureExport.cc
  ${BTS_HOST_SOURCE_DIR}/ui/dialogs/TextureExportDialog.cc
  ${BTS_HOST_SOURCE_DIR}/assets/SmartMaterial.cc
  ${BTS_HOST_SOURCE_DIR}/ui/panels/MeshMapsPanel.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshBakeDialog.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshMapImport.cc
  ${BTS_HOST_SOURCE_DIR}/baking/MeshMapAssign.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshMapIO.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshBakeFilter.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshBaker.cc
  ${BTS_HOST_SOURCE_DIR}/blender/AssetThumbnail.cc
  ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ResourceIO.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ResourceImport.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/FillTextureDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/FillTextureIO.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/FillSymmetryDemo.cc
  ${BTS_HOST_SOURCE_DIR}/fill/FillSymmetryPanel.cc
  ${BTS_HOST_SOURCE_DIR}/fill/FillPanel.cc
  ${BTS_HOST_SOURCE_DIR}/blender/BitmapMask.cc
  ${BTS_HOST_SOURCE_DIR}/blender/TexturePadding.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SymmetryIO.cc
  ${BTS_HOST_SOURCE_DIR}/blender/LayerThumbnail.cc
  ${BTS_HOST_SOURCE_DIR}/blender/EnvironmentResource.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ProjectIO.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SurfaceBrush.cc
  ${BTS_HOST_SOURCE_DIR}/layers/AmbientOcclusion.cc
  ${BTS_HOST_SOURCE_DIR}/layers/NormalMix.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayerShader.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayeringSources.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayeringSourceSnapshots.cc
  ${BTS_HOST_SOURCE_DIR}/blender/OpenPbrShader.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MaterialInspection.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NativeWorldNormalShader.cc
  ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc
  ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc
  ${BTS_HOST_SOURCE_DIR}/blender/NewProjectProfile.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/MeshExportDemo.cc
  ${BTS_HOST_SOURCE_DIR}/blender/UsdTextureBindings.cc
  ${BTS_HOST_SOURCE_DIR}/blender/UsdMeshExport.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshExport.cc
  ${BTS_HOST_SOURCE_DIR}/ui/dialogs/MeshExportDialog.cc
  ${BTS_HOST_SOURCE_DIR}/blender/MeshImport.cc
  ${BTS_HOST_SOURCE_DIR}/ui/dialogs/NewProjectDialog.cc
  ${BTS_HOST_SOURCE_DIR}/main.cc
  ${BTS_HOST_SOURCE_DIR}/blender/BlenderHost.cc
  ${BTS_HOST_SOURCE_DIR}/blender/EeveeCompletionSnapshot.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SurfaceGraphSnapshot.cc
  ${BTS_HOST_SOURCE_DIR}/BlenderHost.hh
  ${BTS_HOST_SOURCE_DIR}/blender/SceneSnapshot.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SceneSnapshot.hh
  ${BTS_HOST_SOURCE_DIR}/ui/shell/WorkspaceShell.cc
  ${BTS_HOST_SOURCE_DIR}/ui/shell/WorkspaceShell.hh
  ${BTS_HOST_SOURCE_DIR}/viewport/ViewLayout.hh
  ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.cc
  ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.hh
  ${BTS_HOST_SOURCE_DIR}/blender/SceneTriangleOverlay.cc
  ${BTS_HOST_SOURCE_DIR}/blender/UvGpuOverlay.cc
  ${BTS_HOST_SOURCE_DIR}/blender/UvPreview.cc
  ${BTS_HOST_SOURCE_DIR}/viewport/UvPreview.hh
  ${BTS_HOST_SOURCE_DIR}/viewport/UvView.cc
  ${BTS_HOST_SOURCE_DIR}/viewport/UvView.hh
)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/blender/BlendAssetLibrary.cc PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/source/blender/asset_system")
# Immutable geometry owner captures real ObjectInfos using stock draw resource sync.
set_property(SOURCE
  ${BTS_HOST_SOURCE_DIR}/blender/NativeFragmentGeometry.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialInspectionDemo.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/source/blender/draw/intern")
# The explicit generated-source diagnostic uses the real EEVEE amendment.
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/../harness/ViewportHeadlessDemo.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES
    "${CMAKE_SOURCE_DIR}/source/blender/draw/engines/eevee"
    "${CMAKE_SOURCE_DIR}/source/blender/draw/intern")
# Explicit fragment Normal Map bindings consume the real ObjectInfos layout.
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialFragmentGpuCompiler.cc
  ${BTS_HOST_SOURCE_DIR}/../harness/NativeMaterialFragmentGpuTest.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/source/blender/draw/intern")
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/EeveeCompletionSnapshot.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES
    "${CMAKE_SOURCE_DIR}/source/blender/draw/engines/eevee"
    "${CMAKE_SOURCE_DIR}/source/blender/draw/intern"
    "${CMAKE_SOURCE_DIR}/source/blender/blentranslation")
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayeringSourceSnapshots.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/source/blender/imbuf/opencolorio")
target_compile_definitions(bts-host PRIVATE WITH_BTS_HOST QT_NO_KEYWORDS)
add_custom_command(TARGET bts-host POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bts-host>/branding COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BTS_HOST_SOURCE_DIR}/../../assets/branding/splash-featured.png ${BTS_HOST_SOURCE_DIR}/../../assets/branding/splash-featured.focus $<TARGET_FILE_DIR:bts-host>/branding)
install(FILES ${BTS_HOST_SOURCE_DIR}/../../assets/branding/splash-featured.png ${BTS_HOST_SOURCE_DIR}/../../assets/branding/splash-featured.focus DESTINATION branding)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/app/Branding.cc PROPERTIES COMPILE_DEFINITIONS "BTS_PRODUCT_VERSION=\"${BTS_PRODUCT_VERSION}\"")
target_compile_features(bts-host PRIVATE cxx_std_20)
# Direct SPSM source inspection is worker-side, independent of external vendor runtimes.
add_subdirectory(${BTS_HOST_SOURCE_DIR}/../spsm ${CMAKE_CURRENT_BINARY_DIR}/spsm-reader)
add_subdirectory(${BTS_HOST_SOURCE_DIR}/../spsm/semantic ${CMAKE_CURRENT_BINARY_DIR}/spsm-semantic)
add_subdirectory(${BTS_HOST_SOURCE_DIR}/../spsm/bitmaps ${CMAKE_CURRENT_BINARY_DIR}/spsm-bitmaps)
target_sources(bts-host PRIVATE
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmMaterialImport.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SpsmBitmapTexture.cc
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmBitmapLowering.cc
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmMeshMapLowering.cc
  ${BTS_HOST_SOURCE_DIR}/blender/SpsmSource.cc
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmImportUi.cc)
target_link_libraries(bts-host PRIVATE bts-spsm-semantic bts-spsm-bitmaps)
add_executable(bts-spsm-bitmap-options-tests ${BTS_HOST_SOURCE_DIR}/../tests/spsm_bitmap_options_test.cc)
target_compile_features(bts-spsm-bitmap-options-tests PRIVATE cxx_std_20)
target_link_libraries(bts-spsm-bitmap-options-tests PRIVATE Qt6::Widgets)
target_include_directories(bts-spsm-bitmap-options-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
add_executable(bts-spsm-application-tests
  ${BTS_HOST_SOURCE_DIR}/../tests/spsm_application_test.cc
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmImportUi.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_link_libraries(bts-spsm-application-tests PRIVATE bts-export-config bts-spsm-semantic Qt6::Widgets)

if(WITH_CPU_CHECK)
  target_link_libraries(bts-host PRIVATE blender_cpu_check)
endif()


target_link_libraries(bts-host PRIVATE ${LIB} bf_io_wavefront_obj bf_io_alembic bf::imbuf bf::dependencies::epoxy Qt6::Widgets Qt6::OpenGLWidgets Qt6::Network Qt6::GuiPrivate)
target_link_libraries(bts-host PRIVATE bf::dependencies::optional::openimagedenoise)
if(WITH_IO_FBX)
  target_link_libraries(bts-host PRIVATE bf_io_fbx)
  set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/MeshImport.cc APPEND PROPERTY COMPILE_DEFINITIONS BTS_WITH_FBX)
endif()
add_dependencies(bts-host makesdna)
if(WITH_BUILDINFO)
  add_dependencies(bts-host buildinfo)
endif()
setup_platform_linker_flags(bts-host)
setup_platform_linker_libs(bts-host)
setup_platform_linker_symbol_hiding(bts-host)
blender_target_include_dirs(bts-host ${INC}
  ${BTS_HOST_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/source/blender/makesrna
  ${CMAKE_SOURCE_DIR}/intern/ghost
  ${CMAKE_SOURCE_DIR}/intern/ghost/intern
  ${CMAKE_SOURCE_DIR}/source/blender/gpu
  ${CMAKE_SOURCE_DIR}/source/blender/gpu/intern
  ${CMAKE_SOURCE_DIR}/source/blender/gpu/opengl
  ${CMAKE_SOURCE_DIR}/source/blender/draw
  ${CMAKE_SOURCE_DIR}/source/blender/io/fbx
  ${CMAKE_SOURCE_DIR}/source/blender/io/usd
  ${CMAKE_SOURCE_DIR}/source/blender/io/alembic
  ${CMAKE_SOURCE_DIR}/source/blender/io/wavefront_obj
  ${CMAKE_SOURCE_DIR}/source/blender/geometry
  ${CMAKE_SOURCE_DIR}/source/blender/bmesh
  ${CMAKE_SOURCE_DIR}/source/blender/nodes
  ${CMAKE_SOURCE_DIR}/source/blender/io/common
  ${CMAKE_SOURCE_DIR}/source/blender/render
  ${CMAKE_SOURCE_DIR}/source/blender/imbuf
  ${CMAKE_SOURCE_DIR}/source/blender/blenloader
)
if(WITH_VULKAN_BACKEND)
  target_compile_definitions(bts-host PRIVATE WITH_VULKAN_BACKEND)
endif()
install(TARGETS bts-host RUNTIME DESTINATION . BUNDLE DESTINATION .)

# Pure authoring math/storage checks; no GPU or Blender process required.
add_executable(bts-paint-tests
  ${BTS_HOST_SOURCE_DIR}/../tests/paint_pixels_test.cc
  ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc
  ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-paint-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-paint-tests PRIVATE cxx_std_20)
target_link_libraries(bts-paint-tests PRIVATE Qt6::Core)

add_executable(bts-mask-tests ${BTS_HOST_SOURCE_DIR}/../tests/layer_mask_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-mask-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-tests PRIVATE Qt6::Core)

add_executable(bts-erase-tests ${BTS_HOST_SOURCE_DIR}/../tests/erase_pixels_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-erase-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-erase-tests PRIVATE cxx_std_20)
target_link_libraries(bts-erase-tests PRIVATE Qt6::Core)

add_executable(bts-alpha-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_alpha_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-alpha-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-alpha-tests PRIVATE cxx_std_20)

add_executable(bts-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_projection_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-projection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-projection-tests PRIVATE cxx_std_20)

add_executable(bts-symmetry-tests ${BTS_HOST_SOURCE_DIR}/../tests/symmetry_test.cc)
target_include_directories(bts-symmetry-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-symmetry-tests PRIVATE cxx_std_20)
target_link_libraries(bts-symmetry-tests PRIVATE Qt6::Core)

add_executable(bts-straight-tests ${BTS_HOST_SOURCE_DIR}/../tests/straight_stroke_test.cc)
target_include_directories(bts-straight-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-straight-tests PRIVATE cxx_std_20)

add_executable(bts-lazy-tests ${BTS_HOST_SOURCE_DIR}/../tests/lazy_mouse_test.cc)
target_include_directories(bts-lazy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-lazy-tests PRIVATE cxx_std_20)

add_executable(bts-polygon-tests ${BTS_HOST_SOURCE_DIR}/../tests/polygon_fill_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-polygon-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-polygon-tests PRIVATE cxx_std_20)
target_link_libraries(bts-polygon-tests PRIVATE Qt6::Core)

add_executable(bts-fill-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_projection_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-fill-projection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-fill-projection-tests PRIVATE cxx_std_20)
target_link_libraries(bts-fill-projection-tests PRIVATE Qt6::Core)

add_executable(bts-asset-tests ${BTS_HOST_SOURCE_DIR}/../tests/asset_catalog_test.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_include_directories(bts-asset-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-asset-tests PRIVATE cxx_std_20)
target_link_libraries(bts-asset-tests PRIVATE Qt6::Core Qt6::Gui bts-export-config)

add_executable(bts-mesh-map-tests ${BTS_HOST_SOURCE_DIR}/../tests/mesh_maps_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-mesh-map-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mesh-map-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mesh-map-tests PRIVATE Qt6::Core)

add_executable(bts-mesh-map-storage-tests ${BTS_HOST_SOURCE_DIR}/../tests/mesh_map_storage_test.cc)
target_include_directories(bts-mesh-map-storage-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mesh-map-storage-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mesh-map-storage-tests PRIVATE Qt6::Core)

add_executable(bts-gpu-bake-bvh-tests ${BTS_HOST_SOURCE_DIR}/../tests/gpu_bake_bvh_test.cc)
target_include_directories(bts-gpu-bake-bvh-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-gpu-bake-bvh-tests PRIVATE cxx_std_20)


add_executable(bts-bake-match-tests ${BTS_HOST_SOURCE_DIR}/../tests/bake_match_test.cc)
target_include_directories(bts-bake-match-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-bake-match-tests PRIVATE cxx_std_20)

add_executable(bts-map-import-tests ${BTS_HOST_SOURCE_DIR}/../tests/mesh_map_import_test.cc ${BTS_HOST_SOURCE_DIR}/baking/MeshMapAssign.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-map-import-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-map-import-tests PRIVATE cxx_std_20)
target_link_libraries(bts-map-import-tests PRIVATE Qt6::Core)

add_executable(bts-normal-mix-tests ${BTS_HOST_SOURCE_DIR}/../tests/normal_mix_test.cc ${BTS_HOST_SOURCE_DIR}/layers/NormalMix.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-normal-mix-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-normal-mix-tests PRIVATE cxx_std_20)
target_link_libraries(bts-normal-mix-tests PRIVATE Qt6::Core)

add_executable(bts-height-normal-tests ${BTS_HOST_SOURCE_DIR}/../tests/height_normal_test.cc ${BTS_HOST_SOURCE_DIR}/layers/NormalMix.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-height-normal-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-height-normal-tests PRIVATE cxx_std_20)
target_link_libraries(bts-height-normal-tests PRIVATE Qt6::Core)

add_executable(bts-ao-tests ${BTS_HOST_SOURCE_DIR}/../tests/ambient_occlusion_test.cc ${BTS_HOST_SOURCE_DIR}/layers/AmbientOcclusion.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-ao-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-ao-tests PRIVATE cxx_std_20)
target_link_libraries(bts-ao-tests PRIVATE Qt6::Core)

add_executable(bts-export-naming-tests ${BTS_HOST_SOURCE_DIR}/../tests/export_naming_test.cc)
target_include_directories(bts-export-naming-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-export-naming-tests PRIVATE cxx_std_20)
target_link_libraries(bts-export-naming-tests PRIVATE Qt6::Core)

add_executable(bts-export-packing-tests ${BTS_HOST_SOURCE_DIR}/../tests/export_packing_test.cc)
target_include_directories(bts-export-packing-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-export-packing-tests PRIVATE cxx_std_20)

# Shared metadata parser is also linked by the pure TextureDocument test targets.
add_library(bts-export-config STATIC ${BTS_HOST_SOURCE_DIR}/baking/SkewArchive.cc ${BTS_HOST_SOURCE_DIR}/layers/Displacement.cc ${BTS_HOST_SOURCE_DIR}/layers/CustomChannel.cc ${BTS_HOST_SOURCE_DIR}/fill/PhysicalScale.cc ${BTS_HOST_SOURCE_DIR}/io/ExportConfiguration.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeArchive.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeTransfer.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-export-config PUBLIC ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-export-config PUBLIC cxx_std_20)
target_link_libraries(bts-export-config PUBLIC Qt6::Core)
qt6_add_resources(BTS_BUILTIN_BRUSH_RESOURCES ${BTS_HOST_SOURCE_DIR}/../../assets/brush-alphas/builtin-brush-alphas.qrc)
qt6_add_resources(BTS_BUILTIN_GRUNGE_RESOURCES ${BTS_HOST_SOURCE_DIR}/../../assets/grunges/builtin-grunges.qrc)
qt6_add_resources(BTS_MASK_PREVIEW_RESOURCES ${BTS_HOST_SOURCE_DIR}/../../assets/mask-preview/builtin-mask-preview.qrc)
add_library(bts-builtin-smartmasks STATIC ${BTS_BUILTIN_GRUNGE_RESOURCES} ${BTS_MASK_PREVIEW_RESOURCES} ${BTS_BUILTIN_BRUSH_RESOURCES} ${BTS_HOST_SOURCE_DIR}/assets/BuiltinSmartMasks.cc ${BTS_HOST_SOURCE_DIR}/assets/BuiltinBrushPresets.cc ${BTS_HOST_SOURCE_DIR}/assets/BuiltinProceduralAssets.cc ${BTS_HOST_SOURCE_DIR}/assets/BuiltinStandardMaterials.cc ${BTS_HOST_SOURCE_DIR}/assets/BuiltinPolyHaven.cc)
target_include_directories(bts-builtin-smartmasks PUBLIC ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-builtin-smartmasks PUBLIC cxx_std_20)
target_link_libraries(bts-builtin-smartmasks PUBLIC Qt6::Gui bts-export-config)
get_property(_bts_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_bts_target IN LISTS _bts_targets)
  get_target_property(_bts_sources ${_bts_target} SOURCES)
  foreach(_bts_source IN LISTS _bts_sources)
    if(_bts_source MATCHES "/TextureDocument[.]cc$")
      target_link_libraries(${_bts_target} PRIVATE bts-export-config)
      break()
    endif()
  endforeach()
endforeach()
# Portable material assets validate their standalone document on a worker. The
# static archive supplies the model only to targets that do not compile it.
target_sources(bts-export-config PRIVATE ${BTS_HOST_SOURCE_DIR}/assets/SmartMaterialPreset.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_sources(bts-export-config PRIVATE ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmAssetDispatch.cc)
add_executable(bts-spsm-asset-tests ${BTS_HOST_SOURCE_DIR}/../tests/spsm_asset_test.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_link_libraries(bts-spsm-asset-tests PRIVATE bts-export-config)
add_executable(bts-smart-material-preset-tests ${BTS_HOST_SOURCE_DIR}/../tests/smart_material_preset_test.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_link_libraries(bts-smart-material-preset-tests PRIVATE bts-export-config)

# Explicit, headless Blender interchange. Uses the same archive codecs as the shelf.
add_executable(bts-blender-presets ${BTS_HOST_SOURCE_DIR}/assets/BlenderPresetBridge.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_link_libraries(bts-blender-presets PRIVATE bts-builtin-smartmasks)

add_executable(bts-export-config-tests ${BTS_HOST_SOURCE_DIR}/../tests/export_configuration_test.cc)
target_link_libraries(bts-export-config-tests PRIVATE bts-export-config)

add_executable(bts-stroke-history-tests ${BTS_HOST_SOURCE_DIR}/../tests/stroke_history_test.cc)
target_link_libraries(bts-stroke-history-tests PRIVATE bts-export-config)

add_executable(bts-stroke-archive-tests ${BTS_HOST_SOURCE_DIR}/../tests/stroke_archive_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-stroke-archive-tests PRIVATE bts-export-config)

find_package(ZLIB REQUIRED)
target_link_libraries(bts-export-config PUBLIC ZLIB::ZLIB)
add_executable(bts-history-codec-tests ${BTS_HOST_SOURCE_DIR}/../tests/history_codec_test.cc)
target_include_directories(bts-history-codec-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-history-codec-tests PRIVATE cxx_std_20)
target_link_libraries(bts-history-codec-tests PRIVATE Qt6::Core ZLIB::ZLIB)

add_executable(bts-uv-view-tests ${BTS_HOST_SOURCE_DIR}/../tests/uv_view_test.cc ${BTS_HOST_SOURCE_DIR}/viewport/UvView.cc)
target_include_directories(bts-uv-view-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-uv-view-tests PRIVATE cxx_std_20)
target_link_libraries(bts-uv-view-tests PRIVATE Qt6::Widgets)

add_executable(bts-uv-transfer-tests ${BTS_HOST_SOURCE_DIR}/../tests/uv_transfer_test.cc)
target_include_directories(bts-uv-transfer-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-uv-transfer-tests PRIVATE cxx_std_20)

add_executable(bts-stroke-transfer-tests ${BTS_HOST_SOURCE_DIR}/../tests/stroke_transfer_test.cc)
target_link_libraries(bts-stroke-transfer-tests PRIVATE bts-export-config)

add_executable(bts-set-assignment-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_set_assignment_test.cc)
target_include_directories(bts-set-assignment-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-set-assignment-tests PRIVATE cxx_std_20)
target_link_libraries(bts-set-assignment-tests PRIVATE Qt6::Core)

add_executable(bts-project-template-tests ${BTS_HOST_SOURCE_DIR}/../tests/project_template_test.cc ${BTS_HOST_SOURCE_DIR}/persistence/ProjectTemplate.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-project-template-tests PRIVATE bts-export-config)

add_executable(bts-fragment-tangents-tests ${BTS_HOST_SOURCE_DIR}/../tests/fragment_tangents_test.cc)
target_include_directories(bts-fragment-tangents-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-fragment-tangents-tests PRIVATE cxx_std_20)

add_executable(bts-trackball-tests ${BTS_HOST_SOURCE_DIR}/../tests/trackball_test.cc ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.cc)
target_include_directories(bts-trackball-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-trackball-tests PRIVATE cxx_std_20)
target_compile_options(bts-trackball-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-trackball-tests PRIVATE Qt6::Gui)
add_test(NAME bts-trackball COMMAND bts-trackball-tests)

add_executable(bts-camera-pose-tests ${BTS_HOST_SOURCE_DIR}/../tests/camera_pose_test.cc ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.cc)
target_include_directories(bts-camera-pose-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-camera-pose-tests PRIVATE cxx_std_20)
target_link_libraries(bts-camera-pose-tests PRIVATE Qt6::Gui)
add_executable(bts-scene-bridge-tests ${BTS_HOST_SOURCE_DIR}/../tests/scene_bridge_test.cc ${BTS_HOST_SOURCE_DIR}/io/SceneBridge.cc)
target_include_directories(bts-scene-bridge-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-scene-bridge-tests PRIVATE cxx_std_20)
target_link_libraries(bts-scene-bridge-tests PRIVATE Qt6::Gui assimp::assimp)
add_executable(bts-scene-formats-tests ${BTS_HOST_SOURCE_DIR}/../tests/scene_formats_test.cc ${BTS_HOST_SOURCE_DIR}/io/SceneBridge.cc)
target_include_directories(bts-scene-formats-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-scene-formats-tests PRIVATE cxx_std_20)
target_link_libraries(bts-scene-formats-tests PRIVATE Qt6::Gui assimp::assimp)
add_test(NAME bts-scene-formats COMMAND bts-scene-formats-tests)

add_executable(bts-usd-scene-tests ${BTS_HOST_SOURCE_DIR}/../tests/usd_scene_test.cc ${BTS_HOST_SOURCE_DIR}/io/UsdScene.cc)
target_include_directories(bts-usd-scene-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-usd-scene-tests PRIVATE cxx_std_20)
target_compile_definitions(bts-usd-scene-tests PRIVATE QT_NO_KEYWORDS BTS_USD_TEST_ROOT="${CMAKE_BINARY_DIR}/../.." BTS_USD_FIXTURE="${BTS_HOST_SOURCE_DIR}/../tests/fixtures/usd_scene_contract.usda")
target_link_libraries(bts-usd-scene-tests PRIVATE Qt6::Core bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)
setup_platform_linker_libs(bts-usd-scene-tests)

add_executable(bts-physical-scale-tests ${BTS_HOST_SOURCE_DIR}/../tests/physical_scale_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/fill/PhysicalScaleSource.cc ${BTS_HOST_SOURCE_DIR}/fill/PhysicalScale.cc ${BTS_HOST_SOURCE_DIR}/io/UsdScene.cc)
target_include_directories(bts-physical-scale-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-physical-scale-tests PRIVATE cxx_std_20)
target_compile_definitions(bts-physical-scale-tests PRIVATE QT_NO_KEYWORDS BTS_PHYSICAL_USD_FIXTURE="${BTS_HOST_SOURCE_DIR}/../tests/fixtures/usd_scene_contract.usda" BTS_PHYSICAL_TEST_ROOT="${CMAKE_BINARY_DIR}")
target_link_libraries(bts-physical-scale-tests PRIVATE bts-export-config Qt6::Core bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)
setup_platform_linker_libs(bts-physical-scale-tests)


add_executable(bts-dynamic-resolution-tests ${BTS_HOST_SOURCE_DIR}/../tests/dynamic_resolution_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-dynamic-resolution-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-dynamic-resolution-tests PRIVATE cxx_std_20)
target_link_libraries(bts-dynamic-resolution-tests PRIVATE bts-export-config)

add_executable(bts-channel-precision-tests ${BTS_HOST_SOURCE_DIR}/../tests/channel_precision_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-channel-precision-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-channel-precision-tests PRIVATE cxx_std_20)
target_link_libraries(bts-channel-precision-tests PRIVATE bts-export-config)

add_executable(bts-custom-channel-tests ${BTS_HOST_SOURCE_DIR}/../tests/custom_channel_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-custom-channel-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-custom-channel-tests PRIVATE cxx_std_20)
target_link_libraries(bts-custom-channel-tests PRIVATE bts-export-config)

add_executable(bts-tile-resolution-tests ${BTS_HOST_SOURCE_DIR}/../tests/tile_resolution_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-tile-resolution-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-tile-resolution-tests PRIVATE cxx_std_20)
target_link_libraries(bts-tile-resolution-tests PRIVATE bts-export-config)

add_executable(bts-blend-families-tests ${BTS_HOST_SOURCE_DIR}/../tests/blend_families_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-blend-families-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-blend-families-tests PRIVATE cxx_std_20)
target_link_libraries(bts-blend-families-tests PRIVATE bts-export-config)

add_executable(bts-normal-vector-blend-tests ${BTS_HOST_SOURCE_DIR}/../tests/normal_vector_blend_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-normal-vector-blend-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-normal-vector-blend-tests PRIVATE cxx_std_20)
target_link_libraries(bts-normal-vector-blend-tests PRIVATE bts-export-config)

add_executable(bts-levels-tests ${BTS_HOST_SOURCE_DIR}/../tests/levels_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-levels-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-levels-tests PRIVATE cxx_std_20)
target_link_libraries(bts-levels-tests PRIVATE bts-export-config)

add_executable(bts-color-selection-tests ${BTS_HOST_SOURCE_DIR}/../tests/color_selection_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-color-selection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-color-selection-tests PRIVATE cxx_std_20)
target_link_libraries(bts-color-selection-tests PRIVATE bts-export-config)

add_executable(bts-mask-copy-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_copy_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-mask-copy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-copy-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-copy-tests PRIVATE bts-export-config)

add_executable(bts-mask-preset-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_preset_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_include_directories(bts-mask-preset-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-preset-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-preset-tests PRIVATE bts-export-config)

add_executable(bts-mask-replace-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_replace_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-mask-replace-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-replace-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-replace-tests PRIVATE bts-export-config)

add_executable(bts-mask-effect-stack-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_effect_stack_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-mask-effect-stack-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-effect-stack-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-effect-stack-tests PRIVATE bts-export-config)

add_executable(bts-mask-generator-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_generator_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-mask-generator-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-generator-tests PRIVATE cxx_std_20)
target_link_libraries(bts-mask-generator-tests PRIVATE bts-export-config)

add_executable(bts-levels-components-tests ${BTS_HOST_SOURCE_DIR}/../tests/levels_components_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-levels-components-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-levels-components-tests PRIVATE cxx_std_20)
target_link_libraries(bts-levels-components-tests PRIVATE bts-export-config)

add_executable(bts-compare-mask-tests ${BTS_HOST_SOURCE_DIR}/../tests/compare_mask_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-compare-mask-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-compare-mask-tests PRIVATE cxx_std_20)
target_link_libraries(bts-compare-mask-tests PRIVATE bts-export-config)

add_executable(bts-paint-effect-tests ${BTS_HOST_SOURCE_DIR}/../tests/paint_effect_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-paint-effect-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-paint-effect-tests PRIVATE cxx_std_20)
target_link_libraries(bts-paint-effect-tests PRIVATE bts-export-config)

add_executable(bts-filter-effect-tests ${BTS_HOST_SOURCE_DIR}/../tests/filter_effect_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-filter-effect-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-filter-effect-tests PRIVATE cxx_std_20)
target_link_libraries(bts-filter-effect-tests PRIVATE bts-export-config Qt6::Widgets)

add_executable(bts-fill-effect-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_effect_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-fill-effect-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-fill-effect-tests PRIVATE cxx_std_20)
target_link_libraries(bts-fill-effect-tests PRIVATE bts-export-config)

add_executable(bts-anchor-reference-tests ${BTS_HOST_SOURCE_DIR}/../tests/anchor_reference_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-anchor-reference-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-anchor-reference-tests PRIVATE cxx_std_20)
target_link_libraries(bts-anchor-reference-tests PRIVATE bts-export-config)

add_executable(bts-anchor-validity-tests ${BTS_HOST_SOURCE_DIR}/../tests/anchor_validity_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-anchor-validity-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-anchor-validity-tests PRIVATE cxx_std_20)
target_link_libraries(bts-anchor-validity-tests PRIVATE bts-export-config)

add_executable(bts-geometry-mask-tests ${BTS_HOST_SOURCE_DIR}/../tests/geometry_mask_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-geometry-mask-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-geometry-mask-tests PRIVATE cxx_std_20)
target_link_libraries(bts-geometry-mask-tests PRIVATE bts-export-config)

add_executable(bts-instance-tests ${BTS_HOST_SOURCE_DIR}/../tests/layer_instance_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-instance-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-instance-tests PRIVATE cxx_std_20)
target_link_libraries(bts-instance-tests PRIVATE bts-export-config)

add_executable(bts-jitter-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_jitter_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-jitter-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-jitter-tests PRIVATE cxx_std_20)

add_executable(bts-stencil-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_stencil_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-stencil-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-stencil-tests PRIVATE cxx_std_20)

add_executable(bts-direction-tests ${BTS_HOST_SOURCE_DIR}/../tests/stroke_direction_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-direction-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-direction-tests PRIVATE cxx_std_20)

add_executable(bts-material-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/material_projection_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-material-projection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-material-projection-tests PRIVATE cxx_std_20)

target_sources(bts-export-config PRIVATE ${BTS_HOST_SOURCE_DIR}/io/SurfacePathIO.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeMetadata.cc)
target_sources(bts-export-config PRIVATE ${BTS_HOST_SOURCE_DIR}/painting/SmudgeStroke.cc)

add_executable(bts-smudge-tests ${BTS_HOST_SOURCE_DIR}/../tests/smudge_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/painting/SmudgeStroke.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc)
target_include_directories(bts-smudge-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-smudge-tests PRIVATE cxx_std_20)

add_executable(bts-clone-tests ${BTS_HOST_SOURCE_DIR}/../tests/clone_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/painting/SmudgeStroke.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc)
target_include_directories(bts-clone-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-clone-tests PRIVATE cxx_std_20)

add_executable(bts-quick-mask-tests ${BTS_HOST_SOURCE_DIR}/../tests/quick_mask_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-quick-mask-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-quick-mask-tests PRIVATE cxx_std_20)

add_executable(bts-dynamic-stroke-tests ${BTS_HOST_SOURCE_DIR}/../tests/dynamic_stroke_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-dynamic-stroke-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-dynamic-stroke-tests PRIVATE cxx_std_20)

add_executable(bts-surface-path-tests ${BTS_HOST_SOURCE_DIR}/../tests/surface_path_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc)
target_include_directories(bts-surface-path-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-surface-path-tests PRIVATE cxx_std_20)

add_executable(bts-surface-path-archive-tests ${BTS_HOST_SOURCE_DIR}/../tests/surface_path_archive_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-surface-path-archive-tests PRIVATE bts-export-config)

add_executable(bts-surface-path-raster-tests ${BTS_HOST_SOURCE_DIR}/../tests/surface_path_raster_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-surface-path-raster-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-surface-path-raster-tests PRIVATE cxx_std_20)

add_executable(bts-surface-path-effect-archive-tests ${BTS_HOST_SOURCE_DIR}/../tests/surface_path_effect_archive_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-surface-path-effect-archive-tests PRIVATE bts-export-config)

add_executable(bts-path-curve-tests ${BTS_HOST_SOURCE_DIR}/../tests/path_curve_test.cc)
target_include_directories(bts-path-curve-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-path-curve-tests PRIVATE cxx_std_20)

add_executable(bts-ribbon-tests ${BTS_HOST_SOURCE_DIR}/../tests/ribbon_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-ribbon-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-ribbon-tests PRIVATE cxx_std_20)

add_executable(bts-color-default-tests ${BTS_HOST_SOURCE_DIR}/../tests/color_defaults_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-color-default-tests PRIVATE bts-export-config)

add_executable(bts-bump-migration-tests ${BTS_HOST_SOURCE_DIR}/../tests/bump_migration_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-bump-migration-tests PRIVATE bts-export-config)

add_executable(bts-displacement-tests ${BTS_HOST_SOURCE_DIR}/../tests/displacement_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-displacement-tests PRIVATE bts-export-config)

# Release/NDEBUG must not disable the assertions that form the CPU test oracle.
add_executable(bts-fill-texture-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_texture_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-fill-texture-tests PRIVATE bts-export-config)
add_executable(bts-bake-preferences-tests ${BTS_HOST_SOURCE_DIR}/../tests/bake_preferences_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-bake-preferences-tests PRIVATE bts-export-config)
add_executable(bts-edit-state-tests ${BTS_HOST_SOURCE_DIR}/../tests/edit_state_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-edit-state-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-edit-state-tests PRIVATE cxx_std_20)
target_link_libraries(bts-edit-state-tests PRIVATE bts-export-config)

add_executable(bts-panel-scroll-tests ${BTS_HOST_SOURCE_DIR}/../tests/panel_scrolling_test.cc ${BTS_HOST_SOURCE_DIR}/ui/widgets/PanelScrolling.cc)
target_include_directories(bts-panel-scroll-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-panel-scroll-tests PRIVATE cxx_std_20)
target_link_libraries(bts-panel-scroll-tests PRIVATE Qt6::Widgets)

add_executable(bts-ribbon-archive-tests ${BTS_HOST_SOURCE_DIR}/../tests/ribbon_archive_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-ribbon-archive-tests PRIVATE bts-export-config)

add_executable(bts-filled-path-tests ${BTS_HOST_SOURCE_DIR}/../tests/filled_path_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-filled-path-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-filled-path-tests PRIVATE cxx_std_20)
target_compile_definitions(bts-filled-path-tests PRIVATE QT_NO_KEYWORDS)
target_link_libraries(bts-filled-path-tests PRIVATE bts-export-config)

add_executable(bts-planar-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/planar_projection_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-planar-projection-tests PRIVATE bts-export-config)

add_executable(bts-spherical-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/spherical_projection_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-spherical-projection-tests PRIVATE bts-export-config)

add_executable(bts-cylindrical-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/cylindrical_projection_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-cylindrical-projection-tests PRIVATE bts-export-config)

add_executable(bts-fill-channel-texture-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_channel_texture_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-fill-channel-texture-tests PRIVATE bts-export-config)

add_executable(bts-property-composite-tests ${BTS_HOST_SOURCE_DIR}/../tests/property_composite_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-property-composite-tests PRIVATE bts-export-config)

add_executable(bts-stroke-spline-tests ${BTS_HOST_SOURCE_DIR}/../tests/stroke_spline_test.cc)
target_include_directories(bts-stroke-spline-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-stroke-spline-tests PRIVATE cxx_std_20)

add_executable(bts-warp-grid-tests ${BTS_HOST_SOURCE_DIR}/../tests/warp_grid_test.cc)
target_include_directories(bts-warp-grid-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-warp-grid-tests PRIVATE cxx_std_20)
target_link_libraries(bts-warp-grid-tests PRIVATE Qt6::Core)

add_executable(bts-warp-editor-tests ${BTS_HOST_SOURCE_DIR}/../tests/warp_editor_test.cc
  ${BTS_HOST_SOURCE_DIR}/ui/widgets/WarpEditor.cc ${BTS_HOST_SOURCE_DIR}/blender/ColorPicker.cc)
target_include_directories(bts-warp-editor-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-warp-editor-tests PRIVATE cxx_std_20)
target_compile_definitions(bts-warp-editor-tests PRIVATE BTS_STANDALONE_COLOR_PICKER)
target_link_libraries(bts-warp-editor-tests PRIVATE bts-export-config Qt6::Widgets)

add_executable(bts-image-sequence-tests ${BTS_HOST_SOURCE_DIR}/../tests/image_sequence_test.cc)
target_include_directories(bts-image-sequence-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-image-sequence-tests PRIVATE cxx_std_20)
target_link_libraries(bts-image-sequence-tests PRIVATE Qt6::Core)

add_custom_target(bts-cpu-tests)
# Discover after every target in this directory has been declared, including additions below.
function(bts_register_cpu_tests)
  get_property(_bts_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_bts_test IN LISTS _bts_test_targets)
    if(_bts_test MATCHES "^bts-.*-tests$" AND NOT _bts_test STREQUAL "bts-cpu-tests")
      target_compile_options(${_bts_test} PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
      add_dependencies(bts-cpu-tests ${_bts_test})
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL bts_register_cpu_tests)

# BTS_HOST_DOMAINS and BTS_SOURCE_INCLUDE_DIRS: the include path of every bts target.
include(${BTS_HOST_SOURCE_DIR}/SourceIncludeDirs.cmake)
function(bts_add_source_include_dirs)
  get_property(_bts_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_bts_target IN LISTS _bts_targets)
    if(_bts_target MATCHES "^bts-")
      get_target_property(_bts_type ${_bts_target} TYPE)
      if(_bts_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
        target_include_directories(${_bts_target} PRIVATE ${BTS_SOURCE_INCLUDE_DIRS})
      endif()
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL bts_add_source_include_dirs)

target_link_libraries(bts-host PRIVATE assimp::assimp bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)

# Real image encoding joins the CPU assertions; this host path never initializes OpenGL.
find_program(BTS_CPU_PYTHON_EXECUTABLE NAMES python3 python REQUIRED)
add_custom_target(bts-release-check
  COMMAND ${BTS_CPU_PYTHON_EXECUTABLE} ${BTS_HOST_SOURCE_DIR}/../../scripts/check-release.py
    --build-dir ${CMAKE_BINARY_DIR}
  DEPENDS bts-branding-tests bts-asset-tests USES_TERMINAL)

# Full reference CPU suite remains available independently of the focused release checks.
add_custom_target(bts-cpu-check
  COMMAND ${BTS_CPU_PYTHON_EXECUTABLE} ${BTS_HOST_SOURCE_DIR}/../../scripts/host/verify-cpu.py
    --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/cpu-check
  DEPENDS bts-cpu-tests bts-host USES_TERMINAL)

add_executable(bts-skew-correction-tests ${BTS_HOST_SOURCE_DIR}/../tests/skew_correction_test.cc)
target_include_directories(bts-skew-correction-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-skew-correction-tests PRIVATE cxx_std_20)
add_dependencies(bts-cpu-tests bts-skew-correction-tests)
add_executable(bts-skew-field-tests ${BTS_HOST_SOURCE_DIR}/../tests/skew_field_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-skew-field-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-skew-field-tests PRIVATE cxx_std_20)
add_dependencies(bts-cpu-tests bts-skew-field-tests)

add_executable(bts-skew-archive-tests ${BTS_HOST_SOURCE_DIR}/../tests/skew_archive_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-skew-archive-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-skew-archive-tests PRIVATE cxx_std_20)
target_link_libraries(bts-skew-archive-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-skew-archive-tests)

add_executable(bts-bake-quality-tests ${BTS_HOST_SOURCE_DIR}/../tests/bake_quality_test.cc)
target_include_directories(bts-bake-quality-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-bake-quality-tests PRIVATE cxx_std_20)
target_link_libraries(bts-bake-quality-tests PRIVATE Qt6::Core)
add_executable(bts-displacement-bake-tests ${BTS_HOST_SOURCE_DIR}/../tests/displacement_bake_test.cc)
target_include_directories(bts-displacement-bake-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-displacement-bake-tests PRIVATE cxx_std_20)
target_compile_options(bts-displacement-bake-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-displacement-bake-tests)
add_executable(bts-baked-map-tests ${BTS_HOST_SOURCE_DIR}/../tests/baked_map_shelf_test.cc)
target_include_directories(bts-baked-map-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-baked-map-tests PRIVATE cxx_std_20)
target_link_libraries(bts-baked-map-tests PRIVATE Qt6::Gui)

add_executable(bts-shader-instance-tests ${BTS_HOST_SOURCE_DIR}/../tests/shader_instances_test.cc)
target_include_directories(bts-shader-instance-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-shader-instance-tests PRIVATE cxx_std_20)
target_link_libraries(bts-shader-instance-tests PRIVATE Qt6::Core)

add_executable(bts-shader-document-tests ${BTS_HOST_SOURCE_DIR}/../tests/shader_document_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-shader-document-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-shader-document-tests PRIVATE cxx_std_20)
target_link_libraries(bts-shader-document-tests PRIVATE bts-export-config Qt6::Core)

add_executable(bts-display-post-tests ${BTS_HOST_SOURCE_DIR}/../tests/display_post_test.cc)
target_include_directories(bts-display-post-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-display-post-tests PRIVATE Qt6::Core)

add_executable(bts-lens-projection-tests ${BTS_HOST_SOURCE_DIR}/../tests/lens_projection_test.cc)
target_include_directories(bts-lens-projection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})

add_executable(bts-subsurface-tests ${BTS_HOST_SOURCE_DIR}/../tests/subsurface_test.cc)
target_include_directories(bts-subsurface-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-subsurface-tests PRIVATE cxx_std_20)
target_link_libraries(bts-subsurface-tests PRIVATE Qt6::Core)

add_executable(bts-input-space-tests ${BTS_HOST_SOURCE_DIR}/../tests/input_space_test.cc)
target_include_directories(bts-input-space-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-input-space-tests PRIVATE cxx_std_20)
target_link_libraries(bts-input-space-tests PRIVATE Qt6::Core)

add_executable(bts-derived-output-tests ${BTS_HOST_SOURCE_DIR}/../tests/derived_output_test.cc)
target_include_directories(bts-derived-output-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-derived-output-tests PRIVATE cxx_std_20)
target_link_libraries(bts-derived-output-tests PRIVATE bts-export-config Qt6::Core)

add_executable(bts-mesh-displacement-tests ${BTS_HOST_SOURCE_DIR}/../tests/mesh_displacement_test.cc)
target_include_directories(bts-mesh-displacement-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mesh-displacement-tests PRIVATE cxx_std_20)

add_executable(bts-usd-preset-tests ${BTS_HOST_SOURCE_DIR}/../tests/usd_preset_test.cc)
target_include_directories(bts-usd-preset-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-usd-preset-tests PRIVATE cxx_std_20)
target_link_libraries(bts-usd-preset-tests PRIVATE bts-export-config Qt6::Core)

add_executable(bts-folder-composition-tests ${BTS_HOST_SOURCE_DIR}/../tests/folder_composition_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/layers/FlattenLayer.cc)
target_include_directories(bts-folder-composition-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-folder-composition-tests PRIVATE cxx_std_20)
target_compile_options(bts-folder-composition-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-folder-composition-tests PRIVATE bts-export-config Qt6::Core)

add_executable(bts-flatten-layer-tests ${BTS_HOST_SOURCE_DIR}/../tests/flatten_layer_test.cc ${BTS_HOST_SOURCE_DIR}/layers/FlattenLayer.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-flatten-layer-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-flatten-layer-tests PRIVATE cxx_std_20)
target_link_libraries(bts-flatten-layer-tests PRIVATE bts-export-config Qt6::Core)

add_executable(bts-file-dialog-tests ${BTS_HOST_SOURCE_DIR}/../tests/file_dialog_test.cc ${BTS_FILE_BROWSER_WIDGET_SOURCES})
target_include_directories(bts-file-dialog-tests PRIVATE ${BTS_HOST_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/source/blender/imbuf)
target_compile_features(bts-file-dialog-tests PRIVATE cxx_std_20)
target_compile_options(bts-file-dialog-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-file-dialog-tests PRIVATE Qt6::Widgets Qt6::GuiPrivate)

add_executable(bts-layer-api-tests ${BTS_HOST_SOURCE_DIR}/../tests/layer_api_test.cc ${BTS_HOST_SOURCE_DIR}/layers/LayerApi.cc ${BTS_HOST_SOURCE_DIR}/assets/SmartMaterial.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-layer-api-tests PRIVATE bts-export-config)

add_executable(bts-channel-configuration-tests ${BTS_HOST_SOURCE_DIR}/../tests/channel_configuration_test.cc ${BTS_HOST_SOURCE_DIR}/layers/ChannelConfiguration.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-channel-configuration-tests PRIVATE bts-export-config)

add_executable(bts-fill-symmetry-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_symmetry_test.cc)
target_include_directories(bts-fill-symmetry-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-fill-symmetry-tests PRIVATE cxx_std_20)
target_link_libraries(bts-fill-symmetry-tests PRIVATE Qt6::Core)

add_executable(bts-fill-symmetry-document-tests ${BTS_HOST_SOURCE_DIR}/../tests/fill_symmetry_document_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-fill-symmetry-document-tests PRIVATE bts-export-config)

add_executable(bts-warp-geometry-tests ${BTS_HOST_SOURCE_DIR}/../tests/warp_geometry_test.cc)
target_include_directories(bts-warp-geometry-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-warp-geometry-tests PRIVATE cxx_std_20)

add_executable(bts-warp-geometry-io-tests ${BTS_HOST_SOURCE_DIR}/../tests/warp_geometry_io_test.cc)
target_include_directories(bts-warp-geometry-io-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-warp-geometry-io-tests PRIVATE cxx_std_20)
target_link_libraries(bts-warp-geometry-io-tests PRIVATE Qt6::Core)

add_executable(bts-warp-geometry-instance-tests ${BTS_HOST_SOURCE_DIR}/../tests/warp_geometry_instance_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_link_libraries(bts-warp-geometry-instance-tests PRIVATE bts-export-config)

add_executable(bts-texture-residency-policy-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_residency_policy_test.cc)
target_include_directories(bts-texture-residency-policy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-texture-residency-policy-tests PRIVATE cxx_std_20)

add_executable(bts-material-layering-tests ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_test.cc)
target_include_directories(bts-material-layering-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-material-layering-tests PRIVATE cxx_std_20)
target_link_libraries(bts-material-layering-tests PRIVATE Qt6::Core)

add_executable(bts-material-layering-document-tests ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_document_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/materials/layering/MaterialLayerParameterIO.cc)
target_link_libraries(bts-material-layering-document-tests PRIVATE bts-export-config)

add_executable(bts-symmetry-intersection-tests ${BTS_HOST_SOURCE_DIR}/../tests/symmetry_intersection_test.cc ${BTS_HOST_SOURCE_DIR}/viewport/OrbitCamera.cc)
target_include_directories(bts-symmetry-intersection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-symmetry-intersection-tests PRIVATE Qt6::Gui)
target_compile_features(bts-symmetry-intersection-tests PRIVATE cxx_std_20)
target_compile_options(bts-symmetry-intersection-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-symmetry-intersection-tests)

add_executable(bts-session-guard-tests ${BTS_HOST_SOURCE_DIR}/../tests/session_guard_test.cc)
target_include_directories(bts-session-guard-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-session-guard-tests PRIVATE Qt6::Core)
target_compile_features(bts-session-guard-tests PRIVATE cxx_std_20)
target_compile_options(bts-session-guard-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-session-guard-tests)
add_executable(bts-ui-stall-tests ${BTS_HOST_SOURCE_DIR}/../tests/ui_stall_test.cc)
target_include_directories(bts-ui-stall-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-ui-stall-tests PRIVATE Qt6::Widgets)
target_compile_features(bts-ui-stall-tests PRIVATE cxx_std_20)
add_dependencies(bts-cpu-tests bts-ui-stall-tests)

add_executable(bts-viewport-preferences-tests ${BTS_HOST_SOURCE_DIR}/../tests/viewport_texture_preferences_test.cc ${BTS_HOST_SOURCE_DIR}/app/Preferences.cc)
target_include_directories(bts-viewport-preferences-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-viewport-preferences-tests PRIVATE Qt6::Widgets)
target_compile_features(bts-viewport-preferences-tests PRIVATE cxx_std_20)
target_compile_options(bts-viewport-preferences-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-viewport-preferences-tests)

add_executable(bts-texture-resource-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_resource_test.cc)
target_include_directories(bts-texture-resource-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-texture-resource-tests PRIVATE Qt6::Core)
target_compile_features(bts-texture-resource-tests PRIVATE cxx_std_20)
target_compile_options(bts-texture-resource-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-texture-resource-tests)

add_executable(bts-composite-memory-tests ${BTS_HOST_SOURCE_DIR}/../tests/composite_memory_test.cc)
target_include_directories(bts-composite-memory-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-composite-memory-tests PRIVATE Qt6::Core)
target_compile_features(bts-composite-memory-tests PRIVATE cxx_std_20)
target_compile_options(bts-composite-memory-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-composite-memory-tests)

add_executable(bts-gpu-stack-plan-tests ${BTS_HOST_SOURCE_DIR}/../tests/gpu_stack_plan_test.cc)
target_include_directories(bts-gpu-stack-plan-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-gpu-stack-plan-tests PRIVATE cxx_std_20)
target_compile_options(bts-gpu-stack-plan-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-gpu-stack-plan-tests)

add_executable(bts-texture-io-await-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_io_await_test.cc)
target_include_directories(bts-texture-io-await-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-texture-io-await-tests PRIVATE Qt6::Widgets)
target_compile_features(bts-texture-io-await-tests PRIVATE cxx_std_20)
target_compile_options(bts-texture-io-await-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-texture-io-await-tests)

add_executable(bts-brush-preview-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_preview_test.cc)
target_include_directories(bts-brush-preview-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-brush-preview-tests PRIVATE Qt6::Widgets)
target_compile_features(bts-brush-preview-tests PRIVATE cxx_std_20)
target_compile_options(bts-brush-preview-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-brush-preview-tests)

add_executable(bts-paint-source-tests ${BTS_HOST_SOURCE_DIR}/../tests/paint_source_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc)
target_include_directories(bts-paint-source-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-paint-source-tests PRIVATE Qt6::Core)
target_compile_features(bts-paint-source-tests PRIVATE cxx_std_20)
target_compile_options(bts-paint-source-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-paint-source-tests)

# Persistence is an interactive path too. Keep the CPU worker restricted to
# explicit exports and diagnostic oracles, checked on every host build.
add_custom_target(bts-gpu-only-paths
  COMMAND ${BTS_CPU_PYTHON_EXECUTABLE} ${BTS_HOST_SOURCE_DIR}/../../scripts/host/verify-gpu-only-paths.py
  VERBATIM)
add_dependencies(bts-host bts-gpu-only-paths)
add_dependencies(bts-cpu-tests bts-gpu-only-paths)

# Sparse page backing uses Qt's portable file mapping, including standalone
# paint/brush tests which previously needed no Qt headers or Core linkage.
get_property(_bts_page_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_bts_page_target IN LISTS _bts_page_targets)
  get_target_property(_bts_page_sources ${_bts_page_target} SOURCES)
  if("${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc" IN_LIST _bts_page_sources)
    target_link_libraries(${_bts_page_target} PRIVATE Qt6::Core)
  endif()
endforeach()

add_executable(bts-history-memory-tests
  ${BTS_HOST_SOURCE_DIR}/../tests/history_memory_test.cc
  ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc
  ${BTS_HOST_SOURCE_DIR}/painting/SmudgeStroke.cc)
target_link_libraries(bts-history-memory-tests PRIVATE bts-export-config)
target_compile_options(bts-history-memory-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-history-memory-tests)

add_executable(bts-property-latency-tests ${BTS_HOST_SOURCE_DIR}/../tests/property_latency_test.cc)
target_include_directories(bts-property-latency-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-property-latency-tests PRIVATE Qt6::Core)
target_compile_features(bts-property-latency-tests PRIVATE cxx_std_20)
target_compile_options(bts-property-latency-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-property-latency-tests)

add_executable(bts-uv-contour-overlay-tests ${BTS_HOST_SOURCE_DIR}/../tests/uv_contour_overlay_test.cc)
target_include_directories(bts-uv-contour-overlay-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-uv-contour-overlay-tests PRIVATE Qt6::Gui)
target_compile_features(bts-uv-contour-overlay-tests PRIVATE cxx_std_20)
target_compile_options(bts-uv-contour-overlay-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-uv-contour-overlay-tests)

add_executable(bts-openpbr-parameter-tests ${BTS_HOST_SOURCE_DIR}/../tests/openpbr_parameters_test.cc)
target_include_directories(bts-openpbr-parameter-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-openpbr-parameter-tests PRIVATE Qt6::Core)
target_compile_features(bts-openpbr-parameter-tests PRIVATE cxx_std_20)
target_compile_options(bts-openpbr-parameter-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-openpbr-parameter-tests)

add_executable(bts-project-template-shader-tests ${BTS_HOST_SOURCE_DIR}/../tests/project_template_shader_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/persistence/ProjectTemplate.cc)
target_include_directories(bts-project-template-shader-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_link_libraries(bts-project-template-shader-tests PRIVATE bts-export-config Qt6::Core)
target_compile_features(bts-project-template-shader-tests PRIVATE cxx_std_20)
target_compile_options(bts-project-template-shader-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-project-template-shader-tests)

add_executable(bts-openpbr-import-policy-tests ${BTS_HOST_SOURCE_DIR}/../tests/openpbr_import_policy_test.cc)
target_include_directories(bts-openpbr-import-policy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-openpbr-import-policy-tests PRIVATE cxx_std_20)
target_compile_options(bts-openpbr-import-policy-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-openpbr-import-policy-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-openpbr-import-policy-tests)

if(WITH_MATERIALX)
  file(READ ${BTS_HOST_SOURCE_DIR}/openpbr/open_pbr_surface.mtlx BTS_OPENPBR_XML)
  file(READ ${BTS_HOST_SOURCE_DIR}/openpbr/LICENSE BTS_OPENPBR_LICENSE)
  configure_file(${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrReferenceData.hh.in ${CMAKE_CURRENT_BINARY_DIR}/OpenPbrReferenceData.hh @ONLY)
  add_library(bts-openpbr-materialx STATIC ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrMaterialX.cc)
  target_include_directories(bts-openpbr-materialx PUBLIC ${BTS_HOST_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_compile_features(bts-openpbr-materialx PUBLIC cxx_std_20)
  target_link_libraries(bts-openpbr-materialx PUBLIC Qt6::Core PRIVATE MaterialXCore MaterialXFormat)
  add_executable(bts-openpbr-materialx-tests ${BTS_HOST_SOURCE_DIR}/../tests/openpbr_materialx_test.cc)
  target_link_libraries(bts-openpbr-materialx-tests PRIVATE bts-openpbr-materialx MaterialXCore MaterialXFormat)
  target_include_directories(bts-openpbr-materialx-tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_compile_options(bts-openpbr-materialx-tests PRIVATE -UNDEBUG)
  target_compile_definitions(bts-openpbr-materialx-tests PRIVATE BTS_MATERIALX_BASE="${MATERIALX_BASE_DIR}")
  add_dependencies(bts-cpu-tests bts-openpbr-materialx-tests)
  add_library(bts-openpbr-usd STATIC ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrUsd.cc ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrUsdImport.cc ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrUsdResources.cc)
  target_include_directories(bts-openpbr-usd PUBLIC ${BTS_HOST_SOURCE_DIR})
  target_compile_features(bts-openpbr-usd PRIVATE cxx_std_20)
  target_compile_definitions(bts-openpbr-usd PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(bts-openpbr-usd PUBLIC Qt6::Core PRIVATE MaterialXCore MaterialXFormat bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)
  add_executable(bts-openpbr-usd-tests ${BTS_HOST_SOURCE_DIR}/../tests/openpbr_usd_test.cc)
  target_compile_features(bts-openpbr-usd-tests PRIVATE cxx_std_20)
  target_compile_options(bts-openpbr-usd-tests PRIVATE -UNDEBUG)
  target_compile_definitions(bts-openpbr-usd-tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(bts-openpbr-usd-tests PRIVATE bts-openpbr-materialx bts-openpbr-usd MaterialXCore MaterialXFormat bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)
  setup_platform_linker_libs(bts-openpbr-usd-tests)
  add_dependencies(bts-cpu-tests bts-openpbr-usd-tests)
  add_executable(bts-openpbr-usd-resource-tests ${BTS_HOST_SOURCE_DIR}/../tests/openpbr_usd_resources_test.cc)
  target_compile_features(bts-openpbr-usd-resource-tests PRIVATE cxx_std_20)
  target_compile_options(bts-openpbr-usd-resource-tests PRIVATE -UNDEBUG)
  target_compile_definitions(bts-openpbr-usd-resource-tests PRIVATE QT_NO_KEYWORDS)
  target_link_libraries(bts-openpbr-usd-resource-tests PRIVATE bts-openpbr-materialx bts-openpbr-usd Qt6::Core bf::dependencies::optional::usd bf::dependencies::optional::tbb bf::dependencies::optional::python)
  setup_platform_linker_libs(bts-openpbr-usd-resource-tests)
  add_dependencies(bts-cpu-tests bts-openpbr-usd-resource-tests)


  add_library(bts-openpbr-texture-package STATIC ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrTexturePackage.cc)
  target_link_libraries(bts-openpbr-texture-package PUBLIC bts-export-config bts-openpbr-materialx bts-openpbr-usd Qt6::Core)
  target_link_libraries(bts-host PRIVATE bts-openpbr-texture-package)
  target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../harness/OpenPbrExportCpuTest.cc ${BTS_HOST_SOURCE_DIR}/materials/openpbr/OpenPbrMaterialImport.cc)
  set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../harness/OpenPbrExportCpuTest.cc PROPERTIES INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:MaterialXCore,INTERFACE_INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:MaterialXFormat,INTERFACE_INCLUDE_DIRECTORIES>")
  set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/TextureExport.cc ${BTS_HOST_SOURCE_DIR}/ui/dialogs/TextureExportDialog.cc ${BTS_HOST_SOURCE_DIR}/../harness/ExportCpuDemo.cc ${BTS_HOST_SOURCE_DIR}/blender/MeshImport.cc ${BTS_HOST_SOURCE_DIR}/main.cc APPEND PROPERTY COMPILE_DEFINITIONS BTS_WITH_MATERIALX)
endif()

# The Linux prebuilt monolithic USD library requires these shared libraries.
# Standalone CPU tests do not inherit the host's full dependency closure.
if(UNIX AND NOT APPLE)
  foreach(_bts_usd_cpu_target bts-usd-scene-tests bts-physical-scale-tests bts-openpbr-usd-tests bts-openpbr-usd-resource-tests)
    if(TARGET ${_bts_usd_cpu_target})
      target_link_libraries(${_bts_usd_cpu_target} PRIVATE
        bf::dependencies::optional::openvdb
        bf::dependencies::openimageio
        bf::dependencies::optional::opensubdiv)
    endif()
  endforeach()
endif()


add_executable(bts-blend-selection-tests ${BTS_HOST_SOURCE_DIR}/../tests/blend_selection_test.cc)
target_include_directories(bts-blend-selection-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-blend-selection-tests PRIVATE cxx_std_20)
target_compile_options(bts-blend-selection-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-blend-selection-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-blend-selection-tests)

add_executable(bts-layer-channel-tests ${BTS_HOST_SOURCE_DIR}/../tests/layer_channel_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-layer-channel-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-layer-channel-tests PRIVATE cxx_std_20)
target_link_libraries(bts-layer-channel-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-layer-channel-tests)

add_executable(bts-levels-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/levels_editor_ui_test.cc)
target_include_directories(bts-levels-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-levels-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-levels-ui-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-levels-ui-tests PRIVATE bts-export-config Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-levels-ui-tests)

add_executable(bts-baking-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/baking_ui_test.cc ${BTS_HOST_SOURCE_DIR}/baking/SkewPanel.cc ${BTS_FILE_BROWSER_WIDGET_SOURCES})
target_include_directories(bts-baking-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/source/blender/imbuf)
target_compile_features(bts-baking-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-baking-ui-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-baking-ui-tests PRIVATE Qt6::Widgets Qt6::GuiPrivate)
add_dependencies(bts-cpu-tests bts-baking-ui-tests)

add_executable(bts-rename-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/rename_ui_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-rename-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-rename-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-rename-ui-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-rename-ui-tests PRIVATE bts-export-config Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-rename-ui-tests)

add_executable(bts-levels-mix-tests ${BTS_HOST_SOURCE_DIR}/../tests/levels_mix_test.cc)
target_include_directories(bts-levels-mix-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-levels-mix-tests PRIVATE cxx_std_20)
target_compile_options(bts-levels-mix-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-levels-mix-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-levels-mix-tests)

add_executable(bts-painter-workflow-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/painter_workflow_ui_test.cc)
target_include_directories(bts-painter-workflow-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-painter-workflow-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-painter-workflow-ui-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-painter-workflow-ui-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-painter-workflow-ui-tests)

add_executable(bts-painter-workflow-document-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/painter_workflow_document_ui_test.cc)
target_include_directories(bts-painter-workflow-document-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-painter-workflow-document-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-painter-workflow-document-ui-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-painter-workflow-document-ui-tests PRIVATE bts-export-config Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-painter-workflow-document-ui-tests)

add_executable(bts-layer-drop-tests ${BTS_HOST_SOURCE_DIR}/../tests/layer_drop_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-layer-drop-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-layer-drop-tests PRIVATE cxx_std_20)
target_compile_options(bts-layer-drop-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-layer-drop-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-layer-drop-tests)

add_executable(bts-temporary-ink-tests ${BTS_HOST_SOURCE_DIR}/../tests/temporary_ink_test.cc)
target_include_directories(bts-temporary-ink-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-temporary-ink-tests PRIVATE cxx_std_20)
target_compile_options(bts-temporary-ink-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-temporary-ink-tests)

target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../harness/StrokeTargetPerformanceDemo.cc)

add_executable(bts-brush-bounds-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_bounds_test.cc)
target_include_directories(bts-brush-bounds-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-brush-bounds-tests PRIVATE cxx_std_20)
target_compile_options(bts-brush-bounds-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-brush-bounds-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-brush-bounds-tests)

target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../harness/MaskFillGpuDemo.cc)

add_executable(bts-numeric-locale-tests ${BTS_HOST_SOURCE_DIR}/../tests/numeric_locale_test.cc ${BTS_HOST_SOURCE_DIR}/ui/panels/DisplacementPanel.cc)
target_include_directories(bts-numeric-locale-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-numeric-locale-tests PRIVATE cxx_std_20)
target_compile_options(bts-numeric-locale-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-numeric-locale-tests PRIVATE bts-export-config Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-numeric-locale-tests)

add_executable(bts-height-neutral-tests ${BTS_HOST_SOURCE_DIR}/../tests/height_neutral_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-height-neutral-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-height-neutral-tests PRIVATE cxx_std_20)
target_compile_options(bts-height-neutral-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-height-neutral-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-height-neutral-tests)

add_executable(bts-branding-tests ${BTS_HOST_SOURCE_DIR}/../tests/branding_test.cc ${BTS_HOST_SOURCE_DIR}/app/Branding.cc ${BTS_BRANDING_RESOURCES})
target_include_directories(bts-branding-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-branding-tests PRIVATE cxx_std_20)
target_compile_options(bts-branding-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-branding-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-branding-tests)

add_executable(bts-bake-source-tests ${BTS_HOST_SOURCE_DIR}/../tests/bake_source_test.cc)
target_include_directories(bts-bake-source-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-bake-source-tests PRIVATE cxx_std_20)
target_compile_options(bts-bake-source-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-bake-source-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-bake-source-tests)

add_executable(bts-native-material-object-table-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_object_table_test.cc)
target_include_directories(bts-native-material-object-table-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-object-table-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-object-table-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-native-material-object-table-tests)

add_executable(bts-native-material-source-policy-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_source_policy_test.cc)
target_include_directories(bts-native-material-source-policy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-source-policy-tests PRIVATE cxx_std_20)
add_dependencies(bts-cpu-tests bts-native-material-source-policy-tests)
add_test(NAME native-material-source-policy-cpu COMMAND bts-native-material-source-policy-tests)

add_executable(bts-native-material-parameters-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_parameters_test.cc)
target_include_directories(bts-native-material-parameters-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-parameters-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-parameters-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-parameters-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-material-parameters-tests)

add_executable(bts-native-material-mapping-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_mapping_test.cc)
target_include_directories(bts-native-material-mapping-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-mapping-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-mapping-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-mapping-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-material-mapping-tests)

add_executable(bts-native-cycles-publication-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_cycles_publication_test.cc)
target_include_directories(bts-native-cycles-publication-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-cycles-publication-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-cycles-publication-tests PRIVATE -UNDEBUG)
add_dependencies(bts-cpu-tests bts-native-cycles-publication-tests)

add_executable(bts-native-cycles-revisions-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_cycles_revisions_test.cc)
target_include_directories(bts-native-cycles-revisions-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-cycles-revisions-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-cycles-revisions-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-native-cycles-revisions-tests)

add_executable(bts-native-material-fill-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_fill_test.cc)
target_include_directories(bts-native-material-fill-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-fill-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-fill-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-fill-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-material-fill-tests)
add_executable(bts-native-material-projection-context-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_projection_context_test.cc)
target_include_directories(bts-native-material-projection-context-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-projection-context-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-projection-context-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-projection-context-tests PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(bts-cpu-tests bts-native-material-projection-context-tests)

add_executable(bts-native-material-edit-diff-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_edit_diff_test.cc)
target_include_directories(bts-native-material-edit-diff-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-edit-diff-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-edit-diff-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-edit-diff-tests PRIVATE bts-export-config Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-material-edit-diff-tests)

add_executable(bts-native-material-bundle-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_bundle_test.cc)
target_include_directories(bts-native-material-bundle-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-material-bundle-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-material-bundle-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-native-material-bundle-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-material-bundle-tests)

add_executable(bts-native-image-variant-metadata-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_image_variant_metadata_test.cc)
target_include_directories(bts-native-image-variant-metadata-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-image-variant-metadata-tests PRIVATE cxx_std_20)
target_link_libraries(bts-native-image-variant-metadata-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-image-variant-metadata-tests)

add_executable(bts-document-edit-commit-tests ${BTS_HOST_SOURCE_DIR}/../tests/document_edit_commit_test.cc)
target_include_directories(bts-document-edit-commit-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-document-edit-commit-tests PRIVATE cxx_std_20)
add_dependencies(bts-cpu-tests bts-document-edit-commit-tests)

add_executable(bts-native-image-panel-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_material_image_panel_test.cc ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialImagePanel.cc)
target_include_directories(bts-native-image-panel-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-image-panel-tests PRIVATE cxx_std_20)
target_link_libraries(bts-native-image-panel-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-native-image-panel-tests)


add_executable(bts-resource-budget-tests ${BTS_HOST_SOURCE_DIR}/../tests/resource_budget_test.cc)
target_include_directories(bts-resource-budget-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-resource-budget-tests PRIVATE cxx_std_20)
target_compile_options(bts-resource-budget-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-resource-budget-tests)

add_executable(bts-disk-scratch-tests ${BTS_HOST_SOURCE_DIR}/../tests/disk_scratch_test.cc)
target_include_directories(bts-disk-scratch-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-disk-scratch-tests PRIVATE cxx_std_20)
target_link_libraries(bts-disk-scratch-tests PRIVATE Qt6::Core)
add_test(NAME disk-scratch-full-resolution COMMAND bts-disk-scratch-tests)

add_executable(bts-texture-set-selection-ui-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_set_selection_ui_test.cc)
target_include_directories(bts-texture-set-selection-ui-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-texture-set-selection-ui-tests PRIVATE cxx_std_20)
target_compile_options(bts-texture-set-selection-ui-tests PRIVATE -UNDEBUG)
target_link_libraries(bts-texture-set-selection-ui-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-texture-set-selection-ui-tests)

# Descriptor, template and editing admission checks run without native image I/O.
add_executable(bts-resource-caps-tests
  ${BTS_HOST_SOURCE_DIR}/../tests/resource_caps_test.cc
  ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc
  ${BTS_HOST_SOURCE_DIR}/persistence/ProjectTemplate.cc)
target_include_directories(bts-resource-caps-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-resource-caps-tests PRIVATE cxx_std_20)
target_compile_options(bts-resource-caps-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-resource-caps-tests PRIVATE bts-export-config Qt6::Gui)
add_dependencies(bts-cpu-tests bts-resource-caps-tests)

# Sparse metadata tests use real page-key bounds and never allocate dense textures.
add_executable(bts-texture-extent-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_extent_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeMetadata.cc ${BTS_HOST_SOURCE_DIR}/io/SurfacePathIO.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc)
target_include_directories(bts-texture-extent-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-texture-extent-tests PRIVATE cxx_std_20)
target_compile_options(bts-texture-extent-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-texture-extent-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-texture-extent-tests)

# Worker delivery, cancellation and disposal must remain nonblocking on the UI.
add_executable(bts-owner-worker-job-tests ${BTS_HOST_SOURCE_DIR}/../tests/owner_worker_job_test.cc)
target_include_directories(bts-owner-worker-job-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-owner-worker-job-tests PRIVATE cxx_std_20)
target_compile_options(bts-owner-worker-job-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-owner-worker-job-tests PRIVATE Qt6::Widgets bts-export-config)
add_dependencies(bts-cpu-tests bts-owner-worker-job-tests)

# Ordered raw linear RGB must preserve source values until its explicit matrix.
add_executable(bts-linear-color-transform-tests ${BTS_HOST_SOURCE_DIR}/../tests/linear_color_transform_test.cc ${BTS_HOST_SOURCE_DIR}/layers/LayerApi.cc ${BTS_HOST_SOURCE_DIR}/assets/SmartMaterial.cc)
target_link_libraries(bts-linear-color-transform-tests PRIVATE bts-export-config)
target_compile_options(bts-linear-color-transform-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-linear-color-transform-tests)

# Reuse the real decoder/controller regression in the standard native CPU mode.
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../tests/ordinary_color_source_test.cc)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../tests/ordinary_color_source_test.cc
  PROPERTIES COMPILE_DEFINITIONS BTS_EMBED_SOURCE_REGRESSIONS=1)

# Synthetic ordered RGB lowering is fixture-free and runs once in native CPU mode.
# Source-level include properties bypass CMake's usual filtering of compiler
# implicit paths. Re-adding /usr/include as -isystem breaks GCC include_next.
get_target_property(BTS_HDF5_FIXTURE_INCLUDES bts-hdf5 INTERFACE_INCLUDE_DIRECTORIES)
# The lowering fixture this file pulls in includes <lz4.h> as well as <hdf5.h>,
# and lz4 belongs to the bitmaps library alone, so it is not on bts-host's own
# include path. Where lz4 is built from the pinned source there is no system
# copy to fall back on, which is where the Windows build stopped.
get_target_property(BTS_LZ4_FIXTURE_INCLUDES bts-lz4-usage INTERFACE_INCLUDE_DIRECTORIES)
if(BTS_LZ4_FIXTURE_INCLUDES)
  list(APPEND BTS_HDF5_FIXTURE_INCLUDES ${BTS_LZ4_FIXTURE_INCLUDES})
endif()
list(REMOVE_ITEM BTS_HDF5_FIXTURE_INCLUDES ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../tests/spsm_bitmap_color_test.cc)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../tests/spsm_bitmap_color_test.cc
  PROPERTIES
    COMPILE_DEFINITIONS "BTS_EMBED_SOURCE_REGRESSIONS=1;$<TARGET_PROPERTY:bts-hdf5,INTERFACE_COMPILE_DEFINITIONS>"
    INCLUDE_DIRECTORIES "${BTS_HDF5_FIXTURE_INCLUDES}")

# Instance interpretation is explicit, strict and separate from source metadata.
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialHeightResource.cc)
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialHeightProject.cc)
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/blender/NativeMaterialHeightContext.cc)
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/persistence/PendingProjectGpu.cc)
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/persistence/ProjectOpenJob.cc)
add_executable(bts-native-height-policy-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_height_policy_test.cc)
target_include_directories(bts-native-height-policy-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-height-policy-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-height-policy-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-native-height-policy-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-height-policy-tests)

# Independent literal raw-image reference used by the explicit Height GPU actor.
add_executable(bts-native-height-image-reference-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_height_image_reference_test.cc)
target_compile_features(bts-native-height-image-reference-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-height-image-reference-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-native-height-image-reference-tests)
add_executable(bts-native-height-image-retention-tests ${BTS_HOST_SOURCE_DIR}/../tests/native_height_image_retention_test.cc)
target_include_directories(bts-native-height-image-retention-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-native-height-image-retention-tests PRIVATE cxx_std_20)
target_compile_options(bts-native-height-image-retention-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-native-height-image-retention-tests PRIVATE Qt6::Core)
add_dependencies(bts-cpu-tests bts-native-height-image-retention-tests)

target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialHeightGpu.cc ${BTS_HOST_SOURCE_DIR}/materials/native/NativeMaterialHeightUi.cc)

# Real WorkspaceShell/FillPanel hide-time editor regression, before Blender startup.
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../tests/workspace_shutdown_test.cc)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../tests/workspace_shutdown_test.cc PROPERTIES
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")

add_executable(bts-material-layering-authoring-tests ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_authoring_test.cc)
target_link_libraries(bts-material-layering-authoring-tests PRIVATE bts-export-config)
add_dependencies(bts-cpu-tests bts-material-layering-authoring-tests)
# Real picker conversion and retirement run in the existing host CPU dispatcher.
# Declaration-default admission needs the node declaration translation header.
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayerParameterSurface.cc APPEND PROPERTY
  INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/source/blender/blentranslation")
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/materials/layering/MaterialLayerParameterImportJob.cc ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayerParameterReadback.cc ${BTS_HOST_SOURCE_DIR}/blender/MaterialLayerParameterSurface.cc ${BTS_HOST_SOURCE_DIR}/materials/layering/MaterialLayerParameterIO.cc ${BTS_HOST_SOURCE_DIR}/ui/panels/MaterialLayeringPanel.cc ${BTS_HOST_SOURCE_DIR}/materials/layering/MaterialLayeringUi.cc ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_panel_test.cc ${BTS_HOST_SOURCE_DIR}/../tests/shader_controller_target_test.cc ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_source_pixels_test.cc)

# Keep the opaque loader probe on the real Cycles ABI and flags.
if(TARGET bf_intern_cycles)
  get_filename_component(BTS_MATERIAL_LAYER_SOURCE_PROBE
    "${BTS_HOST_SOURCE_DIR}/../tests/material_layering_cycles_source_probe.cc" ABSOLUTE)
  target_sources(bf_intern_cycles PRIVATE "${BTS_MATERIAL_LAYER_SOURCE_PROBE}")
  set_property(SOURCE "${BTS_HOST_SOURCE_DIR}/../tests/material_layering_source_pixels_test.cc"
    APPEND PROPERTY COMPILE_DEFINITIONS BTS_MATERIAL_LAYER_SOURCE_LOADER_TEST)
  target_sources(bts-host PRIVATE
    "${BTS_HOST_SOURCE_DIR}/../tests/material_layering_sources_gpu_test.cc")
  set_property(SOURCE "${BTS_HOST_SOURCE_DIR}/../harness/ViewportHeadlessDemo.cc"
    APPEND PROPERTY COMPILE_DEFINITIONS BTS_MATERIAL_LAYER_SOURCE_GPU_TEST)
endif()

set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../tests/material_layering_authoring_test.cc ${BTS_HOST_SOURCE_DIR}/../tests/material_layering_panel_test.cc ${BTS_HOST_SOURCE_DIR}/../tests/shader_controller_target_test.cc PROPERTIES
  COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")

# Fixed-pair async admission/drain assertions; verify-cpu discovers this target.
add_executable(bts-gpu-program-warmup-tests ${BTS_HOST_SOURCE_DIR}/../tests/gpu_program_warmup_test.cc)
target_include_directories(bts-gpu-program-warmup-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-gpu-program-warmup-tests PRIVATE cxx_std_20)
target_compile_options(bts-gpu-program-warmup-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_dependencies(bts-cpu-tests bts-gpu-program-warmup-tests)

# Widget behavior tests are independent of the Blender runtime; this companion
# exercises the production native image and embedded blend thumbnail decoder.
add_executable(bts-file-browser-native-check EXCLUDE_FROM_ALL ${SRC} ${BTS_HOST_SOURCE_DIR}/../tests/file_browser_native_test.cc ${BTS_HOST_SOURCE_DIR}/blender/FileBrowserNativeThumbnails.cc ${BTS_FILE_BROWSER_WIDGET_SOURCES})
# BLO thumbnail reads share Blender's real creator symbols and complete target
# dependency closure. ${LIB} is mutable in included CMake files; do not reuse its
# late value here or guess a subset of static libraries.
# TARGET_PROPERTY hands back the property's raw text, and generator
# expressions inside that text are not evaluated again, so they would be
# written into the build graph verbatim: Blender's pinned Windows libraries
# arrive as per-config expressions, and ninja refuses a whole graph over one
# unexpanded dollar. GENEX_EVAL evaluates what comes back, and does nothing
# where there is nothing to evaluate.
target_include_directories(bts-file-browser-native-check PRIVATE $<GENEX_EVAL:$<TARGET_PROPERTY:bts-host,INCLUDE_DIRECTORIES>>)
target_compile_definitions(bts-file-browser-native-check PRIVATE $<GENEX_EVAL:$<TARGET_PROPERTY:bts-host,COMPILE_DEFINITIONS>>)
target_link_options(bts-file-browser-native-check PRIVATE $<GENEX_EVAL:$<TARGET_PROPERTY:bts-host,LINK_OPTIONS>>)
get_target_property(_bts_browser_link_flags bts-host LINK_FLAGS)
if(_bts_browser_link_flags)
  set_target_properties(bts-file-browser-native-check PROPERTIES LINK_FLAGS "${_bts_browser_link_flags}")
endif()
add_dependencies(bts-file-browser-native-check makesdna)
if(WITH_BUILDINFO)
  add_dependencies(bts-file-browser-native-check buildinfo)
endif()
target_compile_features(bts-file-browser-native-check PRIVATE cxx_std_20)
# Keep creator_args.cc ABI consistent with Blender's Release libraries; only
# the test actor needs assertions (unsetting NDEBUG on creator enables helpers
# that are deliberately absent from Release blenlib).
set_property(SOURCE ${BTS_HOST_SOURCE_DIR}/../tests/file_browser_native_test.cc
  APPEND PROPERTY COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-file-browser-native-check PRIVATE $<GENEX_EVAL:$<TARGET_PROPERTY:bts-host,LINK_LIBRARIES>>)

# This fixture-aware check deliberately stays outside verify-cpu.py's no-args
# bts-*-tests glob. CTest supplies explicit pinned fixtures and isolated paths.
# Other platforms use different native cache roots; do not run this Linux XDG
# isolation wrapper against an artist's Windows/macOS thumbnail cache.
if(UNIX AND NOT APPLE)
  add_test(NAME bts-file-browser-native
    COMMAND ${CMAKE_COMMAND}
      -DBROWSER_EXECUTABLE=$<TARGET_FILE:bts-file-browser-native-check>
      -DBROWSER_BLEND=${CMAKE_SOURCE_DIR}/release/datafiles/preview_grease_pencil.blend
      -DBROWSER_OCIO=${CMAKE_SOURCE_DIR}/release/datafiles/colormanagement/config.ocio
      -DBROWSER_OUTPUT_PARENT=${CMAKE_CURRENT_BINARY_DIR}/file-browser-native-check
      -P ${BTS_HOST_SOURCE_DIR}/../tests/run_file_browser_native.cmake)
  set_tests_properties(bts-file-browser-native PROPERTIES LABELS "bts;native;offscreen" TIMEOUT 45)
endif()

add_executable(bts-mask-effect-path-tests ${BTS_HOST_SOURCE_DIR}/../tests/mask_effect_path_test.cc)
target_include_directories(bts-mask-effect-path-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-mask-effect-path-tests PRIVATE cxx_std_20)
target_compile_options(bts-mask-effect-path-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-mask-effect-path-tests PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(bts-cpu-tests bts-mask-effect-path-tests)

add_executable(bts-nested-mask-model-tests ${BTS_HOST_SOURCE_DIR}/../tests/nested_mask_model_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-nested-mask-model-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-nested-mask-model-tests PRIVATE cxx_std_20)
target_compile_options(bts-nested-mask-model-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-nested-mask-model-tests PRIVATE bts-export-config)
add_dependencies(bts-cpu-tests bts-nested-mask-model-tests)

add_executable(bts-folder-mask-target-tests ${BTS_HOST_SOURCE_DIR}/../tests/folder_mask_target_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-folder-mask-target-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-folder-mask-target-tests PRIVATE cxx_std_20)
target_link_libraries(bts-folder-mask-target-tests PRIVATE Qt6::Core Qt6::Gui bts-export-config)
add_dependencies(bts-cpu-tests bts-folder-mask-target-tests)



add_executable(bts-generator-surface-tests ${BTS_HOST_SOURCE_DIR}/../tests/generator_surface_test.cc)
target_include_directories(bts-generator-surface-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-generator-surface-tests PRIVATE cxx_std_20)
target_compile_options(bts-generator-surface-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-generator-surface-tests PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(bts-cpu-tests bts-generator-surface-tests)

add_executable(bts-smart-mask-model-tests ${BTS_HOST_SOURCE_DIR}/../tests/smart_mask_model_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_include_directories(bts-smart-mask-model-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-smart-mask-model-tests PRIVATE cxx_std_20)
target_compile_options(bts-smart-mask-model-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-smart-mask-model-tests PRIVATE bts-builtin-smartmasks)
add_dependencies(bts-cpu-tests bts-smart-mask-model-tests)

# Catalog consumers share one resource decoder/recipe implementation. The qrc
# initializer is referenced explicitly, including when linked from a static lib.
get_property(_bts_catalog_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_bts_target IN LISTS _bts_catalog_targets)
  get_target_property(_bts_sources ${_bts_target} SOURCES)
  foreach(_bts_source IN LISTS _bts_sources)
    if(_bts_source MATCHES "/AssetCatalog[.]cc$")
      target_link_libraries(${_bts_target} PRIVATE bts-builtin-smartmasks)
      break()
    endif()
  endforeach()
endforeach()

# Portable native import boundaries run in the ordinary CPU suite too. Keep
# this independent of Blender/host initialization and the document archive.
add_library(bts-spsm-native-lowering-cpu STATIC
  ${BTS_HOST_SOURCE_DIR}/spsm-import/SpsmMaterialImport.cc
  ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc
  ${BTS_HOST_SOURCE_DIR}/painting/StrokeMetadata.cc
  ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc
  ${BTS_HOST_SOURCE_DIR}/io/SurfacePathIO.cc
 )
target_include_directories(bts-spsm-native-lowering-cpu PUBLIC ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-spsm-native-lowering-cpu PUBLIC cxx_std_20)
target_link_libraries(bts-spsm-native-lowering-cpu PUBLIC bts-spsm-semantic Qt6::Widgets Qt6::OpenGL)
add_executable(bts-spsm-native-lowering-tests ${BTS_HOST_SOURCE_DIR}/../tests/spsm_native_lowering_test.cc)
target_compile_options(bts-spsm-native-lowering-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-spsm-native-lowering-tests PRIVATE bts-spsm-native-lowering-cpu)
add_dependencies(bts-cpu-tests bts-spsm-native-lowering-tests)
add_test(NAME spmsk-native-synthetic-boundaries COMMAND bts-spsm-native-lowering-tests)
set(BTS_SPMSK_FIXTURE_DIR "" CACHE PATH "Optional local directory containing the fourteen DataSmartMask fixtures")
if(BTS_SPMSK_FIXTURE_DIR)
 file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spmsk-empty-material-fixtures")
 add_test(NAME spmsk-native-fourteen-files COMMAND bts-spsm-native-lowering-tests
  "${CMAKE_CURRENT_BINARY_DIR}/spmsk-empty-material-fixtures" "${BTS_SPMSK_FIXTURE_DIR}")
endif()

target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../harness/SmartMaskNativeProof.cc)

# Standalone mask generators use a synthetic Group owner. Exercise actual embedded
# image resource reading without Blender globals or color interpretation.
add_executable(bts-spmsk-embedded-bitmap-tests ${BTS_HOST_SOURCE_DIR}/../tests/spmsk_embedded_bitmap_test.cc)
target_compile_options(bts-spmsk-embedded-bitmap-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_include_directories(bts-spmsk-embedded-bitmap-tests PRIVATE
  $<TARGET_PROPERTY:bts-spsm-reader,INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:bts-spsm-bitmaps,INCLUDE_DIRECTORIES>)
target_link_libraries(bts-spmsk-embedded-bitmap-tests PRIVATE bts-spsm-native-lowering-cpu bts-spsm-bitmaps bts-hdf5)
add_dependencies(bts-cpu-tests bts-spmsk-embedded-bitmap-tests)
add_test(NAME spmsk-embedded-image-boundary COMMAND bts-spmsk-embedded-bitmap-tests)

# Complete portable brush assets share one codec across save/import/catalog apply.
add_executable(bts-brush-preset-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_preset_test.cc)
target_include_directories(bts-brush-preset-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-brush-preset-tests PRIVATE cxx_std_20)
target_compile_options(bts-brush-preset-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-brush-preset-tests PRIVATE Qt6::Core)
add_executable(bts-brush-preset-catalog-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_preset_catalog_test.cc ${BTS_HOST_SOURCE_DIR}/assets/AssetCatalog.cc)
target_include_directories(bts-brush-preset-catalog-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-brush-preset-catalog-tests PRIVATE cxx_std_20)
target_compile_options(bts-brush-preset-catalog-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-brush-preset-catalog-tests PRIVATE bts-builtin-smartmasks Qt6::Core Qt6::Gui bts-export-config)
add_dependencies(bts-cpu-tests bts-brush-preset-tests bts-brush-preset-catalog-tests)
add_test(NAME brush-presets-complete-codec COMMAND bts-brush-preset-tests)
add_test(NAME brush-presets-user-catalog COMMAND bts-brush-preset-catalog-tests)

# One GPU owner handles both Shelf brush thumbnails and the property swatch.
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/painting/BrushPresetThumbnailJobs.cc)
add_executable(bts-brush-preset-thumbnail-stroke-tests ${BTS_HOST_SOURCE_DIR}/../tests/brush_preset_thumbnail_stroke_test.cc)
target_include_directories(bts-brush-preset-thumbnail-stroke-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-brush-preset-thumbnail-stroke-tests PRIVATE cxx_std_20)
target_compile_options(bts-brush-preset-thumbnail-stroke-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-brush-preset-thumbnail-stroke-tests PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(bts-cpu-tests bts-brush-preset-thumbnail-stroke-tests)
add_test(NAME brush-presets-thumbnail-stroke-metadata COMMAND bts-brush-preset-thumbnail-stroke-tests)

# BM metadata admission/routing; no pixel evaluator or GPU dependency.
add_executable(bts-adjustment-paint-plan-tests ${BTS_HOST_SOURCE_DIR}/../tests/adjustment_paint_plan_test.cc)
target_include_directories(bts-adjustment-paint-plan-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-adjustment-paint-plan-tests PRIVATE cxx_std_20)
target_compile_options(bts-adjustment-paint-plan-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-adjustment-paint-plan-tests PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(bts-cpu-tests bts-adjustment-paint-plan-tests)
add_test(NAME adjustment-paint-plan COMMAND bts-adjustment-paint-plan-tests)

add_dependencies(bts-cpu-tests bts-trackball-tests)

add_executable(bts-reference-image-data-tests ${BTS_HOST_SOURCE_DIR}/../tests/reference_image_data_test.cc)
target_include_directories(bts-reference-image-data-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-reference-image-data-tests PRIVATE cxx_std_20)
target_compile_options(bts-reference-image-data-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-reference-image-data-tests PRIVATE Qt6::Gui)
add_dependencies(bts-cpu-tests bts-reference-image-data-tests)
add_test(NAME reference-image-data-cpu COMMAND bts-reference-image-data-tests)

add_executable(bts-reference-image-tests
  ${BTS_HOST_SOURCE_DIR}/../tests/reference_image_test.cc
  ${BTS_HOST_SOURCE_DIR}/blender/ColorPicker.cc
  ${BTS_HOST_SOURCE_DIR}/app/Preferences.cc
  ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-reference-image-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-reference-image-tests PRIVATE cxx_std_20)
target_compile_definitions(bts-reference-image-tests PRIVATE BTS_STANDALONE_COLOR_PICKER)
target_compile_options(bts-reference-image-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-reference-image-tests PRIVATE bts-export-config Qt6::Widgets)
add_dependencies(bts-cpu-tests bts-reference-image-tests)
add_test(NAME reference-image-cpu COMMAND bts-reference-image-tests)

# Explicit CPU runner manifest. Unrelated executables left in bin/ must never
# become CPU tests merely because their filename happens to end in "-tests".
# Explicit offscreen CPU reference artifacts; never used by authoring/viewport paths.
add_executable(bts-smart-mask-render ${BTS_HOST_SOURCE_DIR}/../tests/smart_mask_render.cc)
target_link_libraries(bts-smart-mask-render PRIVATE bts-spsm-native-lowering-cpu bts-builtin-smartmasks)

function(bts_write_cpu_manifest)
  get_property(BTS_CPU_TEST_TARGETS TARGET bts-cpu-tests PROPERTY MANUALLY_ADDED_DEPENDENCIES)
  list(SORT BTS_CPU_TEST_TARGETS)
  set(BTS_CPU_TEST_MANIFEST "{\"version\":1,\"configuration\":\"$<CONFIG>\",\"binaries\":[")
  set(BTS_CPU_TEST_SEPARATOR "")
  foreach(BTS_CPU_TEST_TARGET IN LISTS BTS_CPU_TEST_TARGETS)
    get_target_property(BTS_CPU_TEST_TYPE ${BTS_CPU_TEST_TARGET} TYPE)
    # Build-time source checks are run by their custom target, not as binaries.
    if(BTS_CPU_TEST_TYPE STREQUAL "UTILITY")
      continue()
    endif()
    if(NOT BTS_CPU_TEST_TYPE STREQUAL "EXECUTABLE")
      message(FATAL_ERROR "CPU test group contains a non-executable: ${BTS_CPU_TEST_TARGET}")
    endif()
    string(APPEND BTS_CPU_TEST_MANIFEST "${BTS_CPU_TEST_SEPARATOR}\"$<TARGET_FILE_NAME:${BTS_CPU_TEST_TARGET}>\"")
    set(BTS_CPU_TEST_SEPARATOR ",")
  endforeach()
  string(APPEND BTS_CPU_TEST_MANIFEST "]}\n")
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/bts-cpu-tests-$<CONFIG>.json" CONTENT "${BTS_CPU_TEST_MANIFEST}")
endfunction()
# Registration above is deferred too: write only after it has populated the group.
cmake_language(DEFER CALL bts_write_cpu_manifest)

add_executable(bts-hybrid-normal-to-bump-tests ${BTS_HOST_SOURCE_DIR}/../tests/hybrid_normal_to_bump_test.cc)
target_include_directories(bts-hybrid-normal-to-bump-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-hybrid-normal-to-bump-tests PRIVATE cxx_std_20)
add_test(NAME hybrid-normal-to-bump COMMAND bts-hybrid-normal-to-bump-tests)

add_executable(bts-requested-features-tests ${BTS_HOST_SOURCE_DIR}/../tests/requested_features_test.cc)
target_include_directories(bts-requested-features-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-requested-features-tests PRIVATE cxx_std_20)
target_link_libraries(bts-requested-features-tests PRIVATE bts-export-config Qt6::Widgets)
add_test(NAME requested-features COMMAND bts-requested-features-tests)

add_executable(bts-asset-dragdrop-tests ${BTS_HOST_SOURCE_DIR}/../tests/asset_dragdrop_test.cc)
target_include_directories(bts-asset-dragdrop-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-asset-dragdrop-tests PRIVATE cxx_std_20)
target_link_libraries(bts-asset-dragdrop-tests PRIVATE Qt6::Widgets)
add_test(NAME asset-dragdrop-progress COMMAND bts-asset-dragdrop-tests)
set_tests_properties(asset-dragdrop-progress PROPERTIES ENVIRONMENT "BTS_NO_BACKGROUND_GPU=1;QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1")
target_sources(bts-host PRIVATE ${BTS_HOST_SOURCE_DIR}/../tests/asset_workspace_drop_test.cc)
set_source_files_properties(${BTS_HOST_SOURCE_DIR}/../tests/asset_workspace_drop_test.cc PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_test(NAME asset-dragdrop-workspace COMMAND bts-host --cpu-asset-dragdrop-test ${CMAKE_BINARY_DIR}/asset-dragdrop-proof)
set_tests_properties(asset-dragdrop-workspace PROPERTIES ENVIRONMENT "BTS_NO_BACKGROUND_GPU=1;QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/asset-dragdrop-proof/config;XDG_DATA_HOME=${CMAKE_BINARY_DIR}/asset-dragdrop-proof/data;XDG_CACHE_HOME=${CMAKE_BINARY_DIR}/asset-dragdrop-proof/cache")

add_executable(bts-project-save-safety-tests ${BTS_HOST_SOURCE_DIR}/../tests/project_save_safety_test.cc ${BTS_HOST_SOURCE_DIR}/painting/PaintPixels.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeMetadata.cc ${BTS_HOST_SOURCE_DIR}/io/SurfacePathIO.cc ${BTS_HOST_SOURCE_DIR}/painting/StrokeHistory.cc)
add_executable(bts-viewport-draw-guard-tests ${BTS_HOST_SOURCE_DIR}/../tests/viewport_draw_guard_test.cc)
foreach(target bts-project-save-safety-tests bts-viewport-draw-guard-tests)
  target_include_directories(${target} PRIVATE ${BTS_HOST_SOURCE_DIR})
  target_compile_features(${target} PRIVATE cxx_std_20)
  target_compile_options(${target} PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
  target_link_libraries(${target} PRIVATE Qt6::Widgets)
  add_dependencies(bts-cpu-tests ${target})
  add_test(NAME ${target} COMMAND ${target})
  set_tests_properties(${target} PROPERTIES ENVIRONMENT "BTS_NO_BACKGROUND_GPU=1;QT_QPA_PLATFORM=offscreen;QT_QPA_OFFSCREEN_NO_GLX=1")
endforeach()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Fontconfig REQUIRED)
  target_link_libraries(bts-host PRIVATE Fontconfig::Fontconfig)
endif()

set_target_properties(bts-host PROPERTIES OUTPUT_NAME mpainter)
if(APPLE)
  set_target_properties(bts-host PROPERTIES MACOSX_BUNDLE TRUE OUTPUT_NAME mPainter
    MACOSX_BUNDLE_INFO_PLIST "${BTS_HOST_SOURCE_DIR}/../../packaging/Info.plist")
  target_sources(bts-host PRIVATE "${BTS_HOST_SOURCE_DIR}/../../assets/branding/mpainter.icns")
  set_source_files_properties("${BTS_HOST_SOURCE_DIR}/../../assets/branding/mpainter.icns" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
  set_target_properties(bts-host PROPERTIES OUTPUT_NAME mPainter WIN32_EXECUTABLE TRUE)
  target_include_directories(bts-host PRIVATE "${BTS_HOST_SOURCE_DIR}/../../packaging")
  target_sources(bts-host PRIVATE "${BTS_HOST_SOURCE_DIR}/../../packaging/mpainter.rc")
endif()

install(FILES
  ${BTS_HOST_SOURCE_DIR}/../../assets/meshes/sphere.obj
  ${BTS_HOST_SOURCE_DIR}/../../assets/meshes/Shader_Ball_JL_01.obj
  ${BTS_HOST_SOURCE_DIR}/../../assets/meshes/Shader_Ball_JL_01-NOTICE.md
  ${BTS_HOST_SOURCE_DIR}/../../assets/meshes/CC-BY-4.0.txt
  DESTINATION assets/meshes)

add_executable(bts-theme-tests ${BTS_HOST_SOURCE_DIR}/../tests/theme_test.cc ${BTS_HOST_SOURCE_DIR}/ui/theme/Theme.cc)
target_include_directories(bts-theme-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-theme-tests PRIVATE cxx_std_20)
target_compile_options(bts-theme-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-theme-tests PRIVATE Qt6::Widgets)
add_dependencies(bts-release-check bts-theme-tests)
add_test(NAME bts-theme-dropdown-rendering COMMAND bts-theme-tests)

# Keep the product environment available in both direct Linux builds and installs.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(BTS_STUDIO_ASSETS
    ${BTS_HOST_SOURCE_DIR}/../../assets/environments/default-studio.hdr
    ${BTS_HOST_SOURCE_DIR}/../../assets/environments/default-studio.txt)
  add_custom_command(TARGET bts-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:bts-host>/5.2/datafiles/studiolights/world
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BTS_STUDIO_ASSETS} $<TARGET_FILE_DIR:bts-host>/5.2/datafiles/studiolights/world)
  install(FILES ${BTS_STUDIO_ASSETS} DESTINATION 5.2/datafiles/studiolights/world)
endif()

add_executable(bts-environment-rotation-gesture-tests ${BTS_HOST_SOURCE_DIR}/../tests/environment_rotation_gesture_test.cc)
target_include_directories(bts-environment-rotation-gesture-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-environment-rotation-gesture-tests PRIVATE cxx_std_20)
target_compile_options(bts-environment-rotation-gesture-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-environment-rotation-gesture-tests PRIVATE Qt6::Gui)
add_test(NAME bts-environment-rotation-gesture COMMAND bts-environment-rotation-gesture-tests)
add_dependencies(bts-release-check bts-environment-rotation-gesture-tests)

add_executable(bts-texture-set-residency-tests ${BTS_HOST_SOURCE_DIR}/../tests/texture_set_residency_test.cc ${BTS_HOST_SOURCE_DIR}/document/TextureDocument.cc)
target_include_directories(bts-texture-set-residency-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-texture-set-residency-tests PRIVATE cxx_std_20)
target_compile_options(bts-texture-set-residency-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
target_link_libraries(bts-texture-set-residency-tests PRIVATE bts-export-config)
add_test(NAME bts-texture-set-residency COMMAND bts-texture-set-residency-tests)
add_dependencies(bts-release-check bts-texture-set-residency-tests)

add_executable(bts-surface-curvature-tests ${BTS_HOST_SOURCE_DIR}/../tests/surface_curvature_test.cc)
target_include_directories(bts-surface-curvature-tests PRIVATE ${BTS_HOST_SOURCE_DIR})
target_compile_features(bts-surface-curvature-tests PRIVATE cxx_std_20)
target_compile_options(bts-surface-curvature-tests PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
add_test(NAME bts-surface-curvature COMMAND bts-surface-curvature-tests)
add_dependencies(bts-cpu-tests bts-surface-curvature-tests)

target_compile_definitions(bts-builtin-smartmasks PRIVATE BTS_POLYHAVEN_ASSET_DIR="${BTS_HOST_SOURCE_DIR}/../../assets/materials/polyhaven")
install(DIRECTORY ${BTS_HOST_SOURCE_DIR}/../../assets/materials/polyhaven DESTINATION assets/materials)
