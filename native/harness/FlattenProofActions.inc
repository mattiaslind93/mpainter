// SPDX-License-Identifier: GPL-3.0-or-later
if (parser.isSet("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_FLATTEN_DEMO")) {
  auto *setup = new QAction(workspace);
  setup->setObjectName("guiFlattenSetup");
  QObject::connect(setup, &QAction::triggered, workspace, [&] {
    edit_material([&](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      if (qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE")) {
        set.channels = {true, false, false, false, true};
        set.precision.fill(bts::ChannelPrecision::RGB32F);
        set.precision[4] = bts::ChannelPrecision::L32F;
        bts::TextureDocument::add_displacement_channel(set);
        set.displacement.midlevel = 0;
        set.displacement.precision = bts::ChannelPrecision::L32F;
        layer.values.bump = .125f;
        layer.displacement.enabled = true;
        layer.displacement.value = .001f;
      }
      if (qEnvironmentVariableIsSet("BTS_FLATTEN_LEGACY_NORMAL")) {
        set.channels[3] = true;
        layer.values.normal = {.2f, -.1f, .9746794f};
      }
    });
  });
  auto *paint_setup = new QAction(workspace);
  paint_setup->setObjectName("guiFlattenPaintSetup");
  QObject::connect(paint_setup, &QAction::triggered, workspace, [&] {
    if (qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE")) {
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::layer(set, set.selected_layer).displacement.enabled = true;
      });
      brush.displacement = true;
      brush.displacement_value = -.002f;
      brush.values.bump = -.25f;
    }
  });
  auto *group_setup = new QAction(workspace);
  group_setup->setObjectName("guiFlattenGroupSetup");
  QObject::connect(group_setup, &QAction::triggered, workspace, [&] {
    if (qEnvironmentVariableIsSet("BTS_FLATTEN_LARGE"))
      edit_material([&](bts::TextureSet &set) {
        bts::TextureDocument::layer(set, set.selected_layer).displacement.enabled = true;
      });
  });
  auto *plan = new QAction(workspace);
  plan->setObjectName("guiFlattenProbePlan");
  QObject::connect(plan, &QAction::triggered, workspace, [&] {
    bts::PaintMetadataScope metadata;
    if (!document || selected_material < 0)
      return;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    QJsonArray entries;
    const auto add = [&](const bts::TextureLayer &layer,
                         const bts::TilePaintPixels &tiles,
                         int channel,
                         bool mask,
                         int plane = -1) {
      if (plane < 0)
        plane = channel;
      for (const auto &[tile, pixels] : tiles.tiles()) {
        const int canvas = pixels.resolution();
        const auto versions = pixels.page_versions();
        if (versions[plane].empty())
          continue;
        const auto entry = [&](std::uint32_t page, bool empty) {
          const int x = int(page % 65536) * 64, y = int(page / 65536) * 64;
          entries.append(QJsonObject{{"layer", layer.id.toString()},
                                     {"tile", tile},
                                     {"channel", channel},
                                     {"mask", mask},
                                     {"page", empty ? QString("empty") : QString::number(page)},
                                     {"x", x},
                                     {"y", y},
                                     {"canvas", canvas},
                                     {"width", std::min(64, canvas - x)},
                                     {"height", std::min(64, canvas - y)}});
        };
        for (const auto &[page, version] : versions[plane])
          entry(page, false);
        if (!versions[plane].contains(0))
          entry(0, true);
      }
    };
    for (const auto &layer : set.layers) {
      if (layer.kind == bts::LayerKind::Paint)
        for (int c = 0; c < 5; ++c)
          add(layer, layer.pixels, c, false);
      if (layer.kind == bts::LayerKind::Paint)
        add(layer, layer.displacement.pixels, 14, false, 2);
      if (layer.mask)
        add(layer, layer.mask->pixels, 2, true);
    }
    workspace->setProperty("flattenProbePlan", entries);
  });
  auto *start = new QAction(workspace);
  start->setObjectName("guiFlattenProbeStart");
  QObject::connect(start, &QAction::triggered, workspace, [&] {
    const auto p = workspace->property("flattenProbeRequest").toJsonObject();
    auto source = document->set(snapshot.materials[selected_material].session_id);
    source.layers = bts::evaluation_layers(source);
    source.instance_layers.reset();
    source.tile_resolutions.clear();
    source.texture_resolution = p["canvas"].toInt();
    source.texture_height = 0;
    source.precision.fill(bts::ChannelPrecision::RGB32F);
    source.precision[4] = bts::ChannelPrecision::L32F;
    source.displacement.precision = bts::ChannelPrecision::L32F;
    source.channels.fill(true);
    auto &target = bts::TextureDocument::layer(source, QUuid(p["layer"].toString()));
    target.visible = true;
    target.depth = 0;
    target.parent = QUuid();
    target.geometry = {};
    if (target.mask)
      target.mask->enabled = true;
    bts::LayerCaptureRequest request{target.id,
                                     p["mask"].toBool() ? 0 : p["channel"].toInt(),
                                     p["tile"].toInt(),
                                     p["x"].toInt(),
                                     p["y"].toInt(),
                                     p["width"].toInt(),
                                     p["height"].toInt(),
                                     source.width(p["tile"].toInt()),
                                     source.rows(p["tile"].toInt()),
                                     p["mask"].toBool(),
                                     false};
    workspace->setProperty("flattenProbeReady", false);
    workspace->setProperty("flattenProbeTicket",
                           qulonglong(host.request_layer_capture(source, request)));
    viewport->update();
  });
  auto *poll = new QAction(workspace);
  poll->setObjectName("guiFlattenProbePoll");
  QObject::connect(poll, &QAction::triggered, workspace, [&] {
    auto result = host.layer_capture_result();
    if (!result || result->ticket != workspace->property("flattenProbeTicket").toULongLong())
      return;
    if (!result->valid || !result->pixels)
      qFatal("Flatten source proof GPU capture failed");
    const auto bytes = QByteArray::fromRawData(
        reinterpret_cast<const char *>(result->pixels->data()),
        qsizetype(result->pixels->size() * sizeof(bts::FillTexture::Pixel)));
    workspace->setProperty(
        "flattenProbeHash",
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
    workspace->setProperty("flattenProbeReady", true);
  });
  auto *material = new QAction(workspace);
  material->setObjectName("guiFlattenMaterialProbes");
  QObject::connect(material, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    QJsonArray values;
    for (int tile : set.tiles)
      for (int c : {4, 16}) {
        if (c == 16 && !set.displacement.channel)
          continue;
        for (const auto &uv :
             std::array<std::array<double, 2>, 3>{{{.25, .25}, {.65, .5}, {.9, .9}}})
        {
          const int x = std::min(set.width(tile) - 1, int(uv[0] * set.width(tile))),
                    y = std::min(set.rows(tile) - 1, int(uv[1] * set.rows(tile)));
          const auto pixel = host.diagnostic_gpu_pixel(set.material_session_uid, c, x, y, tile);
          if (pixel) {
            const float base = c == 4 ? set.base_values.bump : set.displacement.midlevel;
            values.append(QJsonObject{{"channel", c == 16 ? 14 : c},
                                      {"tile", tile},
                                      {"x", x},
                                      {"y", y},
                                      {"value", (*pixel)[0] + base * (1 - (*pixel)[3])}});
          }
        }
      }
    workspace->setProperty("flattenMaterialProbes", values);
  });
}
