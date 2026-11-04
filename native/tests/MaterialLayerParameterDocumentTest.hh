#pragma once
#include "MaterialLayerParameterDocument.hh"
#include <stdexcept>
#include <vector>

namespace bts {
// Invoked with real decoded package originals by the native CPU fixture. The
// renderer/cold-Open acceptance remains a separate native runtime control.
inline QJsonObject verify_material_layer_parameter_document(
    const TextureDocument &source,
    std::uint32_t material_uid,
    const MaterialLayerParameters &parameters,
    const MaterialParameterDecoder &decode,
    const MaterialParameterProcessor &processor,
    const MaterialParameterTargetValidator &native_gate)
{
  const auto check = [](bool passed, const char *message) {
    if (!passed) {
      throw std::runtime_error(message);
    }
  };
  check(source.texture_sets().size() >= 2, "Document test needs an unrelated Texture Set");
  const auto before = source.to_json();
  const auto shaders_before = source.shader_instances();
  const auto target = std::find_if(
      source.texture_sets().begin(), source.texture_sets().end(),
      [material_uid](const TextureSet &set) {
        return set.material_session_uid == material_uid;
      });
  check(target != source.texture_sets().end(), "Document test target missing");
  const auto old_shader = shaders_before.assignments.at(target->id);
  auto result = reconstruct_material_layer_document(
      source, material_uid, parameters, parameters.working_space,
      parameters.scene_linear_to_xyz, decode, processor, native_gate);
  check(source.to_json() == before, "Reconstruction modified source document");
  const auto &library = result.shader_instances();
  const auto local = library.assignments.at(target->id);
  check(local != old_shader && !shaders_before.instances.contains(local),
        "Reconstruction reused a source shader identity");
  check(library.default_instance == shaders_before.default_instance,
        "Reconstruction changed the default shader");
  check(library.instances.size() == shaders_before.instances.size() + 1,
        "Reconstruction did not create exactly one shader");
  for (const auto &[id, original] : shaders_before.instances) {
    check(library.instances.at(id) == original, "Existing shader was modified");
  }
  std::size_t unrelated_assignments = 0;
  for (const auto &[id, original] : shaders_before.assignments) {
    if (id != target->id) {
      check(library.assignments.at(id) == original, "Unrelated shader assignment changed");
      ++unrelated_assignments;
    }
  }
  check(library.for_set(target->id).parameters == parameters.shader,
        "Assigned shader lost exported parameters");
  check(result.set(material_uid).shader_parameters == parameters.shader,
        "Texture Set did not adopt assigned shader parameters");

  std::vector<TextureSetBinding> bindings;
  for (const auto &set : result.texture_sets()) {
    bindings.push_back({set.id, set.material_session_uid, set.name});
  }
  const auto encoded = result.to_json();
  const auto set_metadata = [](const QJsonObject &value, const QUuid &id) {
    for (const auto &entry : value["texture_sets"].toArray()) {
      const auto set = entry.toObject();
      if (QUuid(set["id"].toString()) == id) return set;
    }
    throw std::runtime_error("Document test Texture Set metadata missing");
  };
  for (const auto &set : source.texture_sets()) {
    if (set.id != target->id) {
      check(set_metadata(encoded, set.id) == set_metadata(before, set.id),
            "Reconstruction modified an unrelated Texture Set");
    }
  }
  check(unrelated_assignments > 0, "Document test did not exercise an unrelated assignment");
  auto restored = TextureDocument::from_json(encoded, bindings);
  check(restored.to_json() == encoded, "Reconstructed metadata roundtrip differs");
  check(restored.shader_instances().assignments.at(target->id) == local,
        "Local shader identity changed across metadata roundtrip");
  check(restored.set(material_uid).shader_parameters == parameters.shader,
        "Roundtrip or refresh restored the old shader parameters");
  // An ordinary refresh must not restore the old target's shader parameters.
  restored.set_shader_instances(restored.shader_instances());
  check(restored.set(material_uid).shader_parameters == parameters.shader,
        "Roundtrip or refresh restored the old shader parameters");
  return {{"fresh_local_shader_instance", true},
          {"unrelated_texture_sets_checked", qint64(unrelated_assignments)},
          {"unrelated_shader_assignments_unchanged", true},
          {"source_document_unchanged", true},
          {"metadata_roundtrip_and_refresh_preserve_parameters", true}};
}
}  // namespace bts
