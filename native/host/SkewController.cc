#include "Theme.hh"
#include "ResourceAvailability.hh"
#include "SkewController.hh"
#include "SurfaceBakeSettings.hh"
#include "TextureMemory.hh"
#include "SkewPanel.hh"
#include "SkewJSON.hh"
#include "BakeQuality.hh"
#include "EvaluationScope.hh"
#include "Performance.hh"
#include <QMainWindow>
#include <QDialog>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QApplication>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QThread>
#include <QDoubleSpinBox>
#include <QDateTime>
#include <QVariant>
namespace bts {
namespace {
void merge(PaintPixels::Bounds &a, PaintPixels::Bounds b)
{
  if (b.empty())
    return;
  if (a.empty())
    a = b;
  else
    a = {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}
void append(SceneSnapshot &to, SceneSnapshot source)
{
  const auto offset = to.materials.size();
  to.materials.insert(to.materials.end(), source.materials.begin(), source.materials.end());
  for (auto &t : source.uv_triangles) {
    t.material += offset;
    to.uv_triangles.push_back(std::move(t));
  }
  to.object_names.insert(source.object_names.begin(), source.object_names.end());
  to.object_vertex_counts.insert(source.object_vertex_counts.begin(),
                                 source.object_vertex_counts.end());
  to.mesh_objects += source.mesh_objects;
  for (int c = 0; c < 3; ++c) {
    to.minimum[c] = to.has_bounds ? std::min(to.minimum[c], source.minimum[c]) : source.minimum[c];
    to.maximum[c] = to.has_bounds ? std::max(to.maximum[c], source.maximum[c]) : source.maximum[c];
  }
  to.has_bounds = true;
}
SkewBakeOptions skew_options(const QJsonObject &r)
{
  SkewBakeOptions o;
  o.resolution = r["resolution"].toInt(512);
  o.height = r["height"].toInt(o.resolution);
  o.sampling = r["sampling"].toInt(1);
  o.dilation = r["dilation"].toInt(32);
  o.diffusion = r["diffusion"].toBool();
  auto &ray = o.ray;
  ray.frontal = r["frontal"].toDouble(.01);
  ray.rear = r["rear"].toDouble(.01);
  ray.relative = r["relative"].toBool(true);
  ray.average_normals = r["average"].toBool(true);
  ray.use_cage = r["use_cage"].toBool();
  ray.ignore_backface = r["ignore_backface"].toBool(false);
  ray.match_names = r["match_names"].toBool();
  ray.low_marker = r["low_marker"].toString("_low").toStdString();
  ray.high_marker = r["high_marker"].toString("_high").toStdString();
  ray.ignore_marker = r["ignore_marker"].toString("_ignorebf").toStdString();
  ray.per_fragment = r["per_fragment"].toBool();
  ray.directx = r["directx"].toBool();
  return o;
}
}  // namespace
SkewController::SkewController(QMainWindow *owner, SkewHooks hooks)
    : QObject(owner), hooks_(std::move(hooks))
{
  dock_ = new QDialog(owner);
  dock_->setObjectName("bakingDock");
  dock_->setWindowTitle("Baking");
  dock_->setModal(false);
  for (const auto *name : {"undoDocument", "redoDocument"})
    if (auto *action = owner->findChild<QAction *>(name))
      dock_->addAction(action);
  dock_->setMinimumSize(820, 520);
  dock_->setStyleSheet(
      bts::theme::qss(owner->styleSheet() + "\nQDialog{background:@Surface@;color:@Text@;}"));
  auto *layout = new QVBoxLayout(dock_);
  layout->setContentsMargins(0, 0, 0, 0);
  panel_ = new SkewPanel(dock_);
  layout->addWidget(panel_);
  connect(dock_, &QDialog::rejected, this, [this] {
    if (mode() > 0)
      mode(0);
  });
  dock_->hide();
  if (auto *old = owner->findChild<QAction *>("bakeMeshMapsAction"))
    old->setShortcut({});
  auto *action = new QAction("Baking mode", owner);
  action->setObjectName("enterBakingMode");
  action->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
  owner->addAction(action);
  dock_->addAction(action);
  connect(action, &QAction::triggered, this, [this] { mode(1); });
  if (auto *menu = owner->findChild<QMenu *>("viewportMenu"))
    menu->addAction(action);
  else
    owner->menuBar()->addAction(action);
  panel_->paint_mode = [this] { mode(0); };
  panel_->paint_skew = [this] {
    if (eligible())
      mode(mode() == 2 ? 1 : 2);
  };
  panel_->configure = [this] {
    finish_edit();
    if (hooks_.configure)
      hooks_.configure();
    sync();
  };
  panel_->bake_selected = [this] {
    finish_edit();
    auto *doc = hooks_.document();
    if (!doc)
      return;
    const auto recipe = doc->bake_configuration();
    const auto selected = recipe["enabled"].toArray();
    for (int role = 0; role < 7; ++role)
      if (!recipe.contains("enabled") || selected.at(role).toBool())
        request_map(role, true, true);
  };
  panel_->rebake = [this] {
    finish_edit();
    request_map(0, true, true);
  };
  panel_->bake_map = [this](int role) {
    finish_edit();
    request_map(role, true, true);
  };
  panel_->preview_map = [this](int role) {
    if (hooks_.preview)
      hooks_.preview(role);
    sync();
  };
  panel_->sync_map = [this](int role) { synchronize_quality(role); };
  panel_->auto_changed = [this](bool on) { choose_auto(on ? 0 : -1); };
  panel_->auto_role_changed = [this](int role) { choose_auto(role); };
  panel_->value_changed = [this](float v) { value_ = v; };
  panel_->settings_changed = [this](const auto &s) { settings(s); };
  panel_->quality_changed = [this](int role, const auto &q) { quality(role, q); };
  panel_->recipe_changed = [this](const auto &recipe) {
    performance::Scope timing("bake_recipe_edit");
    hooks_.end_input();
    auto *doc = hooks_.document();
    if (!doc)
      return;
    if (!settings_before_)
      settings_before_ = std::make_unique<TextureDocument>(*doc);
    doc->set_bake_configuration(recipe);
    settings_bake_ = true;
    sync();
    idle_.start();
    if (mode() > 0 && auto_role() >= 0)
      request(true);
  };
  idle_.setSingleShot(true);
  idle_.setInterval(150);
  connect(&idle_, &QTimer::timeout, this, [this] { commit_settings(); });
  debounce_.setSingleShot(true);
  debounce_.setInterval(100);
  connect(&debounce_, &QTimer::timeout, this, [this] { start_job(); });
  poll_.setInterval(30);
  connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
}
SkewController::~SkewController()
{
  if (cancel_)
    cancel_->store(true);
  if (job_.valid())
    while (job_.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
      process_gpu();
}
void SkewController::request_gpu(const std::function<void()> &work,
                                 const std::shared_ptr<std::atomic<bool>> &cancel)
{
  std::packaged_task<void()> task([&] {
    if (cancel->load())
      throw std::runtime_error("Bake cancelled");
    hooks_.with_gpu(work);
  });
  auto result = task.get_future();
  {
    std::lock_guard lock(gpu_mutex_);
    gpu_task_ = std::move(task);
  }
  result.get();
}
void SkewController::process_gpu()
{
  std::packaged_task<void()> task;
  {
    std::lock_guard lock(gpu_mutex_);
    task = std::move(gpu_task_);
  }
  if (task.valid())
    task();
}
void SkewController::reset_document()
{
  dock_->hide();
  idle_.stop();
  debounce_.stop();
  before_.reset();
  settings_before_.reset();
  requests_.clear();
  regions_.clear();
  if (cancel_)
    cancel_->store(true);
  engine_.reset();
  general_.reset();
  geometry_.reset();
  low_.reset();
  high_.reset();
  cage_.reset();
  source_key_.clear();
  seen_token_.clear();
  failed_token_.clear();
  vector_key_.clear();
  vectors_.clear();
  error_.clear();
  setProperty("sourceLoads", 0);
  prior_mode_ = 0;
}
int SkewController::mode() const
{
  const auto *doc = hooks_.document();
  return doc ? doc->bake_configuration()["ui_mode"].toInt() : 0;
}
int SkewController::auto_role() const
{
  const auto *doc = hooks_.document();
  return doc ? doc->bake_configuration()["auto_role"].toInt(-1) : -1;
}
bool SkewController::eligible() const
{
  const auto *doc = hooks_.document();
  if (!doc)
    return false;
  const auto r = doc->bake_configuration();
  if (r["project_mesh"].toBool(true) || r["use_cage"].toBool() || !r["average"].toBool(true) ||
      r["high_files"].toArray().isEmpty())
    return false;
  for (const auto &p : r["high_files"].toArray())
    if (!QFileInfo(p.toString()).isFile())
      return false;
  return r["frontal"].toDouble(.01) + r["rear"].toDouble(.01) > 0;
}
bool SkewController::bake_eligible() const
{
  const auto *doc = hooks_.document();
  if (!doc)
    return false;
  const auto r = doc->bake_configuration();
  if (!r["project_mesh"].toBool(true)) {
    if (r["high_files"].toArray().isEmpty())
      return false;
    for (const auto &p : r["high_files"].toArray())
      if (!QFileInfo(p.toString()).isFile())
        return false;
  }
  if (r["use_cage"].toBool())
    return QFileInfo(r["cage_file"].toString()).isFile();
  return r["project_mesh"].toBool(true) ||
         r["frontal"].toDouble(.01) + r["rear"].toDouble(.01) > 0;
}
bool SkewController::painting() const
{
  return mode() == 2 && eligible() && hooks_.material() &&
         hooks_.document()->set(hooks_.material()).skew.settings.enabled;
}
void SkewController::mode(int value)
{
  auto *doc = hooks_.document();
  if (!doc)
    return;
  if (value == mode()) {
    if (value == 1) {
      dock_->show();
      dock_->raise();
    }
    return;
  }
  finish_edit();
  auto before = std::make_unique<TextureDocument>(*doc);
  auto r = doc->bake_configuration();
  r["ui_mode"] = value;
  if (value == 2)
    r["auto_role"] = 0;
  doc->set_bake_configuration(r);
  hooks_.history(std::move(before), "Switch baking mode", hooks_.material());
  sync();
  if (value == 2)
    request(true);
}
void SkewController::choose_auto(int role)
{
  if (role < -1 || role > 6)
    return;
  finish_edit();
  auto *doc = hooks_.document();
  if (!doc || role == auto_role())
    return;
  auto before = std::make_unique<TextureDocument>(*doc);
  auto r = doc->bake_configuration();
  r["auto_role"] = role;
  doc->set_bake_configuration(r);
  for (auto it = requests_.begin(); it != requests_.end();)
    if (!it->second.manual && it->first.second != role)
      it = requests_.erase(it);
    else
      ++it;
  hooks_.history(std::move(before),
                 role < 0 ? QString("Automatic mesh-map bake") :
                            QString("Automatic ") + mesh_map_names[role] + " bake",
                 hooks_.material());
  if (role >= 0)
    request(true);
  sync();
}
QJsonObject SkewController::options(std::uint32_t uid, int role) const
{
  auto *doc = hooks_.document();
  auto r = doc->bake_configuration();
  const auto q = bake_quality(r, doc->set(uid).id, role);
  r.remove("ui_mode");
  r.remove("auto_role");
  r.remove("map_options");
  for (auto it = q.begin(); it != q.end(); ++it)
    r[it.key()] = it.value();
  r["directx"] = doc->settings().normal_map_format == NormalMapFormat::DirectX;
  r["per_fragment"] = doc->settings().tangent_per_fragment;
  return r;
}
std::set<int> SkewController::tiles(std::uint32_t uid) const
{
  auto *doc = hooks_.document();
  const auto &set = doc->set(uid);
  const auto selected = doc->bake_configuration()["tiles"].toObject();
  if (!selected.contains(set.id.toString()))
    return set.tiles;
  std::set<int> result;
  for (const auto &tile : selected[set.id.toString()].toArray())
    if (set.tiles.contains(tile.toInt()))
      result.insert(tile.toInt());
  return result;
}
QString SkewController::token(std::uint32_t uid, int role) const
{
  if (!uid)
    uid = hooks_.material();
  if (role < 0)
    role = std::max(0, auto_role());
  auto *doc = hooks_.document();
  if (!doc || !uid)
    return {};
  EditStateScope identity;
  const auto &set = doc->set(uid);
  auto o = skew_field_json(set.skew);
  o["set"] = set.id.toString();
  o["role"] = role;
  o["options"] = options(uid, role);
  if (role == 3 || role == 4 || role == 6) {
    QJsonArray channels;
    for (const auto &source : doc->texture_sets())
      if (!source.disabled &&
          (source.displacement.channel || o["options"].toObject()["normal_detail"].toDouble() > 0))
      {
        auto metadata =
            QJsonDocument::fromJson(TextureDocument::mask_dependency_signature(source)).object();
        metadata.remove("mesh_maps");
        metadata.remove("skew_correction");
        metadata.remove("selected_layer");
        metadata.remove("selected_effect");
        metadata.remove("selected_mask");
        channels.append(metadata);
      }
    o["surface_channels"] = channels;
  }
  if (role == 2) {
    QJsonArray color;
    for (float c : set.base_values.base_color)
      color.append(c);
    o["base"] = color;
  }
  for (auto key : {"vectors", "vector_length", "vector_density", "vector_opacity"})
    o.remove(key);
  return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString SkewController::source_key() const
{
  auto *doc = hooks_.document();
  if (!doc)
    return {};
  const auto r = doc->bake_configuration();
  QJsonObject key{{"source_scale", r.value("source_scale").toDouble(1)},
                  {"source_frame_enabled", r.value("source_frame_enabled").toBool()},
                  {"source_frame", r.value("source_frame").toDouble(1)},
                  {"document", doc->id().toString()},
                  {"mesh", QString::number(quintptr(hooks_.scene().uv_triangles.data()), 16)},
                  {"project_mesh", r["project_mesh"].toBool(true)},
                  {"use_cage", r["use_cage"].toBool()}};
  QJsonArray displacement;
  for (const auto &set : doc->texture_sets())
    displacement.append(QJsonObject{{"id", set.id.toString()},
                                    {"disabled", set.disabled},
                                    {"settings", displacement_json(set.displacement)}});
  key["displacement"] = displacement;
  const auto file = [](const QString &path) {
    QFileInfo f(path);
    return QJsonObject{{"path", f.absoluteFilePath()},
                       {"size", double(f.size())},
                       {"mtime", QString::number(f.lastModified().toMSecsSinceEpoch())}};
  };
  if (!r["project_mesh"].toBool(true)) {
    QJsonArray files;
    for (const auto &path : r["high_files"].toArray())
      files.append(file(path.toString()));
    key["high"] = files;
  }
  if (r["use_cage"].toBool())
    key["cage"] = file(r["cage_file"].toString());
  return QString::fromUtf8(QJsonDocument(key).toJson(QJsonDocument::Compact));
}
void SkewController::sync_panel()
{
  auto *doc = hooks_.document();
  const auto uid = hooks_.material();
  if (!doc || !uid)
    return;
  const auto &set = doc->set(uid);
  int count = 0;
  for (const auto &s : doc->texture_sets())
    count += !s.disabled;
  panel_->synchronize(doc->bake_configuration(),
                      set.skew.settings,
                      mode() == 2,
                      auto_role() == 0,
                      set.id,
                      auto_role(),
                      hooks_.preview_role ? hooks_.preview_role() : -1,
                      count,
                      QString::fromStdString(set.name));
}
void SkewController::sync()
{
  performance::Scope timing("bake_ui_sync");
  auto *doc = hooks_.document();
  const auto uid = hooks_.material();
  const int current = mode();
  if (current > 0 && !dock_->property("shortcutsInstalled").toBool()) {
    bool ready = true;
    auto *owner = qobject_cast<QMainWindow *>(parent());
    for (const auto *name : {"undoDocument", "redoDocument"}) {
      auto *action = owner ? owner->findChild<QAction *>(name) : nullptr;
      if (action)
        dock_->addAction(action);
      else
        ready = false;
    }
    dock_->setProperty("shortcutsInstalled", ready);
  }
  if (current > 0 && doc && prior_mode_ == 0) {
    const auto *owner = qobject_cast<QMainWindow *>(parent());
    if (owner) {
      const int width = std::clamp(owner->width() - 48, 820, 1200),
                height = std::clamp(owner->height() - 80, 520, 900);
      dock_->resize(width, height);
      dock_->move(owner->frameGeometry().center() - QPoint(width / 2, height / 2));
    }
  }
  dock_->setVisible(current == 1 && doc);
  if (current == 2 && prior_mode_ != 2 && hooks_.log)
    hooks_.log("Paint skew in the viewport. Ctrl+Shift+B returns to baking settings.");
  if (!doc || !uid) {
    hooks_.view(false, {}, {}, {});
    prior_mode_ = current;
    return;
  }
  auto &set = doc->set(uid);
  sync_panel();
  const QString key = doc->id().toString() + QString::number(uid) +
                      QString::number(quintptr(hooks_.scene().uv_triangles.data()), 16) +
                      QString::number(set.skew.settings.vector_density);
  if (current == 2 && key != vector_key_) {
    TexturePadding geometry(hooks_.scene(), uid);
    vectors_ = geometry.skew_vectors(set.skew.settings.vector_density, set.tiles);
    vector_key_ = key;
  }
  hooks_.view(painting(),
              current == 2 ? std::optional<SkewField>(set.skew) : std::nullopt,
              vectors_,
              geometry_);
  const auto now = token();
  const auto wanted = source_key() + now;
  if (current > 0 && auto_role() >= 0 && !before_ && !settings_before_ &&
      failed_token_ != wanted &&
      (seen_token_ != now || prior_mode_ == 0 || source_key_ != source_key()))
    request(true);
  seen_token_ = now;
  prior_mode_ = current;
}
void SkewController::settings(const SkewSettings &s)
{
  auto *doc = hooks_.document();
  if (!doc || !hooks_.material())
    return;
  hooks_.end_input();
  if (!settings_before_)
    settings_before_ = std::make_unique<TextureDocument>(*doc);
  auto &old = doc->set(hooks_.material()).skew.settings;
  const bool affects_bake = old.enabled != s.enabled || old.edge_protection != s.edge_protection ||
                            old.edge_distance != s.edge_distance ||
                            old.edge_contrast != s.edge_contrast;
  settings_bake_ |= affects_bake;
  old = s;
  sync();
  idle_.start();
  if (affects_bake && mode() > 0 && auto_role() >= 0)
    request(true);
}
void SkewController::quality(int role, const QJsonObject &q)
{
  performance::Scope timing("bake_quality_edit");
  hooks_.end_input();
  auto *doc = hooks_.document();
  if (!doc || !hooks_.material())
    return;
  if (!settings_before_)
    settings_before_ = std::make_unique<TextureDocument>(*doc);
  doc->set_bake_configuration(
      with_bake_quality(doc->bake_configuration(), doc->set(hooks_.material()).id, role, q));
  settings_bake_ = true;
  sync();
  idle_.start();
  if (mode() > 0 && auto_role() == role)
    request(true);
}
void SkewController::synchronize_quality(int role)
{
  finish_edit();
  auto *doc = hooks_.document();
  if (!doc || !hooks_.material())
    return;
  auto before = std::make_unique<TextureDocument>(*doc);
  std::vector<QUuid> ids;
  for (const auto &set : doc->texture_sets())
    if (!set.disabled)
      ids.push_back(set.id);
  doc->set_bake_configuration(synchronized_bake_quality(
      doc->bake_configuration(), doc->set(hooks_.material()).id, role, ids));
  hooks_.history(std::move(before), "Sync bake parameters", hooks_.material());
  if (auto_role() == role)
    for (const auto &set : doc->texture_sets())
      if (!set.disabled)
        request_map(role, true, false, set.material_session_uid);
  sync();
}
void SkewController::commit_settings()
{
  if (!settings_before_)
    return;
  idle_.stop();
  auto before = std::move(settings_before_);
  const bool recipe = before->bake_configuration() != hooks_.document()->bake_configuration();
  hooks_.history(
      std::move(before), recipe ? "Edit bake parameters" : "Skew settings", hooks_.material());
  settings_bake_ = false;
}
bool SkewController::begin(const BrushSettings &brush)
{
  if (!painting() || before_)
    return false;
  commit_settings();
  before_ = std::make_unique<TextureDocument>(*hooks_.document());
  stroke_uid_ = hooks_.material();
  ink_ = {};
  prefix_ = {};
  dirty_ = {};
  tail_ = {};
  opacity_ = brush.stroke_opacity / 100;
  erase_ = brush.erase;
  return true;
}
void SkewController::stamps(const std::vector<PaintPixels::Stamp> &samples, std::size_t stable)
{
  if (!before_)
    return;
  auto &set = hooks_.document()->set(stroke_uid_);
  ink_ = prefix_;
  auto touched = tail_;
  PaintPixels::Bounds tail{};
  stable = std::min(stable, samples.size());
  for (std::size_t i = 0; i < samples.size(); ++i) {
    auto stamp = samples[i];
    stamp.channels = {false, false, true, false, false};
    stamp.values.roughness = value_;
    stamp.coverage_mask.reset();
    const auto b = ink_.stamp_working(set.skew.resolution, stamp, set.tiles);
    merge(touched, b);
    if (i >= stable)
      merge(tail, b);
    if (i + 1 == stable)
      prefix_ = ink_;
  }
  tail_ = tail;
  set.skew.pixels.over_stroke(
      before_->set(stroke_uid_).skew.pixels, ink_, opacity_, touched, erase_);
  merge(dirty_, touched);
  sync();
}
void SkewController::bounds(PaintPixels::Bounds touched)
{
  auto *doc = hooks_.document();
  if (!doc || touched.empty())
    return;
  const auto &set = doc->set(hooks_.material());
  const auto r = doc->bake_configuration();
  const int w = r["resolution"].toInt(512), h = r["height"].toInt(w);
  for (int tile : set.tiles) {
    const double sx = double(w) / set.skew.resolution, sy = double(h) / set.skew.resolution;
    merge(regions_[hooks_.material()][tile],
          {int(std::floor((touched.x0 - 1) * sx)),
           int(std::floor((touched.y0 - 1) * sy)),
           int(std::ceil((touched.x1 + 1) * sx)),
           int(std::ceil((touched.y1 + 1) * sy))});
  }
}
void SkewController::end(bool cancel)
{
  if (!before_)
    return;
  auto before = std::move(before_);
  auto *doc = hooks_.document();
  if (cancel)
    doc->set(stroke_uid_).skew = before->set(stroke_uid_).skew;
  else if (!dirty_.empty()) {
    bounds(dirty_);
    hooks_.history(std::move(before),
                   erase_ ? "Erase skew correction" : "Paint skew correction",
                   stroke_uid_);
    if (auto_role() >= 0)
      request(false);
  }
  sync();
}
void SkewController::polygon(const PaintPixels::Stamp &source)
{
  if (!painting())
    return;
  hooks_.end_input();
  commit_settings();
  auto *doc = hooks_.document();
  auto before = std::make_unique<TextureDocument>(*doc);
  auto &set = doc->set(hooks_.material());
  auto stamp = source;
  stamp.channels = {false, false, true, false, false};
  const auto area = set.skew.pixels.stamp(set.skew.resolution, stamp, set.tiles);
  if (area.empty())
    return;
  bounds(area);
  hooks_.history(std::move(before), "Fill skew correction", hooks_.material());
  if (auto_role() >= 0)
    request(false);
  sync();
}
bool SkewController::cancel_transient()
{
  if (!settings_before_)
    return false;
  idle_.stop();
  auto *doc = hooks_.document();
  if (doc && hooks_.material()) {
    doc->set(hooks_.material()).skew.settings =
        settings_before_->set(hooks_.material()).skew.settings;
    doc->set_bake_configuration(settings_before_->bake_configuration());
  }
  settings_before_.reset();
  settings_bake_ = false;
  sync();
  return true;
}
void SkewController::invert()
{
  value_ = 1 - value_;
  if (auto *w = panel_->findChild<QDoubleSpinBox *>("skewPaintValue"))
    w->setValue(value_);
}
void SkewController::request(bool full)
{
  request_map(std::max(0, auto_role()), full, false);
}
void SkewController::request_map(int role, bool full, bool manual, std::uint32_t uid)
{
  if (!uid)
    uid = hooks_.material();
  if (role < 0 || role > 6 || !uid || !bake_eligible())
    return;
  if (!manual && failed_token_ == source_key() + token(uid, role))
    return;
  auto &request = requests_[{uid, role}];
  request.full |= full;
  request.manual |= manual;
  error_.clear();
  failed_token_.clear();
  if (!debounce_.isActive())
    debounce_.start();
  panel_->progress(true, QString(mesh_map_names[role]) + " queued");
}
bool SkewController::pending() const
{
  return bool(settings_before_) || !requests_.empty() || job_.valid() || debounce_.isActive();
}
void SkewController::start_job()
{
  performance::Scope timing("bake_submit");
  if (job_.valid() || requests_.empty())
    return;
  if (!bake_eligible()) {
    requests_.clear();
    error_ = "Choose a project/high-poly mesh and valid cage before baking.";
    panel_->progress(false, error_);
    return;
  }
  auto *doc = hooks_.document();
  auto chosen = requests_.lower_bound({hooks_.material(), -1});
  if (chosen == requests_.end() || chosen->first.first != hooks_.material())
    chosen = requests_.begin();
  const auto [uid, role] = chosen->first;
  const auto pending = chosen->second;
  const auto recipe = options(uid, role);
  const auto source = source_key();
  try {
    if (source_key_ != source) {
      engine_.reset();
      general_.reset();
      geometry_.reset();
      low_ = std::make_shared<const SceneSnapshot>(
          hooks_.bake_scene && recipe["project_mesh"].toBool(true) ? hooks_.bake_scene() :
                                                                     hooks_.scene());
      if (recipe["project_mesh"].toBool(true))
        high_ = low_;
      else {
        auto high = std::make_shared<SceneSnapshot>();
        for (const auto &path : recipe["high_files"].toArray()) {
          append(*high,
                 hooks_.load_source(path.toString().toStdString(), bake_source_options(recipe)));
          setProperty("sourceLoads", property("sourceLoads").toInt() + 1);
        }
        high_ = high;
      }
      cage_.reset();
      if (recipe["use_cage"].toBool()) {
        cage_ = std::make_shared<const SceneSnapshot>(hooks_.load_source(
            recipe["cage_file"].toString().toStdString(), bake_source_options(recipe)));
        setProperty("sourceLoads", property("sourceLoads").toInt() + 1);
      }
      source_key_ = source;
    }
    const auto selected = tiles(uid);
    if (selected.empty())
      throw std::invalid_argument("Select at least one UV tile for this Texture Set.");
    const auto opts = skew_options(recipe);
    const bool skew_normal = role == 0 && !recipe["project_mesh"].toBool(true) &&
                             !opts.ray.use_cage && opts.ray.average_normals;
    AutoBakeOptions automatic;
    automatic.ray = opts.ray;
    auto &maps = automatic.maps;
    maps.resolution = opts.resolution;
    maps.height = opts.height;
    maps.sampling = opts.sampling;
    maps.dilation = opts.dilation;
    maps.diffusion = opts.diffusion;
    maps.rays = recipe["rays"].toInt(256);
    maps.use_project_mesh = recipe["project_mesh"].toBool(true);
    maps.enabled.fill(false);
    maps.enabled[role] = true;
    read_surface_bake_settings(maps, recipe);
    capture_bake_channels(maps, *doc, *low_);
    const auto &set = doc->set(uid);
    const auto field = set.skew;
    const auto name = set.name;
    const auto base = set.base_values;
    const auto area = pending.full ? std::map<int, PaintPixels::Bounds>{} : regions_[uid];
    const auto wanted = token(uid, role);
    const auto id = doc->id();
    auto engine = engine_;
    auto general = general_;
    auto low = low_, high = high_, cage = cage_;
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    auto cancel = cancel_;
    requests_.erase(chosen);
    if (hooks_.with_gpu)
      automatic.maps.with_gpu = [this, cancel](const std::function<void()> &work) {
        request_gpu(work, cancel);
      };
    job_ = std::async(std::launch::async, [=]() mutable {
      MeshMap map;
      if (skew_normal) {
        if (!engine)
          engine = std::make_shared<SkewBakeEngine>(low, high);
        map = engine->bake(uid, field, selected, opts, area, *cancel);
      }
      else {
        if (!general)
          general = std::make_shared<AutoBakeEngine>(low, high, cage);
        auto result = general->bake(uid, name, base, selected, field, automatic, *cancel);
        if (result[role])
          map = std::move(*result[role]);
      }
      return Result{
          engine, general, std::move(map), source, wanted, id, uid, role, pending.manual};
    });
    panel_->progress(true, QString("Projecting ") + mesh_map_names[role]);
    poll_.start();
  }
  catch (const std::exception &e) {
    error_ = QString::fromUtf8(e.what());
    failed_token_ = source + token(uid, role);
    requests_.clear();
    panel_->progress(false, error_);
    hooks_.log(error_);
  }
}
void SkewController::poll()
{
  performance::Scope timing("bake_result_poll");
  process_gpu();
  if (!job_.valid() || job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  try {
    auto result = job_.get();
    auto *doc = hooks_.document();
    if (doc && result.document == doc->id() && result.source == source_key()) {
      engine_ = result.engine;
      general_ = result.general;
      if (engine_)
        geometry_ = engine_->geometry();
      else if (general_)
        geometry_ = general_->geometry();
      if (result.token == token(result.material, result.role) &&
          (result.manual || result.role == auto_role()) && !result.map.tiles.empty())
      {
        if (result.role == 0)
          regions_.erase(result.material);
        ++completed_;
        ++role_completed_[result.role];
        const auto &previous = doc->set(result.material).mesh_maps[result.role];
        if (previous) {
          auto merged = *previous;
          merged.name = result.map.name;
          for (auto &[tile, plane] : result.map.tiles)
            merged.tiles[tile] = std::move(plane);
          result.map = std::move(merged);
        }
        std::uint64_t bytes = 0;
        for (const auto &set : doc->texture_sets())
          for (int role = 0; role < 7; ++role) {
            const MeshMap *map = set.material_session_uid == result.material &&
                                         role == result.role ?
                                     &result.map :
                                 set.mesh_maps[role] ? &*set.mesh_maps[role] :
                                                       nullptr;
            if (map)
              for (const auto &[tile, plane] : map->tiles)
                bytes += std::uint64_t(plane.resolution) * plane.rows() * 16;
          }

        bool different = !previous || previous->tiles.size() != result.map.tiles.size();
        for (const auto &[tile, plane] : result.map.tiles)
          different |= !previous || !previous->tiles.contains(tile) ||
                       previous->tiles.at(tile).hash != plane.hash;
        std::unique_ptr<TextureDocument> before;
        if (different && result.manual) {
          before = std::make_unique<TextureDocument>(*doc);
          auto recipe = before->bake_configuration();
          recipe["ui_mode"] = 1;
          before->set_bake_configuration(recipe);
        }
        if (different)
          hooks_.install(result.material, result.role, std::move(result.map));
        if (before)
          hooks_.history(std::move(before),
                         "Bake skew " + QString::fromUtf8(mesh_map_names[result.role]),
                         result.material);
        error_.clear();
        failed_token_.clear();
        panel_->progress(false, QString::fromUtf8(mesh_map_names[result.role]) + " updated");
      }
      else
        ++dropped_;
    }
    else
      ++dropped_;
  }
  catch (const std::exception &e) {
    requests_.clear();
    debounce_.stop();
    error_ = QString::fromUtf8(e.what());
    failed_token_ = source_key() + token();
    panel_->progress(false, error_);
    hooks_.log(error_);
  }
  poll_.stop();
  sync();
  if (!requests_.empty() && !debounce_.isActive())
    debounce_.start();
  else if (error_.isEmpty() && mode() > 0 && auto_role() < 0)
    panel_->progress(false, "Automatic baking is off; use a map's Bake button after edits.");
}
bool SkewController::flush()
{
  finish_edit();
  if (!pending())
    return error_.isEmpty();
  debounce_.stop();
  start_job();
  QProgressDialog progress(
      "Finishing mesh-map bake…", "Cancel", 0, 0, qobject_cast<QWidget *>(parent()));
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(200);
  QElapsedTimer elapsed;
  elapsed.start();
  while (pending()) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    poll();
    if (progress.wasCanceled() || elapsed.elapsed() > 120000)
      return false;
    QThread::msleep(1);
  }
  return error_.isEmpty();
}
QJsonObject SkewController::reference()
{
  if (!flush() || !low_ || !high_)
    return {{"exact", false}};
  auto *doc = hooks_.document();
  const auto uid = hooks_.material();
  const int role = auto_role() >= 0    ? auto_role() :
                   hooks_.preview_role ? std::max(0, hooks_.preview_role()) :
                                         0;
  const auto recipe = options(uid, role);
  const auto opts = skew_options(recipe);
  const auto field = doc->set(uid).skew;
  const auto selected = tiles(uid);
  const auto name = doc->set(uid).name;
  const auto base = doc->set(uid).base_values;
  auto low = low_, high = high_, cage = cage_;
  MeshBakeSettings channels;
  channels.use_project_mesh = recipe["project_mesh"].toBool(true);
  read_surface_bake_settings(channels, recipe);
  capture_bake_channels(channels, *doc, *low);
  auto reference_cancel = std::make_shared<std::atomic<bool>>(false);
  auto task = std::async(std::launch::async, [=, this] {
    std::atomic<bool> cancel = false;
    if (role == 0 && !recipe["project_mesh"].toBool(true) && !opts.ray.use_cage &&
        opts.ray.average_normals)
    {
      SkewBakeEngine engine(low, high);
      return engine.bake(uid, field, selected, opts, {}, cancel);
    }
    AutoBakeOptions o;
    o.ray = opts.ray;
    o.maps.resolution = opts.resolution;
    o.maps.height = opts.height;
    o.maps.sampling = opts.sampling;
    o.maps.dilation = opts.dilation;
    o.maps.diffusion = opts.diffusion;
    o.maps.rays = recipe["rays"].toInt(256);
    o.maps.use_project_mesh = recipe["project_mesh"].toBool(true);
    o.maps.enabled.fill(false);
    o.maps.enabled[role] = true;
    read_surface_bake_settings(o.maps, recipe);
    o.maps.displacement = channels.displacement;
    o.maps.normal_maps = channels.normal_maps;
    if (hooks_.with_gpu)
      o.maps.with_gpu = [this, reference_cancel](const auto &work) {
        request_gpu(work, reference_cancel);
      };
    AutoBakeEngine engine(low, high, cage);
    auto maps = engine.bake(uid, name, base, selected, field, o, cancel);
    return maps[role] ? std::move(*maps[role]) : MeshMap{};
  });
  while (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    process_gpu();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  const auto expected = task.get();
  const auto &current = doc->set(uid).mesh_maps[role];
  bool exact = current && !expected.tiles.empty();
  for (const auto &[tile, plane] : expected.tiles)
    exact &= current && current->tiles.contains(tile) &&
             current->tiles.at(tile).hash == plane.hash;
  return {{"exact", exact}, {"role", role}, {"tiles", int(expected.tiles.size())}};
}
QJsonObject SkewController::state() const
{
  QJsonArray roles;
  for (int count : role_completed_)
    roles.append(count);
  return {{"settings_visible", dock_->isVisible()},
          {"mode", mode()},
          {"auto_role", auto_role()},
          {"pending", pending()},
          {"queue_size", int(requests_.size())},
          {"source_loads", property("sourceLoads").toInt()},
          {"completed", completed_},
          {"role_completed", roles},
          {"dropped", dropped_},
          {"error", error_},
          {"vectors", int(vectors_.size())},
          {"eligible", eligible()},
          {"bake_eligible", bake_eligible()}};
}
}  // namespace bts
