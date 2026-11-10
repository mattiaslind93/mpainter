// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlenderHost.hh"
#include "NumericLocale.hh"
#include "OrbitCamera.hh"
#include "SpsmMeshMapFixture.hh"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QTextStream>
#include <QThread>
#include <bit>
#include <clocale>
using namespace bts;
using namespace bts::spsm_meshmap_test;
namespace {
int run(BlenderHost &host,
        QOpenGLContext &context,
        QOffscreenSurface &surface,
        QOpenGLFramebufferObject &target,
        const QString &out,
        const QString &fixtures)
{
  QDir().mkpath(out);
  {
    QFile mtl(out + "/two-tiles.mtl");
    require(mtl.open(QIODevice::WriteOnly), "Cannot create material fixture");
    mtl.write("newmtl AliasFixture\nKd 0.5 0.5 0.5\n");
  }
  {
    QFile obj(out + "/two-tiles.obj");
    require(obj.open(QIODevice::WriteOnly), "Cannot create mesh fixture");
    QTextStream f(&obj);
    f << "mtllib two-tiles.mtl\no AliasFixture\nusemtl AliasFixture\n";
    for (int i = 0; i < 2; ++i) {
      f << "v " << i * 1.2 << " 0 0\nv " << i * 1.2 + 1 << " 0 0\nv " << i * 1.2 + 1 << " 1 0\nv "
        << i * 1.2 << " 1 0\n";
      for (auto uv : std::array<std::array<float, 2>, 4>{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}})
        f << "vt " << i + uv[0] << ' ' << uv[1] << '\n';
      f << "f";
      for (int c = 1; c <= 4; ++c)
        f << ' ' << i * 4 + c << '/' << i * 4 + c;
      f << '\n';
    }
  }
  NewProjectOptions options;
  options.absolute_mesh_path = (out + "/two-tiles.obj").toStdString();
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  const auto environment = qEnvironmentVariable(
      "BTS_ENVIRONMENT",
      qEnvironmentVariable("BTS_ROOT") + "/build/environment-cache/TT_environmentLights_v001.hdr");
  auto imported = host.new_project(
      options.absolute_mesh_path, environment.toStdString(), &options);
  auto scene = host.scene_snapshot();
  TextureDocument doc(options, imported, scene);
  require(doc.texture_sets().size() == 1, "Expected one set");
  auto &set = doc.set(doc.texture_sets().front().material_session_uid);
  require(set.tiles == std::set<int>({1001, 1002}), "New project lost two tiles");
  set.precision[0] = ChannelPrecision::RGB32F;
  set.layers.front().visible = false;
  set.layers.front().channels.fill(false);
  maps(set);
  Builder builder;
  auto original = builder.prepare();
  auto prepared = enhance_prepared_spsm_mesh_maps(original, policy());
  apply_material_preset(set, prepared.material);
  OrbitCamera camera;
  require(camera.frame_bounds({scene.minimum[0], scene.minimum[1], scene.minimum[2]},
                              {scene.maximum[0], scene.maximum[1], scene.maximum[2]},
                              {target.width(), target.height()}),
          "Cannot frame fixture");
  ViewFrame frame;
  frame.pane = {ViewKind::Scene, 0, 0, target.width(), target.height()};
  auto view = camera.view_matrix(),
       projection = camera.projection_matrix({target.width(), target.height()});
  std::copy_n(view.constData(), 16, frame.view.begin());
  std::copy_n(projection.constData(), 16, frame.projection.begin());
  auto clip = camera.clipping_range();
  frame.clipping = {clip.first, clip.second};
  int draw_calls = 0;
  double maximum = 0;
  std::uint64_t checks = 0;
  auto publish = [&] {
    auto &current = doc.texture_sets().front();
    host.apply_texture_set(current, 128);
    host.set_selected_material(current.material_session_uid);
    QElapsedTimer timer;
    timer.start();
    do {
      require(context.makeCurrent(&surface), "Lost offscreen context");
      target.bind();
      host.draw(std::span(&frame, 1), target.width(), target.height());
      ++draw_calls;
      // Offscreen has no swap/presentation pacing. Wait for this real render
      // fence before submitting another frame: otherwise compute_ready() can
      // repeatedly see a newer unfinished frame and starve staged map uploads.
      // Diagnostic-only synchronization, never an interactive/performance path.
      host.with_gpu([&] { context.functions()->glFinish(); });
      require(host.shader_failures().empty(), "Native shader failure");
      require(timer.elapsed() < 30000, "GPU publication timed out");
      if (host.texture_updates_pending())
        QThread::msleep(1);
    } while (host.texture_updates_pending());
  };
  std::vector<std::array<int, 2>> coordinates;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      coordinates.push_back({x, y});
  auto oracle = [&](int revision, bool independent) {
    std::map<int, std::uint64_t> hashes;
    const auto &current = doc.texture_sets().front();
    for (int tile : current.tiles) {
      const auto pixels = host.diagnostic_gpu_pixels(
          current.material_session_uid, 0, coordinates, tile);
      require(pixels.size() == coordinates.size(), "Native GPU tile missing pixels");
      std::uint64_t hash = 1469598103934665603ULL;
      for (const auto &pixel : pixels)
        for (float c : pixel) {
          hash ^= std::bit_cast<std::uint32_t>(c);
          hash *= 1099511628211ULL;
        }
      hashes[tile] = hash;
      for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
          const auto want =
              independent ?
                  expected(tile, x, y, revision) :
                  std::array<float, 3>{
                      TextureDocument::evaluate(current, x, y, tile).material.base_color[0],
                      TextureDocument::evaluate(current, x, y, tile).material.base_color[1],
                      TextureDocument::evaluate(current, x, y, tile).material.base_color[2]};
          for (int c = 0; c < 3; ++c) {
            const auto value = pixels[y * 128 + x][c];
            require(std::isfinite(value), "Nonfinite native AO alias pixels");
            const auto error = std::abs(double(value) - want[c]);
            maximum = std::max(maximum, error);
            ++checks;
            if (error > 3e-5) {
              std::fprintf(stderr,
                           "AO_ALIAS_GPU tile=%d x=%d y=%d c=%d actual=%.9g "
                           "expected=%.9g\n",
                           tile,
                           x,
                           y,
                           c,
                           value,
                           want[c]);
              throw std::runtime_error("Native AO alias differs from oracle");
            }
          }
        }
    }
    return hashes;
  };
  publish();
  const auto before = oracle(0, true);
  maps(set, 1);
  publish();
  const auto after = oracle(1, true);
  for (int tile : {1001, 1002})
    require(after.at(tile) != before.at(tile), "Live rebake did not invalidate native GPU output");
  const auto saved = out + "/ao-alias.blend";
  host.save_project(doc, saved.toStdString());
  doc = host.open_project(saved.toStdString());
  require(doc.texture_sets().front().mesh_maps[3] &&
              doc.texture_sets().front().mesh_maps[3]->tiles.size() == 2,
          "Packed Open lost AO tiles");
  publish();
  oracle(1, true);
  // Real source metadata controls the restored branch. Other source layers are
  // still present/disabled as reported. This is a native implementation oracle,
  // never an external vendor appearance reference.
  const auto leather = fixtures + "/worn leather chest.spsm";
  auto sha = file_hash(leather);
  auto real_input = prepare_spsm_material(std::filesystem::path(leather.toStdString()),
                                          color_policy());
  auto real = enhance_prepared_spsm_mesh_maps(real_input, policy());
  require(has(real, "meshmap-ao-color-restored"), "Actual Leather direct alias not restored");
  auto &actual = doc.set(doc.texture_sets().front().material_session_uid);
  actual.layers = {TextureLayer{}};
  actual.layers[0].visible = false;
  actual.selected_layer = actual.layers[0].id;
  apply_material_preset(actual, real.material);
  publish();
  oracle(1, false);
  const auto actual_saved = out + "/leather-native-partial.blend";
  host.save_project(doc, actual_saved.toStdString());
  doc = host.open_project(actual_saved.toStdString());
  publish();
  oracle(1, false);
  require(file_hash(leather) == sha && real.source == real_input.source &&
              !leaf(real_input).visible,
          "Original source changed");
  QFile proof(out + "/verified.json");
  require(proof.open(QIODevice::WriteOnly), "Cannot write proof");
  proof.write(QJsonDocument(QJsonObject{{"tiles", QJsonArray{1001, 1002}},
                                        {"resolution", 128},
                                        {"renderer", QString::fromStdString(host.renderer())},
                                        {"visible_windows", 0},
                                        {"native_draw_calls", draw_calls},
                                        {"component_checks", qint64(checks)},
                                        {"maximum_error", maximum},
                                        {"tolerance", 3e-5},
                                        {"synthetic_independent_formula", true},
                                        {"genuine_premultiplied_native_maps", true},
                                        {"genuine_gpu_readback", true},
                                        {"offscreen_render_completion_pacing", true},
                                        {"live_rebake_both_tile_hashes_changed", true},
                                        {"source_free_packed_open", true},
                                        {"actual_leather_source_sha256", QString::fromLatin1(sha)},
                                        {"actual_partial_native_roundtrip", true},
                                        {"appearance_verified", false},
                                        {"procedural_packages_executed", 0}})
                  .toJson());
  std::printf("SPSM_MESHMAP_NATIVE_PASS checks=%llu max=%.9g visible_windows=0\n",
              (unsigned long long)checks,
              maximum);
  return 0;
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(format);
  initialize_numeric_locale();
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");
  try {
    require(argc == 3, "Expected output and fixture directories");
    BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "Cannot create offscreen context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface),
            "Cannot activate offscreen surface");
    QOpenGLFramebufferObject target(384, 256, QOpenGLFramebufferObject::CombinedDepthStencil);
    require(target.isValid(), "Cannot create offscreen framebuffer");
    struct Close {
      BlenderHost &host;
      ~Close()
      {
        host.shutdown();
      }
    } close{host};
    host.initialize_gpu(context, surface, target.handle());
    require(QString::fromStdString(host.renderer()).contains("NVIDIA", Qt::CaseInsensitive),
            "Requires NVIDIA hardware renderer");
    return run(host,
               context,
               surface,
               target,
               QString::fromLocal8Bit(argv[1]),
               QString::fromLocal8Bit(argv[2]));
  }
  catch (const std::exception &e) {
    std::fprintf(stderr, "SPSM_MESHMAP_NATIVE_FAIL %s\n", e.what());
    return 1;
  }
}
