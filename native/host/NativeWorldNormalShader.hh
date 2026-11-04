#pragma once
#include <array>

namespace blender {struct Main;struct bNodeTree;struct Image;struct bNode;}
namespace bts {
struct TextureSet;

// Fixed private shader topology. Images are signed, associated WORLD vectors
// with identical front/back coverage, Float32 storage and Non-Color sampling.
// Installation creates metadata only; it never decodes pixels or allocates GPU
// targets. Before evaluating this graph the GPU owner MUST install even the
// inactive zero placeholders (including the UDIM map). enabled=0 is a uniform,
// not a guarantee that Blender will avoid evaluating the Image Texture inputs.
// The owner replaces placeholders on admitted activation and retires full-size
// storage when disabled, without changing these Image IDs or sampler types.
// Central material admission must reserve the two extra samplers (and tiled
// image mappings); this helper does not waive the existing shader capacity cap.
void install_native_world_normal_shader(
    blender::Main &,blender::bNodeTree &,const TextureSet &,
    std::array<blender::Image *,2> &,blender::bNode *&enabled,
    blender::bNode &legacy_normal,blender::bNode &principled);

// Metadata/topology only; no GPU calls, buffer acquisition or pixel comparison.
// Equal front/back alpha is enforced by the pair producer, not by a readback.
void validate_native_world_normal_shader(
    const blender::Main &,const blender::bNodeTree &,const TextureSet &,
    std::array<blender::Image *,2>,const blender::bNode &legacy_normal,
    const blender::bNode &principled);
}
