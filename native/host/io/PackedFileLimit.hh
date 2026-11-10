// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace bts {
inline void require_packed_file_size(std::uint64_t bytes, const char *resource)
{
  if (bytes >= (std::uint64_t(1) << 31))
    throw std::length_error(
        std::string(resource) +
        " is 2 GiB or larger. Save refused: Blender packed resources must be smaller than 2 GiB. Reduce the resource or split the project.");
}
}  // namespace bts
