#pragma once
#include "LongOperationCancel.hh"
#include "MeshBaker.hh"
#include "TextureDocument.hh"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTimer>

namespace bts {
// A real worker reaches its owner rendezvous, then Escape wakes it without
// ever creating a graphics context or dispatching a GPU operation.
inline int run_mesh_bake_cancel_test(const QString &directory)
{
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Cancel fixture";
  material.uniform_editable = true;
  scene.materials = {material};
  UvFaceTriangle triangle;
  triangle.positions = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
  triangle.corners = {{{0, 0}, {1, 0}, {0, 1}}};
  triangle.vertex_ids = {1, 2, 3};
  scene.uv_triangles = {triangle};
  scene.maximum = {1, 1, 1};
  scene.has_bounds = true;
  ImportSummary imported;
  imported.materials = {{1, material.name}};
  TextureDocument document({}, imported, scene);
  const auto original = document.to_json();
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = atlas->height = 128;
  atlas->extent = 1;
  FillSurfacePoint point;
  point.valid = true;
  point.position = {.25, .25, 0};
  point.normal = {0, 0, 1};
  atlas->tiles[1001] = std::vector<FillSurfacePoint>(128 * 128, point);
  MeshBakeSettings settings;
  settings.resolution = 128;
  settings.external_source = true;
  settings.enabled.fill(false);
  settings.enabled[int(MeshMapRole::AmbientOcclusion)] = true;
  bool gpu_called = false;
  settings.with_gpu = [&](const auto &) {
    gpu_called = true;
    throw std::runtime_error("Cancel test must not dispatch GPU work");
  };
  MeshBakeInput input;
  input.material_uid = 1;
  input.name = material.name;
  input.atlas = atlas;
  MeshBakeJob job(scene, {input}, settings);
  CancellableOperationDialog progress;
  QLineEdit editor(&progress);
  bool requested = false, timed_out = false;
  int beats = 0;
  QElapsedTimer elapsed;
  elapsed.start();
  progress.pending = [&] { return !job.finished.load(std::memory_order_acquire); };
  progress.cancel = [&] {
    requested = true;
    job.cancel();
  };
  LongOperationCancel escape(progress.pending, [&] { progress.reject(); });
  QTimer timer;
  timer.setInterval(1);
  QObject::connect(&timer, &QTimer::timeout, &progress, [&] {
    ++beats;
    if (elapsed.elapsed() > 10000) {
      timed_out = true;
      job.cancel();
    }
    if (!requested && job.gpu_pending()) {
      QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QApplication::sendEvent(&editor, &key);
      require(requested && progress.isVisible(),
              "Escape closed the progress UI before worker retirement");
    }
    if (job.finished.load(std::memory_order_acquire))
      progress.reject();
  });
  timer.start();
  require(progress.exec() == QDialog::Rejected, "Cancelled bake dialog was accepted");
  require(!timed_out && requested && job.cancelled.load() && job.finished.load(),
          "Bake cancellation did not finish");
  require(job.error == "Bake cancelled" && job.result.empty() && !gpu_called && beats > 1,
          "Cancelled bake published work or blocked the UI");
  require(document.to_json() == original, "Cancelled bake changed the document");
  QDir().mkpath(directory);
  QFile output(directory + "/esc-bake.json");
  require(output.open(QIODevice::WriteOnly), "Cannot write bake cancel proof");
  output.write(QJsonDocument(QJsonObject{{"cancelled", true},
                                         {"document_unchanged", true},
                                         {"gpu_dispatches", 0},
                                         {"ui_heartbeats", beats},
                                         {"elapsed_ms", elapsed.elapsed()}})
                   .toJson());
  std::puts(
      "ESC_BAKE_CANCEL PASS real worker, owner rendezvous, unchanged document, zero GPU dispatches");
  return 0;
}
}  // namespace bts
