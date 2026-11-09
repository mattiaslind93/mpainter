// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialProjection.hh"
#include "GpuSpatialParameters.hh"

namespace bts {
inline std::shared_ptr<const NativeMaterialProjection> native_material_projection(
    const FillProjection &fill, const FillSurfaceAtlas *atlas, const TextureSet &set)
{
  validate_fill_projection(fill);
  if (fill_surface_projection(fill) &&
      (!atlas || !std::isfinite(atlas->extent) || atlas->extent <= 0))
    throw std::invalid_argument("Native spatial Fill requires a prepared surface atlas");
  auto result = std::make_shared<NativeMaterialProjection>();
  result->mode = NativeMaterialProjectionMode(fill.mode);
  auto &parameters = result->parameters;
  parameters.set("projection_mode", fill.mode);
  parameters.set("projection_wrap", fill.wrap);
  parameters.set("source_filter", fill.mode == 6 ? 1 : fill.filtering);
  auto mapping = fill;
  mapping.native_per_uv = set.native_per_uv;
  mapping.cm_per_native = set.cm_per_native;
  if (fill.mode == 6) {
    mapping.scale = {1, 1};
    mapping.offset = {0, 0};
    mapping.rotation = 0;
    mapping.physical = false;
  }
  else if (fill_surface_projection(fill))
    mapping.physical = false;
  const auto a = fill_uv_map(mapping, 0, 0), b = fill_uv_map(mapping, 1, 0),
             c = fill_uv_map(mapping, 0, 1);
  const std::array<float, 3> u{float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
  const std::array<float, 3> v{float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
  parameters.set3("map_u", u.data());
  parameters.set3("map_v", v.data());
  result->identity_uv = fill.mode == 0 && fill.wrap == 0 && u == std::array<float, 3>{1, 0, 0} &&
                        v == std::array<float, 3>{0, 1, 0};
  if (fill.mode == 6) {
    result->validate();
    return result;
  }
  // Existing packed spatial/physical/symmetry/Warp rules are the single source
  // of truth. Only the small immutable projector data is copied, not geometry.
  auto spatial = fill;
  spatial.native_per_uv = set.native_per_uv;
  spatial.cm_per_native = set.cm_per_native;
  gpu_spatial_parameters(parameters, spatial, atlas, set);
  result->validate();
  return result;
}
}  // namespace bts
