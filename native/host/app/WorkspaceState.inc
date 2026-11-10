// SPDX-License-Identifier: GPL-3.0-or-later
// Session state: the document, brushes, history, timers and the controllers the session owns. Part
// of WorkspaceSession::run() (app/WorkspaceSession.inc), included in order inside its body; it
// shares that function's locals and is not a standalone file.
if (int(harness.is_set("demo-dir")) + int(harness.is_set("layout-demo-dir")) +
        int(harness.is_set("navigation-demo-dir")) + int(harness.is_set("new-demo-dir")) >
    1)
{
  throw std::invalid_argument("Choose one demo sequence per run");
}
branding.stage("Building Painter workspace…");
const bool use_workspace = !harness.is_set("demo-dir") ||
                           harness.is_set("material-layering-demo") || parser.isSet("workspace") ||
                           harness.is_set("open-document") || harness.is_set("layout-demo-dir") ||
                           harness.is_set("navigation-demo-dir") || harness.is_set("new-mesh") ||
                           harness.is_set("new-demo-dir");
if (use_workspace && !parser.isSet("project"))
  host.initialize_empty_workspace_working_space();
bts::SceneSnapshot snapshot = use_workspace ? host.scene_snapshot() : bts::SceneSnapshot{};
int selected_material = -1;
std::unique_ptr<bts::TextureDocument> document;
std::function<void(const QString &, const QJsonObject &)> python_event;
auto last_document_edit = bts::performance::Clock::now();
bool opening_project = false;
bool dirty = false;
std::uint64_t document_revision = 0, last_recovery_revision = ~std::uint64_t(0);
QUuid last_recovery_document;
QString document_path;
bts::ProjectFileIdentity document_disk_identity;
bts::BrushSettings brush, paint_brush, eraser_brush, projection_brush,
    smudge_brush = bts::default_smudge_brush(), clone_brush;
clone_brush.clone = true;
projection_brush.projected.emplace();
projection_brush.values = projection_brush.projected->values;
std::optional<bts::LayerMask> mask_clipboard;
std::optional<bts::GeometryMask> geometry_clipboard;
bool hide_excluded_geometry = false;
std::optional<bts::LayerInstance> instance_clipboard;
QUuid instance_clipboard_document;
std::string instance_clipboard_name;
int inspected_map = -1;
bool id_picker_active = false;
int id_picker_previous_map = -1;
std::uint32_t id_picker_material = 0;
QUuid id_picker_effect;
bts::InspectionMode id_picker_previous_mode = bts::InspectionMode::Material;
std::function<void(bool)> change_id_picker;
bts::SymmetrySettings symmetry, fill_display_symmetry;
bts::AssetCatalog assets(
    parser.isSet("library-dir") ?
        QFileInfo(parser.value("library-dir")).absoluteFilePath() :
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("shelf"));
if (!parser.value("environment").isEmpty()) {
  const auto source = bts::read_image_resource(
      QFileInfo(parser.value("environment")).absoluteFilePath(), false);
  bts::StudioAsset environment;
  environment.name = "Studio";
  environment.filename = source.filename;
  environment.path = source.path;
  environment.scope = bts::AssetScope::Builtin;
  environment.usages = {"environment"};
  environment.bytes = source.bytes;
  assets.add(std::move(environment));
}
QString library_error;
try {
  assets.load_user_library();
}
catch (const std::exception &error) {
  library_error = QString::fromUtf8(error.what());
}
bool quick_enabled = false, quick_editing = false;
std::map<std::uint32_t, bts::QuickMaskField> quick_masks;
bts::BrushSettings quick_brush, quick_return_brush;
quick_brush.mask_value = 0;
std::optional<bts::QuickMaskField> quick_stroke_before;
std::uint32_t quick_stroke_uid = 0;
bts::TilePaintPixels quick_stroke_pixels;
std::unique_ptr<bts::TextureDocument> path_drag_before;
std::uint32_t path_drag_uid = 0;
std::string path_drag_id, path_drag_error;
QString path_drag_label;
enum class UniformKind { Roughness, ColorFill, ColorBrush, Property };
UniformKind uniform_kind = UniformKind::Roughness;
bool uniform_previewed = false, color_picker_pending = false, secondary_picker_pending = false;
std::optional<bts::BrushSettings> uniform_brush_before;
std::unique_ptr<bts::TextureDocument> uniform_before;
std::uint32_t uniform_uid = 0;
std::function<void(bool)> finish_uniform_edit;
struct PendingMaterialPick {
  bts::MaterialSampleRequest request;
  std::uint64_t ticket, revision;
  QUuid document, set, layer, effect;
  bool selected_mask, color, secondary;
  bts::MaterialValues brush_values;
};
std::optional<PendingMaterialPick> pending_material_pick;
QTimer material_pick_timer;
material_pick_timer.setInterval(16);
std::optional<std::pair<QUuid, std::uint32_t>> property_ui_refresh;
bool property_ui_scheduled = false, defer_material_ui = false, known_fill_insertion = false;
QTimer property_frame_timer, property_idle_timer;
property_frame_timer.setSingleShot(true);
property_frame_timer.setInterval(16);
property_idle_timer.setSingleShot(true);
property_idle_timer.setInterval(150);
std::function<void(bts::TextureSet &)> pending_property_edit;
std::optional<bts::CompositeChannels> pending_property_channels, queued_property_channels;
std::function<void()> flush_property_edit;
std::function<void(const QString &)> begin_property_edit;
std::function<void(const bts::TextureSet &, bts::CompositeChannels, bool)> queue_property_pixels;
std::function<void(std::function<void(bts::TextureSet &)>, const QString &)> edit_property;
QJsonObject property_before_state;
QString property_label;
bts::CompositeChannels property_channels{};
bool property_dragging = false;
// Eight user scalar planes plus the independent Displacement plane share stroke mechanics.
const auto scalar_layer = [](auto &layer, int slot) -> auto & {
  return slot == 8 ? layer.displacement : layer.users[slot];
};
const auto scalar_pixels = [](auto &effect, int slot) -> auto & {
  return slot == 8 ? effect.displacement : effect.users[slot];
};
const auto scalar_history = [](auto &effect, int slot) -> auto & {
  return slot == 8 ? effect.displacement_strokes : effect.user_strokes[slot];
};
const auto scalar_enabled = [&](const auto &set, const auto &layer, bool effect, int slot) {
  return slot == 8 ? set.displacement.channel && brush.displacement && layer.displacement.enabled :
                     set.users[slot].present && brush.users[slot] && layer.users[slot].enabled;
};
const auto scalar_value = [&](int slot) {
  return slot == 8 ? brush.displacement_value : brush.user_values[slot];
};
QPointer<bts::SkewController> skew;
bool skew_profile_active = false;
bts::BrushSettings skew_return_brush, skew_brush, skew_eraser;
skew_brush.mask_value = 1;
skew_eraser.mask_value = 1;
skew_eraser.erase = true;
std::unique_ptr<bts::TextureDocument> stroke_before;
std::uint32_t stroke_uid = 0;
bool stroke_changed = false;
std::array<bts::TilePaintPixels, 9> stroke_users;
bts::TilePaintPixels stroke_pixels, stroke_ao_pixels;
float stroke_opacity = 1;
bool stroke_mask = false, stroke_erase = false, stroke_smudge = false, stroke_clone = false;
std::shared_ptr<const bts::AdjustmentPaintSnapshot> stroke_adjustment;
bts::AdjustmentPaintTransactions adjustment_transactions;
std::unique_ptr<bts::CloneStroke> clone_paint, clone_ao;
std::array<std::unique_ptr<bts::CloneStroke>, 9> clone_users;
std::unique_ptr<bts::SmudgeStroke> smudge_paint, smudge_ao;
std::array<std::unique_ptr<bts::SmudgeStroke>, 9> smudge_users;
// COW checkpoints contain only the committed spline prefix. The immediate
// predicted tail is replaced on the next frame, including smudge/clone state.
struct StrokePrefix {
  bts::TilePaintPixels pixels, ao;
  std::array<bts::TilePaintPixels, 9> users;
  std::optional<bts::SmudgeStroke> smudge, smudge_ao;
  std::array<std::optional<bts::SmudgeStroke>, 9> smudge_users;
  std::optional<bts::CloneStroke> clone, clone_ao;
  std::array<std::optional<bts::CloneStroke>, 9> clone_users;
} stroke_prefix;
bts::PaintPixels::Bounds stroke_tail_bounds{};
bts::TilePaintPixels quick_prefix;
bts::PaintPixels::Bounds quick_tail_bounds{};
const auto save_stroke_prefix = [&](const auto &pixels, const auto &ao, const auto &users) {
  stroke_prefix.pixels = pixels;
  stroke_prefix.ao = ao;
  stroke_prefix.users = users;
  stroke_prefix.smudge = smudge_paint ? std::optional{*smudge_paint} : std::nullopt;
  stroke_prefix.smudge_ao = smudge_ao ? std::optional{*smudge_ao} : std::nullopt;
  stroke_prefix.clone = clone_paint ? std::optional{*clone_paint} : std::nullopt;
  stroke_prefix.clone_ao = clone_ao ? std::optional{*clone_ao} : std::nullopt;
  for (int slot = 0; slot < 9; ++slot) {
    stroke_prefix.smudge_users[slot] = smudge_users[slot] ? std::optional{*smudge_users[slot]} :
                                                            std::nullopt;
    stroke_prefix.clone_users[slot] = clone_users[slot] ? std::optional{*clone_users[slot]} :
                                                          std::nullopt;
  }
};
const auto restore_stroke_prefix = [&] {
  stroke_pixels = stroke_prefix.pixels;
  stroke_ao_pixels = stroke_prefix.ao;
  stroke_users = stroke_prefix.users;
  smudge_paint = stroke_prefix.smudge ?
                     std::make_unique<bts::SmudgeStroke>(*stroke_prefix.smudge) :
                     nullptr;
  smudge_ao = stroke_prefix.smudge_ao ?
                  std::make_unique<bts::SmudgeStroke>(*stroke_prefix.smudge_ao) :
                  nullptr;
  clone_paint = stroke_prefix.clone ? std::make_unique<bts::CloneStroke>(*stroke_prefix.clone) :
                                      nullptr;
  clone_ao = stroke_prefix.clone_ao ? std::make_unique<bts::CloneStroke>(*stroke_prefix.clone_ao) :
                                      nullptr;
  for (int slot = 0; slot < 9; ++slot) {
    smudge_users[slot] = stroke_prefix.smudge_users[slot] ?
                             std::make_unique<bts::SmudgeStroke>(
                                 *stroke_prefix.smudge_users[slot]) :
                             nullptr;
    clone_users[slot] = stroke_prefix.clone_users[slot] ?
                            std::make_unique<bts::CloneStroke>(*stroke_prefix.clone_users[slot]) :
                            nullptr;
  }
};
struct HistoryEntry {
  std::unique_ptr<bts::TextureDocument> state;
  std::uint32_t material_uid;
  QString label = "Edit layer";
  std::optional<std::array<float, 4>> brush;
  QJsonObject environment;
  std::optional<std::pair<QUuid, QString>> asset_name;
};
QElapsedTimer environment_edit_clock;
std::vector<HistoryEntry> undo_history, redo_history;
QAction *undo_action = nullptr, *redo_action = nullptr;
const auto refresh_history_actions = [&] {
  if (undo_action) {
    undo_action->setEnabled(bool(uniform_before) || bool(stroke_before) ||
                            bool(path_drag_before) || !undo_history.empty());
    undo_action->setText(undo_history.empty() ? "Undo" : "Undo " + undo_history.back().label);
  }
  if (redo_action) {
    redo_action->setEnabled(!stroke_before && !path_drag_before && !redo_history.empty());
    redo_action->setText(redo_history.empty() ? "Redo" : "Redo " + redo_history.back().label);
  }
};
std::function<void(std::function<void(bts::TextureSet &)>)> edit_material;
bts::WorkspaceShell *workspace = nullptr;
Viewport *viewport = nullptr;
QSlider *roughness = nullptr;
std::function<void(int)> select_material;
bool layer_selection_refresh = false;
std::function<void()> refresh_paths, restore_path_profile, path_brush_changed, leave_path;
bts::BrushSettings path_return_brush;
bts::SymmetrySettings path_return_symmetry;
bool path_guard = false;
bts::RibbonSettings ribbon_profile;
bool filled_profile = false;

std::function<void(const QString &)> change_environment_resource;
// The window owns the viewport and is destroyed before its callback state and native host.
// Its temporary ownership also covers failure while constructing WorkspaceShell.
std::unique_ptr<QMainWindow> window = std::make_unique<QMainWindow>();
QTimer resource_ui_timer;
resource_ui_timer.setSingleShot(true);
resource_ui_timer.setInterval(16);
std::unique_ptr<bts::ResourceMonitor> resource_monitor;
std::unique_ptr<bts::TextureLoadController> texture_loader;
std::unique_ptr<bts::NativeMaterialSourceLoader> native_material_loader;
std::map<std::pair<std::uint32_t, int>, std::uint64_t> residency_requests;
std::unique_ptr<bts::SmartMaskThumbnailJobs> smart_mask_thumbnails;
std::unique_ptr<bts::BrushPresetThumbnailJobs> brush_thumbnails;
bts::BrushPresetUi *brush_presets = nullptr;
std::unique_ptr<bts::NativeMaterialImageUi> native_image_ui;
std::unique_ptr<bts::MaterialLayeringUi> material_layering_ui;
std::unique_ptr<bts::MaterialLayerParameterImportJob> parameter_import;
std::function<bool(std::uint64_t,
                   std::uint32_t,
                   std::unique_ptr<bts::TextureDocument> &,
                   const std::vector<bts::StudioAsset> &,
                   const QUuid &)>
    publish_parameters;
std::function<void()> pd149_actor_retire;
std::function<void()> program_warmup_retire;
auto pending_new = std::make_shared<bts::PendingProjectCreate<bts::NewProjectOptions>>();
QPointer<QProgressDialog> pending_new_progress;
QPointer<QTimer> pending_new_poll;
const auto clear_pending_new_ui = [&](std::optional<std::uint64_t> generation = {}) {
  if (generation && (!pending_new_progress ||
                     pending_new_progress->property("newGeneration").toULongLong() != *generation))
    return;
  if (pending_new_poll)
    pending_new_poll->stop();
  pending_new_poll = nullptr;
  if (pending_new_progress) {
    pending_new_progress->blockSignals(true);
    pending_new_progress->close();
    pending_new_progress->deleteLater();
  }
  pending_new_progress = nullptr;
};
const auto cancel_pending_new = [&] {
  pending_new->cancel();
  clear_pending_new_ui();
};
const auto owner_programs_pending = [&] {
  return !host.renderer().empty() &&
         qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1 &&
         host.texture_updates_pending() && blender::GPU_shader_compiler_has_pending_work();
};
bts::MaterialLayeringBrushRoute material_layering_brush;
bool material_layering_edit = false;
std::uint64_t native_apply_serial = 0;
#ifdef BTS_WITH_MATERIALX
std::unique_ptr<bts::OpenPbrMaterialImporter> material_importer;
QUuid material_import_document;
std::uint64_t material_import_revision = 0;
std::function<void()> schedule_material_import;
#endif
struct ThumbnailShutdown {
  std::unique_ptr<bts::SmartMaskThumbnailJobs> &smart;
  std::unique_ptr<bts::BrushPresetThumbnailJobs> &brush;
  ~ThumbnailShutdown()
  {
    bts::stop_asset_thumbnail_jobs();
    brush.reset();
    smart.reset();
  }
} thumbnail_shutdown{smart_mask_thumbnails, brush_thumbnails};
std::unique_ptr<bts::ShaderController> shader;
std::unique_ptr<bts::RenderController> render_controller;
std::unique_ptr<bts::SkewController> skew_owner;  // Destroy before window/viewport/Blender.
change_environment_resource = [&](const QString &path) {
  host.set_environment_resource(path.toStdString());
  workspace->set_environment(host.environment_state());
  dirty = bool(document);
  viewport->update();
  if (dirty)
    window->setWindowTitle((document_path.isEmpty() ?
                                QString("Untitled") :
                                QFileInfo(document_path).completeBaseName()) +
                           " * — mPainter");
};
