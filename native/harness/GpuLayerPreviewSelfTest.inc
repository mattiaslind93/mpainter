// SPDX-License-Identifier: GPL-3.0-or-later
// GpuLayerPreview::self_test(): the owner-context GPU stack oracle that BTS_VERIFY_GPU_STACK runs.
// It is test code, so it lives in the harness. GpuLayerPreview.cc includes it at its end, inside
// namespace bts, because the oracle uses that file's internal helpers and Impl.
void GpuLayerPreview::self_test()
{
  // Owner-context integration oracle: actual compute output, no mock GPU. No readback in
  // production.
  testing_stack = true;
#include "GpuFillCoverageTests.inc"
  if (qEnvironmentVariableIsSet("BTS_GPU_FILL_COVERAGE_TEST")) {
    testing_stack = false;
    return;
  }
#include "GpuFolderCompositionTests.inc"
#include "GpuSmartMaskTests.inc"
  GpuLayerPreview preview;
  auto target = texture("BTS GPU test", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  TextureSet set;
  set.texture_resolution = 128;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.base_color = {.13f, .42f, .71f, 1};
  set.layers.front().values.roughness = .34;
  set.layers.front().values.metallic = .27;
  TextureDocument::add_fill(set);
  auto &top = set.layers.back();
  top.values.base_color = {.67f, .21f, .39f, 1};
  top.opacity.fill(.63f);
  top.mask = LayerMask{};
  top.mask->background = .37;
  PaintPixels::Stamp ink{{.53, .47}, {.43, 0}, {0, .34}};
  ink.values.base_color = {.79f, .31f, .52f, 1};
  ink.values.metallic = .8;
  ink.values.roughness = .61;
  ink.flow = .73;
  ink.hardness = .34;
  auto mask = ink;
  mask.channels = {false, false, true, false, false};
  mask.values.roughness = .86;
  top.mask->pixels.stamp(128, mask);
  auto resource = std::make_shared<FillTexture>();
  resource->width = 4;
  resource->height = 4;
  resource->hash = "self-test";
  auto texels = std::make_shared<std::vector<FillTexture::Pixel>>();
  for (int y = 0; y < 4; ++y)
    for (int x = 0; x < 4; ++x)
      texels->push_back({.1f + x * .24f, .07f + y * .27f, .4f, (x + y) % 2 ? .31f : .89f});
  resource->pixels = texels;
  int cases = verify_gpu_texture_probe() + verify_gpu_brush_raster();
#include "GpuPolygonRasterTests.inc"
  auto check = [&](int channel) {
    preview.begin_frame();
    if (preview.update(set, channel, target.get()) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU stack oracle did not execute");
    ++cases;
  };
  for (int channel = 0; channel < 3; ++channel)
    for (int mode = 0; mode <= 28; ++mode) {
      if (mode == 4)
        continue;
      top.blending[channel] = LayerBlend(mode);
      check(channel);
    }
  for (int channel = 0; channel < 3; ++channel) {
    top.blending[channel] = LayerBlend::Normal;
    top.fill.texture.reset();
    top.fill.channel_textures = {};
    fill_channel_texture(top.fill, channel) = resource;
    top.fill.scale = {2.3, .7};
    top.fill.offset = {.17, -.11};
    top.fill.rotation = 37;
    check(channel);
  }
  // Same source bytes, different per-usage OCIO interpretations must occupy distinct GPU slots.
  auto alternate = std::make_shared<FillTexture>(*resource);
  alternate->source_space = "ACEScg";
  auto converted = std::make_shared<std::vector<FillTexture::Pixel>>(*texels);
  for (auto &pixel : *converted)
    for (int c = 0; c < 3; ++c)
      pixel[c] = pixel[c] * .3f + .1f;
  alternate->pixels = converted;
  top.fill.texture = resource;
  top.fill.channel_textures = {};
  check(0);
  top.fill.texture = alternate;
  check(0);
  top.fill.texture = resource;
  check(0);
  // Distinct asset identities and sequence wrappers share one physical GPU source.
  const auto allocations = preview.impl_->resource_allocations;
  for (int use = 0; use < 3; ++use) {
    auto shared = std::make_shared<FillTexture>(*resource);
    shared->asset = QUuid::createUuid().toString().toStdString();
    shared->name = "shared " + std::to_string(use);
    auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
    members->emplace(1001, resource);
    shared->tiles = members;
    shared->hash = "sequence wrapper " + std::to_string(use);
    top.fill.texture = shared;
    check(0);
  }
  if (preview.impl_->resource_allocations != allocations)
    throw std::runtime_error("Shared texture allocated duplicate GPU storage");
  std::printf("BTS_GPU_RESOURCE_SHARED usages=3 extra_allocations=0\n");
  // Optional real encoded 4K resource test; ordinary GPU regression stays small.
  if (const char *path = std::getenv("BTS_VERIFY_SHARED_TEXTURE")) {
    const auto source = reference_image_resource(QString::fromUtf8(path));
    StudioAsset asset;
    asset.id = QUuid::createUuid();
    asset.name = source.name;
    asset.filename = source.filename;
    asset.path = path;
    asset.scope = AssetScope::Project;
    asset.usages = {"texture"};
    asset.texture_files = source.files;
    const auto decoded = decode_fill_texture(asset);
    if (!decoded->tiles || decoded->tiles->size() != 2)
      throw std::runtime_error("Shared 4K fixture needs two tiles");
    TextureSet shared_set;
    shared_set.texture_resolution = 128;
    shared_set.tiles = {1001, 1002};
    shared_set.layers.front().kind = LayerKind::Fill;
    shared_set.layers.front().fill.mode = 6;
    shared_set.layers.front().fill.texture = decoded;
    GpuLayerPreview shared_preview;
    const auto render = [&] {
      for (int tile : shared_set.tiles) {
        const auto started = performance::Clock::now();
        for (;;) {
          shared_preview.begin_frame();
          const auto result = shared_preview.update(shared_set, 0, target.get(), tile);
          if (result == GpuPreviewUpdate::Updated)
            break;
          if (result == GpuPreviewUpdate::Unsupported || performance::ms(started) > 10000)
            throw std::runtime_error("Shared 4K source failed to upload");
        }
        ++cases;
      }
    };
    render();
    const auto once = shared_preview.impl_->resource_allocations;
    if (once != 2)
      throw std::runtime_error("Expected one GPU source allocation per 4K UDIM tile");
    for (int use = 1; use < 3; ++use) {
      auto alias = asset;
      alias.id = QUuid::createUuid();
      alias.name = "Alias " + QString::number(use);
      const auto reused = decode_fill_texture(alias);
      for (int tile : shared_set.tiles)
        if (reused->tiles->at(tile)->pixels.get() != decoded->tiles->at(tile)->pixels.get())
          throw std::runtime_error("4K alias copied RAM storage");
      TextureDocument::add_fill(shared_set);
      auto &layer = shared_set.layers.back();
      layer.fill.mode = 6;
      layer.fill.texture = reused;
      layer.opacity[0] = .37;
      render();
    }
    if (shared_preview.impl_->resource_allocations != once)
      throw std::runtime_error("4K aliases duplicated GPU storage");
    std::uint64_t resource_bytes = 0;
    int resources = 0;
    for (const auto &[key, entry] : shared_preview.impl_->inputs)
      if (entry.resource) {
        if (entry.width != 4096 || entry.height != 4096)
          throw std::runtime_error("4K source was downscaled");
        resource_bytes += entry.bytes();
        ++resources;
      }
    if (resources != 2)
      throw std::runtime_error("4K shared storage lost a tile");
    std::printf(
        "BTS_GPU_RESOURCE_SHARED_4K usages=3 tiles=2 width=4096 height=4096 allocations=%llu bytes=%llu ram_shared=1\n",
        (unsigned long long)once,
        (unsigned long long)resource_bytes);
  }

  // AB: alpha is a per-use sampling policy, never a separate RAM/GPU image.
  {
    TextureSet data_set;
    data_set.texture_resolution = 128;
    data_set.tiles = {1001, 1002, 1011};
    data_set.precision[4] = ChannelPrecision::L32F;
    TextureDocument::add_displacement_channel(data_set);
    TextureDocument::add_fill(data_set);
    auto &layer = data_set.layers.back();
    layer.fill.mode = 6;
    layer.displacement.enabled = true;
    auto sequence = std::make_shared<FillTexture>();
    sequence->raw = true;
    sequence->width = sequence->height = 4;
    sequence->hash = "AB scalar sequence";
    auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
    for (int tile : data_set.tiles) {
      auto image = std::make_shared<FillTexture>(*sequence);
      image->hash = "AB raw tile " + std::to_string(tile);
      const float height = tile == 1001 ? -.001f : tile == 1002 ? .002f : 2.25f;
      image->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
          16, FillTexture::Pixel{height, height, height, 0});
      members->emplace(tile, image);
    }
    sequence->pixels = members->begin()->second->pixels;
    sequence->tiles = members;
    fill_channel_texture(layer.fill, 14) = sequence;
    fill_channel_texture(layer.fill, 4) = sequence;
    GpuLayerPreview data_preview;
    auto output = texture("AB signed data", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
    const int start = cases;
    for (bool alpha : {false, true, false})
      for (int filtering : {0, 1, 2})
        for (int channel : {4, 16})
          for (int tile : data_set.tiles) {
            const int source = channel == 16 ? 14 : 4;
            layer.fill.alpha_mask[source] = alpha;
            layer.fill.filtering = filtering;
            data_preview.begin_frame();
            if (data_preview.update(data_set, channel, output.get(), tile) !=
                GpuPreviewUpdate::Updated)
              throw std::runtime_error("AB scalar GPU source deferred");
            auto *pixels = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
            const auto index = (32 * 128 + 32) * 4;
            const float expected = tile == 1001 ? -.001f : tile == 1002 ? .002f : 2.25f;
            const bool valid = std::abs(pixels[index] - (alpha ? 0 : expected)) < 1e-6 &&
                               pixels[index + 3] == (alpha ? 0 : 1);
            MEM_delete(pixels);
            if (!valid)
              throw std::runtime_error("AB GPU signed zero-alpha data differs");
            ++cases;
          }
    if (data_preview.impl_->resource_allocations != 3)
      throw std::runtime_error("Alpha/channel policy duplicated GPU resources");
    std::printf(
        "BTS_GPU_AB_ALPHA_VERIFIED cases=%d tiles=3 source_allocations=3 alpha_opt_in=1 signed_hdr=1 float32=1\n",
        cases - start);
  }
  // Zero free VRAM is a valid pressure report, not an unavailable query.
  {
    const auto old_valid = texture_vram_query_valid.exchange(true);
    const auto old_free = texture_free_vram.exchange(0);
    const auto limit = source_cache_budget(0);
    texture_free_vram = old_free;
    texture_vram_query_valid = old_valid;
    if (limit != 0)
      throw std::runtime_error("Known exhausted VRAM fell back to a RAM-derived cache budget");
    ++cases;
    std::puts("BTS_GPU_ZERO_FREE_VRAM_VERIFIED inactive_cache_budget=0 fallback=0");
  }
  // Preparing a resource spans bounded owner frames and never publishes a
  // partially uploaded image. The subsequent graph reuses those exact objects.
  {
    auto source = std::make_shared<FillTexture>();
    source->raw = true;
    source->width = source->height = 1024;
    source->hash = "AB staged sequence";
    auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
    for (int tile : {1001, 1002, 1011}) {
      auto member = std::make_shared<FillTexture>(*source);
      member->hash = "AB staged tile " + std::to_string(tile);
      member->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
          1024 * 1024, FillTexture::Pixel{.001f, .001f, .001f, 0});
      members->emplace(tile, member);
    }
    source->tiles = members;
    source->pixels = members->begin()->second->pixels;
    GpuLayerPreview staged;
    const auto free_before = texture_free_vram.exchange(2ULL * 1024 * 1024);
    ++texture_vram_query_generation;
    texture_free_vram = free_before;
    ++texture_vram_query_generation;
    if (staged.texture_resource_ready(*source))
      throw std::runtime_error("Unallocated texture reported ready");
    staged.begin_frame();
    if (staged.prepare_texture_resource(*source) != GpuPreviewUpdate::Pending ||
        staged.texture_resource_ready(*source))
      throw std::runtime_error("Partial resource reported ready");
    ++cases;
    staged.begin_frame();
    if (staged.prepare_texture_resource(*source) != GpuPreviewUpdate::Updated ||
        !staged.texture_resource_ready(*source))
      throw std::runtime_error("Prepared resource did not become ready");
    ++cases;
    const auto allocations = staged.impl_->resource_allocations;
    TextureSet model;
    model.texture_resolution = 128;
    model.tiles = {1001, 1002, 1011};
    TextureDocument::add_displacement_channel(model);
    TextureDocument::add_fill(model);
    auto &fill = model.layers.back();
    fill.fill.mode = 6;
    fill.displacement.enabled = true;
    fill_channel_texture(fill.fill, 14) = source;
    auto image = texture("AB prepared output", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
    for (int tile : model.tiles) {
      staged.begin_frame();
      if (staged.update(model, 16, image.get(), tile) != GpuPreviewUpdate::Updated)
        throw std::runtime_error("Prepared graph needed another source upload");
      ++cases;
    }
    if (allocations != 3 || staged.impl_->resource_allocations != allocations)
      throw std::runtime_error("Prepared graph duplicated source storage");
    std::puts(
        "BTS_GPU_TEXTURE_PREPARE_VERIFIED tiles=3 frames=2 bytes=50331648 no_partial_publish=1 reused_allocations=3");
  }
  top.fill = FillProjection{};
  top.kind = LayerKind::Paint;
  top.pixels.stamp(128, ink);
  for (int channel = 0; channel < 3; ++channel) {
    top.blending[channel] = LayerBlend::Multiply;
    check(channel);
  }
  auto before = top.pixels;
  ink.values.base_color = {.15f, .91f, .23f, 1};
  top.pixels.stamp(128, ink);
  check(0);
  top.pixels = before;
  check(0);
  top.pixels = {};
  top.opacity[0] = 0;
  top.blending[0] = LayerBlend::Replace;
  check(0);
  top.opacity[0] = .63;
  top.blending[0] = LayerBlend::Normal;
  top.pixels.stamp(128, ink);
  for (int precision = 0; precision < 9; ++precision) {
    set.precision[0] = ChannelPrecision(precision);
    set.id = QUuid::createUuid();
    check(0);
  }
  // Persistent prefix and isolated folder caches, measured with real GPU work.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.clear();
  TextureLayer under;
  under.kind = LayerKind::Fill;
  under.values.base_color = {.2f, .5f, .7f, 1};
  set.layers.push_back(under);
  TextureLayer folder;
  folder.kind = LayerKind::Folder;
  folder.opacity[0] = .7f;
  folder.blending[0] = LayerBlend::Passthrough;
  TextureLayer child;
  child.parent = folder.id;
  child.depth = 1;
  auto dab = ink;
  dab.center = {.2, .2};
  dab.axis_x = {.015, 0};
  dab.axis_y = {0, .015};
  child.pixels.stamp(512, dab);
  set.layers.push_back(child);
  set.layers.push_back(folder);
  TextureLayer above;
  above.kind = LayerKind::Fill;
  above.opacity[0] = .3f;
  above.values.base_color = {.9f, .2f, .1f, 1};
  set.layers.push_back(above);
  target = texture("BTS GPU cached test", 512, 512, gpu::TextureFormat::SFLOAT_16_16_16_16);
  check(0);
  const auto full_work = preview.impl_->last_dispatch_texels;
  auto saved = set.layers[1].pixels;
  dab.center = {.3, .2};
  set.layers[1].pixels.stamp(512, dab);
  check(0);
  const auto partial_work = preview.impl_->last_dispatch_texels;
  if (preview.impl_->last_cached_nodes < 1 || partial_work >= full_work)
    throw std::runtime_error("GPU dirty edit did not reuse lower prefix");
  set.layers[1].pixels = saved;
  check(0);
  set.layers[2].opacity[0] = .4f;
  check(0);
  if (preview.impl_->last_cached_nodes < 2)
    throw std::runtime_error("Folder opacity recomputed cached children");
  set.layers[2].visible = false;
  check(0);
  set.layers[2].visible = true;
  check(0);
  std::printf(
      "BTS_GPU_PREFIX_VERIFIED full_texels=%llu dirty_texels=%llu cached_under_and_folder=1\n",
      (unsigned long long)full_work,
      (unsigned long long)partial_work);
  // Opaque uniform fills disconnect hidden paint. Partial opacity reconnects it;
  // uniform-only stacks and folder results remain single GPU texels.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.front().pixels.stamp(512, dab);
  for (int i = 0; i < 12; ++i) {
    TextureDocument::add_fill(set);
    set.layers.back().values.base_color = {.03f * i, .2f, .7f, 1};
  }
  check(0);
  const auto prefix_shape = [&]() {
    std::size_t count = 0, texels = 0;
    for (const auto &[key, entry] : preview.impl_->stacks)
      if (key.starts_with(set.id.toString().toStdString()))
        for (const auto &[id, image] : entry.outputs) {
          ++count;
          texels += GPU_texture_width(image.get()) * GPU_texture_height(image.get());
        }
    return std::pair{count, texels};
  };
  auto shape = prefix_shape();
  if (shape.first != 1 || shape.second != 1)
    throw std::runtime_error("Opaque constant prefix retained hidden full-resolution layers");
  set.layers.back().opacity[0] = .4f;
  check(0);
  shape = prefix_shape();
  if (shape.first != 2 || shape.second != 2)
    throw std::runtime_error("Constant prefix used full-resolution storage");
  for (std::size_t i = 1; i < set.layers.size(); ++i)
    set.layers[i].opacity[0] = .3f;
  check(0);
  shape = prefix_shape();
  if (shape.second <= shape.first)
    throw std::runtime_error("Transparent stack did not reconnect spatial paint");
  set.layers.back().blending[0] = LayerBlend::Replace;
  check(0);
  shape = prefix_shape();
  if (preview.impl_->last_active_nodes != 1)
    throw std::runtime_error("Replace did not disconnect hidden spatial paint");
  set.layers.back().mask = LayerMask{};
  set.layers.back().mask->background = .3;
  check(0);
  set.layers.back().mask->enabled = false;
  check(0);
  shape = prefix_shape();
  if (preview.impl_->last_active_nodes != 1)
    throw std::runtime_error("Disabled mask prevented uniform prefix");
  std::printf(
      "BTS_GPU_CONSTANT_PREFIX_VERIFIED opaque_pruned=1 translucent_reconnect=1 replace=1 mask=1 uniform_texels=1 layers=13\n");
  // Cached prefixes survive opaque occlusion, but hidden edits and memory
  // eviction still invalidate them before they can contribute again.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.front().pixels.stamp(512, dab);
  TextureDocument::add_fill(set);
  set.layers.back().opacity[0] = .4f;
  check(0);
  const auto warm_work = preview.impl_->last_dispatch_texels;
  set.layers.back().opacity[0] = 1;
  check(0);
  if (preview.impl_->last_active_nodes != 1)
    throw std::runtime_error("Opaque cache warming evaluated hidden paint");
  set.layers.back().opacity[0] = .4f;
  check(0);
  const auto reconnect_work = preview.impl_->last_dispatch_texels;
  if (preview.impl_->last_cached_nodes < 1 || reconnect_work >= warm_work)
    throw std::runtime_error("Opacity reconnect discarded a valid lower prefix");
  set.layers.back().opacity[0] = 1;
  check(0);
  dab.center = {.12, .12};
  set.layers.front().pixels.stamp(512, dab);
  check(0);
  if (preview.impl_->last_active_nodes != 1)
    throw std::runtime_error("Hidden source edit evaluated occluded work");
  set.layers.back().opacity[0] = .4f;
  check(0);
  if (preview.impl_->last_dispatch_texels <= reconnect_work)
    throw std::runtime_error("Hidden source edit reused stale lower pixels");
  const auto prior_free = texture_free_vram.exchange(2ULL * 1024 * 1024);
  set.layers.back().opacity[0] = 1;
  check(0);
  texture_free_vram = prior_free;
  set.layers.back().opacity[0] = .4f;
  check(0);
  if (preview.impl_->last_dispatch_texels < warm_work)
    throw std::runtime_error("Evicted lower cache kept valid plan metadata");
  std::printf(
      "BTS_GPU_OCCLUDED_PREFIX_VERIFIED warm_texels=%llu reconnect_texels=%llu hidden_dirty=1 pressure_evict=1 no_hidden_dispatch=1\n",
      (unsigned long long)warm_work,
      (unsigned long long)reconnect_work);
  // Thumbnail/source staging must not turn a uniform opaque fill into a
  // full-resolution prefix that depends on hidden paint.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.front().pixels.stamp(512, dab);
  TextureDocument::add_fill(set);
  check(0);
  for (auto &[key, entry] : preview.impl_->stacks)
    if (key.starts_with(set.id.toString().toStdString()))
      entry.queried_sources.insert(set.layers.back().id.toString().toStdString());
  check(0);
  if (preview.impl_->last_active_nodes != 2)
    throw std::runtime_error("Uniform source query reconnected hidden paint");
  for (const auto &[key, entry] : preview.impl_->stacks)
    if (key.starts_with(set.id.toString().toStdString()))
      for (const auto &id : {set.layers.back().id.toString().toStdString(),
                             set.layers.back().id.toString().toStdString() + "/source"})
        if (GPU_texture_width(entry.outputs.at(id).get()) != 1 ||
            GPU_texture_height(entry.outputs.at(id).get()) != 1)
          throw std::runtime_error("Uniform queried fill lost constant storage");
  set.layers.back().opacity[0] = .4f;
  check(0);
  set.layers.back().opacity[0] = 1;
  check(0);
  if (preview.impl_->last_active_nodes != 2)
    throw std::runtime_error("Restored queried fill lost opaque pruning");
  std::puts(
      "BTS_GPU_QUERIED_CONSTANT_VERIFIED active_nodes=2 uniform_storage=1 opaque_pruned=1 reconnect_exact=1");
  // Geometry masks evaluate raw ownership IDs on GPU, sharing the atlas across
  // layers/channels. Unknown IDs and uncovered UV texels always exclude.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.geometry_names = {{11, "left"}, {42, "right"}};
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.base_color = {.3f, .2f, .7f, 1};
  TextureDocument::add_fill(set);
  set.layers.back().values.base_color = {.8f, .4f, .1f, 1};
  auto ownership = std::make_shared<GeometryOwnershipTile>();
  ownership->width = ownership->height = 128;
  ownership->owners.resize(128 * 128);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      ownership->owners[y * 128 + x] = x < 40 ? 11 : x < 80 ? 42 : x < 96 ? 0 : 99;
  set.geometry_ownership[1001] = ownership;
  target = texture("BTS geometry mask oracle", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  const auto geometry_allocations = preview.impl_->geometry_masks.atlas_count();
  for (bool default_include : {false, true})
    for (bool left : {false, true})
      for (bool right : {false, true}) {
        auto &mask = set.layers.back().geometry;
        mask = {};
        mask.default_include = default_include;
        mask.set("left", left);
        mask.set("right", right);
        for (int c : {0, 1, 2, 4})
          check(c);
      }
  if (preview.impl_->geometry_masks.atlas_count() != geometry_allocations + 1)
    throw std::runtime_error("Geometry ownership was duplicated per channel");
  auto &selection = set.layers.back().geometry;
  selection = {};
  selection.set("left", false);
  check(0);
  auto changed = std::make_shared<GeometryOwnershipTile>(*ownership);
  std::swap_ranges(
      changed->owners.begin(), changed->owners.begin() + 4096, changed->owners.begin() + 8192);
  std::fill(changed->owners.begin(), changed->owners.begin() + 4096, 42);
  set.geometry_ownership[1001] = changed;
  check(0);
  selection.type = 1;
  selection.set_tile(1001, false);
  check(0);
  selection.set_tile(1001, true);
  check(0);
  std::printf(
      "BTS_GPU_GEOMETRY_MASK_VERIFIED channels=4 shared_atlas=1 known_unknown_uncovered=1 selection_rebind=1 udim_selection=1\n");
  // Actual array image with two tiles sharing an atlas layer and a third row.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.tiles = {1001, 1002, 1011};
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.base_color = {1, 1, 1, 1};
  set.layers.front().fill.mode = 6;
  auto sequence = std::make_shared<FillTexture>();
  auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
  int member_index = 0;
  for (int tile : set.tiles) {
    auto member = std::make_shared<FillTexture>();
    member->width = member->height = 4;
    member->hash = "array-oracle-" + std::to_string(tile);
    auto values = std::make_shared<std::vector<FillTexture::Pixel>>(16);
    for (int i = 0; i < 16; ++i)
      (*values)[i] = {.12f + member_index * .2f + (i % 4) * .03f,
                      .3f + (i / 4) * .1f,
                      .8f - member_index * .1f,
                      .4f + .1f * (i % 4)};
    member->pixels = values;
    members->emplace(tile, member);
    ++member_index;
  }
  *sequence = *members->begin()->second;
  sequence->hash = "array-oracle-sequence";
  sequence->tiles = members;
  set.layers.front().fill.texture = sequence;
  Texture array{GPU_texture_create_2d_array("BTS native UDIM packing test",
                                            256,
                                            128,
                                            2,
                                            1,
                                            gpu::TextureFormat::SFLOAT_32_32_32_32,
                                            GPU_TEXTURE_USAGE_GENERAL,
                                            nullptr),
                GPU_texture_free};
  const float sentinel[4] = {-.25f, -.25f, -.25f, -.25f};
  GPU_texture_clear(array.get(), GPU_DATA_FLOAT, sentinel);
  const std::map<int, std::array<int, 3>> regions{
      {1001, {0, 0, 0}}, {1002, {128, 0, 0}}, {1011, {0, 0, 1}}};
  for (int filtering : {0, 2}) {
    set.layers.front().fill.filtering = filtering;
    for (const auto &[tile, offset] : regions) {
      preview.begin_frame();
      if (preview.update(set, 0, array.get(), tile, offset) != GpuPreviewUpdate::Updated)
        throw std::runtime_error("UDIM GPU dispatch failed");
      ++cases;
    }
  }
  auto *array_pixels = static_cast<float *>(GPU_texture_read(array.get(), GPU_DATA_FLOAT, 0));
  for (int y = 0; y < 128; ++y)
    for (int x = 128; x < 256; ++x)
      for (int k = 0; k < 4; ++k)
        if (array_pixels[((std::size_t(128) + y) * 256 + x) * 4 + k] != sentinel[k])
          throw std::runtime_error("UDIM dispatch wrote outside its atlas region");
  MEM_delete(array_pixels);
  std::printf(
      "BTS_GPU_UDIM_VERIFIED tiles=1001,1002,1011 shared_array_layer=1 untouched_region=1 filtering=bilinear,nearest\n");
  // Non-square repeating textures sample -1 at the wrap seam. Cover both
  // axes and a real-world triplanar position near an integer UV boundary.
  {
    TextureSet wrapped;
    wrapped.texture_resolution = 128;
    wrapped.precision.fill(ChannelPrecision::RGB32F);
    wrapped.layers.front().kind = LayerKind::Fill;
    auto edge = std::make_shared<FillTexture>();
    edge->width = 200;
    edge->height = 203;
    edge->raw = true;
    edge->hash = "non-square-wrap-seam";
    auto data = std::make_shared<std::vector<FillTexture::Pixel>>(200 * 203);
    for (int y = 0; y < 203; ++y)
      for (int x = 0; x < 200; ++x) {
        const float v = (x == 199 || y == 202) ? 1 : .1f;
        (*data)[y * 200 + x] = {v, v, v, 1};
      }
    edge->pixels = data;
    auto &layer = wrapped.layers.front();
    layer.fill.texture = edge;
    fill_channel_texture(layer.fill, 2) = edge;
    layer.fill.offset = {.0625, .0625};
    auto geometry = std::make_shared<FillSurfaceAtlas>();
    geometry->resolution = geometry->height = 128;
    geometry->extent = .90712344646453857;
    FillSurfacePoint point;
    point.position = {-.0683562309f, -.183462828f, .589798927f};
    point.normal = {-.386040688f, -.871991932f, .3010028f};
    point.footprint = .000332799507f;
    point.valid = true;
    geometry->tiles[1001].assign(128 * 128, point);
    wrapped.projection_geometry = geometry;
    GpuLayerPreview seam_preview;
    auto image = texture(
        "BTS wrap seam regression", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
    for (int mode : {0, 1}) {
      layer.fill.mode = mode;
      layer.fill.scale = mode == 1 ? std::array<double, 2>{10, 10} : std::array<double, 2>{1, 1};
      for (int channel : {0, 2}) {
        seam_preview.begin_frame();
        if (seam_preview.update(wrapped, channel, image.get()) != GpuPreviewUpdate::Updated)
          throw std::runtime_error("GPU wrap seam did not execute");
        ++cases;
      }
    }
    std::printf(
        "BTS_GPU_WRAP_SEAM_VERIFIED non_square=200x203 uv_triplanar=1 negative_neighbor=1\n");
  }
  // Signed bump/displacement and every custom scalar use float32 without a 0..1 clamp.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.precision[4] = ChannelPrecision::L32F;
  set.displacement.allocated = set.displacement.channel = true;
  set.displacement.precision = ChannelPrecision::L32F;
  for (auto &user : set.users) {
    user.present = user.allocated = true;
    user.precision = ChannelPrecision::L32F;
  }
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.bump = -.75f;
  set.layers.front().displacement.enabled = true;
  set.layers.front().displacement.value = -.75f;
  for (auto &user : set.layers.front().users)
    user.value = -.75f;
  TextureLayer signed_paint;
  signed_paint.opacity.fill(.7f);
  signed_paint.mask = LayerMask{};
  signed_paint.mask->background = .6f;
  signed_paint.displacement.enabled = true;
  signed_paint.displacement.opacity = .7f;
  for (auto &user : signed_paint.users)
    user.opacity = .7f;
  auto signed_dab = ink;
  signed_dab.channels = {false, false, true, false, true};
  signed_dab.values.roughness = 2.5f;
  signed_dab.values.bump = 2.5f;
  signed_paint.pixels.stamp(128, signed_dab);
  signed_paint.displacement.pixels.stamp(128, signed_dab);
  for (auto &user : signed_paint.users)
    user.pixels.stamp(128, signed_dab);
  set.layers.push_back(signed_paint);
  target = texture("BTS signed scalar test", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
  for (int c : {4, 7, 8, 9, 10, 11, 12, 13, 14, 16})
    check(c);
  auto signed_texture = std::make_shared<FillTexture>();
  signed_texture->raw = true;
  signed_texture->input_space = "Non-Color/raw";
  signed_texture->width = signed_texture->height = 4;
  signed_texture->hash = "signed-scalar-test";
  auto signed_values = std::make_shared<std::vector<FillTexture::Pixel>>(16);
  for (int i = 0; i < 16; ++i) {
    const float value = i % 2 ? 2.5f : -.75f;
    (*signed_values)[i] = {value, value, value, 1};
  }
  signed_texture->pixels = signed_values;
  for (int c : {4, 7, 8, 9, 10, 11, 12, 13, 14, 16}) {
    const auto data = gpu_layer_channel(set.layers.front(), c);
    fill_channel_texture(set.layers.front().fill, data.fill_channel) = signed_texture;
    check(c);
  }
  std::printf(
      "BTS_GPU_SCALARS_VERIFIED channels=bump,user0..7,displacement signed_range=-0.75..2.5 float32=1 paint_fill_mask=1\n");
  // Immutable geometry is shared by every channel; projection math stays on GPU.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.layers.front().kind = LayerKind::Fill;
  auto atlas = std::make_shared<FillSurfaceAtlas>();
  atlas->resolution = 128;
  atlas->height = 128;
  atlas->extent = 2.3;
  auto &surface_points = atlas->tiles[1001];
  surface_points.resize(128 * 128);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      auto &point = surface_points[y * 128 + x];
      point.position = {float(x) / 128 - 0.5f, float(y) / 128 - .5f, float(x + y) / 256 - .5f};
      point.normal = {.26726124f, .53452248f, .80178373f};
      point.footprint = .019;
      point.valid = x > 12 && y > 9;
    }
  set.projection_geometry = atlas;
  auto &projected = set.layers.front();
  projected.fill.mode = 1;
  projected.values.base_color = {.4, .7, .9, 1};
  projected.values.bump = .01;
  projected.fill.texture = resource;
  fill_channel_texture(projected.fill, 1) = resource;
  fill_channel_texture(projected.fill, 2) = resource;
  target = texture("BTS triplanar reference", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (int variant = 0; variant < 4; ++variant) {
    projected.fill.hardness = variant / 3.;
    projected.fill.rotation3 = {17. * variant, -11. * variant, 23. * variant};
    projected.fill.offset3 = {.1 * variant, -.2 * variant, .07};
    projected.fill.scale3 = {1.3, -.8, 2.1};
    projected.fill.scale = {1.7, .9};
    projected.fill.rotation = 37;
    projected.fill.physical = variant == 3;
    for (int c = 0; c < 3; ++c)
      check(c);
  }
  std::printf(
      "BTS_GPU_TRIPLANAR_VERIFIED channels=3 rotation_physical_hardness=1 shared_geometry=1\n");
  for (int mode : {0, 1, 2, 3, 4, 6})
    for (int variant = 0; variant < 4; ++variant) {
      projected.fill.mode = mode;
      projected.fill.physical = variant == 3;
      projected.fill.offset3 = {.08, -.07, .03};
      projected.fill.rotation3 = {17, -11, 23};
      projected.fill.scale3 = {.65, -.8, .75};
      projected.fill.scale = {1.7, .9};
      projected.fill.offset = {.13, -.17};
      projected.fill.filtering = variant % 3;
      projected.fill.wrap = variant;
      projected.fill.shape_crop = variant == 1;
      projected.fill.depth_culling = true;
      projected.fill.depth_hardness = .35;
      projected.fill.backface_culling = variant == 2;
      projected.fill.backface_angle = 113;
      projected.fill.backface_hardness = .3;
      projected.fill.cylinder_angle = variant == 2 ? 180 : 360;
      projected.fill.atom = variant == 0;
      projected.fill.texture = variant >= 2 ? resource : nullptr;
      projected.fill.border_width = .73;
      projected.fill.invert = variant == 0;
      projected.fill.middle_dot = true;
      projected.fill.border_dots = true;
      for (int c : {1, 2})
        fill_channel_texture(projected.fill, c) = variant >= 2 ? resource : nullptr;
      for (int c : {0, 1, 2, 4})
        check(c);
    }
  const auto warp_diagnostic = [&] {
    const auto [diagnostic_x, diagnostic_y] = preview.impl_->oracle_position;
    using namespace gpu::shader;
    ShaderCreateInfo info("pyGPU_Shader");
    info.local_group_size(1, 1)
        .sampler(5, ImageType::Float2D, "layer_parameters_img")
        .image(0,
               gpu::TextureFormat::SFLOAT_32_32_32_32,
               Qualifier::write,
               ImageReadWriteType::image2D,
               "result_img")
        .push_constant(Type::float3_t, "world_point");
    info.compute_source_generated = GpuLayerParameters::declarations() + gpu_warp::source + R"GLSL(
void main(){vec3 v=world_point-projection_offset,p=vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v))*inverse_denominator;vec2 uv=vec2(-99);float depth=0;vec3 normal,du,dv;bool valid=warp_project(p,uv,depth,normal,du,dv);imageStore(result_img,ivec2(0),vec4(uv,depth,float(valid)));imageStore(result_img,ivec2(1,0),vec4(du,warp_projection_error));imageStore(result_img,ivec2(2,0),vec4(dv,0));}
)GLSL";
    Shader shader(
        GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
        GPU_shader_free);
    if (!shader)
      throw std::runtime_error("Warp diagnostic shader failed");
    GpuLayerParameters parameters;
    gpu_spatial_parameters(parameters, projected.fill, atlas.get(), set);
    auto input = texture("BTS warp diagnostic metadata",
                         parameters.values.size(),
                         1,
                         gpu::TextureFormat::SFLOAT_32_32_32_32);
    GPU_texture_update(input.get(), GPU_DATA_FLOAT, parameters.values.data());
    auto output = texture("BTS warp diagnostic", 3, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
    auto *layer_pixels = static_cast<float *>(GPU_texture_read(target.get(), GPU_DATA_FLOAT, 0));
    const auto pixel = layer_pixels + (diagnostic_y * 128 + diagnostic_x) * 4;
    std::fprintf(
        stderr, "BTS_GPU_WARP_LAYER rgba=%g,%g,%g,%g\n", pixel[0], pixel[1], pixel[2], pixel[3]);
    MEM_delete(layer_pixels);
    const auto &point = atlas->tiles.at(1001)[diagnostic_y * 128 + diagnostic_x];
    GPU_shader_bind(shader.get());
    GPU_texture_bind(input.get(), 5);
    GPU_texture_image_bind(output.get(), 0);
    GPU_shader_uniform_3fv(shader.get(), "world_point", point.position.data());
    GPU_compute_dispatch(shader.get(), 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
    auto *actual = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
    const auto frame = fill_planar_frame(projected.fill, point, *atlas);
    const auto expected = fill_warp_coordinate(projected.fill, frame);
    std::fprintf(
        stderr,
        "BTS_GPU_WARP_DIAGNOSTIC gpu_uv=%g,%g depth=%g valid=%g error=%g du=%g,%g,%g dv=%g,%g,%g cpu_uv=%g,%g depth=%g error=%g du=%g,%g,%g dv=%g,%g,%g coverage=%g sample=%g,%g\n",
        actual[0],
        actual[1],
        actual[2],
        actual[3],
        actual[7],
        actual[4],
        actual[5],
        actual[6],
        actual[8],
        actual[9],
        actual[10],
        expected ? expected->u : -99,
        expected ? expected->v : -99,
        expected ? expected->depth : -99,
        expected ? expected->error : -1,
        expected ? expected->du[0] : 0,
        expected ? expected->du[1] : 0,
        expected ? expected->du[2] : 0,
        expected ? expected->dv[0] : 0,
        expected ? expected->dv[1] : 0,
        expected ? expected->dv[2] : 0,
        expected ? fill_warp_coverage(projected.fill, frame, *expected) : 0,
        TextureDocument::fill_sample(set, projected, 0, diagnostic_x, diagnostic_y)[0],
        TextureDocument::fill_sample(set, projected, 0, diagnostic_x, diagnostic_y)[3]);
    QJsonArray rows;
    for (const auto &row : parameters.values)
      rows.append(QJsonArray{row[0], row[1], row[2], row[3]});
    std::printf(
        "BTS_GPU_WARP_NATIVE_INPUT %s\n",
        QJsonDocument(
            QJsonObject{
                {"parameters", rows},
                {"point", QJsonArray{point.position[0], point.position[1], point.position[2]}}})
            .toJson(QJsonDocument::Compact)
            .constData());
    MEM_delete(actual);
    GPU_texture_unbind_all();
    GPU_texture_image_unbind_all();
    GPU_shader_unbind();
  };
  const int warp_case_begin = cases;
  for (int deformation = 0; deformation < 4; ++deformation)
    for (int variant = 0; variant < 4; ++variant) {
      projected.fill.mode = 5;
      projected.fill.physical = variant == 3;
      projected.fill.offset3 = {.08, -.07, .03};
      projected.fill.rotation3 = {17, -11, 23};
      projected.fill.scale3 = {.65, -.8, .75};
      projected.fill.scale = {1.7, .9};
      projected.fill.offset = {.13, -.17};
      projected.fill.filtering = variant % 3;
      projected.fill.wrap = variant;
      projected.fill.shape_crop = variant == 1;
      projected.fill.depth_culling = true;
      projected.fill.depth_hardness = .35;
      projected.fill.backface_culling = variant == 2;
      projected.fill.backface_angle = 113;
      projected.fill.backface_hardness = .3;
      projected.fill.warp_depth = 1.3;
      auto grid = std::make_shared<WarpGrid>();
      if (deformation) {
        grid->move(4, {.1, -.5 - .06 * deformation, .04});
        grid->move(8, {.55, -.48, .43});
      }
      if (deformation >= 2) {
        grid->automatic_tangents = false;
        grid->at(1, 1).du = {1.2, .11, .03};
        grid->at(1, 1).dv = {.02, -.13, .87};
        grid->split(true, .27);
      }
      projected.fill.warp = grid;
      projected.fill.warp_geometry.reset();
      if (deformation == 3) {
        auto settings = std::make_shared<WarpGeometrySettings>();
        settings->authored_reference = grid;
        settings->frozen_reference = grid;
        settings->validate();
        projected.fill.warp_geometry = settings;
      }
      projected.fill.atom = variant == 0;
      projected.fill.texture = variant >= 2 ? resource : nullptr;
      for (int c : {1, 2})
        fill_channel_texture(projected.fill, c) = variant >= 2 ? resource : nullptr;
      for (int c : {0, 1, 2, 4})
        try {
          check(c);
        }
        catch (const std::exception &error) {
          warp_diagnostic();
          throw std::runtime_error("Warp deformation=" + std::to_string(deformation) +
                                   " variant=" + std::to_string(variant) +
                                   " channel=" + std::to_string(c) + ": " + error.what());
        }
    }
  // Timing-only owner-context run. Blocking query reads are confined to this
  // test; interactive production never waits for timer or texture readbacks.
  if (std::getenv("BTS_GPU_WARP_TIMING")) {
    auto sample = set;
    sample.texture_resolution = 1024;
    auto &layer = sample.layers.front();
    layer.fill.physical = false;
    layer.fill.offset3 = {};
    layer.fill.rotation3 = {};
    layer.fill.scale3 = {1, 1, 1};
    layer.fill.scale = {1, 1};
    layer.fill.offset = {};
    layer.fill.rotation = 0;
    layer.fill.texture = resource;
    layer.fill.atom = false;
    layer.fill.filtering = 1;
    layer.fill.backface_culling = false;
    layer.fill.warp_geometry.reset();
    auto grid = std::make_shared<WarpGrid>();
    grid->move(4, {.1, -.56, .04});
    layer.fill.warp = grid;
    GpuLayerPreview measured;
    auto image = texture("BTS Warp timing", 1024, 1024, gpu::TextureFormat::SFLOAT_16_16_16_16);
    GLuint queries[2];
    glGenQueries(2, queries);
    benchmark_stack = true;
    for (int operation = 0; operation < 4; ++operation) {
      if (operation == 2)
        layer.opacity[0] = .5;
      if (operation == 3) {
        auto changed = std::make_shared<WarpGrid>(*grid);
        changed->move(4, {.13, -.54, .06});
        layer.fill.warp = changed;
      }
      measured.begin_frame();
      const auto start = performance::Clock::now();
      glQueryCounter(queries[0], GL_TIMESTAMP);
      if (measured.update(sample, 0, image.get()) != GpuPreviewUpdate::Updated)
        throw std::runtime_error("Warp timing unexpectedly deferred");
      const double cpu_ms = performance::ms(start);
      glQueryCounter(queries[1], GL_TIMESTAMP);
      GLuint64 begin, end;
      glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &end);
      glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &begin);
      std::printf(
          "BTS_GPU_WARP_TIMING size=1024 operation=%s cpu_submit_ms=%.3f gpu_ms=%.3f test_query_wait_ms=%.3f\n",
          std::array{"initial", "unchanged", "opacity", "control-point"}[operation],
          cpu_ms,
          double(end - begin) / 1e6,
          performance::ms(start) - cpu_ms);
    }
    benchmark_stack = false;
    glDeleteQueries(2, queries);
  }
  const int warp_layer_cases = cases - warp_case_begin;
  // Warp mask sources use the same GPU solver and one shared prefix per tile.
  projected.fill = FillProjection{};
  projected.values.base_color = {.3f, .7f, .9f, 1};
  projected.mask = LayerMask{};
  projected.mask->background = .6;
  MaskEffect warp_mask;
  warp_mask.kind = MaskEffectKind::Fill;
  warp_mask.fill = .83;
  warp_mask.opacity = .71;
  warp_mask.blending = LayerBlend::Multiply;
  warp_mask.mapping.projection.mode = 5;
  warp_mask.mapping.projection.warp_depth = 1.7;
  warp_mask.mapping.projection.depth_culling = true;
  warp_mask.mapping.projection.depth_hardness = .24;
  auto mask_grid = std::make_shared<WarpGrid>();
  mask_grid->move(4, {.06, -.56, -.03});
  warp_mask.mapping.projection.warp = mask_grid;
  auto udim_atlas = std::make_shared<FillSurfaceAtlas>(*atlas);
  udim_atlas->tiles[1002] = udim_atlas->tiles.at(1001);
  for (auto &point : udim_atlas->tiles[1002])
    point.position[0] += .14;
  set.projection_geometry = udim_atlas;
  set.tiles = {1001, 1002};
  for (bool atom : {false, true})
    for (int filter = 0; filter < 3; ++filter) {
      warp_mask.mapping.projection.atom = atom;
      warp_mask.mapping.projection.filtering = filter;
      projected.mask->effects = {warp_mask};
      for (int tile : set.tiles) {
        for (int channel : {0, 2}) {
          const auto work = preview.impl_->mask_dispatch_texels;
          preview.begin_frame();
          if (preview.update(set, channel, target.get(), tile) != GpuPreviewUpdate::Updated)
            throw std::runtime_error("GPU Warp mask UDIM deferred");
          if (channel == 2 && preview.impl_->mask_dispatch_texels != work)
            throw std::runtime_error("Warp mask duplicated between channels");
          ++cases;
        }
      }
    }
  set.selected_layer = projected.id;
  set.selected_mask = true;
  for (int tile : set.tiles) {
    preview.begin_frame();
    if (preview.update(set, 15, target.get(), tile) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU Warp mask isolation deferred");
    ++cases;
  }
  set.selected_mask = false;
  set.tiles = {1001};
  set.projection_geometry = atlas;
  projected.mask.reset();
  std::printf(
      "BTS_GPU_WARP_MASK_VERIFIED cases=26 tiles=2 uniform_atom=1 filters=3 shared_channels=2 isolation=1\n");
  projected.fill.warp.reset();
  projected.fill.warp_geometry.reset();
  std::printf(
      "BTS_GPU_WARP_VERIFIED cases=%d identity_deformed_tangents_split=1 uniform_atom_texture=1 filter_wrap_physical=1 crop_depth_backface=1 frozen_envelope=1\n",
      warp_layer_cases);
  std::printf(
      "BTS_GPU_PROJECTION_VERIFIED modes=uv,triplanar,planar,spherical,cylindrical,udim atom_uniform_texture=1 crop_cull_wrap_filter_physical=1\n");
  projected.fill.texture = resource;
  projected.fill.atom = false;
  projected.fill.physical = false;
  projected.fill.shape_crop = false;
  projected.fill.backface_culling = false;
  projected.fill.filtering = 0;
  auto symmetry = std::make_shared<FillSymmetry>();
  symmetry->constraint.enabled = true;
  symmetry->constraint.copies = 5;
  symmetry->constraint.center = {.03, -.11, .05};
  symmetry->constraint.offset = .1;
  projected.fill.symmetry = symmetry;
  for (int mode : {1, 2, 3, 4})
    for (int axis = 0; axis < 3; ++axis)
      for (int kind = 0; kind < 2; ++kind) {
        symmetry->constraint.axis = axis;
        symmetry->constraint.mode = SymmetryMode(kind);
        symmetry->flip_u = true;
        symmetry->flip_v = axis == 1;
        projected.fill.mode = mode;
        check(0);
      }
  std::printf(
      "BTS_GPU_PROJECTION_SYMMETRY_VERIFIED modes=triplanar,planar,spherical,cylindrical axes=xyz mirror_radial_flip=1\n");

  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels[3] = true;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.normal = normalized_normal({.3f, .1f, .9f});
  TextureDocument::add_fill(set);
  auto &normal_layer = set.layers.back();
  normal_layer.values.normal = normalized_normal({-.2f, .4f, .8f});
  normal_layer.opacity[3] = .67;
  normal_layer.mask = LayerMask{};
  normal_layer.mask->background = .59f;
  target = texture(
      "BTS authored normal reference", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (int mode = 0; mode <= 28; ++mode) {
    normal_layer.blending[3] = LayerBlend(mode);
    check(6);
  }
  normal_layer.blending[3] = LayerBlend::NormalDetail;
  fill_channel_texture(normal_layer.fill, 3) = resource;
  check(6);
  normal_layer.kind = LayerKind::Paint;
  auto normal_ink = ink;
  normal_ink.channels = {false, false, false, true, false};
  normal_ink.values.normal = normalized_normal({-.4f, .3f, .8f});
  normal_layer.pixels.stamp(128, normal_ink);
  check(6);
  std::printf("BTS_GPU_AUTHORED_NORMAL_VERIFIED modes=29 fill_texture_paint_mask=1\n");
  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels[3] = true;
  auto &resampled = set.layers.front();
  resampled.mask = LayerMask{};
  resampled.mask->background = .25;
  auto resize_ink = ink;
  resize_ink.channels = {true, true, true, true, true};
  resize_ink.values.normal = normalized_normal({.2f, -.3f, .9f});
  resize_ink.values.bump = -.1f;
  resampled.pixels.stamp(512, resize_ink);
  resampled.mask->pixels.stamp(256, mask);
  for (int height : {128, 256}) {
    set.texture_height = height;
    target = texture(
        "BTS mixed-axis authored resampling", 128, height, gpu::TextureFormat::SFLOAT_16_16_16_16);
    for (int c : {0, 1, 2, 4, 6})
      check(c);
  }
  std::printf(
      "BTS_GPU_RESAMPLING_VERIFIED source=512 target=128x128,128x256 paint_mask_normal=1\n");
  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels[3] = true;
  set.displacement.channel = true;
  set.users[0].present = true;
  set.layers.front().kind = LayerKind::Fill;
  TextureDocument::add_fill(set);
  auto &effect_layer = set.layers.back();
  effect_layer.values.base_color = {.2, .4, .7, 1};
  effect_layer.values.metallic = .34;
  effect_layer.values.roughness = .67;
  effect_layer.values.bump = -.2;
  effect_layer.values.normal = normalized_normal({.2f, -.3f, .9f});
  effect_layer.opacity.fill(.68);
  effect_layer.displacement.enabled = true;
  effect_layer.displacement.value = -.2;
  effect_layer.users[0].enabled = true;
  effect_layer.users[0].value = .7;
  effect_layer.mask = LayerMask{};
  effect_layer.mask->background = .43;
  MaskEffect mask_levels;
  mask_levels.kind = MaskEffectKind::Levels;
  mask_levels.levels.gamma = 1.6;
  mask_levels.levels.output_high = .9;
  effect_layer.mask->effects.push_back(mask_levels);
  MaskEffect mask_filter;
  mask_filter.kind = MaskEffectKind::Filter;
  mask_filter.filter.resource = 1;
  mask_filter.opacity = .6;
  effect_layer.mask->effects.push_back(mask_filter);
  MaskEffect mask_fill;
  mask_fill.kind = MaskEffectKind::Fill;
  mask_fill.fill = .7;
  mask_fill.opacity = .4;
  mask_fill.blending = LayerBlend::Multiply;
  effect_layer.mask->effects.push_back(mask_fill);
  target = texture(
      "BTS content and mask effects reference", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (int c : {0, 1, 2, 4, 6, 7, 16}) {
    const int ec = gpu_layer_channel(effect_layer, c).fill_channel;
    LevelsEffect levels;
    levels.channel = ec;
    levels.input_low = -.1;
    levels.input_high = .9;
    levels.gamma = 1.7;
    levels.output_low = -.2;
    levels.output_high = 1.3;
    levels.rgb_curves[1].gamma = .8;
    LevelsEffect filter;
    filter.kind = ContentEffectKind::Filter;
    filter.channel = ec;
    filter.filter.resource = 1;
    filter.opacity = .37;
    filter.blending = LayerBlend::Overlay;
    effect_layer.effects = {levels, filter};
    for (int variant = 0; variant < 4; ++variant) {
      effect_layer.effects[0].clamp = variant == 1;
      effect_layer.effects[1].filter.resource = variant;
      const auto blend = variant == 3 ? LayerBlend::Passthrough :
                         variant == 2 ? LayerBlend::Replace :
                                        LayerBlend::Normal;
      effect_layer.blending.fill(blend);
      effect_layer.displacement.blending = blend;
      effect_layer.users[0].blending = blend;
      check(c);
    }
  }
  const int levels_mix_first = cases;
  set.selected_mask = true;
  for (int channel : {0, 1, 2, 4, 7, 16})
    for (auto mode : {LayerBlend::Normal,
                      LayerBlend::Multiply,
                      LayerBlend::Overlay,
                      LayerBlend::LinearDodge,
                      LayerBlend::Disabled})
      for (float opacity : {0.f, .37f, 1.f}) {
        LevelsEffect levels;
        levels.channel = gpu_layer_channel(effect_layer, channel).fill_channel;
        levels.gamma = 1.7;
        levels.output_low = .1;
        levels.output_high = .9;
        levels.opacity = opacity;
        levels.blending = mode;
        effect_layer.effects = {levels};
        auto &mask_mix = effect_layer.mask->effects.front();
        mask_mix.opacity = opacity;
        mask_mix.blending = mode;
        check(channel);
        if (channel == 0)
          check(15);
      }
  set.selected_mask = false;
  std::printf(
      "BTS_GPU_LEVELS_MIX_VERIFIED cases=%d channels=color,metallic,roughness,bump,user,displacement mask_isolation=1 modes=normal,multiply,overlay,add,disabled opacity=0,0.37,1 cpu_oracle=1\n",
      cases - levels_mix_first);
  effect_layer.effects.clear();
  effect_layer.mask->effects.clear();
  check(0);
  std::printf(
      "BTS_GPU_POINT_EFFECTS_VERIFIED channels=color,metallic,roughness,bump,normal,user,displacement content_levels_filters_mask_levels_fill=1 passthrough_replace=1\n");
  // Baked mask inputs stay shared and invalidate only their consuming prefixes.
  for (int role : {2, 4, 5}) {
    MeshMap map;
    map.name = "Mask source";
    std::vector<float> values(32 * 32 * 4);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        const int i = (y * 32 + x) * 4;
        const float alpha = x < 6 ? 0 : x < 16 ? .4f : 1;
        values[i] = alpha * (x + .5f) / 32;
        values[i + 1] = alpha * (y + .5f) / 32;
        values[i + 2] = alpha * .4f;
        values[i + 3] = alpha;
      }
    auto pixels = std::make_shared<MeshMapPixels>(32, std::move(values));
    map.tiles[1001] = {32, pixels->hash, pixels};
    set.mesh_maps[role] = map;
  }
  MaskEffect id_selection;
  id_selection.kind = MaskEffectKind::ColorSelection;
  id_selection.output = .83;
  id_selection.hardness = .4;
  id_selection.tolerance = .2;
  for (int i = 0; i < 17; ++i)
    id_selection.colors.push_back({(i + .5f) / 17, .43f, .4f});
  effect_layer.mask->effects = {id_selection, mask_levels, mask_filter};
  for (int variant = 0; variant < 4; ++variant) {
    effect_layer.mask->effects[0].use_id_map = variant != 1;
    if (variant == 2)
      effect_layer.mask->effects[0].colors.clear();
    if (variant == 3)
      effect_layer.mask->effects[0] = id_selection;
    for (int c : {0, 2, 4, 6, 7, 16})
      check(c);
  }
  const auto original_generator_ao = set.mesh_maps[3],
             original_generator_thickness = set.mesh_maps[6],
             original_generator_normal = set.mesh_maps[1];
  set.mesh_maps[3] = set.mesh_maps[4];
  set.mesh_maps[6] = set.mesh_maps[4];
  set.mesh_maps[1] = set.mesh_maps[5];
  for (int generator = 0; generator <= 6; ++generator)
    for (int axis = 0; axis < 3; ++axis)
      for (int curve = 0; curve < 3; ++curve)
        for (int filtering = 0; filtering < 2; ++filtering) {
          MaskEffect effect;
          effect.kind = MaskEffectKind::Generator;
          effect.generator = generator;
          effect.axis = axis;
          effect.curve = curve;
          effect.map_filter = filtering;
          effect.contrast = 2.3;
          effect.bias = -.1;
          effect.invert = axis == 1;
          effect.opacity = .73;
          effect.blending = LayerBlend::Multiply;
          effect_layer.mask->effects = {effect, mask_filter};
          for (int c : {0, 2, 4})
            check(c);
        }
  for (const auto direction :
       std::array<std::array<float, 3>, 5>{
           {{1, 0, 0}, {0, -1, 0}, {1, .3f, -.7f}, {0, 0, 0}, {0, 0, 1e-8f}}})
  {
    MaskEffect light;
    light.kind = MaskEffectKind::Generator;
    light.generator = 6;
    light.light_direction = direction;
    light.map_filter = 1;
    effect_layer.mask->effects = {light};
    check(0);
    check(2);
  }
  for (float influence : {0.f, .3f, 1.f}) {
    MaskEffect dirt;
    dirt.kind = MaskEffectKind::Generator;
    dirt.generator = 5;
    dirt.dirt_curvature = influence;
    effect_layer.mask->effects = {dirt};
    check(0);
    check(4);
  }
  for (int generator : {4, 5, 6}) {
    const auto roles = mask_generator_inputs(generator);
    for (int role = 0; role < 7; ++role)
      if (roles & (1u << role)) {
        const auto original = set.mesh_maps[role];
        MaskEffect live;
        live.kind = MaskEffectKind::Generator;
        live.generator = generator;
        effect_layer.mask->effects = {live};
        set.mesh_maps[role].reset();
        check(0);
        set.mesh_maps[role] = set.mesh_maps[5];
        check(0);
        set.mesh_maps[role] = original;
        check(0);
      }
  }
  // Isolation must bind every input itself, never inherit a previous layer draw.
  for (int generator : {4, 6}) {
    MaskEffect resource;
    resource.kind = MaskEffectKind::Generator;
    resource.generator = generator;
    effect_layer.mask->effects = {resource};
    GPU_texture_unbind_all();
    auto *isolated = preview.mask_texture(set, effect_layer.id, 1001);
    if (!isolated)
      throw std::runtime_error("Generator isolation did not finish");
    auto *values = static_cast<float *>(GPU_texture_read(isolated, GPU_DATA_FLOAT, 0));
    if (!values)
      throw std::runtime_error("Generator isolation readback failed");
    double error = 0;
    for (int y = 4; y < 128; y += 16)
      for (int x = 4; x < 128; x += 16)
        error = std::max(error,
                         double(std::abs(values[y * 128 + x] -
                                         effect_layer.mask->value(
                                             x, y, 1001, 128, 128, nullptr, &set.mesh_maps))));
    MEM_delete(values);
    if (error > 2e-6)
      throw std::runtime_error("Generator isolation inherited stale sampler bindings");
    GPU_texture_unbind_all();
  }
  std::printf(
      "BTS_GPU_MESH_GENERATORS_VERIFIED thickness=1 dirt_ao_curvature=1 world_normal_light=1 directions=5 missing_rebake=1 clean_bindings=1\n");
  set.mesh_maps[3] = original_generator_ao;
  set.mesh_maps[6] = original_generator_thickness;
  set.mesh_maps[1] = original_generator_normal;
  for (int generator : {2, 3}) {
    const int role = mask_generator_role(generator);
    const auto original = set.mesh_maps[role];
    MaskEffect live;
    live.kind = MaskEffectKind::Generator;
    live.generator = generator;
    live.curve = generator == 3 ? 2 : 1;
    live.map_filter = 1;
    live.invert = true;
    effect_layer.mask->effects = {live};
    set.mesh_maps[role].reset();
    check(0);
    set.mesh_maps[role] = set.mesh_maps[5];
    check(0);
    set.mesh_maps[role] = original;
    check(0);
  }
  std::printf("BTS_GPU_SMART_MATERIAL_GENERATORS_VERIFIED ao=1 curves=3 filters=2 axes=3\n");
  // A map disappearing and returning must not leave a stale cached result.
  const auto saved_position = set.mesh_maps[5];
  MaskEffect height_mask;
  height_mask.kind = MaskEffectKind::Generator;
  height_mask.generator = 1;
  height_mask.axis = 0;
  effect_layer.mask->effects = {height_mask};
  set.mesh_maps[5].reset();
  check(0);
  set.mesh_maps[5] = saved_position;
  check(0);
  set.mesh_maps[5]->tiles.clear();
  check(0);
  set.mesh_maps[5] = saved_position;
  check(0);
  for (int operation = 0; operation < 3; ++operation)
    for (int left : {2, 3})
      for (int right : {2, 3}) {
        MaskEffect compare;
        compare.kind = MaskEffectKind::Compare;
        compare.operation = operation;
        compare.left = left;
        compare.right = right;
        compare.constant = .47;
        compare.hardness = .4;
        compare.tolerance = .23;
        compare.opacity = .8;
        effect_layer.mask->effects = {height_mask, compare};
        check(0);
        check(2);
      }
  effect_layer.mask->effects.clear();
  check(0);
  std::printf(
      "BTS_GPU_MASK_MAPS_VERIFIED id_colors=17 generators=height,curvature axes=xyz constant_compare=1 missing_rebake=1 shared_maps=1\n");
  // Material-channel Compare reads the GPU source/below prefix at this exact
  // layer depth. Never substitute the final channel, or let a mask read itself.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels.fill(true);
  set.ao_channel = true;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.base_color = {.15f, .3f, .55f, 1};
  set.layers.front().values.roughness = .23f;
  set.layers.front().values.metallic = .65f;
  set.layers.front().values.bump = -.3f;
  set.layers.front().values.normal = normalized_normal({-.2f, .3f, .9f});
  set.layers.front().ao_enabled = true;
  set.layers.front().values.ambient_occlusion = .31f;
  TextureDocument::add_fill(set);
  auto &compared = set.layers.back();
  compared.channels.fill(true);
  compared.ao_enabled = true;
  compared.values.base_color = {.7f, .4f, .2f, 1};
  compared.values.roughness = .68f;
  compared.values.metallic = .35f;
  compared.values.bump = .2f;
  compared.values.normal = normalized_normal({.3f, -.2f, .9f});
  compared.values.ambient_occlusion = .74f;
  compared.opacity.fill(.71f);
  compared.mask = LayerMask{};
  compared.mask->background = .37f;
  MaskEffect material_compare;
  material_compare.kind = MaskEffectKind::Compare;
  material_compare.hardness = .43f;
  material_compare.tolerance = .28f;
  material_compare.constant = .42f;
  material_compare.opacity = .81f;
  target = texture(
      "BTS GPU material comparison", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  const auto compare_case_start = cases;
  for (int c = 0; c < 6; ++c)
    for (int operation = 0; operation < 3; ++operation)
      for (int left = 0; left < 4; ++left)
        for (int right = 0; right < 4; ++right) {
          if (left >= 2 && right >= 2)
            continue;
          material_compare.compare_channel = c;
          material_compare.left = left;
          material_compare.right = right;
          material_compare.operation = operation;
          compared.mask->effects = {material_compare};
          check(0);
          check(2);
        }
  for (float offset : {-.3f, 0.f, .4f})
    for (float width : {0.f, .2f, 1.f})
      for (bool invert : {false, true}) {
        auto ramp = material_compare;
        ramp.compare_channel = 4;
        ramp.left = 1;
        ramp.right = 0;
        ramp.compare_offset = offset;
        ramp.compare_width = width;
        ramp.compare_invert = invert;
        ramp.compare_ramp = {0, .1f, .8f, .6f, 1};
        compared.mask->effects = {ramp};
        check(0);
        check(15);
      }
  material_compare.compare_channel = 2;
  material_compare.left = 1;
  material_compare.right = 0;
  material_compare.operation = 2;
  compared.mask->effects = {material_compare};
  set.selected_layer = compared.id;
  // Isolation still evaluates hidden/disabled masks and their source metadata.
  for (int variant = 0; variant < 6; ++variant) {
    compared.visible = variant != 1;
    compared.mask->enabled = variant != 2;
    compared.channels[2] = variant != 3;
    compared.opacity[2] = variant == 4 ? 0 : .71f;
    compared.blending[2] = variant == 5 ? LayerBlend::Passthrough : LayerBlend::Normal;
    check(15);
    check(0);
  }
  compared.visible = true;
  compared.mask->enabled = true;
  compared.channels[2] = true;
  compared.opacity[2] = .71f;
  compared.blending[2] = LayerBlend::Normal;
  LevelsEffect comparison_paint;
  comparison_paint.kind = ContentEffectKind::Paint;
  comparison_paint.paint.pixels.stamp(128, ink);
  LevelsEffect comparison_levels;
  comparison_levels.channel = 2;
  comparison_levels.gamma = 1.8f;
  compared.effects = {comparison_paint, comparison_levels};
  check(0);
  check(15);
  compared.effects.clear();
  const auto comparison_saved = set;
  std::swap(set.layers[0], set.layers[1]);
  check(0);
  set = comparison_saved;
  check(0);
  // Nested folders use their own depth accumulator for the Below operand.
  set = comparison_saved;
  TextureLayer comparison_child = set.layers.back();
  comparison_child.id = QUuid::createUuid();
  comparison_child.depth = 1;
  TextureLayer group;
  group.kind = LayerKind::Folder;
  group.channels.fill(true);
  group.mask = LayerMask{};
  group.mask->background = .8f;
  group.mask->effects = {material_compare};
  set.layers = {set.layers.front(), comparison_child, group};
  set.selected_layer = comparison_child.id;
  check(0);
  check(15);
  set.selected_layer = group.id;
  check(15);
  check(2);
  // An anchor consumes an earlier mask prefix, including its material queries.
  MaskEffect point_anchor;
  point_anchor.kind = MaskEffectKind::Anchor;
  set.layers[1].mask->effects.push_back(point_anchor);
  MaskEffect from_anchor;
  from_anchor.kind = MaskEffectKind::Fill;
  from_anchor.anchor_source = point_anchor.id;
  set.layers[2].mask->effects = {from_anchor};
  check(0);
  check(15);
  set.layers[1].visible = false;
  set.layers[1].mask->enabled = false;
  check(0);
  check(15);
  set = comparison_saved;
  set.texture_resolution = 512;
  set.layers.front().kind = LayerKind::Paint;
  auto comparison_ink = ink;
  comparison_ink.channels = {true, true, true, true, true};
  comparison_ink.values.bump = -.3f;
  comparison_ink.center = {.15, .15};
  comparison_ink.axis_x = {.06, 0};
  comparison_ink.axis_y = {0, .06};
  set.layers.front().pixels.stamp(512, comparison_ink);
  set.layers.back().mask->effects[0].compare_channel = 4;
  target = texture(
      "BTS material comparison dirty chunks", 512, 512, gpu::TextureFormat::SFLOAT_16_16_16_16);
  check(0);
  const auto comparison_dispatch_before = preview.impl_->mask_dispatch_texels;
  comparison_ink.center = {.1, .1};
  comparison_ink.values.bump = .8f;
  set.layers.front().pixels.stamp(512, comparison_ink);
  check(0);
  const auto comparison_dirty = preview.impl_->mask_dispatch_texels - comparison_dispatch_before;
  if (!comparison_dirty || comparison_dirty > 2ULL * 256 * 256)
    throw std::runtime_error("Material Compare recomputed clean mask chunks");
  const auto comparison_unchanged = preview.impl_->mask_dispatch_texels;
  check(0);
  if (preview.impl_->mask_dispatch_texels != comparison_unchanged ||
      preview.impl_->last_dispatch_texels)
    throw std::runtime_error("Unchanged material Compare recomputed prefixes");
  std::printf(
      "BTS_GPU_MATERIAL_COMPARE_VERIFIED cases=%d channels=6 operands=4 operations=3 hidden_disabled_opacity_zero=1 content_paint_levels=1 passthrough=1 reorder_undo=1 isolation=1 production_readback=0\n",
      cases - compare_case_start);
  // UI histograms reduce GPU prefixes asynchronously. The CPU oracle below is
  // test-only and matches the former panel sampling positions and Auto range.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels.fill(true);
  set.ao_channel = true;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.roughness = .19f;
  set.layers.front().values.base_color = {.17f, .33f, .58f, 1};
  TextureDocument::add_fill(set);
  auto &hist_layer = set.layers.back();
  hist_layer.values.roughness = .71f;
  hist_layer.values.base_color = {.69f, .41f, .23f, 1};
  hist_layer.channels.fill(true);
  hist_layer.ao_enabled = true;
  hist_layer.mask = LayerMask{};
  hist_layer.mask->background = .37f;
  set.selected_layer = hist_layer.id;
  MaskEffect hist_compare;
  hist_compare.kind = MaskEffectKind::Compare;
  hist_compare.constant = .43f;
  hist_layer.mask->effects = {hist_compare};
  const auto histogram_start = cases;
  std::uint64_t histogram_ticket = 0;
  const auto histogram_reference = [&](const HistogramRequest &request) {
    HistogramMetadata expected;
    expected.minimum = 65504;
    expected.maximum = -65504;
    expected.ticket = histogram_ticket;
    const auto &layer = TextureDocument::layer(set, request.layer);
    LayerMask prefix;
    if (request.mask) {
      const auto input = mask_effect_input(*layer.mask, request.effect);
      if (!input)
        throw std::runtime_error("Histogram mask effect missing");
      prefix = *input;
    }
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        const int px = (2 * x + 1) * 128 / 32, py = (2 * y + 1) * 128 / 32;
        const auto record = [&](int series, float value) {
          ++expected.bins[series][std::clamp(int(std::clamp(value, 0.f, 1.f) * 63), 0, 63)];
          expected.minimum = std::min(expected.minimum, value);
          expected.maximum = std::max(expected.maximum, value);
        };
        if (request.compare) {
          const auto &effect = layer.mask->effects.back();
          const auto context = TextureDocument::comparison_inputs(set, layer.id, px, py, 1001);
          const float previous = prefix.value(
              px, py, 1001, 128, 128, nullptr, &set.mesh_maps, &context);
          record(0, mask_compare_operand(effect, effect.left, previous, &context));
          record(1, mask_compare_operand(effect, effect.right, previous, &context));
        }
        else {
          std::array<float, 3> rgb;
          if (request.mask) {
            const auto context = TextureDocument::comparison_inputs(set, layer.id, px, py, 1001);
            rgb.fill(prefix.value(px, py, 1001, 128, 128, nullptr, &set.mesh_maps, &context));
          }
          else
            rgb = TextureDocument::effect_input(set, request.effect, px, py, 1001);
          for (int c = 0; c < 3; ++c)
            if (!request.component || request.component == c + 1)
              record(0, request.component ? levels_curve_value(request.curve, rgb[c]) : rgb[c]);
        }
      }
    return expected;
  };
  const auto check_histogram = [&](HistogramRequest request) {
    ++histogram_ticket;
    preview.begin_frame();
    if (preview.histogram(set, request, histogram_ticket) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU histogram request deferred");
    const auto start = performance::Clock::now();
    while (preview.impl_->histograms.pending()) {
      preview.impl_->histograms.poll();
      if (performance::ms(start) > 3000)
        throw std::runtime_error("Histogram metadata timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto actual = preview.histogram_result(request.effect);
    const auto expected = histogram_reference(request);
    if (!actual || actual->ticket != histogram_ticket || actual->bins != expected.bins ||
        std::abs(actual->minimum - expected.minimum) > 2e-5 ||
        std::abs(actual->maximum - expected.maximum) > 2e-5)
    {
      std::fprintf(
          stderr,
          "BTS_GPU_HISTOGRAM_ORACLE compare=%d mask=%d channel=%d component=%d ticket=%llu min=%g/%g max=%g/%g bins_equal=%d\n",
          request.compare,
          request.mask,
          request.channel,
          request.component,
          (unsigned long long)histogram_ticket,
          actual ? actual->minimum : -999,
          expected.minimum,
          actual ? actual->maximum : -999,
          expected.maximum,
          actual && actual->bins == expected.bins);
      throw std::runtime_error("GPU UI histogram differs from CPU oracle");
    }
    ++cases;
  };
  HistogramRequest histogram_request;
  histogram_request.layer = hist_layer.id;
  histogram_request.effect = hist_compare.id;
  histogram_request.mask = histogram_request.compare = true;
  for (int c = 0; c < 6; ++c)
    for (int left = 0; left < 4; ++left)
      for (int right = 0; right < 4; ++right) {
        hist_layer.mask->effects.back().compare_channel = c;
        hist_layer.mask->effects.back().left = left;
        hist_layer.mask->effects.back().right = right;
        histogram_request.channel = c;
        check_histogram(histogram_request);
      }
  LevelsEffect hist_levels;
  hist_levels.channel = 0;
  hist_layer.effects = {hist_levels};
  histogram_request.mask = histogram_request.compare = false;
  histogram_request.effect = hist_levels.id;
  histogram_request.channel = 0;
  for (int component = 0; component < 4; ++component) {
    histogram_request.component = component;
    histogram_request.curve.gamma = 1.8f;
    check_histogram(histogram_request);
  }
  // Prefix before Levels includes earlier content operations, but excludes the
  // selected effect even when it is disabled. Signed Auto endpoints survive.
  hist_layer.effects.front().channel = 4;
  hist_layer.values.bump = -.17f;
  histogram_request.channel = 4;
  histogram_request.component = 0;
  check_histogram(histogram_request);
  hist_layer.values.bump = 1.7f;
  check_histogram(histogram_request);
  hist_layer.effects.front().enabled = false;
  check_histogram(histogram_request);
  TextureDocument::add_user_channel(set, 0);
  TextureDocument::add_displacement_channel(set);
  hist_layer.users[0].enabled = hist_layer.displacement.enabled = true;
  hist_layer.users[0].value = -.37f;
  hist_layer.displacement.value = 1.7f;
  for (int channel : {6, 14}) {
    hist_layer.effects.front().channel = channel;
    histogram_request.channel = channel;
    check_histogram(histogram_request);
    hist_layer.visible = false;
    check_histogram(histogram_request);
    hist_layer.visible = true;
    auto &scalar = channel == 14 ? hist_layer.displacement : hist_layer.users[0];
    scalar.opacity = 0;
    check_histogram(histogram_request);
    scalar.opacity = 1;
    auto owners = std::make_shared<GeometryOwnershipTile>();
    owners->width = owners->height = 128;
    owners->owners.resize(128 * 128);
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        owners->owners[y * 128 + x] = x < 64 ? 11 : 42;
    set.geometry_ownership[1001] = owners;
    set.geometry_names = {{11, "left"}, {42, "right"}};
    hist_layer.geometry.meshes = {{"right", false}};
    check_histogram(histogram_request);
    hist_layer.geometry = {};
  }
  hist_layer.effects.front().channel = 4;
  histogram_request.channel = 4;
  MaskEffect hist_mask_levels;
  hist_mask_levels.kind = MaskEffectKind::Levels;
  hist_layer.mask->effects.push_back(hist_mask_levels);
  histogram_request.effect = hist_mask_levels.id;
  histogram_request.mask = true;
  check_histogram(histogram_request);
  // A Levels child sees its isolated sibling prefix, independent of both
  // ancestor mixes/backgrounds and later siblings at either nesting depth.
  {
    MaskEffect first;
    first.kind = MaskEffectKind::Fill;
    first.fill = .27f;
    MaskEffect later = first;
    later.id = QUuid::createUuid();
    later.fill = .93f;
    MaskEffect inner;
    inner.kind = MaskEffectKind::Group;
    inner.opacity = .23f;
    inner.blending = LayerBlend::Multiply;
    inner.group_effects = std::make_shared<const std::vector<MaskEffect>>(
        std::vector{first, hist_mask_levels, later});
    MaskEffect outer = inner;
    outer.id = QUuid::createUuid();
    MaskEffect before = later;
    before.id = QUuid::createUuid();
    MaskEffect after = later;
    after.id = QUuid::createUuid();
    outer.group_effects = std::make_shared<const std::vector<MaskEffect>>(
        std::vector{before, inner});
    hist_layer.mask->background = .81f;
    hist_layer.mask->effects = {outer, after};
    check_histogram(histogram_request);
    const auto nested = preview.histogram_result(hist_mask_levels.id);
    if (!nested || std::abs(nested->minimum - .27f) > 2e-5 ||
        std::abs(nested->maximum - .27f) > 2e-5 || nested->bins[0][17] != 768)
      throw std::runtime_error("Nested Levels histogram includes ancestor/later effects");
    std::printf(
        "BTS_GPU_NESTED_LEVELS_INPUT_VERIFIED samples=256 values=%u minimum=%.9g maximum=%.9g ancestor_isolation=1\n",
        nested->bins[0][17],
        nested->minimum,
        nested->maximum);
  }
  // Replacing an in-flight histogram may publish only the newest ticket.
  histogram_request.effect = hist_compare.id;
  histogram_request.compare = true;
  hist_layer.mask->effects = {hist_compare};
  hist_layer.mask->effects.back().left = 3;
  hist_layer.mask->effects.back().right = 3;
  preview.begin_frame();
  hist_layer.mask->effects.back().constant = .13f;
  if (preview.histogram(set, histogram_request, ++histogram_ticket) != GpuPreviewUpdate::Updated)
    throw std::runtime_error("First async histogram rejected");
  hist_layer.mask->effects.back().constant = .83f;
  if (preview.histogram(set, histogram_request, ++histogram_ticket) != GpuPreviewUpdate::Updated)
    throw std::runtime_error("Latest async histogram rejected");
  const auto hist_start = performance::Clock::now();
  while (preview.impl_->histograms.pending()) {
    preview.impl_->histograms.poll();
    if (performance::ms(hist_start) > 3000)
      throw std::runtime_error("Latest histogram timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto latest = preview.histogram_result(hist_compare.id);
  if (!latest || latest->ticket != histogram_ticket || latest->bins[0][52] != 256 ||
      !preview.impl_->histograms.discarded)
    throw std::runtime_error("Stale histogram published");
  ++cases;
  std::printf(
      "BTS_GPU_HISTOGRAM_VERIFIED cases=%d samples=256 packet_bytes=520 compare=1 levels=1 signed_auto=1 latest_wins=1 cpu_composition=0\n",
      cases - histogram_start);
  // Material picker reads the cached unlit authoring prefixes, never the
  // framebuffer or a lazily materialized CPU paint recipe.
  const int picker_start = cases;
  std::uint64_t picker_ticket = 0;
  set = TextureSet{};
  set.texture_resolution = 128;
  set.tiles = {1001, 1002};
  set.channels.fill(true);
  set.ao_channel = true;
  set.precision.fill(ChannelPrecision::RGB32F);
  set.base_values.base_color = {.23f, .41f, 1.7f, 1};
  set.base_values.normal = {.2f, .1f, .97f};
  auto &picker_fill = set.layers.front();
  picker_fill.kind = LayerKind::Fill;
  picker_fill.channels.fill(true);
  picker_fill.ao_enabled = true;
  picker_fill.values.base_color = {.71f, .11f, 2.4f, 1};
  picker_fill.values.normal = {.6f, .2f, .77f};
  picker_fill.values.bump = -.27f;
  picker_fill.values.ambient_occlusion = .31f;
  picker_fill.opacity.fill(.43f);
  picker_fill.ao_opacity = .61f;
  TextureDocument::add_paint(set);
  auto &picker_paint = set.layers.back();
  picker_paint.channels.fill(true);
  picker_paint.ao_enabled = true;
  picker_paint.opacity.fill(.71f);
  picker_paint.ao_opacity = .81f;
  picker_paint.mask = LayerMask{};
  picker_paint.mask->background = .37f;
  PaintPixels::Stamp picker_stamp;
  picker_stamp.center = {.3, .4};
  picker_stamp.axis_x = {.17, 0};
  picker_stamp.axis_y = {0, .13};
  picker_stamp.values.base_color = {.17f, 1.3f, .08f, 1};
  picker_stamp.values.metallic = .83f;
  picker_stamp.values.roughness = .21f;
  picker_stamp.values.bump = 1.7f;
  picker_stamp.channels.fill(true);
  picker_stamp.flow = .71f;
  {
    PaintRecordingScope recording(true);
    picker_paint.pixels.stamp(128, picker_stamp, set.tiles);
    picker_stamp.center = {1.7, .6};
    picker_paint.pixels.stamp(128, picker_stamp, set.tiles);
  }
  const auto picker_values = [](const MaterialValues &v) {
    return std::array<float, 11>{v.base_color[0],
                                 v.base_color[1],
                                 v.base_color[2],
                                 v.base_color[3],
                                 v.metallic,
                                 v.roughness,
                                 v.normal[0],
                                 v.normal[1],
                                 v.normal[2],
                                 v.bump,
                                 v.ambient_occlusion};
  };
  double picker_maximum = 0;
  const auto check_picker = [&](MaterialSampleRequest request) {
    preview.begin_frame();
    const auto reads = PaintPixels::recipe_materializations();
    if (preview.sample_material(set, request, ++picker_ticket) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU material pick deferred");
    if (PaintPixels::recipe_materializations() != reads)
      throw std::runtime_error("Material pick materialized CPU paint");
    if (preview.sample_material(set, request, picker_ticket + 1) != GpuPreviewUpdate::Pending)
      throw std::runtime_error("Material sample overwrote in-flight packet");
    const auto started = performance::Clock::now();
    while (preview.impl_->material_samples.pending()) {
      preview.impl_->material_samples.poll();
      if (performance::ms(started) > 3000)
        throw std::runtime_error("Material pick timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto actual = preview.material_sample_result();
    if (!actual || !actual->valid || actual->ticket != picker_ticket)
      throw std::runtime_error("Wrong material pick ticket");
    double error = 0;
    if (!request.mask.isNull())
      error = std::abs(actual->mask - TextureDocument::mask_value(
                                          set, request.mask, request.x, request.y, request.tile));
    else {
      const auto a = picker_values(actual->material),
                 b = picker_values(
                     TextureDocument::evaluate(set, request.x, request.y, request.tile).material);
      for (int c = 0; c < 11; ++c)
        error = std::max(error, double(std::abs(a[c] - b[c])));
    }
    picker_maximum = std::max(picker_maximum, error);
    if (error > 3e-5) {
      std::fprintf(stderr,
                   "BTS_GPU_MATERIAL_PICK_ORACLE tile=%d x=%d y=%d mask=%d error=%.9g\n",
                   request.tile,
                   request.x,
                   request.y,
                   !request.mask.isNull(),
                   error);
      throw std::runtime_error("GPU material pick differs from channel-precision CPU oracle");
    }
    ++cases;
  };
  for (int precision : {8, 6, 4, 2, 0}) {
    set.precision.fill(ChannelPrecision(precision));
    for (int tile : set.tiles)
      for (auto point :
           std::array<std::array<int, 2>, 4>{{{7, 11}, {38, 51}, {89, 76}, {123, 119}}})
      {
        MaterialSampleRequest request;
        request.tile = tile;
        request.x = point[0];
        request.y = point[1];
        check_picker(request);
        request.mask = picker_paint.id;
        check_picker(request);
      }
  }
  MaskEffect picker_compare;
  picker_compare.kind = MaskEffectKind::Compare;
  picker_compare.compare_channel = 2;
  picker_compare.left = 0;
  picker_compare.right = 3;
  picker_compare.constant = .2;
  picker_paint.mask->effects = {picker_compare};
  MaterialSampleRequest picker_request;
  picker_request.x = 38;
  picker_request.y = 51;
  picker_request.mask = picker_paint.id;
  set.precision.fill(ChannelPrecision::RGB32F);
  check_picker(picker_request);
  picker_paint.mask->enabled = false;
  check_picker(picker_request);
  picker_request.tile = 1003;
  if (preview.sample_material(set, picker_request, ++picker_ticket) !=
      GpuPreviewUpdate::Unsupported)
    throw std::runtime_error("Missing pick tile accepted");
  ++cases;
  std::printf(
      "BTS_GPU_MATERIAL_PICK_VERIFIED cases=%d packet_bytes=96 udim=2 precision_modes=5 signed_hdr=1 mask_compare=1 in_flight_guard=1 cpu_composition=0 max_error=%.9g\n",
      cases - picker_start,
      picker_maximum);
  const int thumbnail_start = cases;
  std::uint64_t thumbnail_ticket = 0;
  double thumbnail_maximum = 0;
  set = TextureSet{};
  set.texture_resolution = 128;
  set.tiles = {1001, 1002, 1011};
  set.channels.fill(true);
  set.ao_channel = true;
  set.precision.fill(ChannelPrecision::RGB32F);
  set.users[0].present = true;
  set.displacement.channel = set.displacement.allocated = true;
  auto &thumbnail_layer = set.layers.front();
  thumbnail_layer.kind = LayerKind::Paint;
  thumbnail_layer.channels.fill(true);
  thumbnail_layer.ao_enabled = true;
  thumbnail_layer.users[0].enabled = true;
  thumbnail_layer.displacement.enabled = true;
  picker_stamp.center = {.25, .5};
  picker_stamp.axis_x = {.21, 0};
  picker_stamp.axis_y = {0, .31};
  picker_stamp.values.normal = {.2f, .6f, .77f};
  {
    PaintRecordingScope recording(true);
    for (const auto center :
         std::array<std::array<double, 2>, 3>{{{.25, .5}, {1.7, .6}, {.7, 1.3}}})
    {
      picker_stamp.center = center;
      thumbnail_layer.pixels.stamp(128, picker_stamp, set.tiles);
      auto scalar_stamp = picker_stamp;
      scalar_stamp.channels = {false, false, true, false, false};
      scalar_stamp.values.roughness = -.23f;
      thumbnail_layer.ao_pixels.stamp(128, scalar_stamp, set.tiles);
      thumbnail_layer.users[0].pixels.stamp(128, scalar_stamp, set.tiles);
      thumbnail_layer.displacement.pixels.stamp(128, scalar_stamp, set.tiles);
    }
  }
  thumbnail_layer.mask = LayerMask{};
  thumbnail_layer.mask->background = .27f;
  MaskEffect thumbnail_levels;
  thumbnail_levels.kind = MaskEffectKind::Levels;
  thumbnail_levels.levels.gamma = 1.7;
  thumbnail_layer.mask->effects = {thumbnail_levels};
  const auto check_thumbnail = [&](ThumbnailRequest request) {
    preview.begin_frame();
    const auto before = PaintPixels::recipe_materializations();
    if (preview.thumbnail(set, request, ++thumbnail_ticket) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU thumbnail deferred");
    if (PaintPixels::recipe_materializations() != before)
      throw std::runtime_error("Thumbnail materialized CPU paint");
    const auto started = performance::Clock::now();
    while (preview.impl_->thumbnails.pending()) {
      preview.impl_->thumbnails.poll();
      if (performance::ms(started) > 3000)
        throw std::runtime_error("Thumbnail fence timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto actual = preview.thumbnail_result();
    if (!actual || actual->ticket != thumbnail_ticket || !actual->valid)
      throw std::runtime_error("Wrong thumbnail ticket");
    auto oracle = set;
    auto &own = oracle.layers.front();
    own.visible = true;
    own.opacity.fill(1);
    own.ao_opacity = 1;
    own.displacement.opacity = 1;
    own.users[0].opacity = 1;
    own.blending.fill(LayerBlend::Normal);
    own.ao_blending = own.displacement.blending = own.users[0].blending = LayerBlend::Normal;
    own.mask.reset();
    LevelsEffect sentinel;
    sentinel.channel = request.channel;
    own.effects.push_back(sentinel);
    const std::vector<int> tiles(set.tiles.begin(), set.tiles.end());
    double maximum = 0;
    for (int y = 0; y < 48; ++y)
      for (int x = 0; x < 48; ++x) {
        const int index = (y * 2 / 48) * 2 + x * 2 / 48;
        std::array<float, 4> expected{};
        if (index < int(tiles.size())) {
          const int tile = tiles[index],
                    px = int(std::fmod((x + .5) * 2 / 48, 1.) * set.width(tile)),
                    py = int((1 - std::fmod((y + .5) * 2 / 48, 1.)) * set.rows(tile));
          if (request.mask) {
            const float v = TextureDocument::mask_value(set, request.layer, px, py, tile);
            expected = {v, v, v, 1};
          }
          else {
            const auto rgb = TextureDocument::effect_input(oracle, sentinel.id, px, py, tile);
            float alpha;
            if (request.channel == 14)
              alpha = TextureDocument::composite_displacement(oracle, px, py, tile)[1];
            else if (request.channel >= 6)
              alpha = TextureDocument::composite_user(
                  oracle, request.channel - 6, px, py, tile)[1];
            else {
              const auto c = TextureDocument::evaluate(oracle, px, py, tile);
              alpha = request.channel == 5 ? c.ao_coverage : c.coverage[request.channel];
            }
            expected = {rgb[0], rgb[1], rgb[2], alpha};
          }
        }
        const auto &pixel = actual->pixels[y * 48 + x];
        for (int c = 0; c < 4; ++c)
          maximum = std::max(maximum, double(std::abs(pixel[c] - expected[c])));
      }
    thumbnail_maximum = std::max(thumbnail_maximum, maximum);
    if (maximum > 3e-5) {
      std::fprintf(stderr,
                   "BTS_GPU_THUMBNAIL_ORACLE channel=%d mask=%d error=%.9g\n",
                   request.channel,
                   request.mask,
                   maximum);
      throw std::runtime_error("GPU thumbnail differs from source CPU oracle");
    }
    ++cases;
  };
  for (int channel : {0, 1, 2, 3, 4, 5, 6, 14}) {
    thumbnail_layer.visible = true;
    thumbnail_layer.opacity.fill(.61f);
    thumbnail_layer.blending.fill(LayerBlend::Normal);
    check_thumbnail({thumbnail_layer.id, channel, false});
    thumbnail_layer.visible = false;
    thumbnail_layer.opacity.fill(0);
    thumbnail_layer.ao_opacity = thumbnail_layer.users[0].opacity =
        thumbnail_layer.displacement.opacity = 0;
    thumbnail_layer.blending.fill(LayerBlend::Disabled);
    thumbnail_layer.ao_blending = thumbnail_layer.users[0].blending =
        thumbnail_layer.displacement.blending = LayerBlend::Disabled;
    check_thumbnail({thumbnail_layer.id, channel, false});
  }
  check_thumbnail({thumbnail_layer.id, 0, true});
  thumbnail_layer.mask->enabled = false;
  check_thumbnail({thumbnail_layer.id, 0, true});
  // A thumbnail must not overwrite an already-returned Compare source in
  // the same GPU frame (especially Passthrough or zero-opacity content effects).
  for (bool zero_opacity : {false, true}) {
    set = TextureSet{};
    set.texture_resolution = 128;
    set.channels.fill(true);
    set.precision.fill(ChannelPrecision::RGB32F);
    set.layers.front().kind = LayerKind::Fill;
    set.layers.front().values.base_color = {.2f, .4f, .6f, 1};
    TextureDocument::add_paint(set);
    auto &own = set.layers.back();
    own.blending[0] = zero_opacity ? LayerBlend::Normal : LayerBlend::Passthrough;
    own.opacity[0] = zero_opacity ? 0 : .6f;
    picker_stamp.center = {.3, .4};
    picker_stamp.channels = {true, false, false, false, false};
    picker_stamp.flow = .5f;
    own.pixels.stamp(128, picker_stamp, set.tiles);
    if (zero_opacity) {
      LevelsEffect paint;
      paint.kind = ContentEffectKind::Paint;
      paint.opacity = .4f;
      auto effect_stamp = picker_stamp;
      effect_stamp.values.base_color = {.8f, .2f, .1f, 1};
      paint.paint.pixels.stamp(128, effect_stamp, set.tiles);
      own.effects = {paint};
    }
    preview.begin_frame();
    GpuMaskStage stage;
    stage.comparison_layer = own.id;
    stage.comparison_channel = 0;
    TextureLayer compare_description;
    compare_description.mask = LayerMask{};
    MaskEffect compare_effect;
    compare_effect.kind = MaskEffectKind::Compare;
    compare_effect.left = 0;
    compare_effect.right = 1;
    compare_effect.compare_channel = 0;
    compare_description.mask->effects = {compare_effect};
    gpu_append_mask_effects(stage.rows, compare_description, set, 1001);
    auto inputs = preview.comparison_inputs(set, stage, preview.impl_->zero.get(), 1001);
    if (!inputs)
      throw std::runtime_error("Thumbnail isolation comparison missing");
    const std::array<std::array<int, 3>, 1> point{{{38, 51, 0}}};
    const auto before = probe_gpu_texture((*inputs)[1].image, point).front();
    if (preview.thumbnail(set, {own.id, 0, false}, ++thumbnail_ticket) !=
        GpuPreviewUpdate::Updated)
      throw std::runtime_error("Thumbnail isolation deferred");
    const auto after = probe_gpu_texture((*inputs)[1].image, point).front();
    if (before != after)
      throw std::runtime_error("Thumbnail overwrote live Compare input");
    const auto started = performance::Clock::now();
    while (preview.impl_->thumbnails.pending()) {
      preview.impl_->thumbnails.poll();
      if (performance::ms(started) > 3000)
        throw std::runtime_error("Thumbnail isolation timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ++cases;
  }
#include "GpuDeferredThumbnailTest.inc"
  std::printf(
      "BTS_GPU_THUMBNAIL_VERIFIED cases=%d pixels_per_case=2304 udim=3 channels=8 hidden_zero_opacity_disabled=1 raw_mask=1 same_frame_compare_isolation=1 cpu_composition=0 max_error=%.9g\n",
      cases - thumbnail_start,
      thumbnail_maximum);
  // Independent scalar mask prefixes: interleaved paint/point effects, exact
  // ordering, shared across channels, dirty chunks and cached Undo sources.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.front().kind = LayerKind::Fill;
  TextureDocument::add_fill(set);
  auto &paint_mask_layer = set.layers.back();
  paint_mask_layer.values.base_color = {.7f, .2f, .4f, 1};
  paint_mask_layer.opacity.fill(.71f);
  paint_mask_layer.mask = LayerMask{};
  paint_mask_layer.mask->background = .31f;
  MaskEffect first_paint;
  first_paint.kind = MaskEffectKind::Paint;
  first_paint.opacity = .73f;
  auto effect_ink = mask;
  effect_ink.center = {.2, .2};
  effect_ink.axis_x = {.08, 0};
  effect_ink.axis_y = {0, .08};
  first_paint.paint.pixels.stamp(512, effect_ink);
  MaskEffect paint_levels;
  paint_levels.kind = MaskEffectKind::Levels;
  paint_levels.levels.gamma = 1.3;
  paint_levels.levels.output_low = -.2;
  paint_levels.levels.output_high = 1.4;
  paint_levels.levels.clamp = false;
  MaskEffect second_paint;
  second_paint.kind = MaskEffectKind::Paint;
  second_paint.opacity = .61f;
  effect_ink.center = {.3, .3};
  effect_ink.values.roughness = .18;
  second_paint.paint.pixels.stamp(256, effect_ink);
  paint_mask_layer.mask->effects = {first_paint, paint_levels, second_paint};
  target = texture("BTS GPU mask paint effects", 512, 512, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (int blend = 0; blend <= 28; ++blend) {
    paint_mask_layer.mask->effects[2].blending = LayerBlend(blend);
    check(0);
    check(2);
  }
  paint_mask_layer.mask->effects[2].blending = LayerBlend::Normal;
  check(0);
  const auto shared_work = preview.impl_->mask_dispatch_texels;
  check(1);
  check(2);
  check(4);
  if (preview.impl_->mask_dispatch_texels != shared_work)
    throw std::runtime_error("Mask prefixes duplicated per channel");
  const auto saved_mask_effects = paint_mask_layer.mask->effects;
  auto &last_paint = paint_mask_layer.mask->effects[2];
  effect_ink.center = {.05, .05};
  effect_ink.axis_x = {.01, 0};
  effect_ink.axis_y = {0, .01};
  last_paint.paint.pixels.stamp(256, effect_ink);
  const auto dirty_before = preview.impl_->mask_dispatch_texels;
  check(0);
  const auto dirty_mask = preview.impl_->mask_dispatch_texels - dirty_before;
  if (dirty_mask > 256 * 256 || dirty_mask == 0)
    throw std::runtime_error("Mask paint recomputed unchanged lower prefixes/chunks");
  paint_mask_layer.mask->effects = saved_mask_effects;
  check(0);
  paint_mask_layer.mask->effects[0].enabled = false;
  check(0);
  paint_mask_layer.mask->effects[0].enabled = true;
  check(0);
  std::swap(paint_mask_layer.mask->effects[0], paint_mask_layer.mask->effects[2]);
  check(0);
  paint_mask_layer.mask->effects[0].paint.pixels = {};
  check(0);
  paint_mask_layer.mask->effects.erase(paint_mask_layer.mask->effects.begin());
  check(0);
  paint_mask_layer.mask.reset();
  check(0);
  for (const auto &[key, entry] : preview.impl_->mask_stacks)
    if (key.starts_with(set.id.toString().toStdString()))
      throw std::runtime_error("Deleted mask retained GPU prefixes");
  std::printf(
      "BTS_GPU_MASK_PAINT_VERIFIED modes=29 interleaved_levels=1 resampled=1 shared_channels=4 dirty_texels=%llu undo_reorder_clear=1 production_readback=0\n",
      (unsigned long long)dirty_mask);

  // Atom masks use their own wrap/filter contract (different from fill maps),
  // both inline and between cached paint-effect prefixes, including UDIM offsets.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.layers.front().kind = LayerKind::Fill;
  TextureDocument::add_fill(set);
  auto &atom_layer = set.layers.back();
  atom_layer.values.base_color = {.17f, .48f, .73f, 1};
  atom_layer.opacity.fill(.71f);
  atom_layer.mask = LayerMask{};
  atom_layer.mask->background = .43;
  MaskEffect atom_mask;
  atom_mask.kind = MaskEffectKind::Fill;
  atom_mask.fill = .77f;
  atom_mask.opacity = .83f;
  atom_mask.blending = LayerBlend::Multiply;
  atom_mask.mapping.projection.atom = true;
  atom_mask.mapping.projection.scale = {2.3, .7};
  atom_mask.mapping.projection.offset = {-.2, .3};
  atom_mask.mapping.projection.rotation = 33;
  target = texture("BTS GPU Atom mask oracle", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (bool staged : {false, true})
    for (int wrap = 0; wrap < 3; ++wrap)
      for (int filter = 0; filter < 2; ++filter)
        for (bool invert : {false, true}) {
          atom_mask.mapping.wrap = wrap;
          atom_mask.mapping.filtering = filter;
          atom_mask.mapping.projection.invert = invert;
          atom_layer.mask->effects = {atom_mask};
          if (staged)
            atom_layer.mask->effects.insert(atom_layer.mask->effects.begin(), first_paint);
          for (int channel : {0, 2})
            check(channel);
        }
  set.tiles = {1001, 1002, 1011};
  atom_layer.mask->effects = {first_paint, atom_mask, paint_levels};
  for (int tile : set.tiles) {
    preview.begin_frame();
    if (preview.update(set, 0, target.get(), tile) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU UDIM mask did not execute");
    ++cases;
  }
  atom_layer.mask->effects[1].mapping.projection.physical = true;
  set.cm_per_native = 100;
  set.native_per_uv = {1.4, .6};
  check(0);
  std::printf(
      "BTS_GPU_MASK_ATOM_VERIFIED wrap=repeat,clamp,none filters=bilinear,nearest inline_and_paint_prefix=1 tiles=1001,1002,1011 physical=1\n");
  // Reuse the same shader projection contract for mask Fill sources.
  set = TextureSet{};
  set.texture_resolution = 128;
  set.layers.front().kind = LayerKind::Fill;
  TextureDocument::add_fill(set);
  set.projection_geometry = atlas;
  auto &spatial_mask_layer = set.layers.back();
  spatial_mask_layer.values.base_color = {.18f, .5f, .76f, 1};
  spatial_mask_layer.values.roughness = .2;
  spatial_mask_layer.mask = LayerMask{};
  spatial_mask_layer.mask->background = .8;
  MaskEffect spatial_mask;
  spatial_mask.kind = MaskEffectKind::Fill;
  spatial_mask.fill = .7;
  spatial_mask.opacity = .8;
  spatial_mask.blending = LayerBlend::Multiply;
  spatial_mask.mapping.projection.rotation3 = {13, 21, -9};
  spatial_mask.mapping.projection.scale3 = {1.3, .8, .7};
  spatial_mask.mapping.projection.scale = {1.4, .9};
  spatial_mask.mapping.projection.offset = {.17, -.21};
  spatial_mask.mapping.projection.backface_culling = true;
  for (int mode = 1; mode <= 4; ++mode)
    for (bool atom : {false, true})
      for (int filter : {0, 1, 2}) {
        spatial_mask.mapping.projection.mode = mode;
        spatial_mask.mapping.projection.atom = atom;
        spatial_mask.mapping.projection.filtering = filter;
        spatial_mask_layer.mask->effects = {first_paint, spatial_mask, paint_levels};
        for (int c : {0, 2})
          check(c);
      }
  spatial_mask_layer.mask->effects = {spatial_mask};
  check(0);
  spatial_mask_layer.mask->effects[0].mapping.projection.physical = true;
  set.cm_per_native = 100;
  check(0);
  auto mask_atlas = std::make_shared<FillSurfaceAtlas>(*atlas);
  mask_atlas->tiles[1001][0].normal = {0, 0, 0};
  mask_atlas->tiles[1002] = mask_atlas->tiles.at(1001);
  set.projection_geometry = mask_atlas;
  set.tiles = {1001, 1002};
  spatial_mask_layer.mask->effects[0].mapping.projection.mode = 1;
  spatial_mask_layer.mask->effects[0].mapping.projection.atom = false;
  for (int tile : set.tiles) {
    preview.begin_frame();
    if (preview.update(set, 0, target.get(), tile) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("GPU spatial mask UDIM did not execute");
    ++cases;
  }
  spatial_mask_layer.mask->effects = {first_paint, spatial_mask};
  spatial_mask_layer.mask->effects[1].blending = LayerBlend::Disabled;
  set.projection_geometry.reset();
  check(0);
  std::printf(
      "BTS_GPU_MASK_SPATIAL_VERIFIED modes=triplanar,planar,spherical,cylindrical atom_uniform=1 filters=3 paint_prefix=1 physical=1 udim=1 disabled_no_geometry=1\n");
  // Live anchors refer to earlier mask prefixes, including hidden/disabled
  // producer masks. They clamp the exposed value without clamping its stream.
  set = TextureSet{};
  set.texture_resolution = 512;
  set.layers.clear();
  TextureLayer anchor_producer;
  anchor_producer.kind = LayerKind::Fill;
  anchor_producer.visible = false;
  anchor_producer.mask = LayerMask{};
  anchor_producer.mask->background = .2;
  anchor_producer.mask->enabled = false;
  MaskEffect anchor_a;
  anchor_a.kind = MaskEffectKind::Anchor;
  MaskEffect anchor_b = anchor_a;
  anchor_b.id = QUuid::createUuid();
  MaskEffect after_anchor;
  after_anchor.kind = MaskEffectKind::Filter;
  after_anchor.filter.resource = 1;
  anchor_producer.mask->effects = {first_paint, anchor_a, after_anchor};
  set.layers.push_back(anchor_producer);
  TextureLayer anchor_middle;
  anchor_middle.kind = LayerKind::Fill;
  anchor_middle.values.base_color = {.7f, .2f, .1f, 1};
  anchor_middle.opacity.fill(.65);
  anchor_middle.mask = LayerMask{};
  MaskEffect reference_a;
  reference_a.kind = MaskEffectKind::Fill;
  reference_a.anchor_source = anchor_a.id;
  reference_a.opacity = .73;
  anchor_middle.mask->effects = {reference_a, anchor_b, paint_levels};
  set.layers.push_back(anchor_middle);
  TextureLayer anchor_top;
  anchor_top.kind = LayerKind::Fill;
  anchor_top.values.base_color = {.1f, .3f, .8f, 1};
  anchor_top.opacity.fill(.7);
  anchor_top.mask = LayerMask{};
  MaskEffect reference_b = reference_a;
  reference_b.id = QUuid::createUuid();
  reference_b.anchor_source = anchor_b.id;
  reference_b.blending = LayerBlend::Multiply;
  anchor_top.mask->effects = {reference_a, reference_b};
  set.layers.push_back(anchor_top);
  target = texture("BTS GPU live mask anchors", 512, 512, gpu::TextureFormat::SFLOAT_16_16_16_16);
  check(0);
  const auto anchored_work = preview.impl_->mask_dispatch_texels;
  check(2);
  check(4);
  if (preview.impl_->mask_dispatch_texels != anchored_work)
    throw std::runtime_error("Anchor producers were recomputed per channel");
  set.layers[0].mask->effects[2].filter.resource = 2;
  check(0);
  if (preview.impl_->mask_dispatch_texels != anchored_work)
    throw std::runtime_error("Post-anchor change invalidated its consumers");
  auto anchor_before = set.layers[0].mask->effects[0].paint.pixels;
  auto anchor_ink = mask;
  anchor_ink.center = {.05, .05};
  anchor_ink.axis_x = {.01, 0};
  anchor_ink.axis_y = {0, .01};
  set.layers[0].mask->effects[0].paint.pixels.stamp(512, anchor_ink);
  const auto anchor_dirty_start = preview.impl_->mask_dispatch_texels;
  check(0);
  const auto anchor_dirty = preview.impl_->mask_dispatch_texels - anchor_dirty_start;
  if (anchor_dirty == 0 || anchor_dirty >= 512 * 512 * 8)
    throw std::runtime_error("Anchor edit did not propagate sparse dirty chunks");
  set.layers[0].mask->effects[0].paint.pixels = anchor_before;
  check(0);
  set.layers[0].mask->effects[1].enabled = false;
  check(0);
  set.layers[0].mask->effects[1].enabled = true;
  check(0);
  set.layers[0].mask->background = 1;
  set.layers[0].mask->effects.insert(set.layers[0].mask->effects.begin(), paint_levels);
  check(0);
  set.layers[0].mask->enabled = true;
  check(0);
  set.layers[0].visible = true;
  check(0);
  const auto producer_saved = set.layers[0];
  set.layers[0].mask.reset();
  check(0);
  set.layers[0] = producer_saved;
  check(0);
  set.layers[2].mask->effects[0].anchor_source = QUuid::createUuid();
  check(0);
  set.layers[2].mask->effects[0].anchor_source = anchor_a.id;
  check(0);
  std::swap(set.layers[0], set.layers[2]);
  check(0);
  std::swap(set.layers[0], set.layers[2]);
  check(0);
  std::printf(
      "BTS_GPU_MASK_ANCHORS_VERIFIED nested=1 shared_channels=3 hidden_disabled_producer=1 post_anchor_unchanged=1 dirty_texels=%llu undo_missing_forward_reorder=1 clamped_reference=1\n",
      (unsigned long long)anchor_dirty);
  // Isolation reuses the exact cached mask stream, even on disabled/hidden layers.
  for (int i : {0, 1, 2, 0}) {
    set.selected_layer = set.layers[i].id;
    set.layers[i].mask->enabled = false;
    check(15);
  }
  check(15);
  if (preview.wrote_image())
    throw std::runtime_error("Unchanged mask inspection uploaded its image");
  set.layers[0].mask->effects.back().enabled = false;
  check(15);
  set.layers[0].mask->effects.back().enabled = true;
  check(15);
  set.selected_layer = set.layers[0].id;
  check(15);
  const auto isolation_before = preview.impl_->mask_dispatch_texels;
  set.layers[0].mask->effects[1].paint.pixels.stamp(512, anchor_ink);
  check(15);
  const auto isolation_dirty = preview.impl_->mask_dispatch_texels - isolation_before;
  if (!isolation_dirty || isolation_dirty > 256 * 256 * 4)
    throw std::runtime_error("Mask inspection lost sparse prefix reuse");
  set.layers[0].mask->effects.clear();
  set.layers[0].mask->background = .37;
  check(15);
  set.layers[0].mask->effects = {first_paint, atom_mask, paint_levels};
  set.tiles = {1001, 1002, 1011};
  set.texture_resolution = 128;
  target = Texture(GPU_texture_create_2d_array("BTS isolated masks atlas",
                                               384,
                                               128,
                                               1,
                                               1,
                                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                                               GPU_TEXTURE_USAGE_GENERAL,
                                               nullptr),
                   GPU_texture_free);
  const std::map<int, std::array<int, 3>> isolated_regions{
      {1001, {0, 0, 0}}, {1002, {128, 0, 0}}, {1011, {256, 0, 0}}};
  preview.begin_frame();
  if (preview.update_tiles(set, 15, target.get(), isolated_regions) != GpuPreviewUpdate::Updated)
    throw std::runtime_error("Isolated mask UDIM atlas did not execute");
  ++cases;
  set.projection_geometry = mask_atlas;
  set.layers[0].mask->effects = {spatial_mask};
  set.tiles = {1001, 1002};
  preview.begin_frame();
  if (preview.update_tiles(set, 15, target.get(), {{1001, {0, 0, 0}}, {1002, {128, 0, 0}}}) !=
      GpuPreviewUpdate::Updated)
    throw std::runtime_error("Spatial mask isolation did not execute");
  ++cases;
  std::printf(
      "BTS_GPU_MASK_ISOLATION_VERIFIED disabled_hidden=1 selection=1 unchanged_upload=0 dirty_texels=%llu uniform_paint_levels_atom_spatial_anchors=1 udim_atlas=1 production_readback=0\n",
      (unsigned long long)isolation_dirty);
#include "GpuContentAnchorTests.inc"
#include "GpuFillAnchorTests.inc"
#include "GpuGeneratorAnchorTests.inc"
  // Temporary Quick Mask overlay consumes the original scalar pages on GPU.
  {
    TextureSet original;
    original.texture_resolution = 512;
    original.tiles = {1001, 1002, 1011};
    original.layers[0].kind = LayerKind::Fill;
    original.layers[0].values.base_color = {.7f, .3f, .1f, 1};
    QuickMaskField field;
    field.resolution = 256;
    auto stripe = ink;
    stripe.channels = {false, false, true, false, false};
    stripe.values.roughness = 0;
    stripe.center = {.45, .5};
    stripe.axis_x = {.04, 0};
    stripe.axis_y = {0, .3};
    field.pixels.stamp(256, stripe, original.tiles);
    stripe.center = {1.6, .5};
    field.pixels.stamp(256, stripe, original.tiles);
    for (int width : {128, 200, 512})
      for (bool editing : {false, true})
        for (bool inverted : {false, true}) {
          original.texture_resolution = width;
          original.texture_height = width == 200 ? 203 : 0;
          field.inverted = inverted;
          set = quick_mask_preview(original, &field, editing);
          target = texture("BTS quick mask virtual source",
                           width,
                           set.rows(),
                           gpu::TextureFormat::SFLOAT_32_32_32_32);
          for (int tile : original.tiles) {
            preview.begin_frame();
            if (preview.update_tile(set, 0, target.get(), tile, {}) != GpuPreviewUpdate::Updated)
              throw std::runtime_error("Quick mask GPU tile deferred");
            ++cases;
          }
        }
    original.texture_resolution = 1024;
    original.texture_height = 0;
    field.inverted = false;
    set = quick_mask_preview(original, &field, true);
    target = texture(
        "BTS quick mask dirty prefix", 1024, 1024, gpu::TextureFormat::SFLOAT_32_32_32_32);
    check(0);
    check(0);
    if (preview.impl_->last_dispatch_texels || preview.impl_->remaining != upload_budget)
      throw std::runtime_error("Quick mask unchanged work");
    const auto saved = field;
    stripe.center = {.249, .249};
    stripe.axis_x = {.006, 0};
    stripe.axis_y = {0, .006};
    {
      PaintRecordingScope record;
      field.pixels.stamp(256, stripe, original.tiles);
    }
    set = quick_mask_preview(original, &field, true);
    check(0);
    const auto dirty = preview.impl_->last_dispatch_texels;
    if (!dirty || dirty >= 1024 * 1024 * 2)
      throw std::runtime_error("Quick mask did not retain lower prefix");
    set = quick_mask_preview(original, &saved, true);
    check(0);
    set = original;
    check(0);
    set = quick_mask_preview(original, nullptr, true);
    check(0);
    set = quick_mask_preview(original, nullptr, false);
    check(0);
    std::printf(
        "BTS_GPU_QUICK_MASK_VERIFIED tiles=3 virtual_256_source=1 full_resolution_target=1 inversion=1 dirty_texels=%llu unchanged_upload=0 undo=1 production_readback=0\n",
        (unsigned long long)dirty);
  }
  // GPU bounds/presence use dirty chunks and a fenced 16-byte result, never pixels.
  {
    GpuRangeSummary ranges;
    GpuHistogram histograms;
    std::map<std::uint32_t, std::uint64_t> displacement_epochs;
    std::vector<float> a(512 * 512 * 4), b(512 * 256 * 4);
    for (std::size_t i = 0; i < a.size(); i += 4) {
      a[i] = .25f;
      a[i + 3] = 1;
    }
    a[40 * 4] = -.001f;
    b[10 * 4] = 2;
    b[10 * 4 + 3] = 1;
    b[300 * 4] = .7;
    b[300 * 4 + 3] = 1;
    b[11 * 4] = 200;  // Zero coverage must not affect bounds.
    auto first = texture("BTS range source A", 512, 512, gpu::TextureFormat::SFLOAT_32_32_32_32);
    auto second = texture("BTS range source B", 512, 256, gpu::TextureFormat::SFLOAT_32_32_32_32);
    GPU_texture_update(first.get(), GPU_DATA_FLOAT, a.data());
    GPU_texture_update(second.get(), GPU_DATA_FLOAT, b.data());
    std::vector<GpuRangeSummary::Source> sources{{23, 512, 512, first.get(), 1, {}},
                                                 {1002, 512, 256, second.get(), 1, {}}};
    sources[0].tile = 1001;
    const auto wait_range = [&]() {
      const auto start = performance::Clock::now();
      std::vector<GpuRangeSummary::Result> result;
      while (ranges.pending()) {
        auto ready = ranges.poll();
        result.insert(result.end(), ready.begin(), ready.end());
        if (performance::ms(start) > 3000)
          throw std::runtime_error("GPU range metadata timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (result.empty())
        throw std::runtime_error("Missing async GPU range");
      return result.back();
    };
    ranges.update(23, sources);
    auto range = wait_range();
    if (!range.present || std::abs(range.minimum + .001f) > 1e-7 || range.maximum != 2)
      throw std::runtime_error("Signed GPU range/coverage incorrect");
    const auto full = ranges.reduced_texels;
    ranges.update(23, sources);
    if (ranges.pending() || ranges.reduced_texels != full)
      throw std::runtime_error("Unchanged GPU range was recomputed");
    std::vector<float> dirty(256 * 256 * 4);
    GPU_texture_update_sub(second.get(), GPU_DATA_FLOAT, dirty.data(), 0, 0, 0, 256, 256, 1);
    sources[1].version = 2;
    sources[1].dirty = {0};
    ranges.update(23, sources);
    for (std::size_t i = 0; i < dirty.size(); i += 4) {
      dirty[i] = -.5;
      dirty[i + 3] = 1;
    }
    GPU_texture_update_sub(second.get(), GPU_DATA_FLOAT, dirty.data(), 0, 0, 0, 256, 256, 1);
    sources[1].version = 3;
    ranges.update(23, sources);
    range = wait_range();
    if (range.minimum != -.5f || std::abs(range.maximum - .7f) > 1e-7 || ranges.discarded != 1 ||
        ranges.reduced_texels - full != 2 * 256 * 256)
      throw std::runtime_error("GPU dirty range/coalescing mismatch");
    sources.resize(1);
    std::fill(a.begin(), a.end(), 0);
    GPU_texture_update(first.get(), GPU_DATA_FLOAT, a.data());
    sources[0].version = 2;
    sources[0].dirty = {0, 1, 2, 3};
    ranges.update(23, sources);
    range = wait_range();
    if (range.present)
      throw std::runtime_error("GPU range clear retained coverage");
    const auto before_epoch = ranges.reduced_texels;
    ranges.update(23, sources, 7);
    range = wait_range();
    if (range.epoch != 7 || ranges.reduced_texels != before_epoch)
      throw std::runtime_error("GPU range CPU-save epoch transition lost metadata");
    std::printf(
        "BTS_GPU_RANGE_VERIFIED tiles=2 signed_unbounded=1 zero_alpha_ignored=1 dirty_texels=%llu discarded_stale=%llu metadata_bytes=%llu unchanged_dispatch=0 pixel_readback=0\n",
        (unsigned long long)(2 * 256 * 256),
        (unsigned long long)ranges.discarded,
        (unsigned long long)ranges.readback_bytes);
    cases += 5;
  }
  // Per-effect GPU source prefixes preserve coverage, encoded normals and ordering.
  for (auto parent_kind : {LayerKind::Fill, LayerKind::Paint, LayerKind::Folder}) {
    set = TextureSet{};
    set.texture_resolution = 128;
    set.channels.fill(true);
    set.ao_channel = true;
    set.displacement.channel = true;
    set.users[0].present = true;
    set.layers.clear();
    TextureLayer lower;
    lower.kind = LayerKind::Fill;
    lower.values.base_color = {.2f, .3f, .1f, 1};
    lower.opacity.fill(.43);
    set.layers.push_back(lower);
    if (parent_kind == LayerKind::Folder) {
      auto child = lower;
      child.id = QUuid::createUuid();
      child.depth = 1;
      child.values.base_color = {.6f, .1f, .3f, 1};
      set.layers.push_back(child);
    }
    TextureLayer painted;
    painted.kind = parent_kind;
    painted.values.base_color = {.3f, .7f, .4f, 1};
    painted.values.bump = -.2;
    painted.values.roughness = .61;
    painted.opacity.fill(.71);
    painted.ao_enabled = true;
    painted.ao_opacity = .6;
    painted.values.ambient_occlusion = .7;
    painted.displacement.enabled = true;
    painted.displacement.value = -.3;
    painted.displacement.opacity = .7;
    painted.users[0].enabled = true;
    painted.users[0].value = .2;
    auto content_ink = effect_ink;
    content_ink.center = {.5, .5};
    content_ink.axis_x = {.35, 0};
    content_ink.axis_y = {0, .35};
    content_ink.channels.fill(true);
    content_ink.values.base_color = {.7f, .1f, .4f, 1};
    content_ink.values.bump = -.13;
    content_ink.values.normal = {.2f, .3f, .93f};
    content_ink.values.roughness = .28;
    painted.pixels.stamp(128, content_ink);
    painted.ao_pixels.stamp(128, content_ink);
    painted.users[0].pixels.stamp(128, content_ink);
    painted.displacement.pixels.stamp(128, content_ink);
    set.layers.push_back(painted);
    target = texture(
        "BTS GPU content paint effects", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
    for (int c : {0, 1, 2, 4, 5, 6, 7, 16}) {
      auto &layer = set.layers.back();
      const int effect_channel = gpu_layer_channel(layer, c).fill_channel;
      LevelsEffect pre;
      pre.channel = effect_channel;
      pre.gamma = 1.2;
      pre.opacity = .83;
      LevelsEffect ink;
      ink.kind = ContentEffectKind::Paint;
      ink.channel = 0;
      ink.opacity = .67;
      ink.paint.pixels = painted.pixels;
      ink.paint.ao = painted.ao_pixels;
      ink.paint.users[0] = painted.users[0].pixels;
      ink.paint.displacement = painted.displacement.pixels;
      LevelsEffect post = pre;
      post.id = QUuid::createUuid();
      post.output_low = -.1;
      post.output_high = 1.2;
      post.clamp = false;
      layer.effects = {pre, ink, post};
      for (int mode = 0; mode <= 28; ++mode) {
        layer.effects[1].blending = LayerBlend(mode);
        try {
          check(c);
        }
        catch (...) {
          std::fprintf(
              stderr,
              "BTS_GPU_CONTENT_PAINT_FAILURE parent=%d channel=%d paint_blend=%d pre_opacity=%.9g post_opacity=%.9g\n",
              int(parent_kind),
              c,
              mode,
              pre.opacity,
              post.opacity);
          throw;
        }
      }
      layer.effects[1].paint = {};
      layer.effects[1].blending = LayerBlend::Replace;
      check(c);
      layer.effects[1] = ink;
      std::swap(layer.effects[0], layer.effects[1]);
      check(c);
      layer.effects[0].enabled = false;
      check(c);
      layer.effects = {pre, ink, post};
      for (auto mode : {LayerBlend::Passthrough, LayerBlend::Replace}) {
        if (c < 5)
          layer.blending[c] = mode;
        else if (c == 6)
          layer.blending[3] = mode;
        else if (c == 5)
          layer.ao_blending = mode;
        else if (c == 16)
          layer.displacement.blending = mode;
        else
          layer.users[0].blending = mode;
        check(c);
      }
    }
  }
  set.texture_resolution = 512;
  set.layers.back().kind = LayerKind::Fill;
  set.layers.back().effects.clear();
  LevelsEffect sparse_content;
  sparse_content.kind = ContentEffectKind::Paint;
  sparse_content.channel = 0;
  set.layers.back().effects = {sparse_content};
  target = texture("BTS sparse content effect", 512, 512, gpu::TextureFormat::SFLOAT_32_32_32_32);
  check(0);
  auto content_saved = set.layers.back().effects;
  auto content_dab = effect_ink;
  content_dab.center = {.05, .05};
  content_dab.axis_x = {.01, 0};
  content_dab.axis_y = {0, .01};
  content_dab.channels.fill(true);
  set.layers.back().effects[0].paint.pixels.stamp(512, content_dab);
  check(0);
  const auto content_dirty = preview.impl_->last_dispatch_texels;
  if (content_dirty == 0 || content_dirty >= 512 * 512 * 3)
    throw std::runtime_error("Paint effect invalidated unchanged source prefixes");
  set.layers.back().effects = content_saved;
  check(0);
  check(0);
  if (preview.wrote_image())
    throw std::runtime_error("Unchanged Paint effect wrote image");
  std::printf(
      "BTS_GPU_CONTENT_PAINT_VERIFIED parents=fill,paint,folder channels=8 blend_modes=29 levels_order=1 empty_replace=1 passthrough=1 undo=1 dirty_texels=%llu unchanged_upload=0 production_readback=0\n",
      (unsigned long long)content_dirty);
  set = TextureSet{};
  set.texture_resolution = 128;
  set.ao_channel = true;
  set.base_values.ambient_occlusion = .8;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.ambient_occlusion = .37;
  set.layers.front().ao_opacity = .6;
  set.layers.front().mask = LayerMask{};
  set.layers.front().mask->background = .7;
  TextureDocument::add_fill(set);
  auto &ao_top = set.layers.back();
  ao_top.values.ambient_occlusion = .68;
  ao_top.ao_opacity = .43;
  target = texture(
      "BTS authored and baked AO reference", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (int mode = 0; mode <= 28; ++mode) {
    ao_top.ao_blending = LayerBlend(mode);
    check(5);
  }
  ao_top.ao_blending = LayerBlend::Normal;
  auto baked_values = std::vector<float>(128 * 128 * 4);
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const auto i = (y * 128 + x) * 4;
      const float alpha = x < 64 ? .3 : 1;
      for (int c = 0; c < 3; ++c)
        baked_values[i + c] = (.2f + .6f * y / 128) * alpha;
      baked_values[i + 3] = alpha;
    }
  auto baked = std::make_shared<MeshMapPixels>(128, std::move(baked_values));
  MeshMap map;
  map.name = "AO";
  map.tiles[1001] = {128, baked->hash, baked};
  set.mesh_maps[3] = map;
  for (int mode = 0; mode < 2; ++mode) {
    set.ao_mixing = AoMixing(mode);
    for (bool enabled : {false, true}) {
      set.ao_channel = enabled;
      check(5);
    }
  }
  set.ao_channel = true;
  set.layers.clear();
  TextureLayer ao_paint;
  ao_paint.ao_enabled = true;
  ao_paint.kind = LayerKind::Paint;
  set.layers.push_back(ao_paint);
  set.tiles = {1001, 1002};
  set.mesh_maps[3]->tiles[1002] = {128, baked->hash, baked};
  auto ao_ink = mask;
  ao_ink.values.roughness = .25;
  ao_ink.center = {1.5, .5};
  set.layers.front().ao_pixels.stamp(128, ao_ink, {1002});
  target = Texture(GPU_texture_create_2d_array("BTS cross-tile AO atlas",
                                               256,
                                               128,
                                               1,
                                               1,
                                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                                               GPU_TEXTURE_USAGE_GENERAL,
                                               nullptr),
                   GPU_texture_free);
  const std::map<int, std::array<int, 3>> ao_regions{{1001, {0, 0, 0}}, {1002, {128, 0, 0}}};
  for (auto precision : {ChannelPrecision::L8, ChannelPrecision::L16F, ChannelPrecision::L32F}) {
    set.precision[5] = precision;
    for (int variant = 0; variant < 3; ++variant) {
      set.ao_mixing = variant == 0 ? AoMixing::Multiply : AoMixing::Replace;
      set.layers.front().visible = variant != 2;
      preview.begin_frame();
      if (preview.update_tiles(set, 5, target.get(), ao_regions) != GpuPreviewUpdate::Updated)
        throw std::runtime_error("GPU cross-tile AO did not execute");
      ++cases;
    }
  }
  std::printf(
      "BTS_GPU_AO_VERIFIED baked_authored=1 modes=29 tiles=1001,1002 global_presence=1 float32=1\n");
  set = TextureSet{};
  set.texture_resolution = 128;
  set.channels[3] = true;
  set.channels[4] = true;
  set.layers.front().kind = LayerKind::Fill;
  set.layers.front().values.normal = normalized_normal({.3f, -.2f, .9f});
  set.layers.front().values.bump = .002;
  set.layers.front().opacity[3] = .6;
  target = texture(
      "BTS derived normal reference", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
  for (bool bump : {false, true}) {
    set.channels[4] = bump;
    check(3);
  }
  set.channels[4] = true;
  set.layers.front().kind = LayerKind::Paint;
  set.tiles = {1001, 1002, 1011};
  set.tile_resolutions[1002] = 256;
  for (int tile : set.tiles) {
    const int w = set.width(tile), h = set.rows(tile);
    std::vector<float> heights(std::size_t(w) * h * 4), normals(heights.size()),
        bake_values(heights.size());
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const auto i = (std::size_t(y) * w + x) * 4;
        const float alpha = x > w / 4 ? .73f : 0.f;
        const float height_value = .0002f * ((x + .5f) / w + (tile - 1001) % 10) -
                                   .0001f * ((y + .5f) / h + (tile - 1001) / 10);
        for (int c = 0; c < 3; ++c) {
          heights[i + c] = height_value;
          normals[i + c] = set.layers.front().values.normal[c] * alpha;
          bake_values[i + c] = (c == 0 ? .63f : c == 1 ? .42f : .98f) * (y > h / 4 ? .4f : 0.f);
        }
        heights[i + 3] = 1;
        normals[i + 3] = alpha;
        bake_values[i + 3] = y > h / 4 ? .4f : 0.f;
      }
    set.layers.front().pixels.assign_channel(w, 4, heights, tile);
    set.layers.front().pixels.assign_channel(w, 3, normals, tile);
    auto pixels = std::make_shared<MeshMapPixels>(w, std::move(bake_values), h);
    if (!set.mesh_maps[0]) {
      MeshMap map;
      map.name = "Normal";
      set.mesh_maps[0] = map;
    }
    set.mesh_maps[0]->tiles[tile] = {w, pixels->hash, pixels, h};
  }
  target = Texture(GPU_texture_create_2d_array("BTS derived normal UDIM atlas",
                                               512,
                                               256,
                                               2,
                                               1,
                                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                                               GPU_TEXTURE_USAGE_GENERAL,
                                               nullptr),
                   GPU_texture_free);
  const std::map<int, std::array<int, 3>> normal_regions{
      {1001, {0, 0, 0}}, {1002, {128, 0, 0}}, {1011, {0, 0, 1}}};
  set.precision[3] = ChannelPrecision::RGB32F;
  set.precision[4] = ChannelPrecision::L32F;
  for (int method = 0; method < 2; ++method)
    for (int mixing = 0; mixing < 2; ++mixing)
      for (bool dx : {false, true}) {
        set.bump_method = BumpMethod(method);
        set.normal_mixing = NormalMixing(mixing);
        set.normal_format = dx ? NormalMapFormat::DirectX : NormalMapFormat::OpenGL;
        preview.begin_frame();
        if (preview.update_tiles(set, 3, target.get(), normal_regions) !=
            GpuPreviewUpdate::Updated)
          throw std::runtime_error("GPU derived normal did not execute");
        ++cases;
      }
  set.channels[3] = false;
  preview.begin_frame();
  if (preview.update_tiles(set, 3, target.get(), normal_regions) != GpuPreviewUpdate::Updated)
    throw std::runtime_error("GPU baked-only normal did not execute");
  ++cases;
  std::printf(
      "BTS_GPU_DERIVED_NORMAL_VERIFIED baked_authored_bump=1 tiles=1001,1002,1011 resolutions=128,256 global_presence=1 sharp_sobel_directx=1 float32=1\n");
  {
    TextureSet dirty_set;
    dirty_set.texture_resolution = 512;
    dirty_set.tiles = {1001, 1002, 1011};
    dirty_set.channels[3] = dirty_set.channels[4] = true;
    dirty_set.ao_channel = true;
    dirty_set.precision[3] = ChannelPrecision::RGB32F;
    dirty_set.precision[4] = dirty_set.precision[5] = ChannelPrecision::L32F;
    dirty_set.base_values.normal = normalized_normal({.2f, .3f, .9f});
    dirty_set.base_values.ambient_occlusion = .7f;
    dirty_set.ao_mixing = AoMixing::Replace;
    dirty_set.normal_mixing = NormalMixing::Replace;
    auto &paint = dirty_set.layers.front();
    paint.kind = LayerKind::Paint;
    paint.channels[3] = paint.channels[4] = paint.ao_enabled = true;
    for (int role : {0, 3}) {
      MeshMap map;
      map.name = role == 0 ? "Normal" : "AO";
      std::vector<float> values(32 * 32 * 4);
      for (int i = 0; i < 32 * 32; ++i) {
        values[i * 4] = role == 0 ? .65f : .4f;
        values[i * 4 + 1] = role == 0 ? .4f : .4f;
        values[i * 4 + 2] = role == 0 ? .98f : .4f;
        values[i * 4 + 3] = 1;
      }
      auto pixels = std::make_shared<MeshMapPixels>(32, std::move(values));
      for (int tile : dirty_set.tiles)
        map.tiles[tile] = {32, pixels->hash, pixels};
      dirty_set.mesh_maps[role] = std::move(map);
    }
    GpuLayerPreview dirty_preview;
    Texture normal{GPU_texture_create_2d_array("BTS dirty normal tiles",
                                               512,
                                               512,
                                               3,
                                               1,
                                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                                               GPU_TEXTURE_USAGE_GENERAL,
                                               nullptr),
                   GPU_texture_free};
    Texture ao{GPU_texture_create_2d_array("BTS dirty AO tiles",
                                           512,
                                           512,
                                           3,
                                           1,
                                           gpu::TextureFormat::SFLOAT_32_32_32_32,
                                           GPU_TEXTURE_USAGE_GENERAL,
                                           nullptr),
               GPU_texture_free};
    const std::map<int, std::array<int, 3>> regions{
        {1001, {0, 0, 0}}, {1002, {0, 0, 1}}, {1011, {0, 0, 2}}};
    bool partial_normal = false, partial_ao = false;
    for (int step = 0; step < 4; ++step) {
      if (step == 3) {
        paint.pixels = {};
        paint.ao_pixels = {};
      }
      else if (step) {
        auto dab = ink;
        dab.center = step == 1 ? std::array<double, 2>{.99, .5} : std::array<double, 2>{.25, .25};
        dab.axis_x = {.025, 0};
        dab.axis_y = {0, .025};
        dab.channels = {false, false, false, true, true};
        dab.values.normal = normalized_normal({-.3f, .4f, .8f});
        dab.values.bump = .002f;
        paint.pixels.stamp(512, dab, dirty_set.tiles);
        dab.channels = {false, false, true, false, false};
        dab.values.roughness = .2f;
        paint.ao_pixels.stamp(512, dab, dirty_set.tiles);
      }
      for (int c : {3, 5}) {
        auto *destination = c == 3 ? normal.get() : ao.get();
        dirty_preview.begin_frame();
        if (dirty_preview.update_tiles(dirty_set, c, destination, regions) !=
            GpuPreviewUpdate::Updated)
          throw std::runtime_error("Dirty derived tiles did not execute");
        const auto changed = dirty_preview.impl_->last_derived_chunks,
                   total = dirty_preview.impl_->last_derived_total;
        if (step == 2) {
          if (c == 3)
            partial_normal = changed > 0 && changed < total;
          else
            partial_ao = changed > 0 && changed < total;
        }
        if (c == 5) {
          CompositeChannels wanted{};
          wanted[5] = true;
          std::atomic_bool cancel = false;
          const auto reference = native_composite_job(
              dirty_set, true, nullptr, 1, cancel, false, wanted);
          auto *actual = static_cast<float *>(GPU_texture_read(destination, GPU_DATA_FLOAT, 0));
          for (const auto &[tile, offset] : regions) {
            const auto &expected = reference->tiles.at(tile)[5];
            for (std::size_t i = 0; i < expected.size(); ++i)
              if (std::abs(actual[std::size_t(offset[2]) * 512 * 512 * 4 + i] - expected[i]) >
                  1e-6)
                throw std::runtime_error("Dirty AO/global coverage differs from CPU");
          }
          MEM_delete(actual);
        }
        dirty_preview.begin_frame();
        if (dirty_preview.update_tiles(dirty_set, c, destination, regions) !=
                GpuPreviewUpdate::Updated ||
            dirty_preview.wrote_image())
          throw std::runtime_error("Unchanged derived channel was recomputed");
        ++cases;
      }
    }
    if (!partial_normal || !partial_ao)
      throw std::runtime_error("Derived channels ignored dirty chunk boundaries");
    std::printf(
        "BTS_GPU_DERIVED_DIRTY_VERIFIED tiles=1001,1002,1011 size=512 partial_normal=1 partial_ao=1 cross_tile_halo=1 global_coverage_transition=1 unchanged_dispatch=0\n");
  }
  {
    SceneSnapshot scene;
    MaterialSummary material;
    material.session_id = 31;
    scene.materials.push_back(material);
    scene.minimum = {0, -1, 0};
    scene.maximum = {1, 1, 0};
    UvFaceTriangle first;
    first.material = 0;
    first.vertex_ids = {0, 1, 2};
    first.positions = {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    first.corners = {{{.1f, .1f}, {.45f, .1f}, {.1f, .8f}}};
    first.triangle_id = 1;
    UvFaceTriangle second;
    second.material = 0;
    second.vertex_ids = {1, 0, 3};
    second.positions = {{{1, 0, 0}, {0, 0, 0}, {0, -1, 0}}};
    second.corners = {{{.6f, .6f}, {.9f, .6f}, {.9f, .9f}}};
    second.triangle_id = 2;
    scene.uv_triangles = {first, second};
    TexturePadding padding(scene, 31);
    std::size_t sparse_operations = 0;
    double max_error = 0;
    for (int rows : {64, 128})
      for (bool adjacent : {false, true}) {
        const auto plan = padding.gpu_margin_plan(128, rows, 1001, adjacent);
        if (plan != padding.gpu_margin_plan(128, rows, 1001, adjacent))
          throw std::runtime_error("Geometry-only margin plan was not cached");
        sparse_operations += plan->operations.size();
        std::vector<float> expected(std::size_t(128) * rows * 4);
        for (std::size_t i = 0; i < expected.size(); ++i)
          expected[i] = float((i * 13) % 971) / 400 - .7f;
        auto actual = expected;
        auto *buffer = IMB_allocImBuf(128, rows, ImBufFlags::FloatData);
        std::copy(expected.begin(), expected.end(), buffer->float_data_for_write());
        padding.apply(*buffer, 1001, adjacent);
        std::copy_n(buffer->float_data(), expected.size(), expected.begin());
        IMB_freeImBuf(buffer);
        plan->evaluate_reference(actual);
        for (std::size_t i = 0; i < actual.size(); ++i) {
          max_error = std::max(max_error, double(std::abs(actual[i] - expected[i])));
          if (std::abs(actual[i] - expected[i]) > 1e-6)
            throw std::runtime_error("GPU margin instructions differ from native Blender padding");
        }
        ++cases;
      }
    if (!sparse_operations)
      throw std::runtime_error("Margin geometry oracle produced no margin");
    std::printf(
        "BTS_MARGIN_PLAN_VERIFIED extend_adjacent=1 dimensions=128x64,128x128 operations=%zu max_error=%.9g pixels_stored=0\n",
        sparse_operations,
        max_error);
    GpuMarginExecutor margins;
    GpuGeometryMask geometry_masks;
    std::uint64_t initial_samples = 0, partial_samples = 0;
    double gpu_error = 0;
    bool remote_update = false;
    for (bool adjacent : {false, true}) {
      constexpr int width = 512, rows = 512;
      const auto plan = padding.gpu_margin_plan(width, rows, 1001, adjacent);
      Texture image{GPU_texture_create_2d_array("BTS margin atlas oracle",
                                                width + 32,
                                                rows + 16,
                                                2,
                                                1,
                                                gpu::TextureFormat::SFLOAT_32_32_32_32,
                                                GPU_TEXTURE_USAGE_GENERAL,
                                                nullptr),
                    GPU_texture_free};
      const float sentinel[4] = {-9, -8, -7, -6};
      GPU_texture_clear(image.get(), GPU_DATA_FLOAT, sentinel);
      std::vector<float> original(std::size_t(width) * rows * 4);
      for (std::size_t i = 0; i < original.size(); ++i)
        original[i] = float((i * 13) % 971) / 400 - .7f;
      std::vector<float> previous;
      const std::array<int, 3> offset{16, 8, 1};
      for (int step = 0; step <= 20; ++step) {
        const int chunk = step ? ((step - 1) % 4) : 0, left = step ? (chunk % 2) * 256 : 0,
                  bottom = step ? (chunk / 2) * 256 : 0, w = step ? 256 : width,
                  h = step ? 256 : rows;
        if (step)
          for (int y = bottom; y < bottom + h; ++y)
            for (int x = left; x < left + w; ++x)
              for (int c = 0; c < 4; ++c)
                original[(std::size_t(y) * width + x) * 4 + c] += .031f * (c + 1);
        std::vector<float> rectangle(std::size_t(w) * h * 4);
        for (int y = 0; y < h; ++y)
          std::copy_n(original.data() + (std::size_t(y + bottom) * width + left) * 4,
                      w * 4,
                      rectangle.data() + std::size_t(y) * w * 4);
        GPU_texture_update_sub(image.get(),
                               GPU_DATA_FLOAT,
                               rectangle.data(),
                               left + offset[0],
                               bottom + offset[1],
                               1,
                               w,
                               h,
                               1);
        std::vector<std::uint32_t> dirty(4, step ? 0 : 1);
        dirty[chunk] = 1;
        if (!margins.apply(
                adjacent ? "adjacent" : "extend", plan, image.get(), offset, dirty, true))
          throw std::runtime_error("Sparse GPU margin dispatch failed");
        if (step)
          partial_samples += margins.original_samples_updated();
        else
          initial_samples += margins.original_samples_updated();
        auto *buffer = IMB_allocImBuf(width, rows, ImBufFlags::FloatData);
        std::copy(original.begin(), original.end(), buffer->float_data_for_write());
        padding.apply(*buffer, 1001, adjacent);
        const auto *expected = buffer->float_data();
        auto *actual = static_cast<float *>(GPU_texture_read(image.get(), GPU_DATA_FLOAT, 0));
        for (int y = 0; y < rows; ++y)
          for (int x = 0; x < width; ++x)
            for (int c = 0; c < 4; ++c) {
              const auto i = (std::size_t(y) * width + x) * 4 + c,
                         atlas = ((std::size_t(rows + 16) + y + offset[1]) * (width + 32) + x +
                                  offset[0]) *
                                     4 +
                                 c;
              const double difference = std::abs(actual[atlas] - expected[i]);
              gpu_error = std::max(gpu_error, difference);
              if (difference > 1e-6) {
                std::fprintf(
                    stderr,
                    "BTS_GPU_MARGIN_DIFFERENCE adjacent=%d step=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",
                    adjacent,
                    step,
                    x,
                    y,
                    c,
                    expected[i],
                    actual[atlas]);
                throw std::runtime_error("Sparse GPU margin differs from Blender");
              }
              if (step && adjacent &&
                  (x < left || x >= left + w || y < bottom || y >= bottom + h) &&
                  std::abs(expected[i] - previous[i]) > 1e-6)
                remote_update = true;
            }
        for (int c = 0; c < 4; ++c)
          if (actual[c] != sentinel[c])
            throw std::runtime_error("GPU margin overwrote another atlas region");
        previous.assign(expected, expected + original.size());
        MEM_delete(actual);
        IMB_freeImBuf(buffer);
        ++cases;
      }
    }
    if (!remote_update || partial_samples >= initial_samples * 20)
      throw std::runtime_error("GPU margin dirty donors were not reused across UV seams");
    std::printf(
        "BTS_GPU_MARGIN_VERIFIED extend_adjacent=1 dirty_edits=40 remote_seam=1 original_samples=%llu partial_samples=%llu max_error=%.9g atlas_untouched=1 production_readback=0\n",
        (unsigned long long)initial_samples,
        (unsigned long long)partial_samples,
        gpu_error);
    GpuLayerPreview padded_preview;
    TextureSet padded_set;
    padded_set.material_session_uid = 31;
    padded_set.texture_resolution = 128;
    padded_set.channels.fill(true);
    padded_set.ao_channel = true;
    padded_set.precision.fill(ChannelPrecision::RGB32F);
    padded_set.displacement.allocated = padded_set.displacement.channel = true;
    auto &fill = padded_set.layers.front();
    fill.kind = LayerKind::Fill;
    fill.channels.fill(true);
    fill.ao_enabled = true;
    fill.displacement.enabled = true;
    fill.values.bump = .013f;
    fill.values.ambient_occlusion = .37f;
    fill.fill.texture = resource;
    fill_channel_texture(fill.fill, 4) = signed_texture;
    fill_channel_texture(fill.fill, 14) = signed_texture;
    fill.opacity.fill(.71f);
    padded_preview.set_padding_geometry(31,
                                        {{1001, padding.gpu_margin_plan(128, 128, 1001, false)}},
                                        {{1001, padding.gpu_margin_plan(128, 128, 1001, true)}});
    auto padded_target = texture(
        "BTS complete padded stack oracle", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
    double stack_error = 0;
    for (bool adjacent : {false, true})
      for (int c : {0, 1, 2, 3, 4, 5, 6, 16}) {
        padded_set.padding_3d = adjacent;
        padded_preview.begin_frame();
        if (padded_preview.update(padded_set, c, padded_target.get()) != GpuPreviewUpdate::Updated)
          throw std::runtime_error("Padded GPU stack was not ready");
        CompositeChannels channels{};
        channels[c] = true;
        std::atomic_bool cancelled = false;
        const auto reference = native_composite_job(
            padded_set, true, padding.clone(), 1, cancelled, false, channels);
        auto *actual = static_cast<float *>(
            GPU_texture_read(padded_target.get(), GPU_DATA_FLOAT, 0));
        const auto &expected = reference->tiles.at(1001)[c];
        for (std::size_t i = 0; i < expected.size(); ++i) {
          const double error = std::abs(actual[i] - expected[i]);
          stack_error = std::max(stack_error, error);
          if (error > (c == 3 ? 3e-5 : 2e-6)) {
            std::fprintf(
                stderr,
                "BTS_GPU_PADDED_STACK_DIFFERENCE channel=%d adjacent=%d index=%zu expected=%.9g actual=%.9g\n",
                c,
                adjacent,
                i,
                expected[i],
                actual[i]);
            throw std::runtime_error("Padded GPU stack differs from CPU reference");
          }
        }
        MEM_delete(actual);
        ++cases;
      }
    std::printf(
        "BTS_GPU_PADDED_STACK_VERIFIED channels=8 extend_adjacent=1 padded_bump_normal=1 max_error=%.9g\n",
        stack_error);
  }
  // Content resources own coverage and are channel-local, including empty
  // Paint/Folder sources. Every operation starts with clean sampler bindings.
  {
    set = TextureSet{};
    set.texture_resolution = 128;
    set.tiles = {1001};
    set.channels.fill(true);
    set.ao_channel = true;
    set.displacement.channel = set.displacement.allocated = true;
    set.users[0].present = set.users[0].allocated = true;
    set.precision.fill(ChannelPrecision::RGB32F);
    set.displacement.precision = ChannelPrecision::L32F;
    for (int role : {1, 3, 4, 5, 6}) {
      MeshMap map;
      map.name = "Content generator input";
      for (int tile : {1001, 1002}) {
        std::vector<float> rgba(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
          for (int x = 0; x < 32; ++x) {
            const auto at = (y * 32 + x) * 4;
            rgba[at] = (x + .5f) / 32;
            rgba[at + 1] = (y + .5f) / 32;
            rgba[at + 2] = tile == 1001 ? .8f : .2f;
            rgba[at + 3] = 1;
          }
        auto pixels = std::make_shared<MeshMapPixels>(32, std::move(rgba));
        map.tiles[tile] = {32, pixels->hash, pixels};
      }
      set.mesh_maps[role] = std::move(map);
    }
    auto &layer = set.layers.front();
    layer.channels.fill(true);
    layer.ao_enabled = true;
    layer.users[0].enabled = true;
    layer.displacement.enabled = true;
    target = texture("BTS content generators", 128, 128, gpu::TextureFormat::SFLOAT_16_16_16_16);
    const int first = cases;
    for (auto kind : {LayerKind::Paint, LayerKind::Fill, LayerKind::Folder})
      for (int channel : {0, 1, 2, 4, 5, 7, 16})
        for (int resource = 1; resource <= 6; ++resource)
          for (auto mode : {LayerBlend::Normal, LayerBlend::Multiply, LayerBlend::Replace})
            for (float opacity : {.37f, 1.f}) {
              layer.kind = kind;
              LevelsEffect effect;
              effect.kind = ContentEffectKind::Generator;
              effect.channel = gpu_layer_channel(layer, channel).fill_channel;
              effect.generator = resource;
              effect.opacity = opacity;
              effect.blending = mode;
              effect.light_direction = {.3f, .4f, 1};
              effect.map_filter = resource % 2;
              layer.effects = {effect};
              GPU_texture_unbind_all();
              check(channel);
            }
    set.tiles.insert(1002);
    layer.kind = LayerKind::Paint;
    layer.effects.front().channel = 14;
    layer.effects.front().generator = 6;
    GPU_texture_unbind_all();
    preview.begin_frame();
    if (preview.update(set, 16, target.get(), 1002) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("Content generator UDIM oracle deferred");
    ++cases;
    const auto source = set.mesh_maps[1];
    set.mesh_maps[1].reset();
    check(16);
    set.mesh_maps[1] = set.mesh_maps[5];
    check(16);
    set.mesh_maps[1] = source;
    check(16);
    layer.effects.front().enabled = false;
    check(16);
    layer.effects.front().enabled = true;
    check(16);
    LevelsEffect levels;
    levels.channel = 14;
    levels.gamma = 1.7;
    layer.effects.push_back(levels);
    check(16);
    std::swap(layer.effects[0], layer.effects[1]);
    check(16);
    std::printf(
        "BTS_GPU_CONTENT_GENERATORS_VERIFIED cases=%d parents=paint,fill,folder channels=7 resources=6 blends=3 opacity=2 missing_rebake=1 udim=1 levels_order=1 clean_bindings=1\n",
        cases - first);
  }
  cases += verify_gpu_flatten();
  testing_stack = false;
  std::printf(
      "BTS_GPU_STACK_SELF_TEST cases=%d blend_mask_texture_paint_versions_precision=pass\n",
      cases);
}
