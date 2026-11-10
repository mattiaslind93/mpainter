// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MeshMaps.hh"

namespace bts {
// Match the retained GPU map key, without reading pixels or hashing them again.
// Saved descriptor hashes are only meaningful while the plane is unresolved.
inline bool same_mesh_map_plane(const MeshMapPlane &a, const MeshMapPlane &b)
{
  if (a.resolution != b.resolution || a.rows() != b.rows() || bool(a.pixels) != bool(b.pixels))
    return false;
  if (!a.pixels)
    return a.hash == b.hash;
  if (a.pixels == b.pixels)
    return true;
  return a.pixels->resolution == b.pixels->resolution && a.pixels->height == b.pixels->height &&
         a.pixels->hash == b.pixels->hash;
}

inline unsigned changed_mesh_map_roles(const MeshMaps &before, const MeshMaps &after)
{
  unsigned changed = 0;
  for (unsigned role = 0; role < before.size(); ++role) {
    const auto &a = before[role], &b = after[role];
    bool equal = bool(a) == bool(b);
    if (equal && a) {
      equal = a->tiles.size() == b->tiles.size();
      if (equal) {
        auto i = a->tiles.begin(), j = b->tiles.begin();
        for (; i != a->tiles.end(); ++i, ++j)
          if (i->first != j->first || !same_mesh_map_plane(i->second, j->second)) {
            equal = false;
            break;
          }
      }
    }
    if (!equal)
      changed |= 1u << role;
  }
  return changed;
}
}  // namespace bts
