// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <string_view>

namespace bts {
struct BuiltinMaskName {
  std::string_view name, legacy_name, slug;
  int generator;
};
// Reference catalog names. Keep the mPainter filenames/UUID inputs stable:
// projects, libraries and saved shelf selections can already refer to them.
inline constexpr std::array builtin_mask_names{
    BuiltinMaskName{"Rust", "Oxide Flecks", "oxide-flecks", 9},
    BuiltinMaskName{"Edge Wear", "Exposed Edges", "exposed-edges", 8},
    BuiltinMaskName{"Crease Dirt", "Crevice Sediment", "crevice-sediment", 9},
    BuiltinMaskName{"Moss", "Organic Growth", "organic-growth", 9},
    BuiltinMaskName{"Ground Dirt", "Ground Contact", "ground-contact", 1},
    BuiltinMaskName{"Scratches", "Surface Scuffs", "surface-scuffs", 8},
    BuiltinMaskName{"Leaks & Stains", "Gravity Streaks", "gravity-streaks", 9},
    BuiltinMaskName{"Surface Dust", "Settled Dust", "settled-dust", 6},
    BuiltinMaskName{"Position height", "Height Fade", "height-fade", 1},
    BuiltinMaskName{"Curvature edges", "Curvature Ridge", "curvature-ridge", 2},
};
inline std::string_view restored_builtin_mask_name(std::string_view name, int generator)
{
  for (const auto &entry : builtin_mask_names)
    if (entry.legacy_name == name && entry.generator == generator)
      return entry.name;
  return name;
}
}  // namespace bts
