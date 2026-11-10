// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeJsonProperty.hh"
#include <optional>
#include <string>
namespace bts {
// Metadata values are dynamic IDProperty strings. The fixed DNA color-space
// name buffer is a separate native storage boundary, not a metadata limit.
inline std::optional<std::string> image_color_metadata(const blender::IDProperty *metadata,
                                                       const char *key)
{
  if (!metadata)
    return {};
  const auto *property = blender::IDP_GetPropertyFromGroup(metadata, key);
  if (!property || property->type != blender::IDP_STRING)
    return {};
  const auto bytes = native_json_property_bytes(*property, "Image color metadata text");
  return std::string(bytes.constData(), std::size_t(bytes.size()));
}
}  // namespace bts
