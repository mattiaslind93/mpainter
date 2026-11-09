// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshBaker.hh"
#include "TextureDocument.hh"
#include <QJsonObject>
namespace bts {
inline void read_surface_bake_settings(MeshBakeSettings &o, const QJsonObject &r)
{
  o.curvature_radius = r["curvature_radius"].toDouble(0);
  o.curvature_scale = r["curvature_scale"].toDouble(1);
  o.ao_distance = r["ao_distance"].toDouble(0);
  o.thickness_distance = r["thickness_distance"].toDouble(0);
  o.ray_offset = r["ray_offset"].toDouble(0);
  o.normal_detail = 0;
  o.denoise = r["denoise"].toBool(true);
}
// Snapshot active authored channels on the owner thread. Include occluder sets,
// not only the selected output set. Disabled/unloaded tiles remain excluded.
inline void capture_bake_channels(MeshBakeSettings &options,
                                  const TextureDocument &document,
                                  const SceneSnapshot &snapshot)
{
  options.displacement.clear();
  options.normal_maps.clear();
  if (!options.use_project_mesh)
    return;
  for (const auto &set : document.texture_sets()) {
    if (set.disabled)
      continue;
    if (set.displacement.channel && set.displacement.enabled && set.displacement.allocated &&
        set.displacement.strength != 0)
    {
      BakeDisplacement field;
      field.subdivision = snapshot.evaluated_subdivision ?
                              0 :
                          snapshot.preview_subdivision_levels.contains(set.material_session_uid) ?
                              snapshot.preview_subdivision_levels.at(set.material_session_uid) :
                              set.displacement.subdivision;
      field.midlevel = set.displacement.midlevel;
      field.strength = set.displacement.strength;
      // Preserve channel resolution; the baker samples this field bilinearly.
      for (int tile : set.loaded_tiles()) {
        field.width = std::max(field.width, set.width(tile));
        field.rows = std::max(field.rows, set.rows(tile));
      }
      for (int tile : set.loaded_tiles()) {
        auto &samples = field.samples[tile];
        samples.resize(std::size_t(field.width) * field.rows);
        const int sw = set.width(tile), sh = set.rows(tile);
        for (int y = 0; y < field.rows; ++y)
          for (int x = 0; x < field.width; ++x)
            samples[std::size_t(y) * field.width + x] = TextureDocument::displacement_value(
                set,
                std::min(sw - 1, int((x + .5) / field.width * sw)),
                std::min(sh - 1, int((y + .5) / field.rows * sh)),
                tile);
      }
      options.displacement[set.material_session_uid] = std::move(field);
    }
  }
}
}  // namespace bts
