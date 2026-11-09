#pragma once
#include <string_view>
#include "SceneSnapshot.hh"
#include <set>
namespace bts {
// external vendor's keyword rule uses the first marker, so Leg_low_top and Leg_high share Leg.
inline std::string_view bake_match_key(std::string_view name, std::string_view marker)
{
  if (marker.empty())
    return name;
  const auto found = name.find(marker);
  return found == std::string_view::npos ? name : name.substr(0, found);
}
inline bool bake_names_match(std::string_view low,
                             std::string_view high,
                             std::string_view low_marker = "_low",
                             std::string_view high_marker = "_high")
{
  return bake_match_key(low, low_marker) == bake_match_key(high, high_marker);
}
inline bool bake_ignores_backfaces(std::string_view name, std::string_view marker)
{
  return !marker.empty() && name.find(marker) != std::string_view::npos;
}
inline bool bake_filters_backface(const SceneSnapshot &scene,
                                  std::uint32_t object,
                                  bool enabled,
                                  std::string_view marker)
{
  if (!enabled)
    return false;
  if (marker.empty())
    return true;
  const auto name = scene.object_names.find(object);
  return name != scene.object_names.end() && bake_ignores_backfaces(name->second, marker);
}
inline std::set<std::uint32_t> bake_backface_objects(const SceneSnapshot &scene,
                                                     bool enabled,
                                                     std::string_view marker)
{
  std::set<std::uint32_t> result;
  if (enabled)
    for (const auto &tri : scene.uv_triangles) {
      const auto object = std::uint32_t(tri.vertex_ids[0] >> 32);
      if (bake_filters_backface(scene, object, enabled, marker))
        result.insert(object);
    }
  return result;
}
}  // namespace bts
