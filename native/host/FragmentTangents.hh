#pragma once
#include "UvTransfer.hh"
namespace bts {
// The shader derives the same raw frame from screen derivatives of position/UV.
inline TangentFrame fragment_tangent_frame(const TangentFrame &raw, std::array<float, 3> normal)
{
  const auto dot = [](const auto &a, const auto &b) {
    float v = 0;
    for (int i = 0; i < 3; ++i)
      v += a[i] * b[i];
    return v;
  };
  const auto cross = [](const auto &a, const auto &b) {
    return std::array<float, 3>{
        a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  };
  float length = std::sqrt(dot(normal, normal));
  if (length < 1e-10f)
    return raw;
  for (auto &v : normal)
    v /= length;
  auto tangent = raw[0];
  const float along = dot(tangent, normal);
  for (int i = 0; i < 3; ++i)
    tangent[i] -= normal[i] * along;
  length = std::sqrt(dot(tangent, tangent));
  if (length < 1e-10f)
    return raw;
  for (auto &v : tangent)
    v /= length;
  auto bitangent = cross(normal, tangent);
  if (dot(bitangent, raw[1]) < 0)
    for (auto &v : bitangent)
      v = -v;
  return {tangent, bitangent, normal};
}
inline std::array<float, 3> fragment_world_to_tangent(const TangentFrame &frame,
                                                      const std::array<float, 3> &world)
{
  std::array<float, 3> result{};
  for (int c = 0; c < 3; ++c)
    for (int axis = 0; axis < 3; ++axis)
      result[c] += frame[c][axis] * world[axis];
  return result;
}
}  // namespace bts
