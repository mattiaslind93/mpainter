// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// CPU tests of the exact planner/transaction/cache used by the native uploader.
// OwnerModel is ordinary metadata, never a fabricated GPU handle or texture.
#include "NativeMaterialTiledSourceTest.hh"
#include "NativeMaterialTiledStorage.hh"
#include "NativeMaterialSampler.hh"
#include "NativeFragmentImageReadiness.hh"
#include "BLI_boxpack_2d.h"
#include "BLI_listbase.h"
#include <functional>
#include <limits>
#include <optional>
#include <tuple>

namespace bts::native_tiled_storage_test {
using native_tiled_source_test::require;
using native_tiled_source_test::work;
using native_tiled_source_test::numbers;
using Dimensions = std::array<std::array<int, 2>, 4>;
inline constexpr Dimensions mixed{{{32, 16}, {16, 32}, {48, 32}, {32, 48}}};
inline constexpr NativeTiledCaps caps{4096, 256, 16384};
inline void write(const QString &directory, const Dimensions &dimensions, bool half_last = false)
{
  using namespace blender;
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    const auto [w, h] = dimensions[i];
    auto *buffer = IMB_allocImBuf(w, h, ImBufFlags::FloatData);
    require(buffer, "Cannot allocate owned storage fixture pixels");
    const auto release = qScopeGuard([&] { IMB_freeImBuf(buffer); });
    buffer->channels = 4;
    buffer->color_mode = ImColorMode::RGBA;
    buffer->ftype = IMB_FTYPE_OPENEXR;
    buffer->flags |= ImBufFlags::AlphaChannelPacked;
    if (half_last && i == numbers.size() - 1)
      buffer->foptions.flag |= OPENEXR_HALF;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const auto value = native_tiled_source_test::pixel(int(i), x, y);
        std::copy(value.begin(), value.end(), buffer->float_data_for_write() + 4 * (y * w + x));
      }
    require(
        IMB_save_image(buffer,
                       native_tiled_source_test::path(directory, numbers[i]).toUtf8().constData(),
                       ImBufFlags::FloatData),
        "Cannot encode storage fixture pixels");
  }
}
inline void order(blender::Image &image, const std::array<int, 4> &numbers)
{
  std::map<int, blender::ImageTile *> tiles;
  for (auto &tile : image.tiles)
    tiles.emplace(tile.tile_number, &tile);
  for (const int number : numbers) {
    auto *tile = tiles.at(number);
    blender::BLI_remlink(&image.tiles, tile);
    blender::BLI_addtail(&image.tiles, tile);
  }
}
using RegionTuple = std::tuple<int, int, int, int, int>;
inline std::map<int, RegionTuple> tuples(const NativeTiledStoragePlan &plan)
{
  std::map<int, RegionTuple> result;
  for (const auto &r : plan.regions)
    require(result.emplace(r.tile, RegionTuple{r.x, r.y, r.width, r.height, r.layer}).second,
            "Planner emitted duplicate tile regions");
  return result;
}
struct StockReference {
  int width = 0, height = 0, layers = 0;
  std::map<int, RegionTuple> regions;
  std::vector<std::array<float, 4>> mapping;
};
inline StockReference stock_reference(const blender::Image &image,
                                      const NativeMaterialImageTileSet &source)
{
  using namespace blender;
  // Independent reference of image_gpu.cc's CPU metadata block. Invoke the
  // actual stock list sorter and fixed-area packer, not a replacement packer.
  struct Node {
    FixedSizeBoxPack box{};
    int tile = 0;
    float score = 0;
  };
  std::vector<Node> nodes;
  nodes.reserve(source.tiles().size());
  ListBaseT<FixedSizeBoxPack> boxes{};
  StockReference result;
  for (const auto &tile : image.tiles) {
    const auto *buffer = source.tiles().at(tile.tile_number)->buffer();
    Node node;
    node.tile = tile.tile_number;
    node.box.w = buffer->x;
    node.box.h = buffer->y;
    const float w = node.box.w, h = node.box.h;
    node.score = std::max(w, h) / std::min(w, h) * w * h;
    nodes.push_back(node);
    BLI_addtail(&boxes, &nodes.back().box);
    result.width = std::max(result.width, buffer->x);
    result.height = std::max(result.height, buffer->y);
  }
  BLI_listbase_sort(&boxes, [](const void *a, const void *b) {
    return int(reinterpret_cast<const Node *>(a)->score <
               reinterpret_cast<const Node *>(b)->score);
  });
  while (!boxes.is_empty()) {
    ListBaseT<FixedSizeBoxPack> packed{};
    BLI_box_pack_2d_fixedarea(&boxes, result.width, result.height, &packed);
    require(!packed.is_empty(), "Stock reference packing made no progress");
    for (const auto &box : packed) {
      const auto &node = *reinterpret_cast<const Node *>(&box);
      result.regions.emplace(node.tile, RegionTuple{box.x, box.y, box.w, box.h, result.layers});
    }
    ++result.layers;
  }
  const int width = source.tiles().rbegin()->first - 1000;
  result.mapping.resize(std::size_t(width) * 2);
  for (int i = 0; i < width; ++i)
    result.mapping[i][0] = -1;
  for (const auto &[tile, r] : result.regions) {
    const auto [x, y, w, h, layer] = r;
    const int index = tile - 1001;
    result.mapping[index][0] = float(layer);
    result.mapping[width + index] = {float(x) / result.width,
                                     float(y) / result.height,
                                     float(w) / result.width,
                                     float(h) / result.height};
  }
  return result;
}
inline bool rejects(const std::function<void()> &function)
{
  try {
    function();
  }
  catch (const std::exception &) {
    return true;
  }
  return false;
}
inline void compare_stock(const NativeTiledStoragePlan &plan, const StockReference &stock)
{
  require(plan.width == stock.width && plan.height == stock.height &&
              plan.layers == stock.layers && tuples(plan) == stock.regions &&
              plan.mapping == stock.mapping,
          "Native plan differs from stock CPU packing/descriptor layout");
  require(plan.format == NativeTiledFormat::Float32Rgba &&
              plan.texture_bytes == native_tiled_mip_texels(plan.width, plan.height) *
                                        std::uint64_t(plan.layers) * 16,
          "Stock layout unexpectedly changed source format or exact mip storage");
  require(plan.map_width == 1000 && plan.mapping_bytes == 32000 && plan.mapping.size() == 2000,
          "Sparse map width/two-row byte layout changed");
  for (int hole : {2, 3, 9, 11, 998})
    require(plan.mapping[hole][0] == -1, "Sparse hole lost stock negative-layer sentinel");
}
struct OwnerModel {
  bool array = false, map = false, mips = false;
  QString key;
  std::map<int, std::vector<int>> visits;
  std::vector<int> map_visits;
};
using Owner = std::shared_ptr<OwnerModel>;
struct Transaction {
  const NativeTiledStoragePlan &plan;
  const NativeMaterialImageTileSet &source;
  Owner published = std::make_shared<OwnerModel>();
  std::weak_ptr<OwnerModel> original = published, created;
  std::optional<NativeTiledPhase> fail;
  bool generation_matches = true;
  int publishes = 0, pixel_actions = 0, map_actions = 0, mips_actions = 0, multirow_actions = 0,
      split_x_actions = 0;
  std::size_t budget = 80;
  explicit Transaction(const NativeTiledStoragePlan &p, const NativeMaterialImageTileSet &s)
      : plan(p), source(s)
  {
  }
  void operator()(const NativeTiledUploadAction &a, Owner &pending)
  {
    if (fail && a.phase == *fail)
      throw std::runtime_error("CPU metadata action failure");
    if (a.phase == NativeTiledPhase::Allocate) {
      require(!pending, "Transaction reused an allocation under construction");
      pending = std::make_shared<OwnerModel>();
      created = pending;
      pending->array = true;
      pending->key = plan.key;
      for (const auto &r : plan.regions)
        pending->visits[r.tile].resize(std::size_t(r.width) * r.height);
      pending->map_visits.resize(plan.mapping.size());
      return;
    }
    require(pending && pending->key == plan.key,
            "Transaction lost its private owner/layout identity");
    if (a.phase == NativeTiledPhase::Pixels) {
      require(a.width > 0 && a.height > 0 &&
                  a.transfer_bytes == std::uint64_t(a.width) * a.height * 16 &&
                  a.transfer_bytes <= budget,
              "Source packet exceeds its byte budget");
      const auto view = native_tiled_packet_view(source, a);
      const auto *buffer = source.tiles().at(a.tile)->buffer();
      require(view.pixels == buffer->float_data() + 4 * (a.source_y * buffer->x + a.source_x) &&
                  view.source_row_length == buffer->x && view.width == a.width &&
                  view.height == a.height,
              "Production packet view changed source pointer/row stride");
      const auto found = std::find_if(plan.regions.begin(),
                                      plan.regions.end(),
                                      [&](const auto &r) { return r.tile == a.tile; });
      require(found != plan.regions.end() && a.target_x == found->x + a.source_x &&
                  a.target_y == found->y + a.source_y && a.layer == found->layer,
              "Packet destination no longer matches its stock region");
      for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
          const auto index = (a.source_y + y) * buffer->x + a.source_x + x;
          require(++pending->visits.at(a.tile).at(std::size_t(index)) == 1,
                  "Source packet overlapped an earlier pixel");
          for (int c = 0; c < 4; ++c)
            require(view.pixels[4 * (y * view.source_row_length + x) + c] ==
                        buffer->float_data()[4 * index + c],
                    "Source-strided packet changed a component");
        }
      ++pixel_actions;
      if (a.height > 1)
        ++multirow_actions;
      if (a.source_x > 0)
        ++split_x_actions;
    }
    else if (a.phase == NativeTiledPhase::Mips) {
      for (const auto &[tile, visits] : pending->visits)
        require(std::all_of(visits.begin(), visits.end(), [](int n) { return n == 1; }),
                "Mip phase began before all tile pixels completed");
      pending->mips = true;
      ++mips_actions;
    }
    else if (a.phase == NativeTiledPhase::AllocateMapping) {
      require(pending->mips && !pending->map,
              "Mapping allocation repeated or preceded source mips");
      pending->map = true;
    }
    else if (a.phase == NativeTiledPhase::Mapping) {
      require(pending->map && a.source_y >= 0 && a.source_y < 2 && a.width > 0 && a.height == 1 &&
                  a.transfer_bytes <= budget,
              "Invalid bounded mapping packet");
      for (int x = 0; x < a.width; ++x)
        require(++pending->map_visits.at(std::size_t(a.source_y) * plan.map_width + a.source_x +
                                         x) == 1,
                "Mapping upload overlapped descriptors");
      ++map_actions;
    }
    else if (a.phase == NativeTiledPhase::Publish) {
      require(generation_matches, "Source/cache generation changed before commit");
      require(pending->array && pending->mips &&
                  (plan.representation == NativeTiledRepresentation::FirstTile2D ||
                   (pending->map && std::all_of(pending->map_visits.begin(),
                                                pending->map_visits.end(),
                                                [](int n) { return n == 1; }))),
              "Incomplete array/map metadata was published");
      require(publishes == 0 && published == original.lock(),
              "Previous published owner changed before one atomic commit");
      published = pending;
      ++publishes;
      return;
    }
    require(publishes == 0 && published == original.lock(),
            "Partial action changed the published owner");
  }
};
inline void complete(NativeTiledUploadState<Owner> &state, Transaction &transaction)
{
  int steps = 0;
  while (!state.step(transaction.budget, std::ref(transaction)))
    require(++steps < 10000, "Tiled transaction failed to advance");
  require(transaction.publishes == 1 && state.phase() == NativeTiledPhase::Done &&
              !state.pending() && transaction.original.expired(),
          "Complete commit did not replace/release the previous metadata owner");
  const auto callbacks = transaction.pixel_actions + transaction.map_actions +
                         transaction.mips_actions;
  require(state.step(transaction.budget, std::ref(transaction)) &&
              callbacks ==
                  transaction.pixel_actions + transaction.map_actions + transaction.mips_actions,
          "Completed state repeated an upload action");
}
inline QJsonObject transactions(const NativeTiledStoragePlan &plan,
                                const NativeMaterialImageTileSet &source)
{
  const std::array phases{NativeTiledPhase::Allocate,
                          NativeTiledPhase::Pixels,
                          NativeTiledPhase::Mips,
                          NativeTiledPhase::AllocateMapping,
                          NativeTiledPhase::Mapping,
                          NativeTiledPhase::Publish};
  int failures = 0, cancels = 0;
  for (const auto phase : phases) {
    NativeTiledUploadState<Owner> state(plan);
    Transaction transaction(plan, source);
    transaction.fail = phase;
    require(rejects([&] {
              int steps = 0;
              while (!state.step(transaction.budget, std::ref(transaction)))
                require(++steps < 10000, "Failed action was never reached");
            }),
            "Injected metadata action failure was ignored");
    require(state.phase() == NativeTiledPhase::Failed && !state.pending() &&
                transaction.publishes == 0 && !transaction.original.expired() &&
                transaction.created.expired(),
            "Failure leaked private owner or replaced previous pair");
    ++failures;
  }
  for (const auto phase : phases) {
    NativeTiledUploadState<Owner> state(plan);
    Transaction transaction(plan, source);
    int steps = 0;
    while (state.phase() != phase) {
      state.step(transaction.budget, std::ref(transaction));
      require(++steps < 10000, "Cancellation phase was never reached");
    }
    state.cancel();
    require(state.phase() == NativeTiledPhase::Failed && !state.pending() &&
                transaction.publishes == 0 && !transaction.original.expired() &&
                transaction.created.expired() &&
                rejects([&] { state.step(transaction.budget, std::ref(transaction)); }),
            "Cancellation retained partial allocation or changed published pair");
    ++cancels;
  }
  NativeTiledUploadState<Owner> stale(plan);
  Transaction changed(plan, source);
  changed.generation_matches = false;
  require(rejects([&] {
            while (!stale.step(changed.budget, std::ref(changed))) {
            };
          }) &&
              changed.publishes == 0 && stale.phase() == NativeTiledPhase::Failed &&
              !stale.pending() && !changed.original.expired(),
          "Changed generation committed a stale pair");
  NativeTiledUploadState<Owner> done(plan);
  Transaction transaction(plan, source);
  complete(done, transaction);
  const auto expected = std::uint64_t(plan.mapping.size()) * 16 + [&] {
    std::uint64_t sum = 0;
    for (const auto &r : plan.regions)
      sum += std::uint64_t(r.width) * r.height * 16;
    return sum;
  }();
  require(done.transferred_bytes() == expected && transaction.mips_actions == 1 &&
              transaction.pixel_actions > 4 && transaction.map_actions > 2 &&
              transaction.split_x_actions > 0,
          "Bounded upload coverage/bytes/mip phase differs");
  NativeTiledUploadState<Owner> rows(plan);
  Transaction multirow(plan, source);
  multirow.budget = 4096;
  complete(rows, multirow);
  require(rows.transferred_bytes() == expected && multirow.multirow_actions > 0,
          "Source-strided multirow packet path was not exercised");
  return {{"failure_phases", failures},
          {"cancel_phases", cancels},
          {"generation_change_rejected_before_commit", true},
          {"partial_publish_count", 0},
          {"complete_publish_count", 1},
          {"source_strided_X_Y_packets_exact", true},
          {"pixel_and_mapping_coverage_once", true},
          {"transfer_bytes", qint64(expected)},
          {"packet_budget_bytes", qint64(transaction.budget)},
          {"multirow_packet_budget_bytes", qint64(multirow.budget)},
          {"actual_multirow_packets_exercised", true},
          {"actual_X_split_packets_exercised", true},
          {"whole_mip_actions", transaction.mips_actions},
          {"previous_pair_retained_until_commit", true},
          {"private_owners_released_after_failure_cancel", true},
          {"completed_state_deduplicated", true}};
}
inline QJsonObject cache(const NativeTiledStoragePlan &array,
                         const NativeTiledStoragePlan &first,
                         const NativeMaterialImageTileSet &source)
{
  NativeTiledOwnerCache<std::weak_ptr<OwnerModel>> cache;
  const auto valid = [&](const auto &entry) {
    const auto owner = entry.lock();
    return owner && owner->array && owner->mips && (owner->key == first.key || owner->map);
  };
  const auto same = [](const auto &a, const auto &b) {
    return !a.owner_before(b) && !b.owner_before(a);
  };
  int combinations = 0;
  for (bool reverse : {false, true}) {
    Owner a, b;
    for (const auto *plan : reverse ? std::array{&first, &array} : std::array{&array, &first}) {
      NativeTiledUploadState<Owner> state(*plan);
      Transaction transaction(*plan, source);
      complete(state, transaction);
      auto &held = plan == &array ? a : b;
      held = transaction.published;
      cache.remember(plan->key, std::weak_ptr<OwnerModel>(held), same);
      const auto *found = cache.find(plan->key, valid);
      require(found && found->lock() == held,
              "Complete layout cache failed exact metadata-token reuse");
      cache.remember(plan->key, std::weak_ptr<OwnerModel>(held), same);
    }
    require(a && b && a != b && cache.find(array.key, valid) && cache.find(first.key, valid),
            "Array+mapping and first-tile 2D failed to coexist");
    a->map = false;
    require(!cache.find(array.key, valid) && cache.find(first.key, valid),
            "Lost map invalidated the unrelated 2D representation");
    NativeTiledUploadState<Owner> recovery(array);
    Transaction replacement(array, source);
    complete(recovery, replacement);
    a = replacement.published;
    cache.remember(array.key, std::weak_ptr<OwnerModel>(a), same);
    require(cache.find(array.key, valid) && cache.find(first.key, valid),
            "Lost map did not recover independently");
    const std::weak_ptr<OwnerModel> wa = a, wb = b;
    a.reset();
    b.reset();
    replacement.published.reset();
    require(wa.expired() && wb.expired() && !cache.find(array.key, valid) &&
                !cache.find(first.key, valid),
            "Cache retained retired owners or accepted expired tokens");
    cache.clear();
    ++combinations;
  }
  NativeTiledOwnerCache<std::weak_ptr<OwnerModel>> expired;
  {
    auto owner = std::make_shared<OwnerModel>();
    expired.remember("unrequested retired layout", std::weak_ptr<OwnerModel>(owner), same);
  }
  int pruned = 0;
  expired.prune([&](const auto &owner) {
    ++pruned;
    return !owner.expired();
  });
  require(pruned == 1, "Expired-layout prune did not visit the unrelated metadata key");
  expired.prune([&](const auto &) {
    ++pruned;
    return false;
  });
  require(pruned == 1, "Expired-layout prune retained dead metadata on an unrequested key");
  return {{"representation_orders", combinations},
          {"array_and_first_2D_keys_distinct", true},
          {"same_complete_owner_and_layout_reused", true},
          {"lost_mapping_rearms_only_array", true},
          {"cache_loss_recovery", true},
          {"retirement_and_pool_clear_release", true},
          {"expired_other_keys_pruned_without_handle_queries", true},
          {"GPU_handles_created", 0}};
}
inline QJsonObject requests_and_optional(const std::atomic_bool &cancel)
{
  QTemporaryDir directory;
  require(directory.isValid(), "Cannot create request/fence fixtures");
  write(directory.path(), mixed);
  auto graph = native_tiled_source_test::graph(directory.path(), "Non-Color", cancel);
  prepare_native_material_images(*graph, cancel);
  prepare_optional_native_world_normal(graph, cancel);
  require(graph->world_normal_tree && graph->world_normal_error.isEmpty(),
          "Missing real private Normal AOV root");
  const auto original = native_tiled_source_test::state(*graph);
  auto &texture = native_tiled_source_test::image_node(*graph->world_normal_tree);
  static_cast<blender::NodeTexImage *>(texture.storage)->projection = blender::SHD_PROJ_SPHERE;
  std::array<blender::bNodeTree *, 1> extra{graph->world_normal_tree};
  const auto ordinary = native_material_image_upload_requests(*graph),
             only_extra = native_material_image_upload_requests(*graph, extra, false),
             both = native_material_image_upload_requests(*graph, extra, true);
  require(ordinary.size() == 1 && ordinary.front().tiled &&
              ordinary.front().representation == NativeTiledRepresentation::ArrayMapping &&
              only_extra.size() == 1 && only_extra.front().tiled &&
              only_extra.front().representation == NativeTiledRepresentation::FirstTile2D &&
              both.size() == 2 && both[0].image == both[1].image,
          "Actual AOV inventory lost empty-program/extra-root representations");
  NativeMaterialGpuLibrary library;
  library.install("CPU request inventory", "CPU request inventory", graph);
  require(library.statistics().programs == 0 && library.statistics().world_programs == 0,
          "Request inventory compiled a GPU program");
  std::set<NativeTiledRepresentation> available;
  int validations = 0, touches = 0;
  NativeFragmentImageReadiness readiness;
  const auto validate = [&](const auto &requests) {
    return readiness.validate_available([&] {
      native_tiled_requests_validate(requests, [&](const auto &request) {
        ++validations;
        if (!available.contains(request.representation))
          throw NativeFragmentImagePending("CPU metadata owner is pending");
      });
    });
  };
  require(!validate(both) && touches == 0,
          "Empty program map incorrectly admitted missing required image owners");
  available.insert(NativeTiledRepresentation::ArrayMapping);
  require(!validate(both) && !validate(only_extra) && touches == 0,
          "Partial ordinary ownership admitted an extra-root-only consumer");
  available.insert(NativeTiledRepresentation::FirstTile2D);
  require(validate(both) && validate(only_extra),
          "Complete exact representation metadata was not admitted");
  readiness.require_ready(true);
  ++touches;
  require(touches == 1 && validations == 7 && native_tiled_source_test::state(*graph) == original,
          "Request readiness changed source controls or repeated target publication");
  QTemporaryDir optional_directory;
  require(optional_directory.isValid(), "Cannot create optional Normal fence fixture");
  write(optional_directory.path(), mixed);
  auto normal = native_tiled_source_test::graph(
      optional_directory.path(), "Non-Color", cancel, true, 1001, false);
  prepare_optional_native_world_normal(normal, cancel);
  const auto normal_state = native_tiled_source_test::state(*normal);
  const auto rows = prepare_native_material_images(*normal, cancel);
  require(rows.isEmpty() && normal->tiled_image_resources.empty(),
          "Optional Normal image escaped into ordinary PBR roots");
  const auto archive = archive_native_material(
      *normal, optional_directory.filePath("normal.blend"), cancel, true);
  const auto refs = native_material_bundle_image_manifest(archive.bytes);
  prepare_native_world_normal_images(normal, cancel, refs);
  const auto context = NativeFragmentImageContext::capture({normal, normal->world_normal_tree});
  require(normal->world_normal_error.isEmpty() && normal->tiled_image_resources.size() == 1 &&
              context.bindings().size() == 1 && context.bindings().front().tiled &&
              context.bindings().front().tiled_mapping &&
              native_tiled_source_test::state(*normal) == normal_state,
          "Actual optional Normal worker did not retain its complete fenced inventory");
  auto asset = native_material_asset(
      "Optional Normal TILED CPU", archive.bytes, archive.description);
  asset.scope = AssetScope::Project;
  auto hydrated = hydrate_native_material_resource(asset, cancel, archive.textures);
  require(hydrated->world_normal_tree && hydrated->world_normal_error.isEmpty() &&
              hydrated->tiled_image_resources.size() == 1,
          "Optional Normal TILED hydration lost its actual worker owner");
  QTemporaryDir changed_directory;
  require(changed_directory.isValid(), "Cannot create optional mismatch fixture");
  write(changed_directory.path(), mixed);
  auto changed = native_tiled_source_test::graph(
      changed_directory.path(), "Non-Color", cancel, true, 1001, false);
  prepare_optional_native_world_normal(changed, cancel);
  const auto before = native_tiled_source_test::state(*changed);
  const auto metadata = native_material_description_json(changed->description);
  const auto saved = archive_native_material(
      *changed, changed_directory.filePath("before.blend"), cancel, true);
  native_tiled_source_test::write(changed_directory.path(), 2000);
  prepare_native_world_normal_images(
      changed, cancel, native_material_bundle_image_manifest(saved.bytes));
  const auto color = std::find_if(changed->channels.begin(),
                                  changed->channels.end(),
                                  [](const auto &channel) { return channel.channel == 0; });
  require(
      changed->world_normal_error.contains("tile 2000") &&
          changed->world_normal_error.contains(
              "decoded source differs from immutable archive bytes") &&
          changed->tiled_image_resources.empty() && color != changed->channels.end() &&
          color->tree && native_tiled_source_test::state(*changed) == before &&
          native_material_description_json(changed->description) == metadata,
      "Optional Normal digest mismatch discarded PBR, retained mismatched owners or changed source metadata");
  return {{"empty_program_inventory_requires_both_representations", true},
          {"extra_root_only_requires_first2D", true},
          {"readiness_validation_callbacks", validations},
          {"partial_target_touches", 0},
          {"complete_target_touches", touches},
          {"programs", 0},
          {"optional_Normal_ordinary_decode_rows", 0},
          {"optional_Normal_extra_root_inventory_complete", true},
          {"optional_Normal_archive_hydrate_context", true},
          {"optional_Normal_digest_mismatch_isolated_and_rolled_back", true},
          {"independent_PBR_and_source_controls_retained", true}};
}
inline QJsonObject storage_shapes(const blender::Image &image,
                                  const NativeMaterialImageTileSet &source,
                                  const NativeTiledStoragePlan &plan)
{
  NativeTiledStorageShape shape;
  shape.width = plan.width;
  shape.height = plan.height;
  shape.layers = plan.layers;
  shape.dimensions = 2;
  shape.array = true;
  shape.format = plan.format;
  shape.supported_format = true;
  shape.map_width = plan.map_width;
  shape.map_layers = 2;
  shape.map_dimensions = 1;
  shape.map_array = true;
  shape.map_float32_rgba = true;
  require(native_tiled_storage_shape_valid(plan, shape) &&
              native_tiled_storage_source_valid(image, source, plan),
          "Actual plan failed its production pre-publication source/storage guards");
  int rejections = 0;
  for (int i = 0; i < 12; ++i) {
    auto wrong = shape;
    switch (i) {
      case 0:
        ++wrong.width;
        break;
      case 1:
        ++wrong.height;
        break;
      case 2:
        ++wrong.layers;
        break;
      case 3:
        wrong.dimensions = 1;
        break;
      case 4:
        wrong.array = false;
        break;
      case 5:
        wrong.format = NativeTiledFormat::Float16Rgba;
        break;
      case 6:
        wrong.supported_format = false;
        break;
      case 7:
        ++wrong.map_width;
        break;
      case 8:
        wrong.map_layers = 1;
        break;
      case 9:
        wrong.map_dimensions = 2;
        break;
      case 10:
        wrong.map_array = false;
        break;
      case 11:
        wrong.map_float32_rgba = false;
        break;
    }
    require(!native_tiled_storage_shape_valid(plan, wrong),
            "Malformed retained GPU storage metadata passed its production guard");
    ++rejections;
  }
  auto stale = plan;
  stale.source_key = "different immutable owner";
  require(!native_tiled_storage_source_valid(image, source, stale),
          "Different source generation passed the actual publication guard");
  return {{"actual_storage_metadata_rejections", rejections},
          {"source_generation_mismatch_rejected", true}};
}
inline QJsonObject alpha(const QString &directory, const std::atomic_bool &cancel)
{
  int modes = 0, packets = 0;
  for (const auto mode : {blender::IMA_ALPHA_STRAIGHT,
                          blender::IMA_ALPHA_PREMUL,
                          blender::IMA_ALPHA_IGNORE,
                          blender::IMA_ALPHA_CHANNEL_PACKED})
  {
    auto graph = native_tiled_source_test::graph(directory, "Linear Rec.709", cancel);
    auto &image = native_tiled_source_test::image(*graph);
    image.alpha_mode = mode;
    prepare_native_material_images(*graph, cancel);
    const auto source = native_tiled_source_test::inventory(*graph);
    const auto original = native_tiled_source_test::state(*graph);
    for (const auto &[tile, owner] : source->tiles()) {
      const auto *buffer = owner->buffer();
      NativeTiledUploadAction action;
      action.phase = NativeTiledPhase::Pixels;
      action.tile = tile;
      action.source_x = 1;
      action.source_y = 1;
      action.width = 3;
      action.height = 2;
      const auto view = native_tiled_packet_view(*source, action);
      std::vector<float> reference(std::size_t(action.width) * action.height * 4);
      blender::IMB_colormanagement_imbuf_to_float_texture(
          reference.data(),
          action.source_x,
          action.source_y,
          action.width,
          action.height,
          buffer,
          blender::BKE_image_has_gpu_texture_premultiplied_alpha(
              &image, const_cast<blender::ImBuf *>(buffer)));
      for (int y = 0; y < action.height; ++y)
        for (int x = 0; x < action.width; ++x)
          for (int c = 0; c < 4; ++c)
            require(view.pixels[4 * (y * view.source_row_length + x) + c] ==
                        reference[4 * (y * action.width + x) + c],
                    "Direct normalized packet differs from stock CPU alpha conversion");
      ++packets;
    }
    require(native_tiled_source_test::state(*graph) == original,
            "Packet alpha oracle changed original image policy/source controls");
    ++modes;
  }
  return {{"authored_alpha_modes", modes},
          {"stock_conversion_packets", packets},
          {"alpha_zero_and_half_tested", true},
          {"stock_CPU_conversion_exact", true},
          {"source_alpha_and_controls_preserved", true}};
}
}  // namespace bts::native_tiled_storage_test

namespace bts {
inline QJsonObject verify_native_material_tiled_storage()
{
  using namespace native_tiled_storage_test;
  require(QCoreApplication::instance() &&
              QThread::currentThread() == QCoreApplication::instance()->thread(),
          "Tiled storage fixture requires initialized CPU owner");
  require(!blender::GPU_context_active_get(), "Tiled storage fixture entered with GPU context");
  const auto compilations = blender::GPU_pass_global_compilation_count();
  std::atomic_bool cancel = false;
  const auto proof = work([&] {
    int stock_cases = 0;
    QJsonObject transaction_proof, cache_proof, alpha_proof, shape_proof;
    std::vector<std::weak_ptr<NativeMaterialImageResource>> retired_pixels;
    std::optional<NativeTiledStoragePlan> detached_plan;
    const std::array<Dimensions, 3> cases{mixed,
                                          Dimensions{{{11, 7}, {7, 13}, {17, 5}, {5, 19}}},
                                          Dimensions{{{16, 16}, {16, 16}, {16, 16}, {16, 16}}}};
    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
      QTemporaryDir directory;
      require(directory.isValid(), "Cannot create storage fixture directory");
      write(directory.path(), cases[case_index]);
      auto graph = native_tiled_source_test::graph(directory.path(), "Non-Color", cancel);
      prepare_native_material_images(*graph, cancel);
      const auto source = native_tiled_source_test::inventory(*graph);
      auto &image = native_tiled_source_test::image(*graph);
      QString array_key, first_key;
      std::map<int, RegionTuple> previous_layout;
      for (const auto &sequence : {numbers, std::array<int, 4>{2000, 1002, 1011, 1001}}) {
        order(image, sequence);
        blender::ImageUser stock_user{};
        blender::BKE_imageuser_default(&stock_user);
        char stock_path[FILE_MAX]{};
        blender::BKE_image_user_file_path_ex(
            graph->main.get(), &stock_user, &image, stock_path, true, true);
        require(QFileInfo(QString::fromUtf8(stock_path)).fileName() ==
                    QString("source.%1.exr").arg(sequence.front()),
                "Stock default ImageUser did not select actual first Image tile");
        stock_user.tile = 1001;
        char explicit_path[FILE_MAX]{};
        blender::BKE_image_user_file_path_ex(
            graph->main.get(), &stock_user, &image, explicit_path, true, true);
        require(QFileInfo(QString::fromUtf8(explicit_path)).fileName() == "source.1001.exr",
                "Stock explicit tile1001 path was incorrectly treated as first-tile alias");
        const auto before = native_tiled_source_test::state(*graph);
        const auto plan = native_tiled_storage_plan(
            image, *source, NativeTiledRepresentation::ArrayMapping, caps);
        const auto first = native_tiled_storage_plan(
            image, *source, NativeTiledRepresentation::FirstTile2D, caps);
        const auto stock = stock_reference(image, *source);
        compare_stock(plan, stock);
        require(plan.source_order == std::vector<int>(sequence.begin(), sequence.end()) &&
                    first.first_tile == sequence.front() &&
                    first.first_tile == blender::BKE_image_get_tile(&image, 0)->tile_number &&
                    first.regions.size() == 1 && first.regions.front().tile == sequence.front() &&
                    first.map_width == 0 && first.mapping.empty() && first.mapping_bytes == 0 &&
                    first.key != plan.key,
                "Planner guessed first tile or aliased the two representations");
        require(first.width == source->tiles().at(sequence.front())->buffer()->x &&
                    first.height == source->tiles().at(sequence.front())->buffer()->y &&
                    first.layers == 1,
                "First-tile 2D dimensions/layer differ from actual selected owner");
        if (!first_key.isEmpty())
          require(first.key != first_key,
                  "Changing actual first tile reused an incorrect 2D cache identity");
        if (!array_key.isEmpty() && previous_layout != tuples(plan))
          require(plan.key != array_key, "Different stock layouts shared one array key");
        array_key = plan.key;
        first_key = first.key;
        previous_layout = tuples(plan);
        require(
            native_tiled_source_test::state(*graph) == before &&
                !blender::BKE_image_has_loaded_ibuf(&image),
            "Storage planning sorted/mutated original Image order, runtime or source controls");
        ++stock_cases;
        if (case_index == 0 && sequence.front() == 1001) {
          transaction_proof = transactions(plan, *source);
          cache_proof = cache(plan, first, *source);
          shape_proof = storage_shapes(image, *source, plan);
        }
      }
      if (case_index == 0)
        alpha_proof = alpha(directory.path(), cancel);
      for (const auto &[tile, owner] : source->tiles())
        retired_pixels.push_back(owner);
      detached_plan = native_tiled_storage_plan(
          image, *source, NativeTiledRepresentation::ArrayMapping, caps);
    }
    require(detached_plan && retired_pixels.size() == 12 &&
                std::all_of(retired_pixels.begin(),
                            retired_pixels.end(),
                            [](const auto &owner) { return owner.expired(); }),
            "Metadata plan retained source Main or decoded pixel owners");
    require(native_tiled_mip_texels(1, 1024) == 2047 && native_tiled_mip_texels(1024, 1) == 2047 &&
                native_tiled_mip_texels(1, 1) == 1 && native_tiled_mip_texels(3, 5) == 18,
            "Rectangular/NPOT mip texel sum differs from actual per-level extents");
    int rejections = 0;
    require(rejects([&] {
              resource_product(native_tiled_mip_texels(std::numeric_limits<int>::max(),
                                                       std::numeric_limits<int>::max()),
                               16);
            }),
            "Exact mip-byte multiplication wrapped instead of rejecting overflow");
    ++rejections;
    for (const auto dimensions :
         {std::array<int, 2>{0, 1}, std::array<int, 2>{1, 0}, std::array<int, 2>{-1, 8}})
    {
      require(rejects([&] { native_tiled_mip_texels(dimensions[0], dimensions[1]); }),
              "Invalid mip dimensions were accepted");
      ++rejections;
    }
    QTemporaryDir line;
    require(line.isValid(), "Cannot create mip fixture directory");
    write(line.path(), Dimensions{{{1, 1024}, {1, 1024}, {1, 1024}, {1, 1024}}});
    auto graph = native_tiled_source_test::graph(line.path(), "Non-Color", cancel);
    prepare_native_material_images(*graph, cancel);
    const auto source = native_tiled_source_test::inventory(*graph);
    auto &image = native_tiled_source_test::image(*graph);
    const auto plan = native_tiled_storage_plan(
        image, *source, NativeTiledRepresentation::ArrayMapping, caps);
    const auto first = native_tiled_storage_plan(
        image, *source, NativeTiledRepresentation::FirstTile2D, caps);
    require(plan.layers == 4 && plan.texture_bytes == 2047ULL * 4 * 16 &&
                first.texture_bytes == 32752 && plan.mapping_bytes == 32000,
            "Exact RGBA32F rectangular texture/map byte accounting differs");
    auto low = native_tiled_source_test::graph(line.path(), "Non-Color", cancel);
    native_tiled_source_test::image(*low).flag &= ~blender::IMA_HIGH_BITDEPTH;
    prepare_native_material_images(*low, cancel);
    const auto half = native_tiled_storage_plan(native_tiled_source_test::image(*low),
                                                *native_tiled_source_test::inventory(*low),
                                                NativeTiledRepresentation::FirstTile2D,
                                                caps);
    require(half.format == NativeTiledFormat::Float16Rgba && half.texture_bytes == 16376,
            "Exact RGBA16F rectangular mip byte accounting differs");
    for (const auto limits : {NativeTiledCaps{0, 256, 16384},
                              NativeTiledCaps{1023, 256, 16384},
                              NativeTiledCaps{4096, 3, 16384},
                              NativeTiledCaps{4096, 256, 999}})
    {
      require(rejects([&] {
                native_tiled_storage_plan(
                    image, *source, NativeTiledRepresentation::ArrayMapping, limits);
              }),
              "Over-cap source plan reached allocation");
      ++rejections;
    }
    int packet_rejections = 0;
    for (int invalid = 0; invalid < 8; ++invalid) {
      NativeTiledUploadAction action;
      action.phase = NativeTiledPhase::Pixels;
      action.tile = 1001;
      action.width = action.height = 1;
      switch (invalid) {
        case 0:
          action.tile = 9999;
          break;
        case 1:
          action.source_x = -1;
          break;
        case 2:
          action.source_y = 1024;
          break;
        case 3:
          action.width = 0;
          break;
        case 4:
          action.height = 0;
          break;
        case 5:
          action.width = 2;
          break;
        case 6:
          action.source_y = 1023;
          action.height = 2;
          break;
        case 7:
          action.phase = NativeTiledPhase::Mapping;
          break;
      }
      require(rejects([&] { native_tiled_packet_view(*source, action); }),
              "Invalid packet reached source pixels");
      ++packet_rejections;
    }
    NativeTiledUploadState<Owner> tiny(plan);
    Transaction untouched(plan, *source);
    require(rejects([&] { tiny.step(15, std::ref(untouched)); }) && untouched.created.expired() &&
                untouched.publishes == 0 && tiny.phase() == NativeTiledPhase::Allocate,
            "Subpixel packet budget invoked allocation or changed state");
    QTemporaryDir mixed_format;
    require(mixed_format.isValid(), "Cannot create mixed format fixture");
    write(mixed_format.path(), mixed, true);
    auto incompatible = native_tiled_source_test::graph(mixed_format.path(), "Non-Color", cancel);
    prepare_native_material_images(*incompatible, cancel);
    const auto incompatible_source = native_tiled_source_test::inventory(*incompatible);
    require(rejects([&] {
              native_tiled_storage_plan(native_tiled_source_test::image(*incompatible),
                                        *incompatible_source,
                                        NativeTiledRepresentation::ArrayMapping,
                                        caps);
            }),
            "Mixed HALF/FLOAT tiled array was silently coerced");
    ++rejections;
    const auto selected = native_tiled_storage_plan(native_tiled_source_test::image(*incompatible),
                                                    *incompatible_source,
                                                    NativeTiledRepresentation::FirstTile2D,
                                                    caps);
    require(selected.format == NativeTiledFormat::Float32Rgba && selected.first_tile == 1001,
            "Unselected HALF tile blocked/changed first-tile 2D representation");
    require(native_image_sampler_slots(true) == 2 && native_image_sampler_slots(false) == 1 &&
                native_image_sampler_slots(true) + native_image_sampler_slots(false) + 1 + 1 == 5,
            "Flat/nonFlat/AO/ramp sampler arithmetic differs");
    require(native_tiled_default_user_tile(0) && !native_tiled_default_user_tile(1001) &&
                !native_tiled_default_user_tile(1002) && !native_tiled_default_user_tile(2000),
            "Unsupported explicit ImageUser tile bypassed the shared selection guard");
    QTemporaryDir selection;
    require(selection.isValid(), "Cannot create selection fixture directory");
    write(selection.path(), mixed);
    auto selected_graph = native_tiled_source_test::graph(selection.path(), "Non-Color", cancel);
    prepare_native_material_images(*selected_graph, cancel);
    auto &selected_image = native_tiled_source_test::image(*selected_graph);
    const auto selected_source = native_tiled_source_test::inventory(*selected_graph);
    order(selected_image, {2000, 1002, 1011, 1001});
    const auto before_selection = native_tiled_source_test::state(*selected_graph);
    prepare_optional_native_world_normal(selected_graph, cancel);
    require(selected_graph->world_normal_tree && selected_graph->world_normal_error.isEmpty(),
            "TILED fixture failed private Normal source extraction");
    NativeFragmentImageContext::capture({selected_graph, selected_graph->world_normal_tree});
    auto &texture = native_tiled_source_test::image_node(*selected_graph->world_normal_tree);
    auto &user = static_cast<blender::NodeTexImage *>(texture.storage)->iuser;
    int typed_user_rejections = 0;
    {
      const auto restore = qScopeGuard([&] { user.tile = 0; });
      for (const int tile : {1001, 1002, 2000}) {
        user.tile = tile;
        bool typed = false;
        try {
          NativeFragmentImageContext::capture({selected_graph, selected_graph->world_normal_tree});
        }
        catch (const NativeFragmentContextError &error) {
          typed = error.code == NativeFragmentContextCode::Images;
        }
        require(typed, "Explicit nonzero TILED ImageUser was not rejected before GPU binding");
        ++typed_user_rejections;
      }
    }
    require(native_tiled_source_test::state(*selected_graph) == before_selection,
            "Private ImageUser admission test changed authored source controls");
    const auto original_array = native_tiled_storage_plan(selected_image,
                                                          *selected_source,
                                                          NativeTiledRepresentation::ArrayMapping,
                                                          caps),
               original_first = native_tiled_storage_plan(
                   selected_image, *selected_source, NativeTiledRepresentation::FirstTile2D, caps);
    native_tiled_source_test::write(selection.path(), 1001);
    auto changed_graph = native_tiled_source_test::graph(selection.path(), "Non-Color", cancel);
    prepare_native_material_images(*changed_graph, cancel);
    auto &changed_image = native_tiled_source_test::image(*changed_graph);
    order(changed_image, {2000, 1002, 1011, 1001});
    const auto changed_source = native_tiled_source_test::inventory(*changed_graph);
    const auto changed_array = native_tiled_storage_plan(
                   changed_image, *changed_source, NativeTiledRepresentation::ArrayMapping, caps),
               changed_first = native_tiled_storage_plan(
                   changed_image, *changed_source, NativeTiledRepresentation::FirstTile2D, caps);
    require(
        original_array.key != changed_array.key && original_first.key == changed_first.key &&
            selected_source->tiles().at(2000) == changed_source->tiles().at(2000),
        "Unused tile replacement invalidated selected first-tile 2D ownership or failed to invalidate the array");
    auto projected = blender::GPUSamplerState::default_sampler();
    projected.disable_filtering_flag(blender::GPU_SAMPLER_FILTERING_MIPMAP);
    const auto original = projected;
    projected = native_material_image_sampler(
        projected, blender::GPU_SAMPLER_FILTERING_ANISOTROPIC_4, false);
    require(!(projected.filtering & blender::GPU_SAMPLER_FILTERING_MIPMAP) &&
                projected.extend_x == original.extend_x &&
                projected.extend_yz == original.extend_yz,
            "NonFlat binding gained mipmaps or changed extension");
    return QJsonObject{{"stock_layout_cases", stock_cases},
                       {"stock_rectangles_layers_and_map_exact", true},
                       {"equal_score_original_order_cases", true},
                       {"actual_first_Image_tile_2000_with_1001_present", true},
                       {"source_order_and_controls_unmodified", true},
                       {"metadata_plan_pixel_owners_expired", 12},
                       {"array_layout_and_first_tile_keys_separate", true},
                       {"unused_tile_replacement_keeps_first2D_key_and_pixels", true},
                       {"mips",
                        QJsonObject{{"line_1x1024_texels", 2047},
                                    {"line_RGBA16F_bytes", 16376},
                                    {"line_RGBA32F_bytes", 32752},
                                    {"array_layers", 4},
                                    {"array_RGBA32F_bytes", qint64(plan.texture_bytes)},
                                    {"sparse_mapping_bytes", qint64(plan.mapping_bytes)},
                                    {"NPOT_3x5_texels", 18}}},
                       {"cap_format_and_invalid_dimension_rejections", rejections},
                       {"packet_view_rejections", packet_rejections},
                       {"subpixel_packet_budget_rejected_before_action", true},
                       {"mixed_format_array_rejected_first2D_admitted", true},
                       {"flat_array_sampler_slots", 2},
                       {"nonFlat_sampler_slots", 1},
                       {"AO_ramp_combined_sampler_slots", 5},
                       {"nondefault_user_tile_guard", true},
                       {"typed_nondefault_user_rejections", typed_user_rejections},
                       {"stock_default_and_explicit_filepath_selection_verified", true},
                       {"checked_mip_byte_overflow_rejected", true},
                       {"nonFlat_sampler_flags_preserved", true},
                       {"transactions", transaction_proof},
                       {"sharing", cache_proof},
                       {"alpha", alpha_proof},
                       {"storage_guards", shape_proof},
                       {"requests_and_optional", requests_and_optional(cancel)}};
  });
  require(!blender::GPU_context_active_get() &&
              blender::GPU_pass_global_compilation_count() == compilations,
          "Tiled storage CPU fixture created a GPU context/program");
  return {
      {"complete", true},
      {"scope",
       "CPU stock layout, exact storage accounting and actual upload-state/packet/cache helpers only; GPU allocation, upload, shader, filtering and rendered parity unexecuted"},
      {"storage", proof},
      {"gpu_context_created", false},
      {"gpu_compilations", 0}};
}
}  // namespace bts
