// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit AW workspace actor only. No mutation or normal-frame readback.
if (qEnvironmentVariableIsSet("BTS_AW_HEIGHT_IMAGE_UI")) {
  if (auto *choice = workspace->findChild<QComboBox *>("nativeHeightPolicy"))
    state["height_control_text"] = choice->currentText();
  state["document_path"] = document_path;
  state["height_pending"] = height_ui.pending();
  state["native_source_pending"] = native_material_loader && native_material_loader->pending();
  state["open_pending"] = opening_project || project_opener.pending();
  state["save_pending"] = bool(active_save) || bool(requested_save) || host.save_project_pending();
  state["open_error"] = workspace->property("projectOpenError").toString();
  state["save_error"] = workspace->property("projectSaveError").toString();
  state["height_error"] = workspace->property("nativeHeightError").toString();
  state["material_error"] = last_material_error;
  state["redo_count"] = int(redo_history.size());
  state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
  state["cpu_layer_compositions"] = qint64(bts::local_paint_materializations);
  state["compiled_shaders"] = qint64(blender::GPU_pass_global_compilation_count());
  state["inspection"] = int(host.inspection_mode());
  state["layer_thumbnails_pending"] = workspace->layer_thumbnails_pending();
  if (auto *error = workspace->findChild<QLabel *>("workspaceErrorText");
      error && error->isVisible())
    state["workspace_error"] = error->text();
  if (document && selected_material >= 0) {
    bts::EditStateScope metadata;
    bts::PaintMetadataScope paint_metadata;
    const auto &set = document->set(snapshot.materials.at(selected_material).session_id);
    const auto it = std::find_if(set.layers.begin(), set.layers.end(), [&](const auto &layer) {
      return layer.id == set.selected_layer;
    });
    if (it == set.layers.end())
      throw std::runtime_error("AW selected native Fill UUID missing");
    const auto &layer = *it;
    state["set"] = set.id.toString();
    state["layer"] = layer.id.toString();
    state["selected_mask"] = set.selected_mask;
    state["selected_effect"] = set.selected_effect.toString();
    state["edit_state"] = document->edit_state();
    state["height_policy"] = layer.fill.native_material ?
                                 int(layer.fill.native_material->height_policy) :
                                 -1;
    state["height_enabled"] = layer.channels[4];
    state["artist_normal"] = layer.channels[3];
    state["bump_origin"] = set.bump_origin;
    state["precision"] = int(set.precision[4]);
    state["instance"] = layer.fill.native_material ?
                            bts::native_material_description_json(*layer.fill.native_material) :
                            QJsonObject{};
    const QUuid source(QString::fromStdString(layer.fill.material_resource));
    for (const auto &asset : assets.assets())
      if (asset.id == source) {
        state["source"] = bts::asset_metadata(asset);
        break;
      }
    QJsonArray tiles, dimensions;
    for (int tile : set.tiles) {
      tiles.append(tile);
      dimensions.append(QJsonArray{tile, set.width(tile), set.rows(tile)});
    }
    state["tiles"] = tiles;
    state["dimensions"] = dimensions;
    const auto binding = host.diagnostic_native_source_queues()["bindings"]
                             .toObject()[QString::number(set.material_session_uid)]
                             .toObject();
    state["binding"] = binding;
    const auto handles = binding["handles"].toArray(), waiting = binding["waiting"].toArray();
    const auto role = handles.size() > 4 ? handles[4].toArray() : QJsonArray{};
    const bool ready = waiting.size() > 4 && !waiting[4].toBool() && role.size() == 3 &&
                       (role[1].toString() != "0" || role[2].toString() != "0");
    state["aw_probe_ready"] = ready;
    if (workspace->property("awHeightReadGpu").toBool() && !host.texture_updates_pending() &&
        ready)
    {
      constexpr std::array<std::array<int, 2>, 4> xy{{{13, 17}, {109, 17}, {13, 91}, {109, 113}}};
      QJsonArray packets;
      for (int tile : set.tiles) {
        const auto pixels = host.diagnostic_gpu_pixels(set.material_session_uid, 4, xy, tile);
        if (pixels.size() != xy.size())
          throw std::runtime_error("AW Bump point packet missing");
        QJsonArray points;
        for (std::size_t i = 0; i < xy.size(); ++i) {
          QJsonArray rgba;
          for (float value : pixels[i])
            rgba.append(value);
          points.append(QJsonObject{{"xy", QJsonArray{xy[i][0], xy[i][1]}}, {"rgba", rgba}});
        }
        packets.append(QJsonObject{{"tile", tile}, {"channel", 4}, {"points", points}});
      }
      state["gpu"] = packets;
    }
  }
  return state;
}
