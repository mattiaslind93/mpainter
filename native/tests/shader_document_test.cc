// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlendUniformChannels.hh"
#include "TextureDocument.hh"
#include <cassert>
#include <cstdio>
using namespace bts;
int main()
{
  {
    TextureLayer layer;
    layer.values.normal = {.3f, .4f, .5f};
    layer.values.bump = -.25f;
    layer.values.ambient_occlusion = .2f;
    layer.channels = {true, true, true, true, true};
    auto texture = std::make_shared<FillTexture>();
    fill_channel_texture(layer.fill, 0) = texture;
    fill_channel_texture(layer.fill, 1) = texture;
    fill_channel_texture(layer.fill, 4) = texture;
    auto previous = layer;
    auto values = new_base_values();
    values.base_color = {.2f, .3f, .4f, 1};
    values.metallic = .8f;
    values.roughness = .23f;
    apply_blend_uniform_channels(layer, values, false);
    assert(layer.values.base_color == values.base_color &&
           layer.values.metallic == values.metallic && layer.values.roughness == values.roughness);
    assert(layer.values.normal == previous.values.normal &&
           layer.values.bump == previous.values.bump &&
           layer.values.ambient_occlusion == previous.values.ambient_occlusion);
    assert(layer.channels == previous.channels && layer.ao_enabled == previous.ao_enabled);
    assert(!fill_channel_texture(layer.fill, 0) && !fill_channel_texture(layer.fill, 1) &&
           fill_channel_texture(layer.fill, 4) == texture);
    assert(fill_channel_texture(previous.fill, 1) == texture);
    TextureLayer fresh;
    apply_blend_uniform_channels(fresh, values, true);
    assert((fresh.channels == std::array<bool, 5>{true, true, true, false, false}) &&
           !fresh.ao_enabled);
  }
  SceneSnapshot scene;
  ImportSummary imported;
  for (int uid = 1; uid <= 2; ++uid) {
    MaterialSummary material;
    material.session_id = uid;
    material.name = uid == 1 ? "Cube" : "Sphere";
    material.uniform_editable = true;
    scene.materials.push_back(material);
    imported.materials.push_back({std::uint32_t(uid), material.name, {1001}});
  }
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument doc(options, imported, scene);
  const auto a = doc.set(1).id, b = doc.set(2).id;
  const std::array bindings{TextureSetBinding{a, 1, "Cube"}, TextureSetBinding{b, 2, "Sphere"}};
  const auto original = doc.to_json();
  auto earlier = doc;
  auto shaders = doc.shader_instances();
  const auto main = shaders.default_instance;
  shaders.rename(main, "Shared");
  shaders.parameters(main, {2, .75f});
  const auto cube = shaders.create(main, "Cube shader");
  shaders.assign(a, cube);
  shaders.parameters(cube, {0, .25f});
  doc.set_shader_instances(shaders);
  assert(doc.set(1).shader_parameters.normal_intensity == 0 &&
         doc.set(2).shader_parameters.normal_intensity == 2);
  assert(doc.to_json()["texture_sets"] == original["texture_sets"]);
  const auto saved = doc.to_json();
  assert(saved["schema"] == texture_document_schema);
  auto opened = TextureDocument::from_json(saved, bindings);
  assert(opened.to_json() == saved && opened.shader_instances() == shaders);
  assert(opened.set(2).shader_parameters.ao_intensity == .75f);
  auto v58 = saved;
  v58["schema"] = 58;
  auto old_shaders = v58["shader_instances"].toObject();
  old_shaders["version"] = 1;
  auto entries = old_shaders["instances"].toArray();
  for (int i = 0; i < entries.size(); ++i) {
    auto instance = entries[i].toObject();
    auto p = instance["parameters"].toObject();
    p.remove("subsurface");
    instance["parameters"] = p;
    entries[i] = instance;
  }
  old_shaders["instances"] = entries;
  v58["shader_instances"] = old_shaders;
  auto from58 = TextureDocument::from_json(v58, bindings);
  assert(from58.to_json()["texture_sets"] == saved["texture_sets"]);
  assert(from58.set(2).shader_parameters.normal_intensity == 2 &&
         from58.set(2).shader_parameters.subsurface == SubsurfaceParameters{});
  auto legacy = saved;
  legacy["schema"] = 57;
  legacy.remove("shader_instances");
  auto old = TextureDocument::from_json(legacy, bindings);
  assert(old.shader_instances().for_set(a).id == old.shader_instances().for_set(b).id);
  assert(old.shader_instances().for_set(a).name == "Main shader");
  assert(old.set(1).shader_parameters == ShaderParameters{});
  assert(old.to_json()["texture_sets"] == saved["texture_sets"]);
  auto corrupt = saved;
  auto invalid = shaders;
  invalid.assignments.erase(a);
  const auto before = doc.to_json();
  bool failed = false;
  try {
    doc.set_shader_instances(invalid);
  }
  catch (...) {
    failed = true;
  }
  assert(failed && doc.to_json() == before);
  corrupt.remove("shader_instances");
  failed = false;
  try {
    TextureDocument::from_json(corrupt, bindings);
  }
  catch (...) {
    failed = true;
  }
  assert(failed);
  earlier.preserve_shader_instances(doc.shader_instances());
  assert(earlier.shader_instances() == shaders &&
         earlier.to_json()["texture_sets"] == original["texture_sets"]);
  auto compatible = doc.shader_instances();
  auto profile = compatible.for_set(a).parameters;
  profile.openpbr.emplace();
  profile.openpbr->set("base_weight", {.4f, 0, 0});
  compatible.parameters(compatible.assignments.at(a), profile);
  auto portable = doc;
  portable.set_shader_instances(compatible);
  auto compatibility_open = TextureDocument::from_json(portable.to_json(), bindings);
  assert(compatibility_open.to_json() == portable.to_json() &&
         compatibility_open.set(1).shader_parameters.openpbr == profile.openpbr &&
         compatibility_open.to_json()["texture_sets"] == doc.to_json()["texture_sets"]);

  auto read_only = portable;
  read_only.set(1).uniform_editable = false;
  auto read_only_json = read_only.to_json();
  auto read_only_open = TextureDocument::from_json(read_only_json, bindings);
  assert(!read_only_open.set(1).uniform_editable && read_only_open.set(2).uniform_editable &&
         read_only_open.to_json() == read_only_json);
  auto legacy72 = read_only_json;
  legacy72["schema"] = 72;
  auto from72 = TextureDocument::from_json(legacy72, bindings);
  assert(from72.set(1).uniform_editable && from72.set(2).uniform_editable);
  auto invalid_editable = read_only_json;
  auto invalid_sets = invalid_editable["texture_sets"].toArray();
  auto invalid_set = invalid_sets[0].toObject();
  invalid_set["uniform_editable"] = "false";
  invalid_sets[0] = invalid_set;
  invalid_editable["texture_sets"] = invalid_sets;
  failed = false;
  try {
    TextureDocument::from_json(invalid_editable, bindings);
  }
  catch (...) {
    failed = true;
  }
  assert(failed);
  auto blend_document = doc;
  BlendMeshSelection blend_selection{{"Body", "Öga"}, {"Character"}};
  blend_document.set_blend_options(blend_selection);
  auto blend_saved = blend_document.to_json();
  auto blend_open = TextureDocument::from_json(blend_saved, bindings);
  assert(blend_open.settings().blend == blend_selection && blend_open.to_json() == blend_saved);
  auto blend_legacy = blend_saved;
  blend_legacy["schema"] = 73;
  auto old_blend = TextureDocument::from_json(blend_legacy, bindings);
  assert(old_blend.settings().blend.empty());
  auto sets = doc.texture_sets();
  TextureSet fresh;
  fresh.material_session_uid = 3;
  fresh.name = "New set";
  const auto c = fresh.id;
  sets.push_back(fresh);
  doc.stage_texture_sets(sets);
  assert(doc.shader_instances().for_set(c).id == main &&
         doc.set(3).shader_parameters.normal_intensity == 2);
  std::puts(
      "shader document: schema59 packed metadata, legacy58/57 defaults, exact layer data, runtime mirrors, transactional rejection and independent painting-history state PASS");
}
