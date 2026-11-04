#include "ResourceBudget.hh"
#include "MeshExport.hh"
#include "UsdMeshExport.hh"
#include "UsdTextureBindings.hh"
#include "SceneSnapshot.hh"
#include "TextureDocument.hh"
#include "NativeCompositeJob.hh"
#include "MeshDisplacementSample.hh"
#include <atomic>
#include <bit>
#include <unordered_map>
#include "BKE_attribute.hh"
#include "BLI_math_vector.hh"
#include <QFileInfo>
#include <QDir>
#include <map>
#include <stdexcept>
#include "BKE_collection.hh"
#include "BKE_context.hh"
#include "BKE_layer.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"
#include "BLI_math_matrix.hh"
#include "BLI_string.h"
#include "BLI_listbase.h"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "IMB_colormanagement.hh"
#include "IO_wavefront_obj.hh"
#include "exporter/obj_exporter.hh"
namespace bts {
using namespace blender;
struct NativeMeshExport::Impl {
 Main *main=nullptr;Scene *scene=nullptr;Depsgraph *graph=nullptr;
 qint64 objects=0,vertices=0,faces=0,corners=0,triangles=0;
 bool displaced=false,recompute=true,applied=false;
 struct Surface {TextureSet set;std::shared_ptr<TexturePadding> padding;};
 std::map<std::uint32_t,Surface> surfaces;
 std::map<Mesh *,std::vector<std::uint32_t>> mesh_materials;
 std::map<std::string,std::string> occlusion,material_libraries;
 ~Impl(){if(graph)DEG_graph_free(graph);if(main)BKE_main_free(main);}
};
NativeMeshExport::NativeMeshExport():impl_(std::make_unique<Impl>()){}
NativeMeshExport::~NativeMeshExport()=default;
std::shared_ptr<NativeMeshExport> NativeMeshExport::prepare(Main &live,Scene &scene,Depsgraph &graph,View3D *view,const SourceMeshMap &sources,const TextureDocument *document,const SceneSnapshot *geometry,const std::set<std::uint32_t> &active,bool recompute_normals){
 auto job=std::shared_ptr<NativeMeshExport>(new NativeMeshExport);auto &p=*job->impl_;
 p.displaced=document!=nullptr;p.recompute=recompute_normals;
 if(document){if(!geometry||active.empty())throw std::invalid_argument("No active displacement preview to export");std::uint64_t texels=0;for(const auto &set:document->texture_sets())if(active.contains(set.material_session_uid)){texels+=set.evaluation_texels();require_texture_ram(resource_product(texels,16),"Displacement export textures");p.surfaces.emplace(set.material_session_uid,Impl::Surface{set,std::make_shared<TexturePadding>(*geometry,set.material_session_uid)});}}
 p.main=BKE_main_new();IMB_colormanagement_working_space_convert(p.main,&live);p.scene=BKE_scene_add(p.main,"Mesh export snapshot");p.scene->unit=scene.unit;
 auto *layer=BKE_view_layer_default_view(&scene);BKE_view_layer_synced_ensure(live,&scene,layer);
 std::map<const Material *,Material *> materials;
 for(Base &base:*BKE_view_layer_object_bases_get(layer)){
  if(!BKE_base_is_visible(view,&base))continue;
  auto *evaluated=DEG_get_evaluated(&graph,base.object);if(evaluated->type!=OB_MESH||!(BKE_object_visibility(evaluated,DAG_EVAL_VIEWPORT)&OB_VISIBLE_SELF))continue;
  const Mesh *source=BKE_object_get_evaluated_mesh(evaluated);const auto found=sources.find(base.object->id.session_uid);if(!p.displaced&&found!=sources.end())source=found->second;if(!source)continue;
  require_texture_ram(resource_sum(resource_product(resource_sum(p.vertices,source->verts_num),sizeof(float3)*3),resource_product(resource_sum(p.corners,source->corners_num),sizeof(int)*4+sizeof(float2))),"Mesh export snapshot");
  auto *mesh=reinterpret_cast<Mesh *>(BKE_id_copy(p.main,&source->id));
  auto &material_uids=p.mesh_materials[mesh];material_uids.resize(mesh->totcol);
  // Preserve the evaluated shading normals before freezing nonuniform transforms.
  Vector<float3> source_normals(source->corner_normals());bke::mesh_set_custom_normals_normalized(*mesh,source_normals);
  // The copy temporarily references source material IDs; replace them on the owner
  // thread before the independent snapshot can enter the export worker.
  for(int slot=0;slot<mesh->totcol;++slot){auto *original=BKE_object_material_get_eval(evaluated,slot+1);if(original)original=DEG_get_original(original);material_uids[slot]=original?original->id.session_uid:0;auto *old=mesh->mat[slot];if(old)id_us_min(&old->id);mesh->mat[slot]=nullptr;if(!original)continue;
   auto [it,inserted]=materials.try_emplace(original,nullptr);if(inserted){auto *material=BKE_material_add(p.main,original->id.name+2);const auto values=read_material_summary(live,original,scene).values;material->r=values.base_color[0];material->g=values.base_color[1];material->b=values.base_color[2];material->alpha=values.base_color[3];material->roughness=values.roughness;material->metallic=values.metallic;it->second=material;}
   mesh->mat[slot]=it->second;id_us_plus(&it->second->id);
  }
  // Freeze world space, including nonuniform/sheared transforms, so the worker
  // needs no parent, constraint, modifier, animation or live material ID.
  bke::mesh_transform(*mesh,evaluated->object_to_world(),false);
  if(math::determinant(float3x3(evaluated->object_to_world()))<0)bke::mesh_flip_faces(*mesh,IndexMask(mesh->faces_num));
  auto *object=BKE_object_add_only_object(p.main,OB_MESH,base.object->id.name+2);object->data=&mesh->id;id_us_plus(&mesh->id);BKE_collection_object_add(p.main,p.scene->master_collection,object);
  ++p.objects;p.vertices+=mesh->verts_num;p.faces+=mesh->faces_num;p.corners+=mesh->corners_num;p.triangles+=mesh->corner_tris().size();
 }
 if(!p.objects)throw std::runtime_error("No visible project mesh to export");
 auto *target_layer=BKE_view_layer_default_view(p.scene);BKE_view_layer_synced_ensure(*p.main,p.scene,target_layer);p.graph=DEG_graph_new(p.main,p.scene,target_layer,DAG_EVAL_VIEWPORT);DEG_graph_build_for_all_objects(p.graph);BKE_scene_graph_evaluated_ensure(p.graph,p.main);return job;
}
QJsonObject NativeMeshExport::summary()const{const auto &p=*impl_;return {{"objects",p.objects},{"vertices",p.vertices},{"faces",p.faces},{"corners",p.corners},{"triangles",p.triangles},{"source_geometry",!p.displaced},{"recompute_normals",p.recompute}};}
namespace {
float displacement_sample(const TextureSet &set,const NativeCompositeResult &image,float2 uv){
 if(!std::isfinite(uv.x)||!std::isfinite(uv.y))throw std::runtime_error("Non-finite displacement UV");
 const bool repeat=set.tiles==std::set<int>{1001};const float col=std::floor(uv.x),row=std::floor(uv.y);
 if(!repeat&&(col<0||col>=10||row<0||row>=100))return set.displacement.midlevel;
 const int tile=repeat?1001 : 1001+int(col)+int(row)*10;const auto found=image.tiles.find(tile);
 if(found==image.tiles.end()||found->second[16].empty())return set.displacement.midlevel;
 const auto [width,height]=image.sizes.at(tile);return sample_mesh_displacement(found->second[16],width,height,uv.x-col,uv.y-row,set.displacement.midlevel,repeat);
}
struct VertexKey {int original;std::array<std::uint32_t,3> position;bool operator==(const VertexKey &)const=default;};
struct VertexHash {std::size_t operator()(const VertexKey &key)const{std::size_t h=std::uint32_t(key.original);for(auto value:key.position)h=(h^value)*16777619;return h;}};
Mesh *displace_mesh(const Mesh &source,const std::vector<std::uint32_t> &materials,const std::map<std::uint32_t,TextureSet> &sets,const std::map<std::uint32_t,NativeCompositeResult> &images,bool recompute){
 const auto uv_name=source.active_or_default_uv_map_name();const auto uv=uv_name.is_empty()?VArraySpan<float2>{}:VArraySpan<float2>(*source.attributes().lookup<float2>(uv_name,bke::AttrDomain::Corner));
 if(uv.is_empty())throw std::runtime_error("Displacement export requires UV coordinates");
 const auto faces=source.faces();const auto corners=source.corner_verts();const auto positions=source.vert_positions();Vector<float3> normals(source.corner_normals());
 const VArraySpan<int> slots(*source.attributes().lookup_or_default<int>("material_index",bke::AttrDomain::Face,0));
 std::unordered_map<VertexKey,int,VertexHash> unique;unique.reserve(source.verts_num);Vector<float3> points;Vector<int> source_indices,corner_vertices(source.corners_num);
 for(const int face:faces.index_range()){const int slot=slots[face];const auto uid=slot>=0&&slot<int(materials.size())?materials[slot]:0;const auto set=sets.find(uid);
  for(int corner:faces[face]){const int original=corners[corner];float3 position=positions[original];if(set!=sets.end()){const auto &settings=set->second.displacement;const float height=displacement_sample(set->second,images.at(uid),uv[corner]);position+=normals[corner]*((height-settings.midlevel)*settings.strength);}
   VertexKey key{original,{}};for(int axis=0;axis<3;++axis){if(!std::isfinite(position[axis]))throw std::runtime_error("Non-finite displaced mesh position");key.position[axis]=position[axis]==0?0:std::bit_cast<std::uint32_t>(position[axis]);}
   auto [it,inserted]=unique.try_emplace(key,int(points.size()));if(inserted){if(points.size()>=std::numeric_limits<int>::max())throw std::overflow_error("Displaced mesh vertex index cannot be represented by Blender");if((points.size()&4095)==0)require_texture_ram(resource_product(points.size()+4096,sizeof(VertexKey)+sizeof(float3)+sizeof(int)+4*sizeof(void*)),"Displaced export vertices");points.append(position);source_indices.append(original);}corner_vertices[corner]=it->second;
  }
 }
 auto *mesh=BKE_mesh_new_nomain_from_template(&source,points.size(),0,source.faces_num,source.corners_num);
 try{
  auto attributes=mesh->attributes_for_write();bke::gather_attributes(source.attributes(),bke::AttrDomain::Point,bke::AttrDomain::Point,{},source_indices.as_span(),attributes);
  bke::copy_attributes(source.attributes(),bke::AttrDomain::Face,bke::AttrDomain::Face,{},attributes);bke::copy_attributes(source.attributes(),bke::AttrDomain::Corner,bke::AttrDomain::Corner,{},attributes);
  mesh->face_offsets_for_write().copy_from(source.face_offsets());mesh->corner_verts_for_write().copy_from(corner_vertices);mesh->vert_positions_for_write().copy_from(points);bke::mesh_calc_edges(*mesh,false,false);
  if(recompute)attributes.remove("custom_normal");else bke::mesh_set_custom_normals_normalized(*mesh,normals);
  mesh->tag_positions_changed();return mesh;
 }catch(...){BKE_id_free(nullptr,&mesh->id);throw;}
}
}
void NativeMeshExport::finalize_geometry(){
 if(impl_->displaced&&!impl_->applied){
  std::atomic_bool cancel=false;std::map<std::uint32_t,NativeCompositeResult> images;std::map<std::uint32_t,TextureSet> sets;CompositeChannels channels{};channels[16]=true;
  for(const auto &[uid,surface]:impl_->surfaces){auto image=native_composite_job(surface.set,false,surface.padding,0,cancel,false,channels);if(!image)throw std::runtime_error("Displacement composite cancelled");images.emplace(uid,std::move(*image));sets.emplace(uid,surface.set);}
  std::uint64_t total_vertices=0;for(const auto &[mesh,materials]:impl_->mesh_materials){auto *displaced=displace_mesh(*mesh,materials,sets,images,impl_->recompute);total_vertices+=displaced->verts_num;BKE_mesh_nomain_to_mesh(displaced,mesh,nullptr,false);DEG_id_tag_update_ex(impl_->main,&mesh->id,ID_RECALC_GEOMETRY);}
  for(Object &object:impl_->main->objects)DEG_id_tag_update_ex(impl_->main,&object.id,ID_RECALC_GEOMETRY);BKE_scene_graph_evaluated_ensure(impl_->graph,impl_->main);impl_->applied=true;
 }
}
void NativeMeshExport::attach_usd_textures(std::span<const UsdTextureSetExport> sets){
 std::map<std::uint32_t,Material *> materials;for(const auto &[mesh,uids]:impl_->mesh_materials)for(int slot=0;slot<int(uids.size());++slot)if(mesh->mat[slot])materials[uids[slot]]=mesh->mat[slot];
 for(const auto &set:sets){const auto found=materials.find(set.material_uid);if(found==materials.end())throw std::runtime_error("USD Texture Set has no source material binding");attach_native_usd_textures(*impl_->main,*found->second,set);if(!set.files[3].empty())impl_->occlusion[found->second->id.name+2]=set.files[3];}
 DEG_relations_tag_update(impl_->main);BKE_scene_graph_update_tagged(impl_->graph,impl_->main);
}
void NativeMeshExport::attach_usd_material_libraries(const std::map<std::uint32_t,std::string> &libraries){
 std::map<std::uint32_t,Material *> materials;for(const auto &[mesh,uids]:impl_->mesh_materials)for(int slot=0;slot<int(uids.size());++slot)if(mesh->mat[slot])materials[uids[slot]]=mesh->mat[slot];for(const auto &[uid,path]:libraries){const auto found=materials.find(uid);if(found==materials.end())throw std::runtime_error("USD OpenPBR material has no source binding");if(!QFileInfo::exists(QString::fromStdString(path)))throw std::runtime_error("USD OpenPBR library is missing");impl_->material_libraries[found->second->id.name+2]=path;}
}
void NativeMeshExport::write(const std::string &path,bool triangulate){
 const QFileInfo file(QString::fromStdString(path));if(!file.isAbsolute()||!file.dir().exists()||(file.suffix().compare("obj",Qt::CaseInsensitive)&&file.suffix().compare("usdc",Qt::CaseInsensitive)&&file.suffix().compare("usdz",Qt::CaseInsensitive)))throw std::invalid_argument("Choose an absolute OBJ, USDC or USDZ output path");if(path.size()>=FILE_MAX)throw std::invalid_argument("Mesh export path is too long");
 finalize_geometry();
 if(file.suffix().compare("usdc",Qt::CaseInsensitive)==0||file.suffix().compare("usdz",Qt::CaseInsensitive)==0){write_native_usd_mesh(*impl_->main,*impl_->graph,path,file.suffix().compare("usdz",Qt::CaseInsensitive)==0,triangulate,impl_->occlusion,impl_->material_libraries);return;}
 ReportList reports{};BKE_reports_init(&reports,RPT_STORE|RPT_PRINT_HANDLED_BY_OWNER);struct Cleanup {ReportList &reports;~Cleanup(){BKE_reports_free(&reports);}} cleanup{reports};
 OBJExportParams params;params.blen_filepath="";STRNCPY(params.filepath,path.c_str());params.apply_modifiers=false;params.export_triangulated_mesh=triangulate;params.export_uv=true;params.export_normals=true;params.export_materials=true;params.reports=&reports;
 io::obj::export_frame(impl_->graph,params,params.filepath);
 if(BKE_reports_contain(&reports,RPT_ERROR)||!file.exists()||file.size()==0)throw std::runtime_error("Native Wavefront OBJ export failed");
}
}
