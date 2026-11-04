#pragma once
namespace blender {struct Main;struct Material;struct bNode;}
namespace bts {
// Nonmutating, owner-side export/reconstruction gate. The managed four surface
// inputs have their own topology/model gate. Every other input must be unlinked
// and retain its ACTUAL pinned declaration default; no implicit surface loss.
void require_material_parameter_surface(const blender::Main &,const blender::Material &);
// Exact native mesh-UV ImageUser/TexMapping contract; caller checks node ownership
// before invoking. Settings only: no Image buffers, GPU submission or mutation.
void require_material_parameter_mask_sampling(const blender::bNode &);
}
