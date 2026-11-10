// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolyHavenPreparation.hh"
#include "TextureResidencyMemory.hh"
#include "GPU_host_texture_memory.hh"
#include "TextureSetResidency.hh"
#include "Theme.hh"
#include "RenderStatusTest.hh"
#include "AssetApplicationTarget.hh"
#include "AssetApplyProgress.hh"
#include "AssetViewportDrop.hh"
#include "UpdateNotice.hh"
#include "MaterialIdDialog.hh"
#include "BrushPresetDemo.hh"
#include "ReferenceImage.hh"
#include "BrushPresetUi.hh"
#include "BrushPresetThumbnailJobs.hh"
#include "SpatialOracleScope.hh"
#include "SpsmComparisonState.hh"
#include "MaskEffectPath.hh"
#include "MaskTextureTarget.hh"
// Qt headers precede native headers which expose Blender utility macros.
#include <QtCore>
#include "ProjectOpenJob.hh"
#include "NativeMaterialHeightUi.hh"
#include "NativeMaterialPanel.hh"
#include "NativeMaterialBundle.hh"  // Explicit AW source-byte diagnostic.
#include "ProjectIO.hh"
#include "ProjectSaveConflict.hh"
#include "ViewportDrawGuard.hh"
#include "PortableFonts.hh"
#include <QProgressDialog>
#include "TextureExtent.hh"
#include "ResourceAvailability.hh"
#include "SmartPresetName.hh"
#include "SpsmAssetDispatch.hh"
#include "SpsmImportUi.hh"
#include "SpsmSource.hh"
#include "GpuPerformance.hh"
#include "BakeSourceCpuDemo.hh"
#include "NativeMaterialInspectionDemo.hh"
#include "NativeMaterialSourceLoader.hh"
#include "SmartMaskThumbnailJobs.hh"
#include "BuiltinSmartMasks.hh"
#include "SmartMaskProjectWitness.hh"
#include "SpmskShelfProjectWitness.hh"
#include "NativeMaterialProjectionWitness.hh"
#include "GPU_pass.hh"
#include "NativeMaterialImageUi.hh"
#include "NativeMaterialImagePanel.hh"
#include "DocumentEditCommit.hh"
#include "NativeMaterialApply.hh"
#include "Branding.hh"
#include "TemporaryInk.hh"
#include "LayerDropCommand.hh"
#include "RenameCommand.hh"
#include "LayerChannelCommand.hh"
#include "EvaluationScope.hh"
#include "BlendUniformChannels.hh"
#include "BlendMaterialDialog.hh"
#include "BlendMaterialBindings.hh"
#include "BlendAssetBrowser.hh"
#include "NumericLocale.hh"
#ifdef BTS_WITH_MATERIALX
#  include "OpenPbrMaterialImport.hh"
#endif
#include <QStatusBar>
#include "GuiFileSelection.hh"
#include "SmartMaterialPreset.hh"
#include "BuiltinStandardMaterials.hh"
#include <future>
#include <chrono>
#include "ViewportCapture.hh"
#include "PaintRecording.hh"
#include "BrushFootprintBounds.hh"
#include "GpuLayerPreview.hh"
#include "GPU_shader.hh"
#include "../tests/GpuProgramWarmupDiagnostic.hh"
#include <QScopedValueRollback>
#include <QScopeGuard>
#include "FlattenExport.hh"
#include "FlattenLayerDialog.hh"
#include "MeshExportDialog.hh"
#include "RenderController.hh"
#include "FilledPathRaster.hh"
#include "SkewController.hh"
#include "ShaderController.hh"
#include "ResourceMonitor.hh"
#include <QActionGroup>
#include "ProfileApplication.hh"
#include "UserSessionGuard.hh"
#include "SkewJSON.hh"
#include <QPointer>
#include "RibbonPanel.hh"
#include "RibbonRaster.hh"
#include "FillTextureIO.hh"
#include "FillGizmo.hh"
#include "ImageSequence.hh"
#include "WarpEditor.hh"
#include "WarpProjection.hh"
#include "LayerEditDiff.hh"
#include "OriginalMeshObj.hh"
#include "ExportCpuDemo.hh"
#include "EditTiming.hh"
#include "TexturePadding.hh"
#include <QOpenGLFunctions>
#include "EmptyProjectView.hh"
#include <clocale>
#include <cstring>
#include "GuiFixDemo.hh"
#include "HarnessOptions.hh"
#include "HarnessDispatch.hh"
#include "HarnessContext.hh"
#include "CompositeStatus.hh"
#include "Preferences.hh"
#include "TabletDiagnostics.hh"
#include <QPointer>
#include <QSettings>
#include "SkewBakeCpuDemo.hh"
#include "ColorPicker.hh"
#include "GeometryTileClip.hh"
#include <QInputDialog>
#include "StrokeArchive.hh"
#include "ProjectConfiguration.hh"
#include "ProjectTemplate.hh"
#include "EnvironmentResource.hh"
#include "TextureSetReassignment.hh"
#include <QEventLoop>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include "Recovery.hh"
#include <QStandardPaths>
#include <QDirIterator>
#include <QTableWidget>
#include <QPlainTextEdit>
#include "TextureExportDialog.hh"
#include "GpuTextureExportSource.hh"
#include <QSplitter>
#include "LayerStackList.hh"
#include "BitmapMask.hh"
#include "ResourceImport.hh"
#include "MeshBakeDialog.hh"
#include "MeshMapHistory.hh"
#include "BakedMapShelf.hh"
#include "MeshMapImport.hh"
#include "NormalMix.hh"
#include "AmbientOcclusion.hh"
#include "SmartMaterial.hh"
#include <QStandardPaths>
#include <QDir>
#include <QFormLayout>
#include <QRadioButton>
#include <QProgressBar>
#include <QSpinBox>
#include "SymmetryIO.hh"
#include "SymmetryIntersection.hh"
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QCryptographicHash>
#include <QContextMenuEvent>
#include <QCheckBox>
#include <QScrollArea>
#include "BlenderHost.hh"
#include "PropertyLatency.hh"
#include "OrbitCamera.hh"
#include "NewProjectDialog.hh"
#include "PendingProjectCreate.hh"
#include "TextureDocument.hh"
#include "StrokeSpacing.hh"
#include "StrokeSpline.hh"
#include "StraightStroke.hh"
#include "LazyMouse.hh"
#include "MaterialPicker.hh"
#include "PolygonFill.hh"
#include <QPainterPath>
#include "WorkspaceShell.hh"
#include "MaterialLayeringUi.hh"
#include "MaterialLayerParameterImportJob.hh"
#include "MaterialLayerParameterCatalog.hh"
#include "../tests/MaterialLayeringUiArtist.hh"
#include "MaterialLayeringAuthoring.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include <QBoxLayout>
#include "ViewportHeadlessDemo.hh"
#include "TextureLoadController.hh"
#include "LongOperationCancel.hh"
#include "OrdinaryColorSource.hh"
#include "TextureImportCpuDemo.hh"
#include "TextureIoAwait.hh"
#include "AssetThumbnail.hh"
#include "BrushJitterStamp.hh"
#include "StrokeDirection.hh"
#include "SmudgeStroke.hh"
#include "CloneStroke.hh"
#include "AdjustmentPaintPlan.hh"
#include "AdjustmentPaintTransactions.hh"
#include "QuickMaskPreview.hh"
#include "SurfacePathRaster.hh"
#include "SurfacePathIO.hh"
#include "PathsPanel.hh"
#include "PathCurvePanel.hh"
#include "BrushPreview.hh"
#include "EnvironmentRotationGesture.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <optional>

#include <QApplication>
#include <QAction>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include "FileDialogs.hh"
#include "PythonConsole.hh"
#include "LayerApi.hh"
#include "ChannelConfiguration.hh"
#include "AutomationJobs.hh"
#include <QFileInfo>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QListWidget>
#include <QTreeWidget>
#include <QMainWindow>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTabletEvent>
#include <QPointingDevice>
#include <QTimer>
#include <QTime>
#include <QToolBar>
#include <QToolButton>
#include <QSignalBlocker>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>

namespace {

#include "MainViewport.inc"

void configure_theme(QApplication &app)
{
  bts::theme::apply(app);
}

#include "MainHarnessInput.inc"

#include "WorkspaceSession.inc"

}  // namespace

int main(int argc, char **argv)
{
  // Version queries must work without a display or any Qt/GPU initialization.
  if (argc == 2 && (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0)) {
    const auto version = bts::product_version().toUtf8();
    std::printf("mPainter %s\n", version.constData());
    return 0;
  }
  if (qEnvironmentVariableIsSet("BTS_MATERIAL_FILTER_UI_TEST") && !bts::owned_private_xvfb()) {
    std::fprintf(stderr, "Material Filter UI test requires an owned private Xvfb display\n");
    return 77;
  }
  if (!bts::prepare_test_session(argc, argv))
    return 77;
#ifdef _WIN32
  // The Microsoft CRT rejects zero-sized buffered streams and treats _IOLBF
  // as full buffering. Keep diagnostic output immediate on Windows.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
#else
  std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
  std::setvbuf(stderr, nullptr, _IOLBF, BUFSIZ);
#endif
  const bool cpu_only_test = bts::cpu_test_arguments(argc, argv);
  if (!cpu_only_test)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents);
  QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents);
  QSurfaceFormat format;
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setSamples(0);
  // Request nonblocking swaps on Qt's input thread; the viewport timer and
  // desktop compositor already pace presentation.
  format.setSwapInterval(0);
  format.setAlphaBufferSize(8);
  format.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(format);
  if (!cpu_only_test)
    bts::tablet::configure_xwayland_coordinates();
  bts::initialize_numeric_locale();
  bts::configure_portable_fonts(argv[0]);
  bts::ProfileApplication app(argc, argv);
  // Qt adopts the user's locale. Blender shader code generation requires decimal dots.
  std::setlocale(LC_NUMERIC, "C");
  const bool numeric_locale_invalid = std::strcmp(std::localeconv()->decimal_point, ".") != 0;
  std::printf("BTS_NUMERIC_LOCALE value=%s decimal=%s\n",
              std::setlocale(LC_NUMERIC, nullptr),
              std::localeconv()->decimal_point);
  // Use an independent application identity for preferences and runtime data.
  app.setApplicationName("mpainter");
  app.setApplicationDisplayName("mPainter");
  app.setOrganizationName("Mattias Lind");
  app.setOrganizationDomain("se.mattiaslind");
  app.setDesktopFileName("se.mattiaslind.mpainter");
  app.setApplicationVersion(bts::product_version());
  app.setWindowIcon(bts::product_icon());
  bts::tablet::DeviceLog tablet_device_log(&app);
  configure_theme(app);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"no-splash", "Start without the mPainter splash"});
  parser.addOption({"project", "Open a native .blend project", "file"});
  parser.addOption({"workspace", "Show the Painter workspace around the live viewport"});
  parser.addOption(
      {"environment",
       "New-project Studio Light HDR/EXR path",
       "file",
       qEnvironmentVariable("BTS_ENVIRONMENT",
                            QCoreApplication::applicationDirPath() +
                                "/5.2/datafiles/studiolights/world/default-studio.hdr")});
  parser.addOption(
      {"autosave-seconds", "Idle recovery interval (1–3600 seconds)", "seconds", "120"});
  parser.addOption({"recovery-dir", "Recovery snapshot directory", "directory"});
  parser.addOption({"library-dir", "Use a specific user shelf directory", "directory"});
  // Demo, acceptance and test switches live with the harness.
  bts::harness::add_harness_options(parser);
  parser.process(app);
  if (const auto code = bts::harness::run_pre_host_test(parser))
    return *code;
  // Harness options are read through here; product options stay on the parser.
  const bts::harness::HarnessContext harness(parser);

  bts::BrandingStartup branding(
      !parser.isSet("no-splash") && !harness.is_set("cpu-export-test") &&
      !harness.is_set("cpu-texture-import-test") && !harness.is_set("cpu-skew-test") &&
      !harness.is_set("cpu-bake-source-test") && !harness.is_set("cpu-native-material-test") &&
      !harness.is_set("headless-viewport-test") && !harness.is_set("headless-cycles-cuda-test"));
  branding.stage("Loading Blender scene…");
  try {
    bts::BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),
                          parser.value("project").toStdString());
    bts::configure_spsm_source_hydration();
    if (const auto code = bts::harness::run_host_test(host, parser))
      return *code;
    WorkspaceSession session(app, parser, harness, branding, host, numeric_locale_invalid);
    return session.run();
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "BTS_HOST_FAILED %s\n", error.what());
    return 1;
  }
}
