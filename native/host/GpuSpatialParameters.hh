#pragma once
#include "GpuLayerParameters.hh"
#include "TextureDocument.hh"
#include "WarpProjection.hh"
namespace bts {
inline void gpu_spatial_parameters(GpuLayerParameters &parameters,
                                   const FillProjection &fill,
                                   const FillSurfaceAtlas *atlas,
                                   const TextureSet &set)
{
  parameters.set("projection_symmetry", fill.symmetry && fill.symmetry->constraint.enabled);
  if (fill.symmetry) {
    const auto &sym = *fill.symmetry;
    const float options[4] = {float(sym.constraint.axis),
                              float(sym.constraint.mode),
                              float(sym.constraint.copies),
                              float(sym.constraint.offset)};
    const int flip[2] = {int(sym.flip_u), int(sym.flip_v)};
    const float center[3] = {float(sym.constraint.center[0]),
                             float(sym.constraint.center[1]),
                             float(sym.constraint.center[2])};
    parameters.set4("symmetry_options", options);
    parameters.set2("symmetry_flip", flip);
    parameters.set3("symmetry_center", center);
  }
  if (fill.mode == 5) {
    const auto grid = fill_warp_grid(fill);
    const float info[4] = {float(grid->width()),
                           float(grid->height()),
                           float(fill.warp_depth),
                           float(fill.warp_geometry && (fill.warp_geometry->enabled ||
                                                        fill.warp_geometry->frozen_reference))};
    parameters.set4("warp_info", info);
    parameters.set("warp_identity", grid->undeformed());
    auto *rows = parameters.values.data() + GpuLayerParameters::warp_data_start;
    for (int i = 0; i < grid->width(); ++i)
      rows[i][0] = grid->columns[i];
    for (int i = 0; i < grid->height(); ++i)
      rows[9 + i][0] = grid->rows[i];
    for (std::size_t i = 0; i < grid->nodes.size(); ++i) {
      const auto &node = grid->nodes[i];
      const std::array<WarpVector, 4> data{node.position, node.du, node.dv, node.duv};
      for (int j = 0; j < 4; ++j)
        for (int c = 0; c < 3; ++c)
          rows[18 + i * 4 + j][c] = data[j][c];
    }
  }
  if (atlas) {
    static_assert(offsetof(FillSurfacePoint, normal) == 12 &&
                  offsetof(FillSurfacePoint, footprint) == 24 &&
                  offsetof(FillSurfacePoint, valid) == 28 && sizeof(FillSurfacePoint) % 4 == 0);
    const int dimensions[2] = {atlas->resolution,
                               atlas->height ? atlas->height : atlas->resolution};
    parameters.set2("surface_size", dimensions);
    parameters.set("surface_stride", sizeof(FillSurfacePoint) / 4);
    if (fill.mode == 1) {
      const auto frame = fill_bitmap_triplanar_frame(fill, atlas->extent, set.cm_per_native);
      for (int row = 0; row < 3; ++row)
        parameters.set3(row == 0 ? "rotation_x" :
                        row == 1 ? "rotation_y" :
                                   "rotation_z",
                        frame.rotation[row].data());
      parameters.set3("projection_offset", frame.offset.data());
      parameters.set3("projection_scale", frame.scale.data());
      parameters.set3("inverse_denominator", frame.inverse_denominator.data());
      parameters.set3("map_u", frame.map_u.data());
      parameters.set3("map_v", frame.map_v.data());
      parameters.set("projection_hardness", frame.hardness);
      return;
    }
    std::array<std::array<float, 3>, 3> rotation{};
    for (int column = 0; column < 3; ++column) {
      std::array<double, 3> v{};
      v[column] = 1;
      v = fill_inverse_rotation(v, fill.rotation3);
      for (int row = 0; row < 3; ++row)
        rotation[row][column] = v[row];
    }
    for (int row = 0; row < 3; ++row)
      parameters.set3(row == 0 ? "rotation_x" :
                      row == 1 ? "rotation_y" :
                                 "rotation_z",
                      rotation[row].data());
    std::array<float, 3> offset, scale, denominator;
    const auto size = fill_physical_size(fill);
    for (int k = 0; k < 3; ++k) {
      const bool spatial = fill_spatial_projection(fill);
      const int axis = spatial ? (k == 1 ? 2 : k == 2 ? 1 : 0) : k;
      offset[k] = spatial ? atlas->center[k] + fill.offset3[k] * atlas->extent : fill.offset3[k];
      scale[k] = fill.scale3[k];
      denominator[k] = 1 / (fill.scale3[k] *
                            (fill.physical ? size[axis] / set.cm_per_native : atlas->extent));
    }
    parameters.set3("projection_offset", offset.data());
    parameters.set3("projection_scale", scale.data());
    parameters.set3("inverse_denominator", denominator.data());
    parameters.set("projection_hardness", 1 + fill.hardness * 15);
    const float flags[3] = {
        float(fill.shape_crop), float(fill.depth_culling), float(fill.backface_culling)};
    const double threshold = std::cos(fill.backface_angle * std::numbers::pi / 180);
    const float backface[3] = {
        float(threshold),
        float(std::max(1e-8, (1 - threshold) * (1 - fill.backface_hardness))),
        float(fill.backface_angle)};
    parameters.set3("projection_flags", flags);
    parameters.set3("backface_options", backface);
    parameters.set("depth_hardness", fill.depth_hardness);
    parameters.set("cylinder_radians", fill.cylinder_angle * std::numbers::pi / 180);
  }
}
}  // namespace bts
