// SPDX-License-Identifier: GPL-3.0-or-later
// A full update may retire a thumbnail query, then defer on a source upload.
// Its physical outputs and revision plan must be retired together: otherwise
// the next thumbnail treats a freed prefix as current and throws map::at.
{
  TextureSet model;
  model.texture_resolution = 128;
  model.tiles = {1001, 1002};
  model.precision.fill(ChannelPrecision::RGB32F);
  auto old_image = std::make_shared<FillTexture>();
  old_image->hash = std::string(64, 'a');
  old_image->width = old_image->height = 128;
  old_image->pixels = std::make_shared<std::vector<FillTexture::Pixel>>(
      128 * 128, FillTexture::Pixel{.23f, .4f, .6f, 1});
  model.layers.front().kind = LayerKind::Fill;
  model.layers.front().fill.texture = old_image;
  const auto old_id = model.layers.front().id;
  GpuLayerPreview isolated;
  std::uint64_t ticket = 0;
  const auto thumbnail = [&] {
    isolated.begin_frame();
    if (isolated.thumbnail(model, {old_id, 0, false}, ++ticket) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("Deferred-update thumbnail did not execute");
    const auto started = performance::Clock::now();
    while (isolated.impl_->thumbnails.pending()) {
      isolated.impl_->thumbnails.poll();
      if (performance::ms(started) > 3000)
        throw std::runtime_error("Deferred-update thumbnail fence timeout");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto result = isolated.thumbnail_result();
    if (!result || result->ticket != ticket || std::abs(result->pixels[0][0] - .23f) > 1e-6)
      throw std::runtime_error("Deferred-update thumbnail lost its source pixels");
    ++cases;
  };
  thumbnail();
  TextureDocument::add_fill(model);
  auto next = std::make_shared<FillTexture>();
  next->hash = std::string(64, 'b');
  next->width = next->height = 128;
  next->pixels = std::make_shared<std::vector<FillTexture::Pixel>>(
      128 * 128, FillTexture::Pixel{.9f, .2f, .1f, 1});
  model.layers.back().fill.texture = next;
  isolated.begin_frame();
  isolated.impl_->remaining = 0;
  if (isolated.update(model, 0, target.get()) != GpuPreviewUpdate::Pending)
    throw std::runtime_error("Thumbnail regression failed to defer the replacement source");
  ++cases;
  thumbnail();
  isolated.begin_frame();
  if (isolated.update(model, 0, target.get()) != GpuPreviewUpdate::Updated)
    throw std::runtime_error("Deferred replacement failed to finish");
  ++cases;
  thumbnail();
  std::puts(
      "BTS_GPU_DEFERRED_THUMBNAIL_VERIFIED tiles=2 cases=5 partial_upload=1 retired_query_recomputed=1 cpu_composition=0");
}
