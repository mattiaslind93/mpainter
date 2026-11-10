// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <map>
namespace blender {
struct Mesh;
}
namespace bts {
using SourceMeshMap = std::map<std::uint32_t, const blender::Mesh *>;
}
