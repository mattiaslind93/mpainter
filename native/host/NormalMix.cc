#include "NormalMix.hh"
#include <cmath>
namespace bts {
BakedNormalSample sample_baked_normal(const TextureSet &set, double u, double v, int tile)
{
  BakedNormalSample result;
  if (!set.mesh_maps[0])
    return result;
  const auto found = set.mesh_maps[0]->tiles.find(tile);
  if (found == set.mesh_maps[0]->tiles.end() || !found->second.pixels)
    return result;
  const auto &data = *found->second.pixels;
  const double x = u * data.resolution - .5, y = v * data.height - .5;
  const int ix = int(std::floor(x)), iy = int(std::floor(y));
  const auto at = [&](int x, int y, int c) {
    return data.rgba[(std::size_t(std::clamp(y, 0, data.height - 1)) * data.resolution +
                      std::clamp(x, 0, data.resolution - 1)) *
                         4 +
                     c];
  };
  std::array<float, 4> value;
  for (int c = 0; c < 4; ++c)
    value[c] = std::lerp(std::lerp(at(ix, iy, c), at(ix + 1, iy, c), float(x - ix)),
                         std::lerp(at(ix, iy + 1, c), at(ix + 1, iy + 1, c), float(x - ix)),
                         float(y - iy));
  result.coverage = value[3];
  if (result.coverage <= 1e-8f)
    return result;
  for (int c = 0; c < 3; ++c)
    result.direction[c] = value[c] / value[3] * 2 - 1;
  if (set.normal_format == NormalMapFormat::DirectX)
    result.direction[1] = -result.direction[1];
  result.direction = normalized_normal(result.direction);
  return result;
}
NormalVector mix_baked_normal(const TextureSet &set,
                              NormalVector authored,
                              bool has_authored,
                              const BakedNormalSample &baked)
{
  authored = normalized_normal(authored);
  if (baked.coverage <= 1e-8f)
    return authored;
  if (!set.channels[3] || !has_authored)
    return baked.direction;
  if (set.normal_mixing == NormalMixing::Replace)
    return authored;
  return reoriented_normal(baked.direction, authored);
}
}  // namespace bts
