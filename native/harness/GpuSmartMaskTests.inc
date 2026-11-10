// SPDX-License-Identifier: GPL-3.0-or-later
// Runs only inside the explicit private production-GPU self_test. No authored
// document, UI event, export or interactive thumbnail calls the CPU oracle here.
{
  GpuLayerPreview masks;
  auto image = texture(
      "BN real smart-mask recipes", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
  int applications = 0, causal = 0, fallback_updates = 0, seams = 0, oracle_probes = 0;
  double oracle_maximum = 0;
  std::set<std::uint64_t> unique;
  const auto render = [&](const TextureSet &model, int channel = 0) {
    std::fprintf(stderr,
                 "BTS_GPU_BN_RENDER name=%s kind=%d effects=%zu maps=%d\n",
                 model.layers.back().name.c_str(),
                 int(model.layers.back().kind),
                 model.layers.back().mask ? model.layers.back().mask->effects.size() : 0,
                 int(bool(model.mesh_maps[3])));
    masks.begin_frame();
    if (masks.update(model, channel, image.get()) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("BN native smart-mask renderer did not execute");
    const auto *raw = static_cast<const float *>(GPU_texture_read(image.get(), GPU_DATA_FLOAT, 0));
    if (!raw)
      throw std::runtime_error("BN diagnostic readback unavailable");
    std::vector<float> result(raw, raw + 128 * 128 * 4);
    MEM_delete(raw);
    return result;
  };
  // Explicitly compare the real recipes, independent of the renderer's
  // optional generic diagnostic hooks. CPU and GPU use the same integer texels,
  // 32x32 immutable surface atlas and native RGB32F channel quantization.
  const auto compare_recipe = [&](const TextureSet &model, const std::vector<float> &rgba) {
    std::array<bool, 6> wanted{};
    wanted[0] = true;
    for (int gy = 0; gy < 8; ++gy)
      for (int gx = 0; gx < 8; ++gx) {
        const int x = (2 * gx + 1) * 128 / 16, y = (2 * gy + 1) * 128 / 16;
        const auto result = TextureDocument::evaluate_channels(model, wanted, x, y, 1001);
        const auto full = precision_channel(result.material, 0, model.precision[0]);
        const float alpha = result.coverage[0];
        const std::array<float, 4> expected{
            full[0] - model.base_values.base_color[0] * (1 - alpha),
            full[1] - model.base_values.base_color[1] * (1 - alpha),
            full[2] - model.base_values.base_color[2] * (1 - alpha),
            alpha};
        for (int c = 0; c < 4; ++c) {
          const float actual = rgba[(y * 128 + x) * 4 + c];
          const double error = std::abs(double(actual) - expected[c]);
          oracle_maximum = std::max(oracle_maximum, error);
          // Preserve the existing native RGBA32F stack-oracle bound.
          if (!std::isfinite(actual) || error > 1e-6 * std::max(1.f, std::abs(expected[c])))
            throw std::runtime_error(
                "BN native recipe CPU/GPU mismatch: " + model.layers.back().name +
                " x=" + std::to_string(x) + " y=" + std::to_string(y) + " c=" + std::to_string(c) +
                " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected[c]));
        }
        ++oracle_probes;
      }
  };
  const auto fixture = [&](const StudioAsset &asset, bool folder) {
    TextureSet model;
    smart_mask_probe::profile(model, asset.smart_mask->effects.front().generator);
    model.padding_3d = false;
    model.channels = {true, false, false, false, false};
    model.precision[0] = ChannelPrecision::RGB32F;
    model.base_values.base_color = {0, 0, 0, 1};
    model.layers.clear();
    auto atlas = std::make_shared<FillSurfaceAtlas>(*model.projection_geometry);
    auto &points = atlas->tiles.at(1001);
    for (int y = 0; y < 32; ++y)
      for (int x = 0; x < 32; ++x) {
        auto &point = points[y * 32 + x];
        point.position = {(x + .5f) / 16 - 1, (y + .5f) / 16 - 1, .2f + float(x + y) / 128};
        point.normal = {.2f + x / 64.f, .4f, .9f - y / 64.f};
      }
    model.projection_geometry = std::move(atlas);
    TextureLayer parent;
    parent.kind = folder ? LayerKind::Folder : LayerKind::Fill;
    parent.name = asset.name.toStdString();
    parent.values.base_color = {1, 1, 1, 1};
    parent.mask = *asset.smart_mask;
    if (folder) {
      TextureLayer child;
      child.kind = LayerKind::Fill;
      child.parent = parent.id;
      child.depth = 1;
      child.values.base_color = {1, 1, 1, 1};
      model.layers.push_back(std::move(child));
    }
    model.selected_layer = parent.id;
    model.layers.push_back(std::move(parent));
    return model;
  };
  // New imported response modes share exact scalar/GPU parameter transport.
  for (int response : {1, 2, 3, 4}) {
    auto model = fixture(smart_mask_probe::asset("Rust"), false);
    MaskEffect e;
    e.kind = MaskEffectKind::Generator;
    e.generator = response == 1 ? 8 : 9;
    e.response = response;
    e.axis = 1;
    e.surface_fallback = true;
    e.triplanar_grunge = true;
    e.grunge_amount = .4f;
    e.grunge_scale = 3;
    e.position_influence = .7f;
    e.normal_influence = .2f;
    e.curvature_radius = response == 1 ? 2.3f : 0.f;
    e.grunge_pattern = response == 2 ? 3 : 1;
    e.bias = .1f;
    e.contrast = 2.1f;
    model.layers.back().mask->effects = {e};
    model.layers.back().mask->background = 0;
    const auto pixels = render(model);
    compare_recipe(model, pixels);
  }
  const auto range_hash = [&](const std::vector<float> &rgba) {
    float low = 1, high = 0;
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
      if (!std::isfinite(rgba[i]))
        throw std::runtime_error("BN mask contains nonfinite coverage");
      low = std::min(low, rgba[i]);
      high = std::max(high, rgba[i]);
      std::uint32_t bits;
      std::memcpy(&bits, &rgba[i], sizeof(bits));
      hash = (hash ^ bits) * 1099511628211ULL;
    }
    if (high - low <= 1e-4f)
      throw std::runtime_error("BN real preset GPU coverage is constant");
    return hash;
  };
  if (std::getenv("BTS_BN_PRECISION_DIAG")) {
    const auto original = fixture(smart_mask_probe::asset("Edge Wear"), false);
    const int x = 104, y = 8;
    const auto inspect = [&](const char *stage, TextureSet model) {
      bool valid = true;
      try {
        render(model);
      }
      catch (const std::exception &error) {
        valid = false;
        std::fprintf(stderr, "BTS_BN_PRECISION_ORACLE stage=%s failure=%s\n", stage, error.what());
      }
      const float *raw = static_cast<const float *>(
          GPU_texture_read(image.get(), GPU_DATA_FLOAT, 0));
      if (!raw)
        throw std::runtime_error("BN precision readback unavailable");
      std::array<bool, 6> wanted{};
      wanted[0] = true;
      const auto cpu = TextureDocument::evaluate_channels(model, wanted, x, y, 1001);
      const auto rgb = precision_channel(cpu.material, 0, model.precision[0]);
      std::fprintf(
          stderr,
          "BTS_BN_PRECISION stage=%s valid=%d x=%d y=%d cpu_rgb=%.12g cpu_alpha=%.12g gpu_rgb=%.12g gpu_alpha=%.12g delta=%.12g\n",
          stage,
          int(valid),
          x,
          y,
          rgb[0],
          cpu.coverage[0],
          raw[(y * 128 + x) * 4],
          raw[(y * 128 + x) * 4 + 3],
          double(raw[(y * 128 + x) * 4]) - rgb[0]);
      MEM_delete(raw);
    };
    const auto &point = original.projection_geometry->tiles.at(1001)[2 * 32 + 26];
    std::fprintf(
        stderr,
        "BTS_BN_PRECISION_POINT p=%.12g,%.12g,%.12g n=%.12g,%.12g,%.12g scale=%.12g extent=%.12g\n",
        point.position[0],
        point.position[1],
        point.position[2],
        point.normal[0],
        point.normal[1],
        point.normal[2],
        original.layers.back().mask->effects[2].mapping.projection.scale3[0],
        original.projection_geometry->extent);
    for (std::size_t n = 1; n <= original.layers.back().mask->effects.size(); ++n) {
      auto model = original;
      model.id = QUuid::createUuid();
      model.layers.back().mask->effects.resize(n);
      inspect(("prefix-" + std::to_string(n)).c_str(), std::move(model));
    }
    auto fill = original.layers.back().mask->effects[2];
    fill.opacity = 1;
    fill.blending = LayerBlend::Normal;
    auto isolated = original;
    isolated.id = QUuid::createUuid();
    isolated.layers.back().mask->background = 0;
    isolated.layers.back().mask->effects = {fill};
    inspect("bitmap-only", isolated);
    for (int axis = 0; axis < 3; ++axis) {
      auto model = isolated;
      model.id = QUuid::createUuid();
      auto atlas = std::make_shared<FillSurfaceAtlas>(*model.projection_geometry);
      for (auto &point : atlas->tiles.at(1001)) {
        point.normal = {0, 0, 0};
        point.normal[axis] = 1;
      }
      model.projection_geometry = std::move(atlas);
      inspect(("bitmap-axis-" + std::to_string(axis)).c_str(), std::move(model));
    }
    throw std::runtime_error(
        "BN precision diagnostic completed; this is not a passing verification run");
  }
  // The Float32 bitmap policy also covers premultiplied interpolation of
  // signed/HDR RGB, fractional/zero alpha, alpha-ignore and NPOT dimensions.
  {
    auto bitmap = std::make_shared<FillTexture>();
    bitmap->asset = "b2aedb79-f970-4d13-a5ab-43c5de8c3ce0";
    bitmap->hash = std::string(64, 'd');
    bitmap->name = "BN signed NPOT precision fixture";
    bitmap->width = 7;
    bitmap->height = 5;
    bitmap->raw = true;
    auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(35);
    for (int y = 0; y < 5; ++y)
      for (int x = 0; x < 7; ++x)
        (*pixels)[y * 7 + x] = {-.75f + 3.25f * x / 6,
                                2.5f - 3.5f * y / 4,
                                -1.25f + .45f * (x + y),
                                float((x + y) % 4) / 3};
    bitmap->pixels = pixels;
    int cases = 0;
    for (bool alpha : {false, true})
      for (bool physical : {false, true}) {
        auto model = fixture(smart_mask_probe::asset("Edge Wear"), false);
        auto &layer = model.layers.back();
        layer.mask.reset();
        layer.fill = {};
        layer.fill.mode = 1;
        layer.fill.texture = bitmap;
        layer.fill.alpha_mask[0] = alpha;
        layer.fill.scale3 = {.34, -.51, .72};
        layer.fill.rotation3 = {17, -11, 23};
        layer.fill.offset3 = {.1, -.2, .07};
        layer.fill.scale = {1.7, .9};
        layer.fill.linked = false;
        layer.fill.rotation = 37;
        layer.fill.hardness = .35;
        layer.fill.physical = physical;
        layer.fill.custom_size_cm = {1.4, 2.3, 3.1};
        model.cm_per_native = 2.5;
        auto atlas = std::make_shared<FillSurfaceAtlas>(*model.projection_geometry);
        for (auto &point : atlas->tiles.at(1001))
          point.footprint = .003f;
        model.projection_geometry = std::move(atlas);
        const auto output = render(model);
        compare_recipe(model, output);
        ++cases;
      }
    std::printf(
        "BTS_GPU_BITMAP_TRIPLANAR_FLOAT32_VERIFIED cases=%d npot=7x5 fractional_alpha=1 signed_hdr=1 ignored_alpha=1 physical=1\n",
        cases);
  }
  for (const auto *name : smart_mask_probe::names) {
    const auto &asset = smart_mask_probe::asset(name);
    for (bool folder : {false, true}) {
      auto model = fixture(asset, folder);
      const auto output = render(model);
      compare_recipe(model, output);
      const auto hash = range_hash(output);
      if (!folder)
        unique.insert(hash);
      ++applications;
    }
    // Causal controls use precisely the independent CPU fixture: same bitmap,
    // same 3D point, same mapping and levels, only one baked map changes.
    auto model = fixture(asset, false);
    model.layers.back().mask = smart_mask_probe::causal_mask(*asset.smart_mask);
    const int generator = asset.smart_mask->effects.front().generator;
    smart_mask_probe::profile(model, generator);
    for (const auto &influence : smart_mask_probe::influences(name)) {
      smart_mask_probe::profile(model, generator);
      smart_mask_probe::map(model, influence.role, influence.stronger);
      const auto stronger = render(model);
      smart_mask_probe::map(model, influence.role, influence.weaker);
      const auto weaker = render(model);
      const auto at = (39 * 128 + 47) * 4;
      if (stronger[at] <= weaker[at] + 1e-4f)
        throw std::runtime_error(std::string("BN GPU causal map direction lost: ") + name + " / " +
                                 influence.label);
      std::printf("BTS_GPU_BN_CAUSAL mask=%s input=%s stronger=%.9g weaker=%.9g\n",
                  name,
                  influence.label,
                  stronger[at],
                  weaker[at]);
      ++causal;
    }
    // Keep set/layer IDs and renderer cache unchanged while real map content is
    // installed. This proves dependency invalidation, not just two fresh renders.
    model = fixture(asset, false);
    const auto baked_maps = model.mesh_maps;
    model.mesh_maps = {};
    const auto provisional = render(model);
    range_hash(provisional);
    model.mesh_maps = baked_maps;
    const auto baked = render(model);
    double changed = 0;
    for (std::size_t i = 0; i < baked.size(); i += 4)
      changed = std::max(changed, std::abs(double(baked[i]) - provisional[i]));
    if (changed <= 1e-4)
      throw std::runtime_error(
          std::string("BN baked maps did not replace provisional cached mask: ") + name);
    ++fallback_updates;
  }
  if (unique.size() != 8 || applications != 16 || causal != 21 || fallback_updates != 8)
    throw std::runtime_error("BN smart-mask GPU fixture coverage is incomplete");
  for (int kind : {7, 8, 9}) {
    auto model = fixture(smart_mask_probe::asset("Rust"), false);
    auto atlas = std::make_shared<FillSurfaceAtlas>(*model.projection_geometry);
    auto &points = atlas->tiles.at(1001);
    points[6 * 32 + 25] = points[6 * 32 + 6];
    model.projection_geometry = std::move(atlas);
    MaskEffect generator;
    generator.kind = MaskEffectKind::Generator;
    generator.generator = kind;
    generator.grunge_amount = .7f;
    generator.grunge_scale = 11;
    generator.grunge_seed = 41;
    generator.triplanar_grunge = generator.surface_fallback = true;
    model.layers.back().mask->effects = {generator};
    model.layers.back().mask->background = 0;
    const auto output = render(model, 15);
    const float a = output[((6 * 4 + 1) * 128 + 6 * 4 + 1) * 4],
                b = output[((6 * 4 + 1) * 128 + 25 * 4 + 1) * 4];
    if (std::abs(a - b) > 2e-5f)
      throw std::runtime_error(
          "BN triplanar generator changed across matching surface/UV islands");
    range_hash(output);
    ++seams;
  }
  // Independently exercise map-derived 3D coordinates when no atlas exists,
  // plus installed but uncovered maps that must use the atlas proxy policy.
  int fallback_cases = 0, anchor_frames = 0;
  const auto spatial_maps = [&](TextureSet &model) {
    for (int role : {1, 5}) {
      std::vector<float> values(32 * 32 * 4);
      for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
          const std::array<float, 4> p = role == 5 ?
                                             std::array<float, 4>{(x + .5f) / 32,
                                                                  (y + .5f) / 32,
                                                                  .25f + float(x + y) / 128,
                                                                  1} :
                                             std::array<float, 4>{.6f, .7f, .9f, 1};
          std::copy(p.begin(), p.end(), values.begin() + (y * 32 + x) * 4);
        }
      auto pixels = std::make_shared<const MeshMapPixels>(32, std::move(values));
      MeshMap map;
      map.tiles[1001] = {32, pixels->hash, pixels, 32};
      model.mesh_maps[role] = std::move(map);
    }
  };
  for (int kind : {7, 8, 9}) {
    auto model = fixture(smart_mask_probe::asset("Rust"), false);
    MaskEffect e;
    e.kind = MaskEffectKind::Generator;
    e.generator = kind;
    e.grunge_amount = .7f;
    e.triplanar_grunge = e.surface_fallback = true;
    model.layers.back().mask->effects = {e};
    model.layers.back().mask->background = 0;
    spatial_maps(model);
    model.projection_geometry.reset();
    const auto mapped = render(model);
    compare_recipe(model, mapped);
    range_hash(mapped);
    ++fallback_cases;
    model = fixture(smart_mask_probe::asset("Rust"), false);
    model.layers.back().mask->effects = {e};
    model.layers.back().mask->background = 0;
    model.mesh_maps = {};
    const auto absent = render(model);
    compare_recipe(model, absent);
    for (int role : {1, 3, 4, 5, 6}) {
      std::vector<float> values(32 * 32 * 4, .75f);
      for (int i = 0; i < 32 * 32; ++i)
        values[i * 4 + 3] = 0;
      auto pixels = std::make_shared<const MeshMapPixels>(32, std::move(values));
      MeshMap map;
      map.tiles[1001] = {32, pixels->hash, pixels, 32};
      model.mesh_maps[role] = std::move(map);
    }
    const auto uncovered = render(model);
    compare_recipe(model, uncovered);
    for (std::size_t i = 0; i < uncovered.size(); ++i)
      if (std::abs(uncovered[i] - absent[i]) > 1e-6f)
        throw std::runtime_error("BN uncovered map did not preserve missing-map surface fallback");
    ++fallback_cases;
  }
  // The new importer opts into missing-map proxies while retaining UV grunge.
  // Exercise that exact policy separately from the builtin 3D-grunge recipes.
  for (int kind : {8, 9}) {
    auto model = fixture(smart_mask_probe::asset("Rust"), false);
    model.mesh_maps = {};
    MaskEffect e;
    e.kind = MaskEffectKind::Generator;
    e.generator = kind;
    e.surface_fallback = true;
    e.triplanar_grunge = false;
    e.grunge_amount = .7f;
    e.grunge_scale = 11;
    e.grunge_seed = 41;
    model.layers.back().mask->effects = {e};
    model.layers.back().mask->background = 0;
    const auto output = render(model);
    compare_recipe(model, output);
    range_hash(output);
  }
  std::puts(
      "BTS_GPU_IMPORTED_GENERATOR_FALLBACK_VERIFIED cases=2 missing_maps=1 uv_grunge=1 native_cpu_oracle=1");
  for (int kind : {1, 6}) {
    auto model = fixture(smart_mask_probe::asset("Rust"), false);
    spatial_maps(model);
    model.projection_geometry.reset();
    auto target = model.layers.back();
    TextureLayer producer;
    producer.kind = LayerKind::Fill;
    producer.visible = false;
    producer.values.base_color = {.22f, .6f, .84f, 1};
    LevelsEffect point;
    point.kind = ContentEffectKind::Anchor;
    point.channel = 0;
    producer.effects = {point};
    MaskEffect e;
    e.kind = MaskEffectKind::Generator;
    e.generator = kind;
    e.grunge_amount = .7f;
    e.triplanar_grunge = e.surface_fallback = true;
    e.input_anchors[0] = {point.id, 0, 0, false};
    target.mask->effects = {e};
    target.mask->background = 0;
    model.layers = {producer, target};
    const auto anchored = render(model);
    compare_recipe(model, anchored);
    range_hash(anchored);
    ++anchor_frames;
  }
  std::printf(
      "BTS_GPU_SMART_MASK_FALLBACK_VERIFIED no_atlas_or_uncovered=%d anchored_mesh_frames=%d atlas_policy=mesh_frame_semantic_anchor_separate\n",
      fallback_cases,
      anchor_frames);
  std::printf(
      "BTS_GPU_SMART_MASKS_VERIFIED presets=8 distinct=%zu fill_folder_applications=%d causal_maps=%d fallback_bake_updates=%d generator_seams=%d precision=RGBA32F native_stack_oracle=1 oracle_probes=%d oracle_maximum=%.9g\n",
      unique.size(),
      applications,
      causal,
      fallback_updates,
      seams,
      oracle_probes,
      oracle_maximum);
  // Two actual fenced packets. The second request intentionally examines get()
  // BEFORE polling its new fence; the old completed result must be rejected.
  {
    auto first = texture(
             "BN first thumbnail source", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32),
         second = texture(
             "BN second thumbnail source", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
    GpuLayerPreview writer;
    if (!writer.uniform(first.get(), {.2f, .2f, .2f, 1}) ||
        !writer.uniform(second.get(), {.8f, .8f, .8f, 1}))
      throw std::runtime_error("BN thumbnail source setup failed");
    GpuThumbnail packets;
    const auto wait = [&](std::uint64_t ticket) {
      const auto deadline = performance::Clock::now() + std::chrono::seconds(3);
      while (packets.pending()) {
        packets.poll();
        if (performance::Clock::now() > deadline)
          throw std::runtime_error("BN thumbnail fence timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      const auto value = packets.get();
      if (!smart_mask_thumbnail_packet_ready(packets.pending(), value ? &*value : nullptr, ticket))
        throw std::runtime_error("BN thumbnail ticket does not match completed request");
      return *value;
    };
    packets.update({}, {{first.get(), 128, 128}}, 41);
    const auto old = wait(41);
    packets.update({}, {{second.get(), 128, 128}}, 42);
    const auto retained = packets.get();
    if (!packets.pending() || !retained || retained->ticket != 41 ||
        smart_mask_thumbnail_packet_ready(packets.pending(), &*retained, 42))
      throw std::runtime_error("BN preceding thumbnail published while next packet was pending");
    const auto current = wait(42);
    if (std::abs(old.pixels[0][0] - .2f) > 1e-6f || std::abs(current.pixels[0][0] - .8f) > 1e-6f)
      throw std::runtime_error("BN sequential thumbnail data reused preceding source");
    std::puts(
        "BTS_GPU_SMART_MASK_THUMBNAIL_TICKETS_VERIFIED sequential=2 stale_pending_rejected=1 tickets=41,42");
  }
}
