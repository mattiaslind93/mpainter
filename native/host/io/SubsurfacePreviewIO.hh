// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SubsurfaceSettings.hh"
namespace blender {
struct Main;
struct Scene;
}  // namespace blender
namespace bts {
SubsurfacePreview subsurface_preview(const blender::Scene &);
void set_subsurface_preview(blender::Main &, blender::Scene &, SubsurfacePreview);
}  // namespace bts
