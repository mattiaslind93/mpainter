#pragma once
#include "OpenPbrParameters.hh"
#include <optional>
namespace blender {struct Main;struct Material;}
namespace bts {
// Standard Blender nodes only. Explicit compatibility subset, not OpenPBR BSDF conformance.
bool apply_openpbr_shader(blender::Main &,blender::Material &,const std::optional<OpenPbrParameters> &,bool subsurface_preview);
QJsonObject openpbr_shader_state(blender::Material &);
}
