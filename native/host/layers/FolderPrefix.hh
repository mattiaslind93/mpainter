// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TextureDocument.hh"
namespace bts {
// CPU Flatten/export reference only: stop after the selected prefix at its own
// depth, retaining ancestor modes for contextual Folder entry. No GPU/UI caller.
UniformComposite folder_prefix_composite(
    const TextureSet &, const QUuid &, int x, int y, int tile);
std::array<float, 2> folder_prefix_user(
    const TextureSet &, const QUuid &, int slot, int x, int y, int tile);
std::array<float, 2> folder_prefix_displacement(
    const TextureSet &, const QUuid &, int x, int y, int tile);
}  // namespace bts
