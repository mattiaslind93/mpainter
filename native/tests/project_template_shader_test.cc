// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProjectTemplate.hh"
#include <QCryptographicHash>
#include <QJsonArray>
#include <cassert>
#include <cstdio>
using namespace bts;
namespace {
TextureDocument project(std::initializer_list<std::pair<unsigned, const char *>> names)
{
  SceneSnapshot scene;
  ImportSummary imported;
  for (auto [uid, name] : names) {
    MaterialSummary material;
    material.session_id = uid;
    material.name = name;
    material.uniform_editable = true;
    scene.materials.push_back(material);
    imported.materials.push_back({uid, name, {1001}});
  }
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  return TextureDocument(options, imported, scene);
}
QJsonObject make(const TextureDocument &source)
{
  const QByteArray bytes = "template-parser fixture, native test uses a real HDR";
  const QJsonObject resource{
      {"name", "fixture.hdr"},
      {"data", QString::fromLatin1(bytes.toBase64())},
      {"sha256",
       QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
  return make_project_template(
      source,
      1,
      {{"view_transform", "Standard"}},
      {{"rotation", 0}, {"exposure", 0}, {"opacity", 100}, {"blur", 0}, {"alignment", "Local"}},
      resource,
      {});
}
}  // namespace
int main()
{
  auto source = project({{1, "Cube"}, {2, "Sphere"}});
  auto library = source.shader_instances();
  const auto main = library.default_instance;
  const auto selected = library.create(main, "Coated compatibility");
  library.assign(source.set(1).id, selected);
  auto parameters = library.instances.at(selected).parameters;
  parameters.normal_intensity = .75f;
  parameters.openpbr.emplace();
  parameters.openpbr->set("coat_weight", {.8f, 0, 0});
  library.parameters(selected, parameters);
  source.set_shader_instances(library);
  const auto before = source.to_json(), data = make(source);
  assert(source.to_json() == before && data["version"] == 2 && data["shader"] == "mixed");
  auto target = project({{10, "Cube"}, {20, "Sphere"}, {30, "New material"}});
  const auto id = target.id(), a = target.set(10).selected_layer,
             b = target.set(20).selected_layer, c = target.set(30).selected_layer;
  apply_project_template(target, data);
  const auto &state = target.shader_instances();
  const auto ca = state.for_set(target.set(10).id), cb = state.for_set(target.set(20).id),
             cc = state.for_set(target.set(30).id);
  assert(target.id() == id && target.set(10).selected_layer == a &&
         target.set(20).selected_layer == b && target.set(30).selected_layer == c);
  assert(ca.id == cc.id && ca.id != cb.id && ca.id != selected && cb.id != main &&
         state.default_instance == ca.id);
  assert(ca.parameters == parameters && !cb.parameters.openpbr &&
         ca.name == "Coated compatibility" && cb.name == "Main shader");
  assert(target.set(10).shader_parameters == parameters &&
         target.set(30).shader_parameters == parameters &&
         target.set(20).shader_parameters == ShaderParameters{});
  for (const auto &set : target.texture_sets())
    assert(set.layers.size() == 1 && !TextureDocument::has_pixels(set));
  const auto applied = target.to_json();
  apply_project_template(target, data);
  assert(target.to_json() == applied);  // stable destination identities
  std::array bindings{TextureSetBinding{target.set(10).id, 10, "Cube"},
                      TextureSetBinding{target.set(20).id, 20, "Sphere"},
                      TextureSetBinding{target.set(30).id, 30, "New material"}};
  assert(TextureDocument::from_json(applied, bindings).to_json() == applied);
  auto legacy = data;
  legacy["version"] = 1;
  legacy["shader"] = "principled-metallic-roughness";
  auto migrated = project({{40, "Other mesh"}});
  apply_project_template(migrated, legacy);
  assert(migrated.set(40).shader_parameters == parameters);
  auto bad = data;
  bad["shader"] = "principled-metallic-roughness";
  bool failed = false;
  try {
    apply_project_template(target, bad);
  }
  catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed && target.to_json() == applied);
  bad = data;
  bad["shader"] = "openpbr-1.1";
  failed = false;
  try {
    validate_project_template(bad);
  }
  catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed);
  auto sets = target.texture_sets();
  TextureSet fresh;
  fresh.material_session_uid = 50;
  fresh.name = "Reimported set";
  const auto added = fresh.id;
  sets.push_back(fresh);
  target.stage_texture_sets(sets);
  assert(target.shader_instances().for_set(added).parameters == parameters);
  assert(builtin_project_template(":pbr") && builtin_project_template(":openpbr-compatibility") &&
         !builtin_project_template("") && !builtin_project_template("openpbr-1.1"));
  apply_builtin_project_template(target, ":pbr");
  for (const auto &set : target.texture_sets())
    assert(set.shader_parameters == ShaderParameters{});
  apply_builtin_project_template(target, ":openpbr-compatibility");
  for (const auto &set : target.texture_sets())
    assert(set.shader_parameters.openpbr == OpenPbrParameters{});
  const auto builtin = target.to_json();
  apply_builtin_project_template(target, "");
  assert(target.to_json() == builtin);
  failed = false;
  try {
    apply_builtin_project_template(target, ":unknown");
  }
  catch (const std::invalid_argument &) {
    failed = true;
  }
  assert(failed && target.to_json() == builtin);
  auto only = project({{1, "Cube"}});
  apply_builtin_project_template(only, ":openpbr-compatibility");
  assert(make(only)["shader"] == "openpbr-1.1-principled-compatibility");
  apply_builtin_project_template(only, ":pbr");
  assert(make(only)["shader"] == "principled-metallic-roughness");
  std::puts(
      "Template v2/v1: shader profiles, sharing/independence, project-local deterministic IDs, matched/fallback/default inheritance, untouched layers, exact project roundtrip, atomic invalid roots, explicit compatibility/PBR builtin and no full-BSDF alias PASS");
}
