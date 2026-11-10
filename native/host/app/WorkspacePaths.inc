// SPDX-License-Identifier: GPL-3.0-or-later
// Surface paths: path history, selection, contacts and commits. Part of run_workspace_session()
// (app/WorkspaceSession.inc), included in order inside its body; it shares that function's locals
// and is not a standalone file.
const auto path_history =
    [&](bts::TextureSet &set) -> std::shared_ptr<const bts::StrokeHistory> & {
  if (auto *effect = bts::TextureDocument::paint_effect(set))
    return effect->strokes;
  auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
  if (set.selected_mask) {
    if (!layer.mask)
      throw std::invalid_argument("No writable mask selected");
    return layer.mask->strokes;
  }
  return layer.strokes;
};
const auto selected_path_definition = [&]() -> std::shared_ptr<const bts::SurfacePath> {
  if (!document || selected_material < 0 || viewport->selected_path.empty())
    return {};
  auto &set = document->set(snapshot.materials[selected_material].session_id);
  const auto &history = path_history(set);
  if (history && history->loaded)
    for (const auto &op : history->operations)
      if (op->path && op->path->id == viewport->selected_path)
        return op->path;
  return {};
};
const auto path_contact =
    [&](std::uint32_t uid,
        const bts::SurfacePathPoint &point,
        float diameter,
        bool coverage,
        const bts::BrushProjection &projection) -> std::optional<bts::SurfaceBrushSample> {
  const double distance = std::max({double(snapshot.maximum[0] - snapshot.minimum[0]),
                                    double(snapshot.maximum[1] - snapshot.minimum[1]),
                                    double(snapshot.maximum[2] - snapshot.minimum[2]),
                                    1e-6}) *
                          2;
  std::array<float, 3> origin, direction;
  for (int c = 0; c < 3; ++c) {
    origin[c] = float(point.position[c] + point.normal[c] * distance);
    direction[c] = float(-point.normal[c]);
  }
  return host.surface_stamp(uid, origin, direction, diameter, coverage, true, projection);
};
const auto apply_surface_path = [&](bts::TextureSet &set,
                                    const std::string &id,
                                    std::shared_ptr<const bts::SurfacePath> path,
                                    bool create,
                                    bool render) {
  if (path && path->bump_origin != set.bump_origin)
    path = std::make_shared<bts::SurfacePath>(bts::surface_path_in_origin(*path, set.bump_origin));
  bts::PaintMetadataScope paint_metadata;
  bts::PaintRecordingScope paint_record(gpu_authoring_enabled);
  bts::performance::Scope path_timing("surface_path_authoring");
  auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
  auto *effect = bts::TextureDocument::paint_effect(set);
  const bool mask = set.selected_mask;
  const int resolution = effect ?
                             (effect->resolution ? effect->resolution : set.texture_resolution) :
                             bts::TextureDocument::authoring_resolution(set, layer, mask);
  if (effect)
    effect->resolution = resolution;
  else if (mask)
    layer.mask->author_resolution = resolution;
  else
    layer.author_resolution = resolution;
  auto &primary = path_history(set);
  bts::StrokeRaster before = effect ? bts::StrokeRaster{effect->pixels, effect->ao} :
                             mask   ? bts::StrokeRaster{layer.mask->pixels, {}} :
                                      bts::StrokeRaster{layer.pixels, layer.ao_pixels};
  std::array<int, 8> user_sizes;
  for (int slot = 0; slot < 8; ++slot)
    user_sizes[slot] = effect ? resolution :
                                bts::custom_authoring_resolution(layer.users[slot],
                                                                 set.texture_resolution);
  bts::SurfacePathInk ink;
  if (path && render) {
    auto effective = *path;
    if (mask) {
      effective.brush.channels = {false, false, true, false, false};
      effective.brush.values.roughness = path->brush.mask_value;
      effective.brush.ao = false;
      effective.brush.users.fill(false);
      effective.brush.displacement = false;
    }
    const auto contact = [&](const bts::SurfacePathPoint &point,
                             float diameter,
                             bool coverage,
                             const bts::BrushProjection &projection) {
      return path_contact(set.material_session_uid, point, diameter, coverage, projection);
    };
    const auto first = contact(path->points.front(), 100, false, path->brush.projection);
    if (!first)
      throw std::invalid_argument("Surface path is outside the selected mesh");
    ink = effective.filled ?
              bts::raster_filled_path(effective,
                                      first->radius * 2,
                                      resolution,
                                      user_sizes,
                                      set.tiles,
                                      contact,
                                      snapshot,
                                      std::size_t(selected_material),
                                      quick_enabled ? viewport->quick_mask_gate : nullptr,
                                      effect ? resolution :
                                               bts::custom_authoring_resolution(
                                                   layer.displacement, set.texture_resolution)) :
          effective.ribbon.enabled ?
              bts::raster_ribbon_path(effective,
                                      first->radius * 2,
                                      resolution,
                                      user_sizes,
                                      set.tiles,
                                      contact,
                                      quick_enabled ? viewport->quick_mask_gate : nullptr,
                                      effect ? resolution :
                                               bts::custom_authoring_resolution(
                                                   layer.displacement, set.texture_resolution)) :
              bts::raster_surface_path(effective,
                                       first->radius * 2,
                                       resolution,
                                       user_sizes,
                                       set.tiles,
                                       contact,
                                       quick_enabled ? viewport->quick_mask_gate : nullptr,
                                       effect ? resolution :
                                                bts::custom_authoring_resolution(
                                                    layer.displacement, set.texture_resolution));
  }
  const auto source_ink = [&](const std::shared_ptr<const bts::StrokeHistory> &history)
      -> std::shared_ptr<const bts::StrokeRaster> {
    if (history)
      for (const auto &op : history->operations)
        if (op->path && op->path->id == id)
          return op->ink;
    return {};
  };
  if (create)
    primary = bts::append_surface_path(primary, before, ink.primary, path, resolution);
  else
    primary = bts::edit_surface_path(primary,
                                     id,
                                     path,
                                     render ? std::make_shared<bts::StrokeRaster>(ink.primary) :
                                              source_ink(primary),
                                     resolution);
  const auto after = bts::replay_strokes(*primary, resolution);
  if (effect) {
    effect->pixels = after.paint;
    effect->ao = after.ao;
  }
  else if (mask)
    layer.mask->pixels = after.paint;
  else {
    layer.pixels = after.paint;
    layer.ao_pixels = after.ao;
  }
  if (!mask)
    for (int slot = 0; slot < 8; ++slot) {
      auto &pixels = effect ? effect->users[slot] : layer.users[slot].pixels;
      auto &history = effect ? effect->user_strokes[slot] : layer.users[slot].strokes;
      const auto old = source_ink(history);
      if (create) {
        history = bts::append_surface_path(
            history, {pixels, {}}, {ink.users[slot], {}}, path, user_sizes[slot]);
        if (!effect)
          layer.users[slot].author_resolution = user_sizes[slot];
      }
      else if (old)
        history = bts::edit_surface_path(
            history,
            id,
            path,
            render ? std::make_shared<bts::StrokeRaster>(bts::StrokeRaster{ink.users[slot], {}}) :
                     old,
            user_sizes[slot]);
      else if (path && path->brush.users[slot]) {
        history = bts::append_surface_path(
            history, {pixels, {}}, {ink.users[slot], {}}, path, user_sizes[slot]);
        if (!effect)
          layer.users[slot].author_resolution = user_sizes[slot];
      }
      if (history)
        pixels = bts::replay_strokes(*history, user_sizes[slot]).paint;
    }
  if (!mask) {
    auto &user = layer.displacement;
    auto &pixels = effect ? effect->displacement : user.pixels;
    auto &history = effect ? effect->displacement_strokes : user.strokes;
    const int size = effect ? resolution :
                              bts::custom_authoring_resolution(user, set.texture_resolution);
    const auto old = source_ink(history);
    if (create) {
      history = bts::append_surface_path(
          history, {pixels, {}}, {ink.displacement, {}}, path, size);
      if (!effect)
        user.author_resolution = size;
    }
    else if (old)
      history = bts::edit_surface_path(
          history,
          id,
          path,
          render ? std::make_shared<bts::StrokeRaster>(bts::StrokeRaster{ink.displacement, {}}) :
                   old,
          size);
    else if (path && path->brush.displacement) {
      history = bts::append_surface_path(
          history, {pixels, {}}, {ink.displacement, {}}, path, size);
      if (!effect)
        user.author_resolution = size;
    }
    if (history)
      pixels = bts::replay_strokes(*history, size).paint;
    if (!path && history && history->operations.empty())
      history.reset();
    if (!effect && !history && pixels.empty())
      user.author_resolution = 0;
  }
  if (!path) {
    if (primary && primary->operations.empty())
      primary.reset();
    if (!mask)
      for (int slot = 0; slot < 8; ++slot) {
        auto &history = effect ? effect->user_strokes[slot] : layer.users[slot].strokes;
        const auto &pixels = effect ? effect->users[slot] : layer.users[slot].pixels;
        if (history && history->operations.empty())
          history.reset();
        if (!effect && !history && pixels.empty())
          layer.users[slot].author_resolution = 0;
      }
  }
};
const auto commit_surface_path = [&](const std::string &id,
                                     std::shared_ptr<const bts::SurfacePath> path,
                                     bool create,
                                     bool render,
                                     const QString &label) {
  const auto *before = document.get();
  const bool guard = path_guard;
  path_guard = true;
  edit_material([&](bts::TextureSet &set) { apply_surface_path(set, id, path, create, render); });
  path_guard = guard;
  const bool changed = document.get() != before;
  if (changed && !undo_history.empty()) {
    undo_history.back().label = label;
    refresh_history_actions();
  }
  return changed;
};
if (workspace) {
  viewport->paths_panel = new bts::PathsPanel(viewport);
  viewport->path_curve_panel = new bts::PathCurvePanel(viewport);
  viewport->path_curve_panel->display_changed = [&](const bts::PathDisplayOptions &options) {
    viewport->path_display = options;
    viewport->update();
  };
  refresh_paths = [&] {
    viewport->paths.clear();
    std::uint32_t uid = 0;
    if (document && selected_material >= 0 && viewport->paint_enabled) {
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      uid = set.material_session_uid;
      const auto &history = path_history(set);
      if (history && history->loaded)
        for (const auto &op : history->operations)
          if (op->path)
            viewport->paths.push_back(op->path);
    }
    std::set<std::string> retained;
    std::shared_ptr<const bts::SurfacePath> selected;
    for (const auto &path : viewport->paths) {
      retained.insert(path->id);
      if (path->id == viewport->selected_path)
        selected = path;
      if (!viewport->path_active)
        continue;
      auto &cache = viewport->path_draw[path->id];
      if (cache.definition == path)
        continue;
      cache.definition = path;
      cache.samples.clear();
      try {
        const auto first = path_contact(
            uid, path->points.front(), 100, false, path->brush.projection);
        if (first)
          cache.samples = bts::dense_surface_path(
              *path,
              first->radius * 2,
              [&](const bts::SurfacePathPoint &point) -> std::optional<bts::SurfacePathPoint> {
                const auto hit = path_contact(uid, point, 100, false, path->brush.projection);
                if (!hit)
                  return {};
                auto result = point;
                for (int c = 0; c < 3; ++c)
                  result.position[c] = hit->position[c];
                result.uv = hit->footprint.center;
                return result;
              });
      }
      catch (const std::exception &) {
        cache.samples.clear();
      }
      if (cache.samples.empty()) {
        for (std::size_t i = 0; i < path->points.size(); ++i)
          cache.samples.push_back({path->points[i], i ? i - 1 : 0, i ? 1. : 0.});
        if (path->closed)
          cache.samples.push_back({path->points.front(), path->points.size() - 1, 1});
      }
    }
    std::erase_if(viewport->path_draw,
                  [&](const auto &item) { return !retained.contains(item.first); });
    if (!selected) {
      viewport->selected_path.clear();
      viewport->selected_vertex = -1;
    }
    else if (viewport->selected_vertex >= int(selected->points.size()))
      viewport->selected_vertex = -1;
    viewport->paths_panel->synchronize(viewport->paths, viewport->selected_path);
    viewport->path_curve_panel->synchronize(selected.get(), viewport->selected_vertex);
    viewport->position_paths_panel();
    viewport->update();
  };
  restore_path_profile = [&] {
    if (!viewport->path_active)
      return;
    if (const auto path = selected_path_definition()) {
      brush = path->brush;
      brush.values.bump += .5f - path->bump_origin;
      ribbon_profile = path->ribbon;
      filled_profile = path->filled;
      symmetry = path->symmetry;
      host.set_symmetry(symmetry);
      workspace->set_symmetry(symmetry);
    }
  };
  leave_path = [&] {
    if (!viewport->path_active)
      return;
    viewport->finish_path_drag(false);
    viewport->path_active = viewport->path_editing = false;
    viewport->pending_path_point.reset();
    viewport->selected_path.clear();
    brush = path_return_brush;
    symmetry = path_return_symmetry;
    host.set_symmetry(symmetry);
    workspace->set_symmetry(symmetry);
    workspace->set_path_active(false);
    viewport->position_paths_panel();
    viewport->setFocus();
  };
  path_brush_changed = [&] {
    if (const auto source = selected_path_definition()) {
      auto path = std::make_shared<bts::SurfacePath>(*source);
      path->brush = brush;
      path->brush.values.bump += path->bump_origin - .5f;
      path->symmetry = symmetry;
      try {
        if (bts::surface_path_json(*path) != bts::surface_path_json(*source) &&
            !commit_surface_path(path->id, path, false, true, "Edit surface path"))
        {
          brush = source->brush;
          brush.values.bump += .5f - source->bump_origin;
          symmetry = source->symmetry;
          host.set_symmetry(symmetry);
          workspace->set_symmetry(symmetry);
        }
      }
      catch (const std::exception &error) {
        brush = source->brush;
        brush.values.bump += .5f - source->bump_origin;
        symmetry = source->symmetry;
        workspace->append_console(QString::fromUtf8(error.what()));
      }
    }
  };
  viewport->path_selected = [&](const std::string &id) {
    viewport->selected_path = id;
    viewport->selected_vertex = -1;
    viewport->pending_path_point.reset();
    viewport->path_editing = true;
    restore_path_profile();
    select_material(selected_material);
    viewport->update();
  };
  viewport->path_finished = [&] {
    viewport->path_editing = false;
    viewport->selected_vertex = -1;
    viewport->pending_path_point.reset();
    if (refresh_paths)
      refresh_paths();
    viewport->setFocus();
    viewport->update();
  };
  viewport->path_point_added = [&](const bts::SurfacePathPoint &point) {
    if (!document || selected_material < 0)
      return;
    auto source = selected_path_definition();
    if (viewport->path_editing && source) {
      auto path = std::make_shared<bts::SurfacePath>(*source);
      path->points.push_back(point);
      if (path->filled && path->points.size() == 3)
        path->closed = true;
      if (commit_surface_path(path->id, path, false, true, "Edit surface path")) {
        viewport->selected_vertex = int(path->points.size()) - 1;
        if (refresh_paths)
          refresh_paths();
      }
    }
    else if (viewport->pending_path_point) {
      auto path = std::make_shared<bts::SurfacePath>();
      path->id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      path->name = "Path " + std::to_string(viewport->paths.size() + 1);
      path->seed = ++viewport->jitter_stroke_serial_;
      path->brush = brush;
      path->ribbon = ribbon_profile;
      path->filled = filled_profile;
      if (path->filled)
        path->name = "Filled " + std::to_string(viewport->paths.size() + 1);
      if (path->ribbon.enabled)
        path->name = "Ribbon " + std::to_string(viewport->paths.size() + 1);
      path->symmetry = symmetry;
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      for (int c = 0; c < 5; ++c)
        path->brush.channels[c] &= set.channels[c];
      path->brush.ao &= set.ao_channel && layer.ao_enabled;
      path->brush.displacement &= set.displacement.channel &&
                                  (bts::TextureDocument::paint_effect(set) ||
                                   layer.displacement.enabled);
      for (int slot = 0; slot < 8; ++slot)
        path->brush.users[slot] &= set.users[slot].present &&
                                   (bts::TextureDocument::paint_effect(set) ||
                                    layer.users[slot].enabled);
      path->points = {*viewport->pending_path_point, point};
      if (commit_surface_path(path->id, path, true, true, "Create surface path")) {
        viewport->selected_path = path->id;
        viewport->selected_vertex = int(path->points.size()) - 1;
        viewport->pending_path_point.reset();
        viewport->path_editing = true;
        restore_path_profile();
        select_material(selected_material);
      }
    }
    else {
      viewport->selected_path.clear();
      viewport->selected_vertex = -1;
      viewport->pending_path_point = point;
      viewport->path_editing = true;
      refresh_paths();
    }
    viewport->update();
  };
  viewport->path_removed = [&](const std::string &id) {
    if (commit_surface_path(id, {}, false, false, "Delete surface path")) {
      viewport->selected_path.clear();
      viewport->pending_path_point.reset();
      viewport->path_editing = false;
    }
    select_material(selected_material);
  };
  viewport->path_vertex_selected = [&] { refresh_paths(); };
  viewport->path_curve_action = [&](bts::PathCurveAction action, double value) {
    const auto source = selected_path_definition();
    if (!source)
      return;
    auto path = std::make_shared<bts::SurfacePath>(*source);
    const int index = viewport->selected_vertex;
    const bool vertex = index >= 0 && index < int(path->points.size());
    switch (action) {
      case bts::PathCurveAction::Pressure:
        if (!vertex)
          return;
        path->points[index].pressure = float(value / 100);
        break;
      case bts::PathCurveAction::Smooth:
        if (!vertex)
          return;
        path->points[index].smooth = value != 0;
        path->points[index].custom = path->points[index].broken = false;
        path->points[index].tangent_in = path->points[index].tangent_out = {};
        break;
      case bts::PathCurveAction::Custom:
        if (!vertex)
          return;
        bts::surface_path_custom_handles(*path, index, value != 0);
        break;
      case bts::PathCurveAction::Reverse:
        bts::surface_path_reverse(*path);
        break;
      case bts::PathCurveAction::Closed:
        path->closed = value != 0;
        break;
      case bts::PathCurveAction::DeleteVertex:
        if (!vertex)
          return;
        if (path->points.size() <= 2) {
          viewport->path_removed(path->id);
          return;
        }
        path->points.erase(path->points.begin() + index);
        break;
    }
    if (commit_surface_path(path->id, path, false, true, "Edit path curve")) {
      if (action == bts::PathCurveAction::Reverse && vertex)
        viewport->selected_vertex = int(path->points.size()) - 1 - index;
      else if (action == bts::PathCurveAction::DeleteVertex)
        viewport->selected_vertex = std::min(index, int(path->points.size()) - 1);
      refresh_paths();
    }
  };
  viewport->path_curve_panel->changed = viewport->path_curve_action;
  viewport->path_curve_panel->mode_changed = [&](int mode) {
    try {
      if (const auto source = selected_path_definition()) {
        auto path = std::make_shared<bts::SurfacePath>(*source);
        path->filled = mode == 2;
        path->ribbon.enabled = mode == 1;
        if (path->filled && path->points.size() >= 3)
          path->closed = true;
        commit_surface_path(path->id, path, false, true, "Change path mode");
      }
      else {
        filled_profile = mode == 2;
        ribbon_profile.enabled = mode == 1;
      }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
    select_material(selected_material);
  };
  viewport->path_curve_panel->ribbon_requested = [&] {
    auto profile = selected_path_definition() ? selected_path_definition()->ribbon :
                                                ribbon_profile;
    try {
      if (!bts::edit_ribbon_settings(workspace, profile))
        return;
      if (const auto source = selected_path_definition()) {
        auto path = std::make_shared<bts::SurfacePath>(*source);
        path->ribbon = profile;
        if (profile.enabled)
          path->filled = false;
        if (bts::surface_path_json(*path) != bts::surface_path_json(*source))
          commit_surface_path(path->id, path, false, true, "Edit ribbon path");
      }
      else {
        ribbon_profile = profile;
        if (profile.enabled)
          filled_profile = false;
      }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
    select_material(selected_material);
  };
  viewport->path_insert = [&](std::size_t segment, double parameter) {
    const auto source = selected_path_definition();
    if (!source)
      return;
    auto path = std::make_shared<bts::SurfacePath>(*source);
    const auto uid = snapshot.materials[selected_material].session_id;
    try {
      const auto inserted = bts::surface_path_insert(
          *path,
          segment,
          parameter,
          [&](const bts::SurfacePathPoint &point) -> std::optional<bts::SurfacePathPoint> {
            auto projection = path->brush.projection;
            projection.alignment = bts::BrushAlignment::TangentWrap;
            projection.cull = false;
            const auto hit = path_contact(uid, point, 100, true, projection);
            if (!hit || !hit->footprint.surface)
              return {};
            auto result = point;
            for (int c = 0; c < 3; ++c) {
              result.position[c] = hit->position[c];
              result.normal[c] = hit->footprint.surface->normal[c];
            }
            result.uv = hit->footprint.center;
            return result;
          });
      if (commit_surface_path(path->id, path, false, true, "Insert path vertex")) {
        viewport->selected_vertex = int(inserted);
        refresh_paths();
      }
    }
    catch (const std::exception &error) {
      workspace->append_console(QString::fromUtf8(error.what()));
    }
  };
  viewport->path_drag_begin = [&](const std::string &id, int, int handle) {
    if (!document || path_drag_before)
      return;
    path_drag_before = std::make_unique<bts::TextureDocument>(*document);
    path_drag_uid = snapshot.materials[selected_material].session_id;
    path_drag_id = id;
    path_drag_label = handle < 0 ? "Move path vertex" : "Edit path tangent";
    path_drag_error.clear();
    refresh_history_actions();
  };
  viewport->path_drag_update = [&](std::shared_ptr<const bts::SurfacePath> path) {
    if (!path_drag_before)
      return false;
    try {
      auto candidate = std::make_unique<bts::TextureDocument>(*path_drag_before);
      apply_surface_path(candidate->set(path_drag_uid), path_drag_id, path, false, true);
      maintain_history(*candidate);
      apply_document_layers(*candidate, path_drag_uid, int(candidate->settings().resolution));
      document = std::move(candidate);
      restore_path_profile();
      refresh_paths();
      path_drag_error.clear();
      return true;
    }
    catch (const std::exception &error) {
      if (path_drag_error != error.what()) {
        path_drag_error = error.what();
        workspace->append_console(QString::fromUtf8(error.what()));
      }
      return false;
    }
  };
  viewport->path_drag_end = [&](bool cancel) {
    if (!path_drag_before)
      return;
    const bool changed = document->edit_state() != path_drag_before->edit_state();
    if (cancel || !changed) {
      apply_document_layers(
          *path_drag_before, path_drag_uid, int(path_drag_before->settings().resolution));
      document = std::move(path_drag_before);
      restore_path_profile();
      refresh_paths();
    }
    else {
      undo_history.push_back({std::move(path_drag_before), path_drag_uid, path_drag_label});
      if (undo_history.size() > 128)
        undo_history.erase(undo_history.begin());
      redo_history.clear();
      refresh_document_ui();
    }
    path_drag_before.reset();
    path_drag_id.clear();
    path_drag_error.clear();
    refresh_history_actions();
  };
  viewport->paths_panel->selected = viewport->path_selected;
  viewport->paths_panel->removed = viewport->path_removed;
  viewport->paths_panel->finished = viewport->path_finished;
  viewport->paths_panel->renamed = [&](const std::string &id, const std::string &name) {
    viewport->selected_path = id;
    if (const auto source = selected_path_definition()) {
      auto path = std::make_shared<bts::SurfacePath>(*source);
      path->name = QString::fromStdString(name).trimmed().toStdString();
      commit_surface_path(id, path, false, false, "Rename surface path");
    }
    select_material(selected_material);
  };
  viewport->paths_panel->visibility = [&](const std::string &id, bool visible) {
    viewport->selected_path = id;
    if (const auto source = selected_path_definition()) {
      auto path = std::make_shared<bts::SurfacePath>(*source);
      path->visible = visible;
      commit_surface_path(id, path, false, false, "Change surface path visibility");
    }
    select_material(selected_material);
  };
  viewport->paths_panel->duplicated = [&](const std::string &id) {
    viewport->selected_path = id;
    if (const auto source = selected_path_definition()) {
      auto path = std::make_shared<bts::SurfacePath>(*source);
      path->id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      path->name += " copy";
      if (commit_surface_path(path->id, path, true, true, "Duplicate surface path")) {
        viewport->selected_path = path->id;
        restore_path_profile();
      }
    }
    select_material(selected_material);
  };
  select_material(selected_material);
}
