// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuPointiness.hh"
#include "AutoBakeEngine.hh"
#include "SkewController.hh"
#include <QMainWindow>
#include <QToolButton>
#include "BakeMatch.hh"
#include "BakeSamplesDialogTest.hh"
#include "BakeSamplesFixture.hh"
#include <QCheckBox>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QFile>
namespace bts::gpu_phase_one {
inline void require(bool ok, const char *message)
{
  if (!ok)
    throw std::runtime_error(message);
}
inline void wait(MeshBakeJob &job)
{
  QElapsedTimer timer;
  timer.start();
  while (!job.finished.load(std::memory_order_acquire)) {
    job.process_gpu();
    require(timer.elapsed() < 1800000, "Phase one bake timed out");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(job.error.empty(), job.error.c_str());
}
inline double difference(const MeshMaps &a, const MeshMaps &b, int role)
{
  const auto &x = a[role]->tiles.at(1001).pixels->rgba, &y = b[role]->tiles.at(1001).pixels->rgba;
  require(x.size() == y.size(), "Map size changed");
  double maximum = 0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    require(std::isfinite(y[i]), "Nonfinite GPU map");
    maximum = std::max(maximum, std::abs(double(x[i]) - y[i]));
  }
  return maximum;
}
inline QJsonArray filters(BlenderHost &host)
{
  SceneSnapshot source;
  source.materials.resize(1);
  source.materials[0].session_id = 1;
  source.minimum = {-1, -1, -1};
  source.maximum = {1, 1, 1};
  source.has_bounds = true;
  std::uint32_t index = 0;
  for (const auto &triangle : bake_samples_fixture::triangles()) {
    UvFaceTriangle t;
    t.positions = triangle;
    const auto object = index < 2 ? 1u : 2u;
    for (int k = 0; k < 3; ++k)
      t.vertex_ids[k] = (std::uint64_t(object) << 32) | (index * 3 + k);
    ++index;
    source.uv_triangles.push_back(t);
  }
  source.object_names = {{1, "upper_ignorebf"}, {2, "lower"}};
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 64;
  auto &points = atlas->tiles[1001];
  points.resize(4096);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      auto &p = points[y * 64 + x];
      p.valid = true;
      p.position = {float(x + .5) / 64 - .5f, float(y + .5) / 64 - .5f, 0};
      p.normal = {0, 0, 1};
      p.match_object = 10 + x % 4;
    }
  MeshBakeSettings s;
  s.resolution = s.height = 64;
  s.external_source = true;
  s.rays = 256;
  s.denoise = false;
  s.dilation = 0;
  s.enabled.fill(false);
  s.enabled[3] = s.enabled[6] = true;
  s.ao_distance = s.thickness_distance = 2;
  QJsonArray cases;
  MeshMaps unfiltered;
  for (bool match : {false, true})
    for (int backface : {0, 1, 2}) {
      s.matched_objects = match ? std::map<std::uint32_t, std::set<std::uint32_t>>{{10, {1}},
                                                                                   {11, {2}},
                                                                                   {12, {}}} :
                                  std::map<std::uint32_t, std::set<std::uint32_t>>{};
      s.ignore_backface_objects = bake_backface_objects(
          source, backface != 0, backface == 1 ? "_ignorebf" : "");
      require(backface != 0 || s.ignore_backface_objects.empty(), "OFF must ignore suffix");
      require(backface != 1 || s.ignore_backface_objects == std::set<std::uint32_t>{1},
              "Suffix selection changed");
      s.with_gpu = {};
      MeshBakeJob cpu(source, {{1, "filters", {}, atlas}}, s);
      wait(cpu);
      s.with_gpu = [&](const std::function<void()> &work) { host.with_gpu(work); };
      MeshBakeJob gpu(source, {{1, "filters", {}, atlas}}, s);
      wait(gpu);
      require(gpu.gpu_maps == 2 && gpu.cpu_ray_maps == 0 && !gpu.cpu_fallback,
              "Filtered maps did not take GPU path");
      for (int role : {3, 6}) {
        const double error = difference(cpu.result[0].maps, gpu.result[0].maps, role);
        std::fprintf(stderr,
                     "GPU_FILTER_ERROR role=%d match=%d backface=%d maximum=%.9g\n",
                     role,
                     match,
                     backface,
                     error);
        if (error > 2e-5) {
          const auto &a = cpu.result[0].maps[role]->tiles.at(1001).pixels->rgba,
                     &b = gpu.result[0].maps[role]->tiles.at(1001).pixels->rgba;
          for (std::size_t k = 0; k < a.size(); k += 4)
            if (std::abs(a[k] - b[k]) > 2e-5) {
              std::fprintf(
                  stderr, "GPU_FILTER_PIXEL index=%zu cpu=%.9g gpu=%.9g\n", k / 4, a[k], b[k]);
              break;
            }
        }
        require(error <= 2e-5, "Filtered GPU map exceeds 2e-5 CPU tolerance");
        const double changed = unfiltered[role] ?
                                   difference(unfiltered, gpu.result[0].maps, role) :
                                   0;
        if (match || backface == 2)
          require(changed > 0, "Filter fixture did not alter image");
        cases.append(QJsonObject{{"role", role},
                                 {"match_names", match},
                                 {"ignore_backface", backface != 0},
                                 {"suffix", backface == 1 ? "_ignorebf" : ""},
                                 {"max_error", error},
                                 {"change_from_unfiltered", changed},
                                 {"gpu_maps", gpu.gpu_maps.load()}});
      }
      if (!match && !backface)
        unfiltered = cpu.result[0].maps;
      s.with_gpu = [](const auto &) { throw std::runtime_error("Injected GPU unavailable"); };
      MeshBakeJob fallback(source, {{1, "filters", {}, atlas}}, s);
      wait(fallback);
      require(fallback.cpu_ray_maps == 2 && fallback.cpu_fallback,
              "Filtered CPU fallback missing");
      for (int role : {3, 6})
        require(difference(cpu.result[0].maps, fallback.result[0].maps, role) == 0,
                "Filtered fallback changed image");
    }
  return cases;
}
inline QJsonArray pointiness(BlenderHost &host, SceneSnapshot source)
{
  QJsonArray cases;
  // UV duplicates, original polygon boundaries, disconnected components and
  // spatially coincident separate objects all go through the same welded graph.
  source = prepare_bake_surface(source, MeshBakeSettings{});
  const float extent = std::max({source.maximum[0] - source.minimum[0],
                                 source.maximum[1] - source.minimum[1],
                                 source.maximum[2] - source.minimum[2]});
  for (float radius : {0.f, extent * .02f}) {
    SurfaceCurvature cpu(source, false), gpu = cpu;
    cpu.calculate_pointiness(radius);
    GpuBakeRayStats stats;
    host.with_gpu([&] { gpu_calculate_pointiness(gpu, radius, &stats); });
    double error = 0, low = 1, high = 0;
    for (std::size_t i = 0; i < cpu.vertices.size(); ++i) {
      error = std::max(error,
                       std::abs(double(cpu.vertices[i].curvature) - gpu.vertices[i].curvature));
      low = std::min(low, double(cpu.vertices[i].curvature));
      high = std::max(high, double(cpu.vertices[i].curvature));
    }
    std::fprintf(stderr, "GPU_POINTINESS_ERROR radius=%.9g max=%.9g\n", radius, error);
    require(error <= 2e-5, "Pointiness GPU differs from CPU by more than 2e-5");
    require(high - low > .01, "Pointiness fixture is flat");
    cases.append(QJsonObject{{"radius", radius},
                             {"vertices", qint64(cpu.vertices.size())},
                             {"max_error", error},
                             {"minimum", low},
                             {"maximum", high},
                             {"batches", qint64(stats.batches)}});
  }
  MeshBakeSettings s;
  s.resolution = s.height = 64;
  s.use_project_mesh = true;
  s.enabled.fill(false);
  s.enabled[4] = true;
  s.dilation = 0;
  const auto uid = source.materials.front().session_id;
  TexturePadding geometry(source, uid);
  auto atlas = geometry.projection_geometry(64, {1001}, false);
  for (float radius : {0.f, extent * .02f}) {
    s.curvature_radius = radius;
    s.with_gpu = {};
    MeshBakeJob cpu(source, {{uid, "Pointiness", {}, atlas}}, s);
    wait(cpu);
    s.with_gpu = [&](const auto &work) { host.with_gpu(work); };
    MeshBakeJob gpu(source, {{uid, "Pointiness", {}, atlas}}, s);
    wait(gpu);
    require(gpu.gpu_maps == 1 && !gpu.cpu_fallback, "Curvature map skipped GPU");
    const auto error = difference(cpu.result[0].maps, gpu.result[0].maps, 4);
    require(error <= 2e-5, "Pointiness image exceeds tolerance");
    s.with_gpu = [](const auto &) { throw std::runtime_error("Injected Pointiness failure"); };
    MeshBakeJob fallback(source, {{uid, "Pointiness", {}, atlas}}, s);
    wait(fallback);
    require(fallback.cpu_fallback &&
                difference(cpu.result[0].maps, fallback.result[0].maps, 4) == 0,
            "Pointiness CPU fallback changed map");
    cases.append(QJsonObject{
        {"map_radius", radius}, {"map_max_error", error}, {"fallback_bit_exact", true}});
  }
  return cases;
}
inline QJsonObject widgets(const QString &directory)
{
  using bl_widget_test::control;
  using bl_widget_test::click;
  using bl_widget_test::select_index;
  SceneSnapshot scene;
  scene.materials.resize(1);
  scene.materials[0].session_id = 1;
  scene.materials[0].name = "Preset proof";
  scene.materials[0].uniform_editable = true;
  ImportSummary imported;
  imported.materials = {{1, "Preset proof"}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  QJsonObject recipe;
  const auto id = document.id(), set = document.texture_sets().front().id;
  require(!mesh_bake_template(id)["ignore_backface"].toBool(), "New dialog defaults ON");
  QDialog window;
  auto *layout = new QVBoxLayout(&window);
  auto *panel = new SkewPanel(&window);
  layout->addWidget(panel);
  const auto sync = [&] { panel->synchronize(recipe, {}, false, false, set); };
  panel->recipe_changed = [&](const auto &r) {
    recipe = r;
    sync();
  };
  sync();
  window.resize(1050, 900);
  window.show();
  QApplication::processEvents();
  auto &box = control<QCheckBox>(*panel, "bakeIgnoreBackface");
  require(!box.isChecked(), "New Baking panel defaults ON");
  require(box.parentWidget()->objectName() == "bakingCommonParameters",
          "Panel checkbox outside Common");
  bl_widget_test::reveal(box);
  bl_widget_test::key(box, Qt::Key_Space);
  require(recipe["ignore_backface"].toBool(), "Panel checkbox not saved");
  auto &preset = control<QComboBox>(*panel, "bakeQualityPreset");
  select_index(preset, 0);
  require(recipe["rays"].toInt() == 64 && recipe["sampling"].toInt() == 1 &&
              !recipe["denoise"].toBool(),
          "Panel Draft mismatch");
  window.grab().save(directory + "/draft-panel.png");
  apply_mesh_bake_template(id, recipe);
  window.hide();
  for (int visit = 0; visit < 3; ++visit) {
    std::exception_ptr error;
    bool visited = false;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      try {
        require(dialog, "Bake dialog missing");
        visited = true;
        dialog->resize(1000, 900);
        auto &ignore = control<QCheckBox>(*dialog, "bakeIgnoreBackface");
        require(ignore.isChecked() == (visit == 0), "Dialog/template checkbox roundtrip changed");
        auto &quality = control<QComboBox>(*dialog, "bakeQualityPreset");
        if (visit == 0) {
          require(quality.currentIndex() == 0, "Dialog lost Draft");
          bl_widget_test::reveal(ignore);
          bl_widget_test::key(ignore, Qt::Key_Space);
          select_index(quality, 1);
          dialog->grab().save(directory + "/final-dialog.png");
        }
        if (visit == 1) {
          require(quality.currentIndex() == 1, "Dialog lost Final");
          select_index(quality, 0);
        }
        click(control<QPushButton>(*dialog, "saveBakeSettings"));
      }
      catch (...) {
        error = std::current_exception();
        if (dialog)
          dialog->reject();
      }
    });
    require(!choose_mesh_bake(nullptr, document, scene), "Widget test unexpectedly baked");
    if (error)
      std::rethrow_exception(error);
    require(visited, "Modal test not visited");
    recipe = mesh_bake_template(id);
    sync();
    require(!box.isChecked(), "Panel lost saved OFF");
  }
  require(recipe["rays"].toInt() == 64 && recipe["sampling"].toInt() == 1 &&
              !recipe["denoise"].toBool(),
          "Dialog Draft not persisted");
  document.set_bake_configuration(recipe);
  const auto &binding_set = document.texture_sets().front();
  TextureSetBinding binding{binding_set.id, 1, binding_set.name};
  auto reopened = TextureDocument::from_json(document.to_json(), std::span(&binding, 1));
  require(reopened.bake_configuration() == recipe, "Project JSON changed template");
  apply_mesh_bake_template(id, {{"ignore_backface", true}});
  require(mesh_bake_template(id)["ignore_backface"].toBool(), "Old explicit ON lost");
  apply_mesh_bake_template(id, {});
  require(!mesh_bake_template(id)["ignore_backface"].toBool(), "Reset defaults ON");
  std::exception_ptr default_error;
  QTimer::singleShot(0, [&] {
    auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
    try {
      require(dialog, "Default modal missing");
      require(!control<QCheckBox>(*dialog, "bakeIgnoreBackface").isChecked(),
              "New modal defaults ON");
      dialog->reject();
    }
    catch (...) {
      default_error = std::current_exception();
      if (dialog)
        dialog->reject();
    }
  });
  document.set_bake_configuration({});
  require(!choose_mesh_bake(nullptr, document, scene), "Default modal unexpectedly baked");
  if (default_error)
    std::rethrow_exception(default_error);
  return {{"both_common_checkboxes", true},
          {"default_off", true},
          {"old_explicit_on_preserved", true},
          {"template_roundtrip", true},
          {"project_json_roundtrip", true},
          {"draft_final_controls", true}};
}
inline QJsonArray gui_bakes(BlenderHost &host, const SceneSnapshot &scene)
{
  using bl_widget_test::control;
  using bl_widget_test::click;
  const auto uid = scene.materials.front().session_id;
  ImportSummary imported;
  imported.materials = {{uid, "Character"}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  QJsonObject recipe{{"resolution", 32},
                     {"height", 32},
                     {"rays", 64},
                     {"denoise", false},
                     {"dilation", 0},
                     {"project_mesh", true},
                     {"ignore_marker", ""},
                     {"match_names", true},
                     {"enabled", QJsonArray{false, false, false, true, false, false, true}}};
  document.set_bake_configuration(recipe);
  QMainWindow owner;
  owner.resize(1100, 950);
  int gpu_calls = 0;
  QString error;
  const auto dispatch = [&](const std::function<void()> &work) {
    host.with_gpu(work);
    ++gpu_calls;
  };
  SkewHooks hooks;
  hooks.document = [&] { return &document; };
  hooks.scene = [&]() -> const SceneSnapshot & { return scene; };
  hooks.bake_scene = [&] { return scene; };
  hooks.material = [&] { return uid; };
  hooks.with_gpu = dispatch;
  hooks.end_input = [] {};
  hooks.refresh = [] {};
  hooks.history = [](auto, auto, auto) {};
  hooks.view = [](auto, auto, auto, auto) {};
  hooks.log = [&](const QString &message) { error = message; };
  hooks.install = [&](auto material, int role, MeshMap map) {
    document.set(material).mesh_maps[role] = std::move(map);
  };
  SkewController controller(&owner, std::move(hooks));
  controller.mode(1);
  QApplication::processEvents();
  auto &panel = control<QWidget>(owner, "skewPanel");
  auto &box = control<QCheckBox>(panel, "bakeIgnoreBackface");
  require(!box.isChecked(), "Actual controller default must be OFF");
  QJsonArray proof;
  for (bool on : {false, true}) {
    if (box.isChecked() != on) {
      bl_widget_test::reveal(box);
      bl_widget_test::key(box, Qt::Key_Space);
    }
    for (int role : {3, 4, 6}) {
      const int before = gpu_calls;
      auto *button = panel.findChild<QToolButton *>(QString("bakeNow%1").arg(role));
      require(button, "Panel Bake button missing");
      click(*button);
      require(controller.flush(), "Panel production bake failed");
      require(gpu_calls == before + 1, "Panel map did not complete GPU work");
      require(document.set(uid).mesh_maps[role].has_value(), "Panel did not publish baked map");
      proof.append(QJsonObject{{"ui", "Baking panel"},
                               {"role", role},
                               {"ignore_backface", on},
                               {"gpu_calls", gpu_calls - before}});
    }
  }
  controller.mode(0);
  for (bool on : {false, true}) {
    recipe["ignore_backface"] = on;
    apply_mesh_bake_template(document.id(), recipe);
    std::exception_ptr failure;
    const int before = gpu_calls;
    QTimer::singleShot(0, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      try {
        require(dialog, "Production bake modal missing");
        require(control<QCheckBox>(*dialog, "bakeIgnoreBackface").isChecked() == on,
                "Production modal checkbox lost template");
        click(control<QPushButton>(*dialog, "startMeshBake"));
      }
      catch (...) {
        failure = std::current_exception();
        if (dialog)
          dialog->reject();
      }
    });
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, [&] {
      auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (dialog && dialog->objectName() == "meshBakeProgress") {
        auto *button = dialog->findChild<QPushButton *>("finishMeshBake");
        if (button && button->isEnabled() && button->text() == "OK")
          button->click();
      }
    });
    dismiss.start(10);
    auto result = choose_mesh_bake(nullptr, document, scene, {}, dispatch);
    dismiss.stop();
    if (failure)
      std::rethrow_exception(failure);
    require(result && result->size() == 1, "Dialog failed to bake");
    require(gpu_calls == before + 2, "Dialog AO/Thickness did not complete on GPU");
    proof.append(QJsonObject{
        {"ui", "Bake Mesh Maps"}, {"ignore_backface", on}, {"gpu_calls", gpu_calls - before}});
  }
  return proof;
}
inline void benchmark(BlenderHost &host, SceneSnapshot source, const QString &directory)
{
  const int size = qEnvironmentVariableIntValue("BTS_GPU_PHASE1_SIZE");
  const int role = qEnvironmentVariableIntValue("BTS_GPU_PHASE1_ROLE");
  const bool gpu = qEnvironmentVariableIntValue("BTS_GPU_PHASE1_GPU") != 0;
  require((size == 2048 || size == 4096) && (role == 3 || role == 4 || role == 6),
          "Invalid benchmark selection");
  const auto uid = source.materials.front().session_id;
  MeshBakeSettings settings;
  settings.resolution = settings.height = size;
  settings.use_project_mesh = true;
  settings.dilation = 0;
  settings.denoise = false;
  settings.rays = 64;
  settings.enabled.fill(false);
  settings.enabled[role] = true;
  for (const auto &t : source.uv_triangles) {
    auto id = std::uint32_t(t.vertex_ids[0] >> 32);
    settings.matched_objects[id].insert(id);
    settings.ignore_backface_objects.insert(id);
  }
  if (gpu)
    settings.with_gpu = [&](const auto &work) { host.with_gpu(work); };
  QElapsedTimer timer;
  timer.start();
  auto atlas = [&] {
    TexturePadding geometry(source, uid);
    return geometry.projection_geometry(size, {1001}, false);
  }();
  const double atlas_seconds = timer.nsecsElapsed() / 1e9;
  timer.restart();
  MeshBakeJob job(source, {{uid, "MakeHuman character", {}, std::move(atlas)}}, settings);
  wait(job);
  const double seconds = timer.nsecsElapsed() / 1e9;
  require(!gpu || (job.gpu_maps == 1 && !job.cpu_fallback), "Benchmark GPU fell back");
  const auto &plane = job.result[0].maps[role]->tiles.at(1001);
  const auto &rgba = plane.pixels->rgba;
  std::uint64_t covered = 0;
  double sum = 0;
  for (std::size_t i = 0; i < rgba.size(); i += 4)
    if (rgba[i + 3]) {
      ++covered;
      sum += rgba[i];
    }
  QImage thumbnail(512, 512, QImage::Format_RGB32);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x) {
      const auto i = (std::size_t(y * size / 512) * size + x * size / 512) * 4;
      int value = int(std::clamp(rgba[i], 0.f, 1.f) * 255);
      thumbnail.setPixel(x, 511 - y, qRgb(value, value, value));
    }
  const auto stem = QString("character-%1-%2-%3").arg(size).arg(role).arg(gpu ? "gpu" : "cpu");
  thumbnail.save(directory + "/" + stem + ".png");
  QJsonObject record{{"size", size},
                     {"role", role},
                     {"rays", 64},
                     {"sampling", 1},
                     {"denoise", false},
                     {"match_names", true},
                     {"ignore_backface", true},
                     {"suffix", ""},
                     {"gpu", gpu},
                     {"gpu_maps", job.gpu_maps.load()},
                     {"cpu_ray_maps", job.cpu_ray_maps.load()},
                     {"atlas_seconds", atlas_seconds},
                     {"job_seconds", seconds},
                     {"total_seconds", atlas_seconds + seconds},
                     {"triangles", qint64(source.uv_triangles.size())},
                     {"covered_pixels", qint64(covered)},
                     {"mean", sum / covered},
                     {"renderer", QString::fromStdString(host.renderer())}};
  QFile output(directory + "/" + stem + ".json");
  require(output.open(QIODevice::WriteOnly), "Cannot write benchmark");
  output.write(QJsonDocument(record).toJson());
  std::printf(
      "BTS_GPU_PHASE1_BENCH size=%d role=%d gpu=%d seconds=%.6f\n", size, role, gpu, seconds);
  std::fflush(stdout);
}
inline int run(BlenderHost &host, const QString &directory)
{
  QDir().mkpath(directory);
  auto source = host.load_bake_source(qEnvironmentVariable("BTS_GPU_PHASE1_MESH").toStdString());
  require(!source.materials.empty(), "Character material missing");
  for (auto &t : source.uv_triangles)
    t.material = 0;
  if (qEnvironmentVariableIsSet("BTS_GPU_PHASE1_BENCHMARKS")) {
    for (int size : {2048, 4096})
      for (int role : {3, 6, 4})
        for (int gpu : {0, 1}) {
          qputenv("BTS_GPU_PHASE1_SIZE", QByteArray::number(size));
          qputenv("BTS_GPU_PHASE1_ROLE", QByteArray::number(role));
          qputenv("BTS_GPU_PHASE1_GPU", QByteArray::number(gpu));
          benchmark(host, source, directory);
        }
    return 0;
  }
  if (qEnvironmentVariableIsSet("BTS_GPU_PHASE1_SIZE")) {
    benchmark(host, std::move(source), directory);
    return 0;
  }
  QJsonObject proof;
  proof["filters"] = filters(host);
  proof["pointiness"] = pointiness(host, source);
  proof["widgets"] = widgets(directory);
  proof["gui_bakes"] = gui_bakes(host, source);
  proof["renderer"] = QString::fromStdString(host.renderer());
  QFile output(directory + "/phase-one.json");
  require(output.open(QIODevice::WriteOnly), "Cannot write phase one proof");
  output.write(QJsonDocument(proof).toJson());
  std::puts("BTS_GPU_PHASE1_PASS");
  return 0;
}
}  // namespace bts::gpu_phase_one
