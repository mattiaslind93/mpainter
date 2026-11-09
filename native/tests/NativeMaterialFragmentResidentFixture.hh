// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Gated diagnostic fixture only: actual privately owned mesh capture, then stock
// worker preparation after all source Mesh/Main pointers are destroyed.
#include "NativeFragmentGeometryTest.hh"
#include <QScopeGuard>
#include "NativeMaterialFragmentNormalMapFixture.hh"
namespace bts {
struct NativeFragmentResidentFixtures {
 std::shared_ptr<const NativeFragmentGeometry> original,revised;
 PreparedNativeFragmentNormal named,render,bump;
 bool source_main_destroyed=false,cancelled_preparation_rejected=false;
};
namespace native_fragment_resident_test {
inline PreparedNativeFragmentNormal normal(const char *name){
 auto source=native_fragment_normal_map_test::source();
 for(auto &tree:source.main->nodetrees)for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeNormalMap"))blender::STRNCPY(static_cast<blender::NodeShaderNormalMap *>(node.storage)->uv_map,name);
 std::atomic_bool cancel=false;return prepare_native_fragment_normal(std::move(source),cancel);
}
inline PreparedNativeFragmentNormal bump(){
 auto source=native_fragment_gpu_test::source(.5f,false,false,"named");
 for(auto &node:source.material->nodetree->nodes)if(STREQ(node.idname,"ShaderNodeUVMap"))blender::STRNCPY(static_cast<blender::NodeShaderUVMap *>(node.storage)->uv_map,"NamedUV");
 std::atomic_bool cancel=false;return prepare_native_fragment_normal(std::move(source),cancel);
}
inline std::shared_ptr<const NativeFragmentResidentFixtures> prepare(){
 using namespace blender;using native_fragment_geometry_test::require;
 auto result=std::make_shared<NativeFragmentResidentFixtures>();std::atomic_bool cancel=false;
 std::shared_ptr<const NativeFragmentGeometrySource> original,revised;
 {
  native_fragment_geometry_test::Fixture scene;SourceMeshMap overrides;std::vector<Mesh *> owned;
  const auto release=qScopeGuard([&]{for(auto *mesh:owned)BKE_id_free_ex(nullptr,mesh,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT,true);});
  for(std::size_t oi=0;oi<scene.meshes.size();++oi){
   auto *mesh=BKE_mesh_copy_for_eval(*scene.meshes[oi]);owned.push_back(mesh);overrides.emplace(scene.ids[oi],mesh);auto uv=mesh->attributes_for_write().lookup_for_write_span<float2>("DisplayUV");require(bool(uv),"Resident target UV fixture missing");
   for(auto &v:uv.span){v.x=(oi?2.1f:.7f)+.6f*v.x;v.y=.1f+.6f*v.y;}uv.finish();if(oi==0){mesh->vert_positions_for_write()[0].x=.25f;mesh->tag_positions_changed();}
  }
  NativeFragmentGeometryRequest request;request.revision=41;request.material_uid=scene.material->id.session_uid;request.tiles={1001,1002,1003};request.uv_requests={{NativeFragmentUvKind::Named,"NamedUV"},{NativeFragmentUvKind::Named,"RenderUV"}};
  original=capture_native_fragment_geometry(scene.context,nullptr,overrides,request,cancel);
  auto *mesh=owned[0];mesh->vert_positions_for_write()[1].z=.35f;mesh->tag_positions_changed();request.revision=42;revised=capture_native_fragment_geometry(scene.context,nullptr,overrides,request,cancel);
 }
 result->source_main_destroyed=true;
 return std::async(std::launch::async,[result,original=std::move(original),revised=std::move(revised)]{
  std::atomic_bool cancel=false;result->original=prepare_native_fragment_geometry(original,cancel);result->revised=prepare_native_fragment_geometry(revised,cancel);bool stopped=false;try{std::atomic_bool stop=true;prepare_native_fragment_geometry(revised,stop);}catch(const std::runtime_error &){stopped=true;}native_fragment_geometry_test::require(stopped,"Resident cancelled worker preparation admitted");result->cancelled_preparation_rejected=stopped;result->named=normal("NamedUV");result->render=normal("RenderUV");result->bump=bump();return std::shared_ptr<const NativeFragmentResidentFixtures>(result);
 }).get();
}
}
}
