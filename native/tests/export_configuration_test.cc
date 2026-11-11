// SPDX-License-Identifier: GPL-3.0-or-later
#include "ExportConfiguration.hh"
#include <QJsonArray>
#include <QJsonDocument>
#include "ResourceAvailability.hh"
#include "TextureDocument.hh"
#include <vector>
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  const std::array<QUuid, 2> sets{QUuid::createUuid(), QUuid::createUuid()};
  auto config = read_export_configuration({}, sets);
  assert(config.selected.size() == 2);
  config.presets
      .clear();  // The fixture below owns preset slot 0; Studio defaults are checked separately.

  config.directory = "/unavailable/portable-export";
  config.format = ExportFormat::Png16;
  config.size = 512;
  config.sizes[sets[1]] = 256;
  config.padding = ExportPadding::Diffusion;
  config.selected.erase(sets[1]);
  ExportOutputPreset preset;
  preset.name = "Packed fixture";
  preset.packed = PackedOutput{};
  preset.packed->format = ExportFormat::Exr16;
  preset.standard_outputs = false;
  set_default_export_formats(preset);
  preset.outputs = {
      {"$textureSet_glossiness", ExportMapSource::Glossiness, false, ExportFormat::Jpeg8}};
  config.presets.push_back(preset);
  config.preset = preset.id;
  const auto json = export_configuration_json(config);
  assert(json["version"] == 8);
  assert(export_configuration_json(read_export_configuration(json, sets)) == json);
  ExportRequest request;
  apply_export_preset(request, preset);
  assert(request.formats == preset.formats && request.packed->format == ExportFormat::Exr16 &&
         request.outputs[0].format == ExportFormat::Jpeg8);
  // Variable-size metadata is admitted by memory, not preset/output/string caps.
  {
    auto large = config;
    large.presets.clear();
    large.preset = QUuid();
    large.size = 8192;
    large.directory = "/metadata-only/" + QString(5000, 'd');
    for (int i = 0; i < 140; ++i) {
      auto entry = preset;
      entry.id = QUuid::createUuid();
      entry.name = QString(8192, 'n') + QString::number(i);
      entry.outputs.clear();
      for (int j = 0; j < 40; ++j)
        entry.outputs.push_back(
            {std::string(600, 'x') + std::to_string(j), ExportMapSource::Bump, false});
      large.presets.push_back(std::move(entry));
    }
    const auto data = export_configuration_json(large);
    assert(QJsonDocument(data).toJson().size() > 1024 * 1024);
    assert(export_configuration_json(read_export_configuration(data, sets)) == data);
    auto old = data;
    old["version"] = 7;
    const auto migrated = read_export_configuration(old, sets);
    assert(migrated.presets.size() == 141 && migrated.preset == studio_preset_id);
    {
      TextureRamQueryScope low(+[]() -> std::uint64_t { return 0; });
      bool denied = false;
      try {
        read_export_configuration(data, sets);
      }
      catch (const std::length_error &) {
        denied = true;
      }
      assert(!denied);
    }
  }
  const auto rejects = [&](const QJsonObject &bad) {
    bool rejected = false;
    try {
      read_export_configuration(bad, sets);
    }
    catch (const std::invalid_argument &) {
      rejected = true;
    }
    assert(rejected);
  };
  for (int rejected_size : {8193, 16384, 2147483647}) {
    auto bad = json;
    bad["size"] = rejected_size;
    rejects(bad);
    bad = json;
    bad["sizes"] = QJsonArray{
        QJsonObject{{"id", sets[0].toString(QUuid::WithoutBraces)}, {"size", rejected_size}}};
    rejects(bad);
  }
  for (int test = 0; test < 7; ++test) {
    auto bad = json;
    if (test == 0)
      bad["format"] = 9;
    if (test == 1)
      bad["size"] = 500;
    if (test == 2)
      bad["padding_width"] = -1;
    if (test == 3)
      bad["preset"] = QUuid::createUuid().toString();
    if (test == 4)
      bad["selected"] = QJsonArray{QUuid::createUuid().toString()};
    if (test == 5)
      bad["directory"] = "../relative";
    if (test == 6) {
      auto ps = bad["presets"].toArray();
      auto p = ps[0].toObject();
      auto packed = p["packed"].toObject();
      packed["sources"] = QJsonArray{0, 1, 99};
      p["packed"] = packed;
      ps[0] = p;
      bad["presets"] = ps;
    }
    rejects(bad);
  }
  // Invalid encodings cannot represent JPEG32: reject unknown enum, object, bool,
  // fractional and missing fields rather than silently falling back to global.
  for (const auto &value :
       QJsonArray{5, -1, 1.5, true, "jpeg32", QJsonObject{{"format", "jpeg"}, {"bits", 32}}})
    for (int scope = 0; scope < 3; ++scope) {
      auto bad = json;
      auto ps = bad["presets"].toArray();
      auto p = ps[0].toObject();
      if (scope == 0) {
        auto formats = p["formats"].toArray();
        formats[0] = value;
        p["formats"] = formats;
      }
      else if (scope == 1) {
        auto packed = p["packed"].toObject();
        packed["format"] = value;
        p["packed"] = packed;
      }
      else {
        auto outputs = p["outputs"].toArray();
        auto map = outputs[0].toObject();
        map["format"] = value;
        outputs[0] = map;
        p["outputs"] = outputs;
      }
      ps[0] = p;
      bad["presets"] = ps;
      rejects(bad);
    }
  for (int version = 1; version <= 5; ++version) {
    auto legacy = json;
    legacy["version"] = version;
    auto ps = legacy["presets"].toArray();
    auto p = ps[0].toObject();
    p.remove("formats");
    auto packed = p["packed"].toObject();
    packed.remove("format");
    p["packed"] = packed;
    auto outputs = p["outputs"].toArray();
    auto map = outputs[0].toObject();
    map.remove("format");
    outputs[0] = map;
    p["outputs"] = outputs;
    if (version == 1) {
      auto names = p["names"].toArray();
      names.removeLast();
      names[4] = "$mesh_$textureSet_Height";
      p["names"] = names;
    }
    ps[0] = p;
    legacy["presets"] = ps;
    const auto migrated = read_export_configuration(legacy, sets);
    for (const auto &format : migrated.presets[0].formats)
      assert(!format);
    assert(!migrated.presets[0].packed->format);
    if (version >= 3)
      assert(!migrated.presets[0].outputs[0].format);
    if (version == 1)
      assert(migrated.presets[0].naming[4] == "$mesh_$textureSet_bump");
    assert(export_configuration_json(migrated)["version"] == 8);
  }
  config.mesh_displacement = true;
  config.format = ExportFormat::Jpeg8;
  auto jpeg = export_configuration_json(config);
  assert(read_export_configuration(jpeg, sets).format == ExportFormat::Jpeg8);
  jpeg["version"] = 3;
  rejects(jpeg);
  auto usd = usd_metal_rough_preset();
  apply_export_preset(request, usd);
  for (int c : {0, 1, 2, 7})
    assert(request.formats[c] == ExportFormat::Jpeg8);
  assert(request.outputs.empty());
  auto usd_config = config;
  usd_config.format = ExportFormat::Png8;
  usd_config.presets = {usd};
  usd_config.preset = usd.id;
  auto usd5 = export_configuration_json(usd_config);
  usd5["version"] = 5;
  auto ups = usd5["presets"].toArray();
  auto up = ups[0].toObject();
  up.remove("formats");
  auto uo = up["outputs"].toArray();
  uo.append(QJsonObject{{"name", "$mesh_$textureSet_normal_opengl"},
                        {"source", int(ExportMapSource::NormalOpenGL)},
                        {"alpha", false}});
  up["outputs"] = uo;
  ups[0] = up;
  usd5["presets"] = ups;
  const auto legacy_usd = read_export_configuration(usd5, sets);
  assert(legacy_usd.presets[0].outputs.empty());
  apply_export_preset(request, legacy_usd.presets[0]);
  for (int c : {0, 1, 2, 7})
    assert(request.formats[c] == ExportFormat::Jpeg8);
  assert(request.outputs.empty());
  auto modern = config;
  modern.preset = QUuid();
  modern.format = ExportFormat::Exr32;
  modern.naming_convention = ExportNamingConvention::OpenPbr;
  modern.openpbr_materialx = true;
  const auto modern_json = export_configuration_json(modern);
  auto modern_read = read_export_configuration(modern_json, sets);
  assert(modern_read.naming_convention == ExportNamingConvention::OpenPbr &&
         modern_read.openpbr_materialx && export_configuration_json(modern_read) == modern_json);
  auto version6 = modern_json;
  version6["version"] = 6;
  auto migrated6 = read_export_configuration(version6, sets);
  assert(migrated6.naming_convention == ExportNamingConvention::Classic &&
         !migrated6.openpbr_materialx);
  for (int test = 0; test < 5; ++test) {
    auto bad = modern_json;
    if (test == 0)
      bad["naming_convention"] = 2;
    if (test == 1)
      bad["naming_convention"] = .5;
    if (test == 2)
      bad["openpbr_materialx"] = 1;
    if (test == 3)
      bad["format"] = int(ExportFormat::Png16);
    if (test == 4)
      bad["preset"] = preset.id.toString(QUuid::WithoutBraces);
    rejects(bad);
  }
  // Studio: built-in default for new and never-chosen projects; explicit choices survive.
  {
    const auto fresh = read_export_configuration({}, sets);
    assert(fresh.preset == studio_preset_id && fresh.presets.size() == 1 &&
           fresh.presets[0].kind == ExportPresetKind::Studio && fresh.presets[0].name == "Studio");
    const auto fresh_json = export_configuration_json(fresh);
    assert(fresh_json["version"] == 8 &&
           export_configuration_json(read_export_configuration(fresh_json, sets)) == fresh_json);
    auto legacy = fresh_json;
    legacy["version"] = 7;
    legacy["presets"] = QJsonArray{};
    legacy["preset"] = "";
    const auto adopted = read_export_configuration(legacy, sets);
    assert(adopted.preset == studio_preset_id && adopted.presets.size() == 1 &&
           adopted.presets[0].kind == ExportPresetKind::Studio);
    auto document_choice = fresh_json;
    document_choice["preset"] = "";
    assert(read_export_configuration(document_choice, sets).preset.isNull());
    auto user_choice = json;
    user_choice["version"] = 7;
    const auto kept = read_export_configuration(user_choice, sets);
    assert(kept.preset == preset.id && kept.presets.size() == 1);
    auto old_kind = fresh_json;
    old_kind["version"] = 7;
    rejects(old_kind);
    ExportRequest m;
    apply_export_preset(m, studio_preset());
    assert(m.studio && m.format == ExportFormat::Exr32 && m.outputs.empty() && !m.packed);
    for (const auto &format : m.formats)
      assert(format == ExportFormat::Exr32);
    assert((*m.selected_channels ==
            std::array<bool, 9>{true, true, true, false, true, false, false, false, true}));
    for (int c = 0; c < 9; ++c)
      assert(studio_preset().naming[c] == "$mesh_" + std::string(export_channel_suffixes[c]));
    auto after = m;
    apply_export_preset(after, usd_metal_rough_preset());
    assert(!after.studio);
    auto broken = studio_preset();
    broken.packed = PackedOutput{};
    bool refused = false;
    try {
      ExportRequest r;
      apply_export_preset(r, broken);
    }
    catch (const std::invalid_argument &) {
      refused = true;
    }
    assert(refused);
    // Used channels are metadata only: base defaults and empty layers export nothing.
    TextureSet set;
    set.layers.clear();
    for (int c = 0; c < 9; ++c)
      assert(!export_channel_used(set, c));
    TextureLayer paint;
    paint.kind = LayerKind::Paint;
    std::vector<float> rgba(64 * 64 * 4, 0.f);
    rgba[0] = 1;
    rgba[3] = 1;
    paint.pixels.assign_channel(64, 0, rgba);
    set.layers.push_back(paint);
    assert(export_channel_used(set, 0));
    for (int c : {1, 2, 3, 4, 5, 6, 7, 8})
      assert(!export_channel_used(set, c));
    set.layers[0].visible = false;
    assert(!export_channel_used(set, 0));
    set.layers[0].visible = true;
    set.layers[0].opacity[0] = 0;
    assert(!export_channel_used(set, 0));
    set.layers[0].opacity[0] = 1;
    set.channels[0] = false;
    assert(!export_channel_used(set, 0));
    set.channels[0] = true;
    TextureLayer folder;
    folder.kind = LayerKind::Folder;
    folder.visible = false;
    set.layers[0].parent = folder.id;
    set.layers.push_back(folder);
    assert(!export_channel_used(set, 0));
    set.layers.back().visible = true;
    assert(export_channel_used(set, 0));
    TextureLayer fill;
    fill.kind = LayerKind::Fill;
    fill.channels = {false, false, true, false, true};
    set.layers.push_back(fill);
    assert(export_channel_used(set, 2) && export_channel_used(set, 4) &&
           !export_channel_used(set, 1));
    set.base_values.metallic = .5f;
    assert(!export_channel_used(set, 1));  // Imported/base constants are not authoring.
    TextureSet bumped;
    bumped.layers.clear();
    bumped.base_values.bump = .75f;
    bumped.base_values.base_color = {.2f, .3f, .4f, 1};
    assert(!export_channel_used(bumped, 4) && !export_channel_used(bumped, 0));
    bumped.material_layering.enabled = true;
    assert(export_channel_used(bumped, 0));
  }
  std::cout
      << "Export config v8: Studio default/migration/used channels, naming/MaterialX, legacy v6 defaults, all encoding scopes, legacy 1-6 inheritance, invalid pairs and USD recipe passed\n";
}
