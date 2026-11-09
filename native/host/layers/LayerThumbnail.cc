// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayerThumbnail.hh"
#include "IMB_colormanagement.hh"
#include <algorithm>
#include <cmath>
namespace bts {
QImage thumbnail_image(const ThumbnailMetadata &data, const ThumbnailRequest &request)
{
  QImage image(48, 48, QImage::Format_RGB32);
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const auto &pixel = data.pixels[y * 48 + x];
      float rgb[3] = {pixel[0], pixel[1], pixel[2]};
      if (request.mask) {
        const int gray = qRound(255 * std::clamp(rgb[0], 0.f, 1.f));
        image.setPixel(x, y, qRgb(gray, gray, gray));
        continue;
      }
      if (request.channel == 0)
        blender::IMB_colormanagement_scene_linear_to_srgb_v3(rgb, rgb);
      // The GPU source pass already encodes tangent-space normals as RGB.
      else if (request.channel != 3)
        rgb[1] = rgb[2] = rgb[0];
      const float checker = ((x / 12 + y / 12) % 2) ? 1.f / 3 : .6f,
                  alpha = std::clamp(pixel[3], 0.f, 1.f);
      int color[3];
      for (int c = 0; c < 3; ++c)
        color[c] = qRound(255 * (std::clamp(rgb[c], 0.f, 1.f) * alpha + checker * (1 - alpha)));
      image.setPixel(x, y, qRgb(color[0], color[1], color[2]));
    }
  return image;
}

QImage mask_thumbnail(const LayerMask &mask,
                      const std::set<int> &tiles,
                      const MeshMap *id_map,
                      const MeshMaps *maps,
                      const TextureSet *context_set,
                      const QUuid &layer_id)
{
  QImage image(48, 48, QImage::Format_RGB32);
  const std::vector<int> numbers(tiles.begin(), tiles.end());
  const int columns = std::max(1, int(std::ceil(std::sqrt(numbers.size())))),
            resolution = std::max(48, mask.pixels.resolution());
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const int tile = (y * columns / 48) * columns + x * columns / 48;
      const int px = int(std::fmod((x + .5) * columns / 48, 1.) * resolution),
                py = int((1 - std::fmod((y + .5) * columns / 48, 1.)) * resolution);
      const int gray = qRound(
          255 *
          (tile < int(numbers.size()) ?
               (context_set && mask.channel_dependent() ?
                    TextureDocument::mask_value(
                        *context_set,
                        layer_id,
                        int(double(px) * context_set->width(numbers[tile]) / resolution),
                        int(double(py) * context_set->rows(numbers[tile]) / resolution),
                        numbers[tile]) :
                    mask.value(px, py, numbers[tile], resolution, resolution, id_map, maps)) :
               0));
      image.setPixel(x, y, qRgb(gray, gray, gray));
    }
  return image;
}
QImage layer_thumbnail(const TextureLayer &layer, int channel, const std::set<int> &tiles)
{
  // Display a small atlas of authored tiles. Color uses Blender's working-space to sRGB
  // conversion; scalar and normal channels are data and bypass the color transform.
  constexpr int size = 48;
  QImage image(size, size, QImage::Format_RGB32);
  const std::vector<int> numbers(tiles.begin(), tiles.end());
  const int columns = std::max(1, int(std::ceil(std::sqrt(numbers.size()))));
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      float alpha = 0, rgb[3]{};
      const int tile_index = (y * columns / size) * columns + x * columns / size;
      if ((channel == 14 ? layer.displacement.enabled :
           channel >= 6  ? layer.users[channel - 6].enabled :
           channel == 5  ? layer.ao_enabled :
                           layer.channels[channel]) &&
          tile_index < int(numbers.size()))
      {
        if (layer.kind == LayerKind::Fill) {
          alpha = 1;
          if (channel == 0) {
            std::copy_n(layer.values.base_color.begin(), 3, rgb);
            if (layer.fill.texture || layer.fill.atom) {
              const int px = int(std::fmod((x + .5) * columns / size, 1.) * size),
                        py = int((1 - std::fmod((y + .5) * columns / size, 1.)) * size);
              if (layer.fill.texture && layer.fill.texture->pixels) {
                const auto pixel = layer.fill.mode == 6 ?
                                       fill_tile_match_sample(
                                           layer.fill, px, py, size, size, numbers[tile_index]) :
                                       fill_texture_uv_sample(
                                           layer.fill, px, py, size, numbers[tile_index]);
                for (int c = 0; c < 3; ++c)
                  rgb[c] = pixel[c];
                alpha *= pixel[3];
              }
              else if (layer.fill.atom) {
                auto mapping = layer.fill;
                if (mapping.mode == 6) {
                  mapping.mode = 0;
                  mapping.scale = {1, 1};
                  mapping.offset = {};
                  mapping.rotation = 0;
                  mapping.physical = false;
                }
                const float pattern = fill_uv_sample(
                    mapping,
                    px,
                    py,
                    size,
                    mapping.mode == layer.fill.mode ? numbers[tile_index] : 1001);
                for (auto &v : rgb)
                  v *= pattern;
              }
            }
          }
          else if (channel == 3)
            std::copy_n(layer.values.normal.begin(), 3, rgb);
          else
            rgb[0] = channel == 14 ? layer.displacement.value :
                     channel >= 6  ? layer.users[channel - 6].value :
                     channel == 1  ? layer.values.metallic :
                     channel == 2  ? layer.values.roughness :
                     channel == 4  ? layer.values.bump :
                                     layer.values.ambient_occlusion;
          if (channel != 0) {
            const auto &texture = fill_channel_texture(layer.fill, channel);
            if (texture && texture->pixels) {
              auto mapping = layer.fill;
              mapping.texture = texture;
              mapping.atom = false;
              const int px = int(std::fmod((x + .5) * columns / size, 1.) * size),
                        py = int((1 - std::fmod((y + .5) * columns / size, 1.)) * size);
              const auto pixel =
                  mapping.mode == 6 ?
                      fill_tile_match_sample(mapping, px, py, size, size, numbers[tile_index]) :
                      fill_texture_uv_sample(mapping, px, py, size, numbers[tile_index]);
              if (channel == 3) {
                float length = 0;
                for (int c = 0; c < 3; ++c) {
                  rgb[c] = pixel[c] * 2 - 1;
                  length += rgb[c] * rgb[c];
                }
                if (length > 1e-20)
                  for (auto &value : rgb)
                    value /= std::sqrt(length);
                else {
                  rgb[0] = rgb[1] = 0;
                  rgb[2] = 1;
                }
              }
              else
                rgb[0] = pixel[0];
              alpha *= pixel[3];
            }
          }
        }
        else {
          const auto &pixels = channel == 14 ? layer.displacement.pixels :
                               channel >= 6  ? layer.users[channel - 6].pixels :
                               channel == 5  ? layer.ao_pixels :
                                               layer.pixels;
          const int resolution = pixels.resolution();
          const int px = int(std::fmod((x + .5) * columns / size, 1.0) * resolution);
          const int py = int((1 - std::fmod((y + .5) * columns / size, 1.0)) * resolution);
          const auto pixel = pixels.sample(
              channel >= 5 ? 2 : channel, px, py, numbers[tile_index]);
          alpha = pixel[3];
          if (alpha > 0)
            for (int c = 0; c < 3; ++c)
              rgb[c] = pixel[c] / alpha;
        }
        const int px = int(std::fmod((x + .5) * columns / size, 1.) * size),
                  py = int((1 - std::fmod((y + .5) * columns / size, 1.)) * size);
        std::array<float, 3> working{rgb[0], rgb[1], rgb[2]};
        if (channel == 3)
          for (float &v : working)
            v = v * .5f + .5f;
        else if (channel != 0)
          working.fill(rgb[0]);
        working = apply_content_effects(layer.effects,
                                        channel,
                                        working,
                                        alpha,
                                        px,
                                        py,
                                        numbers[tile_index],
                                        size,
                                        size,
                                        ChannelPrecision::RGB32F);
        for (int c = 0; c < 3; ++c)
          rgb[c] = channel == 3 ? working[c] * 2 - 1 : working[c];
        if (channel == 0)
          blender::IMB_colormanagement_scene_linear_to_srgb_v3(rgb, rgb);
        else if (channel == 3)
          for (float &v : rgb)
            v = v * .5f + .5f;
        else
          rgb[1] = rgb[2] = rgb[0];
      }
      const float checker = ((x / 12 + y / 12) % 2) ? 1.f / 3 : .6f;
      int color[3];
      for (int c = 0; c < 3; ++c)
        color[c] = qRound(255 * (std::clamp(rgb[c], 0.f, 1.f) * alpha + checker * (1 - alpha)));
      image.setPixel(x, y, qRgb(color[0], color[1], color[2]));
    }
  return image;
}
}  // namespace bts
