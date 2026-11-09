// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace blender {
struct Main;
struct Scene;
}  // namespace blender
namespace bts {
class TextureDocument;
void pack_skew_fields(blender::Main &, blender::Scene &, const TextureDocument &);
void read_skew_fields(const blender::Scene &, TextureDocument &, bool required);
}  // namespace bts
