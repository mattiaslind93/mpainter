#pragma once
#include "NativeMaterialGraph.hh"
#include "NativeMaterialRamp.hh"
#include "NativeMaterialMappingValidation.hh"

namespace bts {
// Layout admission is shared by detached packed-Open validation and the owner
// registry. Policy readiness remains a separate, actual prepared-state check.
inline void validate_native_material_instance(const NativeMaterialGraph &source,
                                              const NativeMaterialDescription &instance)
{
  instance.validate();
  const auto &layout = source.description;
  if (instance.parameters.size() != layout.parameters.size() || instance.group != layout.group ||
      instance.surface_type != layout.surface_type)
    throw std::invalid_argument("Native material parameter interface changed");
  for (std::size_t i = 0; i < instance.parameters.size(); ++i) {
    const auto &value = instance.parameters[i], &original = layout.parameters[i];
    if (value.identifier != original.identifier || value.type != original.type ||
        value.source_node != original.source_node ||
        value.source_socket != original.source_socket || value.mapping != original.mapping ||
        !native_material_ramp_compatible(value, original) ||
        (!value.mapping_transform.identity() && !original.mapping_transform_supported()) ||
        (!original.source_node.isEmpty() && value.value != original.value))
      throw std::invalid_argument(
          "Native material linked input change requires source preparation");
  }
  validate_native_mapping_instance(source, instance);
}
}  // namespace bts
