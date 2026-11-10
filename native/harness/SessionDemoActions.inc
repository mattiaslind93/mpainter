// SPDX-License-Identifier: GPL-3.0-or-later
// Harness: the named QActions the --gui-fix-demo actors trigger, and the actor start. Part of
// WorkspaceSession::run() (app/WorkspaceSession.inc), included in order inside its body; it shares
// that function's locals and is not a standalone file.
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_LIGHTING_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiLightingSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/lighting.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiLightingOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/lighting.blend");
  });
  auto *legacy = new QAction(workspace);
  legacy->setObjectName("guiLightingLegacy");
  QObject::connect(legacy, &QAction::triggered, workspace, [&] {
    open_document(qEnvironmentVariable("BTS_LEGACY_LIGHTING_PROJECT"));
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_DISPLACEMENT_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiDisplacementSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/displacement.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiDisplacementOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/displacement.blend");
  });
  auto *source_obj = new QAction(workspace);
  source_obj->setObjectName("guiDisplacementExportOriginal");
  QObject::connect(source_obj, &QAction::triggered, workspace, [&] {
    QFile file(workspace->property("displacementObj").toString());
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Source OBJ proof unavailable");
    file.write(bts::original_snapshot_obj(host.scene_snapshot()));
  });
  auto *legacy = new QAction(workspace);
  legacy->setObjectName("guiDisplacementLegacy");
  QObject::connect(legacy, &QAction::triggered, workspace, [&] {
    open_document(qEnvironmentVariable("BTS_LEGACY_DISPLACEMENT_PROJECT"));
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_LAYER_EDIT_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiLayerEditSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/layers.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiLayerEditOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/layers.blend");
  });
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiLayerEditPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({.5, .5}, 1.2);
    const auto source = host.scene_snapshot();
    QVariantList points;
    QVariantList probes;
    for (QPointF target : {QPointF(.25, .25), QPointF(.5, .5), QPointF(.75, .75)}) {
      QPointF chosen;
      double distance = 1e20;
      for (const auto &triangle : source.uv_triangles)
        if (triangle.material == std::size_t(selected_material)) {
          QPointF uv;
          for (const auto &p : triangle.corners)
            uv += QPointF(p[0], p[1]) / 3.;
          const double d = QLineF(uv, target).length();
          if (d < distance) {
            distance = d;
            chosen = uv;
          }
        }
      points.append(viewport->uv_point(chosen));
      probes.append(chosen);
    }
    workspace->setProperty("layerEditPoints", points);
    workspace->setProperty("layerEditProbes", probes);
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_BAKE_MULTISET_HISTORY_DEMO"))
{
  auto *seed = new QAction(workspace);
  seed->setObjectName("guiSeedMultiSetMapHistory");
  QObject::connect(seed, &QAction::triggered, workspace, [&] {
    if (!document || document->texture_sets().size() != 2 || selected_material != 0)
      throw std::runtime_error("Multiset history fixture needs two sets with A selected");
    int index = 0;
    for (const auto &entry : document->texture_sets()) {
      auto &set = document->set(entry.material_session_uid);
      if (set.disabled || set.tiles != std::set<int>{1001})
        throw std::runtime_error("Multiset history fixture needs enabled single-tile sets");
      set.precision[0] = bts::ChannelPrecision::RGB32F;
      bts::TextureLayer fill;
      fill.kind = bts::LayerKind::Fill;
      fill.name = "AO map history fixture";
      fill.values.base_color = {1, 1, 1, 1};
      fill.channels.fill(false);
      fill.channels[0] = true;
      fill.ao_enabled = false;
      fill.displacement.enabled = false;
      for (auto &user : fill.users)
        user.enabled = false;
      fill.mask = bts::LayerMask{};
      fill.mask->background = 0;
      bts::MaskEffect effect;
      effect.kind = bts::MaskEffectKind::Generator;
      effect.generator = 3;
      effect.map_filter = 0;
      effect.curve = 0;
      effect.contrast = 1;
      effect.bias = 0;
      effect.blending = bts::LayerBlend::Normal;
      fill.mask->effects.push_back(effect);
      set.selected_layer = fill.id;
      set.layers = {std::move(fill)};
      set.selected_mask = false;
      set.selected_effect = QUuid();
      const float value = index++ == 0 ? .25f : .75f;
      std::vector<float> rgba(32 * 32 * 4);
      for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = rgba[i + 1] = rgba[i + 2] = value;
        rgba[i + 3] = 1;
      }
      auto pixels = std::make_shared<const bts::MeshMapPixels>(32, std::move(rgba));
      bts::MeshMap map;
      map.name = "Old AO history fixture";
      map.tiles.emplace(1001, bts::MeshMapPlane{32, pixels->hash, pixels});
      set.mesh_maps[3] = std::move(map);
    }
    document->resolve_instances();
    for (const auto &set : document->texture_sets())
      apply_preview(set, 128);
    workspace->setProperty("mapHistorySeeded", true);
    refresh_document_ui(false);
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_BAKE_FIX_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiBakeFixSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/baking.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiBakeFixOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/baking.blend");
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_FILL_TEXTURE_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiFillTextureSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/fill-texture.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiFillTextureOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/fill-texture.blend");
  });
  auto *legacy = new QAction(workspace);
  legacy->setObjectName("guiFillChannelsLegacy");
  QObject::connect(legacy, &QAction::triggered, workspace, [&] {
    open_document(qEnvironmentVariable("BTS_FILL_CHANNELS_LEGACY"));
  });
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiFillTexturePrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.5, .5}, 1.2);
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_RIBBON_DEMO")) {
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiRibbonSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/ribbon.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiRibbonOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/ribbon.blend");
  });
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiRibbonPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.5, .5}, 1.2);
    workspace->inspector_splitter()->setSizes({80, 180, 560, 0});
    QVariantList points;
    for (const QVector3D p :
         {QVector3D(-.7, 0, -.25), QVector3D(-.1, 0, -.25), QVector3D(.55, 0, .2)})
      points.append(viewport->scene_point(p));
    if (qEnvironmentVariableIsSet("BTS_FILLED_PATH_DEMO")) {
      points.clear();
      for (const QVector3D p : {QVector3D(-.75, 0, -.4),
                                QVector3D(-.1, 0, -.4),
                                QVector3D(-.1, 0, .15),
                                QVector3D(-.75, 0, .15)})
        points.append(viewport->scene_point(p));
    }
    workspace->setProperty("ribbonPoints", points);
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_PLANAR_DEMO")) {
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("guiPlanarSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/planar.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiPlanarOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    open_document(directory + "/planar.blend");
  });
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiPlanarPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv(qEnvironmentVariableIsSet("BTS_IMAGE_SEQUENCE_DEMO") ? QPointF(1.5, .5) :
                                                                              QPointF(.5, .5),
                       qEnvironmentVariableIsSet("BTS_IMAGE_SEQUENCE_DEMO") ? 3.6 : 1.2);
    workspace->inspector_splitter()->setSizes({80, 180, 580, 0});
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_CLIPPING_DEMO"))
{
  auto *pose = new QAction(workspace);
  pose->setObjectName("guiClipFront");
  QObject::connect(pose, &QAction::triggered, workspace, [&] { viewport->clipping_demo_front(); });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_BLEND_IMAGES_DEMO"))
{
  const auto add = [&](const char *name, std::function<void()> run) {
    auto *a = new QAction(workspace);
    a->setObjectName(name);
    QObject::connect(a, &QAction::triggered, workspace, std::move(run));
  };
  add("guiBlendImagesPrepare", [&] {
    edit_material([](bts::TextureSet &set) {
      set.precision[0] = bts::ChannelPrecision::RGB32F;
      set.precision[1] = set.precision[2] = bts::ChannelPrecision::L32F;
    });
  });
  add("guiBlendImagesAlter", [&] {
    edit_material([](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      layer.channels[4] = true;
      layer.values.bump = .125f;
      fill_channel_texture(layer.fill, 2).reset();
      layer.values.roughness = .7f;
    });
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_POLYGON_BUCKET_DEMO"))
{
  const auto directory = harness.value("gui-fix-demo");
  const auto add = [&](const char *name, std::function<void()> run) {
    auto *a = new QAction(workspace);
    a->setObjectName(name);
    QObject::connect(a, &QAction::triggered, workspace, std::move(run));
  };
  add("guiPolygonPrepare", [&] {
    edit_material([](bts::TextureSet &set) {
      set.precision[0] = bts::ChannelPrecision::RGB32F;
      bts::set_layer_channel(set, set.selected_layer, 1, false);
    });
    brush.values.base_color = {1, 0, 0, 1};
    brush.values.roughness = .23;
    brush.values.bump = .01;
    brush.values.metallic = .9;
    viewport->secondary_color = {0, 0, 1, 1};
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.75, .5}, 3.2);
  });
  add("guiPolygonSample", [&] {
    host.flush_texture_updates();
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    QJsonArray samples;
    for (const auto uv : std::array<QPointF, 4>{{{.25, .25}, {1.25, .25}, {.75, .75}, {.95, .05}}})
    {
      const int tile = 1001 + int(uv.x());
      QJsonArray channels;
      for (int c : {0, 1, 2, 4}) {
        const auto p = host.diagnostic_gpu_pixel(set.material_session_uid,
                                                 c,
                                                 int((uv.x() - int(uv.x())) * set.width(tile)),
                                                 int(uv.y() * set.rows(tile)),
                                                 tile);
        QJsonArray rgba;
        if (p)
          for (float n : *p)
            rgba.append(n);
        channels.append(rgba);
      }
      samples.append(channels);
    }
    workspace->setProperty("polygonSamples", samples);
  });
  add("guiPolygonSave", [&, directory] { save_document(directory + "/polygon.blend"); });
  add("guiPolygonOpen", [&, directory] {
    open_document(directory + "/polygon.blend");
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.75, .5}, 3.2);
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_MASK_FILL_GPU_DEMO"))
{
  auto *sample = new QAction(workspace);
  sample->setObjectName("guiMaskFillSample");
  QObject::connect(sample, &QAction::triggered, workspace, [&] {
    host.flush_texture_updates();
    QJsonArray pixels;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    for (int y = 35; y < 512; y += 53)
      for (int x = 27; x < 512; x += 61) {
        const auto p = host.diagnostic_gpu_pixel(set.material_session_uid, 0, x, y);
        if (!p)
          throw std::runtime_error("AO GPU pixel absent");
        pixels.append(QJsonArray{(*p)[0], (*p)[1], (*p)[2], (*p)[3]});
      }
    workspace->setProperty("aoGpuSamples", pixels);
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_STROKE_TARGET_PROFILE"))
{
  auto *target = new QAction(workspace);
  target->setObjectName("guiStrokeProfileTarget");
  QObject::connect(target, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    viewport->end_paint(false);
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    set.selected_mask = workspace->property("strokeProfileMask").toBool();
    set.selected_effect = QUuid();
    select_material(selected_material);
    brush.diameter = 3;
    brush.spacing = 10;
    brush.flow = 100;
    brush.hardness = 1;
    brush.pressure_size = false;
    brush.channels = {true, false, false, false, false};
    brush.ao = false;
    brush.displacement = false;
    brush.values.base_color = {.85f, .2f, .1f, 1};
    brush.mask_value = 0;
    const int tile = workspace->property("strokeProfileTile").toInt();
    const bool scene = workspace->property("strokeProfileScene").toBool();
    viewport->set_layout({scene ? bts::ViewLayout::SceneOnly : bts::ViewLayout::UvOnly, false});
    QPointF point;
    if (!scene) {
      viewport->focus_uv({double((tile - 1001) % 10) + .5, double((tile - 1001) / 10) + .5}, 1.2);
      point = viewport->uv_point(
          {double((tile - 1001) % 10) + .5, double((tile - 1001) / 10) + .5});
    }
    else {
      bool found = false;
      const auto rectangle = viewport->pane_rect(bts::ViewKind::Scene);
      const double ratio = viewport->devicePixelRatioF();
      const bts::ViewPane pane{bts::ViewKind::Scene,
                               qRound(rectangle.x() * ratio),
                               qRound(rectangle.y() * ratio),
                               qRound(rectangle.width() * ratio),
                               qRound(rectangle.height() * ratio)};
      for (const auto &triangle : snapshot.uv_triangles) {
        if (snapshot.materials[triangle.material].session_id != set.material_session_uid)
          continue;
        double u = 0, v = 0;
        QVector3D center;
        for (int i = 0; i < 3; ++i) {
          u += triangle.corners[i][0] / 3.;
          v += triangle.corners[i][1] / 3.;
          center += QVector3D(triangle.positions[i][0],
                              triangle.positions[i][1],
                              triangle.positions[i][2]) /
                    3;
        }
        if (1001 + int(std::floor(u)) + 10 * int(std::floor(v)) != tile)
          continue;
        const auto p = viewport->scene_point(center);
        if (!rectangle.contains(p))
          continue;
        if (viewport->surface_at(pane, p) &&
            viewport->surface_triangle_id_ == triangle.triangle_id)
        {
          point = p;
          found = true;
          break;
        }
      }
      if (!found)
        throw std::runtime_error("No visible original triangle for stroke profile tile");
    }
    workspace->setProperty("strokeProfilePoint", QVariantList{point.x(), point.y()});
    viewport->update();
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_TEMPORARY_INK_DEMO"))
{
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiTemporaryInkPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({.5, .5}, 1.2);
    brush.diameter = 8;
    brush.hardness = 1;
    brush.spacing = 4;
    brush.pressure_size = false;
    brush.pressure_flow = false;
    brush.values.base_color = {1, 0, 0, 1};
    select_material(selected_material);
    auto *picker = static_cast<bts::ColorPicker *>(
        workspace->findChild<QWidget *>("secondaryColorPicker"));
    picker->accept_sample({0, 0, 1, 1});
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_SKEW_DEMO")) {
  const auto directory = harness.value("gui-fix-demo");
  auto *shader_undo = new QAction(workspace);
  shader_undo->setObjectName("guiShaderPaintUndo");
  QObject::connect(shader_undo, &QAction::triggered, workspace, [&] { undo_action->trigger(); });
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiSkewPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.5, .5}, 1.2);
  });
  auto *save = new QAction(workspace);
  save->setObjectName("guiSkewSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/skew.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("guiSkewOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    if (qEnvironmentVariableIsSet("BTS_SPSM_DEMO"))
      begin_open_document(directory + "/skew.blend");
    else
      open_document(directory + "/skew.blend");
  });
  auto *reference = new QAction(workspace);
  reference->setObjectName("guiSkewReference");
  QObject::connect(reference, &QAction::triggered, workspace, [&] {
    workspace->setProperty("skewReference", skew->reference());
  });
}
#include "UvProofActions.inc"
#include "FlattenProofActions.inc"
#include "FolderMaskProofActions.inc"
bool gui_composite_checked = false;
QJsonArray gui_composite_differences;
if (harness.is_set("gui-fix-demo") && workspace) {
  auto *flush = new QAction(workspace);
  flush->setObjectName("flushPointerInput");
  QObject::connect(
      flush, &QAction::triggered, workspace, [&] { viewport->flush_pointer_input(); });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_AA_PERFORMANCE"))
{
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiAaPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    edit_material([&](bts::TextureSet &set) {
      set.channels[3] = true;
      set.ao_channel = true;
    });
    brush.channels = {true, true, true, true, true};
    brush.ao = true;
    brush.values.bump = .08;
    brush.values.base_color = {.1, .2, .3, 1};
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({1, .5}, 2.4);
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_LAYER_CHANNEL_DEMO"))
{
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiChannelPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    brush.values.base_color = {.25, .1, .8, 1};
    brush.diameter = 40;
    brush.hardness = 1;
    brush.spacing = 20;
    brush.pressure_size = false;
    brush.pressure_flow = false;
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({1, .5}, 2.4);
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_AD_STROKE_BENCH"))
{
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiAdPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    brush.channels = {true, false, false, false, false};
    brush.ao = false;
    brush.values.base_color = {.85, .2, .16, 1};
    brush.diameter = 12;
    brush.spacing = 40;
    brush.stroke_opacity = 100;
    brush.pressure_size = false;
    viewport->set_layout({bts::ViewLayout::UvOnly, false});
    viewport->focus_uv({.5, .5}, 1.4);
    select_material(selected_material);
  });
}
if ((harness.is_set("gui-fix-demo") || qEnvironmentVariableIsSet("BTS_WARP_GEOMETRY_DEMO")) &&
    workspace)
{
  auto *check = new QAction(workspace);
  check->setObjectName("checkGuiGpuStack");
  QObject::connect(check, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    host.flush_texture_updates();
    host.verify_gpu_texture_stack(snapshot.materials[selected_material].session_id);
  });
}
if (harness.is_set("gui-fix-demo") && workspace && qEnvironmentVariableIsSet("BTS_SPSM_DEMO")) {
  auto *check = new QAction(workspace);
  check->setObjectName("checkSpsmGpuScope");
  QObject::connect(check, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    host.flush_texture_updates();
    QJsonObject scope{{"scope", "whole_material_cpu_parity"}};
    try {
      scope["maximum_error"] = host.verify_gpu_texture_stack(
          snapshot.materials[selected_material].session_id);
      scope["status"] = "passed";
    }
    catch (const bts::SpatialOracleUnavailable &error) {
      scope["status"] = "unavailable";
      scope["reason"] = QString::fromUtf8(error.what());
    }
    workspace->setProperty("spsmOracleScope", scope);
  });
}

if (harness.is_set("gui-fix-demo") && workspace) {
  auto *check = new QAction(workspace);
  check->setObjectName("checkGuiComposite");
  QObject::connect(check, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    // Compare the actual native GPU targets against the independent CPU
    // oracle. apply_texture_set is asynchronous; reading an obsolete CPU
    // ImBuf immediately afterwards is not a valid reference comparison.
    host.flush_texture_updates();
    try {
      host.verify_gpu_texture_stack(snapshot.materials[selected_material].session_id);
    }
    catch (const std::exception &error) {
      gui_composite_differences.append(QJsonObject{{"error", QString::fromUtf8(error.what())}});
    }
    gui_composite_checked = true;
  });
}
if (harness.is_set("gui-fix-demo") && workspace &&
    (qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO") ||
     qEnvironmentVariableIsSet("BTS_AB_DISPLACEMENT")))
{
  const auto directory = harness.value("gui-fix-demo");
  auto add = [&](const char *name, std::function<void()> fn) {
    auto *action = new QAction(workspace);
    action->setObjectName(name);
    QObject::connect(action, &QAction::triggered, workspace, std::move(fn));
  };
  add("guiTextureImportPrepare", [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({1, .5}, 2.4);
    workspace->inspector_splitter()->setSizes({80, 180, 580, 0});
  });
  add("guiTextureImportSave",
      [&, directory] { save_document(directory + "/texture-import.blend"); });
  add("guiTextureImportOpen",
      [&, directory] { open_document(directory + "/texture-import.blend"); });
  add("guiTextureImportHideSources", [&] {
    const auto folder = qEnvironmentVariable("BTS_TEXTURE_IMPORT_FIXTURES");
    if (!QFileInfo(folder).canonicalFilePath().startsWith(
            QFileInfo(
                qEnvironmentVariable("BTS_ROOT", QCoreApplication::applicationDirPath() + "/..") +
                "/build")
                .canonicalFilePath() +
            "/"))
      throw std::runtime_error("Synthetic fixture directory must be inside BTS build");
    for (const auto &name : QDir(folder).entryList(QDir::Files))
      if (name.endsWith(".exr") || name.endsWith(".png") || name.endsWith(".tif") ||
          name.endsWith(".hdr") || name.endsWith(".jpg"))
        if (!QFile::rename(folder + "/" + name, folder + "/" + name + ".hidden"))
          throw std::runtime_error("Cannot hide synthetic source");
  });
}
if ((harness.is_set("gui-fix-demo") || qEnvironmentVariableIsSet("BTS_WARP_GEOMETRY_DEMO")) &&
    workspace)
{
  auto *capture = new QAction(workspace);
  capture->setObjectName("guiCaptureViewport");
  QObject::connect(capture, &QAction::triggered, workspace, [&] {
    viewport->grabFramebuffer().save(workspace->property("guiCapturePath").toString());
  });
}
if (workspace && qEnvironmentVariableIsSet("BTS_OPENPBR_IMAGES_DEMO")) {
  auto *probe = new QAction(workspace);
  probe->setObjectName("guiOpenPbrImportSample");
  QObject::connect(probe, &QAction::triggered, workspace, [&] {
    if (!document || selected_material < 0)
      return;
    const auto &set = document->set(snapshot.materials[selected_material].session_id);
    bts::MaterialSampleRequest request;
    request.tile = workspace->property("pbrSampleTile").toInt();
    request.x = request.y = 64;
    const auto ticket = host.request_material_sample(set, request);
    workspace->setProperty("pbrSamplePending", true);
    auto *timer = new QTimer(workspace);
    QObject::connect(timer, &QTimer::timeout, workspace, [&, timer, ticket] {
      auto result = host.material_sample_result();
      if (!result || result->ticket != ticket)
        return;
      const auto &v = result->material;
      workspace->setProperty(
          "pbrSample",
          QJsonObject{{"valid", result->valid},
                      {"color", QJsonArray{v.base_color[0], v.base_color[1], v.base_color[2]}},
                      {"metallic", v.metallic},
                      {"roughness", v.roughness},
                      {"bump", v.bump}});
      workspace->setProperty("pbrSamplePending", false);
      timer->stop();
      timer->deleteLater();
    });
    timer->start(16);
    viewport->update();
  });
}
#include "CheckboxWitness.inc"
#include "Res8kWitness.inc"
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_TEXTURE_SET_SELECTION_DEMO"))
{
  auto *prepare = new QAction(workspace);
  prepare->setObjectName("guiTextureSetSelectionPrepare");
  QObject::connect(prepare, &QAction::triggered, workspace, [&] {
    if (!document || snapshot.materials.size() != 3)
      qFatal("Selection regression needs exactly three materials");
    auto shaders = document->shader_instances();
    for (int row = 0; row < 3; ++row) {
      auto &set = document->set(snapshot.materials[row].session_id);
      set.layers.clear();
      for (int layer = 0; layer <= row; ++layer) {
        bts::TextureLayer value;
        value.kind = bts::LayerKind::Fill;
        value.name = "Selection layer " + std::to_string(row) + "/" + std::to_string(layer);
        value.values.roughness = .2f * (row + 1);
        set.layers.push_back(std::move(value));
      }
      set.selected_layer = set.layers.back().id;
      set.texture_resolution = 128 << row;
      const auto id = shaders.create(shaders.assignments.at(set.id),
                                     QString("Selection shader %1").arg(row));
      shaders.assign(set.id, id);
      auto parameters = shaders.for_set(set.id).parameters;
      parameters.normal_intensity = .5f * (row + 1);
      parameters.ao_intensity = .25f * (row + 1);
      shaders.parameters(id, parameters);
    }
    document->set_shader_instances(std::move(shaders));
    for (const auto &set : document->texture_sets())
      apply_preview(set, set.texture_resolution);
    refresh_document_ui(false);
  });
}
if (harness.is_set("material-layering-demo") && workspace) {
  bts::MaterialLayeringArtistHooks test_hooks{
      host,
      [&]() -> const bts::TextureDocument * { return document.get(); },
      [&] {
        return document && selected_material >= 0 ?
                   snapshot.materials.at(selected_material).session_id :
                   0u;
      },
      [&] {
        QJsonArray channels, users;
        for (bool value : brush.channels)
          channels.append(value);
        for (bool value : brush.users)
          users.append(value);
        return QJsonObject{
            {"undo_count", int(undo_history.size())},
            {"redo_count", int(redo_history.size())},
            {"document_revision", qint64(document_revision)},
            {"composite_pending", host.texture_updates_pending()},
            {"property_pending", bool(uniform_before && uniform_kind == UniformKind::Property)},
            {"property_dragging", property_dragging},
            {"render_frames", viewport->frame_count},
            {"shader_failures", QString::fromStdString(host.shader_failures())},
            {"parameter_import_pending", parameter_import && parameter_import->pending()},
            {"parameter_import_text",
             parameter_import ? parameter_import->status().text : QString()},
            {"parameter_import_error",
             parameter_import ? parameter_import->status().error : QString()},
            {"brush_channels", channels},
            {"brush_users", users},
            {"brush_ao", brush.ao},
            {"brush_displacement", brush.displacement},
            {"texture_loading", texture_loader && texture_loader->pending()},
            {"open_pending", opening_project},
            {"open_error", workspace->property("projectOpenError").toString()},
            {"save_pending",
             bool(active_save) || bool(requested_save) || host.save_project_pending()},
            {"save_error", workspace->property("projectSaveError").toString()},
            {"project_resources", [&] {
               QJsonArray rows;
               for (const auto &asset : assets.project_assets())
                 rows.append(QJsonObject{{"id", asset.id.toString()}, {"hash", asset.hash()}});
               return rows;
             }()}};
      },
      [&] {
        viewport->set_layout({bts::ViewLayout::UvOnly, false});
        viewport->focus_uv({1, .5}, 2.4);
        workspace->inspector_splitter()->setSizes({90, 220, 500, 0});
      },
      [&](std::array<double, 2> first, std::array<double, 2> last) {
        const auto a = viewport->uv_point({first[0], first[1]}),
                   b = viewport->uv_point({last[0], last[1]});
        send_mouse_event(*viewport,
                         QEvent::MouseButtonPress,
                         a,
                         Qt::LeftButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        for (int i = 1; i <= 8; ++i)
          send_mouse_event(*viewport,
                           QEvent::MouseMove,
                           a + (b - a) * (double(i) / 8),
                           Qt::NoButton,
                           Qt::LeftButton,
                           Qt::NoModifier);
        send_mouse_event(*viewport,
                         QEvent::MouseButtonRelease,
                         b,
                         Qt::LeftButton,
                         Qt::NoButton,
                         Qt::NoModifier);
      },
      [&](QPointF uv) { return viewport->uv_point(uv); },
      [&](const QString &path) { save_document(path); },
      [&](const QString &path) { begin_open_document(path); },
      [&](std::array<double, 3> p) {
        return viewport->scene_point({float(p[0]), float(p[1]), float(p[2])});
      },
      [&]() -> std::function<void()> {
        // Only the shipped diagnostic calls this adapter. It changes no
        // document state and restores the ordinary view before history/Save.
        const auto layout = viewport->view_layout();
        const bool orthographic = viewport->view_state()["scene_projection"] ==
                                  "Orthographic view";
        const bool paint = viewport->paint_enabled, polygon = viewport->polygon_active,
                   path = viewport->path_active, cursor = viewport->cursor_visible_,
                   pressure = viewport->pressure_overlay_;
        const auto projection = viewport->fill_projection;
        const auto symmetry = viewport->symmetry;
        const auto skew = viewport->skew_field;
        viewport->paint_enabled = false;
        viewport->polygon_active = false;
        viewport->path_active = false;
        viewport->cursor_visible_ = false;
        viewport->pressure_overlay_ = false;
        viewport->fill_projection.reset();
        viewport->symmetry = nullptr;
        viewport->skew_field.reset();
        viewport->set_layout({bts::ViewLayout::SceneOnly, false});
        viewport->set_projection(bts::OrbitCamera::Projection::Orthographic);
        return [weak = QPointer<Viewport>(viewport),
                layout,
                orthographic,
                paint,
                polygon,
                path,
                cursor,
                pressure,
                projection,
                symmetry,
                skew] {
          if (!weak)
            return;
          weak->paint_enabled = paint;
          weak->polygon_active = polygon;
          weak->path_active = path;
          weak->cursor_visible_ = cursor;
          weak->pressure_overlay_ = pressure;
          weak->fill_projection = projection;
          weak->symmetry = symmetry;
          weak->skew_field = skew;
          weak->set_projection(orthographic ? bts::OrbitCamera::Projection::Orthographic :
                                              bts::OrbitCamera::Projection::Perspective);
          weak->set_layout(layout);
          weak->update();
        };
      }};
  test_hooks.publish_parameters = [&](std::uint64_t expected,
                                      std::uint32_t uid,
                                      std::unique_ptr<bts::TextureDocument> &candidate,
                                      const std::vector<bts::StudioAsset> &originals) {
    return publish_parameters(expected, uid, candidate, originals, {});
  };
  pd149_actor_retire = bts::start_material_layering_ui_artist(
      workspace,
      viewport,
      harness.value("material-layering-demo"),
      harness.value("gui-fix-mesh"),
      harness.value("gui-fix-resolution").toInt(),
      std::move(test_hooks));
  auto previous = std::move(viewport->before_native_close);
  viewport->before_native_close = [previous = std::move(previous),
                                   retire = pd149_actor_retire]() noexcept {
    retire();
    if (previous)
      previous();
  };
}
if (harness.is_set("gui-fix-demo") && workspace &&
    qEnvironmentVariableIsSet("BTS_PAINTER_WORKFLOW_DEMO"))
{
  auto *fixture = new QAction(workspace);
  fixture->setObjectName("painterWorkflowImportedKinds");
  QObject::connect(fixture, &QAction::triggered, workspace, [&] {
    // Deterministic typed metadata for the two import-only kinds. Rendering,
    // editing and selection still use the production document/GPU paths.
    edit_material([](bts::TextureSet &set) {
      auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      bts::LevelsEffect fill;
      fill.kind = bts::ContentEffectKind::Fill;
      fill.channel = 0;
      fill.fill_value = {.75f, .42f, .2f};
      fill.opacity = .35f;
      fill.blending = bts::LayerBlend::Multiply;
      fill.name = "Imported color fill";
      layer.effects.push_back(fill);
      if (!layer.mask)
        layer.mask = bts::LayerMask{};
      std::vector<bts::MaskEffect> children;
      for (const auto kind : {bts::MaskEffectKind::Generator,
                              bts::MaskEffectKind::Filter,
                              bts::MaskEffectKind::Fill,
                              bts::MaskEffectKind::Levels,
                              bts::MaskEffectKind::Anchor,
                              bts::MaskEffectKind::Paint})
      {
        bts::MaskEffect child;
        child.kind = kind;
        child.name = kind == bts::MaskEffectKind::Generator ? "Nested Generator" :
                     kind == bts::MaskEffectKind::Filter    ? "Nested Filter" :
                     kind == bts::MaskEffectKind::Fill      ? "Nested Fill" :
                     kind == bts::MaskEffectKind::Levels    ? "Nested Levels" :
                                                              "Nested Paint";
        if (kind == bts::MaskEffectKind::Anchor) {
          child.name.clear();
          child.anchor_name = "Nested Anchor";
        }
        if (kind == bts::MaskEffectKind::Generator) {
          child.generator = 7;
          child.opacity = .864f;
        }
        if (kind == bts::MaskEffectKind::Fill)
          child.fill = .8f;
        if (kind == bts::MaskEffectKind::Paint)
          child.paint.strokes = std::make_shared<const bts::StrokeHistory>();
        if (kind == bts::MaskEffectKind::Filter)
          child.filter.resource = 1;
        child.levels.id = child.id;
        children.push_back(child);
      }
      bts::MaskEffect inner;
      inner.kind = bts::MaskEffectKind::Group;
      inner.name = "Nested mask group";
      inner.group_effects = std::make_shared<const std::vector<bts::MaskEffect>>(
          std::move(children));
      bts::MaskEffect sibling;
      sibling.kind = bts::MaskEffectKind::Fill;
      sibling.fill = .9f;
      sibling.name = "Untouched sibling";
      sibling.enabled = false;
      bts::MaskEffect group;
      group.kind = bts::MaskEffectKind::Group;
      group.name = "Imported mask group";
      group.opacity = .65f;
      group.blending = bts::LayerBlend::Multiply;
      group.group_effects = std::make_shared<const std::vector<bts::MaskEffect>>(
          std::vector{inner, sibling});
      bts::MaskEffect baseline;
      baseline.kind = bts::MaskEffectKind::Fill;
      baseline.fill = 1;
      baseline.name = "Group input baseline";
      layer.mask->effects.push_back(baseline);
      layer.mask->effects.push_back(group);
    });
  });
  auto *paint_setup = new QAction(workspace);
  paint_setup->setObjectName("painterWorkflowPaintSetup");
  QObject::connect(paint_setup, &QAction::triggered, workspace, [&] {
    viewport->set_layout({bts::ViewLayout::Split, false});
    viewport->focus_uv({.5, .5}, 1.3);
    update_inspection(bts::InspectionMode::Mask);
  });
  auto *paint_finish = new QAction(workspace);
  paint_finish->setObjectName("painterWorkflowPaintFinish");
  QObject::connect(paint_finish, &QAction::triggered, workspace, [&] {
    update_inspection(bts::InspectionMode::BaseColor);
  });
  const auto directory = harness.value("gui-fix-demo");
  auto *save = new QAction(workspace);
  save->setObjectName("painterWorkflowSave");
  QObject::connect(save, &QAction::triggered, workspace, [&, directory] {
    save_document(directory + "/painter-workflow.blend");
  });
  auto *open = new QAction(workspace);
  open->setObjectName("painterWorkflowOpen");
  QObject::connect(open, &QAction::triggered, workspace, [&, directory] {
    begin_open_document(directory + "/painter-workflow.blend");
  });
}
#include "SmartMaskProofActions.inc"
#include "SpmskShelfProofActions.inc"
#include "NativeMaterialProjectionProofActions.inc"
if (harness.is_set("brush-presets-demo") && workspace)
  new bts::BrushPresetDemo(
      workspace,
      viewport,
      harness.value("brush-presets-demo"),
      harness.value("brush-presets-mesh"),
      [&] {
        QJsonObject state{
            {"has_document", bool(document)},
            {"render_frames", viewport->frame_count},
            {"undo_count", int(undo_history.size())},
            {"bo_document_undo_count", int(undo_history.size())},
            {"redo_count", int(redo_history.size())},
            {"document_path", document_path},
            {"open_pending", opening_project || project_opener.pending()},
            {"save_pending",
             bool(active_save) || bool(requested_save) || host.save_project_pending()},
            {"texture_loading", texture_loader && texture_loader->pending()},
            {"composite_pending", host.texture_updates_pending()},
            {"property_pending", bool(uniform_before)},
            {"open_error", workspace->property("projectOpenError").toString()},
            {"save_error", workspace->property("projectSaveError").toString()},
            {"material_error", last_material_error},
            {"shader_failures", QString::fromStdString(host.shader_failures())},
            {"brush_thumbnails_pending", brush_thumbnails && brush_thumbnails->pending()},
            {"bo_brush_thumbnails",
             brush_thumbnails ? brush_thumbnails->diagnostics() : QJsonObject{}}};
        if (document && selected_material >= 0) {
          auto &set = document->set(snapshot.materials.at(selected_material).session_id);
          const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
          state["bo_selected_layer_kind"] = int(layer.kind);
          state["bo_selected_layer"] = layer.id.toString(QUuid::WithoutBraces);
        }
        if (auto *error = workspace->findChild<QLabel *>("workspaceErrorText");
            error && error->isVisible())
          state["workspace_error"] = error->text();
        return state;
      },
      [&] { return brush; },
      assets);
// The --gui-fix-demo state probe is a named lambda, not written inside the call: it holds
// #include and #ifdef lines, and the live-render drivers in native/tests turn
// start_gui_fix_demo into a macro, whose arguments may not contain preprocessor directives.
const auto gui_fix_demo_state = [&] {
  QJsonObject state{
      {"composite_failed_jobs", qint64(bts::composite_failed_jobs)},
      {"composite_split_jobs", qint64(bts::composite_split_jobs)},
      {"render", host.render_state()},
      {"render_ui", bool(render_controller && render_controller->active())},
      {"property_pending", bool(uniform_before && uniform_kind == UniformKind::Property)},
      {"property_dragging", property_dragging},
      {"property_idle_ms", property_idle_timer.remainingTime()},
      {"painting", viewport->painting_},
      {"pressure_overlay", viewport->pressure_overlay_},
      {"input_pressure", viewport->input_pressure_},
      {"pointer_device", viewport->pointer_name_},
      {"tablet_contact", viewport->tablet_contact_},
      {"input_points", qint64(viewport->input_points_)},
      {"spline_dabs", viewport->spline_dabs},
      {"compress_tablet", QCoreApplication::testAttribute(Qt::AA_CompressTabletEvents)},
      {"compress_mouse", QCoreApplication::testAttribute(Qt::AA_CompressHighFrequencyEvents)},
      {"environment", host.environment_state()},
      {"view", viewport->view_state()},
      {"input_frames", qint64(viewport->input_frames_)},
      {"stroke_batches", qint64(viewport->stroke_batches_)},
      {"hover_picks", qint64(viewport->hover_picks_)},
      {"render_frames", viewport->frame_count},
      {"first_dab_diameter", viewport->first_dab_diameter_},
      {"last_dab_diameter", viewport->last_dab_diameter_},
      {"texture_loading", texture_loader && texture_loader->pending()},
      {"composite_pending", host.texture_updates_pending()},
      {"composite_checked", gui_composite_checked},
      {"composite_differences", gui_composite_differences},
      {"padding_calls", qint64(bts::TexturePadding::invocation_count())},
      {"has_document", bool(document)},
      {"undo_count", int(undo_history.size())},
      {"decimal", QString::fromLatin1(std::localeconv()->decimal_point)},
      {"shader_failures", QString::fromStdString(host.shader_failures())}};
  if (qEnvironmentVariableIsSet("BTS_SHADERBALL_DEMO")) {
    state["mesh_triangles"] = qint64(snapshot.uv_triangles.size());
    state["missing_uv_meshes"] = int(snapshot.meshes_without_uvs);
    state["open_pending"] = opening_project || project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    return state;
  }
#include "NativeAdjustmentState.inc"
#include "NativeVectorMappingState.inc"
#include "NativeHeightImageUiState.inc"
#include "NativeMaterialProjectionState.inc"
  if (qEnvironmentVariableIsSet("BTS_BN_SPMSK_UI")) {
    state["open_pending"] = project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    state["material_error"] = last_material_error;
    state["layer_thumbnails_pending"] = workspace->layer_thumbnails_pending();
    state["native_mask_thumbnails_pending"] = smart_mask_thumbnails &&
                                              smart_mask_thumbnails->pending();
    state["resource_importing"] = workspace->property("resourceImporting").toBool();
    int source_polls = 0;
    for (auto *poll : workspace->findChildren<QTimer *>("spsmApplicationPoll"))
      source_polls += poll->isActive();
    state["spsm_application_polls"] = source_polls;
    state["redo_count"] = int(redo_history.size());
    state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
    state["cpu_layer_compositions"] = qint64(bts::local_paint_materializations);
    if (auto *error = workspace->findChild<QLabel *>("workspaceErrorText");
        error && error->isVisible())
      state["workspace_error"] = error->text();
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BN_SMART_MASK")) {
    state["open_pending"] = project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    state["material_error"] = last_material_error;
    state["layer_thumbnails_pending"] = workspace->layer_thumbnails_pending();
    state["native_mask_thumbnails_pending"] = smart_mask_thumbnails &&
                                              smart_mask_thumbnails->pending();
    state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
    state["cpu_layer_compositions"] = qint64(bts::local_paint_materializations);
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BK_FOLDER_MASK")) {
    state["brush"] = QJsonObject{{"diameter", brush.diameter},
                                 {"flow", brush.flow},
                                 {"stroke_opacity", brush.stroke_opacity},
                                 {"mask_value", brush.mask_value},
                                 {"spacing", brush.spacing},
                                 {"hardness", brush.hardness},
                                 {"pressure_size", brush.pressure_size},
                                 {"pressure_flow", brush.pressure_flow},
                                 {"erase", brush.erase},
                                 {"smudge", brush.smudge},
                                 {"clone", brush.clone}};
    state["smart_mask_saving"] = workspace->property("smartMaskSaving").toBool();
    state["redo_count"] = int(redo_history.size());
    state["open_pending"] = project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    state["material_error"] = last_material_error;
    state["material_published"] = last_material_published;
    state["paint_enabled"] = viewport->paint_enabled;
    state["samples"] = workspace->property("bkSamples").toJsonArray();
    state["cycles_mirrors"] = workspace->property("bkCyclesMirrors").toJsonObject();
    state["inspection"] = int(host.inspection_mode());
    state["mask_isolated"] = host.inspection_mode() == bts::InspectionMode::Mask;
    state["layer_thumbnails_pending"] = workspace->layer_thumbnails_pending();
    const auto point = viewport->uv_point({.5, .5});
    state["paint_point"] = QJsonArray{point.x(), point.y()};
    state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
    state["cpu_layer_compositions"] = qint64(bts::local_paint_materializations);
    if (document && selected_material >= 0) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::mask_texture_layer(set, set.selected_layer);
      state["selected_layer"] = set.selected_layer.toString();
      state["has_mask"] = bool(layer.mask);
      state["selected_mask"] = set.selected_mask;
      state["selected_effect"] = set.selected_effect.toString();
      if (layer.mask) {
        bts::EditStateScope metadata;
        state["mask_effects"] = bts::mask_effects_json(layer.mask->effects);
        state["mask_strokes"] = layer.mask->strokes ?
                                    (layer.mask->strokes->loaded ?
                                         int(layer.mask->strokes->operations.size()) :
                                         layer.mask->strokes->declared_operations) :
                                    0;
        const auto *effect = bts::find_mask_effect(layer.mask->effects, set.selected_effect);
        if (effect && effect->kind == bts::MaskEffectKind::Paint)
          state["effect_strokes"] = effect->paint.strokes ?
                                        (effect->paint.strokes->loaded ?
                                             int(effect->paint.strokes->operations.size()) :
                                             effect->paint.strokes->declared_operations) :
                                        0;
      }
    }
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->edit_state();
    }
    QJsonArray resources;
    for (const auto &asset : assets.project_assets())
      resources.append(bts::asset_metadata(asset));
    state["project_resources"] = resources;
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_PAINTER_WORKFLOW_DEMO")) {
    state["redo_count"] = int(redo_history.size());
    state["open_pending"] = project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    state["paint_enabled"] = viewport->paint_enabled;
    const auto paint_point = viewport->uv_point({.5, .5});
    state["painter_paint_point"] = QJsonArray{paint_point.x(), paint_point.y()};
    if (document && selected_material >= 0) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto selected = std::find_if(
          set.layers.begin(), set.layers.end(), [&](const auto &candidate) {
            return candidate.id == set.selected_layer;
          });
      if (selected == set.layers.end())
        throw std::logic_error("Painter actor selected layer missing");
      const auto &layer = *selected;
      state["selected_mask"] = set.selected_mask;
      state["selected_effect"] = set.selected_effect.toString();
      if (workspace->property("painterGpuReadback").toBool() && !host.texture_updates_pending())
        if (const auto pixel = host.diagnostic_gpu_pixel(set.material_session_uid, 15, 64, 64)) {
          QJsonArray values;
          for (float value : *pixel)
            values.append(value);
          state["painter_gpu_mask_probe"] = values;
        }
      if (layer.mask) {
        bts::EditStateScope metadata;
        state["painter_mask_effects"] = bts::mask_effects_json(layer.mask->effects);
        if (const auto *effect = bts::find_mask_effect(layer.mask->effects, set.selected_effect)) {
          state["selected_mask_effect"] = bts::mask_effects_json(std::vector{*effect}).at(0);
          state["selected_paint_stroke_operations"] =
              effect->kind == bts::MaskEffectKind::Paint && effect->paint.strokes ?
                  (effect->paint.strokes->loaded ? int(effect->paint.strokes->operations.size()) :
                                                   effect->paint.strokes->declared_operations) :
                  0;
          state["selected_paint_has_strokes"] = state["selected_paint_stroke_operations"].toInt() >
                                                0;
        }
      }
    }
    state["layer_thumbnails_pending"] = workspace->layer_thumbnails_pending();
    state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
    state["cpu_layer_compositions"] = qint64(bts::local_paint_materializations);
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->edit_state();
      state["bake_configuration"] = document->bake_configuration();
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BAKE_MULTISET_HISTORY_DEMO")) {
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
      state["document_revision"] = qint64(document_revision);
      state["redo_count"] = int(redo_history.size());
      if (skew)
        state["skew"] = skew->state();
      state["selected_uid"] = selected_material >= 0 ?
                                  QJsonValue(
                                      qint64(snapshot.materials[selected_material].session_id)) :
                                  QJsonValue();
      state["source_obj_hash"] = QString::fromLatin1(
          QCryptographicHash::hash(bts::original_snapshot_obj(host.scene_snapshot()),
                                   QCryptographicHash::Sha256)
              .toHex());
      state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
      state["map_history_seeded"] = workspace->property("mapHistorySeeded").toBool();
      if (workspace->property("mapHistoryCapture").toBool() && !host.texture_updates_pending()) {
        const std::array<std::array<int, 2>, 3> coordinates{{{32, 32}, {64, 64}, {96, 96}}};
        QJsonArray sets;
        for (const auto &set : document->texture_sets())
          if (!set.disabled) {
            QJsonObject row{{"uid", qint64(set.material_session_uid)},
                            {"id", set.id.toString(QUuid::WithoutBraces)},
                            {"precision", bts::channel_precision_names[int(set.precision[0])]}};
            QJsonArray probes;
            const auto &plane = set.mesh_maps.at(3)->tiles.at(1001);
            if (!plane.pixels)
              throw std::runtime_error("Map history probe lacks retained AO pixels");
            const auto &pixels = *plane.pixels;
            const auto gpu = host.diagnostic_gpu_pixels(
                set.material_session_uid, 0, coordinates, 1001);
            if (gpu.size() != coordinates.size())
              throw std::runtime_error("Map history GPU packet shape differs");
            for (std::size_t i = 0; i < coordinates.size(); ++i) {
              const auto &xy = coordinates[i];
              const int x = std::clamp(
                            int((float(xy[0]) + .5f) / float(set.width(1001)) * pixels.resolution),
                            0,
                            pixels.resolution - 1),
                        y = std::clamp(
                            int((float(xy[1]) + .5f) / float(set.rows(1001)) * pixels.height),
                            0,
                            pixels.height - 1);
              const auto offset = (std::size_t(y) * pixels.resolution + x) * 4;
              QJsonArray raw, actual;
              for (int c = 0; c < 4; ++c) {
                raw.append(pixels.rgba[offset + c]);
                actual.append(gpu[i][c]);
              }
              probes.append(QJsonObject{{"xy", QJsonArray{xy[0], xy[1]}},
                                        {"map_xy", QJsonArray{x, y}},
                                        {"raw", raw},
                                        {"gpu", actual}});
            }
            row["pixel_hash"] = QString::fromStdString(pixels.hash);
            row["descriptor_hash"] = QString::fromStdString(plane.hash);
            row["map_size"] = QJsonArray{pixels.resolution, pixels.height};
            row["probes"] = probes;
            sets.append(row);
          }
        state["map_history_sets"] = sets;
        state["residency"] = host.project_profile()["texture_residency"];
      }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_UPSTREAM")) {
    state["native_mapping_queues"] = host.diagnostic_native_source_queues();
    state["document_revision"] = qint64(document_revision);
    state["redo_count"] = int(redo_history.size());
    state["native_mapping_error"] = last_material_error;
    const bool active = bool(uniform_before && uniform_kind == UniformKind::Property);
    QJsonArray pending_channels;
    if (active)
      for (bool on : property_channels)
        pending_channels.append(on);
    state["native_mapping_property"] = QJsonObject{
        {"active", active},
        {"pending_edit", bool(pending_property_edit)},
        {"previewed", uniform_previewed},
        {"dragging", property_dragging},
        {"frame_timer", property_frame_timer.isActive()},
        {"idle_timer", property_idle_timer.isActive()},
        {"channels", pending_channels}};
    QJsonArray resources;
    for (const auto &asset : assets.assets())
      resources.append(bts::asset_metadata(asset));
    state["native_mapping_catalog"] = resources;
  }
  if (qEnvironmentVariableIsSet("BTS_NATIVE_IMAGE_UI_DEMO")) {
    state["native_image_queues"] = host.diagnostic_native_source_queues();
    state["dirty"] = dirty;
    state["document_revision"] = qint64(document_revision);
    state["redo_count"] = int(redo_history.size());
    state["native_image_loading"] = workspace->property("nativeImageLoading").toBool();
    state["native_image_inspecting"] =
        workspace->native_image_panel()->property("nativeImageInspecting").toBool();
    state["native_image_error"] = workspace->property("nativeImageError").toString();
    state["open_pending"] = project_opener.pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["save_error"] = workspace->property("projectSaveError").toString();
    QJsonArray resources;
    for (const auto &asset : assets.assets())
      resources.append(bts::asset_metadata(asset));
    state["catalog"] = resources;
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->edit_state();
      if (selected_material >= 0 && workspace->property("nativeImageSample").toBool()) {
        QJsonArray probes;
        const auto uid = snapshot.materials[selected_material].session_id;
        for (int c : {0, 1, 2, 4}) {
          QJsonArray rgba;
          if (const auto pixel = host.diagnostic_gpu_pixel(uid, c, 64, 64))
            for (float value : *pixel)
              rgba.append(value);
          probes.append(rgba);
        }
        state["native_image_samples"] = probes;
        QJsonArray consumers;
        for (const auto &consumer : document->texture_sets()) {
          QJsonArray pixel;
          if (const auto rgba = host.diagnostic_gpu_pixel(
                  consumer.material_session_uid, 0, 64, 64))
            for (float value : *rgba)
              pixel.append(value);
          consumers.append(QJsonObject{{"set", consumer.id.toString()}, {"pixel", pixel}});
        }
        state["native_image_dependents"] = consumers;
      }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_SPSM_DEMO")) {
    state["spsm_oracle_scope"] = workspace->property("spsmOracleScope").toJsonObject();
    state["open_pending"] = project_opener.pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["save_error"] = workspace->property("projectSaveError").toString();
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->edit_state();
      if (selected_material >= 0) {
        const auto &set = document->set(snapshot.materials[selected_material].session_id);
        const auto comparison = bts::spsm_comparison_state(
            set, [&](int channel, int x, int y, int tile) {
              return host.diagnostic_gpu_pixel(set.material_session_uid, channel, x, y, tile);
            });
        for (auto item = comparison.begin(); item != comparison.end(); ++item)
          state[item.key()] = item.value();
      }
      QJsonArray resources;
      for (const auto &asset : assets.project_assets()) {
        auto data = bts::asset_metadata(asset);
        data["bytes"] = qint64(asset.bytes.size());
        data["original_spsm"] = bts::is_spsm_asset(asset);
        resources.append(data);
      }
      state["resources"] = resources;
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_CHECKBOX_DEMO")) {
    state["open_pending"] = opening_project || project_opener.pending();
    state["save_pending"] = bool(active_save) || bool(requested_save) ||
                            host.save_project_pending();
    state["open_error"] = workspace->property("projectOpenError").toString();
    state["save_error"] = workspace->property("projectSaveError").toString();
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_TEXTURE_SET_SELECTION_DEMO")) {
    state["selected_index"] = selected_material;
    state["native_selected_uid"] = qint64(host.selected_material().value_or(0));
    state["paint_target"] = viewport->paint_target;
    QJsonArray sets;
    if (document)
      for (const auto &set : document->texture_sets())
        sets.append(QJsonObject{{"layer_id", set.selected_layer.toString()},
                                {"set_id", set.id.toString()}});
    state["sets"] = sets;
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_HEIGHT_NEUTRAL_DEMO")) {
    if (document && selected_material >= 0) {
      bts::EditStateScope metadata;
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      state["document"] = document->to_json();
      state["displacement"] = bts::displacement_json(set.displacement);
      QJsonObject values;
      for (int channel : {4, 3, 16})
        if (auto sample = host.diagnostic_gpu_pixel(set.material_session_uid, channel, 64, 64)) {
          auto pixel = *sample;
          if (channel == 4) {
            const float base = set.channels[4] ? set.bump_display(set.base_values.bump) : .5f;
            for (int c = 0; c < 3; ++c)
              pixel[c] += base * (1 - pixel[3]);
            values["bump"] = pixel[0];
          }
          else if (channel == 16) {
            values["displacement"] = pixel[0] + set.displacement.midlevel * (1 - pixel[3]);
          }
          else
            values["normal"] = QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]};
        }
      state["height_values"] = values;
    }
    return state;
  }
  if (workspace->property("guiProbeOnly").toBool() ||
      (qEnvironmentVariableIsSet("BTS_AA_PERFORMANCE") &&
       workspace->property("aaProbeOnly").toBool()) ||
      (qEnvironmentVariableIsSet("BTS_RESOURCE_MONITOR_DEMO") &&
       workspace->property("resourceProbeOnly").toBool()))
    return state;
  if (qEnvironmentVariableIsSet("BTS_POLYGON_BUCKET_DEMO")) {
    bts::PaintMetadataScope metadata;
    state["native_object_count"] = int(snapshot.mesh_objects);
    state["polygon_active"] = viewport->polygon_active;
    state["polygon_mode"] = int(viewport->polygon_mode);
    state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
    QJsonArray points;
    for (const auto p : {viewport->scene_point({-.75f, 0, 0}),
                         viewport->uv_point({.25, .25}),
                         viewport->uv_point({1.25, .25})})
      points.append(QJsonArray{p.x(), p.y()});
    state["polygon_points"] = points;
    if (document && selected_material >= 0) {
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      state["document"] = document->edit_state();
      state["metallic_pages"] = qint64(layer.pixels.page_count(1));
      state["displacement_default_off"] = !set.displacement.channel && !layer.displacement.enabled;
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_STROKE_TARGET_PROFILE")) {
    state["profile_point"] = QJsonArray::fromVariantList(
        workspace->property("strokeProfilePoint").toList());
    state["cpu_materializations"] = qint64(bts::local_paint_materializations);
    state["free_vram"] = qint64(bts::texture_free_vram.load());
    if (document && selected_material >= 0) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      state["profile_has_mask"] = bool(layer.mask);
      state["profile_mask"] = set.selected_mask;
      QJsonArray tiles;
      for (int tile : set.tiles)
        tiles.append(tile);
      state["profile_tiles"] = tiles;
      state["profile_depth"] = layer.depth;
      state["profile_smudge"] = brush.smudge;
      state["profile_filter_count"] = int(
          std::count_if(layer.effects.begin(), layer.effects.end(), [](const auto &e) {
            return e.kind == bts::ContentEffectKind::Filter && e.enabled && e.filter.resource > 0;
          }));
      state["profile_mask_filter_count"] =
          layer.mask ? int(std::count_if(layer.mask->effects.begin(),
                                         layer.mask->effects.end(),
                                         [](const auto &e) {
                                           return e.kind == bts::MaskEffectKind::Filter &&
                                                  e.enabled && e.filter.resource > 0;
                                         })) :
                       0;
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_TEMPORARY_INK_DEMO")) {
    QJsonArray primary, secondary;
    for (float value : brush.values.base_color)
      primary.append(value);
    for (float value : viewport->secondary_color)
      secondary.append(value);
    state["primary"] = primary;
    state["secondary"] = secondary;
    state["temporary_active"] = viewport->secondary_active_;
    state["first_dab_diameter"] = viewport->first_dab_diameter_;
    state["last_dab_diameter"] = viewport->last_dab_diameter_;
    state["mask_ink"] = brush.mask_value;
    if (document && selected_material >= 0) {
      bts::EditStateScope metadata;
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(document->set(set.material_session_uid),
                                                      set.selected_layer);
      QJsonArray points;
      for (double u : {.2, .4, .6, .8}) {
        const auto point = viewport->uv_point({u, .5});
        points.append(QJsonArray{point.x(), point.y()});
      }
      state["points"] = points;
      state["has_mask"] = bool(layer.mask);
      QJsonArray base;
      for (float value : set.base_values.base_color)
        base.append(value);
      state["base_color"] = base;
      if (workspace->property("temporarySample").toBool()) {
        QJsonArray samples, masks;
        for (double u : {.25, .5, .75}) {
          const int x = int(u * set.width()), y = set.rows() / 2;
          QJsonArray pixel;
          for (float value : host.sample_texture(set.material_session_uid, 0, x, y))
            pixel.append(value);
          samples.append(pixel);
          if (layer.mask)
            masks.append(bts::TextureDocument::mask_value(set, layer.id, x, y));
        }
        state["samples"] = samples;
        state["masks"] = masks;
      }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_LAYER_DROP_DEMO")) {
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_RENAME_DEMO")) {
    if (document) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
      QJsonArray names;
      for (const auto &material : host.scene_snapshot().materials)
        names.append(QString::fromStdString(material.name));
      state["native_material_names"] = names;
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BAKING_LAYOUT_DEMO")) {
    if (skew)
      state["skew"] = skew->state();
    if (document && selected_material >= 0) {
      state["bake_configuration"] = document->bake_configuration();
      QJsonArray roles;
      for (int role = 0; role < 7; ++role)
        if (document->set(snapshot.materials[selected_material].session_id).mesh_maps[role])
          roles.append(role);
      state["mesh_map_roles"] = roles;
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BAKED_MAPS_DEMO")) {
    if (document && selected_material >= 0) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      QJsonArray maps;
      const bool sample = workspace->property("bakedSample").toBool();
      for (int role = 0; role < 7; ++role)
        if (set.mesh_maps[role]) {
          QJsonArray tiles;
          for (const auto &[tile, plane] : set.mesh_maps[role]->tiles) {
            QJsonObject data{{"tile", tile},
                             {"hash", QString::fromStdString(plane.hash)},
                             {"resolution", plane.resolution}};
            if (sample && plane.pixels) {
              const auto &pixels = *plane.pixels;
              int valid = 0;
              for (std::size_t i = 3; i < pixels.rgba.size(); i += 4)
                valid += pixels.rgba[i] > 0;
              data["covered_texels"] = valid;
              QJsonArray probes;
              for (int y = 4; y < pixels.height && probes.size() < 3; y += 8)
                for (int x = 4; x < pixels.resolution && probes.size() < 3; x += 8) {
                  const auto offset = (std::size_t(y) * pixels.resolution + x) * 4;
                  if (pixels.rgba[offset + 3] < .5)
                    continue;
                  const double u = (tile - 1001) % 10 + (x + .5) / pixels.resolution,
                               v = (tile - 1001) / 10 + (y + .5) / pixels.height;
                  bool on_mesh = false;
                  for (const auto &triangle : snapshot.uv_triangles) {
                    if (snapshot.materials[triangle.material].session_id !=
                        set.material_session_uid)
                      continue;
                    const auto &a = triangle.corners[0], &b = triangle.corners[1],
                               &c = triangle.corners[2];
                    const double determinant = (b[0] - a[0]) * (c[1] - a[1]) -
                                               (b[1] - a[1]) * (c[0] - a[0]);
                    if (std::abs(determinant) < 1e-12)
                      continue;
                    const double s = ((u - a[0]) * (c[1] - a[1]) - (v - a[1]) * (c[0] - a[0])) /
                                     determinant,
                                 t = ((b[0] - a[0]) * (v - a[1]) - (b[1] - a[1]) * (u - a[0])) /
                                     determinant;
                    if (s > 1e-5 && t > 1e-5 && s + t < 1 - 1e-5) {
                      on_mesh = true;
                      break;
                    }
                  }
                  if (!on_mesh)
                    continue;
                  QJsonObject probe{
                      {"x", x}, {"y", y}, {"on_mesh", true}, {"baked", pixels.rgba[offset]}};
                  if (role == workspace->property("bakedSourceRole").toInt() && layer.mask) {
                    probe["cpu_mask"] = bts::TextureDocument::mask_value(
                        set, layer.id, x, y, tile);
                    if (auto p = host.diagnostic_gpu_pixel(
                            set.material_session_uid, 0, x, y, tile))
                      probe["gpu_coverage"] = (*p)[3];
                  }
                  probes.append(probe);
                }
              data["probes"] = probes;
            }
            tiles.append(data);
          }
          maps.append(QJsonObject{{"role", role}, {"tiles", tiles}});
        }
      state["maps"] = maps;
      state["source_role"] = workspace->property("bakedSourceRole").toInt();
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (effect.kind == bts::MaskEffectKind::Fill) {
            state["mask_mode"] = effect.mapping.projection.mode;
            if (const auto &texture = effect.mapping.projection.texture) {
              state["mask_raw"] = texture->raw;
              QJsonArray identities;
              if (texture->tiles)
                for (const auto &[tile, member] : *texture->tiles)
                  identities.append(QString::fromStdString(member->storage_identity()));
              state["mask_storage_keys"] = identities;
            }
          }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_LEVELS_EDITOR_DEMO")) {
    if (document && selected_material >= 0) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      state["effects"] = bts::effects_json(layer.effects);
      auto *histogram = workspace->findChild<QWidget *>("levelsHistogram");
      state["histogram_ready"] = histogram->property("gpuHistogramReady").toBool();
      state["histogram_bins"] = QJsonArray::fromVariantList(
          histogram->property("gpuHistogramBins").toList());
      if (workspace->property("levelsSample").toBool()) {
        QJsonArray rgba;
        if (auto pixel = host.diagnostic_gpu_pixel(
                set.material_session_uid, 2, set.width() / 2, set.rows() / 2))
          for (auto value : *pixel)
            rgba.append(value);
        state["roughness_gpu"] = rgba;
      }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_LAYER_CHANNEL_DEMO")) {
    if (document && selected_material >= 0) {
      bts::EditStateScope metadata;
      state["document"] = document->to_json();
      auto &set = document->set(snapshot.materials[selected_material].session_id);
      const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
      QJsonArray flags, pages, points, samples;
      for (int c : {0, 1, 2, 4, 14}) {
        flags.append(bts::layer_channel_enabled(layer, c));
        pages.append(qint64(c == 14 ? layer.displacement.pixels.page_count(2) :
                                      layer.pixels.page_count(c)));
      }
      for (int tile : set.tiles) {
        const auto p = viewport->uv_point(
            {double((tile - 1001) % 10) + .5, double((tile - 1001) / 10) + .5});
        points.append(QJsonArray{p.x(), p.y()});
        if (workspace->property("channelSample").toBool()) {
          QJsonArray rgba;
          if (auto pixel = host.diagnostic_gpu_pixel(
                  set.material_session_uid, 0, set.width(tile) / 2, set.rows(tile) / 2, tile))
            for (auto x : *pixel)
              rgba.append(x);
          samples.append(rgba);
        }
      }
      state["fill_rough_row_hidden"] =
          workspace->findChild<QWidget *>("fillChannelRow2")->isHidden();
      state["channel_flags"] = flags;
      state["channel_pages"] = pages;
      state["channel_points"] = points;
      state["channel_samples"] = samples;
      state["disp_button"] = workspace->findChild<QToolButton *>("brushDisplacement")->isChecked();
      state["disp_settings"] =
          workspace->findChild<QCheckBox *>("displacementChannel")->isChecked();
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BLEND_MATERIAL_DEMO")) {
    if (document)
      state["document"] = document->to_json();
    QJsonArray materials;
    for (const auto &asset : assets.assets())
      if (asset.scope != bts::AssetScope::Builtin && asset.filename.endsWith(".btsuniform")) {
        auto metadata = bts::asset_metadata(asset);
        metadata["scope"] = bts::asset_scope_name(asset.scope);
        metadata["descriptor"] = QJsonDocument::fromJson(asset.bytes).object();
        materials.append(metadata);
      }
    state["blend_materials"] = materials;
    if (document && selected_material >= 0 && !state["composite_pending"].toBool()) {
      const auto &set = document->set(snapshot.materials[selected_material].session_id);
      QJsonArray samples;
      for (int channel : {0, 1, 2}) {
        QJsonArray rgba;
        if (auto pixel = host.diagnostic_gpu_pixel(
                set.material_session_uid, channel, 64, 64, 1001))
          for (auto x : *pixel)
            rgba.append(x);
        samples.append(rgba);
      }
      state["material_gpu_samples"] = samples;
      if (qEnvironmentVariableIsSet("BTS_BLEND_IMAGES_DEMO")) {
        QJsonArray tiles;
        for (int tile : set.tiles) {
          QJsonArray channels;
          for (int channel : {0, 1, 2, 4}) {
            QJsonArray pixel;
            if (auto value = host.diagnostic_gpu_pixel(
                    set.material_session_uid, channel, 64, 64, tile))
              for (float v : *value)
                pixel.append(v);
            channels.append(pixel);
          }
          tiles.append(QJsonObject{{"tile", tile}, {"samples", channels}});
        }
        state["image_tiles"] = tiles;
        QJsonArray resources;
        bool shared = false;
        for (const auto &asset : assets.assets()) {
          if (asset.scope == bts::AssetScope::Project && asset.usages.contains("texture"))
            resources.append(bts::asset_metadata(asset));
          if (asset.material && asset.fill_preset) {
            const auto &a = fill_channel_texture(*asset.fill_preset, 1),
                       &b = fill_channel_texture(*asset.fill_preset, 2);
            if (a && b)
              shared |= a->pixels && a->pixels == b->pixels;
          }
        }
        state["image_resources"] = resources;
        state["shared_scalar_pixels"] = shared;
        state["cpu_materializations"] = qint64(bts::PaintPixels::recipe_materializations());
      }
    }
    return state;
  }
  if (qEnvironmentVariableIsSet("BTS_BLEND_IMPORT_DEMO")) {
    if (document) {
      state["document"] = document->to_json();
      state["geometry"] = host.displacement_preview_state();
      state["source_obj_hash"] = QString::fromLatin1(
          QCryptographicHash::hash(bts::original_snapshot_obj(host.scene_snapshot()),
                                   QCryptographicHash::Sha256)
              .toHex());
    }
    return state;
  }
#ifdef BTS_WITH_MATERIALX
  if (qEnvironmentVariableIsSet("BTS_OPENPBR_IMAGES_DEMO")) {
    state["texture_loading"] = (texture_loader && texture_loader->pending()) ||
                               (material_importer && material_importer->pending());
    if (document)
      state["document"] = document->to_json();
    state["shader_nodes"] = host.shader_parameter_state();
    return state;
  }
#endif
  if (document && selected_material >= 0) {
    auto &set = document->set(snapshot.materials[selected_material].session_id);
    const auto &layer = bts::TextureDocument::layer(set, set.selected_layer);
    state["layers"] = int(set.layers.size());
    if (qEnvironmentVariableIsSet("BTS_SKEW_DEMO")) {
      state["document"] = qEnvironmentVariableIsSet("BTS_FLATTEN_DEMO") ? document->edit_state() :
                                                                          document->to_json();
      if (qEnvironmentVariableIsSet("BTS_FLATTEN_DEMO"))
        state["flatten_material_probes"] =
            workspace->property("flattenMaterialProbes").toJsonArray();
      state["source_obj_hash"] = QString::fromLatin1(
          QCryptographicHash::hash(bts::original_snapshot_obj(host.scene_snapshot()),
                                   QCryptographicHash::Sha256)
              .toHex());
      const auto uv = viewport->uv_point({.65, .5}), poly = viewport->uv_point({.35, .7}),
                 point = viewport->scene_point({0, 0, 0});
      state["skew_uv_point"] = QJsonArray{uv.x(), uv.y()};
      state["skew_polygon_point"] = QJsonArray{poly.x(), poly.y()};
      state["skew_scene_point"] = QJsonArray{point.x(), point.y()};
      state["skew_reference"] = workspace->property("skewReference").toJsonObject();
    }
    if (skew) {
      state["skew"] = skew->state();
      state["skew_metadata"] = bts::skew_field_json(set.skew);
      state["skew_hash"] = QString::number(set.skew.pixels.hash(2), 16);
      if (set.mesh_maps[0] && set.mesh_maps[0]->tiles.contains(1001))
        state["skew_normal_hash"] = QString::fromStdString(set.mesh_maps[0]->tiles.at(1001).hash);
    }
    if (bts::performance::enabled() && !qEnvironmentVariableIsSet("BTS_PROPERTY_PERFORMANCE") &&
        !qEnvironmentVariableIsSet("BTS_RESOURCE_MONITOR_DEMO") &&
        !qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO") &&
        !qEnvironmentVariableIsSet("BTS_AB_DISPLACEMENT"))
    {
      state["roughness"] = layer.values.roughness;
      state["selected_layer"] = layer.id.toString();
      state["color_probe"] = host.sample_texture(
          set.material_session_uid, 0, set.width() / 2, set.rows() / 2)[0];
      return state;
    }
    state["hash"] = QString::number(layer.pixels.hash(0), 16);
    state["bump_hash"] = QString::number(layer.pixels.hash(4), 16);

    if (qEnvironmentVariableIsSet("BTS_PLANAR_DEMO")) {
      state["document"] = document->to_json();
      state["native_hash"] = QString::number(host.texture_hash(set.material_session_uid, 0), 16);
      QJsonArray other;
      const auto scene = host.scene_snapshot();
      for (const auto &target : document->texture_sets())
        if (target.id != set.id) {
          const auto found = std::find_if(
              scene.materials.begin(), scene.materials.end(), [&](const auto &m) {
                return m.session_id == target.material_session_uid;
              });
          QJsonObject item{{"name", QString::fromStdString(target.name)},
                           {"has_image", host.has_texture_pixels(target.material_session_uid)}};
          if (found != scene.materials.end())
            item["native_values"] = QJsonArray{found->values.base_color[0],
                                               found->values.base_color[1],
                                               found->values.base_color[2],
                                               found->values.metallic,
                                               found->values.roughness};
          if (host.has_texture_pixels(target.material_session_uid))
            item["hash"] = QString::number(host.texture_hash(target.material_session_uid, 0), 16);
          other.append(item);
        }
      state["other_sets"] = other;
      if (layer.kind == bts::LayerKind::Fill && host.has_texture_pixels(set.material_session_uid))
      {
        const auto pixels = host.texture_pixels(set.material_session_uid, 0);
        const auto coverage = host.texture_coverage(set.material_session_uid);
        int visible = 0, texels = 0;
        for (std::size_t i = 0; i < coverage.size() && i * 4 + 3 < pixels.size(); ++i)
          if (coverage[i]) {
            ++texels;
            visible += pixels[i * 4 + 3] > .01;
          }
        state["visible_texels"] = visible;
        state["uv_texels"] = texels;
      }
    }
    if (qEnvironmentVariableIsSet("BTS_DISPLAY_LENS_DEMO")) {
      auto world = workspace->property("lensProbeWorld").toJsonArray();
      if (world.isEmpty()) {
        for (const auto &triangle : snapshot.uv_triangles) {
          if (triangle.material != std::size_t(selected_material))
            continue;
          QVector3D center;
          for (const auto &p : triangle.positions)
            center += QVector3D(p[0], p[1], p[2]) / 3;
          const auto screen = viewport->scene_point(center);
          const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
          if (!pane.adjusted(60, 60, -60, -60).contains(screen) ||
              QLineF(screen, pane.center()).length() < 30)
            continue;
          const auto initial = viewport->probe_surface(screen);
          if (initial) {
            const auto position = viewport->surface_position_;
            world = {position.x(), position.y(), position.z()};
            workspace->setProperty("lensProbeWorld", world);
            break;
          }
        }
      }
      if (world.size() == 3) {
        const auto point = viewport->scene_point(
            {float(world[0].toDouble()), float(world[1].toDouble()), float(world[2].toDouble())});
        state["lens_probe_screen"] = QJsonArray{point.x(), point.y()};
        if (const auto hit = viewport->probe_surface(point))
          state["lens_probe_uv"] = QJsonArray{hit->center[0], hit->center[1]};
      }
      const auto pane = viewport->pane_rect(bts::ViewKind::Scene);
      state["lens_corner_hit"] = bool(viewport->probe_surface(pane.topLeft() + QPointF(1, 1)));
    }
    if (qEnvironmentVariableIsSet("BTS_CLIPPING_DEMO")) {
      state["cameras"] = viewport->scene_camera_state();
      state["geometry"] = host.displacement_preview_state();
      state["document"] = document->to_json();
      const auto pick = viewport->pick_path_point(viewport->scene_point({0, 0, 0}));
      if (pick)
        state["picked_uv"] = QJsonArray{pick->uv[0], pick->uv[1]};
    }
    if (qEnvironmentVariableIsSet("BTS_IMAGE_SEQUENCE_DEMO") ||
        qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO"))
    {
      QJsonArray tiles, resources;
      for (int tile : set.tiles) {
        QJsonObject item{{"tile", tile}};
        if (host.has_texture_pixels(set.material_session_uid)) {
          if (!qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO"))
            item["hash"] = QString::number(host.texture_hash(set.material_session_uid, 0, tile),
                                           16);
          QJsonArray color, roughness;
          const auto a = host.sample_texture(
                         set.material_session_uid,
                         0,
                         set.width(tile) /
                             (qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO") ? 4 : 2),
                         set.rows(tile) / 2,
                         tile),
                     b = host.sample_texture(
                         set.material_session_uid,
                         2,
                         set.width(tile) /
                             (qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO") ? 4 : 2),
                         set.rows(tile) / 2,
                         tile);
          for (float value : a)
            color.append(value);
          for (float value : b)
            roughness.append(value);
          item["color"] = color;
          item["roughness"] = roughness;
        }
        tiles.append(item);
      }
      for (const auto &asset : assets.project_assets()) {
        auto info = bts::asset_metadata(asset);
        info["sequence_count"] = bts::is_image_sequence(asset.bytes) ?
                                     int(bts::read_image_sequence(asset.bytes).size()) :
                                     0;
        resources.append(info);
      }
      state["sequence_tiles"] = tiles;
      state["sequence_resources"] = resources;
    }
    if (qEnvironmentVariableIsSet("BTS_WARP_DEMO")) {
      const auto context = viewport->warp_view_context();
      QJsonArray points, selection;
      const auto grid = bts::fill_warp_grid(layer.fill);
      for (const auto &node : grid->nodes) {
        const auto world = (context.matrix *
                            QVector4D(node.position[0], node.position[1], node.position[2], 1))
                               .toVector3DAffine();
        const auto screen = context.screen({world.x(), world.y(), world.z()});
        points.append(QJsonArray{screen.x(), screen.y()});
      }
      for (int id : viewport->warp_editor.selection())
        selection.append(id);
      QJsonArray axes;
      for (const auto &point : viewport->warp_editor.axis_positions(layer.fill, context))
        axes.append(QJsonArray{point.x(), point.y()});
      state["warp_axes"] = axes;
      state["warp_screen"] = points;
      state["warp_selection"] = selection;
    }
    if (qEnvironmentVariableIsSet("BTS_RIBBON_DEMO")) {
      state["document"] = document->to_json();
      QJsonArray paths;
      if (layer.strokes && layer.strokes->loaded)
        for (const auto &op : layer.strokes->operations)
          if (op->path)
            paths.append(bts::surface_path_json(*op->path));
      state["paths"] = paths;
      state["native_hash"] = QString::number(host.texture_hash(set.material_session_uid, 0), 16);
      state["selected_vertex"] = viewport->selected_vertex;
    }
    if (qEnvironmentVariableIsSet("BTS_FILLED_PATH_DEMO")) {
      QJsonArray hashes;
      for (int c = 0; c < 5; ++c)
        hashes.append(QString::number(layer.pixels.hash(c), 16));
      state["channel_hashes"] = hashes;
      QJsonArray probe;
      const int x = int(set.texture_resolution * .3), y = int(set.rows() * .5);
      for (int c = 0; c < 5; ++c) {
        const auto p = layer.pixels.sample(c, x, y);
        probe.append(QJsonArray{p[0], p[1], p[2], p[3]});
      }
      state["ink_probe"] = probe;
      const auto color = host.sample_texture(set.material_session_uid, 0, x, y);
      state["fill_native_color"] = QJsonArray{color[0], color[1], color[2], color[3]};
    }

#ifdef BTS_WITH_MATERIALX
    state["texture_loading"] = (texture_loader && texture_loader->pending()) ||
                               (material_importer && material_importer->pending());
#endif
    if (qEnvironmentVariableIsSet("BTS_SHADER_DEMO")) {
      state["document"] = document->to_json();
      state["shader_nodes"] = host.shader_parameter_state();
    }
    if (qEnvironmentVariableIsSet("BTS_INSPECTION_DEMO")) {
      state["document"] = document->to_json();
      state["inspection"] = QJsonObject{
          {"channel", int(host.inspection_mode())},
          {"map", viewport->mesh_map_active},
          {"label",
           viewport->mesh_map_active >= 0 ?
               QString::fromLatin1(bts::mesh_map_names[viewport->mesh_map_active]) :
               viewport->inspection_label(host.inspection_mode())}};
      state["layer_context"] =
          workspace->findChild<QComboBox *>("layerChannelContext")->currentData().toInt();
      state["selected_set"] = set.id.toString();
    }
    if (qEnvironmentVariableIsSet("BTS_RESOURCE_MONITOR_DEMO")) {
      QJsonArray catalog;
      for (const auto &asset : assets.assets()) {
        auto info = bts::asset_metadata(asset);
        info["scope"] = bts::asset_scope_name(asset.scope);
        catalog.append(info);
      }
      state["resource_catalog"] = catalog;
    }
    if (qEnvironmentVariableIsSet("BTS_AB_DISPLACEMENT")) {
      state["document"] = document->to_json();
      state["geometry"] = host.displacement_preview_state();
      state["residency"] = host.project_profile()["texture_residency"];
      state["midlevel"] = set.displacement.midlevel;
      state["strength"] = set.displacement.strength;
      QJsonArray samples;
      const auto &texture = bts::fill_channel_texture(layer.fill, 14);
      if (texture) {
        state["input_space"] = QString::fromStdString(texture->input_space);
        state["range"] = QJsonArray{texture->minimum, texture->maximum};
        for (int tile : set.tiles) {
          const auto resident = host.diagnostic_gpu_pixel(
              set.material_session_uid, 16, set.width(tile) / 4, set.rows(tile) / 4, tile);
          const auto pixel = resident.value_or(std::array<float, 4>{});
          const auto expected = bts::TextureDocument::composite_displacement(
              set, set.width(tile) / 4, set.rows(tile) / 4, tile);
          samples.append(
              QJsonObject{{"tile", tile},
                          {"gpu_resident", bool(resident)},
                          {"native", QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]}},
                          {"cpu", QJsonArray{expected[0], expected[1]}}});
        }
      }
      state["ab_samples"] = samples;
    }
    if (qEnvironmentVariableIsSet("BTS_FILL_CHANNELS_DEMO")) {
      state["document"] = document->to_json();
      state["geometry"] = host.displacement_preview_state();
      QJsonArray channels;
      for (int c = 0; c < 15; ++c) {
        const auto &texture = bts::fill_channel_texture(layer.fill, c);
        QJsonObject data{{"channel", c},
                         {"name", texture ? QString::fromStdString(texture->name) : QString{}},
                         {"raw", texture && texture->raw},
                         {"resolved", texture && bool(texture->pixels)}};
        if (texture && texture->pixels) {
          const auto pixel = texture->pixels->front();
          data["source_first"] = QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]};
        }
        const int image = c == 14 ? 16 : c >= 6 ? c + 1 : c;
        const bool allocated = c < 5 || (c == 5 && set.ao_channel) ||
                               (c == 14 && set.displacement.allocated) ||
                               (c >= 6 && c < 14 && set.users[c - 6].present);
        if (allocated && host.has_texture_pixels(set.material_session_uid)) {
          data["hash"] = QString::number(host.texture_hash(set.material_session_uid, image), 16);
          QJsonArray probes;
          for (double u : {.25, .75}) {
            const auto pixel = host.sample_texture(
                set.material_session_uid, image, int(u * set.width()), int(.25 * set.rows()));
            probes.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
          }
          data["probes"] = probes;
        }
        channels.append(data);
      }
      state["fill_channels"] = channels;
    }
    if (qEnvironmentVariableIsSet("BTS_COLOR_SPACE_DEMO")) {
      QJsonArray usages;
      for (const auto &entry : set.layers)
        if (entry.fill.texture) {
          const auto &t = *entry.fill.texture;
          QJsonObject data{{"layer", entry.id.toString()},
                           {"asset", QString::fromStdString(t.asset)},
                           {"sha256", QString::fromStdString(t.hash)},
                           {"space", QString::fromStdString(t.source_space)}};
          if (t.pixels) {
            const auto pixel = t.sample(.5, .5, false);
            data["pixel"] = QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]};
          }
          usages.append(data);
        }
      state["input_usages"] = usages;
    }
    if (qEnvironmentVariableIsSet("BTS_FILL_TEXTURE_DEMO") ||
        qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO"))
    {
      state["fill_atom"] = layer.fill.atom;
      state["fill_mode"] = layer.fill.mode;
      state["fill_texture"] = layer.fill.texture ?
                                  QString::fromStdString(layer.fill.texture->name) :
                                  QString{};
      state["fill_texture_hash"] = layer.fill.texture ?
                                       QString::fromStdString(layer.fill.texture->hash) :
                                       QString{};
      state["fill_texture_resolved"] = layer.fill.texture && bool(layer.fill.texture->pixels);
      QJsonArray probes;
      for (const QPointF uv : {QPointF(.25, .25), QPointF(.75, .25)}) {
        const auto pixel = host.sample_texture(set.material_session_uid,
                                               0,
                                               int(uv.x() * set.texture_resolution),
                                               int(uv.y() * set.rows()));
        probes.append(QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]});
      }
      state["texture_probes"] = probes;
      if (!qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_DEMO"))
        state["composite_hash"] = QString::number(host.texture_hash(set.material_session_uid, 0),
                                                  16);
    }
    if (qEnvironmentVariableIsSet("BTS_BAKE_FIX_DEMO")) {
      state["bake_configuration"] = document->bake_configuration();
      QJsonArray maps;
      for (int role = 0; role < 7; ++role)
        if (set.mesh_maps[role]) {
          QJsonArray hashes;
          for (const auto &[tile, plane] : set.mesh_maps[role]->tiles)
            hashes.append(QString::fromStdString(plane.hash));
          maps.append(QJsonObject{{"role", role}, {"hashes", hashes}});
        }
      state["mesh_maps"] = maps;
    }
    if (qEnvironmentVariableIsSet("BTS_LAYER_EDIT_DEMO")) {
      QJsonArray layers;
      for (const auto &l : set.layers)
        layers.append(QJsonObject{{"id", l.id.toString()},
                                  {"visible", l.visible},
                                  {"name", QString::fromStdString(l.name)},
                                  {"pages", qint64(l.pixels.page_count(0))}});
      state["layer_items"] = layers;
      state["hash_bytes"] = qint64(bts::PaintPixels::hash_bytes());
      const auto probes = workspace->property("layerEditProbes").toList();
      if (probes.size() > 1) {
        const auto uv = probes[1].toPointF();
        const auto sample = host.sample_texture(set.material_session_uid,
                                                0,
                                                int(uv.x() * set.texture_resolution),
                                                int(uv.y() * set.rows()));
        state["probe"] = QJsonArray{sample[0], sample[1], sample[2], sample[3]};
      }
    }
    if (qEnvironmentVariableIsSet("BTS_DISPLACEMENT_DEMO")) {
      state["displacement"] = bts::displacement_json(set.displacement);
      state["displacement_hash"] = QString::number(layer.displacement.pixels.hash(2), 16);
      state["displacement_pages"] = qint64(layer.displacement.pixels.page_count(2));
      state["geometry"] = host.displacement_preview_state();
      state["document"] = document->to_json();
      const auto source = host.scene_snapshot();
      QJsonArray triangles;
      for (const auto &triangle : source.uv_triangles) {
        QJsonArray vertices, uv;
        for (const auto &p : triangle.positions)
          vertices.append(QJsonArray{p[0], p[1], p[2]});
        for (const auto &p : triangle.corners)
          uv.append(QJsonArray{p[0], p[1]});
        triangles.append(QJsonObject{{"vertices", vertices}, {"uv", uv}});
      }
      state["source_mesh_hash"] = QString::fromLatin1(
          QCryptographicHash::hash(QJsonDocument(triangles).toJson(QJsonDocument::Compact),
                                   QCryptographicHash::Sha256)
              .toHex());
      for (const auto &triangle : source.uv_triangles)
        if (triangle.material == std::size_t(selected_material)) {
          std::array<float, 3> center{};
          for (const auto &p : triangle.positions)
            for (int c = 0; c < 3; ++c)
              center[c] += p[c] / 3;
          const auto point = viewport->scene_point({center[0], center[1], center[2]});
          if (auto hit = viewport->probe_surface(point)) {
            state["stroke_point"] = QJsonArray{point.x(), point.y()};
            state["picked_uv"] = QJsonArray{hit->center[0], hit->center[1]};
            break;
          }
        }
    }
    if (qEnvironmentVariableIsSet("BTS_SIGNED_DISPLACEMENT_DEMO")) {
      float minimum = 0, maximum = 0;
      for (const auto &[tile, pixels] : layer.displacement.pixels.tiles())
        pixels.visit_pages([&](int, std::uint32_t, std::span<const bts::PaintPixels::Pixel> page) {
          for (const auto &pixel : page)
            if (pixel[3] > 1e-8f) {
              minimum = std::min(minimum, pixel[0] / pixel[3]);
              maximum = std::max(maximum, pixel[0] / pixel[3]);
            }
        });
      state["signed_range"] = QJsonArray{minimum, maximum};
      const auto uv = state["picked_uv"].toArray();
      if (set.displacement.allocated && uv.size() == 2) {
        const auto pixel = host.sample_texture(set.material_session_uid,
                                               16,
                                               int(uv[0].toDouble() * set.texture_resolution),
                                               int(uv[1].toDouble() * set.rows()));
        state["native_displacement"] = QJsonArray{pixel[0], pixel[1], pixel[2], pixel[3]};
      }
    }
    state["roughness"] = layer.values.roughness;
    const int cx = set.texture_resolution / 2, cy = set.rows() / 2;
    const auto gpu_sample = qEnvironmentVariableIsSet("BTS_COLOR_PICKER_DEMO") ?
                                host.diagnostic_gpu_pixel(set.material_session_uid, 0, cx, cy) :
                                std::optional<std::array<float, 4>>{};
    state["native_color_gpu_resident"] = bool(gpu_sample);
    const auto sampled = qEnvironmentVariableIsSet("BTS_COLOR_PICKER_DEMO") ?
                             gpu_sample.value_or(std::array<float, 4>{}) :
                             host.sample_texture(set.material_session_uid, 0, cx, cy);
    const auto composed = bts::TextureDocument::composite(set, cx, cy);
    const auto expected = bts::precision_channel(composed.material, 0, set.precision[0]),
               base = bts::precision_channel(set.base_values, 0, set.precision[0]);
    const float alpha = composed.coverage[0];
    state["native_color"] = QJsonArray{sampled[0], sampled[1], sampled[2], sampled[3]};
    state["native_color_expected"] = QJsonArray{expected[0] - base[0] * (1 - alpha),
                                                expected[1] - base[1] * (1 - alpha),
                                                expected[2] - base[2] * (1 - alpha),
                                                alpha};
    state["brush_color"] = QJsonArray{brush.values.base_color[0],
                                      brush.values.base_color[1],
                                      brush.values.base_color[2],
                                      brush.values.base_color[3]};
    state["base_color"] = QJsonArray{set.base_values.base_color[0],
                                     set.base_values.base_color[1],
                                     set.base_values.base_color[2],
                                     set.base_values.base_color[3]};
    state["layer_color"] = QJsonArray{layer.values.base_color[0],
                                      layer.values.base_color[1],
                                      layer.values.base_color[2],
                                      layer.values.base_color[3]};
  }
  return state;
};
if (harness.is_set("gui-fix-demo") && workspace)
  bts::start_gui_fix_demo(workspace,
                          viewport,
                          harness.value("gui-fix-demo"),
                          harness.value("gui-fix-mesh"),
                          harness.value("gui-fix-resolution").toInt(),
                          gui_fix_demo_state);
