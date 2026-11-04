#pragma once
#include "SubsurfaceSettings.hh"
namespace blender {struct Main;struct Scene;}
namespace bts {
SubsurfacePreview subsurface_preview(const blender::Scene &);
void set_subsurface_preview(blender::Main &,blender::Scene &,SubsurfacePreview);
}
