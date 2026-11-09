#pragma once
#include "GpuPaintPages.hh"
#include "GpuPathRasterTest.hh"
#include "GpuAuthoringProgramTrace.hh"
#include "SmudgeCapture.hh"
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <cmath>
#include <QJsonDocument>
#include <QScopeGuard>
namespace bts {
// Explicit owner diagnostic on the SAME finalized 14 owners. Readback/reference
// evaluation here is never used for artist rendering or startup latency claims.
inline int verify_gpu_authoring_workers(GpuPaintPages &pages)
{
  using namespace blender;
  using Image = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  require(pages.programs_started() && !pages.programs_pending(),
          "Authoring worker oracle needs finalized async owners");
  GpuAuthoringProgramTrace trace;
  for (int i = 0; i < 14; ++i) {
    trace.shaders[i] = pages.program_output(i).get();
    require(trace.shaders[i], "Missing authoring worker output");
  }
  require(!GpuAuthoringProgramTrace::active, "Nested authoring dispatch trace");
  GpuAuthoringProgramTrace::active = &trace;
  const auto clear_trace = qScopeGuard([&] { GpuAuthoringProgramTrace::active = nullptr; });
  constexpr int size = 128;
  int cases = 0, path_cases = 0;
  std::uint64_t components = 0;
  double maximum = 0;
  bool saved = false;
  QString operation = "path";
  const auto path = qEnvironmentVariable("BTS_GPU_AUTHORING_PROGRAM_TEST_OUTPUT");
  require(!path.isEmpty(), "Authoring oracle requires its explicit output path");
  QJsonArray owner_before;
  for (const auto &entry : performance::snapshot()) {
    const auto phase = entry.toObject()["phase"].toString();
    if (phase.startsWith("gpu_authoring_warmup_") || phase == "gpu_stack_warmup_authoring_pages")
      owner_before.append(entry);
  }
  const auto persist = [&](bool passed) {
    static constexpr const char *names[]{"filled",
                                         "ribbon",
                                         "polygon_coverage",
                                         "polygon_pages",
                                         "brush_stamp",
                                         "brush_over",
                                         "brush_contact",
                                         "smudge_gather",
                                         "smudge_sample",
                                         "smudge_carry",
                                         "smudge_eager_stamp",
                                         "smudge_eager_over",
                                         "smudge_contact",
                                         "copy"};
    QJsonArray slots;
    for (int i = 0; i < 14; ++i)
      slots.append(
          QJsonObject{{"slot", i},
                      {"operation", names[i]},
                      {"dispatches", qint64(trace.dispatches[i])},
                      {"same_handle", pages.program_output(i).get() == trace.shaders[i]}});
    QJsonObject proof{{"passed", passed},
                      {"last_operation", operation},
                      {"functional_only", true},
                      {"startup_latency_claim", false},
                      {"authoring_synchronous_compilations_during_suite",
                       qint64(trace.synchronous_compilations)},
                      {"authoring_asynchronous_submissions_during_suite",
                       qint64(trace.asynchronous_submissions)},
                      {"unknown_dispatch", trace.unknown},
                      {"slots", slots},
                      {"cases", cases},
                      {"direct_probe_finite_components", qint64(components)},
                      {"direct_probe_maximum_error", maximum},
                      {"existing_full_pixel_path_cases", path_cases},
                      {"path_original_absolute_bound", 2e-5},
                      {"owner_warmup_before_suite", owner_before}};
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "Cannot write authoring oracle proof");
    const auto json = QJsonDocument(proof).toJson();
    require(file.write(json) == json.size(), "Cannot finish authoring oracle proof");
  };
  const auto failure_evidence = qScopeGuard([&]() noexcept {
    if (!saved)
      try {
        persist(false);
      }
      catch (...) {
        std::fprintf(stderr, "BTS_GPU_AUTHORING_WORKER_PROOF_FAILED\n");
      }
  });
  const auto image = [&](const char *name) {
    Image value(GPU_texture_create_2d(name,
                                      size,
                                      size,
                                      1,
                                      gpu::TextureFormat::SFLOAT_32_32_32_32,
                                      GPU_TEXTURE_USAGE_GENERAL,
                                      nullptr),
                GPU_texture_free);
    require(bool(value), "Authoring oracle allocation failed");
    return value;
  };
  const auto check = [&](gpu::Texture *texture, auto expected, double bound = 2e-5) {
    auto *data = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
    require(data, "Authoring oracle readback failed");
    const auto free = qScopeGuard([&] { MEM_delete(data); });
    const int w = GPU_texture_width(texture), h = GPU_texture_height(texture);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < 4; ++c) {
          const double actual = data[(y * w + x) * 4 + c], reference = expected(x, y, c);
          require(std::isfinite(actual) && std::isfinite(reference),
                  "Nonfinite authoring oracle value");
          const auto error = std::abs(actual - reference);
          maximum = std::max(maximum, error);
          ++components;
          require(error <= bound, "Authoring worker output differs from independent reference");
        }
    ++cases;
  };
  // Existing path/fill/ribbon CPU oracles, exact prepared cache instead of a new
  // synchronously compiled test cache; their existing limits remain unchanged.
  path_cases = verify_gpu_path_raster(&pages);
  cases += path_cases;
  auto target = image("Worker authoring oracle"), before = image("Worker over backdrop"),
       ink = image("Worker over ink");
  const float zero[4] = {};
  // Both preserved raster owners, including the smudge-only contact owner's
  // historically eager stamp/over programs (not asserted as production routes).
  operation = "brush rasters";
  for (auto *raster : {&pages.raster_, &pages.payloads_.raster_}) {
    PaintPixels::Stamp stamp{{.51, .47}, {.23, 0}, {0, .19}};
    stamp.alpha.kind = AlphaKind::Uniform;
    stamp.alpha.uniform = .37;
    stamp.flow = .83;
    stamp.values.base_color = {1.3, .2, .7, 1};
    PaintPixels reference;
    reference.stamp(size, stamp);
    GPU_texture_clear(target.get(), GPU_DATA_FLOAT, zero);
    require(raster->stamp(target.get(), 0, stamp), "Prepared stamp rejected");
    check(target.get(), [&](int x, int y, int c) { return reference.sample(0, x, y)[c]; });
    const std::array<float, 4> base{.2f, .1f, -.05f, .5f}, paint{.4f, .1f, .3f, .8f};
    GPU_texture_clear(before.get(), GPU_DATA_FLOAT, base.data());
    GPU_texture_clear(ink.get(), GPU_DATA_FLOAT, paint.data());
    GPU_texture_clear(target.get(), GPU_DATA_FLOAT, zero);
    require(raster->over(
                target.get(), before.get(), ink.get(), .37f, {0, 0, size, size}, false, false),
            "Prepared over rejected");
    check(target.get(),
          [&](int, int, int c) { return paint[c] * .37 + base[c] * (1 - paint[3] * .37); });
    require(raster->contact(target.get(), stamp, size), "Prepared contact rejected");
    check(target.get(), [&](int x, int y, int c) {
      return c == 0 ? stamp.center[0] + (2 * (x + .5) / size - 1) * stamp.axis_x[0] :
             c == 1 ? stamp.center[1] + (2 * (y + .5) / size - 1) * stamp.axis_y[1] :
             c == 2 ? .37 :
                      1.;
    });
  }
  // Actual polygon graphics coverage and page compute: full-pixel CPU reference,
  // dirty snapshot capture/restore, then exact clear of the selected pages.
  operation = "polygon capture/paint/restore/clear";
  auto &raster = pages.polygons_;
  const std::array<float, 4> background{.2f, .1f, .05f, .5f};
  const std::array<std::uint32_t, 4> keys{0, 1, 65536, 65537};
  PaintPixels::Stamp polygon{};
  auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
  surface->center = {0, 0, 0};
  surface->radius = 1;
  PaintPixels::SurfaceTriangle triangle{};
  triangle.uv = {{{.125, .125}, {.875, .125}, {.125, .875}}};
  surface->triangles.push_back(triangle);
  polygon.surface = surface;
  polygon.alpha.kind = AlphaKind::Uniform;
  polygon.alpha.uniform = 1;
  polygon.values.base_color = {2, .3, -.2, 1};
  PaintPixels polygon_reference;
  polygon_reference.stamp(size, polygon);
  GPU_texture_clear(target.get(), GPU_DATA_FLOAT, background.data());
  auto polygon_snapshot = raster.capture(target.get(), keys);
  raster.paint(target.get(), polygon, 0, size, keys, .37f);
  check(target.get(), [&](int x, int y, int c) {
    const auto value = polygon_reference.sample(0, x, y);
    return value[3] > 0 ? value[c] * .37 + background[c] * (1 - .37) : background[c];
  });
  raster.restore(target.get(), polygon_snapshot);
  check(target.get(), [&](int, int, int c) { return background[c]; }, 0);
  raster.clear(target.get(), keys);
  check(target.get(), [](int, int, int) { return 0.; }, 0);
  // Existing recorded source capture and carry recipes: GPU work first, explicit
  // CPU evaluation only afterwards. No GPU consumer may materialize CPU recipes.
  operation = "smudge gather/sample/contact/carry";
  TilePaintPixels source;
  PaintPixels::Stamp seed{{.57, .48}, {.4, 0}, {0, .37}};
  seed.values.base_color = {1.3, .2, .7, 1};
  seed.flow = .83;
  {
    PaintRecordingScope record;
    source.stamp(size, seed, {1001});
  }
  PaintPixels::Stamp dab{{.51, .49}, {.123, .021}, {-.014, .137}};
  dab.channels = {true, false, false, false, false};
  dab.alpha.kind = AlphaKind::Uniform;
  dab.alpha.uniform = .37;
  std::shared_ptr<SmudgePayload> first;
  {
    PaintRecordingScope record;
    first = capture_brush_payload(source, dab, size);
  }
  const auto reads = local_paint_materializations;
  auto captured = pages.capture(first, 0);
  require(captured && local_paint_materializations == reads,
          "GPU capture materialized CPU authoring pixels");
  const auto &expected = first->evaluated();
  check(captured->channels[0].get(),
        [&](int x, int y, int c) { return expected.channels[0][y * first->side + x][c]; });
  check(captured->contact.get(), [&](int x, int y, int c) {
    const double u = 2 * (x + .5) / first->side - 1, v = 2 * (y + .5) / first->side - 1;
    return c == 0 ? dab.center[0] + u * dab.axis_x[0] + v * dab.axis_y[0] :
           c == 1 ? dab.center[1] + u * dab.axis_x[1] + v * dab.axis_y[1] :
           c == 2 ? double(expected.pickup[y * first->side + x]) :
                    1.;
  });
  dab.center = {.59, .53};
  std::shared_ptr<SmudgePayload> next;
  {
    PaintRecordingScope record;
    auto now = capture_brush_payload(source, dab, size);
    next = std::make_shared<SmudgePayload>();
    next->side = now->side;
    next->recipe = std::make_shared<SmudgeCarry>(now, first, .37f);
  }
  const auto carry_reads = local_paint_materializations;
  auto carried = pages.capture(next, 0);
  require(carried && local_paint_materializations == carry_reads,
          "GPU carry materialized CPU authoring pixels");
  const auto &carry = next->evaluated();
  check(carried->channels[0].get(),
        [&](int x, int y, int c) { return carry.channels[0][y * next->side + x][c]; });
  require(!trace.unknown && !trace.synchronous_compilations && !trace.asynchronous_submissions &&
              !pages.programs_pending(),
          "Authoring suite used an unexpected shader/pending owner or compilation");
  for (int i = 0; i < 14; ++i)
    require(pages.program_output(i).get() == trace.shaders[i] && trace.dispatches[i] > 0,
            "Authoring owner changed or did not dispatch");
  operation = "complete";
  persist(true);
  saved = true;
  std::printf(
      "BTS_GPU_AUTHORING_WORKER_VERIFIED slots=14 cases=%d finite=%llu max_error=%.9g same_owners=1 no_extra_authoring_compilation=1 functional_only=1\n",
      cases,
      (unsigned long long)components,
      maximum);
  return cases;
}
}  // namespace bts
