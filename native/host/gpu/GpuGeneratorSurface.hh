#pragma once
#include "GpuLayerParameters.hh"
#include "FillProjection.hh"
namespace bts {
// Generator stages borrow the existing spatial slots; they never share a draw
// with a Fill projection and do not enlarge the parameter texture schema.
inline void gpu_generator_surface_parameters(GpuLayerParameters &parameters,
                                             const FillSurfaceAtlas *atlas)
{
  if (!atlas || atlas->extent <= 1e-20)
    return;
  const int size[2] = {atlas->resolution, atlas->height ? atlas->height : atlas->resolution};
  parameters.set2("surface_size", size);
  parameters.set("surface_stride", sizeof(FillSurfacePoint) / 4);
  std::array<float, 3> center, scale;
  for (int c = 0; c < 3; ++c) {
    center[c] = atlas->center[c];
    scale[c] = 1 / atlas->extent;
  }
  parameters.set3("projection_offset", center.data());
  parameters.set3("inverse_denominator", scale.data());
}
}  // namespace bts
