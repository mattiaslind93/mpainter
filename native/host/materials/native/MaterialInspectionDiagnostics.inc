// SPDX-License-Identifier: GPL-3.0-or-later
// Read-back diagnostics (copy_pixels, image_hash, sample_pixel, verify_gpu_pixels); never on interactive paths.
// A section of native/host/MaterialInspection.cc, which includes it in place; not a standalone file.
std::vector<float> MaterialInspection::copy_pixels(std::uint32_t uid, int channel, int tile) const
{
  const auto &binding = impl_->bindings.at(uid);
  if (channel == 15 && binding.mask_snapshot) {
    const auto &set = *binding.mask_snapshot;
    std::vector<float> pixels(std::size_t(set.width(tile)) * set.rows(tile) * 4);
    for (int y = 0; y < set.rows(tile); ++y)
      for (int x = 0; x < set.width(tile); ++x) {
        const auto value = gpu_logical_pixel(set, 15, x, y, tile);
        std::copy(value.begin(),
                  value.end(),
                  pixels.begin() + (std::size_t(y) * set.width(tile) + x) * 4);
      }
    return pixels;
  }
  if (binding.gpu_stack_active.at(channel) && binding.gpu_stack_set) {
    // Explicit diagnostics/export boundary, never a viewport composition path.
    // Include native padding and cross-tile dependencies just like persistence;
    // sampling the unpadded authoring stack gave misleading roundtrip hashes.
    std::fprintf(
        stderr, "BTS_GPU_CPU_REFERENCE reason=diagnostic-or-export-query channel=%d\n", channel);
    CompositeChannels wanted{};
    wanted[channel] = true;
    std::atomic_bool cancelled = false;
    const auto &set = *binding.gpu_stack_set;
    auto reference = native_composite_job(set,
                                          bool(binding.ao_image),
                                          binding.padding ? binding.padding->clone() : nullptr,
                                          0,
                                          cancelled,
                                          false,
                                          wanted);
    if (!reference)
      throw std::runtime_error("CPU diagnostic reference cancelled");
    return std::move(reference->tiles.at(tile)[channel]);
  }
  if (channel < 5 && binding.gpu_uniform_values[channel]) {
    const auto size = binding.tile_sizes.at(tile);
    std::vector<float> pixels(std::size_t(size.first) * size.second * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4)
      std::copy(binding.gpu_uniform_values[channel]->begin(),
                binding.gpu_uniform_values[channel]->end(),
                pixels.begin() + i);
    return pixels;
  }
  auto *image = channel == 16 ? binding.displacement_image :
                channel == 15 ? binding.mask_image :
                channel >= 7  ? binding.user_images.at(channel - 7) :
                channel == 6  ? binding.authored_normal_image :
                channel == 5  ? binding.ao_image :
                                binding.images.at(channel);
  if (!image)
    return {};
  ImageUser user{};
  user.tile = tile;
  auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
  struct Release {
    Image *image;
    ImBuf *buffer;
    ~Release()
    {
      if (buffer)
        BKE_image_release_ibuf(image, buffer, nullptr);
    }
  } release{image, buffer};
  if (!buffer || !buffer->float_data() || buffer->channels != 4)
    throw std::runtime_error("Export material buffer unavailable");
  return {buffer->float_data(), buffer->float_data() + std::size_t(buffer->x) * buffer->y * 4};
}
std::uint64_t MaterialInspection::image_hash(std::uint32_t uid, int channel, int tile) const
{
  auto &binding = impl_->bindings.at(uid);
  if ((channel == 15 && binding.mask_snapshot) ||
      (binding.gpu_stack_active.at(channel) && binding.gpu_stack_set))
  {
    const auto pixels = copy_pixels(uid, channel, tile);
    std::uint64_t hash = 1469598103934665603ULL;
    for (float value : pixels) {
      hash ^= std::bit_cast<std::uint32_t>(value);
      hash *= 1099511628211ULL;
    }
    return hash;
  }
  if (channel < 5 && binding.gpu_uniform_values[channel]) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto size = binding.tile_sizes.at(tile);
    for (std::size_t i = 0; i < std::size_t(size.first) * size.second; ++i)
      for (float value : *binding.gpu_uniform_values[channel]) {
        hash ^= std::bit_cast<std::uint32_t>(value);
        hash *= 1099511628211ULL;
      }
    return hash;
  }
  auto *image = binding.preview_active.at(channel) ? binding.preview_images[channel] :
                channel == 16                      ? binding.displacement_image :
                channel == 15                      ? binding.mask_image :
                channel >= 7                       ? binding.user_images.at(channel - 7) :
                channel == 6                       ? binding.authored_normal_image :
                channel == 5                       ? binding.ao_image :
                                                     binding.images.at(channel);
  if (!image)
    throw std::runtime_error("No derived image");
  ImageUser user{};
  user.tile = tile;
  auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
  if (!buffer || !buffer->float_data()) {
    if (buffer)
      BKE_image_release_ibuf(image, buffer, nullptr);
    throw std::runtime_error("Derived image is unavailable");
  }
  std::uint64_t hash = 1469598103934665603ULL;
  const auto *data = buffer->float_data();
  for (std::size_t i = 0; i < std::size_t(buffer->x) * buffer->y * 4; ++i) {
    hash ^= std::bit_cast<std::uint32_t>(data[i]);
    hash *= 1099511628211ULL;
  }
  BKE_image_release_ibuf(image, buffer, nullptr);
  return hash;
}
std::array<float, 4> MaterialInspection::sample_pixel(
    std::uint32_t uid, int channel, int x, int y, int tile) const
{
  auto &binding = impl_->bindings.at(uid);
  if (channel == 15 && binding.mask_snapshot)
    return gpu_logical_pixel(*binding.mask_snapshot, 15, x, y, tile);
  if (binding.gpu_stack_active.at(channel) && binding.gpu_stack_set) {
    if (channel == 3) {
      const auto &set = *binding.gpu_stack_set;
      if (x < 0 || y < 0 || x >= set.width(tile) || y >= set.rows(tile))
        throw std::runtime_error("Invalid GPU normal sample");
      const auto pixels = copy_pixels(uid, channel, tile);
      std::array<float, 4> result;
      std::copy_n(pixels.data() + (std::size_t(y) * set.width(tile) + x) * 4, 4, result.begin());
      return result;
    }
    return gpu_logical_pixel(*binding.gpu_stack_set, channel, x, y, tile);
  }
  if (channel < 5 && binding.gpu_uniform_values[channel])
    return *binding.gpu_uniform_values[channel];
  auto *image = binding.preview_active.at(channel) ? binding.preview_images[channel] :
                channel == 16                      ? binding.displacement_image :
                channel == 15                      ? binding.mask_image :
                channel >= 7                       ? binding.user_images.at(channel - 7) :
                channel == 6                       ? binding.authored_normal_image :
                channel == 5                       ? binding.ao_image :
                                                     binding.images.at(channel);
  if (!image)
    throw std::runtime_error("No composite image");
  ImageUser user{};
  user.tile = tile;
  auto *buffer = BKE_image_acquire_ibuf(image, &user, nullptr);
  if (buffer && binding.preview_active.at(channel)) {
    const auto size = binding.tile_sizes.at(tile);
    x = int(double(x) * buffer->x / size.first);
    y = int(double(y) * buffer->y / size.second);
  }
  if (!buffer || !buffer->float_data() || x < 0 || y < 0 || x >= buffer->x || y >= buffer->y) {
    if (buffer)
      BKE_image_release_ibuf(image, buffer, nullptr);
    throw std::runtime_error("Invalid composite sample");
  }
  std::array<float, 4> result;
  std::copy_n(buffer->float_data() + (std::size_t(y) * buffer->x + x) * 4, 4, result.begin());
  BKE_image_release_ibuf(image, buffer, nullptr);
  return result;
}
std::vector<std::array<float, 4>> MaterialInspection::diagnostic_gpu_pixels(
    std::uint32_t uid,
    int channel,
    std::span<const std::array<int, 2>> coordinates,
    int tile) const
{
  const auto found = impl_->bindings.find(uid);
  if (found == impl_->bindings.end())
    throw std::runtime_error("GPU diagnostic material is missing");
  auto *image = composite_image(found->second, channel);
  if (!image)
    throw std::runtime_error("GPU diagnostic channel is missing");
  const auto native = BKE_image_gpu_override_texture(image);
  if (!native.texture || !*native.texture)
    throw std::runtime_error("GPU diagnostic channel has no native texture");
  ImageGpuTileRegion region;
  if (!BKE_image_gpu_override_tile_region(image, tile, region))
    throw std::runtime_error("GPU diagnostic tile is missing");
  std::vector<std::array<int, 3>> positions;
  positions.reserve(coordinates.size());
  for (const auto &p : coordinates) {
    if (p[0] < 0 || p[1] < 0 || p[0] >= region.width || p[1] >= region.height)
      throw std::invalid_argument("GPU diagnostic coordinate is out of bounds");
    positions.push_back({region.x + p[0], region.y + p[1], region.layer});
  }
  std::vector<std::array<float, 4>> result;
  result.reserve(positions.size());
  for (std::size_t first = 0; first < positions.size(); first += 4096) {
    const auto values = probe_gpu_texture(
        *native.texture,
        std::span(positions).subspan(first,
                                     std::min<std::size_t>(4096, positions.size() - first)));
    result.insert(result.end(), values.begin(), values.end());
  }
  return result;
}
std::optional<std::array<float, 4>> MaterialInspection::diagnostic_gpu_pixel(
    std::uint32_t uid, int channel, int x, int y, int tile) const
{
  const auto found = impl_->bindings.find(uid);
  if (found == impl_->bindings.end())
    return {};
  auto *image = composite_image(found->second, channel);
  if (!image)
    return {};
  const auto native = BKE_image_gpu_override_texture(image);
  if (!native.texture || !*native.texture)
    return {};
  ImageGpuTileRegion region;
  if (!BKE_image_gpu_override_tile_region(image, tile, region) || x < 0 || y < 0 ||
      x >= region.width || y >= region.height)
    return {};
  const std::array<std::array<int, 3>, 1> positions{{{region.x + x, region.y + y, region.layer}}};
  return probe_gpu_texture(*native.texture, positions).front();
}
double MaterialInspection::verify_gpu_pixels(std::uint32_t uid) const
{
  auto &binding = impl_->bindings.at(uid);
  if (!binding.gpu_stack_set)
    throw std::runtime_error("GPU stack diagnostic has no snapshot");
  const auto &set = *binding.gpu_stack_set;
  if (const auto reason = spatial_oracle_unavailable(set)) {
    std::printf("BTS_NATIVE_GPU_ORACLE_UNAVAILABLE scope=whole_material reason=%s\n",
                reason->c_str());
    throw SpatialOracleUnavailable("Whole-material CPU parity unavailable: " + *reason);
  }
  CompositeChannels channels{};
  for (int c = 0; c < 17; ++c)
    if (binding.gpu_stack_active[c]) {
      auto *image = composite_image(binding, c);
      if (image) {
        auto native = BKE_image_gpu_override_texture(image);
        channels[c] = native.texture && *native.texture;
      }
    }
  std::atomic_bool cancelled = false;
  auto oracle = native_composite_job(set,
                                     bool(binding.ao_image),
                                     binding.padding ? binding.padding->clone() : nullptr,
                                     0,
                                     cancelled,
                                     false,
                                     channels);
  if (!oracle)
    throw std::runtime_error("GPU diagnostic CPU oracle cancelled");
  double maximum = 0;
  int probes = 0, checked_channels = 0;
  for (int c = 0; c < 17; ++c)
    if (channels[c]) {
      ++checked_channels;
      auto *image = composite_image(binding, c);
      auto *texture = *BKE_image_gpu_override_texture(image).texture;
      std::vector<std::array<int, 3>> positions, logical_positions;
      std::vector<std::array<float, 4>> expected;
      for (int tile : set.tiles) {
        ImageGpuTileRegion region;
        if (!BKE_image_gpu_override_tile_region(image, tile, region) ||
            region.width != set.width(tile) || region.height != set.rows(tile))
          throw std::runtime_error("GPU diagnostic target is not texture-set resolution");
        const auto &pixels = oracle->tiles.at(tile)[c];
        const bool dense = std::getenv("BTS_GPU_DENSE_ORACLE");
        const int nx = dense ? region.width : 8, ny = dense ? region.height : 8;
        for (int iy = 0; iy < ny; ++iy)
          for (int ix = 0; ix < nx; ++ix) {
            const int x = dense ? ix : (2 * ix + 1) * region.width / 16,
                      y = dense ? iy : (2 * iy + 1) * region.height / 16;
            logical_positions.push_back({tile, x, y});
            positions.push_back({region.x + x, region.y + y, region.layer});
            std::array<float, 4> value;
            std::copy_n(pixels.data() + (std::size_t(y) * region.width + x) * 4, 4, value.begin());
            expected.push_back(value);
          }
      }
      std::vector<std::array<float, 4>> actual;
      for (std::size_t first = 0; first < positions.size(); first += 4096) {
        const auto batch = probe_gpu_texture(
            texture,
            std::span(positions).subspan(first,
                                         std::min<std::size_t>(4096, positions.size() - first)));
        actual.insert(actual.end(), batch.begin(), batch.end());
      }
      double error = 0;
      for (std::size_t i = 0; i < actual.size(); ++i)
        for (int k = 0; k < 4; ++k) {
          const double delta = std::abs(double(actual[i][k]) - expected[i][k]);
          error = std::max(error, delta);
          // A float32 projection can cross an sRGB8 rounding boundary relative to
          // the double CPU oracle. Accept at most one encoded code step, including
          // native half-float storage error; alpha retains its original tolerance.
          bool adjacent_code = false;
          if (c == 0 && k < 3 && set.precision[0] == ChannelPrecision::SRGB8 && delta <= .0095) {
            const auto base = precision_channel(set.base_values, 0, set.precision[0]);
            const auto code = [](float value) {
              value = std::clamp(value, 0.f, 1.f);
              return int(std::lround(255 * (value <= .0031308f ?
                                                12.92f * value :
                                                1.055f * std::pow(value, 1.f / 2.4f) - .055f)));
            };
            adjacent_code = std::abs(code(actual[i][k] + base[k] * (1 - actual[i][3])) -
                                     code(expected[i][k] + base[k] * (1 - expected[i][3]))) <= 1;
          }
          if (!std::isfinite(actual[i][k]) ||
              (delta > 0.002 * std::max(1.f, std::abs(expected[i][k])) && !adjacent_code))
          {
            auto *raw = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
            const auto position = positions[i];
            const std::size_t offset = ((std::size_t(position[2]) * GPU_texture_height(texture) +
                                         position[1]) *
                                            GPU_texture_width(texture) +
                                        position[0]) *
                                       4;
            std::printf(
                "BTS_GPU_ORACLE_DIAG channel=%d probe=%zu position=%d,%d,%d gather=%.8g,%.8g,%.8g,%.8g raw=%.8g,%.8g,%.8g,%.8g expected=%.8g,%.8g,%.8g,%.8g\n",
                c,
                i,
                position[0],
                position[1],
                position[2],
                actual[i][0],
                actual[i][1],
                actual[i][2],
                actual[i][3],
                raw[offset],
                raw[offset + 1],
                raw[offset + 2],
                raw[offset + 3],
                expected[i][0],
                expected[i][1],
                expected[i][2],
                expected[i][3]);
            MEM_delete(raw);
            const auto location = logical_positions[i];
            const int tile = location[0], px = location[1], py = location[2];
            const auto logical = gpu_logical_pixel(set, c, px, py, tile);
            std::printf(
                "BTS_GPU_ORACLE_LOGICAL tile=%d x=%d y=%d value=%.9g,%.9g,%.9g,%.9g precision=%d\n",
                tile,
                px,
                py,
                logical[0],
                logical[1],
                logical[2],
                logical[3],
                int(gpu_channel_precision(set, c)));
            impl_->gpu_preview.diagnose_pixel(set, c, tile, px, py);
            for (const auto &layer : evaluation_layers(set)) {
              const auto channel = gpu_layer_channel(layer, c);
              const auto paint = channel.pixels->sample_resampled(
                  channel.plane, px, py, set.width(tile), tile, set.rows(tile));
              const auto fill = TextureDocument::fill_sample(
                  set, layer, channel.fill_channel, px, py, tile);
              std::printf(
                  "BTS_GPU_ORACLE_CHANNEL id=%s depth=%d enabled=%d opacity=%.9g blend=%d value=%.9g paint=%.9g,%.9g fill=%.9g,%.9g\n",
                  layer.id.toString().toUtf8().constData(),
                  layer.depth,
                  channel.enabled,
                  channel.opacity,
                  int(channel.blend),
                  channel.value[0],
                  paint[0],
                  paint[3],
                  fill[0],
                  fill[3]);
            }
            for (const auto &layer : evaluation_layers(set))
              std::printf(
                  "BTS_GPU_ORACLE_LAYER kind=%d mode=%d atom=%d texture=%d opacity=%.3f blend=%d visible=%d mask=%d rgb=%.5f,%.5f,%.5f\n",
                  int(layer.kind),
                  layer.fill.mode,
                  layer.fill.atom,
                  bool(layer.fill.texture),
                  layer.opacity[0],
                  int(layer.blending[0]),
                  layer.visible,
                  bool(layer.mask),
                  layer.values.base_color[0],
                  layer.values.base_color[1],
                  layer.values.base_color[2]);
            throw std::runtime_error(
                "Native GPU pipeline differs from CPU oracle channel=" + std::to_string(c) +
                " probe=" + std::to_string(i) + " component=" + std::to_string(k) + " actual=" +
                std::to_string(actual[i][k]) + " expected=" + std::to_string(expected[i][k]));
          }
        }
      maximum = std::max(maximum, error);
      probes += int(actual.size());
    }
  if (!probes)
    throw std::runtime_error("No native GPU channel was verified");
  std::printf(
      "BTS_NATIVE_GPU_ORACLE channels=%d probes=%d tiles=%zu max_error=%.9g full_resolution=1\n",
      checked_channels,
      probes,
      set.tiles.size(),
      maximum);
  return maximum;
}
bool MaterialInspection::has_pixels(std::uint32_t uid) const
{
  const auto it = impl_->bindings.find(uid);
  return it != impl_->bindings.end() && it->second.resolution != 0;
}
