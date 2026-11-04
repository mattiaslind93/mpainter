#include "TextureSetResidency.hh"
#include "MaskEffectContext.hh"
#include "NativeWorldNormalObjectContext.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "NativeMaterialFragmentImages.hh"
#include "SpatialOracleScope.hh"
#include "BumpPrecision.hh"
#include "PaintRecording.hh"
#include "MaterialLayerShader.hh"
#include "MaterialLayerParameterIO.hh"
#include "MaterialLayerParameterSurface.hh"
#include "MaterialLayerParameterReadback.hh"
#include "MaterialLayeringSources.hh"
#include "MaterialLayerUdim.hh"
#include "GPU_capabilities.hh"
#include "GPU_context.hh"
#include "OpenPbrShader.hh"
#include "MaterialLayeringStacks.hh"
#include "BKE_image_residency.hh"
#include "Performance.hh"
#include "UniformChannels.hh"
#include "GpuLayerPreview.hh"
#include "GpuTextureProbe.hh"
#include "GpuLayerChannel.hh"
#include "BLI_math_half.hh"
#include "GPU_texture.hh"
#include "GPU_state.hh"
#include "MEM_guardedalloc.h"
#include <set>
#include <utility>
#include "NativeCompositeJob.hh"
#include "CompositeMemory.hh"
#include "CompositeStatus.hh"
#include <future>
#include <deque>
#include <chrono>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QThreadPool>
#include "MaterialInspection.hh"
#include "TextureExtent.hh"
#include "NativeGpuImages.hh"
#include "NativeCyclesImages.hh"
#include "NativeCyclesPublication.hh"
#include "NativeMaterialParameters.hh"
#include "NativeMaterialCoat.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "NativeMaterialGpuProgram.hh"
#include "GpuProjectReadback.hh"
#include "TextureMemory.hh"
#include "BLI_task.hh"
#include "NormalMix.hh"
#include "HeightNormal.hh"
#include "AmbientOcclusion.hh"
#include <bit>
#include "TexturePadding.hh"
#include "ProjectionGeometryJob.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeWorldNormalPlan.hh"
#include "NativeWorldNormalImages.hh"
#include "NativeWorldNormalShader.hh"
#include "MarginGeometryJob.hh"
#include "SceneSnapshot.hh"
#include "TextureDocument.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_lib_id.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include "BKE_main.hh"
#include "BKE_global.hh"
#include "BKE_idprop.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DEG_depsgraph.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
namespace bts {
using namespace blender;
namespace {
bNodeSocket &input(bNode &node, int index)
{
  int i=0;
  for (bNodeSocket &socket : node.inputs) if (i++==index) return socket;
  throw std::runtime_error("Inspection node input is missing");
}
bNodeSocket &output(bNode &node, const char *identifier)
{
  for (bNodeSocket &socket : node.outputs) if (STREQ(socket.identifier,identifier)) return socket;
  throw std::runtime_error("Inspection node output is missing");
}
void scalar(bNodeTree &tree, bNodeSocket &socket, float value)
{
  if (socket.type!=SOCK_FLOAT || !socket.default_value || (socket.flag & ((socket.in_out==SOCK_IN ? SOCK_IS_LINKED : 0) | SOCK_UNAVAIL))) throw std::runtime_error("Inspection scalar socket mismatch");
  if(static_cast<bNodeSocketValueFloat *>(socket.default_value)->value==value)return;
  static_cast<bNodeSocketValueFloat *>(socket.default_value)->value=value;
  BKE_ntree_update_tag_socket_property(&tree,&socket);
}
void color(bNodeTree &tree, bNodeSocket &socket, const std::array<float,4> &value)
{
  if (socket.type!=SOCK_RGBA || !socket.default_value || (socket.flag & (SOCK_IS_LINKED | SOCK_UNAVAIL))) throw std::runtime_error("Inspection color socket mismatch");
  if(std::equal(value.begin(),value.end(),static_cast<bNodeSocketValueRGBA *>(socket.default_value)->value))return;
  std::copy(value.begin(),value.end(),static_cast<bNodeSocketValueRGBA *>(socket.default_value)->value);
  BKE_ntree_update_tag_socket_property(&tree,&socket);
}
bNode &node(bNodeTree &tree,const char *type,const char *name)
{
  if (auto *existing=bke::node_find_node_by_name(tree,name)) {
    if (!STREQ(existing->idname,type)) throw std::runtime_error("Inspection node name conflicts with another node");
    return *existing;
  }
  auto *created=bke::node_add_node(nullptr,tree,UString(type));
  if (!created) throw std::runtime_error("Cannot create native inspection shader");
  STRNCPY(created->name,name);
  return *created;
}
struct Binding {
  Material *material; bNode *view_switch; bNode *coverage_mix;
  MaterialLayerShader layered_shader;
  MaterialLayering material_layering;
  bool layering_directx=false;
  std::string geometry_visibility_key;bool geometry_visibility_on=false;bool residency_hidden=false;
  std::array<Image *,5> images{};
  std::array<Image *,custom_channel_count> user_images{};
  std::array<bNode *,custom_channel_count> user_textures{},user_unpremultiply{};
  std::array<bool,custom_channel_count> user_present{};
  std::array<bNode *,5> textures{},unpremultiply{},base_mix{};
  Image *bake_image=nullptr; bNode *bake_texture=nullptr,*bake_unpremultiply=nullptr;
  bool mesh_displayed=false;std::string bake_hash,bake_shape;
  Image *authored_normal_image=nullptr;
  // Signed WORLD direction is deliberately separate from tangent authored6.
  std::array<Image *,2> world_normal_images{};
  bNode *world_normal_enabled=nullptr;bool world_images_full=false;
  std::shared_ptr<NativeWorldNormalGeometryJob> world_geometry_job;
  std::unique_ptr<NativeMaterialFragmentGpuGeometry> world_geometry;
  NativeFragmentResidentDiagnostics world_geometry_diagnostics;
  std::uint64_t world_publication_generation=0,world_rendered_tiles=0;
  std::uint64_t world_geometry_revision=1,world_queued_generation=~std::uint64_t(0),world_registry_generation=0;
  NativeWorldNormalObjectObserver world_object_context;
  bool world_objects_dirty=false,world_view_dirty=false;NativeWorldNormalContextRoute world_context;
  NativeMaterialBakedAoContext world_ao;NativeBakedAoLease world_ao_lease;int world_ao_tile=0;
  std::string coat_signature;
  std::string world_source_uv,world_resource;bool world_generated=false;std::uint64_t world_source_revision=0;
  std::array<std::shared_ptr<gpu::Texture>,2> world_scratch;
  std::string world_signature,world_published_source_signature,world_error;
  std::map<int,std::string> world_published_signatures,world_pending_signatures;
  std::set<std::pair<QUuid,int>> world_layers_ready;
  std::optional<TextureSet> world_set;
  std::vector<int> world_tiles;std::size_t world_tile=0;

  Image *ao_image=nullptr;std::map<int,std::vector<unsigned char>> ao_presence;std::size_t ao_count=0;
  std::uint64_t preview_generation=0,final_generation=0;
  bool preview_displacement_content=false;std::array<Image *,17> preview_images{};CompositeChannels preview_active{},edit_channels{};
  Image *displacement_image=nullptr; bNode *displacement_texture=nullptr,*displacement_unpremultiply=nullptr,*displacement_mix=nullptr,*displacement_node=nullptr;
  ShaderParameters shader_parameters;
  int scattering_slot=-1;
  DisplacementSettings displacement_settings;std::map<int,std::vector<unsigned char>> displacement_presence;std::size_t displacement_count=0;std::optional<bool> gpu_displacement_presence;std::uint64_t displacement_cpu_epoch=0;float displacement_minimum=0,displacement_maximum=1;
  Image *mask_image=nullptr; bNode *mask_texture=nullptr;std::optional<TextureSet> mask_snapshot;
  std::uint64_t mask_hash=0; float mask_background=-1; bool mask_cached=false,mask_display_linked=false;QUuid mask_async_layer;
  std::shared_ptr<TexturePadding> padding;
  std::array<double,2> native_per_uv{1,1};
  std::map<int,std::vector<unsigned char>> normal_presence;std::size_t normal_count=0;
  float previous_height_base=0;
  std::array<std::array<float,4>,5> last_base{};bool bases_valid=false;
  int resolution=0,height=0;
  std::map<int,std::pair<int,int>> tile_sizes;
  std::map<int,std::shared_ptr<const FillSurfaceAtlas>> projection_cache;
  std::map<int,std::shared_ptr<const GeometryOwnershipTile>> ownership_cache;
  std::map<std::uint32_t,std::string> geometry_names;
  std::shared_ptr<ProjectionGeometryJob> projection_job;bool geometry_upload_pending=false;
  std::shared_ptr<MarginGeometryJob> margin_job;bool margins_ready=false;
  int displayed_channel=-2;
  MaterialValues displayed_values;
  std::array<float,5> displayed_coverage{};
  Main *owner_main=nullptr;
  CompositeChannels gpu_stack_active{},gpu_unbound_waiting{};std::optional<TextureSet> gpu_stack_set;bool gpu_inspection_waiting=false;std::uint64_t gpu_snapshot_generation=0;
  std::array<std::optional<std::array<float,4>>,5> gpu_uniform_values;
};
// Stack-only, read-only checks for explicit shader authoring. Retained pointers
// are compared with their owner's containers before any pointed-to fields read.
struct LayeringTopology {
  const Main &main;const bNodeTree &tree;
  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  const bNode &node(const bNode *wanted,const char *type,const char *name=nullptr)const{
    const bNode *owned=nullptr;for(const auto &n:tree.nodes)if(&n==wanted){owned=&n;break;}
    require(owned,"Material layering: retained node is not in its material tree");
    require(STREQ(owned->idname,type)&&!(owned->flag&NODE_MUTED),"Material layering: native node type or mute state changed");
    require(!name||STREQ(owned->name,name),"Material layering: retained native node identity changed");
    if(name)for(const auto &n:tree.nodes)require(&n==owned||!STREQ(n.name,name),"Material layering: duplicate native node identity");return *owned;
  }
  const bNode &named(const char *name,const char *type)const{
    const bNode *found=nullptr;for(const auto &n:tree.nodes)if(STREQ(n.name,name)){require(!found,"Material layering: duplicate native node identity");found=&n;}return node(found,type,name);
  }
  const bNodeSocket &socket(const bNode &n,bool inputs,const char *id,int type)const{
    const bNodeSocket *found=nullptr;const auto &sockets=inputs?n.inputs:n.outputs;
    for(const auto &s:sockets)if(STREQ(s.identifier,id)){require(!found,"Material layering: duplicate socket identifier");found=&s;}
    require(found&&found->type==type&&!(found->flag&SOCK_UNAVAIL),"Material layering: native socket type or availability changed");return *found;
  }
  const bNodeSocket &in(const bNode &n,const char *id,int type)const{return socket(n,true,id,type);}
  const bNodeSocket &out(const bNode &n,const char *id,int type)const{return socket(n,false,id,type);}
  const bNodeLink &incoming(const bNode &n,const bNodeSocket &s)const{
    const bNodeLink *found=nullptr;for(const auto &link:tree.links)if(link.tosock==&s){require(!found,"Material layering: multiple incoming native links");found=&link;}
    require(found&&found->tonode==&n&&(found->flag&NODE_LINK_VALID)&&!(found->flag&NODE_LINK_MUTED)&&(s.flag&SOCK_IS_LINKED),"Material layering: native link is missing, inactive or changed");return *found;
  }
  void edge(const bNode &from,const bNodeSocket &output,const bNode &to,const bNodeSocket &input)const{
    const auto &link=incoming(to,input);require(link.fromnode==&from&&link.fromsock==&output,"Material layering: native source edge changed");
  }
  const bNode &source(const bNode &to,const bNodeSocket &input,const char *type,const char *name=nullptr)const{return node(incoming(to,input).fromnode,type,name);}
  void unlinked(const bNodeSocket &s)const{
    for(const auto &link:tree.links)require(link.tosock!=&s,"Material layering: expected native constant has a source link");
    require(!(s.flag&SOCK_IS_LINKED),"Material layering: stale native constant link flag");
  }
  void scalar(const bNodeSocket &s,float expected)const{
    unlinked(s);require(s.type==SOCK_FLOAT&&s.default_value,"Material layering: native scalar storage changed");
    const float actual=static_cast<const bNodeSocketValueFloat *>(s.default_value)->value;
    require(std::isfinite(actual)&&actual==expected,"Material layering: native scalar value changed");
  }
  void color(const bNodeSocket &s,const std::array<float,4> &expected)const{
    unlinked(s);require(s.type==SOCK_RGBA&&s.default_value,"Material layering: native color storage changed");
    const auto *actual=static_cast<const bNodeSocketValueRGBA *>(s.default_value)->value;
    require(std::equal(expected.begin(),expected.end(),actual),"Material layering: retained material base changed");
  }
  void mix(const bNode &n,int operation)const{require(n.custom1==operation&&n.custom2==0,"Material layering: native blend operation or flags changed");}
  const Image &image(const Image *wanted)const{
    const Image *owned=nullptr;for(const auto &image:main.images)if(&image==wanted){owned=&image;break;}
    require(owned,"Material layering: retained image is not in the owner Main");
    require(!ID_IS_LINKED(&owned->id)&&!ID_IS_OVERRIDE_LIBRARY(&owned->id)&&!(owned->id.tag&(ID_TAG_NO_MAIN|ID_TAG_LOCALIZED|ID_TAG_COPIED_ON_EVAL|ID_TAG_MISSING)),"Material layering: native image ownership changed");
    // BKE_image_memorypack changes non-tiled generated images to FILE.
    // Exact retained identity plus owned packed storage also covers Save/Open;
    // arbitrary external FILE sources are not admitted. No buffer is decoded.
    const bool packed_file=owned->source==IMA_SRC_FILE&&owned->packedfiles.first;
    require((owned->source==IMA_SRC_GENERATED||owned->source==IMA_SRC_TILED||packed_file)&&owned->alpha_mode==IMA_ALPHA_CHANNEL_PACKED,"Material layering: native image source or alpha mode changed");return *owned;
  }
  void image_shape(const Image &image,const TextureSet &set)const{
    require((image.source==IMA_SRC_TILED)==(set.tiles.size()!=1||!set.tiles.contains(1001)),"Material layering: native image tiling changed");
    std::size_t count=0;for(const auto &tile:image.tiles){++count;require(set.tiles.contains(tile.tile_number)&&tile.gen_x==set.width(tile.tile_number)&&tile.gen_y==set.rows(tile.tile_number),"Material layering: native image tile dimensions changed");}
    require(count==set.tiles.size(),"Material layering: native image tiles changed");
  }
  void texture(const bNode &node,const Image &image)const{
    require(node.id==&image.id&&node.storage,"Material layering: native texture image ID changed");
    const auto &settings=*static_cast<const NodeTexImage *>(node.storage);
    require(settings.projection==SHD_PROJ_FLAT&&settings.interpolation==SHD_INTERP_LINEAR&&settings.extension==SHD_IMAGE_EXTENSION_REPEAT,"Material layering: native texture sampling changed");
    unlinked(in(node,"Vector",SOCK_VECTOR));
  }
};
std::array<float,4> encoded(const MaterialValues &v,int channel) {
  switch(channel) {
    case 0:return v.base_color;
    case 1:return {v.metallic,v.metallic,v.metallic,1};
    case 2:return {v.roughness,v.roughness,v.roughness,1};
    case 3:return {v.normal[0]*.5f+.5f,v.normal[1]*.5f+.5f,v.normal[2]*.5f+.5f,1};
    default:return {v.bump,v.bump,v.bump,1};
  }
}
std::array<float,4> encoded_precision(const MaterialValues &value,int channel,ChannelPrecision precision){const auto rgb=precision_channel(value,channel,precision);return {rgb[0],rgb[1],rgb[2],1};}
std::array<float,4> encoded_material_base(const TextureSet &set,int channel){if(channel==4){const float q=precision_bump_base(set);return {q,q,q,1};}return encoded_precision(set.channels[channel]?set.base_values:MaterialValues{},channel,set.precision[channel]);}
bool gpu_readback_reference_ao_presence(const TextureSet &set){std::array<bool,6> wanted{};wanted[5]=true;for(int tile:set.tiles)for(int y=0;y<set.rows(tile);++y)for(int x=0;x<set.width(tile);++x)if(TextureDocument::evaluate_channels(set,wanted,x,y,tile).ao_coverage>0)return true;return false;}
std::array<float,4> gpu_logical_pixel(const TextureSet &set,int c,int x,int y,int tile,std::optional<bool> ao_present=std::nullopt){if(c==15){const float v=TextureDocument::mask_value(set,set.selected_layer,x,y,tile);return {v,v,v,1};}if(c==5){static bool logged=false;if(!logged){logged=true;std::fprintf(stderr,"BTS_GPU_CPU_REFERENCE reason=diagnostic-or-export-query channel=5\n");}std::array<bool,6> wanted{};wanted[5]=true;const auto composite=TextureDocument::evaluate_channels(set,wanted,x,y,tile);const float value=mix_ambient_occlusion(set,composite.material.ambient_occlusion,ao_present?*ao_present:gpu_readback_reference_ao_presence(set),sample_baked_ao(set,(x+.5)/set.width(tile),(y+.5)/set.rows(tile),tile));const float q=precision_rgb({value,value,value},set.precision[5])[0];return {q,q,q,1};}if(c==6){std::array<bool,6> wanted{};wanted[3]=true;const auto result=TextureDocument::evaluate_channels(set,wanted,x,y,tile);const auto full=precision_channel(result.material,3,set.precision[3]);return {full[0],full[1],full[2],1};}if(c>=7){const auto value=c==16?TextureDocument::composite_displacement(set,x,y,tile):TextureDocument::composite_user(set,c-7,x,y,tile);return {value[0]*value[1],value[0]*value[1],value[0]*value[1],value[1]};}std::array<bool,6> wanted{};wanted[c]=true;const auto composite=TextureDocument::evaluate_channels(set,wanted,x,y,tile);auto full=precision_channel(composite.material,c,set.precision[c]),base=precision_channel(set.channels[c]?set.base_values:MaterialValues{},c,set.precision[c]);if(c==4){full.fill(precision_bump_output(set,composite.material.bump));base.fill(precision_bump_base(set));}const float alpha=composite.coverage[c];return {full[0]-base[0]*(1-alpha),full[1]-base[1]*(1-alpha),full[2]-base[2]*(1-alpha),alpha};}
// Keep original Principled socket defaults intact; only BTS-owned links are installed.
bool subsurface_shader(Binding &binding,bool globally_enabled){
 auto &tree=*binding.material->nodetree;auto *surface=native_principled_surface(*binding.material);if(!surface)return false;
 auto *existing=bke::node_find_node_by_name(tree,"__BTS_SubsurfaceWeight");const bool present=binding.scattering_slot>=0&&binding.user_unpremultiply[binding.scattering_slot];if(!present&&!existing)return false;
 bool changed=false;const auto destination=[&](const char *name)->bNodeSocket &{auto *socket=bke::node_find_enabled_input_socket(*surface,name);if(!socket)throw std::runtime_error("Principled subsurface input missing");return *socket;};
 constexpr auto prior_method="bts_subsurface_original_method";auto *prior=binding.material->id.properties?IDP_GetPropertyFromGroup(binding.material->id.properties,prior_method):nullptr;
 if(!present){for(auto *name:{"Subsurface Weight","Subsurface Scale","Subsurface Radius"}){auto &socket=destination(name);for(auto *link=static_cast<bNodeLink *>(tree.links.first);link;){auto *next=link->next;if(link->tosock==&socket&&STRPREFIX(link->fromnode->name,"__BTS_Subsurface")){bke::node_remove_link(&tree,*link);changed=true;}link=next;}}if(prior&&prior->type==IDP_INT&&surface->custom2!=IDP_int_get(prior)){surface->custom2=IDP_int_get(prior);BKE_ntree_update_tag_node_property(&tree,surface);changed=true;}return changed;}
 if(!prior)IDP_AddToGroup(IDP_EnsureProperties(&binding.material->id),IDP_NewInt(surface->custom2,prior_method));
 const auto &value=binding.shader_parameters.subsurface;const auto prior_count=tree.nodes.count();auto &weight=node(tree,"ShaderNodeMath","__BTS_SubsurfaceWeight");weight.custom1=NODE_MATH_MULTIPLY;auto &scale=node(tree,"ShaderNodeMath","__BTS_SubsurfaceScale");scale.custom1=NODE_MATH_MULTIPLY;auto &radius=node(tree,"ShaderNodeCombineXYZ","__BTS_SubsurfaceRadius");changed|=prior_count!=tree.nodes.count();
 const auto number=[&](bNodeSocket &socket,float v){if(static_cast<bNodeSocketValueFloat *>(socket.default_value)->value!=v){scalar(tree,socket,v);changed=true;}};
 const auto connect=[&](bNode &from,const char *name,bNode &to,bNodeSocket &target){auto &source=output(from,name);for(const auto &link:tree.links)if(link.fromsock==&source&&link.tosock==&target)return;bke::node_remove_socket_links(tree,target);bke::node_add_link(tree,from,source,to,target);changed=true;};
 number(input(weight,1),globally_enabled&&value.enabled?1.f:0.f);number(input(scale,0),value.scale);number(input(scale,1),1);for(int i=0;i<3;++i)number(input(radius,i),value.color[i]);
 connect(*binding.user_unpremultiply[binding.scattering_slot],"Vector",weight,input(weight,0));connect(weight,"Value",*surface,destination("Subsurface Weight"));connect(scale,"Value",*surface,destination("Subsurface Scale"));connect(radius,"Vector",*surface,destination("Subsurface Radius"));
 const int method=value.type==0?SHD_SUBSURFACE_RANDOM_WALK_SKIN:SHD_SUBSURFACE_RANDOM_WALK;if(surface->custom2!=method){surface->custom2=method;BKE_ntree_update_tag_node_property(&tree,surface);changed=true;}return changed;
}
Binding bind(Main &main,std::uint32_t uid)
{
  Material *material=nullptr;
  for (Material &candidate : main.materials) if (candidate.id.session_uid==uid) material=&candidate;
  if (!material || !material->nodetree) throw std::runtime_error("Inspection material is not in the live project");
  auto *principled=native_principled_surface(*material);
  auto *surface_output=native_material_output(*material);
  if (!principled || !surface_output) throw std::runtime_error("Inspection requires a direct Principled material");
  auto &tree=*material->nodetree;
  // Until native Open has an explicit schema/rebind contract, never adopt unknown nodes.
  for (const char *name : {"__BTS_ViewSwitch","__BTS_Inspection","__BTS_InspectCoverage","__BTS_Checker","__BTS_UvCoordinates"})
    if (bke::node_find_node_by_name(tree,name)) throw std::runtime_error("Reserved inspection node name already exists");
  auto &view_switch=node(tree,"ShaderNodeMixShader","__BTS_ViewSwitch");
  auto &emission=node(tree,"ShaderNodeEmission","__BTS_Inspection");
  auto &coverage=node(tree,"ShaderNodeMixRGB","__BTS_InspectCoverage");
  {
    auto &checker=node(tree,"ShaderNodeTexChecker","__BTS_Checker");
    auto &coordinates=node(tree,"ShaderNodeTexCoord","__BTS_UvCoordinates");
    scalar(tree,input(view_switch,0),0);
    scalar(tree,input(emission,1),1);
    color(tree,input(checker,1),{.16f,.16f,.16f,1});
    color(tree,input(checker,2),{.32f,.32f,.32f,1});
    scalar(tree,input(checker,3),16);
    auto *surface=bke::node_find_enabled_input_socket(*surface_output,"Surface");
    if (!surface) throw std::runtime_error("Material output Surface is missing");
    bke::node_remove_socket_links(tree,*surface);
    bke::node_add_link(tree,*principled,output(*principled,"BSDF"),view_switch,input(view_switch,1));
    bke::node_add_link(tree,emission,output(emission,"Emission"),view_switch,input(view_switch,2));
    bke::node_add_link(tree,view_switch,output(view_switch,"Shader"),*surface_output,*surface);
    bke::node_add_link(tree,coordinates,output(coordinates,"UV"),checker,input(checker,0));
    bke::node_add_link(tree,checker,output(checker,"Color"),coverage,input(coverage,1));
    bke::node_add_link(tree,coverage,output(coverage,"Color"),emission,input(emission,0));
    BKE_ntree_update_after_single_tree_change(main,tree);
  }
  Binding binding{material,&view_switch,&coverage};binding.owner_main=&main;return binding;
}
// A derived, unmanaged mask image drives emission in both native views. It never
// replaces parent material channels or the sparse authoring pixels.
PaintPixels::Bounds evaluated_bounds(const TextureSet &set,int tile,PaintPixels::Bounds bounds){const double sx=double(set.width(tile))/set.texture_resolution,sy=double(set.rows(tile))/set.rows();return {std::clamp(int(std::floor(bounds.x0*sx)),0,set.width(tile)),std::clamp(int(std::floor(bounds.y0*sy)),0,set.rows(tile)),std::clamp(int(std::ceil(bounds.x1*sx)),0,set.width(tile)),std::clamp(int(std::ceil(bounds.y1*sy)),0,set.rows(tile))};}
void ensure_ao_image(Main &main,Binding &binding,const TextureSet &set,int resolution){
  const int height=set.rows();
  if(binding.ao_image)return;
  auto &tree=*binding.material->nodetree;auto &texture=node(tree,"ShaderNodeTexImage","__BTS_AOPixels");const float white[4]={1,1,1,1};
  binding.ao_image=native_generated_image(&main,set.width(1001),set.rows(1001),"BTS Complete Ambient Occlusion",32,1,IMA_GENTYPE_BLANK,white,false,true,set.tiles!=std::set<int>{1001});if(!binding.ao_image)throw std::runtime_error("Cannot allocate ambient occlusion image");binding.ao_image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;if(texture.id)id_us_min(texture.id);texture.id=&binding.ao_image->id;id_us_plus(texture.id);
  if(binding.ao_image->source==IMA_SRC_TILED){for(int number:set.tiles)if(number!=1001){auto *tile=BKE_image_add_tile(binding.ao_image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate AO UV tile");tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;std::copy_n(white,4,tile->gen_color);if(!native_fill_tile(binding.ao_image,tile))throw std::runtime_error("Cannot fill AO UV tile");}if(!set.tiles.contains(1001))BKE_image_remove_tile(binding.ao_image,BKE_image_get_tile(binding.ao_image,1001));}
  auto &multiply=node(tree,"ShaderNodeMixRGB","__BTS_AOMaterial");multiply.custom1=MA_RAMP_MULT;scalar(tree,input(multiply,0),binding.shader_parameters.ao_intensity);bke::node_add_link(tree,*binding.base_mix[0],output(*binding.base_mix[0],"Color"),multiply,input(multiply,1));bke::node_add_link(tree,texture,output(texture,"Color"),multiply,input(multiply,2));auto *principled=native_principled_surface(*binding.material);auto *base=principled?bke::node_find_enabled_input_socket(*principled,"Base Color"):nullptr;if(!base)throw std::runtime_error("AO material base input is missing");bke::node_remove_socket_links(tree,*base);bke::node_add_link(tree,multiply,output(multiply,"Color"),*principled,*base);BKE_ntree_update_after_single_tree_change(main,tree);
}

void baked_image(Main &main,Binding &binding,const MeshMap &map,const TextureSet &set) {
  if(map.tiles.empty())throw std::runtime_error("Baked map has no tiles");
  std::string hash,shape;
  for(int number:set.tiles){const auto found=map.tiles.find(number);const int size=found==map.tiles.end()?map.tiles.begin()->second.resolution:found->second.resolution;shape+=std::to_string(number)+":"+std::to_string(size)+":"+std::to_string(found==map.tiles.end()?map.tiles.begin()->second.rows():found->second.rows())+";";if(found!=map.tiles.end())hash+=found->second.hash;}
  hash+=shape;if(binding.bake_image&&binding.bake_hash==hash)return;
  auto &tree=*binding.material->nodetree;
  if(!binding.bake_texture)binding.bake_texture=&node(tree,"ShaderNodeTexImage","__BTS_MeshMapInspection");
  if(!binding.bake_image||binding.bake_shape!=shape){
    if(binding.bake_texture->id)id_us_min(binding.bake_texture->id);
    const float zero[4]={0,0,0,0};const int resolution=map.tiles.contains(1001)?map.tiles.at(1001).resolution:map.tiles.begin()->second.resolution;
    const int height=map.tiles.contains(1001)?map.tiles.at(1001).rows():map.tiles.begin()->second.rows();
    binding.bake_image=native_generated_image(&main,resolution,height,"BTS Mesh Map Inspection",32,1,IMA_GENTYPE_BLANK,zero,false,true,set.tiles!=std::set<int>{1001});
    if(!binding.bake_image)throw std::runtime_error("Cannot allocate mesh map inspection image");
    binding.bake_image->flag|=IMA_HIGH_BITDEPTH;binding.bake_image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;binding.bake_texture->id=&binding.bake_image->id;id_us_plus(binding.bake_texture->id);binding.bake_shape=shape;
    reinterpret_cast<NodeTexImage *>(binding.bake_texture->storage)->extension=SHD_IMAGE_EXTENSION_CLIP;
    if(binding.bake_image->source==IMA_SRC_TILED){
      for(int number:set.tiles)if(number!=1001){auto *tile=BKE_image_add_tile(binding.bake_image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate baked UV tile");tile->gen_x=map.tiles.contains(number)?map.tiles.at(number).resolution:resolution;tile->gen_y=map.tiles.contains(number)?map.tiles.at(number).rows():height;tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;if(!native_fill_tile(binding.bake_image,tile))throw std::runtime_error("Cannot fill baked UV tile");}
      if(!set.tiles.contains(1001))BKE_image_remove_tile(binding.bake_image,BKE_image_get_tile(binding.bake_image,1001));
    }
  }
  for(int number:set.tiles){
    const auto found=map.tiles.find(number);const MeshMapPlane *plane=found==map.tiles.end()?nullptr:&found->second;
    ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(binding.bake_image,&user,nullptr);
    if(!buffer||!buffer->float_data()||(plane&&(!plane->pixels||buffer->x!=plane->resolution||buffer->y!=plane->rows()))){if(buffer)BKE_image_release_ibuf(binding.bake_image,buffer,nullptr);throw std::runtime_error("Baked inspection buffer mismatch");}
    if(plane)std::copy(plane->pixels->rgba.begin(),plane->pixels->rgba.end(),buffer->float_data_for_write());
    else std::fill_n(buffer->float_data_for_write(),std::size_t(buffer->x)*buffer->y*4,0.f);
    BKE_image_mark_dirty(binding.bake_image,buffer);BKE_image_update_gputexture_delayed(binding.bake_image,BKE_image_get_tile(binding.bake_image,number),buffer,0,0,buffer->x,buffer->y);BKE_image_release_ibuf(binding.bake_image,buffer,nullptr);
  }
  if(!binding.bake_unpremultiply){binding.bake_unpremultiply=&node(tree,"ShaderNodeVectorMath","__BTS_MeshMapUnpremultiply");binding.bake_unpremultiply->custom1=NODE_VECTOR_MATH_DIVIDE;}
  bke::node_remove_socket_links(tree,input(*binding.bake_unpremultiply,0));bke::node_remove_socket_links(tree,input(*binding.bake_unpremultiply,1));
  bke::node_add_link(tree,*binding.bake_texture,output(*binding.bake_texture,"Color"),*binding.bake_unpremultiply,input(*binding.bake_unpremultiply,0));bke::node_add_link(tree,*binding.bake_texture,output(*binding.bake_texture,"Alpha"),*binding.bake_unpremultiply,input(*binding.bake_unpremultiply,1));binding.bake_hash=hash;
}
void ensure_authored_normal_image(Main &main,Binding &binding,const TextureSet &set){
  if(binding.authored_normal_image)return;auto &tree=*binding.material->nodetree;auto &texture=node(tree,"ShaderNodeTexImage","__BTS_AuthoredNormalPixels");if(texture.id){if(GS(texture.id->name)!=ID_IM)throw std::runtime_error("Invalid authored normal reference");binding.authored_normal_image=reinterpret_cast<Image *>(texture.id);return;}
  const float flat[4]={.5f,.5f,1,1};auto *image=native_generated_image(&main,set.width(1001),set.rows(1001),"BTS Authored Normal Inspection",32,1,IMA_GENTYPE_BLANK,flat,false,true,set.tiles!=std::set<int>{1001});if(!image)throw std::runtime_error("Cannot allocate authored normal inspection");binding.authored_normal_image=image;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;texture.id=&image->id;id_us_plus(texture.id);if(image->source==IMA_SRC_TILED){for(int number:set.tiles)if(number!=1001){auto *tile=BKE_image_add_tile(image,number,nullptr);if(!tile)throw std::runtime_error("Cannot add authored normal tile");tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;std::copy_n(flat,4,tile->gen_color);if(!native_fill_tile(image,tile))throw std::runtime_error("Cannot fill authored normal tile");}if(!set.tiles.contains(1001))BKE_image_remove_tile(image,BKE_image_get_tile(image,1001));}binding.displayed_channel=-2;
}
void update_user_image(Main &main,Binding &binding,const TextureSet &set,int slot,PaintPixels::Bounds bounds,bool interactive,bool allocate_only=false){
  const int width=set.texture_resolution,height=set.rows();auto &tree=*binding.material->nodetree;auto &image=binding.user_images[slot];const auto texture_name="__BTS_UserPixels"+std::to_string(slot),divide_name="__BTS_UserUnpremultiply"+std::to_string(slot);
  auto &texture=node(tree,"ShaderNodeTexImage",texture_name.c_str());binding.user_textures[slot]=&texture;
  if(!image&&texture.id){if(GS(texture.id->name)!=ID_IM)throw std::runtime_error("Invalid custom image reference");image=reinterpret_cast<Image *>(texture.id);}
  if(!image){const float empty[4]={};image=native_generated_image(&main,set.width(1001),set.rows(1001),("BTS Custom "+set.users[slot].name).c_str(),32,1,IMA_GENTYPE_BLANK,empty,false,true,set.tiles!=std::set<int>{1001});if(!image)throw std::runtime_error("Cannot allocate custom channel image");image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;texture.id=&image->id;id_us_plus(texture.id);if(image->source==IMA_SRC_TILED){for(int number:set.tiles)if(number!=1001){auto *tile=BKE_image_add_tile(image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate custom UV tile");tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;if(!native_fill_tile(image,tile))throw std::runtime_error("Cannot fill custom UV tile");}if(!set.tiles.contains(1001))BKE_image_remove_tile(image,BKE_image_get_tile(image,1001));}bounds={0,0,width,height};}
  if(!binding.user_unpremultiply[slot]){auto &divide=node(tree,"ShaderNodeVectorMath",divide_name.c_str());divide.custom1=NODE_VECTOR_MATH_DIVIDE;BKE_ntree_update_tag_node_property(&tree,&divide);bke::node_add_link(tree,texture,output(texture,"Color"),divide,input(divide,0));bke::node_add_link(tree,texture,output(texture,"Alpha"),divide,input(divide,1));binding.user_unpremultiply[slot]=&divide;binding.displayed_channel=-2;}
  if(allocate_only)return;
  const auto parent_bounds=bounds;for(int number:set.tiles){const int width=set.width(number),height=set.rows(number);const auto bounds=evaluated_bounds(set,number,parent_bounds);ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);if(!buffer||buffer->x!=width||buffer->y!=height||!buffer->float_data()){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Invalid custom composite dimensions");}auto *data=buffer->float_data_for_write();for(int y=bounds.y0;y<bounds.y1;++y)for(int x=bounds.x0;x<bounds.x1;++x){const auto value=TextureDocument::composite_user(set,slot,x,y,number);const auto offset=(std::size_t(y)*width+x)*4;data[offset]=data[offset+1]=data[offset+2]=value[0]*value[1];data[offset+3]=value[1];}if(binding.padding&&!interactive)binding.padding->apply(*buffer,number,set.padding_3d);BKE_image_mark_dirty(image,buffer);const auto dirty=interactive?bounds:PaintPixels::Bounds{0,0,width,height};BKE_image_update_gputexture_delayed(image,BKE_image_get_tile(image,number),buffer,dirty.x0,dirty.y0,dirty.x1-dirty.x0,dirty.y1-dirty.y0);BKE_image_release_ibuf(image,buffer,nullptr);}
  DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SHADING);
}
// Scalar Displacement is independent of the five legacy material channels and eight user slots.
bool displacement_shader(Main &main,Binding &binding){
  if(!binding.displacement_image)return false;
  auto &tree=*binding.material->nodetree;auto *material_output=native_material_output(*binding.material);
  auto *destination=material_output?bke::node_find_enabled_input_socket(*material_output,"Displacement"):nullptr;
  if(!destination)throw std::runtime_error("Material output Displacement is missing");
  const auto &settings=binding.displacement_settings;
  auto &mix=*binding.displacement_mix;auto &disp=*binding.displacement_node;
  const bool active=settings.channel&&settings.enabled&&(binding.preview_active[16]?binding.preview_displacement_content:binding.gpu_displacement_presence.value_or(binding.displacement_count>0))&&settings.strength>0;
  const bool linked=destination->link&&destination->link->fromnode==&disp;
  if(destination->link&&!linked)throw std::runtime_error("Displacement requires an unlinked Material Output Displacement input");
  bool changed=false;
  const auto assign=[&](bNodeSocket &socket,float value){auto *storage=static_cast<bNodeSocketValueFloat *>(socket.default_value);if(storage->value!=value){scalar(tree,socket,value);changed=true;}};
  auto *base=static_cast<bNodeSocketValueRGBA *>(input(mix,1).default_value);if(base->value[0]!=settings.midlevel){color(tree,input(mix,1),{settings.midlevel,settings.midlevel,settings.midlevel,1});changed=true;}
  assign(input(disp,1),settings.midlevel);assign(input(disp,2),settings.strength);
  if(active!=linked){bke::node_remove_socket_links(tree,*destination);if(active)bke::node_add_link(tree,disp,output(disp,"Displacement"),*material_output,*destination);changed=true;}
  const auto method=active?MA_DISPLACEMENT_BOTH:MA_DISPLACEMENT_BUMP;
  if(binding.material->displacement_method!=method){binding.material->displacement_method=method;changed=true;}
  const float bounds=active?std::max(settings.bounds(),settings.strength*std::max(std::abs(binding.displacement_minimum-settings.midlevel),std::abs(binding.displacement_maximum-settings.midlevel))*1.05f+1e-6f):0;const bool bounds_changed=binding.material->inflate_bounds!=bounds;if(bounds_changed){binding.material->inflate_bounds=bounds;DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SYNC_TO_EVAL);}
  if(changed){BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING|ID_RECALC_SYNC_TO_EVAL);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);}return changed||bounds_changed;
}
void ensure_displacement_image(Main &main,Binding &binding,const TextureSet &set){
  binding.displacement_settings=set.displacement;
  if(!set.displacement.allocated){displacement_shader(main,binding);return;}
  auto &tree=*binding.material->nodetree;auto &image=binding.displacement_image;
  auto &texture=node(tree,"ShaderNodeTexImage","__BTS_PixelsDisp");binding.displacement_texture=&texture;
  if(!image&&texture.id){if(GS(texture.id->name)!=ID_IM)throw std::runtime_error("Invalid displacement image reference");image=reinterpret_cast<Image *>(texture.id);}
  if(!image){const float empty[4]={};image=native_generated_image(&main,set.width(1001),set.rows(1001),"BTS Displacement",32,1,IMA_GENTYPE_BLANK,empty,false,true,set.tiles!=std::set<int>{1001});if(!image)throw std::runtime_error("Cannot allocate displacement image");image->flag|=IMA_HIGH_BITDEPTH;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;texture.id=&image->id;id_us_plus(texture.id);if(image->source==IMA_SRC_TILED){for(int number:set.tiles)if(number!=1001){auto *tile=BKE_image_add_tile(image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate displacement UV tile");tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;if(!native_fill_tile(image,tile))throw std::runtime_error("Cannot fill displacement UV tile");}if(!set.tiles.contains(1001))BKE_image_remove_tile(image,BKE_image_get_tile(image,1001));}}
  if(!binding.displacement_node){
    auto &divide=node(tree,"ShaderNodeVectorMath","__BTS_UnpremultiplyDisp");divide.custom1=NODE_VECTOR_MATH_DIVIDE;BKE_ntree_update_tag_node_property(&tree,&divide);
    auto &mix=node(tree,"ShaderNodeMixRGB","__BTS_BaseMixDisp");auto &disp=node(tree,"ShaderNodeDisplacement","__BTS_Displacement");disp.custom1=SHD_SPACE_WORLD;BKE_ntree_update_tag_node_property(&tree,&disp);
    bke::node_add_link(tree,texture,output(texture,"Color"),divide,input(divide,0));bke::node_add_link(tree,texture,output(texture,"Alpha"),divide,input(divide,1));
    bke::node_add_link(tree,texture,output(texture,"Alpha"),mix,input(mix,0));bke::node_add_link(tree,divide,output(divide,"Vector"),mix,input(mix,2));bke::node_add_link(tree,mix,output(mix,"Color"),disp,input(disp,0));
    binding.displacement_unpremultiply=&divide;binding.displacement_mix=&mix;binding.displacement_node=&disp;binding.displayed_channel=-2;BKE_ntree_update_after_single_tree_change(main,tree);
  }
  displacement_shader(main,binding);
}
void update_displacement_image(Main &main,Binding &binding,const TextureSet &set,PaintPixels::Bounds bounds,bool interactive){
  binding.gpu_displacement_presence.reset();++binding.displacement_cpu_epoch;const bool created=!binding.displacement_image;ensure_displacement_image(main,binding,set);if(!set.displacement.allocated)return;
  auto *image=binding.displacement_image;if(created)bounds={0,0,set.texture_resolution,set.rows()};
  const auto parent_bounds=bounds;for(int number:set.tiles){const int width=set.width(number),height=set.rows(number);const auto dirty=evaluated_bounds(set,number,parent_bounds);auto &presence=binding.displacement_presence[number];if(presence.empty())presence.resize(std::size_t(width)*height);
    ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);if(!buffer||buffer->x!=width||buffer->y!=height||!buffer->float_data()){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Invalid displacement composite dimensions");}
    auto *data=buffer->float_data_for_write();for(int y=dirty.y0;y<dirty.y1;++y)for(int x=dirty.x0;x<dirty.x1;++x){const auto value=TextureDocument::composite_displacement(set,x,y,number);const auto pixel=std::size_t(y)*width+x,index=pixel*4;const bool content=value[1]>0;if(content){binding.displacement_minimum=std::min(binding.displacement_minimum,value[0]);binding.displacement_maximum=std::max(binding.displacement_maximum,value[0]);}if(bool(presence[pixel])!=content){if(content)++binding.displacement_count;else --binding.displacement_count;presence[pixel]=content;}data[index]=data[index+1]=data[index+2]=value[0]*value[1];data[index+3]=value[1];}
    if(binding.padding&&!interactive)binding.padding->apply(*buffer,number,set.padding_3d);BKE_image_mark_dirty(image,buffer);const auto upload=interactive?dirty:PaintPixels::Bounds{0,0,width,height};BKE_image_update_gputexture_delayed(image,BKE_image_get_tile(image,number),buffer,upload.x0,upload.y0,upload.x1-upload.x0,upload.y1-upload.y0);BKE_image_release_ibuf(image,buffer,nullptr);
  }DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SHADING);displacement_shader(main,binding);
}
void mask_image(Main &main,Binding &binding,const TextureSet &set,const LayerMask &mask,int resolution,bool prepare_only=false)
{
  const int height=set.rows();
  auto &tree=*binding.material->nodetree;
  if(!binding.mask_texture) {
    binding.mask_texture=&node(tree,"ShaderNodeTexImage","__BTS_MaskInspection");
    if(binding.mask_texture->id) {
      if(GS(binding.mask_texture->id->name)!=ID_IM)throw std::runtime_error("Mask inspection image reference is invalid");
      binding.mask_image=reinterpret_cast<Image *>(binding.mask_texture->id);
    }
  }
  std::uint64_t hash=0;if(!prepare_only){hash=mask.pixels.hash(2);
  const auto include=[&](const QByteArray &bytes){for(unsigned char byte:bytes){hash^=byte;hash*=1099511628211ULL;}};
  include(QJsonDocument(mask_effects_json(mask.effects)).toJson(QJsonDocument::Compact));if(mask.channel_dependent())include(TextureDocument::mask_dependency_signature(set));
  for(int tile:set.tiles)include(QByteArray::number(tile)+":"+QByteArray::number(set.width(tile))+":"+QByteArray::number(set.rows(tile)));
  for(int role=0;role<7;++role)if(set.mesh_maps[role]){include(QByteArray::number(role));for(const auto &[tile,plane]:set.mesh_maps[role]->tiles){include(QByteArray::number(tile));include(QByteArray::fromStdString(plane.pixels?plane.pixels->hash:plane.hash));}}
  if(binding.mask_cached&&binding.mask_hash==hash&&binding.mask_background==mask.background)return;}
  if(!binding.mask_image) {
    const float empty[4]={0,0,0,1};
    binding.mask_image=native_generated_image(&main,set.width(1001),set.rows(1001),"BTS Mask Inspection",32,1,IMA_GENTYPE_BLANK,empty,false,true,set.tiles!=std::set<int>{1001});
    if(!binding.mask_image)throw std::runtime_error("Cannot allocate mask inspection image");
    binding.mask_image->flag|=IMA_HIGH_BITDEPTH;binding.mask_image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;
    binding.mask_texture->id=&binding.mask_image->id;id_us_plus(binding.mask_texture->id);
    if(binding.mask_image->source==IMA_SRC_TILED) {
      for(int number:set.tiles)if(number!=1001) {
        auto *tile=BKE_image_add_tile(binding.mask_image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate mask UV tile");
        tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;
        if(!native_fill_tile(binding.mask_image,tile))throw std::runtime_error("Cannot initialize mask UV tile");
      }
      if(!set.tiles.contains(1001))BKE_image_remove_tile(binding.mask_image,BKE_image_get_tile(binding.mask_image,1001));
    }
  }
  if(prepare_only){binding.mask_cached=false;return;}
  auto *image=binding.mask_image;
  for(int number:set.tiles) {
    const int resolution=set.width(number),height=set.rows(number);
    ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);
    if(!buffer||buffer->x!=resolution||buffer->y!=height||!buffer->float_data()) {
      if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Mask inspection buffer mismatch");
    }
    auto *data=buffer->float_data_for_write();
    for(int y=0;y<height;++y)for(int x=0;x<resolution;++x) {
      const float value=mask.channel_dependent()?TextureDocument::mask_value(set,set.selected_layer,x,y,number):mask.value(x,y,number,resolution,height,set.mesh_maps[2]?&*set.mesh_maps[2]:nullptr,&set.mesh_maps);const auto i=(std::size_t(y)*resolution+x)*4;
      data[i]=data[i+1]=data[i+2]=value;data[i+3]=1;
    }
    BKE_image_mark_dirty(image,buffer);BKE_image_update_gputexture_delayed(image,BKE_image_get_tile(image,number),buffer,0,0,resolution,height);
    BKE_image_release_ibuf(image,buffer,nullptr);
  }
  DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SHADING);
  binding.mask_cached=true;binding.mask_hash=hash;binding.mask_background=mask.background;
}
bool cycles_shader_channel(const Binding &binding,int channel){
 if(channel==6)return binding.displayed_channel==int(InspectionMode::Normal);
 if(channel==15)return binding.displayed_channel==int(InspectionMode::Mask);
 return true;
}
Image *composite_image(Binding &binding,int channel){return channel<5?binding.images[channel]:channel==5?binding.ao_image:channel==6?binding.authored_normal_image:channel==15?binding.mask_image:channel==16?binding.displacement_image:binding.user_images.at(channel-7);}
bNode *composite_texture(Binding &binding,int channel){auto &tree=*binding.material->nodetree;return channel<5?binding.textures[channel]:channel==5?bke::node_find_node_by_name(tree,"__BTS_AOPixels"):channel==6?bke::node_find_node_by_name(tree,"__BTS_AuthoredNormalPixels"):channel==15?binding.mask_texture:channel==16?binding.displacement_texture:binding.user_textures.at(channel-7);}
void image_binding(Main &main,Binding &binding,int channel,Image *image){auto *texture=composite_texture(binding,channel);if(!texture||texture->id==&image->id)return;auto &tree=*binding.material->nodetree;if(texture->id)id_us_min(texture->id);texture->id=&image->id;id_us_plus(texture->id);BKE_ntree_update_tag_node_property(&tree,texture);}
bool restore_preview(Main &main,Binding &binding){bool changed=false;for(int c=0;c<17;++c)if(binding.preview_active[c]){if(auto *image=composite_image(binding,c))image_binding(main,binding,c,image);binding.preview_active[c]=false;changed=true;}binding.edit_channels={};binding.gpu_uniform_values={};binding.gpu_stack_active={};binding.gpu_unbound_waiting={};binding.gpu_stack_set.reset();if(changed){auto &tree=*binding.material->nodetree;BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);}return changed;}
void finish_cpu_channels(Main &main,Binding &binding,const CompositeChannels &channels){
 bool rebound=false;
 for(int c=0;c<17;++c)if(channels[c]){
  if(binding.preview_active[c]){if(auto *image=composite_image(binding,c))image_binding(main,binding,c,image);binding.preview_active[c]=false;rebound=true;}
  binding.edit_channels[c]=false;binding.gpu_stack_active[c]=false;binding.gpu_unbound_waiting[c]=false;if(c<5)binding.gpu_uniform_values[c].reset();
 }
 if(rebound){auto &tree=*binding.material->nodetree;BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);}
}
// Finishing one dirty tile before upload prevents repeated 256x256 transfers
// when a 64x64 staging boundary falls on another frame.
constexpr int image_update_block=256;
// One bounded owner-thread block; CPU staging still yields after 3 ms.
bool stage_block(Main &main,Binding &binding,const NativeCompositeResult &result,int number,int c,int x0,int y0){performance::Scope timing("composite_stage_block");
  property_upload_channel(c);
  auto *image=composite_image(binding,c);if(!image)return false;const auto &source=result.tiles.at(number)[c];if(source.empty())return false;
  ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);if(!buffer||!buffer->float_data()||source.size()!=std::size_t(buffer->x)*buffer->y*4){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Background composite dimensions changed");}
  const int x1=std::min(x0+image_update_block,buffer->x),y1=std::min(y0+image_update_block,buffer->y);auto *destination=buffer->float_data_for_write();bool dirty=false;
  for(int y=y0;y<y1&&!dirty;++y){const auto offset=(std::size_t(y)*buffer->x+x0)*4;dirty=!std::equal(source.begin()+offset,source.begin()+offset+(x1-x0)*4,destination+offset);}
  if(c==16){binding.gpu_displacement_presence.reset();++binding.displacement_cpu_epoch;}
  if(dirty){if(c==15)binding.mask_cached=false;for(int y=y0;y<y1;++y){const auto offset=(std::size_t(y)*buffer->x+x0)*4;std::copy_n(source.data()+offset,(x1-x0)*4,destination+offset);}
    if(c==16){auto &presence=binding.displacement_presence[number];if(presence.empty())presence.resize(std::size_t(buffer->x)*buffer->y);for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){const auto index=std::size_t(y)*buffer->x+x;const bool content=source[index*4+3]>0;if(content){const float value=source[index*4]/source[index*4+3];binding.displacement_minimum=std::min(binding.displacement_minimum,value);binding.displacement_maximum=std::max(binding.displacement_maximum,value);}if(bool(presence[index])!=content){if(content)++binding.displacement_count;else --binding.displacement_count;presence[index]=content;}}}
    BKE_image_mark_dirty(image,buffer);BKE_image_update_gputexture_delayed(image,BKE_image_get_tile(image,number),buffer,x0,y0,x1-x0,y1-y0);DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SHADING);
  }BKE_image_release_ibuf(image,buffer,nullptr);return dirty;
}

}
struct MaterialInspection::Impl {
  NativeCyclesImages cycles;
  NativeFragmentGeometryProvider world_geometry_provider;
  NativeFragmentObjectProvider world_object_provider;
  void invalidate_world_geometry(Binding &binding){
    binding.world_context.invalidate_owners();binding.world_ao_lease.reset();binding.world_ao_tile=0;
    if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;
    binding.world_geometry_job.reset();binding.world_geometry.reset();
    binding.world_published_signatures.clear();binding.world_tiles.clear();binding.world_set.reset();++binding.world_geometry_revision;
  }
  bool observe_world_objects(Binding &binding,std::uint32_t material){return binding.world_object_context.observe(world_object_provider,material);}
  bool cycles_entry_waiting=false,cycles_error_reported=false,cycles_entry_requested=false,cycles_preparation_failed=false;std::string cycles_preparation_error;
  bool subsurface_enabled=false,gpu_mips_pending=false;
  bool cpu_pending()const{return job.valid()||!queued.empty()||pending.has_value();}
  std::map<std::uint32_t,std::string> failures;std::map<std::uint32_t,CompositeChannels> failed_channels;
  std::unordered_map<std::uint32_t,Binding> bindings;std::vector<Image *> retired_visibility_images;
  struct Request{TextureSet set;std::shared_ptr<TexturePadding> padding;std::uint64_t generation;bool ao;CompositeChannels channels=complete_composite_channels;bool layer_edit=false;PropertyTrace trace;performance::Clock::time_point enqueued=performance::Clock::now();bool finalize=true;};
  std::map<std::uint32_t,std::uint64_t> generation;std::map<std::uint32_t,Request> queued;std::future<std::optional<NativeCompositeResult>> job;std::shared_ptr<std::atomic_bool> cancel;std::uint32_t running_uid=0;std::optional<Request> running;
  struct Pending{
    NativeCompositeResult result;std::vector<int> tiles;std::size_t tile=0;int channel=0,x=0,y=0;bool layer_edit=false;
    Pending()=default;Pending(Pending &&)=default;Pending &operator=(Pending &&)=default;
    ~Pending(){
      // Plain CPU vectors only: their large virtual mappings need not be
      // unmapped inside the owner event when staging or cancellation finishes.
      if(!result.tiles.empty())QThreadPool::globalInstance()->start([pixels=std::move(result.tiles)]()mutable{performance::Scope timing("composite_free_cpu_buffers");pixels.clear();});
    }
  };
  std::optional<Pending> pending;
  struct GpuUniformRequest{std::uint64_t generation;std::array<std::optional<std::array<float,4>>,5> values;PropertyTrace trace;performance::Clock::time_point enqueued=performance::Clock::now();};
  std::map<std::uint32_t,GpuUniformRequest> gpu_queued;
  struct LayerCaptureJob {TextureSet set;LayerCaptureRequest request;std::uint64_t ticket;};
  std::optional<LayerCaptureJob> layer_capture_job;std::optional<LayerCaptureResult> layer_capture_failure;std::uint64_t layer_capture_ticket=0;
  struct ThumbnailJob {TextureSet set;ThumbnailRequest request;std::uint64_t ticket;};
  std::optional<ThumbnailJob> thumbnail_job;std::optional<ThumbnailMetadata> thumbnail_failure;std::uint64_t thumbnail_ticket=0;
  struct MaterialSampleJob {TextureSet set;MaterialSampleRequest request;std::uint64_t ticket;};
  std::optional<MaterialSampleJob> material_sample_job;std::optional<MaterialSample> material_sample_failure;std::uint64_t material_sample_ticket=0;
  struct HistogramJob {TextureSet set;HistogramRequest request;std::uint64_t ticket;};
  std::optional<HistogramJob> histogram_job;std::optional<std::pair<QUuid,HistogramMetadata>> histogram_failure;std::uint64_t histogram_ticket=0;
  std::vector<std::shared_ptr<const FillTexture>> preparing_textures;
  QUuid texture_preparation_owner;bool texture_upload_waiting=false;std::string texture_upload_error;
  GpuLayerPreview gpu_preview;MaterialLayeringSources layering_sources;bool layering_sources_pending=false;bool gpu_displacement_changed=false;std::map<std::uint32_t,Request> gpu_stack_queued;
  // Native chains stay vectors until explicit export. Each Fill is rasterized on
  // the actual mesh into target-UV tangent space, then the ordinary GPU layer
  // compositor applies masks, folders, blend modes and opacity.
  void queue_world_normal(Binding &binding,const TextureSet &source,bool view_only=false){
    auto &library=gpu_preview.native_world_sources();
    TextureSet set=source;set.projection_tiles=binding.projection_cache;set.geometry_ownership=binding.ownership_cache;set.geometry_names=binding.geometry_names;
    if(!binding.projection_cache.empty())set.projection_geometry=binding.projection_cache.begin()->second;
    std::vector<QUuid> perturbed;for(const auto &layer:evaluation_layers(set))if(layer.fill.native_material&&library.contains(layer.fill.material_resource)&&library.world_normal_perturbed(layer.fill.material_resource))perturbed.push_back(layer.id);
    const auto plan=native_world_normal_plan(set,perturbed);
    // Retire the former single-layer world override, including restored projects.
    if(binding.world_normal_enabled){auto &socket=output(*binding.world_normal_enabled,"Value");if(static_cast<const bNodeSocketValueFloat *>(socket.default_value)->value!=0){scalar(*binding.material->nodetree,socket,0);BKE_ntree_update_after_single_tree_change(*binding.owner_main,*binding.material->nodetree);DEG_id_tag_update_ex(binding.owner_main,&binding.material->id,ID_RECALC_SHADING);}}
    if(binding.world_images_full){for(int side=0;side<2;++side){native_world_normal_placeholder(*binding.world_normal_images[side]);}binding.world_images_full=false;binding.world_scratch={};}
    gpu_preview.clear_native_normals(set.id);binding.world_layers_ready.clear();binding.world_tiles.clear();binding.world_set.reset();binding.world_error.clear();binding.world_tile=0;
    if(!plan.eligible){if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;binding.world_geometry_job.reset();binding.world_error=plan.reason.toStdString();if(!binding.world_error.empty())gpu_preview.set_native_normal_error(source.id,binding.world_error);return;}
    try{
      if(!world_geometry_provider)throw std::runtime_error("Native Normal geometry owner is not ready");
      NativeFragmentGeometryRequest request;request.material_uid=set.material_session_uid;request.tiles.assign(set.tiles.begin(),set.tiles.end());request.target_uv={NativeFragmentUvKind::DefaultRender,{}};request.uv_requests={{NativeFragmentUvKind::DefaultRender,{}},{NativeFragmentUvKind::DisplayActive,{}}};
      std::set<std::string> uvs;bool needs_ao=false;
      for(const auto &part:plan.contributions){
        if(library.world_normal_context_state(part.resource)==NativeWorldNormalContextState::Unsupported)throw std::runtime_error(library.world_normal_error(part.resource).toStdString());
        const auto uv=library.world_normal_uv(part.resource);if(!uv.empty()&&uvs.insert(uv).second)request.uv_requests.push_back({NativeFragmentUvKind::Named,uv});
        request.generated_stream|=library.world_normal_requires_generated(part.resource);needs_ao|=library.world_normal_requires_baked_ao(part.resource);
      }
      request.allow_original_orco=request.generated_stream;
      std::string layout=std::to_string(request.generated_stream);for(const auto &uv:uvs)layout+=';'+uv;
      const bool objects_changed=observe_world_objects(binding,set.material_session_uid);
      if(!binding.world_object_context.error().empty())throw std::runtime_error(binding.world_object_context.error());
      if(objects_changed||layout!=binding.world_source_uv){if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;binding.world_geometry_job.reset();binding.world_geometry.reset();++binding.world_geometry_revision;}
      binding.world_source_uv=layout;binding.world_generated=request.generated_stream;request.revision=binding.world_geometry_revision;
      binding.world_ao=NativeMaterialBakedAoContext::capture(set.mesh_maps,request.tiles,needs_ao);binding.world_ao_lease.reset();binding.world_ao_tile=0;
      if(!binding.world_geometry&&!binding.world_geometry_job){const std::atomic_bool cancel=false;binding.world_geometry_job=NativeWorldNormalGeometryJob::start(world_geometry_provider(std::move(request),cancel));}
      binding.world_set=std::move(set);binding.world_tiles.assign(source.tiles.begin(),source.tiles.end());
      // Force derived shading refresh also for edits to source-only parameters.
      auto found=gpu_stack_queued.find(source.material_session_uid);
      if(found!=gpu_stack_queued.end()){found->second.channels[3]=true;found->second.channels[6]=true;}
      else {Request refresh{source,binding.padding,generation[source.material_session_uid],bool(binding.ao_image)};refresh.channels={};refresh.channels[3]=refresh.channels[6]=true;gpu_stack_queued.emplace(source.material_session_uid,std::move(refresh));}
    }catch(const std::exception &error){binding.world_error=error.what();pending_composite_error="Native Normal unavailable: "+binding.world_error;if(binding.mask_snapshot)gpu_preview.set_native_normal_error(binding.mask_snapshot->id,pending_composite_error);binding.world_set.reset();binding.world_tiles.clear();if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;binding.world_geometry_job.reset();}
  }
  bool pump_world_normal(Binding &binding){
    const auto uid=binding.material->id.session_uid;
    const bool changed=binding.world_objects_dirty||binding.world_queued_generation!=generation[uid]||binding.world_registry_generation!=gpu_preview.native_world_sources().source_generation();
    if(binding.mask_snapshot&&(changed||binding.world_view_dirty)){binding.world_view_dirty=false;binding.world_objects_dirty=false;binding.world_queued_generation=generation[uid];binding.world_registry_generation=gpu_preview.native_world_sources().source_generation();queue_world_normal(binding,*binding.mask_snapshot);}
    if(!binding.world_set)return false;
    try{
      if(binding.world_geometry_job){auto job=binding.world_geometry_job;if(!job->ready.load(std::memory_order_acquire))return false;if(!job->error.empty())throw std::runtime_error(job->error);if(!job->geometry)throw std::runtime_error("Native Normal geometry cancelled");binding.world_geometry=std::make_unique<NativeMaterialFragmentGpuGeometry>(job->geometry);binding.world_geometry_job.reset();}
      if(!binding.world_geometry||binding.world_tile>=binding.world_tiles.size())return false;
      auto &set=*binding.world_set;auto &library=gpu_preview.native_world_sources();std::vector<QUuid> ids;
      const auto &layers=evaluation_layers(set);for(const auto &layer:layers)if(layer.fill.native_material&&library.contains(layer.fill.material_resource)&&library.world_normal_perturbed(layer.fill.material_resource))ids.push_back(layer.id);
      const auto plan=native_world_normal_plan(set,ids);const int tile=binding.world_tiles[binding.world_tile];
      // Render all contributions for this tile; the compositor waits for every source.
      for(const auto &part:plan.contributions){
        if(binding.world_layers_ready.contains({part.layer,tile}))continue;
        const auto layer=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==part.layer;});
        std::optional<NativeMaterialViewContext> view;if(library.world_normal_requires_view(part.resource))view=gpu_preview.native_world_view(set,*layer,tile);
        const auto *ao_context=library.world_normal_requires_baked_ao(part.resource)?&binding.world_ao:nullptr;
        if(!library.prepare_world_normal(part.resource,&binding.world_geometry->source(),view?&*view:nullptr,ao_context,true)){if(library.world_normal_context_state(part.resource)==NativeWorldNormalContextState::Unsupported)throw std::runtime_error(library.world_normal_error(part.resource).toStdString());return false;}
        std::optional<NativeBakedAoInput> ao;
        if(ao_context){const auto &plane=binding.world_ao.plane(tile);if(binding.world_ao_tile!=tile){binding.world_ao_lease.reset();binding.world_ao_tile=tile;}const auto ready=gpu_preview.prepare_baked_resource(tile,plane,binding.world_ao_lease);if(ready==GpuPreviewUpdate::Pending)return false;if(ready!=GpuPreviewUpdate::Updated)throw std::runtime_error("Native Normal AO upload failed");ao=NativeBakedAoInput{tile,&plane,&binding.world_ao_lease};}
        const auto atlas=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;
        if(fill_surface_projection(layer->fill)&&!atlas)return false;
        const auto projection=native_material_projection(layer->fill,atlas.get(),set);

        std::shared_ptr<gpu::Texture> target(GPU_texture_create_2d("Native tangent Normal layer",set.width(tile),set.rows(tile),1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!target)throw std::bad_alloc();
        library.render_world_normal(part.resource,*layer->fill.native_material,*binding.world_geometry,tile,target.get(),nullptr,library.world_normal_uv(part.resource),view?&*view:nullptr,ao?&*ao:nullptr,true,projection.get());
        gpu_preview.set_native_normal(set.id,part.layer,tile,std::move(target));binding.world_layers_ready.insert({part.layer,tile});++binding.world_rendered_tiles;
      }
      binding.world_geometry_diagnostics=binding.world_geometry->diagnostics();++binding.world_tile;if(binding.world_tile==binding.world_tiles.size())++binding.world_publication_generation;
      return true;
    }catch(const std::exception &error){binding.world_error=error.what();pending_composite_error="Native Normal unavailable: "+binding.world_error;if(binding.mask_snapshot)gpu_preview.set_native_normal_error(binding.mask_snapshot->id,pending_composite_error);binding.world_set.reset();binding.world_tiles.clear();binding.world_geometry_job.reset();return false;}
  }

  bool prepare_layer_sources(bool allow_upload,bool frame_started);
  void collect_projection_geometry(){
    for(auto &[uid,binding]:bindings)if(binding.margin_job&&binding.margin_job->ready.load(std::memory_order_acquire)){
      auto job=std::move(binding.margin_job);
      if(job->cancelled||job->source!=binding.padding||job->sizes!=binding.tile_sizes)continue;
      if(!job->error.empty()){failures[uid]=job->error;pending_composite_error="Margin geometry failed: "+job->error;gpu_stack_queued.erase(uid);std::fprintf(stderr,"BTS_GPU_MARGIN_FAILED uid=%u %s\n",uid,job->error.c_str());continue;}
      gpu_preview.set_padding_geometry(uid,job->extend,job->adjacent);binding.margins_ready=true;
      std::printf("BTS_GPU_MARGIN_READY uid=%u tiles=%zu layer_composite_cpu=0\n",uid,job->extend.size());
    }
    for(auto &[uid,binding]:bindings)if(binding.projection_job&&binding.projection_job->ready.load(std::memory_order_acquire)){
      auto job=std::move(binding.projection_job);
      if(job->cancelled||job->source!=binding.padding||job->sizes!=binding.tile_sizes)continue;
      if(!job->error.empty()){failures[uid]=job->error;pending_composite_error="Projection geometry failed: "+job->error;gpu_stack_queued.erase(uid);std::fprintf(stderr,"BTS_GPU_GEOMETRY_FAILED uid=%u %s\n",uid,job->error.c_str());continue;}
      binding.projection_cache=std::move(job->result);binding.ownership_cache=std::move(job->ownership);binding.geometry_names=std::move(job->names);binding.geometry_upload_pending=true;
      const auto adopt=[&](const TextureSet &set){set.projection_tiles=binding.projection_cache;set.geometry_ownership=binding.ownership_cache;set.geometry_names=binding.geometry_names;if(!binding.projection_cache.empty())set.projection_geometry=binding.projection_cache.begin()->second;};
      if(auto request=gpu_stack_queued.find(uid);request!=gpu_stack_queued.end())adopt(request->second.set);
      if(auto request=queued.find(uid);request!=queued.end())adopt(request->second.set);
      if(binding.gpu_stack_set)adopt(*binding.gpu_stack_set);
      if(binding.mask_snapshot)adopt(*binding.mask_snapshot);if(binding.world_set)adopt(*binding.world_set);binding.world_queued_generation=~std::uint64_t(0);
      std::printf("BTS_GPU_GEOMETRY_READY uid=%u tiles=%zu layer_composite_cpu=0\n",uid,binding.projection_cache.size());
    }
  }
  void invalidate(std::uint32_t uid){gpu_queued.erase(uid);gpu_stack_queued.erase(uid);if(auto found=bindings.find(uid);found!=bindings.end()){found->second.mask_async_layer={};found->second.gpu_uniform_values={};found->second.gpu_stack_active={};found->second.gpu_unbound_waiting={};found->second.gpu_stack_set.reset();}if(pending&&pending->result.uid==uid)pending.reset();++generation[uid];queued.erase(uid);if(job.valid()&&running_uid==uid&&cancel)cancel->store(true);}
};
std::uint64_t MaterialInspection::release_bake_scratch(){return impl_->gpu_preview.release_bake_scratch();}
void MaterialInspection::publish_native_material_preview(const std::string &resource,const std::string &hash){impl_->gpu_preview.publish_native_material_preview(resource,hash);}
void MaterialInspection::prepare_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph> graph){impl_->gpu_preview.prepare_native_material(std::move(resource),std::move(content),std::move(graph));}
void MaterialInspection::cancel_native_preparation(const std::string &resource){impl_->gpu_preview.cancel_native_preparation(resource);}
void MaterialInspection::discard_native_candidate(const std::string &resource){impl_->gpu_preview.discard_native_candidate(resource);}
void MaterialInspection::alias_native_material(const std::string &source,const std::string &destination){impl_->gpu_preview.alias_native_material(source,destination);}
void MaterialInspection::validate_material_layering(const TextureSet &set)const{
  if(set.material_layering.enabled){
    for(const auto &input:set.material_layering.materials)require_material_layer_udim_coverage(input,set.tiles);
  }
  using Check=LayeringTopology;
  const auto found=impl_->bindings.find(set.material_session_uid);
  Check::require(found!=impl_->bindings.end(),"Material layering requires an initialized ordinary paint binding");const auto &binding=found->second;
  // Authoring only: the authoritative live Main is checked before dereferencing
  // a retained Main/material pointer. Detached Open validation never calls here.
  Check::require(G_MAIN&&binding.owner_main==G_MAIN,"Material layering requires the live owner project");
  const auto &main=*G_MAIN;const Material *material=nullptr;for(const auto &candidate:main.materials)if(&candidate==binding.material){material=&candidate;break;}
  Check::require(material&&material->id.session_uid==set.material_session_uid,"Material layering material binding is no longer owned by the project");
  Check::require(!ID_IS_LINKED(&material->id)&&!ID_IS_OVERRIDE_LIBRARY(&material->id)&&BKE_id_is_editable(&main,&material->id)&&!(material->id.tag&(ID_TAG_NO_MAIN|ID_TAG_LOCALIZED|ID_TAG_COPIED_ON_EVAL|ID_TAG_MISSING)),"Material layering requires an editable local material");
  // Embedded tree ownership comes from the verified live Material, never from a
  // caller's descriptor or an unverified saved node-name lookup.
  Check::require(material->nodetree&&!material->adt&&!binding.shader_parameters.openpbr,"Material layering requires an ordinary Principled binding");
  const auto &tree=*material->nodetree;
  Check::require((tree.id.flag&ID_FLAG_EMBEDDED_DATA)&&BKE_id_owner_get(const_cast<ID *>(&tree.id),false)==&material->id&&tree.type==NTREE_SHADER&&!tree.adt,"Material layering requires the unanimated material-owned shader tree");
  // Initial Enable must not adopt or overwrite a pre-existing foreign builder
  // namespace. Allocated disabled legacy graphs keep their retained nodes.
  if(!set.material_layering.allocated())for(const auto &n:tree.nodes)
    Check::require(!STRPREFIX(n.name,"__BTS_Layer"),"Material layering has unowned reserved Layer nodes");
  if(set.material_layering.enabled&&std::any_of(set.material_layering.materials.begin(),set.material_layering.materials.end(),[](const auto &input){return std::any_of(input.textures.begin(),input.textures.end(),[](const auto &source){return source&&source->tiles;});})){
    // Capacity depends on the device, not a fabricated source tile limit.
    // Follow owned incoming edges from material outputs; disconnected images
    // cannot invent capacity rejection. Count retained Image nodes conservatively
    // plus distinct candidate image/sampler pairs; engine overhead is separate.
    std::set<const bNode *> reachable;std::vector<const bNode *> visit;
    for(const auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeOutputMaterial")&&(node.flag&NODE_DO_OUTPUT))visit.push_back(&node);
    if(visit.empty())for(const auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeOutputMaterial"))visit.push_back(&node);
    while(!visit.empty()){const auto *node=visit.back();visit.pop_back();if(!reachable.insert(node).second)continue;for(const auto &link:tree.links)if(link.tonode==node&&!(link.flag&NODE_LINK_MUTED))visit.push_back(link.fromnode);}
    std::set<std::string> samplers;
    for(const auto &node:tree.nodes)if(reachable.contains(&node)&&STREQ(node.idname,"ShaderNodeTexImage")&&node.id&&!STRPREFIX(node.name,"__BTS_LayerMaterial")){
      samplers.insert("retained:"+std::to_string(node.identifier));
      if(GS(node.id->name)==ID_IM&&reinterpret_cast<const Image *>(node.id)->source==IMA_SRC_TILED)samplers.insert("mapping:"+std::to_string(node.identifier));
    }
    for(const auto &input:set.material_layering.materials)for(const auto &source:input.textures)if(source){
      if(source->tiles)for(const auto &[tile,member]:*source->tiles)samplers.insert(gpu_texture_resource_key(*member)+":extend");
      else samplers.insert(gpu_texture_resource_key(*source)+":repeat");
    }
    const int capacity=GPU_max_textures();if(capacity<=0||samplers.size()>std::size_t(capacity))throw std::invalid_argument("Conservative reachable material sampler estimate "+std::to_string(samplers.size())+" exceeds GPU fragment capacity "+std::to_string(capacity)+"; engine sampler overhead is separate");
  }
  Check t{main,tree};
  Check::require(binding.resolution>0&&binding.height>0,"Material layering requires ready native paint targets");
  Check::require(!impl_->queued.contains(set.material_session_uid)&&!impl_->gpu_queued.contains(set.material_session_uid)&&!impl_->gpu_stack_queued.contains(set.material_session_uid)&&!(impl_->pending&&impl_->pending->result.uid==set.material_session_uid)&&!(impl_->job.valid()&&impl_->running_uid==set.material_session_uid),"Wait for the material preview before changing its layered shader");
  constexpr const char *textures[]{"__BTS_Pixels0","__BTS_Pixels1","__BTS_Pixels2","__BTS_Pixels3","__BTS_Pixels4"};
  constexpr const char *divides[]{"__BTS_Unpremultiply0","__BTS_Unpremultiply1","__BTS_Unpremultiply2","__BTS_Unpremultiply3","__BTS_Unpremultiply4"};
  constexpr const char *mixes[]{"__BTS_BaseMix0","__BTS_BaseMix1","__BTS_BaseMix2","__BTS_BaseMix3","__BTS_BaseMix4"};
  for(int c=0;c<5;++c){
    const auto &image=t.image(binding.images[c]);t.image_shape(image,set);
    for(int previous=0;previous<c;++previous)Check::require(binding.images[c]!=binding.images[previous],"Material layering native channel images are aliased");
    Check::require(!binding.preview_active[c],"Finish the native material preview before changing its layered shader");
    const auto &texture=t.node(binding.textures[c],"ShaderNodeTexImage",textures[c]),&divide=t.node(binding.unpremultiply[c],"ShaderNodeVectorMath",divides[c]),&mix=t.node(binding.base_mix[c],"ShaderNodeMixRGB",mixes[c]);
    t.texture(texture,image);Check::require(divide.custom1==NODE_VECTOR_MATH_DIVIDE,"Material layering native unpremultiply operation changed");t.mix(mix,MA_RAMP_BLEND);
    t.edge(texture,t.out(texture,"Color",SOCK_RGBA),divide,t.in(divide,"Vector",SOCK_VECTOR));t.edge(texture,t.out(texture,"Alpha",SOCK_FLOAT),divide,t.in(divide,"Vector_001",SOCK_VECTOR));
    t.edge(texture,t.out(texture,"Alpha",SOCK_FLOAT),mix,t.in(mix,"Fac",SOCK_FLOAT));t.edge(divide,t.out(divide,"Vector",SOCK_VECTOR),mix,t.in(mix,"Color2",SOCK_RGBA));t.color(t.in(mix,"Color1",SOCK_RGBA),encoded_material_base(set,c));
  }
  const auto &view=t.node(binding.view_switch,"ShaderNodeMixShader","__BTS_ViewSwitch"),&coverage=t.node(binding.coverage_mix,"ShaderNodeMixRGB","__BTS_InspectCoverage");
  const auto &emission=t.source(view,t.in(view,"Shader_001",SOCK_SHADER),"ShaderNodeEmission","__BTS_Inspection");
  t.edge(emission,t.out(emission,"Emission",SOCK_SHADER),view,t.in(view,"Shader_001",SOCK_SHADER));t.edge(coverage,t.out(coverage,"Color",SOCK_RGBA),emission,t.in(emission,"Color",SOCK_RGBA));t.scalar(t.in(emission,"Strength",SOCK_FLOAT),1);
  const auto &surface=t.source(view,t.in(view,"Shader",SOCK_SHADER),"ShaderNodeBsdfPrincipled");
  t.edge(surface,t.out(surface,"BSDF",SOCK_SHADER),view,t.in(view,"Shader",SOCK_SHADER));
  int surfaces=0;for(const auto &n:tree.nodes)surfaces+=STREQ(n.idname,"ShaderNodeBsdfPrincipled");Check::require(surfaces==1,"Material layering requires one retained Principled surface");
  const auto &output=t.node(native_material_output(*binding.material),"ShaderNodeOutputMaterial");const auto &endpoint=t.in(output,"Surface",SOCK_SHADER);
  const auto &outer=t.source(output,endpoint,"ShaderNodeMixShader");
  if(&outer==&view)t.edge(view,t.out(view,"Shader",SOCK_SHADER),output,endpoint);
  else{
    t.node(&outer,"ShaderNodeMixShader","__BTS_GeometrySwitch");t.edge(view,t.out(view,"Shader",SOCK_SHADER),outer,t.in(outer,"Shader",SOCK_SHADER));
    const auto &transparent=t.source(outer,t.in(outer,"Shader_001",SOCK_SHADER),"ShaderNodeBsdfTransparent","__BTS_GeometryTransparent");t.edge(transparent,t.out(transparent,"BSDF",SOCK_SHADER),outer,t.in(outer,"Shader_001",SOCK_SHADER));
    const auto &enabled=t.source(outer,t.in(outer,"Fac",SOCK_FLOAT),"ShaderNodeMath","__BTS_GeometryEnabled");Check::require(enabled.custom1==NODE_MATH_MULTIPLY,"Material layering visibility operation changed");t.edge(enabled,t.out(enabled,"Value",SOCK_FLOAT),outer,t.in(outer,"Fac",SOCK_FLOAT));t.edge(outer,t.out(outer,"Shader",SOCK_SHADER),output,endpoint);
  }
  const auto &normal=t.named("__BTS_PaintNormal","ShaderNodeNormalMap"),&height=t.named("__BTS_PaintHeight","ShaderNodeBump");
  Check::require(normal.storage,"Material layering native tangent-normal settings are missing");const auto &normal_settings=*static_cast<const NodeShaderNormalMap *>(normal.storage);
  Check::require(normal_settings.space==SHD_SPACE_TANGENT&&normal_settings.convention==SHD_NORMAL_MAP_CONVENTION_OPENGL&&normal_settings.base==SHD_NORMAL_MAP_BASE_DISPLACED&&!normal_settings.uv_map[0]&&normal.custom1==(set.tangent_per_fragment?1:0),"Material layering native tangent-normal settings changed");
  t.scalar(t.in(normal,"Strength",SOCK_FLOAT),binding.shader_parameters.normal_intensity);t.edge(*binding.base_mix[3],t.out(*binding.base_mix[3],"Color",SOCK_RGBA),normal,t.in(normal,"Color",SOCK_RGBA));
  Check::require(height.custom1==0&&height.custom2==0,"Material layering native Bump operation changed");t.scalar(t.in(height,"Strength",SOCK_FLOAT),0);t.scalar(t.in(height,"Distance",SOCK_FLOAT),.001f);t.scalar(t.in(height,"Filter Width",SOCK_FLOAT),.1f);
  t.edge(normal,t.out(normal,"Normal",SOCK_VECTOR),height,t.in(height,"Normal",SOCK_VECTOR));t.edge(*binding.base_mix[4],t.out(*binding.base_mix[4],"Color",SOCK_RGBA),height,t.in(height,"Height",SOCK_FLOAT));
  const bNode *ordinary_color=binding.base_mix[0];
  if(binding.ao_image){const auto &image=t.image(binding.ao_image);t.image_shape(image,set);const auto &texture=t.named("__BTS_AOPixels","ShaderNodeTexImage"),&mix=t.named("__BTS_AOMaterial","ShaderNodeMixRGB");t.texture(texture,image);t.mix(mix,MA_RAMP_MULT);t.scalar(t.in(mix,"Fac",SOCK_FLOAT),binding.shader_parameters.ao_intensity);t.edge(*binding.base_mix[0],t.out(*binding.base_mix[0],"Color",SOCK_RGBA),mix,t.in(mix,"Color1",SOCK_RGBA));t.edge(texture,t.out(texture,"Color",SOCK_RGBA),mix,t.in(mix,"Color2",SOCK_RGBA));ordinary_color=&mix;}
  if(!binding.ao_image)for(const auto &n:tree.nodes)Check::require(!STREQ(n.name,"__BTS_AOMaterial"),"Material layering has an unowned AO restoration node");
  if(binding.material_layering.enabled){
    // The old active program may contain retained legacy textures. Only its
    // typed endpoints are relevant to safe Disable; uniform eligibility is a
    // separate caller gate. All ordinary restore edges above remain verified.
    const auto &color=t.named("__BTS_LayeredColor","ShaderNodeMixRGB"),&metal=t.named("__BTS_LayeredChannel1","ShaderNodeMixRGB"),&rough=t.named("__BTS_LayeredChannel2","ShaderNodeMixRGB"),&bump=t.named("__BTS_LayeredBump","ShaderNodeBump");t.mix(color,MA_RAMP_MULT);t.mix(metal,MA_RAMP_BLEND);t.mix(rough,MA_RAMP_BLEND);
    t.edge(color,t.out(color,"Color",SOCK_RGBA),surface,t.in(surface,"Base Color",SOCK_RGBA));t.edge(metal,t.out(metal,"Color",SOCK_RGBA),surface,t.in(surface,"Metallic",SOCK_FLOAT));t.edge(rough,t.out(rough,"Color",SOCK_RGBA),surface,t.in(surface,"Roughness",SOCK_FLOAT));t.edge(bump,t.out(bump,"Normal",SOCK_VECTOR),surface,t.in(surface,"Normal",SOCK_VECTOR));
  }else{
    t.edge(*ordinary_color,t.out(*ordinary_color,"Color",SOCK_RGBA),surface,t.in(surface,"Base Color",SOCK_RGBA));t.edge(*binding.base_mix[1],t.out(*binding.base_mix[1],"Color",SOCK_RGBA),surface,t.in(surface,"Metallic",SOCK_FLOAT));t.edge(*binding.base_mix[2],t.out(*binding.base_mix[2],"Color",SOCK_RGBA),surface,t.in(surface,"Roughness",SOCK_FLOAT));if(binding.world_normal_images[0])validate_native_world_normal_shader(main,tree,set,binding.world_normal_images,height,surface);else t.edge(height,t.out(height,"Normal",SOCK_VECTOR),surface,t.in(surface,"Normal",SOCK_VECTOR));
  }
}
void MaterialInspection::validate_native_mapping_edit(const TextureSet &before,const TextureSet &after,NativeMappingEditOrigin origin)const{impl_->gpu_preview.validate_native_mapping_instances(after,origin==NativeMappingEditOrigin::HistoryRestore?nullptr:&before);}
bool MaterialInspection::has_native_material(const std::string &resource)const{return impl_->gpu_preview.has_native_material(resource);}
bool MaterialInspection::native_material_uses_object(const std::string &resource)const{return impl_->gpu_preview.native_material_uses_object(resource);}
bool MaterialInspection::native_material_uses_ao(const std::string &resource)const{return impl_->gpu_preview.native_material_uses_ao(resource);}
NativeMaterialDependencies MaterialInspection::native_requirements(const std::string &resource,const NativeMaterialDescription &instance)const{return impl_->gpu_preview.native_requirements(resource,instance);}
void MaterialInspection::prepare_native_height(const std::string &resource,NativeMaterialHeightResource value){impl_->gpu_preview.prepare_native_height(resource,std::move(value));}
void MaterialInspection::stage_native_height(NativeMaterialHeightGpu &work,NativeMaterialHeightResource resource){impl_->gpu_preview.stage_native_height(work,std::move(resource));}
bool MaterialInspection::step_native_height(NativeMaterialHeightGpu &work){return impl_->gpu_preview.step_native_height(work);}
void MaterialInspection::install_native_height(NativeMaterialHeightGpu &work){impl_->gpu_preview.install_native_height(work);}
bool MaterialInspection::set_native_height_policy(TextureSet &set,const QUuid &id,NativeHeightPolicy policy)const{return impl_->gpu_preview.set_native_height_policy(set,id,policy);}
void MaterialInspection::probe_native_height(const std::string &key,const NativeMaterialDescription &instance,NativeMaterialGpuContext context){impl_->gpu_preview.probe_native_height(key,instance,std::move(context));}
void MaterialInspection::step_native_preparations(){impl_->gpu_preview.step_native_preparations();}
void MaterialInspection::refresh_native_view(){
 auto &s=*impl_;const auto &library=s.gpu_preview.native_world_sources();
 for(auto &[uid,binding]:s.bindings){
  const bool source_pending=binding.world_queued_generation!=s.generation[uid]||binding.world_registry_generation!=library.source_generation();
  if(binding.gpu_stack_set){const auto channels=s.gpu_preview.native_view_channels(*binding.gpu_stack_set);if(std::any_of(channels.begin(),channels.end(),[](bool value){return value;}))queue_layer_edit(*binding.gpu_stack_set,channels,false);}
  if(!binding.mask_snapshot)continue;const auto &set=*binding.mask_snapshot;const auto &layers=evaluation_layers(set);
  const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.fill.native_material&&library.contains(layer.fill.material_resource)&&library.world_normal_perturbed(layer.fill.material_resource)&&library.world_normal_requires_view(layer.fill.material_resource);});
  if(found==layers.end())continue;
  if(binding.world_context.view.notify(true,[&]{return s.gpu_preview.native_world_view(set,*found,set.tiles.empty()?1001:*set.tiles.begin());}))binding.world_view_dirty=true;
  // Compute-view queues may advance the ordinary generation. Their camera-only
  // change must not turn this world output update into owner/mesh observation.
  if(!source_pending){binding.world_queued_generation=s.generation[uid];binding.world_registry_generation=library.source_generation();}
 }
}
NativeMaterialPreparationStatus MaterialInspection::native_preparation_status(const std::string &resource)const{return impl_->gpu_preview.native_preparation_status(resource);}
void MaterialInspection::install_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph> graph){impl_->gpu_preview.install_native_material(std::move(resource),std::move(content),std::move(graph));}
void MaterialInspection::set_native_fragment_geometry_provider(NativeFragmentGeometryProvider provider){impl_->world_geometry_provider=std::move(provider);}
void MaterialInspection::set_native_fragment_object_provider(NativeFragmentObjectProvider provider){impl_->world_object_provider=std::move(provider);}
void MaterialInspection::refresh_native_world_objects(bool owner_revision,bool geometry_revision){
 if(!owner_revision&&!geometry_revision)return;
 if(!GPU_context_active_get())throw std::logic_error("Native world object refresh requires the GPU owner");
 auto &s=*impl_;const auto &library=s.gpu_preview.native_world_sources();
 for(auto &[uid,binding]:s.bindings)if(binding.mask_snapshot){
  bool used=false;for(const auto &layer:evaluation_layers(*binding.mask_snapshot))if(layer.fill.native_material&&library.contains(layer.fill.material_resource)&&library.world_normal_perturbed(layer.fill.material_resource)){used=true;break;}
  if(!used)continue;
  // Geometry Position/Normal and tangent maps also consume owner matrices.
  // Observing the full small table can conservatively invalidate unused color.
  const bool changed=binding.world_object_context.notify(owner_revision,s.world_object_provider,uid,geometry_revision);
  if(changed){s.invalidate_world_geometry(binding);binding.world_objects_dirty=true;}
 }
}
void MaterialInspection::set_native_context_provider(std::function<void(const TextureSet &,const TextureLayer &,int,const NativeMaterialDependencies &,NativeMaterialGpuContext &)> provider){impl_->gpu_preview.set_native_context_provider(std::move(provider));}
MaterialInspection::MaterialInspection() : impl_(std::make_unique<Impl>()) {}
MaterialInspection::~MaterialInspection(){reset();}
void MaterialInspection::initialize_gpu_programs(){performance::Scope timing("gpu_stack_startup_programs");impl_->gpu_preview.begin_frame();}
void MaterialInspection::poll_gpu_programs(){
  impl_->gpu_preview.poll_programs();
  if(auto error=impl_->gpu_preview.take_program_error();!error.empty()){
    pending_composite_error=std::move(error);
    std::fprintf(stderr,"BTS_GPU_STACK_PROGRAM_FAILED %s\n",pending_composite_error.c_str());
  }
}
bool MaterialInspection::gpu_programs_pending()const{return impl_->gpu_preview.programs_pending();}
void MaterialInspection::reset(bool terminal) {if(terminal)NativeWorldNormalGeometryJob::shutdown();impl_->cycles.reset();impl_->cycles_entry_waiting=false;impl_->cycles_entry_requested=false;impl_->cycles_preparation_failed=false;impl_->cycles_preparation_error.clear();impl_->texture_preparation_owner={};impl_->preparing_textures.clear();impl_->texture_upload_waiting=false;impl_->texture_upload_error.clear();bool projection_jobs=false,margin_jobs=false;for(auto &[uid,binding]:impl_->bindings)if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;for(auto &[uid,binding]:impl_->bindings)if(binding.projection_job){binding.projection_job->cancelled=true;projection_jobs=true;}if(projection_jobs)ProjectionGeometryJob::pool().waitForDone();for(auto &[uid,binding]:impl_->bindings)if(binding.margin_job){binding.margin_job->cancelled=true;margin_jobs=true;}if(margin_jobs)MarginGeometryJob::pool().waitForDone();impl_->gpu_mips_pending=false;impl_->histogram_job.reset();impl_->histogram_failure.reset();impl_->material_sample_job.reset();impl_->material_sample_failure.reset();impl_->thumbnail_job.reset();impl_->thumbnail_failure.reset();impl_->layer_capture_job.reset();impl_->layer_capture_failure.reset();impl_->layering_sources.reset();impl_->layering_sources_pending=false;for(auto &[uid,binding]:impl_->bindings){binding.world_ao_lease.reset();binding.world_ao={};}impl_->gpu_preview.reset(terminal);impl_->gpu_stack_queued.clear();impl_->gpu_queued.clear();impl_->pending.reset();if(impl_->cancel)impl_->cancel->store(true);if(impl_->job.valid()){try{impl_->job.get();}catch(...){}}impl_->queued.clear();impl_->running.reset();impl_->cancel.reset();impl_->generation.clear();impl_->bindings.clear();}
bool MaterialInspection::geometry_visibility_enabled(std::uint32_t uid) const{const auto found=impl_->bindings.find(uid);return found!=impl_->bindings.end()&&found->second.geometry_visibility_on;}
// UV helper nodes borrow image IDs without incrementing their users. The host calls
// collection only after releasing helper/engine caches and refreshing the source depsgraph.
void MaterialInspection::collect_retired_visibility_images(Main &main){for(auto it=impl_->retired_visibility_images.begin();it!=impl_->retired_visibility_images.end();){if((*it)->id.us==0){BKE_id_free(&main,*it);it=impl_->retired_visibility_images.erase(it);}else ++it;}}
std::size_t MaterialInspection::geometry_visibility_image_count() const{std::size_t count=impl_->retired_visibility_images.size();for(const auto &[uid,binding]:impl_->bindings){const auto *texture=bke::node_find_node_by_name(*binding.material->nodetree,"__BTS_GeometryVisibility");count+=texture&&texture->id;}return count;}
bool MaterialInspection::set_geometry_visibility(Main &main,const TextureSet *set,bool preserve_others){
  bool changed=false;
  // One selected-layer presentation target at a time. Other Texture Sets remain visible.
  for(auto &[uid,binding]:impl_->bindings)if(!preserve_others&&binding.geometry_visibility_on&&(!set||uid!=set->material_session_uid)){
    if(binding.residency_hidden&&binding.mask_snapshot){auto proxy=*binding.mask_snapshot;proxy.residency_view=false;proxy.layers={TextureLayer{}};proxy.instance_layers.reset();proxy.selected_layer=proxy.layers.front().id;changed|=set_geometry_visibility(main,&proxy,true);continue;}
    auto &tree=*binding.material->nodetree;auto *enabled=bke::node_find_node_by_name(tree,"__BTS_GeometryEnabled");if(enabled)scalar(tree,input(*enabled,1),0);binding.geometry_visibility_on=false;BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);changed=true;
  }
  if(!set)return changed;const auto &layer=TextureDocument::layer(const_cast<TextureSet &>(*set),set->selected_layer);
  if(!impl_->bindings.contains(set->material_session_uid))impl_->bindings.emplace(set->material_session_uid,bind(main,set->material_session_uid));
  auto &binding=impl_->bindings.at(set->material_session_uid);auto &tree=*binding.material->nodetree;
  std::string key=QJsonDocument(geometry_mask_json(layer.geometry)).toJson(QJsonDocument::Compact).toStdString();for(int tile:set->tiles)key+=std::to_string(tile)+":"+std::to_string(set->width(tile))+":"+std::to_string(set->rows(tile))+";";
  key+=set->loaded?"loaded":"unloaded";for(int tile:set->unloaded_tiles)key+="off:"+std::to_string(tile)+";";
  if(binding.geometry_visibility_on&&binding.geometry_visibility_key==key)return changed;
  auto &texture=node(tree,"ShaderNodeTexImage","__BTS_GeometryVisibility");auto *storage=static_cast<NodeTexImage *>(texture.storage);storage->interpolation=SHD_INTERP_CLOSEST;
  auto &enabled=node(tree,"ShaderNodeMath","__BTS_GeometryEnabled");enabled.custom1=NODE_MATH_MULTIPLY;
  auto &transparent=node(tree,"ShaderNodeBsdfTransparent","__BTS_GeometryTransparent");auto &mix=node(tree,"ShaderNodeMixShader","__BTS_GeometrySwitch");
  if(!(input(mix,1).flag&SOCK_IS_LINKED)){ // Establish the known presentation wrapper once.
    auto *surface=native_material_output(*binding.material);if(!surface)throw std::runtime_error("Geometry visibility material output missing");auto &destination=input(*surface,0);bke::node_remove_socket_links(tree,destination);
    bke::node_add_link(tree,*binding.view_switch,output(*binding.view_switch,"Shader"),mix,input(mix,1));bke::node_add_link(tree,transparent,output(transparent,"BSDF"),mix,input(mix,2));bke::node_add_link(tree,mix,output(mix,"Shader"),*surface,destination);
    bke::node_add_link(tree,texture,output(texture,"Color"),enabled,input(enabled,0));bke::node_add_link(tree,enabled,output(enabled,"Value"),mix,input(mix,0));
  }
  if(binding.geometry_visibility_key!=key){
    const bool tiled=set->tiles!=std::set<int>{1001};const bool mesh=layer.geometry.type==0&&!layer.geometry.meshes.empty();
    const auto width=[&](int tile){return mesh?set->width(tile):2;};const auto height=[&](int tile){return mesh?set->rows(tile):2;};const float zero[4]={0,0,0,1};
    Image *image=nullptr;if(texture.id){if(GS(texture.id->name)!=ID_IM)throw std::runtime_error("Geometry visibility image type changed");image=reinterpret_cast<Image *>(texture.id);bool same=(image->source==IMA_SRC_TILED)==tiled;std::size_t count=0;for(const ImageTile &tile:image->tiles){++count;if(!set->tiles.contains(tile.tile_number))same=false;}if(count!=set->tiles.size())same=false;for(int tile:set->tiles)if(same){ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);same=buffer&&buffer->x==width(tile)&&buffer->y==height(tile)&&buffer->float_data();if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);}if(!same)image=nullptr;}
    if(!image){image=native_generated_image(&main,width(1001),height(1001),"BTS Geometry Visibility",32,1,IMA_GENTYPE_BLANK,zero,false,true,tiled);if(!image)throw std::runtime_error("Cannot allocate geometry visibility image");image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;
      if(tiled){for(int number:set->tiles)if(number!=1001){auto *tile=BKE_image_add_tile(image,number,nullptr);if(!tile)throw std::runtime_error("Cannot create geometry visibility tile");tile->gen_x=width(number);tile->gen_y=height(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;std::copy_n(zero,4,tile->gen_color);if(!native_fill_tile(image,tile))throw std::runtime_error("Cannot fill geometry visibility tile");}if(!set->tiles.contains(1001))BKE_image_remove_tile(image,BKE_image_get_tile(image,1001));}
      // Transfer the generated image's initial user to this node.
      auto *old=texture.id;texture.id=&image->id;if(old){id_us_min(old);impl_->retired_visibility_images.push_back(reinterpret_cast<Image *>(old));}
    }
    for(int tile:set->tiles){ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);if(!buffer||!buffer->float_data()){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Geometry visibility buffer unavailable");}auto *data=buffer->float_data_for_write();for(int y=0;y<height(tile);++y)for(int x=0;x<width(tile);++x){const float excluded=!set->tile_loaded(tile)||!TextureDocument::geometry_includes(*set,layer,x,y,tile);const auto offset=(std::size_t(y)*width(tile)+x)*4;data[offset]=data[offset+1]=data[offset+2]=excluded;data[offset+3]=1;}BKE_image_mark_dirty(image,buffer);BKE_image_update_gputexture_delayed(image,BKE_image_get_tile(image,tile),buffer,0,0,width(tile),height(tile));BKE_image_release_ibuf(image,buffer,nullptr);}
    DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SHADING);binding.geometry_visibility_key=key;
  }
  scalar(tree,input(enabled,1),1);binding.geometry_visibility_on=true;BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);return true;
}
void MaterialInspection::evict_unloaded_resources(const TextureDocument &document){impl_->cycles.reset();impl_->gpu_preview.evict_residency(document.texture_sets());}
void MaterialInspection::change_residency(Main &main,const TextureSet &source){
 const auto set=resident_texture_view(source);
 impl_->cycles.reset();
 update_pixels(main,set,set.texture_resolution,{},true,true);
 auto &binding=impl_->bindings.at(set.material_session_uid);
 binding.residency_hidden=!source.fully_loaded();
 binding.world_set.reset();binding.world_geometry.reset();binding.world_ao_lease.reset();binding.world_ao={};
 binding.projection_cache.clear();binding.ownership_cache.clear();
 impl_->histogram_job.reset();impl_->thumbnail_job.reset();impl_->layer_capture_job.reset();impl_->material_sample_job.reset();
 std::vector<TextureSet> resident;for(const auto &[uid,other]:impl_->bindings)if(other.mask_snapshot)resident.push_back(*other.mask_snapshot);
 impl_->gpu_preview.evict_residency(resident);
 if(!queue_layer_edit(set,complete_composite_channels,true))throw std::runtime_error("Cannot restore resident texture targets");
 auto visibility=source;visibility.layers={TextureLayer{}};visibility.instance_layers.reset();visibility.selected_layer=visibility.layers.front().id;
 set_geometry_visibility(main,&visibility,true);
}
void MaterialInspection::begin_project_source_capture(){impl_->gpu_preview.begin_frame();}
bool MaterialInspection::capture_project_source(const std::string &key,const PaintPixels &pixels,int channel,int tile,int x,int y,int width,int height,GpuProjectReadback &readback){
 const auto state=impl_->gpu_preview.capture_author_chunk(key,pixels,channel,tile,x,y,width,height,readback);
 if(state==GpuPreviewUpdate::Unsupported)throw std::runtime_error("Cannot snapshot GPU-authored source pixels; CPU fallback disabled");
 return state==GpuPreviewUpdate::Updated;
}
void MaterialInspection::capture_project_images(GpuProjectReadback &readback)
{
 for(auto &[uid,binding]:impl_->bindings)for(int channel=0;channel<17;++channel){
  if(channel==6||channel==15)continue; // Regenerable inspection-only caches, never stock shading.
  auto *image=composite_image(binding,channel);if(!image)continue;
  const auto overrides=BKE_image_gpu_override_texture(image);auto *texture=overrides.texture?*overrides.texture:nullptr;
  if(!texture)continue; // Unbound inspection caches are not presentation data.
  for(ImageTile &tile:image->tiles){if(tile.gen_x==1&&tile.gen_y==1)continue;ImageGpuTileRegion region;
   if(!BKE_image_gpu_override_tile_region(image,tile.tile_number,region))throw std::runtime_error("Cannot snapshot GPU project tile region");
   readback.submit_native(image->id.session_uid,tile.tile_number,texture,region.x,region.y,region.layer,region.width,region.height);
  }
 }
 for(auto &[uid,binding]:impl_->bindings)for(auto *image:binding.world_normal_images)if(image){const auto overrides=BKE_image_gpu_override_texture(image);auto *texture=overrides.texture?*overrides.texture:nullptr;if(!texture)continue;for(ImageTile &tile:image->tiles){if(tile.gen_x==1&&tile.gen_y==1)continue;ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(image,tile.tile_number,region))throw std::runtime_error("Cannot snapshot private world Normal tile");readback.submit_native(image->id.session_uid,tile.tile_number,texture,region.x,region.y,region.layer,region.width,region.height);}}
}
std::vector<MaterialParameterMaskCapture> MaterialInspection::capture_material_layer_masks(const TextureSet &set,GpuProjectReadback &readback){
 require_material_parameter_main(set);validate_material_layering(set);
 if(!set.material_layering.allocated())throw std::invalid_argument("Apply four-material mode before exporting its three opacity maps");
 auto &binding=impl_->bindings.at(set.material_session_uid);require_material_parameter_surface(*binding.owner_main,*binding.material);
 if(binding.material_layering.mask_slots!=set.material_layering.mask_slots||binding.material_layering.mask_roots!=set.material_layering.mask_roots)throw std::invalid_argument("Opacity ownership is still being published");
 struct Target{Image *image;gpu::Texture *texture;ImageGpuTileRegion region;int stack,tile;};std::vector<Target> targets;std::uint64_t bytes=0;
 auto &tree=*binding.material->nodetree;
 // Authoring admission checks the retained Surface endpoints; export additionally
 // proves the exact three mask factors feed the six serialized material roles.
 LayeringTopology topology{*binding.owner_main,tree};
 for(int channel=0;channel<6;++channel){const bNode *previous=nullptr;
  for(int stack=0;stack<3;++stack){const auto &factor=topology.named(("__BTS_LayerMask"+std::to_string(stack)).c_str(),"ShaderNodeClamp");const auto &mix=topology.named(("__BTS_LayerMix"+std::to_string(channel)+"_"+std::to_string(stack)).c_str(),"ShaderNodeMixRGB");topology.mix(mix,MA_RAMP_BLEND);topology.edge(factor,topology.out(factor,"Result",SOCK_FLOAT),mix,topology.in(mix,"Fac",SOCK_FLOAT));
   if(previous)topology.edge(*previous,topology.out(*previous,"Color",SOCK_RGBA),mix,topology.in(mix,"Color1",SOCK_RGBA));previous=&mix;
  }
  const auto &final=topology.named(("__BTS_LayeredChannel"+std::to_string(channel)).c_str(),"ShaderNodeMixRGB");topology.mix(final,MA_RAMP_BLEND);topology.scalar(topology.in(final,"Fac",SOCK_FLOAT),0);topology.edge(*previous,topology.out(*previous,"Color",SOCK_RGBA),final,topology.in(final,"Color1",SOCK_RGBA));
 }
 const auto edge=[&](const char *from,const char *from_type,const char *output,int output_type,const char *to,const char *to_type,const char *input,int input_type){const auto &a=topology.named(from,from_type),&b=topology.named(to,to_type);topology.edge(a,topology.out(a,output,output_type),b,topology.in(b,input,input_type));};
 edge("__BTS_LayeredChannel0","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayeredColor","ShaderNodeMixRGB","Color1",SOCK_RGBA);
 edge("__BTS_LayeredChannel5","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayeredCompleteAO","ShaderNodeMixRGB","Color1",SOCK_RGBA);
 edge("__BTS_LayeredCompleteAO","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayeredAOIntensity","ShaderNodeMixRGB","Color2",SOCK_RGBA);
 edge("__BTS_LayeredAOIntensity","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayeredColor","ShaderNodeMixRGB","Color2",SOCK_RGBA);
 edge("__BTS_LayeredChannel3","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayerDetailScale","ShaderNodeVectorMath","Vector",SOCK_VECTOR);
 edge("__BTS_LayeredChannel4","ShaderNodeMixRGB","Color",SOCK_RGBA,"__BTS_LayerSharedHeight","ShaderNodeMath","Value",SOCK_FLOAT);
 edge("__BTS_LayerSharedHeight","ShaderNodeMath","Value",SOCK_FLOAT,"__BTS_LayeredBump","ShaderNodeBump","Height",SOCK_FLOAT);
 for(int stack=0;stack<3;++stack){const int slot=set.material_layering.mask_slots[stack];auto *image=binding.user_images.at(slot);auto *sample=binding.user_textures.at(slot);auto *clamp=bke::node_find_node_by_name(tree,("__BTS_LayerMask"+std::to_string(stack)).c_str());
  bool owned=false;for(const Image &item:binding.owner_main->images)owned|=&item==image;
  if(!owned||!sample||bke::node_find_node_by_name(tree,("__BTS_UserPixels"+std::to_string(slot)).c_str())!=sample||sample->id!=&image->id||!STREQ(sample->idname,"ShaderNodeTexImage")||(sample->flag&NODE_MUTED)||!clamp||!STREQ(clamp->idname,"ShaderNodeClamp")||clamp->custom1!=NODE_CLAMP_MINMAX||(clamp->flag&NODE_MUTED)||!sample->storage)throw std::invalid_argument("Opacity stack "+std::to_string(stack+1)+" has no exact owned shader image");
  require_material_parameter_mask_sampling(*sample);
  for(const auto &link:tree.links)if(link.tonode==sample||(link.tonode==clamp&&link.tosock!=&input(*clamp,0)))throw std::invalid_argument("Opacity sampling/clamp has an unsupported input link");
  if(image->alpha_mode!=IMA_ALPHA_CHANNEL_PACKED||!STREQ(image->colorspace_settings.name,IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA)))throw std::invalid_argument("Opacity export requires raw packed-alpha image interpretation");
  auto &value=input(*clamp,0);int links=0;for(const auto &link:tree.links)if(link.tosock==&value){++links;if(link.fromnode!=sample||link.fromsock!=&output(*sample,"Color")||!(link.flag&NODE_LINK_VALID)||(link.flag&NODE_LINK_MUTED))throw std::invalid_argument("Opacity shader source link changed");}
  if(links!=1||static_cast<const bNodeSocketValueFloat *>(input(*clamp,1).default_value)->value!=0||static_cast<const bNodeSocketValueFloat *>(input(*clamp,2).default_value)->value!=1)throw std::invalid_argument("Opacity shader clamp contract changed");
  int consumers=0;for(const auto &link:tree.links)if(link.fromnode==clamp){bool known=false;for(int channel=0;channel<6;++channel){const auto &mix=topology.named(("__BTS_LayerMix"+std::to_string(channel)+"_"+std::to_string(stack)).c_str(),"ShaderNodeMixRGB");known|=link.tonode==&mix&&link.tosock==&topology.in(mix,"Fac",SOCK_FLOAT)&&link.fromsock==&topology.out(*clamp,"Result",SOCK_FLOAT);}if(!known||!(link.flag&NODE_LINK_VALID)||(link.flag&NODE_LINK_MUTED))throw std::invalid_argument("Opacity factor has an unrepresented shader consumer");++consumers;}if(consumers!=6)throw std::invalid_argument("Opacity factor shader-role fanout changed");
  const auto override=BKE_image_gpu_override_texture(image);auto *texture=override.texture?*override.texture:nullptr;std::set<int> tiles;for(const ImageTile &tile:image->tiles)tiles.insert(tile.tile_number);
  if(!texture||tiles!=set.tiles)throw std::invalid_argument("Opacity images are not fully published; enable/apply the mode and wait before export");
  for(int tile:set.tiles){ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(image,tile,region)||region.width!=set.width(tile)||region.height!=set.rows(tile))throw std::invalid_argument("Opacity tile dimensions/publication differ from the selected set");bytes=resource_sum(bytes,resource_product(resource_product(region.width,region.height),16));targets.push_back({image,texture,region,stack,tile});}
 }
 // All roles/tiles are admitted before retaining the first native allocation.
 std::vector<MaterialParameterMaskCapture> result;
 for(const auto &target:targets){const auto &r=target.region;result.push_back({target.stack,target.tile,r.width,r.height,readback.images().size()});readback.submit_native(target.image->id.session_uid,target.tile,target.texture,r.x,r.y,r.layer,r.width,r.height);}
 return result;
}
void MaterialInspection::pack_images()
{
  for(auto &[uid,binding]:impl_->bindings) {
    for(Image *image:binding.images)if(image&&!BKE_image_memorypack(image))throw std::runtime_error("Cannot pack derived composite image");
    for(auto *image:binding.world_normal_images)if(image&&BKE_image_gpu_override_texture(image).texture&&*BKE_image_gpu_override_texture(image).texture&&!BKE_image_memorypack(image))throw std::runtime_error("Cannot pack private world Normal image");
    if(binding.authored_normal_image&&!BKE_image_memorypack(binding.authored_normal_image))throw std::runtime_error("Cannot pack authored normal inspection");
    if(binding.ao_image&&!BKE_image_memorypack(binding.ao_image))throw std::runtime_error("Cannot pack AO image");
    if(binding.displacement_image&&!BKE_image_memorypack(binding.displacement_image))throw std::runtime_error("Cannot pack displacement image");
    for(auto *image:binding.user_images)if(image&&!BKE_image_memorypack(image))throw std::runtime_error("Cannot pack custom channel image");
    if(binding.mask_image&&!BKE_image_memorypack(binding.mask_image))throw std::runtime_error("Cannot pack mask inspection image");

  }
  if(!impl_->bindings.empty())MaterialLayerShader::pack_images(*impl_->bindings.begin()->second.owner_main);
}
void MaterialInspection::restore(Main &main,const TextureDocument &document)
{
  auto pending=std::make_unique<Impl>();pending->subsurface_enabled=impl_->subsurface_enabled;pending->world_geometry_provider=impl_->world_geometry_provider;pending->world_object_provider=impl_->world_object_provider;
  for(const auto &stored:document.texture_sets()) {const auto set=resident_texture_view(stored);
    // A read-only imported material retains its native graph, not BTS targets.
    if(!set.uniform_editable)continue;
    Material *material=nullptr;
    for(Material &candidate:main.materials)if(candidate.id.session_uid==set.material_session_uid)material=&candidate;
    if(!material||!material->nodetree)throw std::runtime_error("Saved Texture Set material is missing");
    auto &tree=*material->nodetree;BKE_ntree_update_after_single_tree_change(main,tree);
    const auto required=[&](const std::string &name,const char *type) {
      auto *n=bke::node_find_node_by_name(tree,name);
      if(!n||!STREQ(n->idname,type))throw std::runtime_error("Saved material node missing or changed: "+name);
      return n;
    };
    if(!native_principled_surface(*material))throw std::runtime_error("Saved material surface was replaced");
    if(auto *enabled=bke::node_find_node_by_name(tree,"__BTS_GeometryEnabled"))scalar(tree,input(*enabled,1),0);
    Binding binding{material,required("__BTS_ViewSwitch","ShaderNodeMixShader"),required("__BTS_InspectCoverage","ShaderNodeMixRGB")};binding.owner_main=&main;
    required("__BTS_Inspection","ShaderNodeEmission");required("__BTS_Checker","ShaderNodeTexChecker");required("__BTS_UvCoordinates","ShaderNodeTexCoord");
    if(bke::node_find_node_by_name(tree,"__BTS_Pixels0")) {
      binding.resolution=set.texture_resolution;binding.height=set.rows();for(int number:set.tiles)binding.tile_sizes[number]={set.width(number),set.rows(number)};
      for(int c=0;c<5;++c) {
        binding.textures[c]=required("__BTS_Pixels"+std::to_string(c),"ShaderNodeTexImage");
        binding.unpremultiply[c]=required("__BTS_Unpremultiply"+std::to_string(c),"ShaderNodeVectorMath");
        binding.base_mix[c]=required("__BTS_BaseMix"+std::to_string(c),"ShaderNodeMixRGB");
        auto *id=binding.textures[c]->id;
        if(!id||GS(id->name)!=ID_IM)throw std::runtime_error("Saved composite image is missing");
        binding.images[c]=reinterpret_cast<Image *>(id);
        auto *image=binding.images[c];
        if(set.tiles!=std::set<int>{1001}&&image->source!=IMA_SRC_TILED)throw std::runtime_error("Saved UDIM image lost its tiles");
        for(int number:set.tiles) {
          if(!BKE_image_get_tile(image,number))throw std::runtime_error("Saved composite tile missing");
          ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);
          const bool valid=buffer&&buffer->x==set.width(number)&&buffer->y==set.rows(number)&&buffer->float_data();
          if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);
          if(!valid)throw std::runtime_error("Saved composite image dimensions or format changed");
        }
      }
      required("__BTS_PaintNormal","ShaderNodeNormalMap");required("__BTS_PaintHeight","ShaderNodeBump");
    }
    if(auto *ao=bke::node_find_node_by_name(tree,"__BTS_AOPixels")){if(!STREQ(ao->idname,"ShaderNodeTexImage")||!ao->id||GS(ao->id->name)!=ID_IM)throw std::runtime_error("Saved AO image is invalid");binding.ao_image=reinterpret_cast<Image *>(ao->id);required("__BTS_AOMaterial","ShaderNodeMixRGB");for(int number:set.tiles){ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(binding.ao_image,&user,nullptr);const bool valid=buffer&&buffer->x==set.width(number)&&buffer->y==set.rows(number)&&buffer->float_data();if(buffer)BKE_image_release_ibuf(binding.ao_image,buffer,nullptr);if(!valid)throw std::runtime_error("Saved AO buffer is invalid");}}
    for(int side=0;side<2;++side){const auto name="__BTS_NativeWorldPixels"+std::to_string(side);if(auto *texture=bke::node_find_node_by_name(tree,name)){if(!STREQ(texture->idname,"ShaderNodeTexImage")||!texture->id||GS(texture->id->name)!=ID_IM)throw std::runtime_error("Saved world Normal image is invalid");binding.world_normal_images[side]=reinterpret_cast<Image *>(texture->id);}}binding.world_normal_enabled=bke::node_find_node_by_name(tree,"__BTS_NativeWorldEnabled");if(binding.resolution&&!set.material_layering.enabled){auto *height=required("__BTS_PaintHeight","ShaderNodeBump");auto *surface=native_principled_surface(*material);install_native_world_normal_shader(main,tree,set,binding.world_normal_images,binding.world_normal_enabled,*height,*surface);}
    if(auto *texture=bke::node_find_node_by_name(tree,"__BTS_AuthoredNormalPixels")){if(!STREQ(texture->idname,"ShaderNodeTexImage")||!texture->id||GS(texture->id->name)!=ID_IM)throw std::runtime_error("Saved authored normal image is invalid");binding.authored_normal_image=reinterpret_cast<Image *>(texture->id);}
    for(int slot=0;slot<custom_channel_count;++slot){const auto name="__BTS_UserPixels"+std::to_string(slot);if(auto *texture=bke::node_find_node_by_name(tree,name)){if(!STREQ(texture->idname,"ShaderNodeTexImage")||!texture->id||GS(texture->id->name)!=ID_IM)throw std::runtime_error("Saved custom image is invalid");binding.user_images[slot]=reinterpret_cast<Image *>(texture->id);binding.user_textures[slot]=texture;binding.user_unpremultiply[slot]=required("__BTS_UserUnpremultiply"+std::to_string(slot),"ShaderNodeVectorMath");}binding.user_present[slot]=set.users[slot].present;}
    if(auto *texture=bke::node_find_node_by_name(tree,"__BTS_PixelsDisp")){if(!STREQ(texture->idname,"ShaderNodeTexImage")||!texture->id||GS(texture->id->name)!=ID_IM)throw std::runtime_error("Saved displacement image is invalid");binding.displacement_image=reinterpret_cast<Image *>(texture->id);binding.displacement_texture=texture;binding.displacement_unpremultiply=required("__BTS_UnpremultiplyDisp","ShaderNodeVectorMath");binding.displacement_mix=required("__BTS_BaseMixDisp","ShaderNodeMixRGB");binding.displacement_node=required("__BTS_Displacement","ShaderNodeDisplacement");}binding.displacement_settings=set.displacement;
    pending->bindings.emplace(set.material_session_uid,std::move(binding));
  }
  impl_->cycles.reset();
  impl_=std::move(pending);
  for(const auto &set:document.texture_sets())shader_parameters(main,set);
}
void MaterialInspection::swap_prepared(MaterialInspection &prepared)noexcept{impl_.swap(prepared.impl_);}
bool MaterialInspection::update(Main &main,InspectionMode mode,std::span<const InspectionMaterial> materials)
{
  const int channel=int(mode);
  if (channel < -1 || channel > 7+custom_channel_count) throw std::invalid_argument("Invalid inspection channel");
  if (mode==InspectionMode::Material && impl_->bindings.empty()) return false;
  // Resolve the whole set before changing the active view switch.
  for (const auto &source : materials) {
    if (!impl_->bindings.contains(source.material_uid)) impl_->bindings.emplace(source.material_uid,bind(main,source.material_uid));
    for (float coverage : source.coverage)
      if (!std::isfinite(coverage) || coverage<0 || coverage>1) throw std::invalid_argument("Invalid inspection coverage");
  }
  bool changed=false;
  for (const auto &source : materials) {
    auto &binding=impl_->bindings.at(source.material_uid);if(source.mask_set)binding.mask_snapshot=*source.mask_set;
    if(!source.mesh_inspection&&!binding.mesh_displayed&&mode==InspectionMode::Mask&&binding.displayed_channel==channel&&binding.mask_display_linked&&source.mask_set&&GpuLayerPreview::supported(*source.mask_set,15)){
      if(binding.mask_async_layer!=source.mask_set->selected_layer){CompositeChannels channels{};channels[15]=true;queue_layer_edit(*source.mask_set,channels,false);changed=true;}continue;
    }
    if (!source.mesh_inspection&&!binding.mesh_displayed&&mode!=InspectionMode::Mask && binding.displayed_channel==channel && (binding.resolution ||
        (binding.displayed_values==source.authored && binding.displayed_coverage==source.coverage))) continue;
    changed=true;binding.mask_display_linked=false;
    auto &tree=*binding.material->nodetree;
    scalar(tree,input(*binding.view_switch,0),channel<0 ? 0 : 1);
    if(source.mesh_inspection){auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);BKE_ntree_update_after_single_tree_change(main,tree);if(source.mesh_map){baked_image(main,binding,*source.mesh_map,*source.mask_set);bke::node_add_link(tree,*binding.bake_texture,output(*binding.bake_texture,"Alpha"),*binding.coverage_mix,factor);bke::node_add_link(tree,*binding.bake_unpremultiply,output(*binding.bake_unpremultiply,"Vector"),*binding.coverage_mix,destination);}else{scalar(tree,factor,0);color(tree,destination,{0,0,0,1});}}
    else if(mode==InspectionMode::Normal){auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);BKE_ntree_update_after_single_tree_change(main,tree);scalar(tree,factor,1);if(binding.authored_normal_image){auto &texture=node(tree,"ShaderNodeTexImage","__BTS_AuthoredNormalPixels");bke::node_add_link(tree,texture,output(texture,"Color"),*binding.coverage_mix,destination);}else color(tree,destination,{.5,.5,1,1});}
    else if(mode==InspectionMode::Displacement){auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);BKE_ntree_update_after_single_tree_change(main,tree);scalar(tree,factor,1);if(binding.displacement_mix)bke::node_add_link(tree,*binding.displacement_mix,output(*binding.displacement_mix,"Color"),*binding.coverage_mix,destination);else color(tree,destination,{.5f,.5f,.5f,1});}
    else if(channel>=7){const int slot=channel-7;auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);BKE_ntree_update_after_single_tree_change(main,tree);if(binding.user_present[slot]&&binding.user_textures[slot]&&binding.user_unpremultiply[slot]){bke::node_add_link(tree,*binding.user_textures[slot],output(*binding.user_textures[slot],"Alpha"),*binding.coverage_mix,factor);bke::node_add_link(tree,*binding.user_unpremultiply[slot],output(*binding.user_unpremultiply[slot],"Vector"),*binding.coverage_mix,destination);}else{scalar(tree,factor,0);color(tree,destination,{0,0,0,1});}}
    else if(mode==InspectionMode::AmbientOcclusion){auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);BKE_ntree_update_after_single_tree_change(main,tree);scalar(tree,factor,1);if(binding.ao_image){auto &texture=node(tree,"ShaderNodeTexImage","__BTS_AOPixels");bke::node_add_link(tree,texture,output(texture,"Color"),*binding.coverage_mix,destination);}else color(tree,destination,{1,1,1,1});}
    else if (mode==InspectionMode::Mask) {
      auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);
      bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);
      BKE_ntree_update_after_single_tree_change(main,tree); // Refresh linked socket flags before assigning constants.
      const LayerMask *mask=nullptr;
      if(source.mask_set)for(const auto &layer:source.mask_set->layers)
        if(layer.id==source.mask_set->selected_layer&&layer.mask)mask=&*layer.mask;
      scalar(tree,factor,mask?1:0);
      if(mask&&(GpuLayerPreview::supported(*source.mask_set,15)||!mask->pixels.empty()||!mask->effects.empty())) {
        if(GpuLayerPreview::supported(*source.mask_set,15)){mask_image(main,binding,*source.mask_set,*mask,source.resolution,true);if(binding.mask_async_layer!=source.mask_set->selected_layer){CompositeChannels channels{};channels[15]=true;queue_layer_edit(*source.mask_set,channels,false);}}
        else if(binding.mask_async_layer!=source.mask_set->selected_layer){std::fprintf(stderr,"BTS_GPU_CPU_FALLBACK reason=unsupported-mask-inspection background=0\n");mask_image(main,binding,*source.mask_set,*mask,source.resolution);}
        bke::node_add_link(tree,*binding.mask_texture,output(*binding.mask_texture,"Color"),*binding.coverage_mix,destination);binding.mask_display_linked=true;
      }
      else {const float value=mask?mask->background:0;color(tree,destination,{value,value,value,1});}
    }
    else if (channel>=0) {
      const auto &v=source.authored;
      std::array<float,4> value;
      switch (mode) {
        case InspectionMode::BaseColor: value=v.base_color; break;
        case InspectionMode::Metallic: value={v.metallic,v.metallic,v.metallic,1}; break;
        case InspectionMode::Roughness: value={v.roughness,v.roughness,v.roughness,1}; break;
        case InspectionMode::Normal: value={v.normal[0]*.5f+.5f,v.normal[1]*.5f+.5f,v.normal[2]*.5f+.5f,1}; break;
        case InspectionMode::Bump: value={v.bump,v.bump,v.bump,1}; break;
        default: value={}; break;
      }
      auto &factor=input(*binding.coverage_mix,0),&destination=input(*binding.coverage_mix,2);
      if (binding.textures[channel]) {
        bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);
        auto &texture=*binding.textures[channel];
        bke::node_add_link(tree,texture,output(texture,"Alpha"),*binding.coverage_mix,factor);
        bke::node_add_link(tree,*binding.unpremultiply[channel],output(*binding.unpremultiply[channel],"Vector"),*binding.coverage_mix,destination);
      }
      else {
        bke::node_remove_socket_links(tree,factor);bke::node_remove_socket_links(tree,destination);
        BKE_ntree_update_after_single_tree_change(main,tree);
        scalar(tree,factor,source.coverage[channel]);color(tree,destination,value);
      }
    }
    BKE_ntree_update_after_single_tree_change(main,tree);
    DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);
    DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);
    binding.mesh_displayed=source.mesh_inspection;binding.displayed_channel=channel;binding.displayed_values=source.authored;binding.displayed_coverage=source.coverage;
    if(mode==InspectionMode::Normal&&!source.mesh_inspection){
      // The authored-normal inspection image may never have been sampled by
      // the material. Request it on demand without cancelling other channels.
      if(auto queued=impl_->gpu_stack_queued.find(source.material_uid);queued!=impl_->gpu_stack_queued.end())queued->second.channels[6]=true;
      else if(binding.gpu_stack_set&&GpuLayerPreview::supported(*binding.gpu_stack_set,6)){Impl::Request request{*binding.gpu_stack_set,binding.padding,impl_->generation[source.material_uid],bool(binding.ao_image)};request.channels={};request.channels[6]=true;impl_->gpu_stack_queued.emplace(source.material_uid,std::move(request));}
    }
  }
  return changed;
}
std::shared_ptr<TexturePadding> MaterialInspection::export_padding(std::uint32_t uid) const {return impl_->bindings.at(uid).padding;}
bool MaterialInspection::has_padding(std::uint32_t uid) const {
  const auto found=impl_->bindings.find(uid);return found!=impl_->bindings.end()&&bool(found->second.padding);
}
void MaterialInspection::set_padding_geometry(Main &main,std::uint32_t uid,const SceneSnapshot &snapshot) {
  auto padding=std::make_shared<TexturePadding>(snapshot,uid);
  if(!impl_->bindings.contains(uid))impl_->bindings.emplace(uid,bind(main,uid));
  if(auto &binding=impl_->bindings.at(uid);true){binding.world_context.invalidate_owners();binding.world_ao_lease.reset();binding.world_ao_tile=0;if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;binding.world_geometry_job.reset();binding.world_geometry.reset();binding.world_signature.clear();++binding.world_geometry_revision;}impl_->bindings.at(uid).mask_cached=false;impl_->bindings.at(uid).projection_cache.clear();impl_->bindings.at(uid).ownership_cache.clear();impl_->bindings.at(uid).geometry_names.clear();if(auto &job=impl_->bindings.at(uid).projection_job){job->cancelled=true;job.reset();}if(auto &job=impl_->bindings.at(uid).margin_job){job->cancelled=true;job.reset();}impl_->bindings.at(uid).margins_ready=false;impl_->gpu_preview.set_padding_geometry(uid,{},{});impl_->bindings.at(uid).padding=std::move(padding);impl_->bindings.at(uid).native_per_uv=native_length_per_uv(snapshot,uid);
}
std::vector<unsigned char> MaterialInspection::uv_coverage(std::uint32_t uid,int tile) const {const auto &binding=impl_->bindings.at(uid);if(!binding.padding)throw std::runtime_error("Export UV geometry is missing");return binding.padding->uv_coverage(binding.tile_sizes.at(tile).first,tile,binding.tile_sizes.at(tile).second);}
std::vector<float> MaterialInspection::copy_pixels(std::uint32_t uid,int channel,int tile) const {
  const auto &binding=impl_->bindings.at(uid);
  if(channel==15&&binding.mask_snapshot){const auto &set=*binding.mask_snapshot;std::vector<float> pixels(std::size_t(set.width(tile))*set.rows(tile)*4);for(int y=0;y<set.rows(tile);++y)for(int x=0;x<set.width(tile);++x){const auto value=gpu_logical_pixel(set,15,x,y,tile);std::copy(value.begin(),value.end(),pixels.begin()+(std::size_t(y)*set.width(tile)+x)*4);}return pixels;}
  if(binding.gpu_stack_active.at(channel)&&binding.gpu_stack_set){
    // Explicit diagnostics/export boundary, never a viewport composition path.
    // Include native padding and cross-tile dependencies just like persistence;
    // sampling the unpadded authoring stack gave misleading roundtrip hashes.
    std::fprintf(stderr,"BTS_GPU_CPU_REFERENCE reason=diagnostic-or-export-query channel=%d\n",channel);
    CompositeChannels wanted{};wanted[channel]=true;std::atomic_bool cancelled=false;
    const auto &set=*binding.gpu_stack_set;auto reference=native_composite_job(set,bool(binding.ao_image),binding.padding?binding.padding->clone():nullptr,0,cancelled,false,wanted);
    if(!reference)throw std::runtime_error("CPU diagnostic reference cancelled");return std::move(reference->tiles.at(tile)[channel]);
  }if(channel<5&&binding.gpu_uniform_values[channel]){const auto size=binding.tile_sizes.at(tile);std::vector<float> pixels(std::size_t(size.first)*size.second*4);for(std::size_t i=0;i<pixels.size();i+=4)std::copy(binding.gpu_uniform_values[channel]->begin(),binding.gpu_uniform_values[channel]->end(),pixels.begin()+i);return pixels;}auto *image=channel==16?binding.displacement_image:channel==15?binding.mask_image:channel>=7?binding.user_images.at(channel-7):channel==6?binding.authored_normal_image:channel==5?binding.ao_image:binding.images.at(channel);if(!image)return {};
  ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);
  struct Release {Image *image;ImBuf *buffer;~Release(){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);}} release{image,buffer};
  if(!buffer||!buffer->float_data()||buffer->channels!=4)throw std::runtime_error("Export material buffer unavailable");
  return {buffer->float_data(),buffer->float_data()+std::size_t(buffer->x)*buffer->y*4};
}
std::uint64_t MaterialInspection::image_hash(std::uint32_t uid,int channel,int tile) const {
  auto &binding=impl_->bindings.at(uid);if((channel==15&&binding.mask_snapshot)||(binding.gpu_stack_active.at(channel)&&binding.gpu_stack_set)){const auto pixels=copy_pixels(uid,channel,tile);std::uint64_t hash=1469598103934665603ULL;for(float value:pixels){hash^=std::bit_cast<std::uint32_t>(value);hash*=1099511628211ULL;}return hash;}if(channel<5&&binding.gpu_uniform_values[channel]){std::uint64_t hash=1469598103934665603ULL;const auto size=binding.tile_sizes.at(tile);for(std::size_t i=0;i<std::size_t(size.first)*size.second;++i)for(float value:*binding.gpu_uniform_values[channel]){hash^=std::bit_cast<std::uint32_t>(value);hash*=1099511628211ULL;}return hash;}auto *image=binding.preview_active.at(channel)?binding.preview_images[channel]:channel==16?binding.displacement_image:channel==15?binding.mask_image:channel>=7?binding.user_images.at(channel-7):channel==6?binding.authored_normal_image:channel==5?binding.ao_image:binding.images.at(channel);if(!image)throw std::runtime_error("No derived image");ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);if(!buffer||!buffer->float_data()){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Derived image is unavailable");}std::uint64_t hash=1469598103934665603ULL;const auto *data=buffer->float_data();for(std::size_t i=0;i<std::size_t(buffer->x)*buffer->y*4;++i){hash^=std::bit_cast<std::uint32_t>(data[i]);hash*=1099511628211ULL;}BKE_image_release_ibuf(image,buffer,nullptr);return hash;
}
std::array<float,4> MaterialInspection::sample_pixel(std::uint32_t uid,int channel,int x,int y,int tile) const {
  auto &binding=impl_->bindings.at(uid);if(channel==15&&binding.mask_snapshot)return gpu_logical_pixel(*binding.mask_snapshot,15,x,y,tile);if(binding.gpu_stack_active.at(channel)&&binding.gpu_stack_set){if(channel==3){const auto &set=*binding.gpu_stack_set;if(x<0||y<0||x>=set.width(tile)||y>=set.rows(tile))throw std::runtime_error("Invalid GPU normal sample");const auto pixels=copy_pixels(uid,channel,tile);std::array<float,4> result;std::copy_n(pixels.data()+(std::size_t(y)*set.width(tile)+x)*4,4,result.begin());return result;}return gpu_logical_pixel(*binding.gpu_stack_set,channel,x,y,tile);}if(channel<5&&binding.gpu_uniform_values[channel])return *binding.gpu_uniform_values[channel];auto *image=binding.preview_active.at(channel)?binding.preview_images[channel]:channel==16?binding.displacement_image:channel==15?binding.mask_image:channel>=7?binding.user_images.at(channel-7):channel==6?binding.authored_normal_image:channel==5?binding.ao_image:binding.images.at(channel);if(!image)throw std::runtime_error("No composite image");
  ImageUser user{};user.tile=tile;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);
  if(buffer&&binding.preview_active.at(channel)){const auto size=binding.tile_sizes.at(tile);x=int(double(x)*buffer->x/size.first);y=int(double(y)*buffer->y/size.second);}
  if(!buffer||!buffer->float_data()||x<0||y<0||x>=buffer->x||y>=buffer->y){if(buffer)BKE_image_release_ibuf(image,buffer,nullptr);throw std::runtime_error("Invalid composite sample");}
  std::array<float,4> result;std::copy_n(buffer->float_data()+(std::size_t(y)*buffer->x+x)*4,4,result.begin());BKE_image_release_ibuf(image,buffer,nullptr);return result;
}
std::vector<std::array<float,4>> MaterialInspection::diagnostic_gpu_pixels(std::uint32_t uid,int channel,std::span<const std::array<int,2>> coordinates,int tile)const{
 const auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end())throw std::runtime_error("GPU diagnostic material is missing");auto *image=composite_image(found->second,channel);if(!image)throw std::runtime_error("GPU diagnostic channel is missing");
 const auto native=BKE_image_gpu_override_texture(image);if(!native.texture||!*native.texture)throw std::runtime_error("GPU diagnostic channel has no native texture");ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(image,tile,region))throw std::runtime_error("GPU diagnostic tile is missing");
 std::vector<std::array<int,3>> positions;positions.reserve(coordinates.size());for(const auto &p:coordinates){if(p[0]<0||p[1]<0||p[0]>=region.width||p[1]>=region.height)throw std::invalid_argument("GPU diagnostic coordinate is out of bounds");positions.push_back({region.x+p[0],region.y+p[1],region.layer});}
 std::vector<std::array<float,4>> result;result.reserve(positions.size());for(std::size_t first=0;first<positions.size();first+=4096){const auto values=probe_gpu_texture(*native.texture,std::span(positions).subspan(first,std::min<std::size_t>(4096,positions.size()-first)));result.insert(result.end(),values.begin(),values.end());}return result;
}
std::optional<std::array<float,4>> MaterialInspection::diagnostic_gpu_pixel(std::uint32_t uid,int channel,int x,int y,int tile)const{
 const auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end())return {};auto *image=composite_image(found->second,channel);if(!image)return {};
 const auto native=BKE_image_gpu_override_texture(image);if(!native.texture||!*native.texture)return {};
 ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(image,tile,region)||x<0||y<0||x>=region.width||y>=region.height)return {};
 const std::array<std::array<int,3>,1> positions{{{region.x+x,region.y+y,region.layer}}};return probe_gpu_texture(*native.texture,positions).front();
}
double MaterialInspection::verify_gpu_pixels(std::uint32_t uid) const {
 auto &binding=impl_->bindings.at(uid);if(!binding.gpu_stack_set)throw std::runtime_error("GPU stack diagnostic has no snapshot");
 const auto &set=*binding.gpu_stack_set;if(const auto reason=spatial_oracle_unavailable(set)){std::printf("BTS_NATIVE_GPU_ORACLE_UNAVAILABLE scope=whole_material reason=%s\n",reason->c_str());throw SpatialOracleUnavailable("Whole-material CPU parity unavailable: "+*reason);}CompositeChannels channels{};
 for(int c=0;c<17;++c)if(binding.gpu_stack_active[c]){auto *image=composite_image(binding,c);if(image){auto native=BKE_image_gpu_override_texture(image);channels[c]=native.texture&&*native.texture;}}
 std::atomic_bool cancelled=false;auto oracle=native_composite_job(set,bool(binding.ao_image),binding.padding?binding.padding->clone():nullptr,0,cancelled,false,channels);
 if(!oracle)throw std::runtime_error("GPU diagnostic CPU oracle cancelled");double maximum=0;int probes=0,checked_channels=0;
 for(int c=0;c<17;++c)if(channels[c]){
  ++checked_channels;auto *image=composite_image(binding,c);auto *texture=*BKE_image_gpu_override_texture(image).texture;
  std::vector<std::array<int,3>> positions,logical_positions;std::vector<std::array<float,4>> expected;
  for(int tile:set.tiles){ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(image,tile,region)||region.width!=set.width(tile)||region.height!=set.rows(tile))throw std::runtime_error("GPU diagnostic target is not texture-set resolution");const auto &pixels=oracle->tiles.at(tile)[c];
   const bool dense=std::getenv("BTS_GPU_DENSE_ORACLE");const int nx=dense?region.width:8,ny=dense?region.height:8;for(int iy=0;iy<ny;++iy)for(int ix=0;ix<nx;++ix){const int x=dense?ix:(2*ix+1)*region.width/16,y=dense?iy:(2*iy+1)*region.height/16;logical_positions.push_back({tile,x,y});positions.push_back({region.x+x,region.y+y,region.layer});std::array<float,4> value;std::copy_n(pixels.data()+(std::size_t(y)*region.width+x)*4,4,value.begin());expected.push_back(value);}
  }
  std::vector<std::array<float,4>> actual;for(std::size_t first=0;first<positions.size();first+=4096){const auto batch=probe_gpu_texture(texture,std::span(positions).subspan(first,std::min<std::size_t>(4096,positions.size()-first)));actual.insert(actual.end(),batch.begin(),batch.end());}double error=0;
  for(std::size_t i=0;i<actual.size();++i)for(int k=0;k<4;++k){const double delta=std::abs(double(actual[i][k])-expected[i][k]);error=std::max(error,delta);
   // A float32 projection can cross an sRGB8 rounding boundary relative to
   // the double CPU oracle. Accept at most one encoded code step, including
   // native half-float storage error; alpha retains its original tolerance.
   bool adjacent_code=false;
   if(c==0&&k<3&&set.precision[0]==ChannelPrecision::SRGB8&&delta<=.0095){
    const auto base=precision_channel(set.base_values,0,set.precision[0]);
    const auto code=[](float value){value=std::clamp(value,0.f,1.f);return int(std::lround(255*(value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f)));};
    adjacent_code=std::abs(code(actual[i][k]+base[k]*(1-actual[i][3]))-code(expected[i][k]+base[k]*(1-expected[i][3])))<=1;
   }
   if(!std::isfinite(actual[i][k])||(delta>0.002*std::max(1.f,std::abs(expected[i][k]))&&!adjacent_code)){auto *raw=static_cast<float *>(GPU_texture_read(texture,GPU_DATA_FLOAT,0));const auto position=positions[i];const std::size_t offset=((std::size_t(position[2])*GPU_texture_height(texture)+position[1])*GPU_texture_width(texture)+position[0])*4;std::printf("BTS_GPU_ORACLE_DIAG channel=%d probe=%zu position=%d,%d,%d gather=%.8g,%.8g,%.8g,%.8g raw=%.8g,%.8g,%.8g,%.8g expected=%.8g,%.8g,%.8g,%.8g\n",c,i,position[0],position[1],position[2],actual[i][0],actual[i][1],actual[i][2],actual[i][3],raw[offset],raw[offset+1],raw[offset+2],raw[offset+3],expected[i][0],expected[i][1],expected[i][2],expected[i][3]);MEM_delete(raw);
const auto location=logical_positions[i];const int tile=location[0],px=location[1],py=location[2];
const auto logical=gpu_logical_pixel(set,c,px,py,tile);std::printf("BTS_GPU_ORACLE_LOGICAL tile=%d x=%d y=%d value=%.9g,%.9g,%.9g,%.9g precision=%d\n",tile,px,py,logical[0],logical[1],logical[2],logical[3],int(gpu_channel_precision(set,c)));
impl_->gpu_preview.diagnose_pixel(set,c,tile,px,py);
for(const auto &layer:evaluation_layers(set)){const auto channel=gpu_layer_channel(layer,c);const auto paint=channel.pixels->sample_resampled(channel.plane,px,py,set.width(tile),tile,set.rows(tile));const auto fill=TextureDocument::fill_sample(set,layer,channel.fill_channel,px,py,tile);std::printf("BTS_GPU_ORACLE_CHANNEL id=%s depth=%d enabled=%d opacity=%.9g blend=%d value=%.9g paint=%.9g,%.9g fill=%.9g,%.9g\n",layer.id.toString().toUtf8().constData(),layer.depth,channel.enabled,channel.opacity,int(channel.blend),channel.value[0],paint[0],paint[3],fill[0],fill[3]);}
for(const auto &layer:evaluation_layers(set))std::printf("BTS_GPU_ORACLE_LAYER kind=%d mode=%d atom=%d texture=%d opacity=%.3f blend=%d visible=%d mask=%d rgb=%.5f,%.5f,%.5f\n",int(layer.kind),layer.fill.mode,layer.fill.atom,bool(layer.fill.texture),layer.opacity[0],int(layer.blending[0]),layer.visible,bool(layer.mask),layer.values.base_color[0],layer.values.base_color[1],layer.values.base_color[2]);throw std::runtime_error("Native GPU pipeline differs from CPU oracle channel="+std::to_string(c)+" probe="+std::to_string(i)+" component="+std::to_string(k)+" actual="+std::to_string(actual[i][k])+" expected="+std::to_string(expected[i][k]));}}
  maximum=std::max(maximum,error);probes+=int(actual.size());
 }
 if(!probes)throw std::runtime_error("No native GPU channel was verified");std::printf("BTS_NATIVE_GPU_ORACLE channels=%d probes=%d tiles=%zu max_error=%.9g full_resolution=1\n",checked_channels,probes,set.tiles.size(),maximum);return maximum;
}
bool MaterialInspection::has_pixels(std::uint32_t uid) const {
  const auto it=impl_->bindings.find(uid);return it!=impl_->bindings.end() && it->second.resolution!=0;
}
bool MaterialInspection::update_pixels(Main &main,const TextureSet &source,int resolution,PaintPixels::Bounds bounds,bool interactive,bool allocate_only) {const auto set=resident_texture_view(source);performance::Scope timing_all(allocate_only?"allocate_gpu_images":"update_pixels_cpu");
  impl_->invalidate(set.material_session_uid);
  const int height=set.rows();
  require_gpu_texture_extent(resolution,height,"Native paint image");
  const auto uid=set.material_session_uid;
  if (!impl_->bindings.contains(uid)) impl_->bindings.emplace(uid,bind(main,uid));
  auto &binding=impl_->bindings.at(uid);binding.mask_snapshot=set;binding.shader_parameters=set.shader_parameters;restore_preview(main,binding);auto &tree=*binding.material->nodetree;set.native_per_uv=binding.native_per_uv;
  const bool created=binding.resolution==0;bool shader_changed=created;
  std::map<int,std::pair<int,int>> target_sizes;for(int number:set.tiles)target_sizes[number]={set.width(number),set.rows(number)};
  if(!created&&(binding.resolution!=resolution||binding.height!=height||binding.tile_sizes!=target_sizes)){
    std::vector<Image *> images(binding.images.begin(),binding.images.end());if(binding.ao_image)images.push_back(binding.ao_image);if(binding.authored_normal_image)images.push_back(binding.authored_normal_image);if(binding.mask_image)images.push_back(binding.mask_image);if(binding.displacement_image)images.push_back(binding.displacement_image);for(auto *image:binding.user_images)if(image)images.push_back(image);
    for(auto *image:images)if(image){for(ImageTile &tile:image->tiles){ImageUser user{};user.tile=tile.tile_number;if(!native_scale_image(image,set.width(tile.tile_number),set.rows(tile.tile_number),&user))throw std::runtime_error("Cannot resize native channel image");tile.gen_x=set.width(tile.tile_number);tile.gen_y=set.rows(tile.tile_number);}BKE_image_free_gputextures(image);}
    for(auto *image:binding.world_normal_images)if(image)native_world_normal_resize(*image,set);binding.world_images_full=false;binding.world_published_signatures.clear();binding.world_scratch={};
    binding.resolution=resolution;binding.height=height;binding.preview_images={};binding.world_context.invalidate_owners();binding.world_ao_lease.reset();binding.world_ao_tile=0;if(binding.world_geometry_job)binding.world_geometry_job->cancelled=true;binding.world_geometry_job.reset();binding.world_geometry.reset();binding.world_signature.clear();binding.world_queued_generation=~std::uint64_t(0);binding.world_registry_generation=0;binding.projection_cache.clear();binding.ownership_cache.clear();binding.geometry_names.clear();if(binding.projection_job){binding.projection_job->cancelled=true;binding.projection_job.reset();}binding.normal_presence.clear();binding.normal_count=0;binding.ao_presence.clear();binding.ao_count=0;binding.displacement_presence.clear();binding.displacement_count=0;binding.displacement_minimum=0;binding.displacement_maximum=1;binding.mask_cached=false;binding.displayed_channel=-2;bounds={0,0,resolution,height};
  }
  if (created) {
    auto *principled=native_principled_surface(*binding.material);
    if (!principled) throw std::runtime_error("Paint material is missing its Principled surface");
    const float empty[4]={0,0,0,0};
    // Allocate all native buffers before linking their outputs into the material.
    for (int channel=0;channel<5;++channel) {
      const std::string name="BTS Composite "+std::to_string(uid)+" "+channel_names[channel];
      auto *image=native_generated_image(&main,set.width(1001),set.rows(1001),name.c_str(),32,1,IMA_GENTYPE_BLANK,empty,false,channel!=0,set.tiles!=std::set<int>{1001});
      if (!image) throw std::runtime_error("Cannot allocate native paint image");
      image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;binding.images[channel]=image;
      if(channel==0)STRNCPY(image->colorspace_settings.name,main.colorspace.scene_linear_name);
      if(image->source==IMA_SRC_TILED) {
        for(int number:set.tiles)if(number!=1001) {
          auto *tile=BKE_image_add_tile(image,number,nullptr);if(!tile)throw std::runtime_error("Cannot allocate UDIM tile");
          tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT;
          if(!native_fill_tile(image,tile))throw std::runtime_error("Cannot initialize UDIM tile");
        }
        if(!set.tiles.contains(1001))BKE_image_remove_tile(image,BKE_image_get_tile(image,1001));
      }
      if (channel==0&&!native_gpu_image(image))for(int number:set.tiles) {
        STRNCPY(image->colorspace_settings.name,main.colorspace.scene_linear_name);
        ImageUser user{};user.tile=number;auto *buffer=BKE_image_acquire_ibuf(image,&user,nullptr);
        if (!buffer) throw std::runtime_error("Cannot acquire native color image");
        IMB_colormanagement_assign_float_colorspace(buffer,main.colorspace.scene_linear_name);
        BKE_image_release_ibuf(image,buffer,nullptr);
      }
    }
    const char *inputs[]={"Base Color","Metallic","Roughness"};
    for (int channel=0;channel<5;++channel) {
      auto &texture=node(tree,"ShaderNodeTexImage",("__BTS_Pixels"+std::to_string(channel)).c_str());
      texture.id=&binding.images[channel]->id;id_us_plus(texture.id);
      auto &mix=node(tree,"ShaderNodeMixRGB",("__BTS_BaseMix"+std::to_string(channel)).c_str());
      bke::node_add_link(tree,texture,output(texture,"Alpha"),mix,input(mix,0));
      // Filter associated color/data and coverage together, then unassociate exactly once.
      // This avoids undefined transparent pixels tinting soft stroke edges under bilinear sampling.
      auto &divide=node(tree,"ShaderNodeVectorMath",("__BTS_Unpremultiply"+std::to_string(channel)).c_str());
      divide.custom1=NODE_VECTOR_MATH_DIVIDE;BKE_ntree_update_tag_node_property(&tree,&divide);
      bke::node_add_link(tree,texture,output(texture,"Color"),divide,input(divide,0));
      bke::node_add_link(tree,texture,output(texture,"Alpha"),divide,input(divide,1));
      bke::node_add_link(tree,divide,output(divide,"Vector"),mix,input(mix,2));
      binding.textures[channel]=&texture;binding.unpremultiply[channel]=&divide;binding.base_mix[channel]=&mix;
      if (channel<3) {
        auto *destination=bke::node_find_enabled_input_socket(*principled,inputs[channel]);
        if (!destination) throw std::runtime_error("Native paint channel input is missing");
        bke::node_add_link(tree,mix,output(mix,"Color"),*principled,*destination);
      }
    }
    auto &normal=node(tree,"ShaderNodeNormalMap","__BTS_PaintNormal");
    auto &bump=node(tree,"ShaderNodeBump","__BTS_PaintHeight");
    auto *normal_color=bke::node_find_enabled_input_socket(normal,"Color");
    auto *bump_normal=bke::node_find_enabled_input_socket(bump,"Normal");
    auto *bump_height=bke::node_find_enabled_input_socket(bump,"Height");
    auto *surface_normal=bke::node_find_enabled_input_socket(*principled,"Normal");
    if (!normal_color || !bump_normal || !bump_height || !surface_normal) throw std::runtime_error("Native normal/height socket missing");
    bke::node_add_link(tree,*binding.base_mix[3],output(*binding.base_mix[3],"Color"),normal,*normal_color);
    bke::node_add_link(tree,normal,output(normal,"Normal"),bump,*bump_normal);
    bke::node_add_link(tree,*binding.base_mix[4],output(*binding.base_mix[4],"Color"),bump,*bump_height);
    bke::node_add_link(tree,bump,output(bump,"Normal"),*principled,*surface_normal);
    binding.resolution=resolution;binding.height=height;binding.displayed_channel=-2;
    bounds={0,0,resolution,height};
  }
  // CPU metadata/oracle setup retains the legacy graph. Install GPU-only
  // world targets lazily, including bindings first bootstrapped without a GPU.
  if(GPU_context_active_get()&&!binding.world_normal_images[0]){
    auto *height_node=bke::node_find_node_by_name(tree,"__BTS_PaintHeight");
    auto *surface=native_principled_surface(*binding.material);
    if(!height_node||!surface)throw std::runtime_error("Native world Normal shader endpoints are missing");
    install_native_world_normal_shader(main,tree,set,binding.world_normal_images,binding.world_normal_enabled,*height_node,*surface);
    for(int side=0;side<2;++side){native_world_normal_placeholder(*binding.world_normal_images[side]);}
    shader_changed=true;
  }
  if(binding.tile_sizes!=target_sizes){if(binding.margin_job){binding.margin_job->cancelled=true;binding.margin_job.reset();}binding.margins_ready=false;if(binding.padding)impl_->gpu_preview.set_padding_geometry(uid,{},{});}
  binding.tile_sizes=target_sizes;
  if(binding.padding&&!binding.margin_job&&!binding.margins_ready)binding.margin_job=MarginGeometryJob::start(binding.padding,target_sizes);
  ensure_authored_normal_image(main,binding,set);
  if(set.ao_channel||set.mesh_maps[3]||binding.ao_image)ensure_ao_image(main,binding,set,resolution);
  shader_changed|=shader_parameters(main,set);
  auto *normal_node=bke::node_find_node_by_name(tree,"__BTS_PaintNormal");if(!normal_node)throw std::runtime_error("Paint normal node is missing");const int tangent_flags=(normal_node->custom1&~1)|(set.tangent_per_fragment?1:0);if(normal_node->custom1!=tangent_flags){shader_changed=true;normal_node->custom1=tangent_flags;BKE_ntree_update_tag_node_property(&tree,normal_node);}
  auto *bump=bke::node_find_node_by_name(tree,"__BTS_PaintHeight");auto *strength=bump?bke::node_find_enabled_input_socket(*bump,"Strength"):nullptr;if(!strength)throw std::runtime_error("Height shader strength is missing");scalar(tree,*strength,0); // Height is converted once into the complete tangent-normal image.
  if(allocate_only){
    // Native Image/node allocation is owner-thread work. No layer evaluation,
    // pixel hashing or CPU padding belongs to interactive initialization.
    for(int channel=0;channel<5;++channel){const auto base=encoded_material_base(set,channel);color(tree,input(*binding.base_mix[channel],1),base);binding.last_base[channel]=base;}
    binding.previous_height_base=precision_channel(set.channels[4]?set.base_values:MaterialValues{},4,set.precision[4])[0];binding.bases_valid=true;
    BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);
    std::printf("BTS_GPU_IMAGE_BOOTSTRAP uid=%u tiles=%zu resolution=%d cpu_composite=0\n",uid,set.tiles.size(),resolution);return created;
  }
  throw std::logic_error("CPU pixel updates are forbidden; allocate a GPU target and queue the GPU stack");
}

bool MaterialInspection::preview_uniform(Main &,const TextureSet &set,int channel){
 CompositeChannels channels{};channels.at(channel)=true;return queue_layer_edit(set,channels,false);
}
bool MaterialInspection::displacement_content(std::uint32_t uid)const{const auto found=impl_->bindings.find(uid);return found!=impl_->bindings.end()&&(found->second.preview_active[16]?found->second.preview_displacement_content:found->second.gpu_displacement_presence.value_or(found->second.displacement_count>0));}
bool MaterialInspection::shader_parameters(Main &main,const TextureSet &set){
  set.shader_parameters.validate();const auto found=impl_->bindings.find(set.material_session_uid);if(found==impl_->bindings.end())return false;auto &binding=found->second;binding.shader_parameters=set.shader_parameters;binding.material_layering=set.material_layering;binding.layering_directx=set.normal_format==NormalMapFormat::DirectX;auto &tree=*binding.material->nodetree;bool changed=false;
  // Custom channels need fixed GPU targets even without the optional layered
  // shader. Allocate once when a slot is introduced, never evaluate its pixels
  // on the owner thread. Subsequent layer edits only publish into this image.
  if(binding.resolution)for(int slot=0;slot<custom_channel_count;++slot){
    const bool needed=set.users[slot].allocated||set.users[slot].present||
      (set.material_layering.enabled&&std::find(set.material_layering.mask_slots.begin(),set.material_layering.mask_slots.end(),slot)!=set.material_layering.mask_slots.end());
    if(needed&&!binding.user_images[slot]){update_user_image(main,binding,set,slot,{},true,true);changed=true;}
    if(binding.user_present[slot]!=set.users[slot].present){binding.user_present[slot]=set.users[slot].present;binding.displayed_channel=-2;}
  }
  const auto update=[&](const char *name,const char *socket,float value){auto *n=bke::node_find_node_by_name(tree,name);auto *s=n?bke::node_find_enabled_input_socket(*n,socket):nullptr;if(!s||!s->default_value)return;if(static_cast<bNodeSocketValueFloat *>(s->default_value)->value==value)return;scalar(tree,*s,value);changed=true;};
  binding.scattering_slot=-1;for(int i=0;i<custom_channel_count;++i)if(set.users[i].present&&set.users[i].scattering)binding.scattering_slot=i;
  if(!set.shader_parameters.openpbr){changed|=apply_openpbr_shader(main,*binding.material,std::nullopt,impl_->subsurface_enabled);changed|=subsurface_shader(binding,impl_->subsurface_enabled);}
  update("__BTS_PaintNormal","Strength",set.shader_parameters.normal_intensity);update("__BTS_AOMaterial","Factor",set.shader_parameters.ao_intensity);
  if(binding.resolution||!set.material_layering.enabled){if(const auto sources=impl_->layering_sources.images(set.material_layering))changed|=binding.layered_shader.apply(main,*binding.material,set.material_layering,set.shader_parameters.normal_intensity,set.shader_parameters.ao_intensity,binding.layering_directx,*sources);else impl_->layering_sources_pending=true;}
  if(set.shader_parameters.openpbr)changed|=apply_openpbr_shader(main,*binding.material,set.shader_parameters.openpbr,impl_->subsurface_enabled);
  if(!set.material_layering.enabled){
   auto &library=impl_->gpu_preview.native_world_sources();const TextureLayer *coat=nullptr;
   for(const auto &layer:evaluation_layers(set))if(layer.visible&&layer.kind==LayerKind::Fill&&layer.channels[0]&&layer.opacity[0]>0){if(layer.fill.native_material&&library.contains(layer.fill.material_resource))coat=&layer;else coat=nullptr;}
   const auto signature=coat?coat->fill.material_resource+QJsonDocument(native_material_description_json(*coat->fill.native_material)).toJson(QJsonDocument::Compact).toStdString()+std::to_string(coat->opacity[0]):std::string{};
   if(signature!=binding.coat_signature){
    if(auto *surface=native_principled_surface(*binding.material))changed|=apply_native_material_coat(main,tree,*surface,coat?&library.source_graph(coat->fill.material_resource):nullptr,coat?coat->fill.native_material.get():nullptr,coat?coat->opacity[0]:0.f);
    binding.coat_signature=signature;
   }
  }
  if(changed){BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);}return changed;
}
bool MaterialInspection::openpbr_reapply_is_stable(Main &main,std::uint32_t uid){const auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end())return true;auto &b=found->second;return !apply_openpbr_shader(main,*b.material,b.shader_parameters.openpbr,impl_->subsurface_enabled);}
bool MaterialInspection::set_subsurface_enabled(Main &main,bool enabled){if(impl_->subsurface_enabled==enabled)return false;impl_->subsurface_enabled=enabled;bool changed=false;for(auto &[uid,binding]:impl_->bindings)if(binding.shader_parameters.openpbr?apply_openpbr_shader(main,*binding.material,binding.shader_parameters.openpbr,enabled):subsurface_shader(binding,enabled)){auto &tree=*binding.material->nodetree;BKE_ntree_update_after_single_tree_change(main,tree);DEG_id_tag_update_ex(&main,&binding.material->id,ID_RECALC_SHADING);DEG_id_tag_update_ex(&main,&tree.id,ID_RECALC_NTREE_OUTPUT);changed=true;}return changed;}
bool MaterialInspection::preview_displacement(Main &main,const TextureSet &set){const auto found=impl_->bindings.find(set.material_session_uid);if(found==impl_->bindings.end()||!found->second.displacement_image)return false;found->second.displacement_settings=set.displacement;const bool changed=displacement_shader(main,found->second);if(changed){impl_->gpu_displacement_changed=true;impl_->cycles.changed(*found->second.displacement_image,set.material_session_uid,16);}return changed;}
void MaterialInspection::prepare_pixels(Main &main,const TextureSet &source){const auto set=resident_texture_view(source);if(!has_pixels(set.material_session_uid)){update_pixels(main,set,set.texture_resolution,{},true,true);if(!queue_layer_edit(set,complete_composite_channels,true))throw std::runtime_error("GPU initial image queue failed");}}
void MaterialInspection::queue_composite(const TextureSet &set){
 // Compatibility entry point: it must never schedule a CPU persistence or
 // interactive fallback. Unsupported input remains visible as an explicit error.
 if(!queue_layer_edit(set,complete_composite_channels,true))throw std::runtime_error("GPU layer stack target is unavailable");
}

bool MaterialInspection::queue_layer_edit(const TextureSet &source,CompositeChannels channels,bool finalize){const auto set=resident_texture_view(source);PaintMetadataScope metadata;performance::Scope timing("queue_layer_edit");
  if(set.disabled||!set.uniform_editable)return true;
  impl_->gpu_preview.validate_native_mapping_instances(set);
  const auto invalidation_start=PropertyClock::now();
  auto found=impl_->bindings.find(set.material_session_uid);if(found==impl_->bindings.end())return false;
  if(!found->second.resolution){update_pixels(*found->second.owner_main,set,set.texture_resolution,{},true,true);channels=complete_composite_channels;}
  auto &binding=found->second;binding.mask_snapshot=set;if(const auto failed=impl_->failed_channels.find(set.material_session_uid);failed!=impl_->failed_channels.end()){for(int c=0;c<17;++c)channels[c]=channels[c]||failed->second[c];}std::map<int,std::pair<int,int>> sizes;for(int tile:set.tiles)sizes[tile]={set.width(tile),set.rows(tile)};if(sizes!=binding.tile_sizes)return false;
  // Prepare immutable geometry while ordinary layer edits are already settling,
  // so switching to a projection does not introduce a first-use CPU detour.
  if(binding.padding){
    if(!binding.margin_job&&!binding.margins_ready)binding.margin_job=MarginGeometryJob::start(binding.padding,sizes);
    const bool needs_surface=std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){
      return (layer.kind==LayerKind::Fill&&bool(layer.fill.native_material))||(layer.geometry.type==0&&!layer.geometry.meshes.empty())||
        std::any_of(layer.effects.begin(),layer.effects.end(),[](const auto &effect){return effect.enabled&&effect.kind==ContentEffectKind::Generator&&(effect.triplanar_grunge||effect.surface_fallback);})||
        (layer.mask&&any_mask_effect(layer.mask->effects,[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Generator&&(effect.triplanar_grunge||effect.surface_fallback);}))||
        (layer.kind==LayerKind::Fill&&fill_surface_projection(layer.fill)&&(layer.fill.atom||fill_has_texture(layer.fill)||fill_spatial_projection(layer.fill)))||
        (layer.mask&&any_mask_effect(layer.mask->effects,[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Fill&&effect.anchor_source.isNull()&&fill_surface_projection(effect.mapping.projection);}));
    });
    if(needs_surface){
      bool complete=binding.ownership_cache.size()==sizes.size();for(const auto &[tile,size]:sizes){const auto found=binding.projection_cache.find(tile);complete&=found!=binding.projection_cache.end()&&found->second->resolution==size.first&&found->second->height==size.second;}
      if(complete){set.geometry_ownership=binding.ownership_cache;set.geometry_names=binding.geometry_names;set.projection_tiles=binding.projection_cache;set.projection_geometry=binding.projection_cache.begin()->second;}
      else if(!binding.projection_job)binding.projection_job=ProjectionGeometryJob::start(binding.padding,sizes);
    }
  }
  // Optional channel allocation changes topology once; its pixels are derived
  // by the existing preview/final jobs rather than recomputing every channel.
  if(set.displacement.allocated||binding.displacement_image)ensure_displacement_image(*binding.owner_main,binding,set);
  shader_parameters(*binding.owner_main,set);
  if(set.ao_channel||set.mesh_maps[3])ensure_ao_image(*binding.owner_main,binding,set,set.texture_resolution);
  for(int c=0;c<5;++c)native_image_precision(binding.images[c],set.precision[c]);
  native_image_precision(binding.authored_normal_image,set.precision[3]);native_image_precision(binding.ao_image,set.precision[5]);native_image_precision(binding.displacement_image,set.displacement.precision);
  for(int i=0;i<8;++i)native_image_precision(binding.user_images[i],set.users[i].precision);
  // Channel enablement changes the uniform base, not the image dimensions.
  // Keep the GPU stack path instead of a synchronous full-resolution re-evaluation.
  bool base_changed=false;auto &tree=*binding.material->nodetree;
  for(int c=0;c<5;++c){const auto base=encoded_material_base(set,c);if(!binding.bases_valid||base!=binding.last_base[c]){color(tree,input(*binding.base_mix[c],1),base);binding.last_base[c]=base;base_changed=true;}}
  if(base_changed){binding.bases_valid=true;BKE_ntree_update_after_single_tree_change(*binding.owner_main,tree);DEG_id_tag_update(&binding.material->id,ID_RECALC_SHADING);}
  // Presentation-only masks share GPU prefixes with material consumers. Instance
  // consumers with channel-dependent masks must recompute their coupled outputs.
  if([&]{for(int c=0;c<17;++c)if(c!=15&&channels[c])return true;return false;}()&&std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return TextureDocument::anchor_dependent(layer);}))for(int c=0;c<17;++c)channels[c]=channels[c]||complete_composite_channels[c];
  const auto &layers=evaluation_layers(set);const auto selected_mask=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==set.selected_layer;});const bool has_selected_mask=selected_mask!=layers.end()&&bool(selected_mask->mask);
  // A mask image can outlive its selection. Do not carry that presentation job
  // into a newly selected unmasked layer, or poison its next save as unsupported.
  if(!has_selected_mask){binding.edit_channels[15]=false;binding.gpu_stack_active[15]=false;binding.mask_async_layer={};
    if(auto job=impl_->gpu_stack_queued.find(set.material_session_uid);job!=impl_->gpu_stack_queued.end()){job->second.channels[15]=false;if(std::none_of(job->second.channels.begin(),job->second.channels.end(),[](bool value){return value;}))impl_->gpu_stack_queued.erase(job);}
    if(auto failed=impl_->failed_channels.find(set.material_session_uid);failed!=impl_->failed_channels.end()){failed->second[15]=false;if(std::none_of(failed->second.begin(),failed->second.end(),[](bool value){return value;})){impl_->failed_channels.erase(failed);impl_->failures.erase(set.material_session_uid);}}
  }
  // Material-view edits can change an upstream content anchor while its old
  // mask inspection image is unbound. Its layer ID alone does not establish
  // freshness: reselecting that same consumer must queue the latest prefix.
  if(!channels[15])for(int c=0;c<17;++c)if(c!=15&&channels[c]){binding.mask_async_layer={};break;}
  channels[15]=(channels[15]||set.selected_mask||binding.displayed_channel==int(InspectionMode::Mask))&&has_selected_mask&&binding.mask_image&&binding.mask_texture;
  if(channels[15])binding.mask_async_layer=set.selected_layer;
  bool any=false;for(int c=0;c<17;++c){if(channels[c]&&composite_image(binding,c)&&composite_texture(binding,c))any=true;else channels[c]=false;}
  if(!any)return true;
  impl_->failed_channels.erase(set.material_session_uid);impl_->failures.erase(set.material_session_uid);
  for(int c=0;c<17;++c)binding.edit_channels[c]=binding.edit_channels[c]||channels[c];
  const auto uid=set.material_session_uid;
  // Keep the full-size GPU images bound. The newest revision replaces queued work.
  if(impl_->pending&&impl_->pending->result.uid==uid)impl_->pending.reset();
  const auto generation=++impl_->generation[uid];impl_->queued.erase(uid);
  if(impl_->job.valid()&&impl_->running_uid==uid&&impl_->cancel)impl_->cancel->store(true);
  Impl::Request request{set,binding.padding,generation,bool(binding.ao_image),binding.edit_channels,true};request.trace=property_current;request.trace.invalidation_ms=property_ms(invalidation_start);request.finalize=finalize;
  binding.gpu_uniform_values={};
  // Uniform layers use the same GPU stack as textured layers. Evaluating even
  // a single CPU pixel here bypassed its cache/dependency rules and duplicated
  // blend logic on every property event.
  impl_->gpu_queued.erase(uid);
  auto gpu_request=request;gpu_request.channels={};bool stack=false;for(int c=0;c<17;++c)if(binding.edit_channels[c]){binding.gpu_unbound_waiting[c]=false;binding.gpu_stack_active[c]=false;if(!(c<5&&binding.gpu_uniform_values[c])&&GpuLayerPreview::supported(set,c)){gpu_request.channels[c]=true;stack=true;}}
  // Any logged unsupported fallback also stages at document resolution.
  auto final_request=request;for(int c=0;c<17;++c)if(gpu_request.channels[c])final_request.channels[c]=false;
  if(stack){if(performance::enabled()&&impl_->gpu_stack_queued.contains(uid))performance::record({performance::flow(),"gpu_stack_replace",0,0,0,true});impl_->gpu_stack_queued.insert_or_assign(uid,std::move(gpu_request));}else impl_->gpu_stack_queued.erase(uid);
  property_pipeline_pending=true;if(std::any_of(final_request.channels.begin(),final_request.channels.end(),[](bool on){return on;})){
    pending_composite_error="GPU layer operation is unsupported; retaining the previous image (CPU fallback disabled)";
    impl_->failures[uid]=pending_composite_error;impl_->failed_channels[uid]=final_request.channels;
    std::fprintf(stderr,"BTS_GPU_STACK_ERROR uid=%u reason=unsupported-operator-or-inspection cpu_fallback=0\n",uid);
  }
  return true;
}
QJsonObject MaterialInspection::native_world_normal_status(std::uint32_t uid)const{
 const auto at=impl_->bindings.find(uid);if(at==impl_->bindings.end())return {{"enabled",false},{"full_targets",false},{"pending",false}};
 const auto &b=at->second;const auto &library=impl_->gpu_preview.native_world_sources();const auto stats=library.statistics();const bool enabled=b.world_error.empty()&&!b.world_layers_ready.empty()&&b.world_tile==b.world_tiles.size();
 QJsonObject result{{"route","layered-tangent-normal"},{"normal_layer_tiles",qint64(b.world_layers_ready.size())},{"material_uid",QString::number(uid)},{"enabled",enabled},{"full_targets",b.world_images_full},{"error",QString::fromStdString(b.world_error)},{"geometry_revision",QString::number(b.world_geometry_revision)},{"publication_generation",QString::number(b.world_publication_generation)},{"pending",bool(b.world_geometry_job)||(b.world_set&&b.world_tile<b.world_tiles.size())},{"completed_tiles",qint64(b.world_tile)},{"target_tiles",qint64(b.world_tiles.size())},{"rendered_tiles",QString::number(b.world_rendered_tiles)},{"programs",qint64(stats.world_programs)},{"dispatches",QString::number(stats.world_dispatches)},{"cycles_entry_epoch",QString::number(impl_->cycles.entry_epoch())},{"cycles_pair_ready",b.world_normal_images[0]&&b.world_normal_images[1]&&impl_->cycles.image_ready(b.world_normal_images[0]->id.session_uid)&&impl_->cycles.image_ready(b.world_normal_images[1]->id.session_uid)}};
 int private_images=0,private_cpu_buffers=0;QJsonArray handles,mappings;for(auto *image:b.world_normal_images){private_images+=image!=nullptr;private_cpu_buffers+=image&&BKE_image_has_loaded_ibuf(image);blender::gpu::Texture *texture=nullptr;if(image){const auto target=BKE_image_gpu_override_texture(image);if(target.texture)texture=*target.texture;}handles.append(QString::number(quintptr(texture),16));mappings.append(QString::number(quintptr(image&&image->runtime?image->runtime->gputexture[TEXTARGET_TILE_MAPPING][0]:nullptr),16));}result["private_images"]=private_images;result["private_cpu_buffers"]=private_cpu_buffers;result["gpu_handles"]=handles;result["gpu_tile_maps"]=mappings;
 if(b.mask_snapshot){result["artist_normal_enabled"]=b.mask_snapshot->channels[3];if(!b.mask_snapshot->selected_layer.isNull()){const auto &layers=evaluation_layers(*b.mask_snapshot);const auto selected=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==b.mask_snapshot->selected_layer;});if(selected!=layers.end()){result["layer_bump_enabled"]=selected->channels[4];if(selected->fill.native_material)result["source_policy"]=native_height_policy_name(selected->fill.native_material->height_policy);}}}
 const auto signature=QByteArray::fromStdString(b.world_published_source_signature);result["published_source_signature"]=QString::fromLatin1(QCryptographicHash::hash(signature,QCryptographicHash::Sha256).toHex());
 if(b.world_geometry){const auto &g=b.world_geometry_diagnostics;result["vertex_uploads"]=qint64(g.vertex_uploads);result["index_uploads"]=qint64(g.index_uploads);result["object_uploads"]=qint64(g.object_uploads);}
 return result;
}
QJsonArray MaterialInspection::diagnostic_native_world_normal_samples(std::uint32_t uid,bool edge)const{
 if(!qEnvironmentVariableIsSet("BTS_NATIVE_WORLD_NORMAL_UI"))throw std::logic_error("Native world Normal probes require the explicit diagnostic gate");
 const auto at=impl_->bindings.find(uid);if(at==impl_->bindings.end()||!at->second.mask_snapshot)return {};const auto &b=at->second;const auto &set=*b.mask_snapshot;QJsonArray samples;
 for(int tile:set.tiles){const std::array<std::array<int,2>,2> points=edge?std::array<std::array<int,2>,2>{{{std::max(0,set.width(tile)/8-1),set.rows(tile)/2},{set.width(tile)/8,set.rows(tile)/2}}}:std::array<std::array<int,2>,2>{{{set.width(tile)/4,set.rows(tile)/2},{set.width(tile)/2,set.rows(tile)/2}}};std::array<std::vector<std::array<float,4>>,2> sides;
 if(b.world_images_full)for(int side=0;side<2;++side){ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(b.world_normal_images[side],tile,region))throw std::runtime_error("Native world probe tile missing");const auto target=BKE_image_gpu_override_texture(b.world_normal_images[side]);if(!target.texture||!*target.texture)throw std::runtime_error("Native world probe target missing");std::vector<std::array<int,3>> positions;for(auto p:points)positions.push_back({p[0]+region.x,p[1]+region.y,region.layer});sides[side]=probe_gpu_texture(*target.texture,positions);}
 const auto bump=diagnostic_gpu_pixels(uid,4,points,tile);for(std::size_t i=0;i<points.size();++i){QJsonObject row{{"tile",tile},{"x",points[i][0]},{"y",points[i][1]}};for(int side=0;side<2;++side){QJsonArray values;if(!sides[side].empty())for(float v:sides[side][i])values.append(v);row[side?"back":"front"]=values;}QJsonArray scalar;for(float v:bump[i])scalar.append(v);row["bump"]=scalar;samples.append(row);}}
 return samples;
}
QJsonObject MaterialInspection::diagnostic_native_source_queues()const{
 const auto flags=[](const CompositeChannels &channels){QJsonArray values;for(bool value:channels)values.append(value);return values;};
 const auto values=[](const auto &uniform){QJsonArray result;for(const auto &value:uniform){if(!value){result.append(QJsonValue());continue;}QJsonArray rgba;for(float v:*value)rgba.append(v);result.append(rgba);}return result;};
 const auto snapshot=[](const TextureSet &set){QJsonArray layers;for(const auto &layer:evaluation_layers(set)){QJsonObject row{{"id",layer.id.toString()},{"source",QString::fromStdString(layer.fill.material_resource)}};if(layer.fill.native_material)row["native"]=native_material_description_json(*layer.fill.native_material);layers.append(row);}return QJsonObject{{"set",set.id.toString()},{"selected_layer",set.selected_layer.toString()},{"layers",layers}};};
 const auto requests=[&](const auto &queue){QJsonObject result;for(const auto &[uid,request]:queue)result[QString::number(uid)]=QJsonObject{{"generation",QString::number(request.generation)},{"channels",flags(request.channels)},{"snapshot",snapshot(request.set)}};return result;};
 QJsonObject generations,failures,failed,uniform,bindings;
 for(const auto &[uid,generation]:impl_->generation)generations[QString::number(uid)]=QString::number(generation);
 for(const auto &[uid,error]:impl_->failures)failures[QString::number(uid)]=QString::fromStdString(error);
 for(const auto &[uid,channels]:impl_->failed_channels)failed[QString::number(uid)]=flags(channels);
 for(const auto &[uid,request]:impl_->gpu_queued)uniform[QString::number(uid)]=QJsonObject{{"generation",QString::number(request.generation)},{"values",values(request.values)}};
 for(auto &[uid,binding]:impl_->bindings){QJsonArray handles;for(int c=0;c<17;++c){auto *image=composite_image(binding,c);QJsonArray native{QString::number(quintptr(image),16)};for(int target:{TEXTARGET_2D,TEXTARGET_2D_ARRAY})native.append(QString::number(quintptr(image&&image->runtime?image->runtime->gputexture[target][0]:nullptr),16));handles.append(native);}QJsonObject state{{"handles",handles},{"edit",flags(binding.edit_channels)},{"active",flags(binding.gpu_stack_active)},{"waiting",flags(binding.gpu_unbound_waiting)},{"mask_async",binding.mask_async_layer.toString()},{"uniform",values(binding.gpu_uniform_values)},{"snapshot_generation",QString::number(binding.gpu_snapshot_generation)}};state["native_world_error"]=QString::fromStdString(binding.world_error);state["native_world_tile"]=qint64(binding.world_tile);state["native_world_geometry_revision"]=QString::number(binding.world_geometry_revision);state["native_world_view_generation"]=QString::number(binding.world_context.view.generation());if(binding.mask_snapshot)state["mask_snapshot"]=snapshot(*binding.mask_snapshot);if(binding.gpu_stack_set)state["gpu_snapshot"]=snapshot(*binding.gpu_stack_set);bindings[QString::number(uid)]=state;}
 QJsonObject result{{"generation",generations},{"queued",requests(impl_->queued)},{"gpu_queued",uniform},{"stack_queued",requests(impl_->gpu_stack_queued)},{"failures",failures},{"failed_channels",failed},{"bindings",bindings},{"pending_cpu",impl_->pending?qint64(impl_->pending->result.uid):-1},{"running_cpu",impl_->job.valid()?qint64(impl_->running_uid):-1},{"cpu_cancel",impl_->cancel?impl_->cancel->load():false}};
 if(std::getenv("BTS_BM_ADJUSTMENT_DEMO"))result["bm_cycles_mirrors"]=impl_->cycles.diagnostic_publication_packets();
 if(std::getenv("BTS_BK_FOLDER_MASK"))result["bk_cycles_mirrors"]=impl_->cycles.diagnostic_publication_packets();
 if(std::getenv("BTS_BJ_PROJECTION_UI")){const auto native=impl_->gpu_preview.native_statistics();result["bj_native_gpu"]=QJsonObject{{"sources",qint64(native[0])},{"programs",qint64(native[1])},{"dispatches",qint64(native[2])}};result["bj_cycles_mirrors"]=impl_->cycles.diagnostic_publication_packets();}return result;
}
NativeSourceEditBatch MaterialInspection::prepare_native_source_edits(std::span<const NativeSourceEdit> edits){
 // Admit every candidate before even constructing a queue transaction.
 for(const auto &edit:edits){if(!edit.before||!edit.after)throw std::invalid_argument("Native source queue snapshot is missing");validate_native_mapping_edit(*edit.before,*edit.after);}
 PaintMetadataScope metadata;
 struct Patch {
  Binding *binding;std::optional<TextureSet> mask_snapshot;QUuid mask_async_layer;
  CompositeChannels edit_channels,gpu_stack_active,gpu_unbound_waiting;
  std::array<std::optional<std::array<float,4>>,5> gpu_uniform_values{};
 };
 struct Plan {
  decltype(Impl::generation) generation;decltype(Impl::queued) queued;
  decltype(Impl::gpu_queued) gpu_queued;decltype(Impl::gpu_stack_queued) gpu_stack_queued;
  decltype(Impl::failures) failures;decltype(Impl::failed_channels) failed_channels;
  std::vector<Patch> patches;
 };
 std::uint64_t bytes=resource_product(edits.size(),sizeof(Patch)+2*sizeof(Impl::Request));
 for(const auto &[uid,request]:impl_->gpu_stack_queued)bytes=resource_sum(bytes,resource_sum(sizeof(Impl::Request),resource_product(request.set.layers.size(),sizeof(TextureLayer))));
 bytes=resource_sum(bytes,resource_product(resource_sum(impl_->generation.size(),resource_sum(impl_->queued.size(),resource_sum(impl_->gpu_queued.size(),resource_sum(impl_->failures.size(),impl_->failed_channels.size())))),sizeof(Impl::Request)));

 auto plan=std::make_shared<Plan>();plan->generation=impl_->generation;plan->queued=impl_->queued;plan->gpu_queued=impl_->gpu_queued;plan->gpu_stack_queued=impl_->gpu_stack_queued;plan->failures=impl_->failures;plan->failed_channels=impl_->failed_channels;plan->patches.reserve(edits.size());
 std::set<std::uint32_t> seen;
 for(const auto &edit:edits){
  if(!edit.before||!edit.after)throw std::invalid_argument("Native source queue snapshot is missing");
  const auto &before=*edit.before;const auto &set=*edit.after;const auto uid=set.material_session_uid;
  if(uid!=before.material_session_uid||set.id!=before.id||!seen.insert(uid).second)throw std::invalid_argument("Native source queue identity changed");
  if(set.disabled||!set.uniform_editable)continue;
  const auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end()||!found->second.resolution)throw std::runtime_error("Native image replacement requires an initialized material preview");
  auto &binding=found->second;
  if(impl_->queued.contains(uid)||(impl_->pending&&impl_->pending->result.uid==uid)||(impl_->job.valid()&&impl_->running_uid==uid))throw std::runtime_error("Wait for the previous material preview before replacing its image");
  std::map<int,std::pair<int,int>> sizes;for(int tile:set.tiles)sizes[tile]={set.width(tile),set.rows(tile)};
  if(sizes!=binding.tile_sizes||set.precision!=before.precision||set.base_values!=before.base_values||set.channels!=before.channels||set.shader_parameters!=before.shader_parameters||set.shader_parameters!=binding.shader_parameters||set.displacement!=before.displacement||set.displacement!=binding.displacement_settings||set.normal_format!=before.normal_format||set.ao_channel!=before.ao_channel)throw std::runtime_error("Native image replacement requires unchanged ready material targets and shader settings");
  const auto same_layering=[](const MaterialLayering &a,const MaterialLayering &b){
   if(a.enabled!=b.enabled||a.mask_slots!=b.mask_slots||a.mask_roots!=b.mask_roots||a.active_mask!=b.active_mask||a.main_selection!=b.main_selection||a.mask_selections!=b.mask_selections)return false;
   for(int i=0;i<4;++i){const auto &x=a.materials[i],&y=b.materials[i];if(x.name!=y.name||x.resource!=y.resource||x.values!=y.values||x.textures!=y.textures||x.tiling!=y.tiling||x.offset!=y.offset||x.rotation!=y.rotation)return false;}return true;
  };
  if(!same_layering(before.material_layering,set.material_layering)||!same_layering(binding.material_layering,set.material_layering)||binding.layering_directx!=(set.normal_format==NormalMapFormat::DirectX))throw std::runtime_error("Native image replacement is waiting for its layered shader settings");
  const auto check_precision=[](Image *image,ChannelPrecision precision){if(image&&native_gpu_image(image)&&bool(image->flag&IMA_HIGH_BITDEPTH)!=(precision==ChannelPrecision::L32F||precision==ChannelPrecision::RGB32F))throw std::runtime_error("Native image replacement is waiting for target precision");};
  for(int c=0;c<5;++c){if(!binding.bases_valid||binding.last_base[c]!=encoded_material_base(set,c))throw std::runtime_error("Native image replacement is waiting for its material base");check_precision(binding.images[c],set.precision[c]);}
  check_precision(binding.authored_normal_image,set.precision[3]);check_precision(binding.ao_image,set.precision[5]);check_precision(binding.displacement_image,set.displacement.precision);
  for(int c=0;c<custom_channel_count;++c){if(set.users[c].present!=binding.user_present[c]||set.users[c].present!=before.users[c].present||set.users[c].allocated!=before.users[c].allocated||set.users[c].precision!=before.users[c].precision)throw std::runtime_error("Native image replacement is waiting for custom channel targets");check_precision(binding.user_images[c],set.users[c].precision);}
  // Adopt only geometry already prepared by normal rendering. No workers or
  // native image/node allocation are started by this transaction.
  TextureSet snapshot=set;snapshot.geometry_ownership=binding.ownership_cache;snapshot.geometry_names=binding.geometry_names;snapshot.projection_tiles=binding.projection_cache;if(!binding.projection_cache.empty())snapshot.projection_geometry=binding.projection_cache.begin()->second;
  auto channels=edit.channels;if(const auto failed=plan->failed_channels.find(uid);failed!=plan->failed_channels.end())for(int c=0;c<17;++c)channels[c]=channels[c]||failed->second[c];
  if(std::any_of(evaluation_layers(snapshot).begin(),evaluation_layers(snapshot).end(),[](const auto &layer){return TextureDocument::anchor_dependent(layer);}))for(int c=0;c<17;++c)channels[c]=channels[c]||complete_composite_channels[c];
  Patch patch{&binding,snapshot,binding.mask_async_layer,binding.edit_channels,binding.gpu_stack_active,binding.gpu_unbound_waiting};
  const auto &layers=evaluation_layers(snapshot);const auto selected=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==snapshot.selected_layer;});const bool has_mask=selected!=layers.end()&&bool(selected->mask);
  if(!has_mask){patch.edit_channels[15]=patch.gpu_stack_active[15]=false;patch.mask_async_layer={};}
  if(!channels[15])for(int c=0;c<17;++c)if(c!=15&&channels[c]){patch.mask_async_layer={};break;}
  channels[15]=(channels[15]||snapshot.selected_mask||binding.displayed_channel==int(InspectionMode::Mask))&&has_mask&&binding.mask_image&&binding.mask_texture;if(channels[15])patch.mask_async_layer=snapshot.selected_layer;
  bool any=false;for(int c=0;c<17;++c){
   const bool required=c<5||c==6||(c==5&&(set.ao_channel||set.mesh_maps[3]))||(c>=7&&c<15&&(set.users[c-7].allocated||set.users[c-7].present))||(c==15&&has_mask&&(set.selected_mask||binding.displayed_channel==int(InspectionMode::Mask)))||(c==16&&set.displacement.allocated);
   const bool target=composite_image(binding,c)&&composite_texture(binding,c);
   if(channels[c]&&required&&!target)throw std::runtime_error("Native image replacement is waiting for a required material channel target");
   if(channels[c]&&target)any=true;else channels[c]=false;patch.edit_channels[c]=patch.edit_channels[c]||channels[c];
  }
  if(!any)continue;
  auto &generation=plan->generation[uid];if(generation==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Native image queue generation exhausted");++generation;
  Impl::Request request{snapshot,binding.padding,generation,bool(binding.ao_image),{},true};request.trace=property_current;
  bool stack=false;for(int c=0;c<17;++c)if(patch.edit_channels[c]){
   if(!composite_image(binding,c)||!composite_texture(binding,c)||!GpuLayerPreview::supported(snapshot,c))throw std::runtime_error("Native image replacement has an unsupported active material channel");
   request.channels[c]=true;stack=true;patch.gpu_stack_active[c]=patch.gpu_unbound_waiting[c]=false;
  }
  plan->queued.erase(uid);plan->gpu_queued.erase(uid);plan->failures.erase(uid);plan->failed_channels.erase(uid);
  if(stack)plan->gpu_stack_queued.insert_or_assign(uid,std::move(request));else plan->gpu_stack_queued.erase(uid);
  plan->patches.push_back(std::move(patch));
 }
 static_assert(std::is_nothrow_swappable_v<decltype(Plan::generation)>);
 static_assert(std::is_nothrow_swappable_v<decltype(Plan::gpu_stack_queued)>);
 static_assert(std::is_nothrow_swappable_v<std::optional<TextureSet>>);
 return NativeSourceEditBatch([this,plan]()noexcept{
  impl_->generation.swap(plan->generation);impl_->queued.swap(plan->queued);impl_->gpu_queued.swap(plan->gpu_queued);impl_->gpu_stack_queued.swap(plan->gpu_stack_queued);impl_->failures.swap(plan->failures);impl_->failed_channels.swap(plan->failed_channels);
  for(auto &patch:plan->patches){auto &binding=*patch.binding;binding.mask_snapshot.swap(patch.mask_snapshot);std::swap(binding.mask_async_layer,patch.mask_async_layer);binding.edit_channels.swap(patch.edit_channels);binding.gpu_stack_active.swap(patch.gpu_stack_active);binding.gpu_unbound_waiting.swap(patch.gpu_unbound_waiting);binding.gpu_uniform_values.swap(patch.gpu_uniform_values);}
  if(!plan->patches.empty())property_pipeline_pending=true;
 });
}
bool MaterialInspection::take_displacement_updates(){return std::exchange(impl_->gpu_displacement_changed,false);}
void MaterialInspection::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit){impl_->gpu_preview.diagnostic_adjustment_budget(limit);}
std::shared_ptr<const AdjustmentPaintSnapshot> MaterialInspection::begin_adjustment_source(const TextureSet &source,const QUuid &layer,const QUuid &document){
 const auto found=impl_->bindings.find(source.material_session_uid);if(found==impl_->bindings.end())throw std::runtime_error("Adjustment target has no prepared material binding");const auto &binding=found->second;auto set=source;set.projection_tiles=binding.projection_cache;set.geometry_ownership=binding.ownership_cache;set.geometry_names=binding.geometry_names;if(!binding.projection_cache.empty())set.projection_geometry=binding.projection_cache.begin()->second;return impl_->gpu_preview.begin_adjustment_source(set,layer,document);
}
TextureSet MaterialInspection::prepare_layer_capture(const TextureSet &set)const{return impl_->gpu_preview.prepare_layer_capture(set);}
std::uint64_t MaterialInspection::request_layer_capture(const TextureSet &set,const LayerCaptureRequest &request){const auto ticket=++impl_->layer_capture_ticket;impl_->layer_capture_failure.reset();impl_->layer_capture_job=Impl::LayerCaptureJob{set,request,ticket};return ticket;}
std::optional<LayerCaptureResult> MaterialInspection::layer_capture_result()const{if(impl_->layer_capture_failure)return impl_->layer_capture_failure;return impl_->gpu_preview.layer_capture_result();}
std::uint64_t MaterialInspection::request_thumbnail(const TextureSet &set,const ThumbnailRequest &request){const auto ticket=++impl_->thumbnail_ticket;impl_->thumbnail_failure.reset();impl_->thumbnail_job=Impl::ThumbnailJob{set,request,ticket};return ticket;}
std::optional<ThumbnailMetadata> MaterialInspection::thumbnail_result()const{if(impl_->thumbnail_failure)return impl_->thumbnail_failure;return impl_->gpu_preview.thumbnail_result();}
std::uint64_t MaterialInspection::request_material_sample(const TextureSet &set,const MaterialSampleRequest &request){const auto ticket=++impl_->material_sample_ticket;impl_->material_sample_failure.reset();impl_->material_sample_job=Impl::MaterialSampleJob{set,request,ticket};return ticket;}
std::optional<MaterialSample> MaterialInspection::material_sample_result()const{if(impl_->material_sample_failure)return impl_->material_sample_failure;return impl_->gpu_preview.material_sample_result();}
std::uint64_t MaterialInspection::request_histogram(const TextureSet &set,const HistogramRequest &request){const auto ticket=++impl_->histogram_ticket;impl_->histogram_failure.reset();impl_->histogram_job=Impl::HistogramJob{set,request,ticket};return ticket;}
std::optional<HistogramMetadata> MaterialInspection::histogram_result(const QUuid &id)const{if(impl_->histogram_failure&&impl_->histogram_failure->first==id)return impl_->histogram_failure->second;return impl_->gpu_preview.histogram_result(id);}
bool MaterialInspection::prepare_texture_resource(const std::shared_ptr<const FillTexture> &texture){return prepare_texture_resources({texture});}
bool MaterialInspection::prepare_texture_resources(const std::vector<std::shared_ptr<const FillTexture>> &textures){
 if(!impl_->texture_preparation_owner.isNull())return false;
 if(textures.empty()||std::any_of(textures.begin(),textures.end(),[](const auto &texture){return !texture;}))throw std::invalid_argument("Cannot prepare an empty texture input group");
 if(impl_->preparing_textures!=textures){impl_->preparing_textures=textures;impl_->texture_upload_error.clear();}
 if(!impl_->texture_upload_error.empty())throw std::runtime_error(impl_->texture_upload_error);
 impl_->texture_upload_waiting=!std::all_of(textures.begin(),textures.end(),[&](const auto &texture){return impl_->gpu_preview.texture_resource_ready(*texture);});return !impl_->texture_upload_waiting;
}
void MaterialInspection::retire_layer_channel(const TextureDocument &document,const TextureSet &set,const TextureLayer &previous,int channel){
 const std::vector<std::shared_ptr<const FillTexture>> empty;impl_->gpu_preview.retire_layer_channel(document,set,previous,channel,impl_->texture_upload_waiting?impl_->preparing_textures:empty);
}
void MaterialInspection::discard_binding(std::uint32_t uid){impl_->invalidate(uid);if(auto found=impl_->bindings.find(uid);found!=impl_->bindings.end()){if(found->second.margin_job)found->second.margin_job->cancelled=true;if(found->second.projection_job)found->second.projection_job->cancelled=true;if(found->second.world_geometry_job)found->second.world_geometry_job->cancelled=true;impl_->bindings.erase(found);}++impl_->generation[uid];}
void MaterialInspection::cancel_texture_preparation(){if(!impl_->texture_preparation_owner.isNull())return;impl_->preparing_textures.clear();impl_->texture_upload_waiting=false;impl_->texture_upload_error.clear();}
bool MaterialInspection::claim_texture_preparation(const QUuid &owner){
 if(owner.isNull())throw std::invalid_argument("Texture preparation requires a nonempty owner token");
 if(!impl_->texture_preparation_owner.isNull())return impl_->texture_preparation_owner==owner;
 if(impl_->texture_upload_waiting||!impl_->texture_upload_error.empty())return false;
 // Legacy callers retain their ready vector after successful uploads. Clearing
 // only this completed bookkeeping does not evict any resident GPU resource.
 impl_->preparing_textures.clear();impl_->texture_preparation_owner=owner;return true;
}
bool MaterialInspection::prepare_texture_resources(const QUuid &owner,const std::vector<std::shared_ptr<const FillTexture>> &textures){
 if(owner.isNull()||impl_->texture_preparation_owner!=owner)throw std::logic_error("Texture preparation owner changed");
 if(textures.empty()||std::any_of(textures.begin(),textures.end(),[](const auto &texture){return !texture;}))throw std::invalid_argument("Cannot prepare an empty texture input group");
 if(impl_->preparing_textures!=textures){impl_->preparing_textures=textures;impl_->texture_upload_error.clear();}
 if(!impl_->texture_upload_error.empty())throw std::runtime_error(impl_->texture_upload_error);
 impl_->texture_upload_waiting=!std::all_of(textures.begin(),textures.end(),[&](const auto &texture){return impl_->gpu_preview.texture_resource_ready(*texture);});return !impl_->texture_upload_waiting;
}
bool MaterialInspection::cancel_texture_preparation(const QUuid &owner){
 if(owner.isNull()||impl_->texture_preparation_owner!=owner)return false;
 impl_->texture_preparation_owner={};impl_->preparing_textures.clear();impl_->texture_upload_waiting=false;impl_->texture_upload_error.clear();return true;
}

bool MaterialInspection::updates_pending(bool include_preparation)const{return impl_->gpu_preview.programs_pending()||impl_->cycles.pending()||impl_->layering_sources_pending||(include_preparation&&(impl_->gpu_preview.native_preparations_pending()||impl_->texture_upload_waiting))||bool(impl_->layer_capture_job)||bool(impl_->thumbnail_job)||bool(impl_->material_sample_job)||bool(impl_->histogram_job)|| impl_->gpu_preview.metadata_pending()||impl_->cpu_pending()||impl_->gpu_mips_pending||!impl_->gpu_queued.empty()||!impl_->gpu_stack_queued.empty()||std::any_of(impl_->bindings.begin(),impl_->bindings.end(),[](const auto &entry){return entry.second.world_view_dirty||entry.second.world_objects_dirty||bool(entry.second.margin_job)||bool(entry.second.projection_job)||NativeWorldNormalContextRoute::producer_pending(entry.second.world_set.has_value(),bool(entry.second.world_geometry_job))||(entry.second.world_set&&entry.second.world_tile<entry.second.world_tiles.size());});}
void MaterialInspection::resume_gpu_channels(){
 // Called on the GPU owner after drawing: a view switch may have caused EEVEE
 // to bind a previously invisible channel for the first time. Keep its latest
 // authored snapshot and resume GPU work; never build a hidden CPU composite.
 for(auto &[uid,binding]:impl_->bindings){
  if(!binding.gpu_stack_set)continue;
  for(int c=0;c<17;++c)if(binding.gpu_unbound_waiting[c]){
   auto *image=composite_image(binding,c);if(!image)continue;const auto native=BKE_image_gpu_override_texture(image);if(!native.texture||!*native.texture)continue;
   auto found=impl_->gpu_stack_queued.find(uid);
   if(found==impl_->gpu_stack_queued.end()){
    Impl::Request request{*binding.gpu_stack_set,binding.padding,impl_->generation[uid],bool(binding.ao_image)};request.channels={};request.layer_edit=true;request.finalize=false;
    found=impl_->gpu_stack_queued.emplace(uid,std::move(request)).first;
   }
   found->second.channels[c]=true;binding.gpu_unbound_waiting[c]=false;
   std::printf("BTS_GPU_CHANNEL_RESUME channel=%d cpu_composite=0\n",c);
  }
 }
}
void MaterialInspection::validate_material_layer_sources(const TextureDocument &document,bool require_ready)const{
 for(const auto &set:document.texture_sets())if(set.material_layering.enabled){
  bool sourced=false;for(const auto &input:set.material_layering.materials)for(const auto &source:input.textures)sourced|=bool(source);
  if(!sourced)continue;
  if(set.disabled||!set.uniform_editable)throw std::invalid_argument("Packed reusable sources require an enabled editable Texture Set");
  const auto found=impl_->bindings.find(set.material_session_uid);
  if(found==impl_->bindings.end()||!found->second.owner_main||!found->second.material||!found->second.resolution)throw std::invalid_argument("Packed reusable source Texture Set has no prepared native channel binding");
  const auto &model=found->second.material_layering;
  if(!model.enabled)throw std::invalid_argument("Packed reusable source model was not restored");
  for(int input=0;input<4;++input)for(int channel=0;channel<6;++channel)
   if(model.materials[input].textures[channel]!=set.material_layering.materials[input].textures[channel])throw std::invalid_argument("Packed reusable source binding differs from its typed document");
  if(require_ready&&!impl_->layering_sources.images(set.material_layering))throw std::invalid_argument("Packed reusable source GPU aliases are incomplete");
 }
}
bool MaterialInspection::Impl::prepare_layer_sources(bool allow_upload,bool frame_started){
 std::vector<const MaterialLayering *> models;Main *main=nullptr;
 for(const auto &[uid,binding]:this->bindings)if(binding.material_layering.enabled&&!binding.resolution)for(const auto &input:binding.material_layering.materials)for(const auto &source:input.textures)if(source)throw std::invalid_argument("Reusable source maps require prepared native channel targets before source staging");
 for(auto &[uid,binding]:this->bindings)if(binding.resolution||!binding.material_layering.enabled){models.push_back(&binding.material_layering);if(main&&main!=binding.owner_main)throw std::logic_error("Reusable sources span different Main owners");main=binding.owner_main;}
 bool ready=std::all_of(models.begin(),models.end(),[&](const auto *model){return bool(this->layering_sources.images(*model));});
 if(!ready&&main&&allow_upload){if(!frame_started)this->gpu_preview.begin_frame();ready=this->layering_sources.prepare(*main,this->gpu_preview,models);}
 this->layering_sources_pending=!ready;
 if(ready){for(auto &[uid,binding]:this->bindings)if(binding.resolution||!binding.material_layering.enabled){const auto images=this->layering_sources.images(binding.material_layering);binding.layered_shader.apply(*binding.owner_main,*binding.material,binding.material_layering,binding.shader_parameters.normal_intensity,binding.shader_parameters.ao_intensity,binding.layering_directx,*images);}this->layering_sources.retain_only(models);}
 return ready;
}
bool MaterialInspection::prepare_material_layer_sources(bool allow_upload){return impl_->prepare_layer_sources(allow_upload,false);}
void MaterialInspection::upload_staging_images(bool allow_gpu_compute,bool input_active){performance::Scope timing_all("upload_staging_images");impl_->collect_projection_geometry();
  if(allow_gpu_compute&&!input_active)impl_->gpu_preview.step_native_preparations();
  // Disabled private world inputs are neutral placeholders, not completed
  // authored images. The full-resolution layered normal is mirrored through
  // composite channel 3; never register these 1x1 sentinels as Cycles products.
  for(auto &[uid,binding]:impl_->bindings)for(int side=0;side<2;++side)if(auto *image=binding.world_normal_images[side]){const auto target=BKE_image_gpu_override_texture(image);if(!target.texture||!*target.texture){native_world_normal_placeholder(*image);binding.world_images_full=false;if(binding.world_normal_enabled)scalar(*binding.material->nodetree,output(*binding.world_normal_enabled,"Value"),0);}}
  for(auto &[uid,binding]:impl_->bindings){
    const bool busy=input_active||impl_->queued.contains(uid)||impl_->gpu_queued.contains(uid)||impl_->gpu_stack_queued.contains(uid)||(impl_->pending&&impl_->pending->result.uid==uid)||(impl_->job.valid()&&impl_->running_uid==uid);
    for(int c=0;c<17;++c)if(auto *image=composite_image(binding,c))BKE_image_gpu_suspend_mipmaps(image,busy);
  }
  for(auto &[uid,binding]:impl_->bindings){
    for(auto *image:binding.images)prepare_native_gpu_image(image);
    prepare_native_gpu_image(binding.ao_image);prepare_native_gpu_image(binding.displacement_image);
    if(binding.displayed_channel==int(InspectionMode::Normal))prepare_native_gpu_image(binding.authored_normal_image);
    if(binding.displayed_channel==int(InspectionMode::Mask))prepare_native_gpu_image(binding.mask_image);
    for(auto *image:binding.user_images)prepare_native_gpu_image(image);
  }
  // Ordinary frame budgets must reset even when no reusable sources exist.
  impl_->gpu_preview.begin_frame();
  // Isolated Open starts its own frame; this upload has already started one.
  impl_->prepare_layer_sources(allow_gpu_compute&&!input_active,true);
  if(allow_gpu_compute){

  for(auto &[uid,request]:impl_->gpu_queued){
    auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end()||impl_->generation[uid]!=request.generation)continue;auto &binding=found->second;performance::record({performance::flow(),"gpu_uniform_queue",0,0,performance::ms(request.enqueued),true});bool rebound=false;int channels=0;const auto started=PropertyClock::now();performance::Scope timing("gpu_uniform_submit");
    for(int c:{0,1,2,4})if(request.values[c]){
      const auto lookup_start=PropertyClock::now();auto *image=binding.images[c];auto textures=BKE_image_gpu_override_texture(image);auto *texture=textures.texture?*textures.texture:nullptr;const double lookup_ms=property_ms(lookup_start);
      if(texture&&GpuProjectReadback::protect_for_write(textures.texture))texture=*textures.texture;
      if(!texture||(!BKE_image_gpu_residency_limit(image)&&(GPU_texture_width(texture)!=binding.tile_sizes.at(1001).first||GPU_texture_height(texture)!=binding.tile_sizes.at(1001).second))){binding.gpu_uniform_values[c].reset();continue;}
      const auto format=GPU_texture_format(texture);auto value=*request.values[c];
      if(format==gpu::TextureFormat::SFLOAT_16_16_16_16){for(auto &v:value)v=math::half_to_float(math::float_to_half_make_finite(v));}
      else if(format!=gpu::TextureFormat::SFLOAT_32_32_32_32){binding.gpu_uniform_values[c].reset();continue;}
      if(binding.preview_active[c]){image_binding(*binding.owner_main,binding,c,image);binding.preview_active[c]=false;rebound=true;}
      const auto clear_start=PropertyClock::now();if(!impl_->gpu_preview.uniform(texture,value)){binding.gpu_uniform_values[c].reset();continue;}BKE_image_gpu_override_updated(image);impl_->cycles.changed(*image,uid,c);const double clear_ms=property_ms(clear_start);const double mip_ms=0;GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);property_upload_channel(c);++channels;
      if(lookup_ms+clear_ms+mip_ms>8||std::getenv("BTS_GPU_FILL_DETAILS"))std::printf("BTS_GPU_FILL_PHASE channel=%d lookup_ms=%.3f broadcast_ms=%.3f mip_ms=%.3f\n",c,lookup_ms,clear_ms,mip_ms);
      // Test only: production presentation has no GPU-to-CPU readback.
      static std::set<std::pair<std::uint32_t,int>> verified;
      if(std::getenv("BTS_VERIFY_GPU_UNIFORM")&&verified.insert({uid,c}).second){auto *pixels=static_cast<float *>(GPU_texture_read(texture,GPU_DATA_FLOAT,0));const auto count=std::size_t(GPU_texture_width(texture))*GPU_texture_height(texture);bool exact=true;for(std::size_t i=0;i<count&&exact;++i)for(int k=0;k<4;++k)exact&=std::isfinite(pixels[i*4+k])&&std::abs(pixels[i*4+k]-value[k])<=.001f*std::max(1.f,std::abs(value[k]));MEM_delete(pixels);if(!exact)throw std::runtime_error("GPU uniform preview differs from CPU reference");std::printf("BTS_GPU_UNIFORM_VERIFIED channel=%d texels=%zu\n",c,count);}
    }
    if(rebound){auto &tree=*binding.material->nodetree;BKE_ntree_update_after_single_tree_change(*binding.owner_main,tree);DEG_id_tag_update_ex(binding.owner_main,&binding.material->id,ID_RECALC_SHADING);}
    if(channels){auto trace=request.trace;trace.preview=true;property_uploaded(trace,property_ms(started));std::printf("BTS_GPU_FILL channels=%d cpu_submit_ms=%.3f host_upload_bytes=0\n",channels,property_ms(started));}
  }
  impl_->gpu_queued.clear();
  }else if(!impl_->gpu_queued.empty())performance::record({performance::flow(),"gpu_compute_deferred",0,0,0,true});

  if(allow_gpu_compute)for(auto &[uid,binding]:impl_->bindings)if(binding.gpu_inspection_waiting&&binding.displayed_channel==int(InspectionMode::Normal)&&binding.gpu_stack_set){
    if(auto found=impl_->gpu_stack_queued.find(uid);found!=impl_->gpu_stack_queued.end())found->second.channels[6]=true;
    else{Impl::Request request{*binding.gpu_stack_set,binding.padding,impl_->generation[uid],bool(binding.ao_image)};request.channels={};request.channels[6]=true;request.layer_edit=true;request.finalize=false;impl_->gpu_stack_queued.emplace(uid,std::move(request));}
  }
  if(allow_gpu_compute)for(auto i=impl_->gpu_stack_queued.begin();i!=impl_->gpu_stack_queued.end();){
    const auto uid=i->first;auto &request=i->second;auto found=impl_->bindings.find(uid);if(found==impl_->bindings.end()||impl_->generation[uid]!=request.generation){i=impl_->gpu_stack_queued.erase(i);continue;}auto &binding=found->second;bool pending=false,rebound=false,changed=false;const auto started=PropertyClock::now();if(!binding.gpu_stack_set||binding.gpu_snapshot_generation!=request.generation){binding.gpu_stack_set=request.set;binding.gpu_snapshot_generation=request.generation;}
    performance::record({performance::flow(),"gpu_stack_queue",0,0,performance::ms(request.enqueued),true});
    for(int c=0;c<17;++c)if(request.channels[c]){auto textures=BKE_image_gpu_override_texture(composite_image(binding,c));auto *texture=textures.texture?*textures.texture:nullptr;if(texture&&GpuProjectReadback::protect_for_write(textures.texture))texture=*textures.texture;
      // Displacement needs presence metadata before its shader is connected.
      // Allocate its canonical target once, without a CPU composite placeholder.
      if(!texture&&c==16){ImageUser user{};user.tile=*request.set.tiles.begin();textures=BKE_image_get_gpu_material_texture(composite_image(binding,c),&user,true);texture=textures.texture?*textures.texture:nullptr;}
      // Unbound inspection images need no low-resolution replacement and no
      // node-tree retag. When selected, let EEVEE allocate once, then fill it.
      if(!texture&&c==6){if(binding.displayed_channel==int(InspectionMode::Normal)){pending=true;continue;}binding.gpu_stack_active[c]=true;binding.gpu_inspection_waiting=true;binding.edit_channels[c]=false;request.channels[c]=false;continue;}
      if(!texture){binding.gpu_unbound_waiting[c]=true;binding.gpu_stack_active[c]=true;binding.edit_channels[c]=false;request.channels[c]=false;std::printf("BTS_GPU_CHANNEL_DEFER channel=%d reason=not-shader-bound cpu_composite=0\n",c);continue;}
      auto result=GpuPreviewUpdate::Updated;if(c==16)impl_->gpu_preview.set_displacement_epoch(uid,binding.displacement_cpu_epoch);
      if(texture){std::map<int,std::array<int,3>> regions;for(int tile:request.set.tiles){ImageGpuTileRegion region;if(!BKE_image_gpu_override_tile_region(composite_image(binding,c),tile,region)||region.width!=request.set.width(tile)||region.height!=request.set.rows(tile)){result=GpuPreviewUpdate::Unsupported;break;}regions[tile]={region.x,region.y,region.layer};}if(result!=GpuPreviewUpdate::Unsupported){auto active=request.set;if(active.residency_view){active.tiles=active.loaded_tiles();std::erase_if(regions,[&](const auto &entry){return !active.tile_loaded(entry.first);});}if(!active.tiles.empty())result=impl_->gpu_preview.update_tiles(active,c,texture,regions);}}
      if(result==GpuPreviewUpdate::Pending){pending=true;continue;}request.channels[c]=false;if(result!=GpuPreviewUpdate::Updated){
        const auto native_error=impl_->gpu_preview.native_image_error();pending_composite_error=!native_error.empty()?native_error:pending_adjustment_failure?pending_adjustment_failure->message:"GPU layer target allocation failed; retaining the previous image (CPU fallback disabled)";
        impl_->failures[uid]=pending_composite_error;impl_->failed_channels[uid][c]=true;
        std::fprintf(stderr,"BTS_GPU_STACK_ERROR uid=%u channel=%d reason=unsupported-target-or-allocation detail=%s cpu_fallback=0\n",uid,c,pending_composite_error.c_str());
        continue;
      }
      if(impl_->gpu_preview.wrote_image()){BKE_image_gpu_override_updated(composite_image(binding,c));if(cycles_shader_channel(binding,c))impl_->cycles.changed(*composite_image(binding,c),uid,c);}binding.gpu_stack_active[c]=true;binding.edit_channels[c]=false;if(c==6)binding.gpu_inspection_waiting=false;if(binding.preview_active[c]){image_binding(*binding.owner_main,binding,c,composite_image(binding,c));binding.preview_active[c]=false;rebound=true;}if(impl_->gpu_preview.wrote_image()){property_upload_channel(c);changed=true;if(performance::enabled())std::printf("BTS_GPU_CHANNEL_PUBLISH channel=%d generation=%llu queue_ms=%.3f\n",c,(unsigned long long)request.generation,performance::ms(request.enqueued));}
    }
    if(rebound){auto &tree=*binding.material->nodetree;BKE_ntree_update_after_single_tree_change(*binding.owner_main,tree);DEG_id_tag_update_ex(binding.owner_main,&binding.material->id,ID_RECALC_SHADING);}
    if(changed){auto trace=request.trace;trace.preview=true;property_uploaded(trace,property_ms(started));}
    if(pending)++i;else {if(request.finalize){binding.final_generation=request.generation;auto trace=request.trace;trace.preview=false;property_uploaded(trace,0);}i=impl_->gpu_stack_queued.erase(i);}
  }
  if(allow_gpu_compute)for(auto &[uid,binding]:impl_->bindings)impl_->pump_world_normal(binding);
  if(allow_gpu_compute)for(auto &[uid,binding]:impl_->bindings)if(binding.geometry_upload_pending)binding.geometry_upload_pending=!impl_->gpu_preview.prefetch_geometry(binding.projection_cache);
  if(allow_gpu_compute&&impl_->material_sample_job){auto &job=*impl_->material_sample_job;auto found=impl_->bindings.find(job.set.material_session_uid);
    if(found==impl_->bindings.end()){MaterialSample failed;failed.ticket=job.ticket;failed.valid=false;impl_->material_sample_failure=failed;impl_->material_sample_job.reset();}
    else{auto &binding=found->second;job.set.projection_tiles=binding.projection_cache;job.set.geometry_ownership=binding.ownership_cache;job.set.geometry_names=binding.geometry_names;if(!binding.projection_cache.empty())job.set.projection_geometry=binding.projection_cache.begin()->second;
      const auto result=impl_->gpu_preview.sample_material(job.set,job.request,job.ticket);if(result!=GpuPreviewUpdate::Pending){if(result==GpuPreviewUpdate::Unsupported){MaterialSample failed;failed.ticket=job.ticket;failed.valid=false;impl_->material_sample_failure=failed;std::fprintf(stderr,"BTS_GPU_MATERIAL_PICK_FAILED ticket=%llu\n",(unsigned long long)job.ticket);}impl_->material_sample_job.reset();}}
  }
  if(allow_gpu_compute&&impl_->layer_capture_job){auto &job=*impl_->layer_capture_job;auto found=impl_->bindings.find(job.set.material_session_uid);
    if(found==impl_->bindings.end()){impl_->layer_capture_failure=LayerCaptureResult{job.ticket,false,{}};impl_->layer_capture_job.reset();}
    else{auto &binding=found->second;
      if(binding.padding){
        std::map<int,std::pair<int,int>> sizes;for(int tile:job.set.tiles)sizes[tile]={job.set.width(tile),job.set.rows(tile)};
        if(binding.tile_sizes.empty())binding.tile_sizes=sizes;
        if(!binding.margin_job&&!binding.margins_ready)binding.margin_job=MarginGeometryJob::start(binding.padding,sizes);
        bool complete=binding.ownership_cache.size()==sizes.size();for(const auto &[tile,size]:sizes){const auto it=binding.projection_cache.find(tile);complete&=it!=binding.projection_cache.end()&&it->second->resolution==size.first&&it->second->height==size.second;}
        if(!complete&&!binding.projection_job)binding.projection_job=ProjectionGeometryJob::start(binding.padding,sizes);
      }
      if(!binding.projection_cache.empty()){job.set.projection_tiles=binding.projection_cache;job.set.projection_geometry=binding.projection_cache.begin()->second;}
      if(!binding.ownership_cache.empty())job.set.geometry_ownership=binding.ownership_cache;
      if(!binding.geometry_names.empty())job.set.geometry_names=binding.geometry_names;
      const auto result=impl_->gpu_preview.capture_layer(job.set,job.request,job.ticket);if(result!=GpuPreviewUpdate::Pending){if(result==GpuPreviewUpdate::Unsupported){impl_->layer_capture_failure=LayerCaptureResult{job.ticket,false,{}};std::fprintf(stderr,"BTS_GPU_LAYER_CAPTURE_FAILED layer=%s\n",job.request.layer.toString().toStdString().c_str());}impl_->layer_capture_job.reset();}}
  }
  if(allow_gpu_compute&&impl_->thumbnail_job){auto &job=*impl_->thumbnail_job;auto found=impl_->bindings.find(job.set.material_session_uid);
    if(found==impl_->bindings.end()){ThumbnailMetadata failed;failed.ticket=job.ticket;failed.valid=false;impl_->thumbnail_failure=failed;impl_->thumbnail_job.reset();}
    else{auto &binding=found->second;job.set.projection_tiles=binding.projection_cache;job.set.geometry_ownership=binding.ownership_cache;job.set.geometry_names=binding.geometry_names;if(!binding.projection_cache.empty())job.set.projection_geometry=binding.projection_cache.begin()->second;
      const auto result=impl_->gpu_preview.thumbnail(job.set,job.request,job.ticket);if(result!=GpuPreviewUpdate::Pending){if(result==GpuPreviewUpdate::Unsupported){ThumbnailMetadata failed;failed.ticket=job.ticket;failed.valid=false;impl_->thumbnail_failure=failed;std::fprintf(stderr,"BTS_GPU_THUMBNAIL_FAILED layer=%s\n",job.request.layer.toString().toStdString().c_str());}impl_->thumbnail_job.reset();}}
  }
  if(allow_gpu_compute&&impl_->histogram_job){auto &job=*impl_->histogram_job;auto found=impl_->bindings.find(job.set.material_session_uid);
    if(found==impl_->bindings.end())impl_->histogram_job.reset();else{auto &binding=found->second;job.set.projection_tiles=binding.projection_cache;job.set.geometry_ownership=binding.ownership_cache;job.set.geometry_names=binding.geometry_names;if(!binding.projection_cache.empty())job.set.projection_geometry=binding.projection_cache.begin()->second;
      const auto result=impl_->gpu_preview.histogram(job.set,job.request,job.ticket);if(result!=GpuPreviewUpdate::Pending){if(result==GpuPreviewUpdate::Unsupported){HistogramMetadata failed;failed.ticket=job.ticket;failed.valid=false;impl_->histogram_failure=std::pair{job.request.effect,failed};std::fprintf(stderr,"BTS_GPU_HISTOGRAM_FAILED effect=%s\n",job.request.effect.toString().toStdString().c_str());}impl_->histogram_job.reset();}}
  }
  // Interactive edits and diagnostics keep priority over a not-yet-assigned resource.
  if(allow_gpu_compute&&!input_active&&impl_->gpu_queued.empty()&&impl_->gpu_stack_queued.empty()&&impl_->texture_upload_waiting&&!impl_->preparing_textures.empty()){
    performance::Scope timing("gpu_texture_prepare");std::vector<const FillTexture *> inputs;for(const auto &texture:impl_->preparing_textures)inputs.push_back(texture.get());const auto result=impl_->gpu_preview.prepare_texture_resources(inputs);
    if(result==GpuPreviewUpdate::Unsupported){impl_->texture_upload_error="Insufficient GPU memory to prepare the material input group at full resolution";impl_->texture_upload_waiting=false;}
    else if(result==GpuPreviewUpdate::Updated){impl_->texture_upload_waiting=false;std::printf("BTS_TEXTURE_GPU_READY name=%s full_resolution=1\n",(impl_->preparing_textures.size()==1?impl_->preparing_textures.front()->name.c_str():"material input group"));}
  }
  for(const auto &summary:impl_->gpu_preview.displacement_summaries())if(auto found=impl_->bindings.find(summary.uid);found!=impl_->bindings.end()){
    auto &binding=found->second;if(summary.epoch!=binding.displacement_cpu_epoch)continue;const bool old_presence=binding.gpu_displacement_presence.value_or(binding.displacement_count>0);binding.gpu_displacement_presence=summary.present;binding.displacement_minimum=summary.minimum;binding.displacement_maximum=summary.maximum;
    const bool shader_changed=displacement_shader(*binding.owner_main,binding);if(shader_changed||old_presence!=summary.present){impl_->gpu_displacement_changed=true;if(binding.displacement_image)impl_->cycles.changed(*binding.displacement_image,summary.uid,16);}
    std::printf("BTS_GPU_DISPLACEMENT_RANGE uid=%u present=%d minimum=%.9g maximum=%.9g async_metadata_bytes=16 cpu_composite=0\n",summary.uid,int(summary.present),summary.minimum,summary.maximum);
  }
  poll_gpu_programs();
  if(!impl_->pending||!impl_->pending->layer_edit)return;const auto found=impl_->bindings.find(impl_->pending->result.uid);if(found==impl_->bindings.end())return;auto &binding=found->second;
  for(int c=0;c<17;++c)if(impl_->pending->result.channels[c])if(auto *image=composite_image(binding,c)){BKE_image_get_gpu_material_texture_try(image,nullptr,false);if(image->source==IMA_SRC_TILED)BKE_image_get_gpu_material_texture_try(image,nullptr,true);}
}
bool MaterialInspection::sync_cycles_images(Main &main,bool active){
 impl_->cycles_entry_waiting=false;
 const auto mirror=[&](bool enabled){return native_cycles_recorded_error_boundary(impl_->cycles_error_reported,
  [&]{return impl_->cycles.update(main,enabled);},
  [&]{return impl_->cycles.error();},
  [](std::string_view detail)noexcept{
   std::fputs("BTS_CYCLES_MIRROR_PAUSED ",stderr);
   std::fwrite(detail.data(),1,detail.size(),stderr);std::fputc('\n',stderr);
  });};
 if(!active){impl_->cycles_entry_requested=false;impl_->cycles_preparation_failed=false;impl_->cycles_preparation_error.clear();impl_->cycles_error_reported=false;return mirror(false);}
 // Packing a generated composite changes its persisted source to FILE.
 // Restore only exact, local binding-owned packed targets; imported texture
 // inputs (including Coat images) are not part of this registry. Keep the
 // existing CPU cache and GPU allocation, which already contain the pixels.
 for(auto &[uid,binding]:impl_->bindings)for(int c=0;c<17;++c){
  if(!cycles_shader_channel(binding,c))continue;auto *image=composite_image(binding,c);
  if(!image||image->source!=IMA_SRC_FILE||!image->packedfiles.first||image->alpha_mode!=IMA_ALPHA_CHANNEL_PACKED||!ELEM(image->type,IMA_TYPE_IMAGE,IMA_TYPE_UV_TEST))continue;
  if(binding.owner_main!=&main||BLI_findindex(&main.images,image)<0||ID_IS_LINKED(&image->id)||ID_IS_OVERRIDE_LIBRARY(&image->id)||(image->id.tag&(ID_TAG_NO_MAIN|ID_TAG_LOCALIZED|ID_TAG_COPIED_ON_EVAL|ID_TAG_MISSING)))continue;
  const auto *node=composite_texture(binding,c);const auto *tile=static_cast<const ImageTile *>(image->tiles.first);
  if(!node||node->id!=&image->id||BLI_listbase_count(&image->tiles)!=1||!tile||tile->tile_number!=1001||tile->gen_x<1||tile->gen_y<1)continue;
  image->source=IMA_SRC_GENERATED;DEG_id_tag_update_ex(&main,&image->id,ID_RECALC_SOURCE);
  std::printf("BTS_CYCLES_RESTORE_PACKED_TARGET uid=%u channel=%d image=%s width=%d height=%d retained_storage=1\n",uid,c,image->id.name+2,tile->gen_x,tile->gen_y);
 }
 if(!impl_->cycles_entry_requested){
  impl_->cycles_entry_requested=true;impl_->cycles_error_reported=false;
  for(auto &[uid,binding]:impl_->bindings)for(int c=0;c<17;++c){
   if(!cycles_shader_channel(binding,c))continue;if(auto *image=composite_image(binding,c))impl_->cycles.changed(*image,uid,c);
  }
 }
 // A failed entry may still own a fence/worker from an earlier live revision.
 // Drain that coordinator without retrying allocation until an explicit re-entry.
 if(impl_->cycles_preparation_failed){mirror(false);return false;}
 const auto producers_pending=[&]{return impl_->gpu_preview.programs_pending()||!impl_->gpu_queued.empty()||!impl_->gpu_stack_queued.empty()||impl_->texture_upload_waiting||impl_->layering_sources_pending||impl_->gpu_preview.native_preparations_pending()||std::any_of(impl_->bindings.begin(),impl_->bindings.end(),[](const auto &entry){return entry.second.world_view_dirty||entry.second.world_objects_dirty||bool(entry.second.margin_job)||bool(entry.second.projection_job)||NativeWorldNormalContextRoute::producer_pending(entry.second.world_set.has_value(),bool(entry.second.world_geometry_job))||(entry.second.world_set&&entry.second.world_tile<entry.second.world_tiles.size());});};
 if(producers_pending()){impl_->cycles_entry_waiting=true;return false;}
 for(auto &[uid,binding]:impl_->bindings)for(int c=0;c<17;++c){
  if(!cycles_shader_channel(binding,c))continue;auto *image=composite_image(binding,c);if(!image)continue;
  const auto existing=BKE_image_gpu_override_texture(image);if(existing.texture&&*existing.texture)continue;
  try{
   performance::Scope timing("cycles_gpu_target_prepare");
   // Runtime-only native ownership is lost by packed Open. Adopt only the
   // validated binding's authoritative GPU stack target, never a bake/input.
   const bool gpu_authoritative=binding.gpu_stack_set&&binding.gpu_stack_active[c];
   std::uint64_t width=1,height=1,count=0;for(const ImageTile &tile:image->tiles){width=std::max(width,std::uint64_t(tile.gen_x));height=std::max(height,std::uint64_t(tile.gen_y));++count;}
   auto bytes=resource_sum(resource_product(resource_product(resource_product(resource_product(width,height),count),image->flag&IMA_HIGH_BITDEPTH?16:8),4),2)/3;
   if(gpu_authoritative){
    if(!ELEM(image->source,IMA_SRC_GENERATED,IMA_SRC_TILED)||!ELEM(image->type,IMA_TYPE_IMAGE,IMA_TYPE_UV_TEST)||!GpuLayerPreview::supported(*binding.gpu_stack_set,c))throw std::runtime_error("Required owned image has no authoritative GPU producer");
    const bool was_native=native_gpu_image(image);const auto flags=image->flag;
    image->runtime->gpuflag|=native_generated_gpu_bit;native_image_precision(image,gpu_channel_precision(*binding.gpu_stack_set,c));
    try{prepare_native_gpu_image(image);}catch(...){if(!was_native)image->runtime->gpuflag&=~native_generated_gpu_bit;image->flag=flags;throw;}
    // New zero storage is never a completed image. Recompose the complete
    // latest source snapshot before admitting the first/live Cycles mirror.
    binding.gpu_unbound_waiting[c]=true;
   }else{
    // Genuine CPU-owned legacy images retain Blender's normal upload path.
    // No layer evaluation, author-page readback or CPU composition occurs here.
    ResourceAvailability budget;int total_kb=0,free_kb=0;if(!GPU_type_matches(GPU_DEVICE_SOFTWARE,GPU_OS_ANY,GPU_DRIVER_ANY)&&GPU_mem_stats_supported())GPU_mem_stats_get(&total_kb,&free_kb);
    budget.vram_known=coherent_vram_report(total_kb,free_kb);budget.available_vram=budget.vram_known?std::uint64_t(free_kb)*1024:0;
    BKE_image_get_gpu_material_texture(image,nullptr,false);if(image->source==IMA_SRC_TILED)BKE_image_get_gpu_material_texture(image,nullptr,true);
   }
   const auto allocated=BKE_image_gpu_override_texture(image);if(!allocated.texture||!*allocated.texture)throw std::runtime_error("Native allocation returned no full-resolution GPU storage");
   bytes=resource_sum(resource_product(resource_product(resource_product(resource_product(GPU_texture_width(*allocated.texture),GPU_texture_height(*allocated.texture)),std::max(1,GPU_texture_layer_count(*allocated.texture))),GPU_texture_format(*allocated.texture)==gpu::TextureFormat::SFLOAT_32_32_32_32?16:8),4),2)/3;
   std::printf("BTS_CYCLES_GPU_TARGET_PREPARE uid=%u channel=%d image=%llx image_uid=%u tiles=%llu bytes=%llu gpu_authoritative=%d cpu_initial_upload=%d cpu_composite=0\n",uid,c,(unsigned long long)reinterpret_cast<std::uintptr_t>(image),image->id.session_uid,(unsigned long long)count,(unsigned long long)bytes,int(gpu_authoritative),int(!gpu_authoritative));
   impl_->cycles.changed(*image,uid,c);resume_gpu_channels();impl_->cycles_entry_waiting=true;return false;
  }catch(const std::exception &error){
   impl_->cycles_preparation_failed=true;try{impl_->cycles_preparation_error="Cannot prepare Cycles channel "+std::to_string(c)+" ("+std::string(image->id.name+2)+"): "+error.what();}catch(...){impl_->cycles_preparation_error.clear();}
   const auto detail=cycles_images_error();std::fputs("BTS_CYCLES_MIRROR_PAUSED ",stderr);std::fwrite(detail.data(),1,detail.size(),stderr);std::fputc('\n',stderr);mirror(false);return false;
  }
 }
 resume_gpu_channels();
 // Hidden allocations are not sufficient: all latest producer writes must be
 // complete before publishing a full required-image entry or live revision.
 if(producers_pending()){impl_->cycles_entry_waiting=true;return false;}
 return mirror(true);
}
bool MaterialInspection::cycles_images_pending()const{return impl_->cycles_entry_waiting||impl_->cycles.pending();}
std::string_view MaterialInspection::cycles_images_error()const{if(impl_->cycles_preparation_failed)return impl_->cycles_preparation_error.empty()?std::string_view("Required Cycles GPU image preparation failed"):std::string_view(impl_->cycles_preparation_error);return impl_->cycles.error();}

void MaterialInspection::finalize_gpu_images(Main &main,bool allow_gpu_compute){
  // One native image per frame; never perform a release-time loop over the set.
  if(allow_gpu_compute){performance::Scope timing("gpu_mipmap_finalize_step");impl_->gpu_mips_pending=BKE_image_gpu_finalize_mipmap_step(&main)>0;}
  else impl_->gpu_mips_pending=true;
}
bool MaterialInspection::poll_composites(Main &,bool wait){
 performance::Scope timing("poll_composites");
 impl_->collect_projection_geometry();
 if(wait&&!impl_->failures.empty())throw std::runtime_error("GPU layer update failed: "+impl_->failures.begin()->second);
 property_pipeline_pending=updates_pending();
 return !impl_->gpu_stack_queued.empty()||!impl_->gpu_queued.empty()||std::any_of(impl_->bindings.begin(),impl_->bindings.end(),[](const auto &entry){const auto &b=entry.second;return bool(b.world_geometry_job)||(b.world_set&&b.world_tile<b.world_tiles.size());});
}

}
