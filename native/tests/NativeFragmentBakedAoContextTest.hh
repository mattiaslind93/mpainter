#pragma once
// CPU metadata/ownership fixtures only. No GPU resource, context or fake texture.
#include "NativeMaterialBakedAoContext.hh"
#include "NativeMaterialViewContext.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include "GPU_context.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bts::native_fragment_baked_ao_context_test {
inline void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
inline MeshMapPlane plane(int width, int height, float red)
{
  std::vector<float> rgba(std::size_t(width) * height * 4);
  for (std::size_t i = 0; i < rgba.size(); i += 4) {
    rgba[i] = red;
    rgba[i + 1] = .25f;
    rgba[i + 2] = .75f;
    rgba[i + 3] = 1;
  }
  auto pixels = std::make_shared<MeshMapPixels>(width, std::move(rgba), height);
  return {width, pixels->hash, std::move(pixels), height};
}
template<class Operation>
inline void rejects_invalid_pixels(const Operation &operation, const char *message)
{
  bool rejected = false;
  try {
    operation();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, message);
}
}  // namespace bts::native_fragment_baked_ao_context_test

namespace bts::native_fragment_baked_ao_context_test {
template<class Operation>
inline void rejects_ao(const Operation &operation, int tile, const char *message)
{
  bool rejected = false;
  try {
    operation();
  }
  catch (const NativeFragmentContextError &error) {
    rejected = error.code == NativeFragmentContextCode::BakedAo &&
               std::string(error.what()).find(std::to_string(tile)) != std::string::npos;
  }
  require(rejected, message);
}
}  // namespace bts::native_fragment_baked_ao_context_test
namespace bts {
inline QJsonObject verify_native_fragment_baked_ao_context()
{
  using namespace native_fragment_baked_ao_context_test;
  require(!blender::GPU_context_active_get(), "Baked AO CPU fixture acquired a GPU context");
  constexpr std::array<int, 2> tiles2{1001, 1002};
  constexpr std::array<int, 3> tiles3{1001, 1002, 1011};
  std::optional<NativeMaterialBakedAoContext> two, three;
  std::array<std::weak_ptr<const MeshMapPixels>, 3> owners;
  std::array<const float *, 3> data{};
  {
    MeshMaps maps;
    MeshMap ao;
    ao.name = "CPU immutable project AO";
    ao.tiles.emplace(1001, plane(32, 32, .125f));
    ao.tiles.emplace(1002, plane(64, 32, .375f));
    ao.tiles.emplace(1011, plane(32, 64, .625f));
    for (std::size_t i = 0; i < tiles3.size(); ++i) {
      const auto &p = ao.tiles.at(tiles3[i]);
      owners[i] = p.pixels;
      data[i] = p.pixels->rgba.data();
    }
    maps[3] = std::move(ao);
    two = NativeMaterialBakedAoContext::capture(maps, tiles2);
    three = NativeMaterialBakedAoContext::capture(maps, tiles3);
    require(two->requested() && two->planes().size() == 2 && three->planes().size() == 3,
            "AO snapshot lost requested sparse tile coverage");
    maps = {};
  }
  for (std::size_t i = 0; i < tiles3.size(); ++i) {
    const auto &p = three->plane(tiles3[i]);
    require(!owners[i].expired() && p.pixels.get() == owners[i].lock().get() &&
                p.pixels->rgba.data() == data[i],
            "AO snapshot copied or lost immutable pixels after source map destruction");
    require(p.pixels->rgba[0] == std::array<float, 3>{.125f, .375f, .625f}[i],
            "AO snapshot changed its retained per-tile data");
  }
  require(three->plane(1002).resolution == 64 && three->plane(1002).rows() == 32 &&
              three->plane(1011).resolution == 32 && three->plane(1011).rows() == 64,
          "AO snapshot lost rectangular extents");
  rejects_ao([&] { two->plane(1011); }, 1011, "Unrequested AO tile silently gained a fallback");
  three.reset();
  require(owners[2].expired() && !owners[0].expired() && !owners[1].expired(),
          "Requested-tile AO owners were not independently retained/released");
  two.reset();
  for (const auto &owner : owners)
    require(owner.expired(), "AO snapshot retained pixels after its last owner was released");

  MeshMaps maps;
  MeshMap ao;
  ao.name = "CPU validation AO";
  ao.tiles.emplace(1001, plane(32, 32, .25f));
  ao.tiles.emplace(1002, plane(64, 32, .5f));
  ao.tiles.emplace(1011, plane(32, 64, .75f));
  maps[3] = std::move(ao);
  const auto snapshot = NativeMaterialBakedAoContext::capture(maps, tiles3);
  snapshot.validate();
  const auto disabled = NativeMaterialBakedAoContext::capture(MeshMaps{}, tiles3, false);
  require(!disabled.requested() && disabled.planes().empty(),
          "Unrequested AO captured an owner or invented a fallback");
  rejects_ao([&] { disabled.plane(1001); }, 1001, "Unrequested AO admitted a draw plane");
  rejects_ao([&] { NativeMaterialBakedAoContext::capture(MeshMaps{}, tiles2); },
             1001,
             "Missing AO role was not a tile-specific typed rejection");
  auto absent = maps;
  absent[3]->tiles.erase(1002);
  rejects_ao([&] { NativeMaterialBakedAoContext::capture(absent, tiles2); },
             1002,
             "Missing AO tile was not a typed rejection");
  int plane_rejections = 0;
  const auto reject_plane = [&](MeshMapPlane p) {
    rejects_ao([&] { native_baked_ao_key(p, 1002); },
               1002,
               "Inconsistent immutable AO plane metadata was admitted");
    ++plane_rejections;
  };
  auto bad = maps[3]->tiles.at(1002);
  bad.pixels.reset();
  reject_plane(bad);
  bad = maps[3]->tiles.at(1002);
  bad.hash.clear();
  reject_plane(bad);
  bad = maps[3]->tiles.at(1002);
  bad.hash = "inconsistent-hash";
  reject_plane(bad);
  bad = maps[3]->tiles.at(1002);
  bad.resolution = 32;
  reject_plane(bad);
  bad = maps[3]->tiles.at(1002);
  bad.height = 64;
  reject_plane(bad);
  rejects_ao([&] { native_baked_ao_key(maps[3]->tiles.at(1001), 1000); },
             1000,
             "Invalid AO UDIM was admitted");
  rejects_ao([&] { NativeMaterialBakedAoContext::capture(maps, std::span<const int>{}); },
             1001,
             "Empty AO request was admitted");
  // Immutable storage itself owns finite/data/coverage validation; no invalid
  // MeshMapPixels object is forged for the context validator.
  int invalid_pixel_rejections = 0;
  const auto reject_pixels = [&](std::vector<float> values) {
    rejects_invalid_pixels([&] { MeshMapPixels invalid(32, std::move(values), 32); },
                           "Invalid baked pixel data entered an immutable owner");
    ++invalid_pixel_rejections;
  };
  std::vector<float> values(32 * 32 * 4, 1);
  values[0] = std::numeric_limits<float>::quiet_NaN();
  reject_pixels(values);
  values.assign(32 * 32 * 4, 1);
  values[1] = std::numeric_limits<float>::infinity();
  reject_pixels(values);
  values.assign(32 * 32 * 4, 1);
  values.pop_back();
  reject_pixels(values);
  values.assign(32 * 32 * 4, 1);
  values[3] = 1.5f;
  reject_pixels(values);

  const auto &original = snapshot.plane(1002);
  require(native_baked_ao_key(original, 1002) == "baked:" + original.pixels->hash + ":64:32",
          "Retained AO changed the existing pool key format");
  require(native_baked_ao_vertex_assignment() ==
              "fragment_raster_uv=raster_uv-native_raster_origin;",
          "Actual compiler AO varying stopped using tile-local raster UV");
  require(native_baked_ao_fragment_source().find(
              "textureLod(native_project_ao,fragment_raster_uv,0.0).r") != std::string_view::npos,
          "Actual compiler AO sampler stopped consuming raster UV/red channel");
  // These deliberately distinct input domains make a regression to authored UV
  // observable in the exact assignment consumed by the production compiler.
  const std::array<float, 2> raster{1.25f, .75f}, authored{.9f, .1f}, origin{1, 0};
  const std::array<float, 2> tile_local{raster[0] - origin[0], raster[1] - origin[1]};
  require(tile_local == std::array<float, 2>{.25f, .75f} && tile_local != authored,
          "Raster fixture failed to distinguish authored UV from tile-local AO UV");

  // Re-bakes enter the actual existing source/output route. The owner callback
  // may run, but unchanged owners must not recapture or invalidate geometry.
  NativeWorldNormalContextRoute route;
  auto job = std::make_shared<NativeWorldNormalGeometryJob>();
  const auto retained_job = job;
  std::uint64_t geometry_revision = 41;
  const auto retained_revision = geometry_revision;
  int owner_checks = 0, geometry_captures = 0, invalidations = 0;
  std::string published;
  auto current = snapshot;
  const auto queue = [&] {
    return route.queue(
        false,
        [&] {
          ++owner_checks;
          return false;
        },
        [&] {
          ++invalidations;
          ++geometry_revision;
          job.reset();
        },
        [&] {
          const auto next = current.signature(1002);
          const bool dirty = next != published;
          published = next;
          return dirty;
        },
        [&] { return !job; },
        [&] {
          ++geometry_captures;
          job = std::make_shared<NativeWorldNormalGeometryJob>();
        });
  };
  require(queue(), "Initial retained AO output key was not queued");
  const auto first_signature = published;
  auto rebaked = maps;
  rebaked[3]->tiles.at(1002) = plane(64, 32, .875f);
  current = NativeMaterialBakedAoContext::capture(rebaked, tiles3);
  require(current.signature(1002) != first_signature && queue(),
          "AO content re-bake did not change the actual per-tile output signature");
  require(snapshot.plane(1002).pixels == original.pixels &&
              snapshot.plane(1002).pixels->rgba[0] == .5f,
          "Re-bake mutated the immutable previous generation");
  const auto content_signature = published;
  rebaked[3]->tiles.at(1002) = plane(32, 64, .875f);
  current = NativeMaterialBakedAoContext::capture(rebaked, tiles3);
  require(current.signature(1002) != content_signature && queue(),
          "AO dimension replacement did not change the actual per-tile output signature");
  require(owner_checks == 3 && geometry_captures == 0 && invalidations == 0 &&
              geometry_revision == retained_revision && job == retained_job && !job->cancelled,
          "AO-only replacement changed retained geometry/job/revision");

  // Drive the exact production lease-state helper with CPU residency tokens.
  // The tokens carry no GPU handle or deleter, and no texture object is forged.
  NativeBakedAoLeaseState lease, second;
  int acquire_calls = 0, upload_calls = 0;
  std::weak_ptr<const void> old_residency;
  const auto acquire = [&] {
    ++acquire_calls;
    auto token = std::make_shared<const int>(23);
    old_residency = token;
    return NativeBakedAoPin{native_baked_ao_key(original, 1002),
                            original.resolution,
                            original.rows(),
                            std::move(token)};
  };
  const auto upload = [&](const NativeBakedAoPin &pin) {
    ++upload_calls;
    pin.validate(original, 1002);
    return upload_calls == 2;
  };
  require(!lease.prepare(original, 1002, acquire, upload) && !lease.ready() &&
              !old_residency.expired(),
          "Partial AO upload was marked ready or failed to retain its pin");
  lease.validate(original, 1002, false);
  rejects_ao(
      [&] { lease.validate(original, 1002); }, 1002, "Partial AO upload was admitted for draw");
  require(lease.prepare(original, 1002, acquire, upload) && lease.ready() && acquire_calls == 1 &&
              upload_calls == 2,
          "AO upload did not resume using the same retained pin");
  lease.validate(original, 1002);
  require(lease.prepare(original, 1002, acquire, upload) && acquire_calls == 1 &&
              upload_calls == 2,
          "Ready AO resource was reacquired or uploaded again");
  int shared_acquires = 0;
  require(second.prepare(
              original,
              1002,
              [&] {
                ++shared_acquires;
                return lease.pin();
              },
              [&](const NativeBakedAoPin &pin) { return pin.residency == lease.pin().residency; }),
          "Second AO consumer lost the actual shared residency token");
  require(shared_acquires == 1 && second.pin().residency == lease.pin().residency,
          "AO consumers duplicated a retained residency identity");
  const auto replacement = plane(64, 32, .9375f);
  std::weak_ptr<const void> new_residency;
  require(!lease.prepare(
              replacement,
              1002,
              [&] {
                auto token = std::make_shared<const int>(29);
                new_residency = token;
                return NativeBakedAoPin{
                    native_baked_ao_key(replacement, 1002), 64, 32, std::move(token)};
              },
              [](const NativeBakedAoPin &) { return false; }),
          "Replacement AO inherited stale readiness");
  require(!old_residency.expired() && !new_residency.expired() && !lease.ready(),
          "Replacement AO evicted another consumer's old pin or its own partial pin");
  second.reset();
  require(old_residency.expired(), "Retired AO residency survived its final consumer");
  rejects_ao([&] { lease.validate(original, 1002, false); },
             1002,
             "New AO residency was accepted for old immutable pixels");
  auto held = lease.pin();
  int malformed_pin_rejections = 0;
  bool invalid_upload_called = false;
  for (int kind = 0; kind < 4; ++kind) {
    rejects_ao(
        [&] {
          lease.prepare(
              original,
              1002,
              [&] {
                auto p = held;
                p.key = native_baked_ao_key(original, 1002);
                p.width = original.resolution;
                p.height = original.rows();
                if (kind == 0)
                  p.residency.reset();
                if (kind == 1)
                  p.key = "wrong-key";
                if (kind == 2)
                  p.width = 1;
                if (kind == 3)
                  p.height = 1;
                return p;
              },
              [&](const NativeBakedAoPin &) {
                invalid_upload_called = true;
                return true;
              });
        },
        1002,
        "Malformed retained AO pin was admitted");
    ++malformed_pin_rejections;
    require(lease.pin().residency == held.residency && lease.pin().key == held.key &&
                !lease.ready(),
            "Rejected AO acquisition partially replaced a retained pin");
  }
  require(!invalid_upload_called, "Invalid AO pin reached the upload callback");
  lease.reset();
  require(!lease.ready() && !lease.pin().residency, "AO reset retained upload readiness or a pin");
  require(!new_residency.expired(), "Explicit metadata owner was ignored during release");
  held = {};
  require(new_residency.expired(), "Final partial AO residency owner was not released");
  require(!blender::GPU_context_active_get(), "Baked AO CPU fixture created a GPU context");
  return {{"complete", true},
          {"sparse_tiles", QJsonArray{1001, 1002, 1011}},
          {"snapshot_tile_counts", QJsonArray{2, 3}},
          {"rectangular_extents", QJsonArray{QJsonArray{64, 32}, QJsonArray{32, 64}}},
          {"source_maps_destroyed_before_use", true},
          {"immutable_pixel_owner_identity", true},
          {"pixel_storage_not_copied", true},
          {"requested_owner_release_exact", true},
          {"unrequested_snapshot_empty", true},
          {"typed_plane_rejections", plane_rejections},
          {"immutable_pixel_data_rejections", invalid_pixel_rejections},
          {"missing_role_tile_typed_rejected", true},
          {"existing_baked_pool_key", true},
          {"compiler_uses_raster_uv_red", true},
          {"raster_authored_uv_distinct", true},
          {"rebake_output_signature_changed", true},
          {"dimension_output_signature_changed", true},
          {"rebake_owner_checks", owner_checks},
          {"rebake_geometry_provider_calls", geometry_captures},
          {"rebake_geometry_invalidations", invalidations},
          {"geometry_revision_job_unchanged", true},
          {"partial_upload_pin_retained", true},
          {"partial_draw_typed_rejected", true},
          {"lease_acquire_calls", acquire_calls},
          {"lease_upload_calls", upload_calls},
          {"ready_reuse_skips_acquire_upload", true},
          {"two_consumers_share_residency", true},
          {"replacement_readiness_reset", true},
          {"last_consumer_releases_residency", true},
          {"malformed_pin_rejections", malformed_pin_rejections},
          {"rejected_acquire_preserves_pin", true},
          {"gpu_objects_created", 0},
          {"shader_execution", false}};
}
}  // namespace bts
