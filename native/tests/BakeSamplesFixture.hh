#pragma once
#include "DisplacementBake.hh"
#include <array>
#include <vector>
namespace bts::bake_samples_fixture {
using V = std::array<float, 3>;
using Triangle = std::array<V, 3>;
inline std::vector<Triangle> triangles()
{
  // Asymmetric finite blockers on BOTH sides of the sampled surface. Rays miss
  // their edges as well as hit them, so an empty plane or a256 clamp cannot pass.
  return {{{V{-.2f, -.4f, .3f}, V{.45f, -.4f, .3f}, V{.45f, .25f, .3f}}},
          {{V{-.2f, -.4f, .3f}, V{.45f, .25f, .3f}, V{-.2f, .25f, .3f}}},
          {{V{-.35f, -.18f, -.07f}, V{.13f, .4f, -.07f}, V{.13f, -.18f, -.07f}}},
          {{V{-.35f, -.18f, -.07f}, V{-.35f, .4f, -.07f}, V{.13f, .4f, -.07f}}}};
}
inline constexpr std::array<V, 4> positions{
    {{-.13f, -.11f, 0}, {.11f, -.08f, 0}, {-.08f, .12f, 0}, {.17f, .16f, 0}}};
inline float maximum(bool inward)
{
  return inward ? .2f : 2.f;
}
inline std::vector<float> reference(int rays, bool inward)
{
  const auto geometry = triangles();
  std::vector<float> result;
  for (std::size_t i = 0; i < positions.size(); ++i)
    result.push_back(bake_cosine_occlusion(geometry,
                                           positions[i],
                                           {0, 0, inward ? -1.f : 1.f},
                                           rays,
                                           maximum(inward),
                                           std::uint32_t(i)));
  return result;
}
}  // namespace bts::bake_samples_fixture
