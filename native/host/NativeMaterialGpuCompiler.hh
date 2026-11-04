#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace blender {struct bNodeTree;struct GPUMaterial;}
namespace bts {
// Static code-generation layout. Values change through the parameter buffer,
// never through this cache key. Diagnostic fixture coordinates are opt-in.
struct NativeMaterialCompilation {
 blender::bNodeTree *tree=nullptr;
 std::string generated;
 std::size_t parameter_count=1;
 bool fixture_context=false;
 std::string error;
 double displacement_scale=1;
 std::vector<std::size_t> parameter_slots;
 bool surface_context=false;
 bool surface_required=false;
 bool mapping_required=false;
 std::size_t logical_parameter_count()const{return parameter_slots.empty()?parameter_count:parameter_slots.size();}
};
// Native owner context only. Returned material is owned by the caller, including
// rejected compilations: inspect error, then GPU_material_free_single.
blender::GPUMaterial *compile_native_material(NativeMaterialCompilation &);
}
