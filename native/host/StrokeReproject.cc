#include "StrokeReproject.hh"
#include "TexturePadding.hh"
#include <algorithm>
#include <cmath>
namespace bts {
void reproject_spatial_strokes(TextureDocument &document,
                               const SceneSnapshot &previous,
                               const SceneSnapshot &replacement,
                               const std::map<std::uint32_t, std::uint32_t> &bindings,
                               bool preserve_world)
{
  for (const auto &[incoming, current] : bindings) {
    auto &set = document.set(current);
    bool needed = false;
    for (const auto &layer : set.layers) {
      needed |= spatial_history(layer.strokes, {layer.pixels, layer.ao_pixels});
      if (layer.mask)
        needed |= spatial_history(layer.mask->strokes, {layer.mask->pixels, {}});
      needed |= spatial_history(layer.displacement.strokes, {layer.displacement.pixels, {}});
      for (const auto &user : layer.users)
        needed |= spatial_history(user.strokes, {user.pixels, {}});
    }
    visit_effect_paint(set, [&](QUuid, QUuid, bool, const EffectPaint &paint) {
      needed |= spatial_history(paint.strokes, {paint.pixels, paint.ao});
      needed |= spatial_history(paint.displacement_strokes, {paint.displacement, {}});
      for (int slot = 0; slot < 8; ++slot)
        needed |= spatial_history(paint.user_strokes[slot], {paint.users[slot], {}});
    });
    if (!needed)
      continue;
    auto high = previous;
    const auto material = std::find_if(
        previous.materials.begin(), previous.materials.end(), [&](const auto &value) {
          return value.session_id == current;
        });
    if (material == previous.materials.end())
      throw std::runtime_error("Spatial source Texture Set is missing");
    const auto index = std::size_t(material - previous.materials.begin());
    std::erase_if(high.uv_triangles,
                  [&](const auto &triangle) { return triangle.material != index; });
    if (!preserve_world)
      for (auto &triangle : high.uv_triangles)
        for (auto &position : triangle.positions)
          for (int axis = 0; axis < 3; ++axis) {
            const double extent = previous.maximum[axis] - previous.minimum[axis];
            if (extent > 1e-9 && (previous.minimum[axis] != replacement.minimum[axis] ||
                                  previous.maximum[axis] != replacement.maximum[axis]))
              position[axis] = float(replacement.minimum[axis] +
                                     (position[axis] - previous.minimum[axis]) / extent *
                                         (replacement.maximum[axis] - replacement.minimum[axis]));
          }
    BakeRayOptions options;
    options.frontal = options.rear = 1;
    options.relative = true;
    options.average_normals = true;
    options.ignore_backface = false;
    std::map<int, UvTransferMap> maps;
    const auto mapping = [&](int resolution) -> const UvTransferMap & {
      if (!maps.contains(resolution)) {
        TexturePadding atlas(replacement, incoming);
        atlas.project_source(replacement,
                             high,
                             nullptr,
                             options,
                             resolution,
                             resolution,
                             set.tiles,
                             false,
                             &maps[resolution]);
      }
      return maps.at(resolution);
    };
    const auto move_controls = [&](std::shared_ptr<const StrokeHistory> &source) {
      if (preserve_world || !has_surface_paths(source))
        return;
      auto history = std::make_shared<StrokeHistory>(*source);
      for (auto &operation : history->operations)
        if (operation->path) {
          auto op = std::make_shared<StrokeOperation>(*operation);
          auto path = std::make_shared<SurfacePath>(*op->path);
          for (auto &point : path->points) {
            double norm = 0;
            for (int axis = 0; axis < 3; ++axis) {
              const double old_extent = previous.maximum[axis] - previous.minimum[axis],
                           new_extent = replacement.maximum[axis] - replacement.minimum[axis];
              if (old_extent > 1e-9 && new_extent > 1e-9) {
                point.position[axis] = replacement.minimum[axis] +
                                       (point.position[axis] - previous.minimum[axis]) /
                                           old_extent * new_extent;
                point.normal[axis] *= old_extent / new_extent;
                point.tangent_in[axis] *= new_extent / old_extent;
                point.tangent_out[axis] *= new_extent / old_extent;
              }
              norm += point.normal[axis] * point.normal[axis];
            }
            if (norm < 1e-20)
              throw std::runtime_error("Path normal collapsed during mesh replacement");
            for (double &value : point.normal)
              value /= std::sqrt(norm);
          }
          if (path->symmetry.enabled) {
            for (int axis = 0; axis < 3; ++axis) {
              const double old_extent = previous.maximum[axis] - previous.minimum[axis],
                           new_extent = replacement.maximum[axis] - replacement.minimum[axis];
              if (old_extent > 1e-9 && new_extent > 1e-9)
                path->symmetry.center[axis] = replacement.minimum[axis] +
                                              (path->symmetry.center[axis] -
                                               previous.minimum[axis]) /
                                                  old_extent * new_extent;
            }
            const int axis = path->symmetry.axis;
            const double old_extent = previous.maximum[axis] - previous.minimum[axis],
                         new_extent = replacement.maximum[axis] - replacement.minimum[axis];
            if (old_extent > 1e-9 && new_extent > 1e-9)
              path->symmetry.offset = replacement.minimum[axis] +
                                      (path->symmetry.offset - previous.minimum[axis]) /
                                          old_extent * new_extent;
          }
          path->validate();
          op->path = std::move(path);
          operation = std::move(op);
        }
      history->declared_paths.clear();
      source = std::move(history);
    };
    for (auto &layer : set.layers) {
      auto [pixels, history] = transfer_stroke_history(
          layer.strokes,
          {layer.pixels, layer.ao_pixels},
          mapping(TextureDocument::authoring_resolution(set, layer)),
          set.tiles);
      layer.pixels = std::move(pixels.paint);
      layer.ao_pixels = std::move(pixels.ao);
      layer.strokes = std::move(history);
      move_controls(layer.strokes);
      if (layer.mask) {
        auto [pixels, history] = transfer_stroke_history(
            layer.mask->strokes,
            {layer.mask->pixels, {}},
            mapping(TextureDocument::authoring_resolution(set, layer, true)),
            set.tiles);
        layer.mask->pixels = std::move(pixels.paint);
        layer.mask->strokes = std::move(history);
        move_controls(layer.mask->strokes);
      }
      for (int slot = 0; slot < 9; ++slot) {
        auto &user = slot == 8 ? layer.displacement : layer.users[slot];
        if (!spatial_history(user.strokes, {user.pixels, {}}))
          continue;
        auto [pixels, history] = transfer_stroke_history(
            user.strokes,
            {user.pixels, {}},
            mapping(custom_authoring_resolution(user, set.texture_resolution)),
            set.tiles);
        user.pixels = std::move(pixels.paint);
        user.strokes = std::move(history);
        move_controls(user.strokes);
      }
    }
    visit_effect_paint(set, [&](QUuid, QUuid, bool, EffectPaint &paint) {
      if (!paint.resolution)
        return;
      if (spatial_history(paint.strokes, {paint.pixels, paint.ao})) {
        auto [pixels, history] = transfer_stroke_history(
            paint.strokes, {paint.pixels, paint.ao}, mapping(paint.resolution), set.tiles);
        paint.pixels = std::move(pixels.paint);
        paint.ao = std::move(pixels.ao);
        paint.strokes = std::move(history);
        move_controls(paint.strokes);
      }
      for (int slot = 0; slot < 9; ++slot) {
        auto &scalar = slot == 8 ? paint.displacement : paint.users[slot];
        auto &history_slot = slot == 8 ? paint.displacement_strokes : paint.user_strokes[slot];
        if (!spatial_history(history_slot, {scalar, {}}))
          continue;
        auto [pixels, history] = transfer_stroke_history(
            history_slot, {scalar, {}}, mapping(paint.resolution), set.tiles);
        scalar = std::move(pixels.paint);
        history_slot = std::move(history);
        move_controls(history_slot);
      }
    });
  }
}
}  // namespace bts
