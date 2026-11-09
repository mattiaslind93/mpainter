// Headless interchange uses the production codecs and evaluator. No Blender linkage.
#include "AssetCatalog.hh"
#include "BuiltinSmartMasks.hh"
#include "MaterialSpherePreview.hh"
#include "MaterialValuesJson.hh"
#include "SmartMaterialPreset.hh"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <iostream>

namespace {
using namespace bts;
constexpr int side = 256;
QByteArray read(const QString &path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot read " + path.toStdString());
  return file.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error("Cannot write " + path.toStdString());
}
QJsonObject json(const QByteArray &bytes)
{
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::invalid_argument("Invalid bridge JSON");
  return doc.object();
}
QString uid(const QUuid &id)
{
  return id.toString(QUuid::WithoutBraces);
}
void floats(const QString &path, const std::vector<float> &values)
{
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  for (float value : values)
    stream << value;
  write(path, data);
}
QJsonObject raster(const QString &dir,
                   const QString &name,
                   int width,
                   int height,
                   const std::vector<float> &values)
{
  floats(dir + "/" + name + ".f32", values);
  return {{"file", name + ".f32"},
          {"width", width},
          {"height", height},
          {"channels", 4},
          {"origin", "bottom-left"},
          {"encoding", "float32-le-linear"}};
}
QJsonObject mask_export(const LayerMask &mask,
                        const QString &dir,
                        const QString &prefix,
                        const detail::ShaderBallUvSurface &surface)
{
  QJsonObject result{{"background", mask.background},
                     {"enabled", mask.enabled},
                     {"effects", mask_effects_json(mask.effects)}};
  bool painted = false;
  for (const auto &[tile, plane] : mask.pixels.tiles())
    painted |= plane.page_count(2) > 0;
  result["has_paint"] = painted;
  QJsonObject resources, fallbacks;
  MaskCompareInputs context;
  context.projection = &surface.atlas;
  for (const auto &effect : mask_effects_recursive(mask.effects)) {
    if (const auto &texture = effect.mapping.projection.texture;
        texture && texture->pixels && !texture->tiles)
    {
      std::vector<float> data;
      data.reserve(texture->pixels->size() * 4);
      for (const auto &pixel : *texture->pixels)
        for (float v : pixel)
          data.push_back(v);
      resources[uid(effect.id)] = raster(
          dir, prefix + "-texture-" + uid(effect.id), texture->width, texture->height, data);
    }
    // Explicit reference for operations without a stock shader-node equivalent.
    // This is never substituted for a supported editable operation.
    if (effect.kind == MaskEffectKind::Generator &&
        (effect.response == 2 || effect.grunge_amount > 0 || effect.generator == 7 ||
         effect.curvature_radius > 0))
    {
      auto single = effect;
      single.opacity = 1;
      single.blending = LayerBlend::Normal;
      LayerMask isolated;
      isolated.background = 0;
      isolated.effects = {single};
      std::vector<float> data;
      for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x) {
          const float v = isolated.value(x, y, 1001, side, side, nullptr, &surface.maps, &context);
          data.insert(data.end(), {v, v, v, 1});
        }
      fallbacks[uid(effect.id)] = raster(
          dir, prefix + "-reference-" + uid(effect.id), side, side, data);
    }
  }
  result["textures"] = resources;
  result["fallbacks"] = fallbacks;
  std::vector<float> data;
  QImage image(side, side, QImage::Format_RGB32);
  for (int y = 0; y < side; ++y)
    for (int x = 0; x < side; ++x) {
      const float v = mask.value(x, y, 1001, side, side, nullptr, &surface.maps, &context);
      data.insert(data.end(), {v, v, v, 1});
      const int c = qRound(v * 255);
      image.setPixel(x, side - 1 - y, qRgb(c, c, c));
    }
  result["reference"] = raster(dir, prefix + "-native", side, side, data);
  if (!image.save(dir + "/" + prefix + "-native.png") ||
      !smart_mask_sphere_preview(mask, side).save(dir + "/" + prefix + "-sphere.png"))
    throw std::runtime_error("Cannot save preview");
  return result;
}
void export_preset(const QString &input, const QString &dir)
{
  QDir().mkpath(dir);
  const auto bytes = read(input);
  const auto surface = detail::shaderball_uv_surface(side);
  QJsonObject root{{"bridge_version", 1},
                   {"archive", QString::fromLatin1(bytes.toBase64())},
                   {"name", QFileInfo(input).completeBaseName()},
                   {"resolution", side}};
  QJsonObject maps;
  for (int role : {1, 3, 4, 5, 6}) {
    const auto &p = *surface.maps[role]->tiles.at(1001).pixels;
    std::vector<float> data;
    for (float v : p.rgba)
      data.push_back(v);
    maps[QString::number(role)] = raster(
        dir, QString("shaderball-jl01-map-%1").arg(role), side, side, data);
  }
  root["maps"] = maps;
  if (input.endsWith(".btsmask")) {
    root["kind"] = "mask";
    root["mask"] = mask_export(read_mask_preset(bytes), dir, "mask", surface);
  }
  else if (input.endsWith(".btsmaterial")) {
    root["kind"] = "material";
    auto preset = read_material_preset(bytes);
    QJsonArray layers;
    for (const auto &layer : preset.set.layers) {
      QJsonArray opacity, blending, channels;
      for (int c = 0; c < 5; ++c) {
        opacity.append(layer.opacity[c]);
        blending.append(layer_blend_names[int(layer.blending[c])]);
        channels.append(layer.channels[c]);
      }
      QJsonObject item{{"id", uid(layer.id)},
                       {"name", QString::fromStdString(layer.name)},
                       {"kind",
                        layer.kind == LayerKind::Folder ? "folder" :
                        layer.kind == LayerKind::Fill   ? "fill" :
                                                          "paint"},
                       {"parent", uid(layer.parent)},
                       {"values", material_values_json(layer.values)},
                       {"opacity", opacity},
                       {"blending", blending},
                       {"channels", channels},
                       {"visible", layer.visible},
                       {"content_effects", effects_json(layer.effects)},
                       {"textured", fill_has_texture(layer.fill)}};
      QJsonObject channel_textures;
      for (int channel : {0, 1, 2})
        if (const auto &texture = fill_channel_texture(layer.fill, channel);
            texture && texture->pixels && !texture->tiles)
        {
          std::vector<float> data;
          for (const auto &pixel : *texture->pixels)
            for (float v : pixel)
              data.push_back(v);
          channel_textures[QString::number(channel)] = raster(dir,
                                                              uid(layer.id) + "-channel-" +
                                                                  QString::number(channel),
                                                              texture->width,
                                                              texture->height,
                                                              data);
        }
      item["channel_textures"] = channel_textures;
      item["projection_mode"] = layer.fill.mode;
      item["projection_filtering"] = layer.fill.filtering;
      if (layer.mask)
        item["mask"] = mask_export(*layer.mask, dir, uid(layer.id), surface);
      layers.append(item);
    }
    root["layers"] = layers;
    preset.set.mesh_maps = surface.maps;
    preset.set.texture_resolution = side;
    preset.set.projection_geometry = std::make_shared<FillSurfaceAtlas>(surface.atlas);
    std::vector<float> color, roughness, metallic;
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < side; ++x) {
        const auto v = TextureDocument::composite(preset.set, x, y, 1001).material;
        color.insert(color.end(), {v.base_color[0], v.base_color[1], v.base_color[2], 1});
        roughness.insert(roughness.end(), {v.roughness, v.roughness, v.roughness, 1});
        metallic.insert(metallic.end(), {v.metallic, v.metallic, v.metallic, 1});
      }
    root["reference_color"] = raster(dir, "material-color-native", side, side, color);
    root["reference_roughness"] = raster(dir, "material-roughness-native", side, side, roughness);
    root["reference_metallic"] = raster(dir, "material-metallic-native", side, side, metallic);
  }
  else
    throw std::invalid_argument("Expected .btsmask or .btsmaterial");
  write(dir + "/preset.json", QJsonDocument(root).toJson());
}
void patch_effects(std::vector<MaskEffect> &effects, const QJsonArray &edits)
{
  if (edits.size() != qsizetype(effects.size()))
    throw std::invalid_argument("Effect topology changed");
  for (int i = 0; i < edits.size(); ++i) {
    auto &old = effects[i];
    const auto obj = edits[i].toObject();
    auto parsed = read_mask_effects(QJsonArray{obj}).front();
    if (parsed.id != old.id || parsed.kind != old.kind)
      throw std::invalid_argument("Effect identity changed");
    if (old.group_effects) {
      auto children = std::make_shared<std::vector<MaskEffect>>(*old.group_effects);
      patch_effects(*children, obj["effects"].toArray());
      parsed.group_effects = children;
    }
    parsed.paint = old.paint;
    parsed.mapping.projection.texture = old.mapping.projection.texture;
    old = std::move(parsed);
  }
}
void patch_mask(LayerMask &mask, const QJsonObject &edit)
{
  const auto bg = edit["background"];
  if (!bg.isDouble() || !std::isfinite(bg.toDouble()) || bg.toDouble() < 0 || bg.toDouble() > 1 ||
      !edit["enabled"].isBool())
    throw std::invalid_argument("Invalid mask state");
  mask.background = float(bg.toDouble());
  mask.enabled = edit["enabled"].toBool();
  patch_effects(mask.effects, edit["effects"].toArray());
}
void import_preset(const QString &input, const QString &output)
{
  const auto root = json(read(input));
  if (root["bridge_version"].toInt() != 1)
    throw std::invalid_argument("Unsupported bridge version");
  const auto decoded = QByteArray::fromBase64Encoding(root["archive"].toString().toLatin1(),
                                                      QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded)
    throw std::invalid_argument("Invalid embedded archive");
  if (root["kind"] == "mask") {
    auto mask = read_mask_preset(decoded.decoded);
    patch_mask(mask, root["mask"].toObject());
    const auto bytes = write_mask_preset(mask);
    read_mask_preset(bytes);
    write(output, bytes);
  }
  else if (root["kind"] == "material") {
    auto preset = read_material_preset(decoded.decoded);
    const auto layers = root["layers"].toArray();
    if (layers.size() != qsizetype(preset.set.layers.size()))
      throw std::invalid_argument("Layer topology changed");
    for (int i = 0; i < layers.size(); ++i) {
      auto &layer = preset.set.layers[i];
      const auto edit = layers[i].toObject();
      if (uid(layer.id) != edit["id"].toString())
        throw std::invalid_argument("Layer identity changed");
      layer.values = read_material_values(edit["values"]);
      if (layer.mask)
        patch_mask(*layer.mask, edit["mask"].toObject());
    }
    const auto bytes = write_material_preset(preset);
    read_material_preset(bytes);
    write(output, bytes);
  }
  else
    throw std::invalid_argument("Unknown preset kind");
}
std::shared_ptr<const FillTexture> baked_texture(const QString &dir,
                                                 const QJsonObject &description,
                                                 bool scalar)
{
  const int width = description["width"].toInt(), height = description["height"].toInt();
  // The command-line bake path has an explicit working-memory bound. Native
  // archive import/export itself continues to use the production codec limits.
  if (width < 32 || height < 32 || width > 4096 || height > 4096 ||
      description["channels"].toInt() != 4 || description["encoding"] != "float32-le-linear" ||
      description["origin"] != "bottom-left")
    throw std::invalid_argument("Invalid bake dimensions or encoding (32..4096 RGBA float32)");
  const auto name = description["file"].toString();
  if (name.isEmpty() || QFileInfo(name).fileName() != name)
    throw std::invalid_argument("Bake payload must be beside its manifest");
  const auto bytes = read(dir + "/" + name);
  if (bytes.size() != qint64(width) * height * 16)
    throw std::invalid_argument("Invalid bake payload length");
  auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(width) * height);
  QDataStream stream(bytes);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
  for (auto &pixel : *pixels) {
    for (float &v : pixel) {
      stream >> v;
      if (!std::isfinite(v) || std::abs(v) > 65504)
        throw std::invalid_argument("Invalid baked value");
    }
    if (scalar && (pixel[0] < 0 || pixel[0] > 1))
      throw std::invalid_argument("Baked mask/roughness/metallic must be in [0,1]");
    if (scalar)
      pixel[1] = pixel[2] = pixel[0];
    pixel[3] = 1;
  }
  auto texture = std::make_shared<FillTexture>();
  texture->width = width;
  texture->height = height;
  texture->raw = true;
  texture->input_space = "Non-Color/raw";
  texture->asset = uid(QUuid::createUuid()).toStdString();
  texture->name = name.toStdString();
  texture->hash =
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
  texture->pixels = std::move(pixels);
  texture->validate();
  return texture;
}
void import_bake(const QString &input, const QString &output)
{
  const auto root = json(read(input));
  if (root["bake_version"].toInt() != 1)
    throw std::invalid_argument("Unsupported bake version");
  const auto dir = QFileInfo(input).absolutePath();
  if (root["kind"] == "mask") {
    LayerMask mask;
    mask.background = 0;
    MaskEffect effect;
    effect.kind = MaskEffectKind::Fill;
    effect.name = "Blender baked scalar graph";
    effect.fill = 1;
    effect.mapping.projection.texture = baked_texture(dir, root["mask"].toObject(), true);
    effect.mapping.filtering = 1;
    effect.mapping.wrap = 1;
    mask.effects.push_back(effect);
    const auto bytes = write_mask_preset(mask);
    read_mask_preset(bytes);
    write(output, bytes);
  }
  else if (root["kind"] == "material") {
    auto color = baked_texture(dir, root["base_color"].toObject(), false),
         roughness = baked_texture(dir, root["roughness"].toObject(), true),
         metallic = baked_texture(dir, root["metallic"].toObject(), true);
    if (color->width != roughness->width || color->height != roughness->height ||
        color->width != metallic->width || color->height != metallic->height)
      throw std::invalid_argument("Bake channel dimensions differ");
    NewProjectOptions options;
    options.resolution = DocumentResolution::Pixels256;
    options.uv_workflow = UvWorkflow::PreserveTiles;
    ImportSummary imported;
    imported.materials = {{1, "Blender bake", {1001}}};
    SceneSnapshot scene;
    MaterialSummary summary;
    summary.session_id = 1;
    summary.name = "Blender bake";
    summary.uniform_editable = true;
    scene.materials.push_back(summary);
    TextureDocument doc(options, imported, scene);
    auto &set = doc.set(1);
    auto &layer = set.layers.front();
    layer.kind = LayerKind::Fill;
    layer.name = "Blender baked material";
    layer.fill.texture = color;
    fill_channel_texture(layer.fill, 2) = roughness;
    fill_channel_texture(layer.fill, 1) = metallic;
    layer.fill.mode = 6;
    layer.fill.filtering = 2;
    TextureDocument::group_layers(set, {layer.id});
    const auto bytes = write_material_preset(capture_material_folder(set, set.selected_layer));
    read_material_preset(bytes);
    write(output, bytes);
  }
  else
    throw std::invalid_argument("Unknown bake kind");
}
void catalog(const QString &dir)
{
  QDir().mkpath(dir);
  for (const auto &asset : builtin_smart_mask_assets()) {
    const auto path = dir + "/" + asset.filename;
    write(path, asset.bytes);
    export_preset(path, dir + "/" + QFileInfo(path).completeBaseName());
  }
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels256;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  ImportSummary imported;
  imported.materials = {{1, "Blender roundtrip", {1001}}};
  SceneSnapshot scene;
  MaterialSummary summary;
  summary.session_id = 1;
  summary.name = "Blender roundtrip";
  summary.uniform_editable = true;
  scene.materials.push_back(summary);
  TextureDocument doc(options, imported, scene);
  auto &set = doc.set(1);
  auto &base = set.layers.front();
  base.kind = LayerKind::Fill;
  base.name = "Metal";
  base.values.base_color = {.12f, .22f, .3f, 1};
  base.values.roughness = .24f;
  base.values.metallic = .8f;
  const auto first = base.id;
  TextureDocument::add_fill(set);
  auto &top = TextureDocument::layer(set, set.selected_layer);
  top.name = "Rust";
  top.values.base_color = {.55f, .12f, .025f, 1};
  top.values.roughness = .78f;
  top.values.metallic = 0;
  top.mask = *builtin_smart_mask_assets().front().smart_mask;
  const auto second = top.id;
  TextureDocument::group_layers(set, {first, second});
  TextureDocument::layer(set, set.selected_layer).name = "Blender Rust Material";
  const auto path = dir + "/rust-material.btsmaterial";
  write(path, write_material_preset(capture_material_folder(set, set.selected_layer)));
  export_preset(path, dir + "/rust-material");
}
}  // namespace
int main(int argc, char **argv)
{
  QGuiApplication app(argc, argv);
  try {
    if (argc == 3 && QString(argv[1]) == "catalog")
      catalog(QString::fromLocal8Bit(argv[2]));
    else if (argc == 4 && QString(argv[1]) == "export")
      export_preset(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    else if (argc == 4 && QString(argv[1]) == "import")
      import_preset(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    else if (argc == 4 && QString(argv[1]) == "import-bake")
      import_bake(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    else
      throw std::invalid_argument(
          "Usage: bts-blender-presets catalog DIR | export PRESET DIR | import preset.json PRESET | import-bake bake.json PRESET");
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
