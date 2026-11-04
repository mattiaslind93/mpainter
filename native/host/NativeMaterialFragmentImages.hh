#pragma once
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialSourcePolicy.hh"
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
namespace blender {struct Image;struct ObjectMatrices;struct ObjectInfos;}
namespace bts {
class NativeFragmentGeometry;struct NativeFragmentGeometryObject;
// Stock shader reads these exact captured model matrices, never a UV-derived normal.
blender::ObjectMatrices native_fragment_image_object_matrices(const NativeFragmentGeometryObject &,const blender::ObjectInfos &);
// Exact stock transform_utils world-to-object normal math. Restrict the
// whole-library dependency: its unused view transforms need unbound matrices.
inline std::string_view native_fragment_image_object_matrices_source(){return "ObjectMatrices object_matrices_get(){return native_object_matrices[0];}\nvoid normal_transform_world_to_object(vec3 vin,out vec3 vout){const ObjectMatrices obj=object_matrices_get();vout=vin*mat3(obj.model);}\nvoid normal_transform_object_to_world(vec3 vin,out vec3 vout){const ObjectMatrices obj=object_matrices_get();vout=vin*mat3(obj.model_inverse);}\n";}
inline bool native_fragment_image_keep_dependency(std::string_view dependency,bool box,std::string_view code){
 if(dependency!="gpu_shader_material_transform_utils.glsl")return true;
 const auto reject=[](){throw NativeFragmentContextError(NativeFragmentContextCode::Images,"Fragment stock transform dependency requires a separately admitted physical binding");};
 if(!box)reject();
 for(const auto prefix:{"normal_transform_","direction_transform_","point_transform_"})for(std::size_t at=0;(at=code.find(prefix,at))!=std::string_view::npos;){auto end=at;while(end<code.size()&&(std::isalnum(static_cast<unsigned char>(code[end]))||code[end]=='_'))++end;if(code.substr(at,end-at)!="normal_transform_world_to_object"&&code.substr(at,end-at)!="normal_transform_object_to_world")reject();at=end;}
 return false;
}
// Optional immutable owner allocations: shared by production and CPU callbacks.
template<class Owner>class NativeFragmentImageOwnerCache {
 std::vector<Owner> entries_;
 public:
 template<class Acquire>const Owner &prepare(std::size_t index,const Acquire &acquire){if(index>=entries_.size())entries_.resize(index+1);auto &entry=entries_[index];if(!entry)entry=acquire();return entry;}
 const Owner &at(std::size_t index)const{return entries_.at(index);}
 bool empty()const{return entries_.empty();}
 void clear(){entries_.clear();}
};
enum class NativeFragmentImagePrecision {Unsupported,Float16Rgba,Float32Rgba};
inline bool native_fragment_geometry_layout_changed(std::string_view old_uv,bool old_generated,std::string_view uv,bool generated){return old_uv!=uv||old_generated!=generated;}
struct NativeFragmentImageBinding {blender::Image *image=nullptr;std::shared_ptr<NativeMaterialImageResource> decoded;std::shared_ptr<const NativeMaterialImageTileSet> tiled;bool tiled_mapping=false,first_tile_2d=false;};
// Entry/program-owned snapshot, never stored back in Graph (no ownership cycle).
// The strong source retains Image IDs; decoded resources share existing pixels.
class NativeFragmentImageContext {
 std::shared_ptr<NativeMaterialGraph> source_;
 std::vector<NativeFragmentImageBinding> bindings_;
 std::string signature_;
 bool default_uv_=false,object_matrices_=false;
 public:
 static NativeFragmentImageContext capture(const PreparedNativeFragmentNormal &,bool require_decoded=true);
 const std::vector<NativeFragmentImageBinding> &bindings()const{return bindings_;}
 const std::shared_ptr<NativeMaterialGraph> &source()const{return source_;}
 const std::string &signature()const{return signature_;}
 bool requires_default_uv()const{return default_uv_;}
 bool requires_object_matrices()const{return object_matrices_;}
 void validate_storage(blender::Image *,int width,int height,bool single_2d,NativeFragmentImagePrecision)const;
 void validate_uv(const NativeFragmentGeometry &,std::string_view source_uv)const;
};
}
