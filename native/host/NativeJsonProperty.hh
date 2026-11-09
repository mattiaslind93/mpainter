#pragma once
#include "ResourceAvailability.hh"
#include <QtCore>  // Qt containers must precede Blender's AT debugging macro.
#include "BKE_idprop.hh"
#include "DNA_ID.h"
#include "MEM_guardedalloc.h"
#include <cstring>
namespace bts {
inline int native_json_string_size(std::uint64_t bytes, const char *purpose)
{
  // IDProperty stores the payload plus its terminator in a signed int.
  if (bytes >= std::uint64_t(std::numeric_limits<int>::max()) ||
      bytes > std::uint64_t(QByteArray::maxSize()))
    throw std::length_error(std::string(purpose) + ": JSON exceeds native string length storage");
  return int(bytes);
}
inline QByteArray native_json_property_bytes(const blender::IDProperty &property,
                                             const char *purpose)
{
  if (property.type != blender::IDP_STRING || property.subtype != blender::IDP_STRING_SUB_UTF8 ||
      property.len < 1 || property.totallen < property.len || !property.data.pointer)
    throw std::invalid_argument(std::string(purpose) + ": invalid native string metadata");
  // These properties are owned by Blender's allocator. Check their declared span
  // before inspecting bytes; never use an unbounded C-string constructor here.
  const auto *data = static_cast<const char *>(property.data.pointer);
  if (MEM_allocN_len(data) < std::size_t(property.totallen))
    throw std::invalid_argument(std::string(purpose) + ": invalid native string span");
  const int size = native_json_string_size(std::uint64_t(property.len - 1), purpose);

  if (data[size] != '\0' || std::memchr(data, '\0', std::size_t(size)))
    throw std::invalid_argument(std::string(purpose) + ": invalid native string terminator");
  return QByteArray(data, size);
}
inline blender::IDProperty *new_native_json_property(const QByteArray &bytes,
                                                     const char *name,
                                                     const char *purpose)
{
  const int size = native_json_string_size(std::uint64_t(bytes.size()), purpose);
  if (bytes.contains('\0'))
    throw std::invalid_argument(std::string(purpose) + ": embedded native string terminator");

  return blender::IDP_NewString(blender::StringRef(bytes.constData(), size), name);
}
}  // namespace bts
