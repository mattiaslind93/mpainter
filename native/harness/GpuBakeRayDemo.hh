#pragma once
#include "BlenderHost.hh"
#include "GpuBakeRays.hh"
#include "../tests/GpuBakePhaseOneTest.hh"
#include "BakeCacheProof.hh"
#include "BakeJobGpuProof.hh"
#include "../tests/gpu_bake_ray_reference.hh"
#include "../tests/NativeBakeSamplesValidation.hh"
#include "ResourceIO.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"
#include "BKE_idprop.hh"
#include "DNA_scene_types.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <bit>
namespace bts {
inline QJsonObject verify_full_resolution_bake_rays()
{
  QJsonArray proofs;
  const std::vector<bake_bvh::Triangle> triangles{{{{-1, -1, 1}, {1, -1, 1}, {0, 1, 1}}}};
  for (int size : {4096, 8192}) {
    ScratchVector<FillSurfacePoint> points(std::size_t(size) * size);
    for (std::size_t i = 0; i < points.size(); ++i) {
      auto &p = points[i];
      p.valid = true;
      p.position = {float(i % size) / size - .5f, float(i / size) / size - .5f, 0};
    }
    ResourceAvailability full;
    full.vram_known = true;
    full.available_vram = texture_free_vram.load();
    if (!full.available_vram)
      full.vram_known = false;
    ResourceAvailability limited = full;
    limited.vram_known = true;
    limited.available_vram = 16ULL * 1024 * 1024;
    GpuBakeRayStats reference_stats, tiled_stats;
    const auto reference = gpu_bake_cosine_occlusion(
        triangles, points, 1, 4, false, &reference_stats, &full);
    const auto tiled = gpu_bake_cosine_occlusion(
        triangles, points, 1, 4, false, &tiled_stats, &limited);
    if (tiled_stats.batches <= reference_stats.batches || reference.size() != tiled.size())
      throw std::runtime_error("Constrained VRAM did not partition full-resolution rays");
    for (std::size_t i = 0; i < reference.size(); ++i)
      if (std::bit_cast<std::uint32_t>(reference[i]) != std::bit_cast<std::uint32_t>(tiled[i]))
        throw std::runtime_error("Tiled bake changed a full-resolution sample");
    const auto extrema = std::minmax_element(tiled.begin(), tiled.end());
    if (*extrema.first == *extrema.second)
      throw std::runtime_error(
          "Full-resolution fixture must include both occluded and clear rays");
    double error = 0;
    for (std::size_t i = 0; i < points.size(); i += 65537) {
      const auto expected = bake_cosine_occlusion(
          triangles, points[i].position, points[i].normal, 1, 4, std::uint32_t(i));
      error = std::max(error, std::abs(double(tiled[i]) - expected));
    }
    if (error > 2e-6)
      throw std::runtime_error("Full-resolution GPU rays differ from CPU oracle");
    proofs.append(QJsonObject{{"resolution", size},
                              {"pixels", qint64(points.size())},
                              {"reference_batches", qint64(reference_stats.batches)},
                              {"constrained_batches", qint64(tiled_stats.batches)},
                              {"simulated_available_vram", qint64(limited.available_vram)},
                              {"bit_exact", true},
                              {"cpu_max_error", error}});
  }
  return {{"maps", proofs}, {"buffers_released_between_maps", true}};
}
inline QJsonObject verify_gpu_bake_bvh()
{
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  std::vector<bake_bvh::Triangle> triangles;
  for (int y = 0; y < 20; ++y)
    for (int x = 0; x < 20; ++x) {
      const float a = x - 10, b = y - 10;
      triangles.push_back({{{a, b, 1}, {a + 1, b, 1}, {a + 1, b + 1, 1}}});
      triangles.push_back({{{a, b, 1}, {a + 1, b + 1, 1}, {a, b + 1, 1}}});
    }
  std::vector<FillSurfacePoint> small(31);
  for (std::size_t i = 0; i < small.size(); ++i) {
    auto &p = small[i];
    p.valid = i != 3;
    p.position = {float(int(i % 5) - 2) * .1f, float(int(i % 7) - 3) * .1f, 0};
    p.normal = {i % 2 ? .2f : 0.f, i % 3 ? .1f : 0.f, 1};
  }
  small[5].normal = {0, 0, 0};
  int exact = 0;
  for (bool inward : {false, true}) {
    const auto reference = legacy_gpu_bake_cosine_occlusion(triangles, small, 16, 4, inward),
               actual = gpu_bake_cosine_occlusion(triangles, small, 16, 4, inward);
    require(reference.size() == actual.size(), "GPU BVH changed output size");
    for (std::size_t i = 0; i < actual.size(); ++i) {
      require(std::bit_cast<std::uint32_t>(actual[i]) ==
                  std::bit_cast<std::uint32_t>(reference[i]),
              "GPU BVH changed the original sample/intersection/accumulation result");
      ++exact;
    }
  }
  std::vector<FillSurfacePoint> points(2049);
  for (std::size_t i = 0; i < points.size(); ++i) {
    points[i].valid = true;
    points[i].normal = {i % 2 ? .2f : -.15f, i % 3 ? .1f : 0.f, 1};
    points[i].position = {float(int(i % 31) - 15) * .01f, float(int(i % 29) - 14) * .01f, 0};
  }
  const auto old_work = resource_product(resource_product(triangles.size(), points.size()), 8);
  require(old_work > 8000000, "GPU fixture did not exceed the former total-work cap");
  bool old_rejected = false;
  try {
    legacy_gpu_bake_cosine_occlusion(triangles, points, 8, 4);
  }
  catch (const std::length_error &) {
    old_rejected = true;
  }
  require(old_rejected, "Legacy GPU fixture did not reproduce the old rejection");
  ResourceAvailability limited;
  limited.vram_known = true;
  limited.available_vram = 512 * 1024;
  GpuBakeRayStats stats;
  QElapsedTimer timer;
  timer.start();
  const auto actual = gpu_bake_cosine_occlusion(triangles, points, 8, 4, false, &stats, &limited);
  const auto gpu_ms = timer.elapsed();
  require(stats.batches > 1, "GPU fixture did not exercise global sampling seeds across batches");
  double maximum_error = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto expected = bake_cosine_occlusion(
        triangles, points[i].position, points[i].normal, 8, 4, std::uint32_t(i));
    maximum_error = std::max(maximum_error, std::abs(double(actual[i]) - expected));
  }
  require(maximum_error < 2e-6, "GPU BVH differs from independent CPU triangle ray reference");
  std::vector<bake_bvh::Triangle> overlap(
      2048, bake_bvh::Triangle{{{-20, -20, 1}, {20, -20, 1}, {0, 20, 1}}});
  std::vector<FillSurfacePoint> one(1);
  one[0].valid = true;
  GpuBakeRayStats resumed;
  const auto continued = gpu_bake_cosine_occlusion(overlap, one, 1, 4, false, &resumed),
             brute = legacy_gpu_bake_cosine_occlusion(overlap, one, 1, 4);
  require(resumed.dispatches > 1 &&
              std::bit_cast<std::uint32_t>(continued[0]) == std::bit_cast<std::uint32_t>(brute[0]),
          "Resumed overlapping BVH traversal changed the original result");
  bool low_ram = false;
  {
    TextureRamQueryScope query(+[]() -> std::uint64_t { return 1024; });
    try {
      gpu_bake_cosine_occlusion(triangles, points, 8, 4);
    }
    catch (const std::length_error &) {
      low_ram = true;
    }
  }
  require(low_ram, "GPU bake skipped actual RAM admission");
  return {{"triangles", int(triangles.size())},
          {"points", int(points.size())},
          {"rays", 8},
          {"former_triangle_ray_tests", qint64(old_work)},
          {"legacy_reject_reproduced", old_rejected},
          {"original_gpu_reference_bit_exact_samples", exact},
          {"cpu_max_error", maximum_error},
          {"gpu_ms", gpu_ms},
          {"batches", qint64(stats.batches)},
          {"peak_batch_rays", qint64(stats.peak_batch_rays)},
          {"overlap_continuation_dispatches", qint64(resumed.dispatches)},
          {"continuation_bit_exact", true},
          {"real_low_ram_rejected", low_ram},
          {"no_global_work_cap", true}};
}
inline QJsonObject verify_resource_manifest_bounds()
{
  using namespace blender;
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
  auto *scene = BKE_scene_add(main.get(), "BC manifest fixture");
  std::vector<StudioAsset> assets;
  assets.reserve(4097);
  for (int i = 0; i < 4097; ++i) {
    StudioAsset asset;
    asset.scope = AssetScope::Project;
    asset.name = "Manifest resource " + QString::number(i);
    asset.filename = "source.bin";
    asset.usages = {"texture"};
    asset.bytes = "x";
    assets.push_back(std::move(asset));
  }
  pack_project_assets(*main, *scene, assets);
  auto loaded = read_project_assets(*scene);
  require(loaded.size() == 4097, "Project resources were limited to4096");
  loaded.clear();
  auto *root = IDP_EnsureProperties(&scene->id);
  {
    TextureRamQueryScope query(+[]() -> std::uint64_t { return 1024; });
    pack_project_assets(*main, *scene, assets);
    require(read_project_assets(*scene).size() == 4097,
            "RAM telemetry blocked manifest roundtrip");
  }
  QByteArray json(R"({"version":2,"resources":[]})");
  json.append(QByteArray(64 * 1024 * 1024, ' '));
  IDP_ReplaceInGroup(root, IDP_NewString(json.constData(), "bts_resources"));
  require(read_project_assets(*scene).empty(), "Manifest larger than64MiB was rejected");
  {
    TextureRamQueryScope query(+[]() -> std::uint64_t { return 1024; });
    require(read_project_assets(*scene).empty(), "RAM telemetry blocked large manifest decode");
  }
  return {{"resources_roundtripped", 4097},
          {"encoded_manifest_bytes", qint64(json.size())},
          {"low_ram_telemetry_does_not_block", true},
          {"idproperty_signed_int_length_preserved", true}};
}
inline int run_gpu_bake_ray_demo(BlenderHost &host, const QString &directory)
{
  if (qEnvironmentVariableIsSet("BTS_GPU_PHASE1_TEST"))
    return gpu_phase_one::run(host, directory);
  if (qEnvironmentVariableIsSet("BTS_BAKE_JOB_GPU_TEST")) {
    auto proof = verify_production_bake_gpu(host);
    proof["renderer"] = QString::fromStdString(host.renderer());
    proof["visible_windows"] = 0;
    QFile output(directory + "/production-bake-gpu.json");
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot write production bake proof");
    output.write(QJsonDocument(proof).toJson());
    return 0;
  }
  if (qEnvironmentVariableIsSet("BTS_BAKE_CACHE_TEST")) {
    QJsonObject proof;
    host.with_gpu([&] { proof = verify_bake_cache_release(); });
    proof["renderer"] = QString::fromStdString(host.renderer());
    proof["visible_windows"] = 0;
    QFile output(directory + "/bake-cache-release.json");
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot write cache release proof");
    output.write(QJsonDocument(proof).toJson());
    return 0;
  }
  if (qEnvironmentVariableIsSet("BTS_FULL_RES_BAKE_TEST")) {
    QJsonObject proof;
    host.with_gpu([&] { proof = verify_full_resolution_bake_rays(); });
    proof["renderer"] = QString::fromStdString(host.renderer());
    proof["visible_windows"] = 0;
    QFile output(directory + "/full-resolution-bake-rays.json");
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot write full-resolution ray proof");
    output.write(QJsonDocument(proof).toJson());
    return 0;
  }
  if (qEnvironmentVariableIsSet("BTS_BL_BAKE_SAMPLES_TEST"))
    return run_bl_bake_samples_validation(host, directory);
  QJsonObject ray;
  host.with_gpu([&] { ray = verify_gpu_bake_bvh(); });
  const auto manifest = verify_resource_manifest_bounds();
  QJsonObject proof{{"renderer", QString::fromStdString(host.renderer())},
                    {"visible_windows", 0},
                    {"gpu_bake", ray},
                    {"resource_manifest", manifest}};
  QFile file(directory + "/bc-gpu-bake-bvh.json");
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot write GPU bake BVH proof");
  file.write(QJsonDocument(proof).toJson());
  std::printf(
      "BTS_GPU_BAKE_BVH_PASS work=13113600 original_reference_bit_exact=62 resources=4097 renderer=%s\n",
      host.renderer().c_str());
  return 0;
}
}  // namespace bts
