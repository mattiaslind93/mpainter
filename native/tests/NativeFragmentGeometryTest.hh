#pragma once
// Source-only focused native regression; register only in a coherent CPU owner.
// No host/GUI/GPU context is created here. Caller has initialized Blender + Qt.
#include "NativeFragmentGeometry.hh"
#include "NativeSurfaceBasis.hh"
#include "NativeMaterialContext.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "BKE_customdata.hh"
#include "NativeWorldNormalObjectContext.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include "NativeWorldNormalUv.hh"
#include <NativeMaterialFragmentGpuCompiler.hh>
#include "ResourceBudget.hh"
#include "BKE_attribute.hh"
#include "BKE_collection.hh"
#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_mesh_tangent.hh"
#include "BKE_object.hh"
#include "BKE_scene.hh"
#include "BLI_math_vector.hh"
#include "BLI_hash.h"
#include "DEG_depsgraph_query.hh"
#include <limits>
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DNA_mesh_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include "draw_shader_shared.hh"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
namespace bts::native_fragment_geometry_test {
inline void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
inline auto worker(auto function){return std::async(std::launch::async,std::move(function)).get();}
inline std::uint64_t tiny_ram(){return 1;}
inline thread_local std::uint64_t simulated_available_ram=0;
inline std::uint64_t simulated_ram(){return simulated_available_ram;}
struct Fixture {
 blender::Main *main=blender::BKE_main_new();blender::Scene *scene=nullptr;blender::bContext *context=nullptr;
 blender::Material *material=nullptr;std::vector<blender::Mesh *> meshes;std::vector<std::uint32_t> ids;
 Fixture(){
  using namespace blender;scene=BKE_scene_add(main,"Fragment geometry owned fixture");material=BKE_material_add(main,"Two objects one material");
  for(int object_index=0;object_index<2;++object_index){
   auto *object=BKE_object_add_only_object(main,OB_MESH,object_index?"Negative nonuniform owner":"Positive nonuniform owner");auto *mesh=BKE_mesh_add(main,"Exact corner geometry");object->data=&mesh->id;auto *temporary=BKE_mesh_new_nomain(4,0,2,6);
   const float3 points[]={{0,0,0},{2,0,0},{0,1,0},{2,1,.2f}};std::copy_n(points,4,temporary->vert_positions_for_write().begin());
   const int offsets[]={0,3,6},corners[]={0,1,2,1,3,2};std::copy_n(offsets,3,temporary->face_offsets_for_write().begin());std::copy_n(corners,6,temporary->corner_verts_for_write().begin());bke::mesh_calc_edges(*temporary,false,false);
   const float2 named[]={{.1f,.1f},{1.1f,.1f},{.1f,1.1f},{1.1f,.1f},{1.1f,1.1f},{.1f,1.1f}};
   for(const char *name:{"NamedUV","RenderUV","DisplayUV"}){
    auto uv=temporary->attributes_for_write().lookup_or_add_for_write_only_span<float2>(name,bke::AttrDomain::Corner);
    for(int i=0;i<6;++i){uv.span[i]=named[i];if(std::string_view(name)!="DisplayUV")uv.span[i]=uv.span[i]*.5f+float2(.2f);if(std::string_view(name)=="RenderUV"||(std::string_view(name)=="DisplayUV"&&object_index==0))uv.span[i].x=1.2f-uv.span[i].x;}
    uv.finish();
   }
   temporary->uv_maps_default_set("RenderUV");temporary->uv_maps_active_set("DisplayUV");
   auto sharp=temporary->attributes_for_write().lookup_or_add_for_write_only_span<bool>("sharp_face",bke::AttrDomain::Face);sharp.span.fill(true);sharp.finish();
   BKE_mesh_nomain_to_mesh(temporary,mesh,object);mesh->uv_maps_default_set("RenderUV");mesh->uv_maps_active_set("DisplayUV");BKE_object_material_assign(main,object,material,1,BKE_MAT_ASSIGN_OBDATA);BKE_collection_object_add(main,scene->master_collection,object);
   object->loc[0]=object_index?4.f:.5f;object->loc[1]=object_index?1.f:0.f;object->scale[0]=object_index?-1.f:2.f;object->scale[1]=object_index?2.f:3.f;object->scale[2]=1.f;object->color[0]=object_index?.75f:.25f;object->color[1]=.4f;object->color[2]=.6f;object->color[3]=object_index?.8f:.3f;meshes.push_back(mesh);ids.push_back(object->id.session_uid);
  }
  BKE_scene_set_background(main,scene);DEG_relations_tag_update(main);context=CTX_create();CTX_data_main_set(context,main);CTX_data_scene_set(context,scene);CTX_data_ensure_evaluated_depsgraph(context);
 }
 ~Fixture(){if(scene)blender::BKE_scene_free_depsgraph_hash(scene);if(context)blender::CTX_free(context);if(main)blender::BKE_main_free(main);}
 Fixture(const Fixture &)=delete;Fixture &operator=(const Fixture &)=delete;
};
// A Generated stream is an optional, separately owned corner payload. It must
// never be reconstructed from physical P or mutate an artist's source Mesh.
struct OrcoMeshes {
 std::vector<blender::Mesh *> meshes;
 blender::Mesh *copy(const blender::Mesh &mesh){auto *value=blender::BKE_mesh_copy_for_eval(mesh);meshes.push_back(value);return value;}
 ~OrcoMeshes(){for(auto *mesh:meshes){mesh->texcomesh=nullptr;blender::BKE_id_free_ex(nullptr,mesh,blender::LIB_ID_FREE_NO_MAIN|blender::LIB_ID_FREE_NO_USER_REFCOUNT,true);}}
};
inline QJsonObject verify_generated_orco(){
 using namespace blender;
 static_assert(sizeof(NativeFragmentVertex)==80,"Generated must remain a separate optional stream");
 std::atomic_bool cancel=false;
 const auto request_for=[](const Fixture &f){NativeFragmentGeometryRequest r;r.revision=31;r.material_uid=f.material->id.session_uid;r.tiles={1001};r.uv_requests={{NativeFragmentUvKind::Named,"NamedUV"}};r.generated_stream=true;return r;};
 const auto typed_reject=[&](auto capture,const char *message){bool rejected=false;try{capture();}catch(const NativeFragmentContextError &error){rejected=error.code==NativeFragmentContextCode::GeneratedOrco;}require(rejected,message);};
 using Expected=std::map<std::uint32_t,std::vector<std::array<float,3>>>;
 const auto append_expected=[](Expected &expected,std::uint32_t uid,const Mesh &mesh,const float3 *orco){auto &rows=expected[uid];for(const auto tri:mesh.corner_tris())for(int k=0;k<3;++k){const auto &value=orco[mesh.corner_verts()[tri[k]]];rows.push_back({value.x*.5f+.5f,value.y*.5f+.5f,value.z*.5f+.5f});}};
 const auto check=[&](const NativeFragmentGeometry &geometry,const Expected &expected,NativeFragmentGeneratedProvenance provenance){
  require(geometry.generated_requested(),"Requested Generated stream was dropped");
  std::size_t values=0;
  for(std::size_t si=0;si<geometry.streams().size();++si){const auto &stream=geometry.streams()[si];const auto &owner=geometry.objects()[stream.object];const auto actual=geometry.generated(si);const auto &wanted=expected.at(owner.object_uid);require(owner.generated_provenance==provenance,"Generated source provenance was invented or lost");require(actual.size()==geometry.vertices(si).size()&&actual.size()==wanted.size(),"Generated stream does not match corner stream topology");
   for(std::size_t vi=0;vi<actual.size();++vi){require(actual[vi]==wanted[vi],"Generated differs from exact stock ORCO *.5+.5 corner conversion");require(actual[vi]!=geometry.vertices(si)[vi].position,"Generated silently aliases world-space physical P");++values;}
  }
  require(values==12,"Two-owner Generated corner coverage differs");return values;
 };
 std::shared_ptr<const NativeFragmentGeometrySource> captured,ordinary_source;Expected explicit_expected;
 {
  Fixture f;OrcoMeshes owned;SourceMeshMap overrides;
  for(std::size_t oi=0;oi<f.meshes.size();++oi){auto *mesh=owned.copy(*f.meshes[oi]);auto *orco=static_cast<float3 *>(CustomData_add_layer(&mesh->vert_data,CD_ORCO,CD_CONSTRUCT,mesh->verts_num));require(orco,"Fixture failed to allocate authoritative CD_ORCO");for(int vi=0;vi<mesh->verts_num;++vi)orco[vi]={-.8f+.1f*vi,.25f*float(oi)+.0625f*vi,1.4f-.2f*vi};append_expected(explicit_expected,f.ids[oi],*mesh,orco);overrides.emplace(f.ids[oi],mesh);}
  auto request=request_for(f);captured=capture_native_fragment_geometry(f.context,nullptr,overrides,request,cancel);
  auto optional=request;optional.generated_stream=false;ordinary_source=capture_native_fragment_geometry(f.context,nullptr,overrides,optional,cancel);require(captured->storage_bytes()>ordinary_source->storage_bytes(),"Unrequested Generated capture still charges optional payload storage");
  auto denied=request;denied.ram_budget_bytes=1;bool refused=false;try{capture_native_fragment_geometry(f.context,nullptr,overrides,denied,cancel);}catch(const std::length_error &){refused=true;}require(refused,"Generated capture bypasses RAM budget");
  bool cancelled=false;try{std::atomic_bool stop=true;capture_native_fragment_geometry(f.context,nullptr,overrides,request,stop);}catch(const std::runtime_error &){cancelled=true;}require(cancelled,"Generated capture ignores cancellation");
  auto *orco=static_cast<float3 *>(CustomData_get_layer_for_write(&owned.meshes.at(0)->vert_data,CD_ORCO,owned.meshes.at(0)->verts_num));const float saved=orco[3].x;for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){orco[3].x=invalid;auto sanitized=capture_native_fragment_geometry(f.context,nullptr,overrides,request,cancel);auto geometry=worker([&]{return prepare_native_fragment_geometry(sanitized,cancel);});require(geometry->vertices(0).size()==3&&geometry->vertices(1).size()==6,"Invalid ORCO rejected unaffected triangles or owners");}orco[3].x=saved;
  // Captured source must own ORCO independently of both the selected override
  // and original Main. All native owners are destroyed on leaving this scope.
 }
 auto explicit_geometry=worker([&]{return prepare_native_fragment_geometry(captured,cancel);});captured.reset();const auto explicit_values=check(*explicit_geometry,explicit_expected,NativeFragmentGeneratedProvenance::CapturedOrco);
 auto ordinary=worker([&]{return prepare_native_fragment_geometry(ordinary_source,cancel);});ordinary_source.reset();require(!ordinary->generated_requested(),"Optional Generated flag appeared on ordinary geometry");for(std::size_t si=0;si<ordinary->streams().size();++si)require(ordinary->generated(si).empty(),"Ordinary geometry allocated an optional Generated stream");for(const auto &owner:ordinary->objects())require(owner.generated_provenance==NativeFragmentGeneratedProvenance::None&&owner.original_mesh_uid==0,"Ordinary geometry acquired Generated provenance");require(ordinary->storage_bytes()<explicit_geometry->storage_bytes(),"Disabled Generated still charges resident stream storage");

 std::shared_ptr<const NativeFragmentGeometrySource> fallback_source;Expected fallback_expected;std::map<std::uint32_t,std::uint32_t> original_uids,selected_uids;std::size_t originals_checked=0;
 {
  Fixture f;auto request=request_for(f);typed_reject([&]{capture_native_fragment_geometry(f.context,nullptr,{},request,cancel);},"Missing CD_ORCO was not a typed rejection");request.allow_original_orco=true;OrcoMeshes owned;SourceMeshMap identical;
  struct Original {Mesh *mesh,*texco;char mesh_flag,texco_flag;std::array<float,3> mesh_loc,mesh_size,texco_loc,texco_size;std::vector<float3> positions,texco_positions;};std::vector<Original> originals;
  for(std::size_t oi=0;oi<f.meshes.size();++oi){auto *mesh=f.meshes[oi];auto *texco=BKE_mesh_add(f.main,"Original Generated texture coordinate mesh");const int texco_count=oi?3:4;auto *temporary=BKE_mesh_new_nomain(texco_count,0,0,0);for(int vi=0;vi<texco_count;++vi)temporary->vert_positions_for_write()[vi]=mesh->vert_positions()[vi]*.4f+float3(5.f+oi,8.f,2.f);BKE_mesh_nomain_to_mesh(temporary,texco,nullptr);mesh->texcomesh=texco;mesh->texspace_flag=ME_TEXSPACE_FLAG_AUTO;texco->texspace_flag=ME_TEXSPACE_FLAG_AUTO;for(int k=0;k<3;++k){mesh->texspace_location[k]=11.f+k;mesh->texspace_size[k]=21.f+k;texco->texspace_location[k]=31.f+k;texco->texspace_size[k]=41.f+k;}
   Original state{mesh,texco,mesh->texspace_flag,texco->texspace_flag,{},{},{},{},{mesh->vert_positions().begin(),mesh->vert_positions().end()},{texco->vert_positions().begin(),texco->vert_positions().end()}};for(int k=0;k<3;++k){state.mesh_loc[k]=mesh->texspace_location[k];state.mesh_size[k]=mesh->texspace_size[k];state.texco_loc[k]=texco->texspace_location[k];state.texco_size[k]=texco->texspace_size[k];}originals.push_back(state);
   auto *selected=owned.copy(*mesh);identical.emplace(f.ids[oi],selected);original_uids[f.ids[oi]]=mesh->id.session_uid;selected_uids[f.ids[oi]]=selected->id.session_uid;auto *reference=owned.copy(*mesh);reference->texcomesh=owned.copy(*texco);Object reference_owner{};reference_owner.type=OB_MESH;reference_owner.data=&reference->id;BKE_mesh_orco_ensure(&reference_owner,reference);const auto *stock=static_cast<const float3 *>(CustomData_get_layer(&reference->vert_data,CD_ORCO));require(stock,"Independent stock reference did not ensure ORCO");append_expected(fallback_expected,f.ids[oi],*reference,stock);
  }
  fallback_source=capture_native_fragment_geometry(f.context,nullptr,identical,request,cancel);
  for(const auto &state:originals){require(state.mesh->texcomesh==state.texco&&!CustomData_has_layer(&state.mesh->vert_data,CD_ORCO)&&!CustomData_has_layer(&state.texco->vert_data,CD_ORCO),"Stock fallback mutated original mesh/texcomesh ORCO ownership");require(state.mesh->texspace_flag==state.mesh_flag&&state.texco->texspace_flag==state.texco_flag,"Stock fallback mutated original auto-texspace flags");for(int k=0;k<3;++k)require(state.mesh->texspace_location[k]==state.mesh_loc[k]&&state.mesh->texspace_size[k]==state.mesh_size[k]&&state.texco->texspace_location[k]==state.texco_loc[k]&&state.texco->texspace_size[k]==state.texco_size[k],"Stock fallback mutated original texspace values");require(std::equal(state.positions.begin(),state.positions.end(),state.mesh->vert_positions().begin())&&std::equal(state.texco_positions.begin(),state.texco_positions.end(),state.texco->vert_positions().begin()),"Stock fallback changed original positions");originals_checked+=2;}
  auto *oversized=BKE_mesh_add(f.main,"Oversized ORCO texcomesh");auto *oversized_data=BKE_mesh_new_nomain(5,0,0,0);oversized_data->vert_positions_for_write().fill(float3(1.f));BKE_mesh_nomain_to_mesh(oversized_data,oversized,nullptr);auto *saved_texco=f.meshes[0]->texcomesh;f.meshes[0]->texcomesh=oversized;typed_reject([&]{capture_native_fragment_geometry(f.context,nullptr,identical,request,cancel);},"Oversized original texcomesh reached stock ORCO copy");f.meshes[0]->texcomesh=saved_texco;
  auto *different=owned.meshes.at(0);different->vert_positions_for_write()[0].x+=.125f;different->tag_positions_changed();typed_reject([&]{capture_native_fragment_geometry(f.context,nullptr,identical,request,cancel);},"Nonidentical source override invented original Generated provenance");different->vert_positions_for_write()[0].x-=.125f;different->tag_positions_changed();auto corners=different->corner_verts_for_write();std::swap(corners[0],corners[1]);typed_reject([&]{capture_native_fragment_geometry(f.context,nullptr,identical,request,cancel);},"Reordered source topology invented original Generated provenance");std::swap(corners[0],corners[1]);
 }
 auto fallback_geometry=worker([&]{return prepare_native_fragment_geometry(fallback_source,cancel);});
 bool worker_budget=false;try{worker([&]{TextureRamQueryScope ram(tiny_ram);return prepare_native_fragment_geometry(fallback_source,cancel);});}catch(const std::length_error &){worker_budget=true;}require(worker_budget,"Generated worker preparation bypasses dynamic RAM refusal");bool worker_cancel=false;try{worker([&]{std::atomic_bool stop=true;return prepare_native_fragment_geometry(fallback_source,stop);});}catch(const std::runtime_error &){worker_cancel=true;}require(worker_cancel,"Generated worker preparation ignores cancellation");fallback_source.reset();const auto fallback_values=check(*fallback_geometry,fallback_expected,NativeFragmentGeneratedProvenance::StockOriginalOrco);for(const auto &owner:fallback_geometry->objects())require(owner.original_mesh_uid==original_uids.at(owner.object_uid)&&owner.source_mesh_uid==selected_uids.at(owner.object_uid)&&owner.source_override,"Identical override lost stock-original proof or selected-source identity");
 return {{"captured_orco_corner_values",int(explicit_values)},{"stock_original_corner_values",int(fallback_values)},{"exact_stock_half_scale_bias",true},{"distinct_from_physical_position",true},{"source_main_destroyed_before_prepare",true},{"optional_stream_bytes",0},{"vertex_bytes",int(sizeof(NativeFragmentVertex))},{"missing_orco_typed_rejected",true},{"nonfinite_orco_triangles_skipped",2},{"nonidentical_override_typed_rejected",2},{"original_meshes_unchanged",int(originals_checked)},{"private_texcomesh_stock_reference",true},{"shorter_texcomesh_stock_zero_fill",true},{"oversized_texcomesh_typed_rejected",true},{"capture_and_worker_RAM_cancel_rejected",true}};
}

}
namespace bts {
inline QJsonObject verify_native_fragment_degenerate_geometry(){
 using namespace blender;using namespace native_fragment_geometry_test;std::atomic_bool cancel=false;
 int cases=0;
 for(int mode=0;mode<9;++mode){
  Fixture fixture;OrcoMeshes owned;SourceMeshMap overrides;
  for(std::size_t oi=0;oi<fixture.meshes.size();++oi){
   auto *mesh=owned.copy(*fixture.meshes[oi]);
   if(mode==0)mesh->vert_positions_for_write()[3]=mesh->vert_positions()[1];
   if(mode==2)mesh->vert_positions_for_write().fill(float3(0));
   if(mode==3)mesh->vert_positions_for_write()[3].x=std::numeric_limits<float>::quiet_NaN();
   if(mode==7)mesh->vert_positions_for_write()[3]=(mesh->vert_positions()[1]+mesh->vert_positions()[2])*.5f;
   if(mode==8)for(auto &p:mesh->vert_positions_for_write())p*=1e-20f;
   if(mode!=1)mesh->tag_positions_changed();
   if(mode==6){const_cast<float3 &>(mesh->vert_normals()[0])=float3(NAN);const_cast<float3 &>(mesh->face_normals()[0])=float3(INFINITY);const_cast<float3 &>(mesh->corner_normals()[0])=float3(0);}
   if(mode==1){auto uv=mesh->attributes_for_write().lookup_for_write_span<float2>("DisplayUV");for(int k=3;k<6;++k)uv.span[k]=uv.span[3];uv.finish();}
   if(mode==4){auto uv=mesh->attributes_for_write().lookup_for_write_span<float2>("NamedUV");uv.span.fill(float2(.5f));uv.finish();}
   if(mode==5){auto uv=mesh->attributes_for_write().lookup_for_write_span<float2>("DisplayUV");uv.span[4].x=std::numeric_limits<float>::infinity();uv.finish();}
   auto *orco=static_cast<float3 *>(CustomData_add_layer(&mesh->vert_data,CD_ORCO,CD_CONSTRUCT,mesh->verts_num));require(orco,"Degenerate fixture ORCO allocation failed");
   for(int vi=0;vi<mesh->verts_num;++vi)orco[vi]={float(vi),float(vi+1),float(vi+2)};
   overrides.emplace(fixture.ids[oi],mesh);
  }
  NativeFragmentGeometryRequest request;request.material_uid=fixture.material->id.session_uid;request.tiles={1001,1002,1011};request.uv_requests={{NativeFragmentUvKind::Named,"NamedUV"}};request.generated_stream=true;
  const auto captured=capture_native_fragment_geometry(fixture.context,nullptr,overrides,request,cancel);
  if(mode==2){bool rejected=false;try{worker([&]{return prepare_native_fragment_geometry(captured,cancel);});}catch(const std::invalid_argument &error){rejected=std::string_view(error.what()).find(mode==2?"no UV-addressable":"nonfinite")!=std::string_view::npos;}require(rejected,"Entirely empty geometry was not rejected precisely");++cases;continue;}
  auto job=NativeWorldNormalGeometryJob::start(captured);NativeWorldNormalGeometryJob::pool().waitForDone();require(job->error.empty()&&bool(job->geometry),"One degenerate triangle rejected valid native Normal surfaces");
  const auto &geometry=*job->geometry;require(geometry.streams().size()==2,"Filtered geometry lost an object stream");
  for(std::size_t si=0;si<geometry.streams().size();++si){const auto vertices=geometry.vertices(si);const auto generated=geometry.generated(si);require(vertices.size()==((mode==4||mode==6||mode==8)?6:3)&&generated.size()==vertices.size(),"Degenerate triangle was not removed consistently from corner/Generated streams");for(int k=0;k<3;++k)require(generated[k]==std::array<float,3>{k*.5f+.5f,(k+1)*.5f+.5f,(k+2)*.5f+.5f},"Compacted Generated stream no longer matches retained triangle corners");}
  for(const auto &range:geometry.ranges())for(std::size_t i=range.first_index;i<std::size_t(range.first_index)+range.index_count;++i)require(geometry.indices()[i]<((mode==4||mode==6||mode==8)?6:3),"Compacted geometry index addresses discarded triangle");
  for(std::size_t si=0;si<geometry.streams().size();++si)for(const auto &v:geometry.vertices(si)){
   for(const auto *n:{&v.normal,&v.initial_normal,&v.geometric_normal}){double length=0;for(float x:*n){require(std::isfinite(x),"Fallback normal is nonfinite");length+=double(x)*x;}require(std::abs(length-1)<1e-5,"Fallback normal is not unit length");}
   double length=0;for(int k=0;k<3;++k){require(std::isfinite(v.tangent[k]),"Fallback tangent is nonfinite");length+=double(v.tangent[k])*v.tangent[k];}require(std::abs(length-1)<1e-5&&(v.tangent[3]==1||v.tangent[3]==-1),"Fallback tangent is invalid");
  }
  ++cases;
 }
 for(const auto invalid:std::array{std::array<float,3>{0,0,0},std::array<float,3>{NAN,0,0},std::array<float,3>{INFINITY,0,0}}){
  const auto n=native_surface_unit(invalid);const auto t=native_surface_tangent({invalid[0],invalid[1],invalid[2],0},n);
  require(n==std::array<float,3>{0,0,1},"Invalid normal has no canonical fallback");require(std::isfinite(t[0])&&std::isfinite(t[1])&&t[2]==0&&t[3]==1,"Invalid tangent has no orthogonal fallback");
 }
 const auto normal=native_surface_unit({.6f,0,.8f});const auto tangent=native_surface_tangent({normal[0],normal[1],normal[2],-1},normal);
 require(std::abs(tangent[0]*normal[0]+tangent[1]*normal[1]+tangent[2]*normal[2])<1e-6&&tangent[3]==-1,"Parallel tangent did not get an orthogonal handed fallback");
 return {{"cases",cases},{"physical_and_uv_degenerate_skipped",true},{"generated_and_indices_compacted",true},{"nonfinite_triangles_skipped",true},{"collapsed_source_uv_fallback",true},{"empty_rejected",true}};
}
inline QJsonObject verify_native_fragment_geometry(){
 using namespace blender;using namespace native_fragment_geometry_test;
 require(QCoreApplication::instance()&&QThread::currentThread()==QCoreApplication::instance()->thread(),"Geometry regression requires initialized native owner");require(!GPU_context_active_get(),"Geometry CPU regression acquired GPU context");const auto compiles=GPU_pass_global_compilation_count();std::atomic_bool cancel=false;
 std::shared_ptr<const NativeFragmentGeometrySource> captured;std::vector<std::uint32_t> ids;std::map<std::string,std::vector<float4>> reference;std::map<std::uint32_t,float> randoms;int property_mutations=0,notification_calls=0,geometry_events=0;
 {
  Fixture f;ids=f.ids;NativeFragmentGeometryRequest request;request.revision=27;request.material_uid=f.material->id.session_uid;request.tiles={1001,1002,1011};request.uv_requests={{NativeFragmentUvKind::DefaultRender,{}},{NativeFragmentUvKind::Named,"RenderUV"},{NativeFragmentUvKind::DisplayActive,{}},{NativeFragmentUvKind::Named,"NamedUV"}};
  // Exact observer used by production: no Generated texspace lookup, geometry
  // capture, atlas, UV traversal or GPU work. Mutate both actual scene owners.
  auto *depsgraph=CTX_data_ensure_evaluated_depsgraph(f.context);NativeWorldNormalObjectObserver observer;
  const NativeWorldNormalObjectObserver::Provider provider=[&](std::uint32_t uid){++notification_calls;NativeMaterialGpuContext context;bind_native_material_objects(*f.main,depsgraph,uid,context,false);return context.objects;};
  const auto observe=[&]{return observer.observe(provider,f.material->id.session_uid);};
  require(observe()&&!observe(),"Unchanged actual owners repeatedly invalidate resident context");
  const auto notify=[&](bool revision,bool geometry=false){return observer.notify(revision,provider,f.material->id.session_uid,geometry);};
  const int initial_calls=notification_calls;require(!notify(false)&&!notify(false)&&notification_calls==initial_calls,"Unchanged owner notifications called the property provider");
  const auto flush=[&](bool expected_geometry=false){const bool geometry=native_material_geometry_updated(*f.main,depsgraph);require(geometry==expected_geometry,"Object/Mesh geometry revision classification differs");const bool revision=DEG_id_type_updated(depsgraph,ID_OB)||DEG_id_type_updated(depsgraph,ID_ME);require(revision,"Actual owner mutation did not tag Object/Mesh revision");BKE_scene_graph_update_tagged(depsgraph,f.main);require(!DEG_id_type_updated(depsgraph,ID_OB)&&!DEG_id_type_updated(depsgraph,ID_ME),"Pinned evaluation no longer clears owner revision flags");const int before=notification_calls;const bool changed=notify(revision,geometry);require(notification_calls==before+1,"Changed owner notification did not call provider exactly once");require(!notify(false)&&notification_calls==before+1,"Settled owner notification still called provider");return changed;};

  for(auto &object:f.main->objects){randoms[object.id.session_uid]=BLI_hash_int_2d(BLI_hash_string(object.id.name+2),0)*(1.0f/float(0xffffffffu));const auto x=object.loc[0],color=object.color[0],alpha=object.color[3];
   object.loc[0]+=.375f;object.color[0]+=.0625f;object.color[3]-=.0625f;DEG_id_tag_update_ex(f.main,&object.id,ID_RECALC_TRANSFORM|ID_RECALC_SHADING);require(flush(),"Actual owner transform/color/alpha revision did not invalidate exactly once");++property_mutations;
   object.loc[0]=x;object.color[0]=color;object.color[3]=alpha;DEG_id_tag_update_ex(f.main,&object.id,ID_RECALC_TRANSFORM|ID_RECALC_SHADING);require(flush(),"Restored actual owner revision did not invalidate exactly once");
  }
  // Geometry can change without changing any owner property row. The same
  // observer must invalidate from the latched real geometry notification.
  auto &mesh=*f.meshes[0];const float z=mesh.vert_positions()[1].z;
  for(float value:{z+.125f,z}){mesh.vert_positions_for_write()[1].z=value;mesh.tag_positions_changed();DEG_id_tag_update_ex(f.main,&mesh.id,ID_RECALC_GEOMETRY_ALL_MODES);require(!observe(),"Mesh positions leaked into the small owner-property stamp");require(flush(true),"Real mesh mutation/restore did not invalidate unchanged owner properties");++geometry_events;}
  auto &geometry_owner=*static_cast<Object *>(f.main->objects.first);DEG_id_tag_update_ex(f.main,&geometry_owner.id,ID_RECALC_GEOMETRY);require(DEG_id_type_updated(depsgraph,ID_OB),"Actual Object geometry evaluation did not tag its owner ID");require(!observe()&&flush(true),"Object geometry evaluation tag failed to invalidate unchanged owner properties");++geometry_events;
  // The exact production observer must reject an unavailable binding once,
  // stay settled, then invalidate once on recovery to the same actual owners.
  const NativeWorldNormalObjectObserver::Provider unavailable=[&](std::uint32_t)->std::shared_ptr<const NativeMaterialObjectTable>{++notification_calls;throw std::invalid_argument("Unavailable actual owner fixture");};
  const int before_error=notification_calls;require(observer.notify(true,unavailable,f.material->id.session_uid)&&!observer.error().empty()&&notification_calls==before_error+1,"Owner error notification did not call provider exactly once");require(!observer.notify(false,unavailable,f.material->id.session_uid)&&notification_calls==before_error+1,"Unchanged owner error notification called provider");require(!observer.notify(true,unavailable,f.material->id.session_uid)&&notification_calls==before_error+2,"Repeated explicit error notification did not settle");
  const int before_recovery=notification_calls;require(notify(true)&&observer.error().empty()&&notification_calls==before_recovery+1&&!notify(false)&&notification_calls==before_recovery+1,"Owner provider recovery did not invalidate exactly once");
  // Explicit source override differs from the evaluated mesh; no preview or
  // export authority is changed by the helper. It must copy this exact source.
  auto *override_mesh=BKE_mesh_copy_for_eval(*f.meshes[0]);override_mesh->vert_positions_for_write()[0].x=.25f;override_mesh->tag_positions_changed();SourceMeshMap overrides{{ids[0],override_mesh}};
  try{
   captured=capture_native_fragment_geometry(f.context,nullptr,overrides,request,cancel);require(captured->revision()==27&&captured->object_count()==2,"Actual two-object source/revision lost");
   for(std::size_t oi=0;oi<f.meshes.size();++oi){const Mesh &mesh=oi?*f.meshes[oi]:*override_mesh;const VArraySpan<bool> sharp=*mesh.attributes().lookup_or_default<bool>("sharp_face",bke::AttrDomain::Face,false);
    for(const char *name:{"NamedUV","RenderUV","DisplayUV"}){
     const VArraySpan<float2> uv=*mesh.attributes().lookup<float2>(name,bke::AttrDomain::Corner);const Span<float2> map=uv;const Span<float2> maps[]={map};auto result=bke::mesh::calc_uv_tangents(mesh.vert_positions(),mesh.faces(),mesh.corner_verts(),mesh.corner_tris(),mesh.corner_tri_faces(),sharp,mesh.vert_normals(),mesh.face_normals(),mesh.corner_normals(),Span<Span<float2>>(maps,1));std::vector<float4> expected;for(const auto tri:mesh.corner_tris())for(int k=0;k<3;++k)expected.push_back(result[0][tri[k]]);reference[std::to_string(ids[oi])+":"+name]=std::move(expected);
    }
   }
   const auto rejects=[&](NativeFragmentGeometryRequest bad,const std::atomic_bool &stop){bool rejected=false;try{capture_native_fragment_geometry(f.context,nullptr,overrides,std::move(bad),stop);}catch(const std::exception &){rejected=true;}require(rejected,"Invalid geometry capture admitted");};
   auto missing=request;missing.uv_requests={{NativeFragmentUvKind::Named,"MissingExactUV"}};rejects(missing,cancel);auto invalid=request;invalid.uv_requests={{NativeFragmentUvKind::Named,{}}};rejects(invalid,cancel);auto limited=request;limited.ram_budget_bytes=1;rejects(limited,cancel);std::atomic_bool stop=true;rejects(request,stop);
   // Destroy every source Mesh/Main/Object/context before worker preparation.
   // Keeping only the copied source must be sufficient, including ObjectInfos.
  }catch(...){BKE_id_free_ex(nullptr,override_mesh,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT,true);throw;}
  BKE_id_free_ex(nullptr,override_mesh,LIB_ID_FREE_NO_MAIN|LIB_ID_FREE_NO_USER_REFCOUNT,true);
 }
 auto job=NativeWorldNormalGeometryJob::start(captured);NativeWorldNormalGeometryJob::pool().waitForDone();require(job->ready&&job->error.empty()&&bool(job->geometry),"Native tangent Normal rejected shared UV geometry");
 auto geometry=job->geometry;bool overlaps=false;try{require_native_world_unique_uv(*geometry,cancel);}catch(const std::invalid_argument &){overlaps=true;}require(overlaps,"Native tangent regression must exercise overlapping target UVs");
 captured.reset();require(geometry->revision()==27&&geometry->objects().size()==2&&geometry->streams().size()==6,"Resolved logical UV streams failed to coalesce exactly");
 std::size_t positive=0,negative=0;std::set<bool> windings;std::map<std::pair<std::size_t,int>,int> groups;QJsonArray objects,streams;
 for(std::size_t oi=0;oi<geometry->objects().size();++oi){const auto &o=geometry->objects()[oi];require(std::find(ids.begin(),ids.end(),o.object_uid)!=ids.end(),"Geometry owner UID invented");require(o.source_override==(o.object_uid==ids[0]),"Actual source override provenance lost");const auto &infos=geometry->object_infos(oi);validate_native_fragment_object_context(o,infos);const bool first=o.object_uid==ids[0];require(infos.ob_color[0]==(first?.25f:.75f)&&infos.ob_color[3]==(first?.3f:.8f)&&infos.random==randoms.at(o.object_uid),"Captured actual owner color/alpha/random did not survive source Main destruction");
  for(int kind=0;kind<3;++kind){auto invalid_owner=o;auto invalid_infos=infos;if(kind==0)invalid_owner.world_to_object[12]=std::numeric_limits<float>::infinity();if(kind==1)invalid_infos.ob_color[3]=std::numeric_limits<float>::quiet_NaN();if(kind==2)invalid_infos.random=std::numeric_limits<float>::infinity();bool rejected=false;try{validate_native_fragment_object_context(invalid_owner,invalid_infos);}catch(const std::invalid_argument &){rejected=true;}require(rejected,"Nonfinite captured owner payload admitted before resident upload");}
  const bool flipped=bool(geometry->object_infos(oi).flag&OBJECT_NEGATIVE_SCALE);flipped?++negative:++positive;require((o.object_to_world[0]<0)==flipped,"Actual ObjectInfos negative-scale flag not preserved");objects.append(QJsonObject{{"uid",int(o.object_uid)},{"negative_scale",flipped},{"world_x_scale",o.object_to_world[0]}});}
 for(std::size_t si=0;si<geometry->streams().size();++si){const auto &stream=geometry->streams()[si];const auto &owner=geometry->objects()[stream.object];const auto vertices=geometry->vertices(si);const auto &expected=reference.at(std::to_string(owner.object_uid)+":"+stream.resolved_uv_name);require(vertices.size()==6&&expected.size()==vertices.size(),"Per-corner stream topology differs");
  require(stream.target_request.kind==NativeFragmentUvKind::DisplayActive&&stream.target_request.name.empty()&&stream.resolved_target_uv_name=="DisplayUV","Painter target UV identity was discarded");
  bool render=false,named=false;for(const auto &r:stream.requests){render|=r.kind==NativeFragmentUvKind::DefaultRender&&r.name.empty();named|=r.kind==NativeFragmentUvKind::Named&&r.name=="RenderUV";}if(stream.resolved_uv_name=="RenderUV")require(render&&named&&stream.requests.size()==2,"Logical default/render identity was renamed or discarded");
  for(std::size_t i=0;i<vertices.size();++i){const auto &v=vertices[i];for(int k=0;k<4;++k)require(std::abs(v.tangent[k]-expected[i][k])<1e-6f,"Mikk corner/sign differs from independent stock source call");if(stream.resolved_uv_name=="NamedUV")require(v.raster_uv!=v.uv,"Target raster UV silently aliased source authored UV");require(v.initial_normal==v.normal&&v.geometric_normal[2]>0,"Canonical front-side N/Ni/Ng lost source orientation");}
  const auto &m=owner.world_to_object;const auto &p=vertices[0].position;const std::array<float,3> local{m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12],m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13],m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]};require(std::abs(local[0]-(owner.object_uid==ids[0]?.25f:0.f))<1e-6f&&std::abs(local[1])<1e-6f&&std::abs(local[2])<1e-6f,"Object coordinate used a normal mat3 or another owner's inverse affine matrix");
  if(stream.resolved_uv_name=="NamedUV"){require(vertices[0].tangent[0]>.99f&&vertices[0].tangent[3]>0,"Analytic named-UV tangent/sign differs");const float expected_x=owner.object_uid==ids[0]?1.f:4.f;require(std::abs(vertices[0].position[0]-expected_x)<1e-6f,"Actual source override/matrix ownership lost");}
  if(stream.resolved_uv_name=="RenderUV")require(vertices[0].tangent[0]<-.99f&&vertices[0].tangent[3]<0,"Render mirror did not change tangent/sign independently");
  streams.append(QJsonObject{{"object",int(stream.object)},{"resolved_uv",QString::fromStdString(stream.resolved_uv_name)},{"vertices",int(vertices.size())},{"logical_requests",int(stream.requests.size())}});
 }
 require(positive==1&&negative==1,"Two-object positive/negative context missing");
 for(const auto &range:geometry->ranges()){
  require(range.stream<geometry->streams().size()&&range.index_count%3==0&&std::uint64_t(range.first_index)+range.index_count<=geometry->indices().size(),"Indexed range outside immutable owner");windings.insert(range.invert_front_face);require(range.invert_front_face==(geometry->objects()[geometry->streams()[range.stream].object].object_uid==ids[0]),"Raster winding used source UV or negative-scale instead of actual target UV");require(++groups[{range.stream,range.tile}]<=2,"Front-side range unexpectedly duplicated");
  const auto offset=range.tile-1001;require(range.raster_origin==std::array<float,2>{float(offset%10),float(offset/10)},"Actual UDIM origin differs");for(std::uint64_t i=range.first_index;i<std::uint64_t(range.first_index)+range.index_count;++i)require(geometry->indices()[i]<geometry->vertices(range.stream).size(),"Range indexes another stream");
 }
 require(windings.size()==2&&std::any_of(geometry->ranges().begin(),geometry->ranges().end(),[](const auto &r){return r.tile==1002;})&&std::any_of(geometry->ranges().begin(),geometry->ranges().end(),[](const auto &r){return r.tile==1011;}),"Mirrored/cross-UDIM independent ranges missing");
 bool owner_rejected=false;try{prepare_native_fragment_geometry({},cancel);}catch(const std::logic_error &){owner_rejected=true;}require(owner_rejected,"Preparation admitted native owner thread");
 bool no_source=false;try{worker([&]{return prepare_native_fragment_geometry({},cancel);});}catch(const std::invalid_argument &){no_source=true;}require(no_source,"Missing immutable source admitted");
 // Budget/cancel tests use a newly captured source rather than any released Main.
 {Fixture f;NativeFragmentGeometryRequest request;request.revision=28;request.material_uid=f.material->id.session_uid;request.tiles={1001};request.uv_requests={{NativeFragmentUvKind::Named,"NamedUV"}};auto source=capture_native_fragment_geometry(f.context,nullptr,{},request,cancel);
  bool cancelled=false;try{worker([&]{std::atomic_bool stop=true;return prepare_native_fragment_geometry(source,stop);});}catch(const std::runtime_error &){cancelled=true;}require(cancelled,"Cancelled preparation admitted");bool denied=false;try{worker([&]{TextureRamQueryScope ram(tiny_ram);return prepare_native_fragment_geometry(source,cancel);});}catch(const std::length_error &){denied=true;}require(denied,"Dynamic worker RAM refusal ignored");
  auto baseline=worker([&]{return prepare_native_fragment_geometry(source,cancel);});const auto total=baseline->storage_bytes();baseline.reset();
  // Current available RAM excludes the held source. This boundary has room for
  // new worker storage/scratch, but not for charging that source a second time.
  auto credited=worker([&]{simulated_available_ram=resource_product(total,2);TextureRamQueryScope ram(simulated_ram);return prepare_native_fragment_geometry(source,cancel);});require(credited->storage_bytes()==total,"Resident-source credit changed immutable geometry output");}
 const auto generated_orco=verify_generated_orco();const auto degenerate_geometry=verify_native_fragment_degenerate_geometry();
 require(!GPU_context_active_get()&&GPU_pass_global_compilation_count()==compiles,"Geometry CPU unit acquired/compiled GPU state");
 return {{"complete",true},{"generated_orco",generated_orco},{"degenerate_geometry",degenerate_geometry},{"scope","Actual source mesh capture and stock worker Mikk only; no GPU resident draw/product admission"},{"source_main_destroyed_before_prepare",true},{"owner_property_mutations",property_mutations},{"geometry_revision_events",geometry_events},{"unchanged_owner_geometry_invalidations",geometry_events},{"object_geometry_revision",true},{"owner_stamp_stable",true},{"production_observer_mutation_restore",true},{"unchanged_notifications_provider_calls",0},{"revision_flag_latched_before_clear",true},{"changed_notification_provider_calls",1},{"owner_provider_error_recovery",true},{"captured_color_alpha_random",true},{"full_inverse_object_coordinates",true},{"nonfinite_owner_payloads_rejected",6},{"objects",objects},{"streams",streams},{"ranges",int(geometry->ranges().size())},{"vertices_shared_across_UDIMs",true},{"canonical_physical_front",true},{"default_render_distinct_from_display",true},{"cancel_RAM_owner_missing_rejected",true},{"GPU_context_or_compile_delta",0}};
}
}
