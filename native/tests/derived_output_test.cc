// SPDX-License-Identifier: GPL-3.0-or-later
#include "DerivedOutput.hh"
#include "ExportConfiguration.hh"
#include <cassert>
#include <cstdio>
using namespace bts;
int main()
{
  MaterialValues v;
  v.base_color = {.5f, .25f, .125f, 1};
  v.roughness = .25f;
  v.metallic = 0;
  v.bump = -.2f;
  const std::array<float, 3> n{.3f, .6f, .9f};
  assert(export_map_value(ExportMapSource::Glossiness, v, n, 1)[0] == .75f);
  assert(export_map_value(ExportMapSource::GlossinessSquared, v, n, 1)[0] == .5625f);
  const auto dx = export_map_value(ExportMapSource::NormalDirectX, v, n, 1);
  assert(dx[0] == n[0] && dx[2] == n[2] && std::abs(dx[1] + n[1] - 1) < 1e-6f);
  assert(export_map_value(ExportMapSource::Diffuse, v, n, 1)[0] == .5f);
  assert(export_map_value(ExportMapSource::Specular, v, n, 1)[0] == .04f);
  assert(export_map_value(ExportMapSource::F0, v, n, 1)[0] == .04f);
  assert(std::abs(export_map_value(ExportMapSource::InverseIor, v, n, 1)[0] - 1 / 1.4f) < 1e-6f);
  assert(export_map_value(ExportMapSource::MixedAo, v, n, .23f)[0] == .23f);
  assert(export_map_value(ExportMapSource::Bump, v, n, 1)[0] == -.2f);
  v.metallic = 1;
  assert(export_map_value(ExportMapSource::Diffuse, v, n, 1)[0] == 0);
  assert(export_map_value(ExportMapSource::Specular, v, n, 1)[0] == .5f);
  assert(export_map_value(ExportMapSource::F0, v, n, 1)[0] == 1);
  assert(export_map_value(ExportMapSource::InverseIor, v, n, 1)[0] == .01f);
  const auto id = QUuid::createUuid();
  ExportConfiguration config;
  config.directory = "/tmp/derived";
  config.selected = {id};
  const auto builtin = converted_specular_glossiness_preset();
  assert(builtin.outputs.size() == 5 && !builtin.standard_outputs && builtin.outputs[0].alpha &&
         builtin.outputs[3].source == ExportMapSource::Bump);
  config.presets = {builtin};
  config.preset = builtin.id;
  const std::array sets{id};
  const auto saved = export_configuration_json(config);
  const auto loaded = read_export_configuration(saved, sets);
  assert(loaded.presets[0].outputs == builtin.outputs &&
         export_configuration_json(loaded) == saved);
  auto old_maps = builtin.outputs;
  for (auto &map : old_maps)
    map.format.reset();
  const auto legacy_json = [&](int version) {
    auto json = saved;
    json["version"] = version;
    json.remove("mesh_displacement");
    auto list = json["presets"].toArray();
    auto preset = list[0].toObject();
    preset.remove("formats");
    preset.remove("kind");
    auto maps = preset["outputs"].toArray();
    for (int n = 0; n < maps.size(); ++n) {
      auto map = maps[n].toObject();
      map.remove("format");
      maps[n] = map;
    }
    preset["outputs"] = maps;
    list[0] = preset;
    json["presets"] = list;
    return json;
  };
  for (int version : {3, 4, 5})
    assert(read_export_configuration(version == 5 ?
                                         [&] {
                                           auto json = legacy_json(5);
                                           json["mesh_displacement"] = false;
                                           auto ps = json["presets"].toArray();
                                           auto p = ps[0].toObject();
                                           p["kind"] = 0;
                                           ps[0] = p;
                                           json["presets"] = ps;
                                           return json;
                                         }() :
                                         legacy_json(version),
                                     sets)
               .presets[0]
               .outputs == old_maps);
  auto legacy = saved;
  legacy["version"] = 2;
  auto list = legacy["presets"].toArray();
  auto preset = list[0].toObject();
  preset.remove("outputs");
  list[0] = preset;
  legacy["presets"] = list;
  assert(read_export_configuration(legacy, sets).presets[0].outputs.empty());
  auto bad = saved;
  list = bad["presets"].toArray();
  preset = list[0].toObject();
  auto outputs = preset["outputs"].toArray();
  auto output = outputs[0].toObject();
  output["source"] = 999;
  outputs[0] = output;
  preset["outputs"] = outputs;
  list[0] = preset;
  bad["presets"] = list;
  bool failed = false;
  try {
    read_export_configuration(bad, sets);
  }
  catch (...) {
    failed = true;
  }
  assert(failed);
  std::puts(
      "Derived formulas, explicit normal conventions, signed Bump, five-map preset, exact config v6 and genuine legacy v5/v4/v3/v2 PASS");
}
