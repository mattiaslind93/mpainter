#include "AmbientOcclusion.hh"
namespace bts {
BakedAoSample sample_baked_ao(const TextureSet &set, double u, double v, int tile)
{
  BakedAoSample result;
  if (!set.mesh_maps[3])
    return result;
  const auto found = set.mesh_maps[3]->tiles.find(tile);
  if (found == set.mesh_maps[3]->tiles.end() || !found->second.pixels)
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
  const auto interpolate = [&](int c) {
    return std::lerp(std::lerp(at(ix, iy, c), at(ix + 1, iy, c), float(x - ix)),
                     std::lerp(at(ix, iy + 1, c), at(ix + 1, iy + 1, c), float(x - ix)),
                     float(y - iy));
  };
  result.coverage = interpolate(3);
  if (result.coverage > 1e-8f)
    result.value = std::clamp(interpolate(0) / result.coverage, 0.f, 1.f);
  return result;
}
float mix_ambient_occlusion(const TextureSet &set,
                            float authored,
                            bool has_authored,
                            const BakedAoSample &baked)
{
  const float base = baked.coverage > 1e-8f ? baked.value : 1;
  authored = std::clamp(authored, 0.f, 1.f);
  if (!set.ao_channel || !has_authored)
    return base;
  return set.ao_mixing == AoMixing::Replace ? authored : authored * base;
}
}  // namespace bts
