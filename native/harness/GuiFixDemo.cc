// SPDX-License-Identifier: GPL-3.0-or-later
#include "PolyHavenDemo.hh"
#include "ShaderballDemo.hh"
#include "CheckboxDemo.hh"
#include "Res8kDemo.hh"
#include "PrecomposeWorkspaceDemo.hh"
#include "OverlayPerformanceDemo.hh"
#include "PainterWorkflowDemo.hh"
#include "SpsmDemo.hh"
#include "BakeSourceDemo.hh"
#include "PolygonBucketDemo.hh"
#include "MaskFillGpuDemo.hh"
#include "StrokeTargetPerformanceDemo.hh"
#include "TemporaryInkDemo.hh"
#include "LayerDropDemo.hh"
#include "RenameDemo.hh"
#include "TextureSetSelectionDemo.hh"
#include "BlendMaterialDemo.hh"
#include "BlendImagesDemo.hh"
#include "BlendAssetLibraryDemo.hh"
#include "NumericLocaleDemo.hh"
#include "HeightNeutralDemo.hh"
#include "ViewportDemoCapture.hh"
#include "AaPerformanceDemo.hh"
#include "UvTextureDemo.hh"
#include "EdgeLengthDemo.hh"
#include "OpenPbrDemo.hh"
#include "TemplateShaderDemo.hh"
#include "OpenPbrExportDemo.hh"
#include "OpenPbrImportDemo.hh"
#include "OpenPbrImagesDemo.hh"
#include "BlendLibraryDemo.hh"
#include "AdStrokeBench.hh"
#include "AePersistenceDemo.hh"
#include "AbDisplacementDemo.hh"
#include "TextureImportDemo.hh"
#include "FlattenDemo.hh"
#include "TabletPressureDemo.hh"
#include "MeshExportDemo.hh"
#include "DerivedExportDemo.hh"
#include "InputSpaceDemo.hh"
#include "SubsurfaceDemo.hh"
#include "RenderModeDemo.hh"
#include "DisplayPostDemo.hh"
#include "ClippingDemo.hh"
#include "SkewDemo.hh"
#include "AutoBakeDemo.hh"
#include "ResourceMonitorDemo.hh"
#include "InspectionDemo.hh"
#include "ShaderDemo.hh"
#include "PerformanceDemo.hh"
#include "SpatialModeDemo.hh"
#include "WarpDemo.hh"
#include "ImageSequenceDemo.hh"
#include "PlanarDemo.hh"
#include "FilledPathDemo.hh"
#include "RibbonDemo.hh"
#include "ScrollDemo.hh"
#include "AlembicDemo.hh"
#include "SignedDisplacementDemo.hh"
#include "FillTextureDemo.hh"
#include "FillChannelsDemo.hh"
#include "PropertyPerformanceDemo.hh"
#include "BakeFixDemo.hh"
#include "LayerEditDemo.hh"
#include "DisplacementDemo.hh"
#include "GuiFixDemo.hh"
#include "LayerChannelDemo.hh"
#include "LevelsEditorDemo.hh"
#include "BakedMapsDemo.hh"
#include "BakingLayoutDemo.hh"
#include "TabletPerformanceDemo.hh"
#include "StrokeSplineDemo.hh"
#include "ColorPickerDemo.hh"
#include "LightingDemo.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include "FolderMaskDemo.hh"
#include "SmartMaskWorkflowDemo.hh"
#include "SpmskShelfWorkflowDemo.hh"
#include "NativeMaterialProjectionDemo.hh"
#include "NativeVectorMappingDemo.hh"
#include "NativeRealMaterialDemo.hh"
#include "NativeWorldNormalDemo.hh"
#include "NativeHeightImageUiDemo.hh"
#include "GpuAdjustmentPaintDemo.hh"
namespace bts {
namespace {
class Demo final : public QObject {
 public:
  QWidget *workspace, *viewport;
  QString directory, mesh;
  int resolution;
  std::function<QJsonObject()> state;
  QJsonArray captures;
  QTimer timer, monitor;
  int step = 0, moves = 0;
  QElapsedTimer ping, monitor_clock;
  double post_release_ms = 0, max_ping_ms = 0, stroke_ping_ms = 0, roughness_ping_ms = 0,
         roughness_release_ms = 0;
  QPointF start, last;
  bool performance = qEnvironmentVariableIsSet("BTS_GUI_PERFORMANCE"), roughness = false;
  int roughness_moves = 0, roughness_post = 0;
  QSlider *slider = nullptr;
  qint64 padding_before = 0, padding_before_release = 0, release_undo_count = 0;
  double release_value = 0;
  Demo(QWidget *w, QWidget *v, QString d, QString m, int r, std::function<QJsonObject()> s)
      : QObject(w),
        workspace(w),
        viewport(v),
        directory(d),
        mesh(m),
        resolution(r),
        state(std::move(s))
  {
    QDir().mkpath(directory);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.setInterval(1000);
    timer.start();
    monitor.setTimerType(Qt::PreciseTimer);
    monitor.setInterval(10);
    connect(&monitor, &QTimer::timeout, this, [this] {
      max_ping_ms = std::max(max_ping_ms, monitor_clock.nsecsElapsed() / 1e6);
      monitor_clock.restart();
    });
  }
  void start_monitor()
  {
    max_ping_ms = 0;
    monitor_clock.start();
    monitor.start();
  }
  double stop_monitor()
  {
    monitor.stop();
    max_ping_ms = std::max(max_ping_ms, monitor_clock.nsecsElapsed() / 1e6);
    return max_ping_ms;
  }
  void capture(const char *name)
  {
    mouse(QEvent::MouseMove, {-1000, -1000}, Qt::NoButton, Qt::NoButton);
    viewport->repaint();
    if (auto *gl = qobject_cast<QOpenGLWidget *>(viewport))
      capture_viewport_framebuffer(gl).save(directory + "/" + name + "-framebuffer.png");
    auto data = state();
    auto *sets = workspace->findChild<QListWidget *>("textureSets");
    QJsonArray names;
    if (sets)
      for (int i = 0; i < sets->count(); ++i)
        names.append(sets->item(i)->text());
    data["texture_sets"] = names;
    auto *empty = viewport->findChild<QLabel *>("emptyProjectMessage");
    data["empty_view_visible"] = empty && empty->isVisible();
    data["empty_view_text"] = empty ? empty->text() : QString{};
    auto *empty_layers = workspace->findChild<QLabel *>("emptyLayersMessage");
    data["empty_layers_visible"] = empty_layers && empty_layers->isVisible();
    data["stage"] = name;
    for (const auto *button : {"addPaintLayer", "addFillLayer"}) {
      auto *widget = workspace->findChild<QToolButton *>(button);
      data[QString(button) + "_enabled"] = widget && widget->isEnabled();
      data[QString(button) + "_tooltip"] = widget ? widget->toolTip() : QString{};
    }
    captures.append(data);
    capture_demo_window(workspace, viewport).save(directory + "/" + name + "-window.png");
  }
  void mouse(QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons)
  {
    QMouseEvent event(
        type, p, viewport->mapToGlobal(p.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
  }
  void advance()
  {
    if (roughness) {
      roughness_step();
      return;
    }
    switch (step++) {
      case 0: {
        capture("startup");
        QAction *action = nullptr;
        for (auto *candidate : workspace->findChildren<QAction *>())
          if (candidate->text() == "&New…")
            action = candidate;
        if (!action)
          qFatal("GUI New action missing");
        action->trigger();
        break;
      }
      case 1: {
        QDialog *dialog = nullptr;
        for (auto *candidate : workspace->findChildren<QDialog *>())
          if (candidate->findChild<QLineEdit *>("meshPath"))
            dialog = candidate;
        if (!dialog)
          qFatal("GUI New dialog missing");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        if (!state()["has_document"].toBool())
          qFatal("GUI New did not create a document");
        capture("created");
        auto *add = workspace->findChild<QToolButton *>("addPaintLayer");
        if (!add || !add->isEnabled())
          qFatal("GUI paint layer disabled after New");
        add->click();
        if (auto *height = workspace->findChild<QDoubleSpinBox *>("materialBump"))
          height->setValue(.08);
        capture("before-stroke");
        padding_before = state()["padding_calls"].toInteger();
        start = {viewport->width() * .43, viewport->height() * .48};
        last = start;
        if (performance)
          start_monitor();
        mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        timer.setInterval(performance ? 33 : 30);
        break;
      }
      default: {
        const int count = performance ? 60 : 8;
        if (moves < count) {
          ++moves;
          last = start + (performance ?
                              QPointF(15 * std::sin(moves * .3), 20 * (1 - std::cos(moves * .3))) :
                              QPointF(moves * 3, 0));
          mouse(QEvent::MouseMove, last, Qt::NoButton, Qt::LeftButton);
          return;
        }
        if (moves == count) {
          ++moves;
          padding_before_release = state()["padding_calls"].toInteger();
          mouse(QEvent::MouseButtonRelease, last, Qt::LeftButton, Qt::NoButton);
          ping.start();
          timer.setInterval(1);
          return;
        }
        if (moves == count + 1) {
          ++moves;
          post_release_ms = ping.nsecsElapsed() / 1e6;
          timer.setInterval(performance ? 2000 : 1000);
          return;
        }
        if (performance && state()["composite_pending"].toBool()) {
          timer.setInterval(100);
          return;
        }
        if (performance) {
          stroke_ping_ms = stop_monitor();
          capture("painted");
          if (auto *check = workspace->findChild<QAction *>("checkGuiComposite"))
            check->trigger();
          capture("paint-reference-checked");
          begin_roughness();
        }
        else
          finish();
        break;
      }
    }
  }
  void begin_roughness()
  {
    timer.stop();
    workspace->findChild<QToolButton *>("addFillLayer")->click();
    auto *value = workspace->findChild<QDoubleSpinBox *>("roughnessControl");
    value->setValue(.2);
    capture("roughness-before");
    slider = workspace->findChild<QSlider *>("roughnessSlider");
    if (!slider || !slider->isEnabled())
      qFatal("Uniform roughness slider missing");
    roughness = true;
    roughness_moves = 0;
    slider->setSliderDown(true);
    start_monitor();
    timer.setInterval(20);
    timer.start();
  }
  void roughness_step()
  {
    if (roughness_moves < 100) {
      slider->setValue(200 + 6 * ++roughness_moves);
      return;
    }
    if (roughness_post == 0) {
      roughness_post = 1;
      ping.start();
      slider->setSliderDown(false);
      timer.setInterval(1);
      return;
    }
    if (roughness_post == 1) {
      roughness_post = 2;
      const auto released = state();
      release_value = released["roughness"].toDouble();
      release_undo_count = released["undo_count"].toInteger();
      roughness_release_ms = ping.nsecsElapsed() / 1e6;
      timer.setInterval(100);
      return;
    }
    if (state()["composite_pending"].toBool())
      return;
    timer.stop();
    roughness_ping_ms = stop_monitor();
    capture("roughness-after");
    if (auto *check = workspace->findChild<QAction *>("checkGuiComposite"))
      check->trigger();
    capture("reference-checked");
    finish();
  }
  void finish()
  {
    timer.stop();
    if (!performance)
      capture("painted");
    auto *gl = qobject_cast<QOpenGLWidget *>(viewport);
    if (!gl)
      qFatal("GUI viewport is not a Qt OpenGL surface");
    const auto image = capture_viewport_framebuffer(gl);
    image.save(directory + (performance ? "/final-framebuffer.png" : "/painted-framebuffer.png"));
    qint64 magenta = 0;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x) {
        const auto pixel = image.pixelColor(x, y);
        magenta += pixel.red() > 240 && pixel.green() < 15 && pixel.blue() > 240;
      }
    QJsonObject result{
        {"pid", QCoreApplication::applicationPid()},
        {"resolution", resolution},
        {"captures", captures},
        {"magenta_pixels", magenta},
        {"post_release_ping_ms", post_release_ms},
        {"performance", performance},
        {"mouse_moves", performance ? 60 : 8},
        {"roughness_value_changes", roughness_moves},
        {"stroke_max_ping_ms", stroke_ping_ms},
        {"roughness_max_ping_ms", roughness_ping_ms},
        {"roughness_release_ms", roughness_release_ms},
        {"roughness_release_value", release_value},
        {"roughness_release_undo_count", release_undo_count},
        {"padding_before_stroke", padding_before},
        {"padding_before_release", padding_before_release},
        {"input",
         "File New QAction, dialog widgets, Add paint layer, Qt mouse events; uniform roughness slider press/valueChanged/release"}};
    QFile file(directory + "/gui-fixes.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("GUI proof output missing");
    file.write(QJsonDocument(result).toJson());
    file.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
};
}  // namespace
void start_gui_fix_demo(QWidget *workspace,
                        QWidget *viewport,
                        const QString &directory,
                        const QString &mesh,
                        int resolution,
                        std::function<QJsonObject()> state)
{
  if (qEnvironmentVariableIsSet("BTS_RES8K_DEMO")) {
    new Res8kDemo(workspace, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_POLYHAVEN_DEMO")) {
    new PolyHavenDemo(workspace, directory, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_SHADERBALL_DEMO")) {
    new ShaderballDemo(workspace, directory, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_CHECKBOX_DEMO")) {
    new CheckboxDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_PRECOMPOSE_WORKSPACE_DEMO")) {
    new PrecomposeWorkspaceDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI")) {
    start_native_world_normal_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_AW_HEIGHT_IMAGE_UI")) {
    new NativeHeightImageUiDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_AW_REAL_UI")) {
    new NativeRealMaterialDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_AW_VECTOR_UI")) {
    new NativeVectorMappingDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO")) {
    new GpuAdjustmentPaintDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BJ_PROJECTION_UI")) {
    new NativeMaterialProjectionDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BN_SPMSK_UI")) {
    new SpmskShelfWorkflowDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BN_SMART_MASK")) {
    new SmartMaskWorkflowDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BK_FOLDER_MASK")) {
    new FolderMaskDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_PAINTER_WORKFLOW_DEMO")) {
    new PainterWorkflowDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_SPSM_DEMO")) {
    new SpsmDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_TEXTURE_SET_SELECTION_DEMO")) {
    new TextureSetSelectionDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_OVERLAY_PROFILE")) {
    new OverlayPerformanceDemo(workspace, viewport, directory, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_BAKE_SOURCE_DEMO")) {
    new BakeSourceDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_HEIGHT_NEUTRAL_DEMO")) {
    new HeightNeutralDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_NUMERIC_LOCALE_DEMO")) {
    new NumericLocaleDemo(workspace, directory, mesh, std::move(state));
    return;
  }
  if (qEnvironmentVariableIsSet("BTS_POLYGON_BUCKET_DEMO"))
    new PolygonBucketDemo(workspace, viewport, directory, mesh, resolution, state);
  else if (qEnvironmentVariableIsSet("BTS_MASK_FILL_GPU_DEMO"))
    start_mask_fill_gpu_demo(
        workspace, viewport, directory, mesh, qEnvironmentVariable("BTS_MASK_FILL_IMAGE"), state);
  else if (qEnvironmentVariableIsSet("BTS_STROKE_TARGET_PROFILE"))
    start_stroke_target_performance_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_TEMPORARY_INK_DEMO"))
    new TemporaryInkDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_LAYER_DROP_DEMO"))
    new LayerDropDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_RENAME_DEMO"))
    new RenameDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BAKING_LAYOUT_DEMO"))
    new BakingLayoutDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BAKED_MAPS_DEMO"))
    new BakedMapsDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_LEVELS_EDITOR_DEMO"))
    new LevelsEditorDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_LAYER_CHANNEL_DEMO"))
    new LayerChannelDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BLEND_ASSET_BROWSER_DEMO")) {
    new BlendAssetLibraryDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  else if (qEnvironmentVariableIsSet("BTS_BLEND_IMAGES_DEMO")) {
    new BlendImagesDemo(workspace, viewport, directory, mesh, std::move(state));
    return;
  }
  else if (qEnvironmentVariableIsSet("BTS_BLEND_MATERIAL_DEMO"))
    new BlendMaterialDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BLEND_IMPORT_DEMO"))
    new BlendLibraryDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_OPENPBR_IMAGES_DEMO"))
    new OpenPbrImagesDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_OPENPBR_IMPORT_DEMO"))
    new OpenPbrImportDemo(workspace, viewport, directory, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_OPENPBR_EXPORT_DEMO"))
    new OpenPbrExportDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_TEMPLATE_SHADER_DEMO"))
    new TemplateShaderDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_OPENPBR_DEMO"))
    new OpenPbrDemo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_EDGE_LENGTH_DEMO"))
    new EdgeLengthDemo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_UV_TEXTURE_DEMO"))
    new UvTextureDemo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_AD_STROKE_BENCH"))
    new AdStrokeBench(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_AE_PERSISTENCE_DEMO"))
    start_ae_persistence_demo(workspace, directory, mesh);
  else if (qEnvironmentVariableIsSet("BTS_AA_PERFORMANCE"))
    new AaPerformanceDemo(workspace, viewport, directory, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_AB_DISPLACEMENT"))
    new AbDisplacementDemo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO"))
    start_texture_import_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_TABLET_REGRESSION"))
    start_tablet_pressure_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_MESH_EXPORT_DEMO"))
    start_mesh_export_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_FLATTEN_DEMO"))
    start_flatten_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_DERIVED_EXPORT_DEMO"))
    start_derived_export_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_COLOR_SPACE_DEMO"))
    start_input_space_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SUBSURFACE_DEMO"))
    start_subsurface_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_RENDER_WORKSPACE_DEMO") ||
           qEnvironmentVariableIsSet("BTS_CYCLES_PREVIEW_PROBE") ||
           qEnvironmentVariableIsSet("BTS_RENDER_MODE_DEMO"))
    start_render_mode_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_DISPLAY_POST_DEMO"))
    start_display_post_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SHADER_DEMO"))
    start_shader_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_INSPECTION_DEMO"))
    start_inspection_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_RESOURCE_MONITOR_DEMO"))
    start_resource_monitor_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_AUTO_BAKE_DEMO"))
    start_auto_bake_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SKEW_DEMO"))
    start_skew_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_CLIPPING_DEMO"))
    start_clipping_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_IMAGE_SEQUENCE_DEMO"))
    start_image_sequence_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_WARP_DEMO"))
    start_warp_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_STROKE_SPLINE_DEMO"))
    start_stroke_spline_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SPATIAL_MODE_DEMO"))
    start_spatial_mode_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_PLANAR_DEMO"))
    start_planar_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_FILLED_PATH_DEMO"))
    start_filled_path_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_RIBBON_DEMO"))
    start_ribbon_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SCROLL_DEMO"))
    start_scroll_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_ALEMBIC_DEMO"))
    start_alembic_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_SIGNED_DISPLACEMENT_DEMO"))
    start_signed_displacement_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_PROPERTY_PERFORMANCE"))
    start_property_performance_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_FILL_CHANNELS_DEMO"))
    start_fill_channels_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_FILL_TEXTURE_DEMO"))
    start_fill_texture_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BAKE_FIX_DEMO"))
    start_bake_fix_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_LAYER_EDIT_DEMO"))
    start_layer_edit_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_DISPLACEMENT_DEMO"))
    start_displacement_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_LIGHTING_DEMO"))
    start_lighting_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_COLOR_PICKER_DEMO"))
    start_color_picker_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_TABLET_PERFORMANCE"))
    start_tablet_performance_demo(
        workspace, viewport, directory, mesh, resolution, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_PERFORMANCE_PROFILE"))
    start_performance_demo(workspace, viewport, directory, mesh, resolution, std::move(state));
  else
    new Demo(workspace, viewport, directory, mesh, resolution, std::move(state));
}
}  // namespace bts
