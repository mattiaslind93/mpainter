#pragma once
#include "GpuBrushRaster.hh"
#include "GpuProjectReadback.hh"
#include "GpuTextureProbe.hh"
#include "GPU_viewport.hh"
#include "GPU_viewport_offscreen_cache.hh"
#include "GPU_framebuffer.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_storage_buffer_poll.hh"
#include <future>
#include "GpuPaintPages.hh"
#include "SmudgeStroke.hh"
#include "CloneStroke.hh"
#include "GpuPathRasterTest.hh"
#include "MEM_guardedalloc.h"
namespace bts {
inline int verify_gpu_brush_raster()
{
  using namespace blender;
  using namespace blender::gpu::host_texture_memory;
  using Image = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  {
    const auto before = live_bytes(), count_before = live_count();
    {
      Image owned(GPU_texture_create_2d_array("BTS storage accounting regression",
                                              16,
                                              8,
                                              3,
                                              4,
                                              gpu::TextureFormat::SFLOAT_32_32_32_32,
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr),
                  GPU_texture_free);
      constexpr std::uint64_t expected = (16 * 8 + 8 * 4 + 4 * 2 + 2 * 1) * 3 * 16;
      if (!owned || live_bytes() - before != expected || live_count() != count_before + 1) {
        const auto values = snapshot();
        const auto found = values.find("BTS storage accounting regression");
        std::fprintf(
            stderr,
            "BTS_GPU_STORAGE_TEST expected=%llu before=%llu after=%llu count_before=%llu count_after=%llu label_bytes=%llu\n",
            (unsigned long long)expected,
            (unsigned long long)before,
            (unsigned long long)live_bytes(),
            (unsigned long long)count_before,
            (unsigned long long)live_count(),
            (unsigned long long)(found == values.end() ? 0 : found->second.bytes));
        throw std::runtime_error("GPU storage mip/layer/format accounting differs");
      }
      const auto groups = snapshot();
      if (!groups.contains("BTS storage accounting regression") ||
          groups.at("BTS storage accounting regression").bytes != expected)
        throw std::runtime_error("GPU storage label accounting differs");
      Image alias(GPU_texture_create_view("BTS storage alias regression",
                                          owned.get(),
                                          GPU_texture_format(owned.get()),
                                          1,
                                          2,
                                          0,
                                          3,
                                          false,
                                          false),
                  GPU_texture_free);
      if (!alias || live_bytes() - before != expected || live_count() != count_before + 1)
        throw std::runtime_error("GPU view duplicated its owner's storage accounting");
      owned.reset();
      if (live_bytes() - before != expected || live_count() != count_before + 1)
        throw std::runtime_error(
            "GPU view lost its retained storage accounting when its original owner died");
      alias.reset();
      if (live_bytes() != before || live_count() != count_before)
        throw std::runtime_error("GPU view retained dead storage accounting");
    }
    if (live_bytes() != before || live_count() != count_before)
      throw std::runtime_error("GPU storage accounting leaked released objects");
    std::puts(
        "BTS_GPU_STORAGE_ACCOUNT_VERIFIED bytes=8160 mips=4 layers=3 view_alias=1 released=1 orphan_view_retained=1");
  }

  {
    constexpr int width = 1025, height = 1027;
    const float expected[4] = {-.001f, .002f, 2.25f, 0};
    Image flat(GPU_texture_create_2d("BTS save readback 2D",
                                     width,
                                     height,
                                     1,
                                     gpu::TextureFormat::SFLOAT_32_32_32_32,
                                     GPU_TEXTURE_USAGE_GENERAL,
                                     nullptr),
               GPU_texture_free);
    Image array(GPU_texture_create_2d_array("BTS save readback UDIM",
                                            width,
                                            height,
                                            2,
                                            1,
                                            gpu::TextureFormat::SFLOAT_32_32_32_32,
                                            GPU_TEXTURE_USAGE_GENERAL,
                                            nullptr),
                GPU_texture_free);
    // Deliberately exceed multiple 4MiB packets with non-aligned rows/workgroups.
    // Every row/column differs, including signed/HDR values and zero alpha.
    std::vector<float> pattern(std::size_t(width) * height * 4);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const auto at = (std::size_t(y) * width + x) * 4;
        pattern[at] = float(x - width / 2) / 1024;
        pattern[at + 1] = float(y) / 512;
        pattern[at + 2] = 2.25f;
        pattern[at + 3] = 0;
      }
    GPU_texture_update(flat.get(), GPU_DATA_FLOAT, pattern.data());
    GPU_texture_clear(array.get(), GPU_DATA_FLOAT, expected);
    GpuProjectReadback transfer;
    GLint previous_row = 0;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &previous_row);
    glPixelStorei(GL_PACK_ROW_LENGTH, 173);
    transfer.submit(17, 1001, flat.get(), 0, 0, 0, width, height);
    transfer.submit(18, 1002, array.get(), 3, 7, 1, 19, 23);
    transfer.submit_native(19, 1001, flat.get(), 0, 0, 0, width, height);
    auto *writable = flat.release();
    if (!GpuProjectReadback::protect_for_write(&writable))
      throw std::runtime_error("Native project snapshot did not protect its source");
    flat.reset(writable);
    GLint restored = 0;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &restored);
    glPixelStorei(GL_PACK_ROW_LENGTH, previous_row);
    if (restored != 173)
      throw std::runtime_error("Project readback changed pixel packing state");
    // Queue a subsequent edit before collecting: saved pixels must remain the
    // submitted revision, including alpha=0 and unclamped signed/HDR values.
    const float changed[4] = {1, 1, 1, 1};
    GPU_texture_clear(flat.get(), GPU_DATA_FLOAT, changed);
    GPU_texture_clear(array.get(), GPU_DATA_FLOAT, changed);
    const auto start = performance::Clock::now();
    while (!transfer.poll()) {
      if (performance::ms(start) > 5000)
        throw std::runtime_error("Project readback did not complete");
      std::this_thread::yield();
    }
    auto worker = std::async(std::launch::async, [&] {
      for (const auto &image : transfer.images())
        for (std::size_t i = 0; i < image.bytes() / sizeof(float); ++i)
          if ((image.uniform ? image.pixels[i % 4] : image.pixels[i]) !=
              (image.uid == 18 ? expected[i % 4] : pattern[i]))
            throw std::runtime_error("Fenced project readback differs from submitted pixels");
    });
    worker.get();
    if (transfer.images()[0].uniform || !transfer.images()[1].uniform)
      throw std::runtime_error("GPU uniform classification missed a last-pixel difference");
    std::puts(
        "BTS_GPU_PROJECT_READBACK_VERIFIED images=3 native_cow=1 signed_hdr_alpha_zero=1 udim_array_region=1 snapshot_revision=1 worker_read=1 wait_timeout=0 packing_restored=1");
  }
  {
    std::unique_ptr<GPUViewport, decltype(&GPU_viewport_free)> viewport(GPU_viewport_create(),
                                                                        GPU_viewport_free);
    GPU_viewport_offscreen_cache_set(viewport.get(), true);
    const auto target = [](int w, int h) {
      char error[256] = {};
      auto *value = GPU_offscreen_create(w,
                                         h,
                                         true,
                                         gpu::TextureFormat::UNORM_8_8_8_8,
                                         GPU_TEXTURE_USAGE_SHADER_READ,
                                         true,
                                         error);
      if (!value)
        throw std::runtime_error(error);
      return std::unique_ptr<GPUOffScreen, decltype(&GPU_offscreen_free)>(value,
                                                                          GPU_offscreen_free);
    };
    auto screen = target(64, 48);
    GPU_offscreen_bind(screen.get(), true);
    GPU_viewport_bind_from_offscreen(viewport.get(), screen.get(), false);
    auto *original = GPU_viewport_color_texture(viewport.get(), 0);
    const float marker[4] = {1, 0, 1, 1};
    GPU_texture_clear(GPU_viewport_overlay_texture(viewport.get(), 0), GPU_DATA_FLOAT, marker);
    GPU_viewport_unbind_from_offscreen(viewport.get(), screen.get(), false, false);
    GPU_offscreen_unbind(screen.get(), true);
    GPU_offscreen_bind(screen.get(), true);
    GPU_viewport_bind_from_offscreen(viewport.get(), screen.get(), false);
    auto stats = GPU_viewport_offscreen_cache_stats(viewport.get());
    if (stats.allocations != 1 || stats.reuses != 1 ||
        GPU_viewport_color_texture(viewport.get(), 0) != original)
      throw std::runtime_error("Offscreen frame recreated color storage");
    const auto pixel = probe_gpu_texture(GPU_viewport_overlay_texture(viewport.get(), 0),
                                         std::array<std::array<int, 3>, 1>{{{13, 17, 0}}})
                           .front();
    if (pixel != std::array<float, 4>{})
      throw std::runtime_error("Cached offscreen overlay was not cleared");
    GPU_viewport_unbind_from_offscreen(viewport.get(), screen.get(), false, false);
    GPU_offscreen_unbind(screen.get(), true);
    screen = target(80, 64);
    GPU_offscreen_bind(screen.get(), true);
    GPU_viewport_bind_from_offscreen(viewport.get(), screen.get(), false);
    stats = GPU_viewport_offscreen_cache_stats(viewport.get());
    if (stats.allocations != 2 || stats.reuses != 1 ||
        GPU_texture_width(GPU_viewport_color_texture(viewport.get(), 0)) != 80 ||
        GPU_texture_height(GPU_viewport_color_texture(viewport.get(), 0)) != 64)
      throw std::runtime_error("Offscreen resize retained incompatible storage");
    GPU_viewport_unbind_from_offscreen(viewport.get(), screen.get(), false, false);
    GPU_offscreen_unbind(screen.get(), true);
    std::puts(
        "BTS_GPU_OFFSCREEN_REUSE_VERIFIED frames=3 stable_color=1 clear_overlay=1 resize_reallocated=1");
  }
  {
    std::array<std::uint32_t, 8> expected{}, changed{}, actual{};
    std::unique_ptr<gpu::StorageBuf, decltype(&GPU_storagebuf_free)> buffer(
        GPU_storagebuf_create_ex(
            sizeof(expected), nullptr, GPU_USAGE_DYNAMIC, "BTS fenced statistics regression"),
        GPU_storagebuf_free);
    if (!buffer)
      throw std::runtime_error("Storage readback allocation failed");
    for (unsigned iteration = 0; iteration < 64; ++iteration) {
      for (unsigned i = 0; i < expected.size(); ++i) {
        expected[i] = iteration * 7919 + i * 37;
        changed[i] = ~expected[i];
      }
      actual.fill(0x12345678u);
      const auto untouched = actual;
      if (iteration == 0 &&
          (GPU_storagebuf_read_if_ready(buffer.get(), actual.data()) || actual != untouched))
        throw std::runtime_error("Unsubmitted storage read changed destination");
      GPU_storagebuf_update(buffer.get(), expected.data());
      GPU_storagebuf_sync_to_host(buffer.get());
      const auto polled = performance::Clock::now();
      const bool ready = GPU_storagebuf_read_if_ready(buffer.get(), actual.data());
      if (performance::ms(polled) > 5)
        throw std::runtime_error("Nonblocking storage poll stalled");
      if ((ready && actual != expected) || (!ready && actual != untouched))
        throw std::runtime_error("Nonblocking storage read changed the wrong revision");
      if (ready)
        GPU_storagebuf_sync_to_host(buffer.get());
      // A subsequent source write must not alter the queued snapshot.
      GPU_storagebuf_update(buffer.get(), changed.data());
      GPU_storagebuf_read(buffer.get(), actual.data());
      if (actual != expected)
        throw std::runtime_error("Asynchronous storage readback observed a later source revision");
    }
    std::puts(
        "BTS_GPU_STORAGE_READBACK_VERIFIED cases=64 captured_revision=1 staging=1 nonblocking=1");
  }
  {
    // The allocator must not recycle the deleted name before the range bind:
    // create both textures first, then destroy the image-only middle slot.
    Image middle(GPU_texture_create_2d("BTS image-only lifetime",
                                       1,
                                       1,
                                       1,
                                       gpu::TextureFormat::SFLOAT_32_32_32_32,
                                       GPU_TEXTURE_USAGE_GENERAL,
                                       nullptr),
                 GPU_texture_free);
    Image sides(GPU_texture_create_2d("BTS image range lifetime",
                                      1,
                                      1,
                                      1,
                                      gpu::TextureFormat::SFLOAT_32_32_32_32,
                                      GPU_TEXTURE_USAGE_GENERAL,
                                      nullptr),
                GPU_texture_free);
    while (glGetError() != GL_NO_ERROR) {
    }
    GPU_texture_image_bind(middle.get(), 1);
    GPU_apply_state();
    middle.reset();
    GPU_texture_image_bind(sides.get(), 0);
    GPU_texture_image_bind(sides.get(), 2);
    GPU_apply_state();
    if (glGetError() != GL_NO_ERROR)
      throw std::runtime_error("Deleted image-only texture remained in the binding cache");
    GLint stale = 0;
    glGetIntegeri_v(GL_IMAGE_BINDING_NAME, 1, &stale);
    if (stale)
      throw std::runtime_error("Released image slot stayed bound");
    GPU_texture_image_unbind(sides.get());
    GPU_apply_state();
    std::puts(
        "BTS_GPU_IMAGE_LIFETIME_VERIFIED sampler_unbound=1 deleted_middle_slot=1 bind_range_valid=1");
  }
  constexpr int size = 128;
  const float empty[4] = {};
  int cases = 0;
  float maximum_error = 0, allowed_error = 2e-5;
  const auto make = [](const char *name) {
    return Image(GPU_texture_create_2d(name,
                                       size,
                                       size,
                                       1,
                                       gpu::TextureFormat::SFLOAT_32_32_32_32,
                                       GPU_TEXTURE_USAGE_GENERAL,
                                       nullptr),
                 GPU_texture_free);
  };
  auto ink_image = make("BTS test GPU ink"), before_image = make("BTS test GPU before"),
       result_image = make("BTS test GPU stroke result");
  const auto check = [&](gpu::Texture *image, const PaintPixels &pixels, int channel) {
    auto *values = static_cast<float *>(GPU_texture_read(image, GPU_DATA_FLOAT, 0));
    float error = 0;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const auto expected = pixels.sample(channel, x, y);
        for (int c = 0; c < 4; ++c)
          error = std::max(error, std::abs(values[(y * size + x) * 4 + c] - expected[c]));
      }
    MEM_delete(values);
    maximum_error = std::max(maximum_error, error);
    if (error > allowed_error)
      throw std::runtime_error("GPU brush raster differs from CPU: " + std::to_string(error));
    ++cases;
  };
  GpuBrushRaster gpu;
  for (int variant = 0; variant < 20; ++variant) {
    PaintPixels::Stamp stamp{{.517, .473}, {.312, .071}, {-.093, .257}};
    stamp.values.base_color = {.8, .2, .45, 1};
    stamp.values.metallic = .7;
    stamp.values.roughness = .19;
    stamp.values.normal = {.2, -.3, .9};
    stamp.values.bump = -.75;
    stamp.alpha.kind = AlphaKind(variant % 5);
    stamp.alpha.uniform = variant == 2 ? -.2f : variant == 7 ? 1.7f : .37f;
    stamp.alpha.inverted = variant % 2;
    stamp.hardness = variant % 4 == 0 ? 1 : .37;
    stamp.flow = .13 + .04 * variant;
    stamp.alpha.dynamic.phase = double(variant) / 20;
    stamp.alpha.dynamic.roundness = .37;
    stamp.alpha.dynamic.width = .16;
    if (stamp.alpha.kind == AlphaKind::Bitmap) {
      auto bitmap = std::make_shared<AlphaBitmap>();
      bitmap->width = 13;
      bitmap->height = 7;
      for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 13; ++x)
          bitmap->pixels.push_back(float((x * 3 + y * 7) % 17) / 16);
      stamp.alpha.bitmap = bitmap;
    }
    allowed_error =
        stamp.alpha.kind == AlphaKind::DynamicOutline ?
            5e-5f :
            2e-5f;  // Sharp procedural edge: float GPU coordinates vs double CPU reference.
    if (variant >= 10) {
      auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
      surface->center = {.517, .473, 0};
      surface->radius = .31;
      surface->projected = variant % 2;
      surface->unwrapped = variant % 3 == 0;
      surface->planar_fade = variant % 3 == 1;
      surface->cull = true;
      const std::array<std::array<double, 2>, 4> uv{
          {{.07, .09}, {.93, .09}, {.93, .91}, {.07, .91}}};
      for (const auto indices : {std::array{0, 1, 2}, std::array{0, 2, 3}}) {
        PaintPixels::SurfaceTriangle t;
        t.seam_edges = {true, true, true};
        for (int v = 0; v < 3; ++v) {
          const auto point = uv[indices[v]];
          t.uv[v] = point;
          t.position[v] = {point[0], point[1], .1 * (point[0] - .5)};
          t.paint_coordinates[v] = {point[0] - .517, point[1] - .473};
        }
        surface->triangles.push_back(t);
      }
      stamp.surface = surface;
    }
    for (int channel = 0; channel < 5; ++channel) {
      GPU_texture_clear(ink_image.get(), GPU_DATA_FLOAT, empty);
      PaintPixels ink;
      for (int dab = 0; dab < 3; ++dab) {
        auto sample = stamp;
        sample.flow *= float(dab + 1) / 3;
        ink.stamp(size, sample);
        if (!gpu.stamp(ink_image.get(), channel, sample))
          throw std::runtime_error("GPU stamp unsupported");
      }
      check(ink_image.get(), ink, channel);
      std::array<PaintPixels::Stamp, 3> batch;
      std::array<const PaintPixels::Stamp *, 3> commands;
      for (int dab = 0; dab < 3; ++dab) {
        batch[dab] = stamp;
        batch[dab].flow *= float(dab + 1) / 3;
        commands[dab] = &batch[dab];
      }
      GPU_texture_clear(ink_image.get(), GPU_DATA_FLOAT, empty);
      if (!gpu.stamps(ink_image.get(), channel, commands))
        throw std::runtime_error("GPU batch stamp unsupported");
      check(ink_image.get(), ink, channel);
      PaintPixels before;
      auto base = stamp;
      base.surface.reset();
      base.flow = .47;
      base.values.base_color = {.1, .9, .3, 1};
      base.values.bump = .5;
      before.stamp(size, base);
      std::vector<float> baseline(size * size * 4);
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          const auto p = before.sample(channel, x, y);
          std::copy(p.begin(), p.end(), baseline.begin() + (y * size + x) * 4);
        }
      GPU_texture_update(before_image.get(), GPU_DATA_FLOAT, baseline.data());
      for (bool erase : {false, true}) {
        GPU_texture_update(result_image.get(), GPU_DATA_FLOAT, baseline.data());
        auto result = before;
        const auto bounds = GpuBrushRaster::bounds(size, stamp);
        result.over_stroke(before, ink, .61, bounds, erase, true);
        if (!gpu.over(
                result_image.get(), before_image.get(), ink_image.get(), .61, bounds, erase, true))
          throw std::runtime_error("GPU whole-stroke opacity failed");
        check(result_image.get(), result, channel);
      }
    }
  }
  std::printf(
      "BTS_GPU_BRUSH_RASTER_VERIFIED cases=%d channels=5 uv_surface=1 alpha_shapes=5 flow=1 whole_stroke_opacity_erase=1 max_error=%.9g production_readback=0\n",
      cases,
      maximum_error);
  if (gpu.metadata_allocations() > 5)
    throw std::runtime_error(
        "Brush metadata reallocated between batch and stroke-opacity commands");
  std::printf("BTS_GPU_BRUSH_BUFFERS_VERIFIED cases=400 allocations=%llu growing_capacity=1\n",
              (unsigned long long)gpu.metadata_allocations());
  allowed_error = 2e-5;
  // Evaluate the persistent page graph without materializing any CPU pixels.
  // Keep actual CPU rasterization as an independent reference for each step.
  {
    GpuPaintPages pages;
    PaintPixels authored, reference, accumulated, cpu_accumulated;
    PaintPixels::Stamp stamp{{.23, .37}, {.11, .04}, {-.03, .16}};
    stamp.values.base_color = {.8, .3, .1, 1};
    stamp.values.bump = -.71;
    stamp.flow = .4;
    stamp.hardness = .23;
    stamp.channels = {true, false, false, false, true};
    PaintPixels before;
    before.stamp(size, stamp);
    authored = reference = before;
    std::vector<PaintPixels> history, cpu_history;
    const auto cpu_count = PaintPixels::recipe_materializations();
    const auto render = [&](const PaintPixels &value, const PaintPixels &oracle) {
      const auto versions = value.page_versions();
      for (int channel : {0, 4}) {
        GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
        for (const auto &[number, revision] : versions[channel])
          if (!pages.write(result_image.get(), value, channel, number))
            throw std::runtime_error("Unbudgeted GPU authoring unexpectedly deferred");
        check(result_image.get(), oracle, channel);
      }
      if (PaintPixels::recipe_materializations() != cpu_count)
        throw std::runtime_error("GPU authoring materialized CPU pixels");
    };
    for (int i = 0; i < 10; ++i) {
      stamp.center[0] = .17 + i * .065;
      stamp.flow = .1f + .08f * i;
      const auto bounds = cpu_accumulated.stamp(size, stamp);
      reference.over_stroke(before, cpu_accumulated, .67, bounds, false, true);
      {
        PaintRecordingScope record;
        accumulated.stamp(size, stamp);
        authored.over_stroke(before, accumulated, .67, bounds, false, true);
      }
      if (i == 0) {
        const auto number = authored.page_versions()[0].begin()->first;
        if (pages.write(result_image.get(), authored, 0, number, performance::Clock::now()) ||
            pages.computed_pages() != 0)
          throw std::runtime_error("Expired GPU frame budget must defer unchanged authoring work");
      }
      history.push_back(authored);
      cpu_history.push_back(reference);
      render(authored, reference);
    }
    const auto computed = pages.computed_pages();
    render(history[3], cpu_history[3]);
    render(history.back(), cpu_history.back());
    if (pages.computed_pages() != computed)
      throw std::runtime_error("Undo recomputed cached immutable GPU pages");
    auto erased = before;
    auto expected = before;
    {
      PaintRecordingScope record;
      erased.over_stroke(before, accumulated, .8, {0, 0, size, size}, true, true);
    }
    expected.over_stroke(before, cpu_accumulated, .8, {0, 0, size, size}, true, true);
    render(erased, expected);
    std::printf(
        "BTS_GPU_PAINT_BUDGET_VERIFIED deadline_defers=1 resumed_exact=1 no_partial_publish=1\n");
    // Persistence materializes later, and retains exact legacy CPU pixel hashes.
    for (int channel = 0; channel < 5; ++channel)
      if (authored.hash(channel) != reference.hash(channel))
        throw std::runtime_error("Recorded authoring changed saved pixels");
    std::printf(
        "BTS_GPU_PAINT_RECIPES_VERIFIED steps=10 undo_cached=1 erase=1 cpu_during_gpu=0 saved_hash_exact=1 pages=%zu computed=%llu source_bytes=%llu\n",
        pages.resident_pages(),
        (unsigned long long)pages.computed_pages(),
        (unsigned long long)pages.source_upload_bytes());
  }
  {
    GpuPaintPages bounded(8 * 64 * 64 * 16);
    PaintPixels base;
    PaintPixels::Stamp dab{{.23, .23}, {.1, 0}, {0, .1}};
    dab.channels = {true, false, false, false, false};
    dab.flow = .4;
    base.stamp(size, dab);
    const auto begin = performance::Clock::now();
    std::vector<PaintPixels> saved, oracle;
    for (int i = 0; i < 24; ++i) {
      auto value = base, expected = base;
      dab.values.base_color = {float(i) / 24, .2f, .7f, 1};
      {
        PaintRecordingScope record;
        value.stamp(size, dab);
      }
      expected.stamp(size, dab);
      saved.push_back(value);
      oracle.push_back(expected);
      GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
      const auto versions = value.page_versions();
      for (const auto &[number, version] : versions[0])
        if (!bounded.write(result_image.get(), value, 0, number))
          throw std::runtime_error("Bounded GPU page render deferred");
      check(result_image.get(), expected, 0);
    }
    GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
    const auto restored_versions = saved.front().page_versions();
    for (const auto &[number, version] : restored_versions[0])
      bounded.write(result_image.get(), saved.front(), 0, number);
    check(result_image.get(), oracle.front(), 0);
    if (!bounded.evicted_pages() || bounded.resident_pages() > 8)
      throw std::runtime_error("GPU page LRU did not respect its working-set budget");
    if (bounded.image_allocations() > 8 || !bounded.image_reuses())
      throw std::runtime_error("GPU page cache reallocated evicted image storage");
    std::printf("BTS_GPU_PAGE_REUSE_VERIFIED allocations=%llu reuses=%llu restored_exact=1\n",
                (unsigned long long)bounded.image_allocations(),
                (unsigned long long)bounded.image_reuses());
    std::printf(
        "BTS_GPU_PAINT_LRU_VERIFIED steps=24 evicted=%llu pages=%zu reload_exact=1 wall_ms=%.3f\n",
        (unsigned long long)bounded.evicted_pages(),
        bounded.resident_pages(),
        performance::ms(begin));
  }
  {
    GpuPaintPages pages;
    PaintPixels recorded, reference;
    PaintPixels::Stamp dab{{.23, .23}, {.1, 0}, {0, .1}};
    dab.channels = {true, false, false, false, false};
    dab.flow = .01;
    for (int i = 0; i < 512; ++i) {
      reference.stamp(size, dab);
      PaintRecordingScope record;
      recorded.stamp(size, dab);
    }
    const auto begin = performance::Clock::now();
    GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
    const auto versions = recorded.page_versions();
    for (const auto &[number, version] : versions[0])
      if (!pages.write(result_image.get(), recorded, 0, number))
        throw std::runtime_error("GPU command batch deferred without budget");
    check(result_image.get(), reference, 0);
    if (pages.computed_pages() > 8)
      throw std::runtime_error("GPU stamp chain was not batched into page dispatches");
    std::printf(
        "BTS_GPU_PAINT_BATCH_VERIFIED dabs=512 dispatches=%llu cpu_oracle=1 wall_ms=%.3f\n",
        (unsigned long long)pages.computed_pages(),
        performance::ms(begin));
  }
  // A complete cumulative stroke replaces only its changed pages. Intermediate
  // opacity previews and rolled-back spline tails are not GPU dependencies.
  for (bool erase : {false, true}) {
    GpuPaintPages pages;
    PaintPixels before, authored, reference, stable, cpu_stable;
    PaintPixels::Stamp dab{{.7, .7}, {.09, 0}, {0, .09}};
    dab.channels = {true, false, false, false, false};
    dab.flow = .4;
    before.stamp(size, dab);
    authored = reference = before;
    PaintPixels::Bounds previous{};
    for (int i = 0; i < 100; ++i) {
      dab.center = {.22 + .04 * std::sin(i * .3), .23 + .04 * std::cos(i * .2)};
      dab.axis_x = {.05, 0};
      dab.axis_y = {0, .05};
      auto ink = stable, expected_ink = cpu_stable;
      auto bounds = expected_ink.stamp(size, dab);
      {
        PaintRecordingScope record;
        ink.stamp(size, dab);
      }
      auto dirty = bounds;
      if (!previous.empty())
        dirty = {std::min(dirty.x0, previous.x0),
                 std::min(dirty.y0, previous.y0),
                 std::max(dirty.x1, previous.x1),
                 std::max(dirty.y1, previous.y1)};
      reference.over_stroke(before, expected_ink, .63, dirty, erase, true);
      {
        PaintRecordingScope record;
        authored.over_stroke(before, ink, .63, dirty, erase, true, true);
      }
      if (i % 3 != 0) {
        stable = ink;
        cpu_stable = expected_ink;
      }
      previous = bounds;
    }
    GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
    const auto versions = authored.page_versions();
    for (const auto &[number, version] : versions[0])
      if (!pages.write(result_image.get(), authored, 0, number))
        throw std::runtime_error("Complete stroke page unexpectedly deferred");
    check(result_image.get(), reference, 0);
    if (pages.computed_pages() > 6)
      throw std::runtime_error("Complete stroke replayed intermediate opacity previews");
    if (authored.hash(0) != reference.hash(0))
      throw std::runtime_error("Complete stroke changed persistent CPU pixels");
    std::printf(
        "BTS_GPU_COMPLETE_STROKE_VERIFIED previews=100 erase=%d rollback_tail=1 dispatches=%llu cpu_oracle=1 saved_exact=1\n",
        erase,
        (unsigned long long)pages.computed_pages());
  }
  // Quick Mask gate: shared immutable source, UV and surface coverage, signed
  // color data, UDIM addressing, inversion and whole-stroke replay remain exact.
  {
    GpuPaintPages pages;
    auto field = std::make_shared<QuickMaskField>();
    field->resolution = 256;
    PaintPixels::Stamp stripe{{.5, .5}, {.12, 0}, {0, .4}};
    stripe.channels = {false, false, true, false, false};
    stripe.values.roughness = 0;
    stripe.flow = .8;
    stripe.hardness = .4;
    {
      PaintRecordingScope record;
      field->pixels.stamp(256, stripe, {1001});
      stripe.center = {1.3, .5};
      field->pixels.stamp(256, stripe, {1002});
      stripe.center = {.6, 1.5};
      field->pixels.stamp(256, stripe, {1011});
    }
    for (int tile : {1001, 1002, 1011, 1003})
      for (bool inverted : {false, true})
        for (bool world : {false, true}) {
          auto gate = std::make_shared<QuickMaskField>(*field);
          gate->inverted = inverted;
          PaintPixels::Stamp dab{{.5, .5}, {.4, 0}, {0, .3}};
          dab.coverage_mask = gate;
          dab.stencil_uv_offset = {double((tile - 1001) % 10), double((tile - 1001) / 10)};
          dab.hardness = .3;
          dab.flow = .63;
          dab.values.base_color = {.7, .2, .3, 1};
          dab.values.bump = -.7;
          if (world) {
            auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
            surface->center = {.5, .5, 0};
            surface->radius = .4;
            surface->projected = true;
            surface->triangles = {
                {{{{0, 0}, {1, 0}, {1, 1}}}, {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}}, {}},
                {{{{0, 0}, {1, 1}, {0, 1}}}, {{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}}}, {}}};
            dab.surface = surface;
          }
          PaintPixels authored, reference;
          for (int i = 0; i < 3; ++i) {
            dab.flow = .2 + .2 * i;
            reference.stamp(size, dab);
            PaintRecordingScope record;
            authored.stamp(size, dab);
          }
          const auto allocations = pages.coverage_allocations();
          for (int channel : {0, 2, 4}) {
            GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
            const auto versions = authored.page_versions();
            for (const auto &[number, version] : versions[channel])
              if (!pages.write(result_image.get(), authored, channel, number))
                throw std::runtime_error("Quick Mask brush source deferred");
            check(result_image.get(), reference, channel);
          }
          if (pages.coverage_allocations() - allocations > 1)
            throw std::runtime_error("Quick Mask source duplicated between channels");
          if (authored.hash(0) != reference.hash(0))
            throw std::runtime_error("Quick Mask GPU recipe changed saved CPU pixels");
        }
    auto blocked = std::make_shared<QuickMaskField>();
    blocked->inverted = true;
    PaintPixels unchanged;
    auto dab = stripe;
    dab.center = {.5, .5};
    dab.coverage_mask = blocked;
    {
      PaintRecordingScope record;
      if (!unchanged.stamp(size, dab).empty() || !unchanged.empty())
        throw std::runtime_error("Fully blocked Quick Mask stroke created authoring data");
    }
    std::printf(
        "BTS_GPU_QUICK_GATE_VERIFIED tiles=4 channels=3 uv_surface=1 inversion=1 shared_source=1 empty_inverted_noop=1 cpu_saved_exact=1 production_readback=0\n");
  }
  // Stencil sampling is evaluated by the same GPU authoring pass as brush ink.
  // The CPU reference uses its original double-precision camera/UV implementation.
  {
    const auto first_case = cases;
    GpuPaintPages pages;
    auto bitmap = std::make_shared<AlphaBitmap>();
    bitmap->width = 13;
    bitmap->height = 7;
    for (int y = 0; y < 7; ++y)
      for (int x = 0; x < 13; ++x)
        bitmap->pixels.push_back(float((x * 3 + y * 7) % 17) / 16);
    for (int mode = 0; mode < 4; ++mode)
      for (int kind = 0; kind < 5; ++kind)
        for (int tiling = 0; tiling < 4; ++tiling)
          for (bool inverted : {false, true}) {
            PaintPixels::Stamp dab{{.517, .473}, {.312, .071}, {-.093, .257}};
            dab.flow = .63;
            dab.hardness = .37;
            dab.values.base_color = {.8, .2, .45, 1};
            dab.values.metallic = .7;
            dab.values.roughness = .19;
            dab.values.normal = {.2, -.3, .9};
            dab.values.bump = -.75;
            auto mapping = std::make_shared<StencilMapping>();
            mapping->world = mode == 1 || mode == 2;
            mapping->viewport_aspect = 1.3;
            mapping->uv_extent = .93;
            mapping->projection = {2, 0, 0, -1, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
            mapping->stencil.scale = .6817;
            mapping->stencil.angle = 23.17;
            mapping->stencil.offset = {.037, -.021};
            mapping->stencil.tiling = tiling;
            BrushAlpha alpha;
            alpha.kind = AlphaKind(kind);
            alpha.uniform = .37;
            alpha.inverted = inverted;
            alpha.bitmap = kind == 3 ? bitmap : nullptr;
            alpha.dynamic.phase = .371;
            alpha.dynamic.roundness = .31;
            alpha.dynamic.width = .173;
            mapping->stencil.resource = alpha;
            const std::array<StencilTriangle, 2> triangles{
                {{{{{0, 0}, {1, 0}, {1, 1}}}, {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}}},
                 {{{{0, 0}, {1, 1}, {0, 1}}}, {{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}}}}}};
            if (mode == 1) {
              auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
              surface->center = {.517, .473, 0};
              surface->radius = .31;
              surface->projected = true;
              for (const auto &triangle : triangles) {
                PaintPixels::SurfaceTriangle t;
                t.uv = triangle.uv;
                t.position = triangle.position;
                surface->triangles.push_back(t);
              }
              dab.surface = surface;
            }
            if (mode == 2)
              mapping->uv_world.assign(triangles.begin(), triangles.end());
            if (mode == 3) {
              dab.stencil_uv_offset = {1, 1};
              mapping->uv_center = {1.5, 1.5};
            }
            if (tiling == 3 && mode != 3) {
              mapping->lens.enabled = true;
              mapping->lens.distortion = .21;
              mapping->lens.roundness = .12;
              mapping->lens.smoothness = .3;
            }
            dab.stencil = mapping;
            PaintPixels authored, reference;
            reference.stamp(size, dab);
            {
              PaintRecordingScope record;
              authored.stamp(size, dab);
            }
            allowed_error = kind == 4 ? 8e-5f : 2e-5f;
            for (int channel = 0; channel < 5; ++channel) {
              GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
              const auto versions = authored.page_versions();
              for (const auto &[number, version] : versions[channel])
                if (!pages.write(result_image.get(), authored, channel, number))
                  throw std::runtime_error("GPU stencil authoring deferred");
              try {
                check(result_image.get(), reference, channel);
              }
              catch (const std::exception &error) {
                throw std::runtime_error(
                    "Stencil mode=" + std::to_string(mode) + " kind=" + std::to_string(kind) +
                    " tiling=" + std::to_string(tiling) + " inverted=" + std::to_string(inverted) +
                    ": " + error.what());
              }
            }
            if (authored.hash(0) != reference.hash(0))
              throw std::runtime_error("GPU stencil recipe changed persisted CPU pixels");
          }
    // Brush and stencil reference the very same image: upload it only once.
    GpuBrushRaster shared;
    PaintPixels::Stamp dab{{.5, .5}, {.2, 0}, {0, .2}};
    dab.alpha.kind = AlphaKind::Bitmap;
    dab.alpha.bitmap = bitmap;
    auto mapping = std::make_shared<StencilMapping>();
    mapping->stencil.resource = dab.alpha;
    dab.stencil = mapping;
    PaintPixels reference;
    reference.stamp(size, dab);
    GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
    if (!shared.stamp(result_image.get(), 0, dab) || shared.alpha_allocations() != 1)
      throw std::runtime_error("Brush/stencil bitmap not shared");
    allowed_error = 2e-5;
    check(result_image.get(), reference, 0);
    // More UV triangles than the driver's maximum texture height must pack across
    // columns, without dropping the final triangle or changing its world mapping.
    auto packed = std::make_shared<StencilMapping>();
    packed->world = true;
    packed->projection = {2, 0, 0, -1, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
    packed->stencil.resource = BrushAlpha{};
    packed->uv_world.resize(GPU_max_texture_size() + 1);
    packed->uv_world.push_back({{{{0, 0}, {1, 0}, {0, 1}}}, {{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}});
    dab.stencil = packed;
    dab.alpha = BrushAlpha{};
    dab.center = {.3, .3};
    dab.axis_x = {.007, 0};
    dab.axis_y = {0, .007};
    reference = PaintPixels{};
    reference.stamp(size, dab);
    GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
    if (!shared.stamp(result_image.get(), 0, dab))
      throw std::runtime_error("Packed stencil triangles unsupported");
    check(result_image.get(), reference, 0);
    std::printf(
        "BTS_GPU_STENCIL_VERIFIED cases=%d uv_world_udim=1 alpha_kinds=5 tiling=4 inversion=1 lens=1 shared_bitmap=1 packed_triangles=1 cpu_saved_exact=1 production_readback=0\n",
        cases - first_case);
  }
  // Material projection uses the same camera mapping as stencil but supplies
  // channel values, including procedural weave and signed scalar overrides.
  {
    const auto first_case = cases;
    GpuPaintPages pages;
    for (int mode = 0; mode < 4; ++mode)
      for (bool weave : {false, true})
        for (int tiling = 0; tiling < 4; ++tiling)
          for (bool override_value : {false, true}) {
            PaintPixels::Stamp dab{{.517, .473}, {.312, .071}, {-.093, .257}};
            dab.flow = .63;
            dab.hardness = .37;
            auto projected = std::make_shared<ProjectionMapping>();
            auto &mapping = projected->mapping;
            mapping.world = mode == 1 || mode == 2;
            mapping.viewport_aspect = 1.3;
            mapping.uv_extent = .93;
            mapping.projection = {2, 0, 0, -1, 0, 2, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1};
            auto &source = projected->source;
            source.transform.scale = .6817;
            source.transform.angle = 23.17;
            source.transform.offset = {.037, -.021};
            source.transform.tiling = tiling;
            source.weave = weave;
            source.density = 37;
            source.values.base_color = {.8, .2, 1.45, 1};
            source.values.metallic = .7;
            source.values.roughness = .19;
            source.values.normal = {.2, -.3, .9};
            source.values.bump = -.75;
            if (override_value)
              projected->scalar_override = -.4;
            const std::array<StencilTriangle, 2> triangles{
                {{{{{0, 0}, {1, 0}, {1, 1}}}, {{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}}},
                 {{{{0, 0}, {1, 1}, {0, 1}}}, {{{0, 0, 0}, {1, 1, 0}, {0, 1, 0}}}}}};
            if (mode == 1) {
              auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
              surface->center = {.517, .473, 0};
              surface->radius = .31;
              surface->projected = true;
              for (const auto &triangle : triangles) {
                PaintPixels::SurfaceTriangle t;
                t.uv = triangle.uv;
                t.position = triangle.position;
                surface->triangles.push_back(t);
              }
              dab.surface = surface;
            }
            if (mode == 2)
              mapping.uv_world.assign(triangles.begin(), triangles.end());
            if (mode == 3) {
              dab.stencil_uv_offset = {1, 1};
              mapping.uv_center = {1.5, 1.5};
            }
            if (tiling == 3 && mode != 3) {
              mapping.lens.enabled = true;
              mapping.lens.distortion = -.21;
              mapping.lens.roundness = .12;
              mapping.lens.smoothness = .3;
            }
            if (override_value) {
              auto stencil = std::make_shared<StencilMapping>(mapping);
              BrushAlpha alpha;
              alpha.kind = AlphaKind::Uniform;
              alpha.uniform = .71;
              stencil->stencil.resource = alpha;
              dab.stencil = stencil;
            }
            dab.projected = projected;
            PaintPixels authored, reference;
            reference.stamp(size, dab);
            const auto cpu_before = PaintPixels::recipe_materializations();
            {
              PaintRecordingScope record;
              authored.stamp(size, dab);
            }
            allowed_error = weave ? 1e-4f : 2e-5f;
            for (int channel = 0; channel < 5; ++channel) {
              GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
              const auto versions = authored.page_versions();
              for (const auto &[number, version] : versions[channel])
                if (!pages.write(result_image.get(), authored, channel, number))
                  throw std::runtime_error("GPU projection authoring deferred");
              try {
                check(result_image.get(), reference, channel);
              }
              catch (const std::exception &error) {
                throw std::runtime_error(
                    "Projection mode=" + std::to_string(mode) + " weave=" + std::to_string(weave) +
                    " tiling=" + std::to_string(tiling) + " channel=" + std::to_string(channel) +
                    ": " + error.what());
              }
            }
            if (PaintPixels::recipe_materializations() != cpu_before)
              throw std::runtime_error("GPU projection materialized CPU pixels");
            if (authored.hash(0) != reference.hash(0))
              throw std::runtime_error("Projection recipe changed saved pixels");
          }
    std::printf(
        "BTS_GPU_MATERIAL_PROJECTION_VERIFIED cases=%d uv_world_udim=1 uniform_weave=1 tiling=4 lens=1 stencil_combined=1 scalar_override=1 cpu_saved_exact=1 production_readback=0\n",
        cases - first_case);
  }
  // Source capture and carried pickup never materialize CPU authoring recipes.
  {
    const auto first_case = cases;
    GpuPaintPages pages;
    TilePaintPixels source;
    PaintPixels::Stamp seed{{.97, .48}, {.43, .04}, {-.05, .37}};
    seed.values.base_color = {1.3, .2, .7, 1};
    seed.values.normal = {.1, -.3, .9};
    seed.values.bump = -.8;
    seed.flow = .83;
    {
      PaintRecordingScope record;
      source.stamp(size, seed, {1001, 1002});
      seed.center = {1.2, .6};
      seed.values.base_color = {.1, .8, .3, 1};
      source.stamp(size, seed, {1001, 1002});
    }
    for (int mode = 0; mode < 5; ++mode)
      for (int kind = 0; kind < 5; ++kind) {
        PaintPixels::Stamp dab{{1.017, .49}, {.123, .021}, {-.014, .137}};
        dab.channels = {true, true, true, true, true};
        dab.alpha.kind = AlphaKind(kind);
        dab.alpha.uniform = .37;
        dab.hardness = .43;
        dab.alpha.inverted = mode % 2;
        dab.alpha.dynamic.phase = .41;
        if (kind == 3) {
          auto bitmap = std::make_shared<AlphaBitmap>();
          bitmap->width = 7;
          bitmap->height = 5;
          for (int i = 0; i < 35; ++i)
            bitmap->pixels.push_back(float(i % 7) / 6);
          dab.alpha.bitmap = bitmap;
        }
        if (mode >= 1 && mode <= 3) {
          auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
          surface->center = {1.017, .49, 0};
          surface->radius = .17;
          surface->planar_fade = true;
          surface->unwrapped = mode == 3;
          PaintPixels::SurfaceTriangle a, b;
          a.uv = {{{.7, .2}, {1.3, .2}, {1.3, .8}}};
          b.uv = {{{.7, .2}, {1.3, .8}, {.7, .8}}};
          for (auto *t : {&a, &b})
            for (int i = 0; i < 3; ++i) {
              t->position[i] = {t->uv[i][0], t->uv[i][1], .031};
              t->paint_coordinates[i] = {t->uv[i][0] - surface->center[0],
                                         t->uv[i][1] - surface->center[1]};
            }
          surface->triangles = {a, b};
          if (mode == 2) {
            auto farther = a;
            for (auto &p : farther.position)
              p[2] = .1;
            for (auto &uv : farther.uv)
              uv[0] -= .5;
            surface->triangles.insert(surface->triangles.begin(), farther);
          }
          dab.surface = surface;
        }
        if (mode == 4) {
          auto stencil = std::make_shared<StencilMapping>();
          stencil->uv_center = {1, .5};
          stencil->uv_extent = .8;
          stencil->viewport_aspect = 1.2;
          stencil->stencil.scale = .7;
          stencil->stencil.angle = 21;
          stencil->stencil.tiling = 3;
          BrushAlpha alpha;
          alpha.kind = AlphaKind::Plus;
          stencil->stencil.resource = alpha;
          dab.stencil = stencil;
        }
        std::shared_ptr<SmudgePayload> payload;
        const auto reads = local_paint_materializations;
        {
          PaintRecordingScope record;
          payload = capture_brush_payload(source, dab, size);
        }
        std::array<std::shared_ptr<GpuSmudgePayload::Entry>, 5> outputs;
        for (int c = 0; c < 5; ++c)
          outputs[c] = pages.capture(payload, c);
        if (local_paint_materializations != reads)
          throw std::runtime_error("GPU capture read CPU source pixels");
        const auto &expected = payload->evaluated();
        for (int c = 0; c < 5; ++c) {
          auto *pixels = static_cast<float *>(
              GPU_texture_read(outputs[c]->channels[c].get(), GPU_DATA_FLOAT, 0));
          float error = 0;
          for (int i = 0; i < payload->side * payload->side; ++i)
            for (int k = 0; k < 4; ++k)
              error = std::max(error, std::abs(pixels[i * 4 + k] - expected.channels[c][i][k]));
          MEM_delete(pixels);
          if (error > 2e-5)
            throw std::runtime_error("GPU source capture mode=" + std::to_string(mode) + " kind=" +
                                     std::to_string(kind) + " error=" + std::to_string(error));
          ++cases;
        }
        auto *contact = static_cast<float *>(
            GPU_texture_read(outputs[0]->contact.get(), GPU_DATA_FLOAT, 0));
        float pickup_error = 0;
        for (int i = 0; i < payload->side * payload->side; ++i)
          pickup_error = std::max(pickup_error, std::abs(contact[i * 4 + 2] - expected.pickup[i]));
        MEM_delete(contact);
        if (pickup_error > 2e-5)
          throw std::runtime_error("GPU pickup differs: " + std::to_string(pickup_error));
        ++cases;
        dab.surface.reset();
        dab.stencil.reset();
        dab.center = {1.07, .53};
        dab.axis_x = {.08, 0};
        dab.axis_y = {0, .08};
        std::shared_ptr<SmudgePayload> next;
        {
          PaintRecordingScope record;
          auto current = capture_brush_payload(source, dab, size);
          next = std::make_shared<SmudgePayload>();
          next->side = current->side;
          next->recipe = std::make_shared<SmudgeCarry>(current, payload, .37f);
        }
        const auto carry_reads = local_paint_materializations;
        auto output = pages.capture(next, 0);
        if (local_paint_materializations != carry_reads)
          throw std::runtime_error("GPU carry read CPU source pixels");
        const auto &oracle = next->evaluated();
        auto *pixels = static_cast<float *>(
            GPU_texture_read(output->channels[0].get(), GPU_DATA_FLOAT, 0));
        float error = 0;
        for (int i = 0; i < next->side * next->side; ++i)
          for (int k = 0; k < 4; ++k)
            error = std::max(error, std::abs(pixels[4 * i + k] - oracle.channels[0][i][k]));
        MEM_delete(pixels);
        if (error > 2e-5)
          throw std::runtime_error("GPU carried pickup differs: " + std::to_string(error));
        ++cases;
      }
    std::printf(
        "BTS_GPU_SOURCE_CAPTURE_VERIFIED cases=%d uv_world_udim=1 nearest_contact=1 alpha_kinds=5 stencil=1 carried_pickup=1 cpu_reads=0\n",
        cases - first_case);
  }
  // Complete transport and lazy pen-up patches, including cold GPU history replay.
  {
    const auto first_case = cases;
    GpuPaintPages pages;
    TilePaintPixels source;
    PaintPixels::Stamp seed{{.93, .47}, {.12, 0}, {0, .14}};
    seed.alpha.kind = AlphaKind::Plus;
    seed.values.base_color = {1.2, .3, .7, 1};
    seed.values.metallic = .43;
    seed.values.roughness = .81;
    seed.values.normal = {.2, -.3, .9};
    seed.values.bump = -.51;
    source.stamp(size, seed, {1001, 1002});
    const auto verify_tiles =
        [&](const TilePaintPixels &actual, const TilePaintPixels &expected, GpuPaintPages &cache) {
          const auto reads = local_paint_materializations;
          for (int tile : {1001, 1002})
            for (int c = 0; c < 5; ++c) {
              GPU_texture_clear(result_image.get(), GPU_DATA_FLOAT, empty);
              const auto versions = actual.tile(tile).page_versions();
              for (const auto &[key, version] : versions[c])
                if (!cache.write(result_image.get(), actual.tile(tile), c, key))
                  throw std::runtime_error("GPU transport page deferred");
              check(result_image.get(), expected.tile(tile), c);
            }
          if (local_paint_materializations != reads)
            throw std::runtime_error("GPU transport materialized authoring pixels");
        };
    for (int mode = 0; mode < 3; ++mode) {
      SmudgeStroke gpu_stroke(source, size), oracle(source, size);
      PaintPixels::Stamp dab{{.93, .47}, {.064, 0}, {0, .071}};
      dab.flow = .57;
      dab.hardness = .43;
      dab.channels = {true, true, true, true, true};
      std::shared_ptr<PaintPixels::SurfaceFootprint> surface;
      if (mode == 1) {
        surface = std::make_shared<PaintPixels::SurfaceFootprint>();
        surface->radius = .13;
        surface->projected = true;
        PaintPixels::SurfaceTriangle a, b;
        a.uv = {{{.7, .2}, {1.5, .2}, {1.5, .8}}};
        b.uv = {{{.7, .2}, {1.5, .8}, {.7, .8}}};
        for (auto *t : {&a, &b})
          for (int v = 0; v < 3; ++v)
            t->position[v] = {t->uv[v][0], t->uv[v][1], 0};
        surface->triangles = {a, b};
        dab.surface = surface;
      }
      if (mode == 2) {
        auto stencil = std::make_shared<StencilMapping>();
        stencil->uv_center = {1, .5};
        stencil->uv_extent = 1;
        BrushAlpha alpha;
        alpha.kind = AlphaKind::Uniform;
        alpha.uniform = .63;
        stencil->stencil.resource = alpha;
        dab.stencil = stencil;
      }
      for (int i = 0; i < 14; ++i) {
        dab.center = {.93 + i * .018, .47};
        if (surface) {
          surface = std::make_shared<PaintPixels::SurfaceFootprint>(*surface);
          surface->center = {dab.center[0], dab.center[1], 0};
          dab.surface = surface;
        }
        oracle.dab(dab, {1001, 1002}, .073);
        oracle.publish(.67);
        const auto reads = local_paint_materializations;
        {
          PaintRecordingScope record;
          gpu_stroke.dab(dab, {1001, 1002}, .073);
          gpu_stroke.publish(.67);
        }
        if (local_paint_materializations != reads)
          throw std::runtime_error("Smudge gesture read CPU pixels");
        if (i == 4 || i == 13) {
          allowed_error = 3e-5;
          verify_tiles(gpu_stroke.pixels(), oracle.pixels(), pages);
        }
      }
      auto history = append_stroke({}, {}, {source, {}}, {source, {}}, true, false, 1);
      std::shared_ptr<const StrokeHistory> recorded;
      StrokeRaster replay;
      {
        PaintRecordingScope record;
        recorded = append_smudge(history, {source, {}}, {gpu_stroke.pixels(), {}}, true);
        replay = replay_strokes(*recorded, size);
      }
      GpuPaintPages cold;
      verify_tiles(replay.paint, oracle.pixels(), cold);
      if (stroke_hashes(replay) != stroke_hashes({oracle.pixels(), {}}))
        throw std::runtime_error("Smudge history/save pixels changed");
      {
        PaintRecordingScope record;
        gpu_stroke.publish(0);
      }
      verify_tiles(gpu_stroke.pixels(), source, pages);
    }
    for (bool absolute : {false, true}) {
      PaintPixels::Stamp sample{{.93, .47}, {.08, 0}, {0, .08}},
          dab{{1.15, .65}, {.08, 0}, {0, .08}};
      sample.channels = dab.channels = {true, true, true, true, true};
      dab.flow = .43;
      CloneStroke gpu_clone(source, size, sample, absolute),
          oracle(source, size, sample, absolute);
      TilePaintPixels actual, expected;
      for (int i = 0; i < 12; ++i) {
        dab.center = {1.15 + i * .009, .65};
        expected.stamp(size, oracle.dab(dab), {1001, 1002});
        const auto reads = local_paint_materializations;
        {
          PaintRecordingScope record;
          actual.stamp(size, gpu_clone.dab(dab), {1001, 1002});
        }
        if (local_paint_materializations != reads)
          throw std::runtime_error("Clone gesture read CPU pixels");
      }
      verify_tiles(actual, expected, pages);
      for (int c = 0; c < 5; ++c)
        if (actual.hash(c) != expected.hash(c))
          throw std::runtime_error("Clone saved pixels changed");
    }
    std::printf(
        "BTS_GPU_TRANSPORT_VERIFIED cases=%d clone_relative_absolute=1 smudge_uv_world_stencil=1 udim=2 channels=5 opacity=1 cold_history=1 saved_exact=1 cpu_reads=0\n",
        cases - first_case);
  }
  {
    GpuPaintPages pages;
    constexpr int full = 1024;
    TilePaintPixels source;
    std::vector<float> pixels(full * full * 4);
    for (int i = 0; i < full * full; ++i) {
      pixels[4 * i] = .7;
      pixels[4 * i + 1] = .2;
      pixels[4 * i + 2] = 1.4;
      pixels[4 * i + 3] = 1;
    }
    source.assign_channel(full, 0, pixels);
    PaintPixels::Stamp dab{{.5, .5}, {.01, 0}, {0, .01}};
    dab.channels = {true, false, false, false, false};
    auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
    surface->center = {0, 0, 0};
    surface->radius = .015;
    PaintPixels::SurfaceTriangle a, b;
    a.position = {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}};
    b.position = {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    for (auto *t : {&a, &b})
      for (int i = 0; i < 3; ++i)
        t->uv[i] = {.5 + .5 * t->position[i][0], .5 + .5 * t->position[i][1]};
    surface->triangles = {a, b};
    dab.surface = surface;
    std::shared_ptr<SmudgePayload> payload;
    {
      PaintRecordingScope record;
      payload = capture_brush_payload(source, dab, full);
    }
    auto result = pages.capture(payload, 0);
    if (pages.capture_staged_pages() != 4 || pages.source_upload_bytes() != 4 * 64 * 64 * 16)
      throw std::runtime_error("Coarse mesh gathered unrelated source pages");
    auto *values = static_cast<float *>(
        GPU_texture_read(result->channels[0].get(), GPU_DATA_FLOAT, 0));
    const auto &oracle = payload->evaluated();
    float error = 0;
    for (int i = 0; i < payload->side * payload->side; ++i)
      for (int c = 0; c < 4; ++c)
        error = std::max(error, std::abs(values[4 * i + c] - oracle.channels[0][i][c]));
    MEM_delete(values);
    if (error > 2e-5) {
      auto *contact = static_cast<float *>(
          GPU_texture_read(result->contact.get(), GPU_DATA_FLOAT, 0));
      auto *actual = static_cast<float *>(
          GPU_texture_read(result->channels[0].get(), GPU_DATA_FLOAT, 0));
      for (int i = 0; i < payload->side * payload->side; ++i)
        if (std::abs(actual[4 * i + 3] - oracle.channels[0][i][3]) > 2e-5) {
          std::fprintf(
              stderr,
              "BTS_CAPTURE_LOCALITY_MISMATCH x=%d y=%d actual=%g,%g,%g,%g expected=%g,%g,%g,%g contact=%g,%g,%g,%g\n",
              i % payload->side,
              i / payload->side,
              actual[4 * i],
              actual[4 * i + 1],
              actual[4 * i + 2],
              actual[4 * i + 3],
              oracle.channels[0][i][0],
              oracle.channels[0][i][1],
              oracle.channels[0][i][2],
              oracle.channels[0][i][3],
              contact[4 * i],
              contact[4 * i + 1],
              contact[4 * i + 2],
              contact[4 * i + 3]);
          break;
        }
      MEM_delete(actual);
      MEM_delete(contact);
      throw std::runtime_error("Clipped source gather changed pixels: " + std::to_string(error));
    }
    ++cases;
    std::printf(
        "BTS_GPU_CAPTURE_LOCALITY_VERIFIED source_pages=256 staged_pages=4 uploaded_bytes=%llu max_error=%g\n",
        (unsigned long long)pages.source_upload_bytes(),
        error);
  }
  return cases + verify_gpu_path_raster();
}
}  // namespace bts
