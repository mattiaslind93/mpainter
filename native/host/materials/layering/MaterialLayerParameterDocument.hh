// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialLayerParameterIO.hh"
#include "TextureDocument.hh"
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <utility>

namespace bts {
// Detached reconstruction only. The caller supplies the actual native-surface
// admission and shared decoder, then publishes this document through the usual
// revision-checked document/GPU path. This does not add an interactive Import.
inline TextureDocument reconstruct_material_layer_document(
    const TextureDocument &document,
    std::uint32_t material_uid,
    const MaterialLayerParameters &parameters,
    const QString &working_space,
    const std::array<double, 9> &scene_linear_to_xyz,
    const MaterialParameterDecoder &decode,
    const MaterialParameterProcessor &processor,
    const MaterialParameterTargetValidator &validate_target,
    const MaterialParameterColorSequenceValidator &validate_sequence = {})
{
  const auto target = std::find_if(
      document.texture_sets().begin(),
      document.texture_sets().end(),
      [material_uid](const TextureSet &set) { return set.material_session_uid == material_uid; });
  if (target == document.texture_sets().end()) {
    throw std::invalid_argument("Parameter reconstruction target no longer exists");
  }

  auto restored = reconstruct_material_layer_parameters(*target,
                                                        parameters,
                                                        working_space,
                                                        scene_linear_to_xyz,
                                                        decode,
                                                        processor,
                                                        validate_target,
                                                        validate_sequence);
  auto shaders = document.shader_instances();
  const auto original_shader = shaders.assignments.at(target->id);
  const QString prefix = QStringLiteral("Imported materials");
  QString name = prefix;
  std::set<QString> names;
  for (const auto &[id, shader] : shaders.instances) {
    names.insert(shader.name);
  }
  // Names are local presentation metadata, never portable instance identities.
  for (std::size_t suffix = 2; names.contains(name); ++suffix) {
    name = prefix + " " + QString::number(qulonglong(suffix));
  }
  const auto local_shader = shaders.create(original_shader, name);
  shaders.parameters(local_shader, parameters.shader);
  shaders.assign(target->id, local_shader);

  auto candidate = document;
  candidate.set(material_uid) = std::move(restored);
  // A TextureSet's cached shader_parameters alone is insufficient: ordinary
  // refresh and Save/Open resolve it from this document-level assignment.
  candidate.set_shader_instances(std::move(shaders));
  return candidate;
}
}  // namespace bts
