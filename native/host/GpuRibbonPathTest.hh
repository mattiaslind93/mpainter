#pragma once
#include "GpuPaintPages.hh"
#include "MEM_guardedalloc.h"
namespace bts {
inline int verify_gpu_ribbon_path(GpuPaintPages *prepared = nullptr)
{
  using namespace blender;
  constexpr int size = 128;
  using Image = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
  Image image(GPU_texture_create_2d("BTS ribbon oracle",
                                    size,
                                    size,
                                    1,
                                    gpu::TextureFormat::SFLOAT_32_32_32_32,
                                    GPU_TEXTURE_USAGE_GENERAL,
                                    nullptr),
              GPU_texture_free);
  GpuPaintPages owned;
  auto &pages = prepared ? *prepared : owned;
  int cases = 0;
  float maximum_error = 0;
  const auto check = [&](const TilePaintPixels &authored,
                         const TilePaintPixels &reference,
                         int channel,
                         int tile) {
    const auto before = PaintPixels::recipe_materializations();
    const float clear[4] = {};
    GPU_texture_clear(image.get(), GPU_DATA_FLOAT, clear);
    const auto &source = authored.tile(tile);
    const auto versions = source.page_versions();
    for (const auto &[key, version] : versions[channel])
      if (!pages.write(image.get(), source, channel, key))
        throw std::runtime_error("Ribbon page unexpectedly deferred");
    if (PaintPixels::recipe_materializations() != before)
      throw std::runtime_error("Ribbon sampled CPU authoring pixels");
    auto *data = static_cast<float *>(GPU_texture_read(image.get(), GPU_DATA_FLOAT, 0));
    float error = 0;
    int mismatches = 0;
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x) {
        const auto expected = reference.sample(channel, x, y, tile);
        float local = 0;
        for (int k = 0; k < 4; ++k) {
          if (!std::isfinite(data[4 * (y * size + x) + k]) || !std::isfinite(expected[k])) {
            MEM_delete(data);
            throw std::runtime_error("Nonfinite GPU path oracle");
          }
          local = std::max(local, std::abs(data[4 * (y * size + x) + k] - expected[k]));
        }
        error = std::max(error, local);
        if (local > 2e-5 && mismatches++ < 4)
          std::fprintf(
              stderr,
              "BTS_RIBBON_MISMATCH case=%d tile=%d channel=%d x=%d y=%d expected=%.9g,%.9g actual=%.9g,%.9g\n",
              cases,
              tile,
              channel,
              x,
              y,
              expected[0],
              expected[3],
              data[4 * (y * size + x)],
              data[4 * (y * size + x) + 3]);
      }
    MEM_delete(data);
    maximum_error = std::max(maximum_error, error);
    if (error > 2e-5)
      throw std::runtime_error("Ribbon GPU differs from CPU: case=" + std::to_string(cases) +
                               " tile=" + std::to_string(tile) + " channel=" +
                               std::to_string(channel) + " error=" + std::to_string(error));
    ++cases;
  };
  std::vector<PaintPixels::SurfaceTriangle> triangles;
  for (int tile : {1001, 1002, 1011}) {
    const double u = (tile - 1001) % 10, v = (tile - 1001) / 10;
    PaintPixels::SurfaceTriangle a, b;
    a.uv = {{{u, v}, {u + 1, v}, {u + 1, v + 1}}};
    a.position = {{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}}};
    b.uv = {{{u, v}, {u + 1, v + 1}, {u, v + 1}}};
    b.position = {{{-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    triangles.push_back(a);
    triangles.push_back(b);
  }
  const SurfacePathContact contact =
      [&](const SurfacePathPoint &p,
          float diameter,
          bool coverage,
          const BrushProjection &) -> std::optional<SurfaceBrushSample> {
    PaintPixels::Stamp stamp{
        {(p.position[0] + 1) * .5, (p.position[1] + 1) * .5}, {.1, 0}, {0, .1}};
    if (coverage) {
      auto surface = std::make_shared<PaintPixels::SurfaceFootprint>();
      surface->center = p.position;
      surface->radius = diameter / 100;
      surface->triangles = triangles;
      stamp.surface = surface;
    }
    return SurfaceBrushSample{
        stamp, {float(p.position[0]), float(p.position[1]), 0}, diameter / 100, 1};
  };
  SurfacePath original;
  original.id = "c0a80001-1234-4000-8000-000000000001";
  original.ribbon.enabled = true;
  original.brush.diameter = 26;
  original.points = {{{-.63, -.19, 0}, {0, 0, 1}, {.185, .405}, .35f},
                     {{.02, .27, 0}, {0, 0, 1}, {.51, .635}, .8f},
                     {{.67, -.14, 0}, {0, 0, 1}, {.835, .43}, 1}};
  original.brush.channels.fill(true);
  original.brush.values.bump = -.3;
  original.brush.values.normal = {.1, .3, .8};
  original.brush.ao = true;
  original.brush.values.ambient_occlusion = .4;
  original.brush.displacement = true;
  original.brush.displacement_value = -.0017;
  original.brush.users[0] = true;
  original.brush.user_values[0] = .7;
  std::array<int, 8> sizes;
  sizes.fill(size);
  auto translucent = std::make_shared<RibbonImage>();
  translucent->width = 2;
  translucent->height = 2;
  translucent->name = "HDR translucent ribbon";
  translucent->pixels = {
      {{1.4, .1, .7, .7}}, {{.2, .5, .1, .3}}, {{.1, .6, .8, .5}}, {{.2, .8, .1, 0}}};
  for (int variant = 0; variant < 17; ++variant) {
    auto path = original;
    std::shared_ptr<QuickMaskField> mask;
    path.ribbon.corner = RibbonCorner(variant % 4);
    if (variant == 4) {
      path.ribbon.tiling = RibbonTiling::Custom;
      path.ribbon.count = 3;
      path.ribbon.mirror = true;
    }
    if (variant == 5) {
      path.ribbon.orientation = 1;
      path.ribbon.flip = true;
    }
    if (variant == 6) {
      path.ribbon.stretch = RibbonStretch::KeepAspect;
      path.ribbon.alignment = RibbonAlignment::Center;
      path.ribbon.remove_clipped = true;
    }
    if (variant == 7) {
      path.ribbon.stretch = RibbonStretch::KeepAspect;
      path.ribbon.ratio = RibbonRatio::Length;
      path.ribbon.tiling = RibbonTiling::Custom;
      path.ribbon.count = 3;
    }
    if (variant == 8) {
      path.ribbon.offsets_only = true;
      path.ribbon.start_offset = .2;
      path.ribbon.end_offset = .1;
      path.closed = true;
      path.ribbon.omit_ends = false;
      path.ribbon.orientation = 3;
    }
    if (variant >= 9 && variant <= 11) {
      path.ribbon.image = translucent;
      path.ribbon.alpha = RibbonAlpha(variant - 9);
      path.points = {{{-.7, -.6, 0}, {0, 0, 1}, {.15, .2}, 1},
                     {{.7, .6, 0}, {0, 0, 1}, {.85, .8}, 1},
                     {{-.7, .6, 0}, {0, 0, 1}, {.15, .8}, 1},
                     {{.7, -.6, 0}, {0, 0, 1}, {.85, .2}, 1}};
    }
    if (variant >= 12) {
      path.points[1].smooth = true;
      path.brush.pressure_flow = true;
      path.brush.pressure_size = true;
      path.brush.minimum_flow = 0;
      path.brush.minimum_size = 10;
      mask = std::make_shared<QuickMaskField>();
      mask->resolution = variant == 15 ? 256 : 64;
      mask->inverted = variant == 14;
      PaintPixels::Stamp dab{{.5, .5}, {.2, 0}, {0, .2}};
      dab.channels = {false, false, true, false, false};
      dab.values.roughness = .15;
      {
        PaintRecordingScope recording;
        mask->pixels.stamp(mask->resolution, dab, {1001});
      }
      path.symmetry.enabled = variant == 15;
      path.symmetry.axis = 0;
      path.symmetry.offset = .1;
    }
    if (variant == 16) {
      path = original;
      path.ribbon.corner = RibbonCorner::Round;
      path.ribbon.image = translucent;
      path.ribbon.alpha = RibbonAlpha::Normal;
      path.closed = true;
      path.brush.diameter = 20;
      path.points = {{{-.7, -.7, 0}, {0, 0, 1}, {.15, .15}, 1},
                     {{.7, -.7, 0}, {0, 0, 1}, {.85, .15}, 1},
                     {{.7, .7, 0}, {0, 0, 1}, {.85, .85}, 1},
                     {{-.7, .7, 0}, {0, 0, 1}, {.15, .85}, 1}};
      mask.reset();
    }
    const auto reference = raster_ribbon_path(
        path, 2, size, sizes, {1001, 1002, 1011}, contact, mask);
    const auto before = PaintPixels::recipe_materializations();
    SurfacePathInk authored;
    {
      PaintRecordingScope recording;
      authored = raster_ribbon_path(path, 2, size, sizes, {1001, 1002, 1011}, contact, mask);
    }
    if (PaintPixels::recipe_materializations() != before)
      throw std::runtime_error("Ribbon recording materialized CPU pixels");
    for (int tile : {1001, 1002, 1011}) {
      for (int c = 0; c < 5; ++c)
        check(authored.primary.paint, reference.primary.paint, c, tile);
      check(authored.primary.ao, reference.primary.ao, 2, tile);
      check(authored.users[0], reference.users[0], 2, tile);
      check(authored.displacement, reference.displacement, 2, tile);
    }
    if (variant == 0 && pages.ribbon_image_allocations() != 1)
      throw std::runtime_error("Ribbon channel/tile consumers duplicated source image");
  }
  if (pages.ribbon_image_allocations() != 2)
    throw std::runtime_error("Ribbon image cache did not share immutable resources");
  std::printf(
      "BTS_GPU_RIBBON_VERIFIED cases=%d tiles=3 image_allocations=2 corners=4 overlap=3 mapping_pressure_symmetry=1 cpu_materializations=0 max_error=%.9g\n",
      cases,
      maximum_error);
  return cases;
}
}  // namespace bts
