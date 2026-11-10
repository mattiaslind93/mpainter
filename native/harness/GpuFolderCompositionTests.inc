// SPDX-License-Identifier: GPL-3.0-or-later
// Executed only by the explicit private production-GPU self_test.
{
  GpuLayerPreview folders;
  auto output = texture("BTS Folder algebra", 128, 128, gpu::TextureFormat::SFLOAT_32_32_32_32);
  int folder_cases = 0;
  const auto verify = [&](const TextureSet &model, double expected, double coverage = 1) {
    for (int channel : {0, 1, 2, 4, 5, 7, 16}) {
      folders.begin_frame();
      if (folders.update_tile(model, channel, output.get(), 1001, {}, true) !=
          GpuPreviewUpdate::Updated)
        throw std::runtime_error("Folder algebra GPU deferred");
      auto *head = folders.diagnostic_composite_texture(model, channel, 1001);
      if (!head)
        throw std::runtime_error("Missing Folder prefix");
      auto *pixels = static_cast<float *>(GPU_texture_read(head, GPU_DATA_FLOAT, 0));
      if (!pixels)
        throw std::runtime_error("Missing Folder algebra readback");
      bool valid = true;
      for (int c = 0; c < 4; ++c)
        valid &= std::abs(pixels[c] - (c == 3 ? coverage : expected)) < 2e-6;
      MEM_delete(pixels);
      if (!valid)
        throw std::runtime_error("Folder GPU differs from independent premultiplied algebra");
      ++folder_cases;
    }
  };
  for (auto mode : {LayerBlend::Normal, LayerBlend::Passthrough})
    for (float mask : {0.f, .25f, 1.f})
      for (float opacity : {0.f, .6f, 1.f}) {
        const auto model = folder_test::fixture(mode, mask, opacity);
        const double full = mode == LayerBlend::Passthrough ? .8 * .5 : .5;
        verify(model, .8 + (full - .8) * double(mask) * opacity);
      }
  verify(folder_test::fixture(LayerBlend::Normal), .755);
  verify(folder_test::fixture(LayerBlend::Passthrough), .74);
  verify(folder_test::fixture(LayerBlend::Passthrough, .25f, .6f, .4f, .25f), .2165, .295);
  verify(folder_test::fixture(LayerBlend::Normal, .25f, .6f, .4f, .25f), .218, .295);
  for (auto mode : {LayerBlend::Normal, LayerBlend::Passthrough}) {
    auto model = folder_test::fixture(mode);
    auto inner = model.layers.back();
    inner.id = QUuid::createUuid();
    inner.parent = model.layers.back().id;
    inner.opacity.fill(.4f);
    inner.ao_opacity = inner.users[0].opacity = inner.displacement.opacity = .4f;
    inner.blending.fill(LayerBlend::Passthrough);
    inner.ao_blending = inner.users[0].blending = inner.displacement.blending =
        LayerBlend::Passthrough;
    inner.mask->background = .5f;
    model.layers[1].parent = inner.id;
    model.layers.insert(model.layers.end() - 1, inner);
    TextureDocument::validate_hierarchy(model);
    verify(model, mode == LayerBlend::Passthrough ? .788 : .791);
  }
  {
    auto hidden = folder_test::fixture(LayerBlend::Passthrough);
    hidden.layers.back().visible = false;
    verify(hidden, .8);
  }
  verify(folder_test::fixture(LayerBlend::Disabled), .8);
  // Folder-only opacity/mask changes retain expensive contextual child results.
  auto model = folder_test::fixture(LayerBlend::Passthrough);
  verify(model, .74);
  const auto child = model.layers[1].id.toString().toStdString();
  const auto revision = folders.content_node_revision(model, 2, 1001, child);
  model.layers.back().opacity.fill(.4f);
  model.layers.back().ao_opacity = model.layers.back().users[0].opacity =
      model.layers.back().displacement.opacity = .4f;
  verify(model, .76);
  if (!revision || folders.content_node_revision(model, 2, 1001, child) != revision)
    throw std::runtime_error("Folder-only edit recomputed contextual children");
  model = folder_test::fixture(LayerBlend::Passthrough);
  model.layers[0].values.base_color = {.6f, .6f, .6f, 1};
  model.layers[0].values.metallic = model.layers[0].values.roughness =
      model.layers[0].values.bump = model.layers[0].values.ambient_occlusion = .6f;
  model.layers[0].users[0].value = model.layers[0].displacement.value = .6f;
  verify(model, .555);
  // A real edit changes both sides of the256 chunk boundary. Compare cached,
  // fresh and Undo results, requiring nonzero right-of-boundary output change.
  model = folder_test::fixture(LayerBlend::Passthrough);
  model.texture_resolution = 512;
  model.layers[0].kind = LayerKind::Paint;
  std::vector<float> plane(512 * 512 * 4, .8f);
  for (std::size_t i = 3; i < plane.size(); i += 4)
    plane[i] = 1;
  model.layers[0].pixels.assign_channel(512, 2, plane);
  const auto original = model;
  auto large = texture(
      "BTS contextual Folder dirty", 512, 512, gpu::TextureFormat::SFLOAT_32_32_32_32);
  const auto render = [&](GpuLayerPreview &owner, const TextureSet &value) {
    owner.begin_frame();
    if (owner.update_tile(value, 2, large.get(), 1001, {}) != GpuPreviewUpdate::Updated)
      throw std::runtime_error("Folder cache GPU deferred");
    auto *raw = static_cast<float *>(GPU_texture_read(large.get(), GPU_DATA_FLOAT, 0));
    if (!raw)
      throw std::runtime_error("Folder cache readback failed");
    std::vector<float> result(raw, raw + 512 * 512 * 4);
    MEM_delete(raw);
    return result;
  };
  const auto before = render(folders, model);
  const auto source_hash = model.layers[0].pixels.hash(2);
  PaintPixels::Stamp stroke{{.5, .5}, {.025, 0}, {0, .025}};
  stroke.channels = {false, false, true, false, false};
  stroke.values.roughness = .6f;
  stroke.hardness = 1;
  model.layers[0].pixels.stamp(512, stroke);
  if (model.layers[0].pixels.hash(2) == source_hash)
    throw std::runtime_error("Folder cached source edit was a no-op");
  const auto cached = render(folders, model);
  GpuLayerPreview fresh;
  const auto rebuilt = render(fresh, model);
  int changed_right = 0;
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x) {
      const auto i = (std::size_t(y) * 512 + x) * 4;
      for (int c = 0; c < 4; ++c)
        if (std::abs(cached[i + c] - rebuilt[i + c]) > 2e-6)
          throw std::runtime_error("Contextual Folder cached/fresh mismatch");
      if (x >= 256 && std::abs(cached[i] - before[i]) > 1e-5)
        ++changed_right;
    }
  if (!changed_right)
    throw std::runtime_error("Folder edit did not change output across chunk boundary");
  const auto undone = render(folders, original);
  for (std::size_t i = 0; i < before.size(); ++i)
    if (std::abs(undone[i] - before[i]) > 2e-6)
      throw std::runtime_error("Contextual Folder Undo differs from original");
  std::printf(
      "BTS_GPU_FOLDER_COMPOSITION_VERIFIED cases=%d Normal=.755 Passthrough=.74 partial_coverage=1 nested=1 mask_opacity=1 cached_fresh_undo=1 changed_right=%d\n",
      folder_cases,
      changed_right);
}
