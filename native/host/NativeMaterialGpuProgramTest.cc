#include "NativeMaterialGpuProgramTest.hh"
#include "NativeMaterialGpuProgram.hh"
#include "FillProjection.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "../tests/NativeMaterialProjectionOracle.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialContext.hh"
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialGpuPreparation.hh"
#include "NativeMaterialPreparationQueue.hh"
#include "IMB_imbuf_types.hh"
#include <QImage>
#include <QColor>
#include <QDir>
#include <QTemporaryDir>
#include <barrier>
#include <future>
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "DNA_image_types.h"
#include "BKE_mesh.hh"
#include "BKE_object.hh"
#include "BKE_object_types.hh"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialAmbient.hh"
#include "../tests/NativeMaterialMappingFixture.hh"
#include "../tests/NativeMaterialVectorMathArchiveFixture.hh"
#include "NativeMaterialRamp.hh"
#include "BKE_material.hh"
#include "DNA_material_types.h"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include "GPU_pass.hh"
#include "GPU_state.hh"
#include "GPU_material.hh"
#include "MEM_guardedalloc.h"
#include <QElapsedTimer>
#include <QJsonArray>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <limits>
namespace bts {
// The native shader borrows this exact packed atlas representation.
static_assert(offsetof(FillSurfacePoint,match_object)==14*sizeof(std::uint32_t));
static_assert(sizeof(FillSurfacePoint)==15*sizeof(std::uint32_t));
static_assert(NativeMaterialObjectTable::stride==12);
QJsonObject verify_native_material_gpu_program(){
 double preparation_step_ms=0;bool tree_worker_isolation=false;int image_upload_steps=0;double image_upload_step_ms=0;double ramp_submit_ms=0;using namespace blender;const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);
 const auto socket=[](bNode &node,const char *id,bool is_input)->bNodeSocket &{for(auto &socket:is_input?node.inputs:node.outputs)if(STREQ(socket.identifier,id))return socket;throw std::runtime_error("Native GPU source test socket missing");};
 const auto make_tree=[&](const char *name,bool noise=false){auto *tree=bke::node_tree_add_tree(main.get(),"Native surface GPU oracle","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;auto *attribute=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name,name);auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel");
  if(!noise)bke::node_add_link(*tree,*attribute,socket(*attribute,"Color",false),*aov,socket(*aov,"Color",true));
  else{auto *node=bke::node_add_node(nullptr,*tree,UString("ShaderNodeTexNoise"));bke::node_add_link(*tree,*attribute,socket(*attribute,"Vector",false),*node,socket(*node,"Vector",true));auto *parameter=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(parameter->storage)->name,"BTS_Param0");auto *multiply=bke::node_add_node(nullptr,*tree,UString("ShaderNodeMath"));multiply->custom1=NODE_MATH_MULTIPLY;auto *first=static_cast<bNodeSocket *>(multiply->inputs.first);bke::node_add_link(*tree,*node,socket(*node,"Fac",false),*multiply,*first);bke::node_add_link(*tree,*parameter,socket(*parameter,"Fac",false),*multiply,*first->next);bke::node_add_link(*tree,*multiply,socket(*multiply,"Value",false),*aov,socket(*aov,"Value",true));}
  bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*main,*tree);return tree;};
 constexpr int width=17,height=9;using Texture=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;Texture target(GPU_texture_create_2d("Native surface oracle",width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);require(bool(target),"Native surface target allocation failed");
 std::vector<FillSurfacePoint> points(width*height);for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto &point=points[y*width+x];point.position={float(2*(x+.5)/width+3),float(3*(y+.5)/height-2),4};point.normal={.6f,0,.8f};point.valid=x!=0;}
 // Exact same packed type as GpuLayerPreview::surface, including uint validity.
 const auto surface_bytes=points.size()*sizeof(FillSurfacePoint);const int words=int((surface_bytes+15)/16);std::vector<unsigned int> packed(words*4);std::memcpy(packed.data(),points.data(),surface_bytes);Texture surface(GPU_texture_create_2d_array("Native borrowed surface oracle",words,1,1,1,gpu::TextureFormat::UINT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);require(bool(surface),"Native surface allocation failed");GPU_texture_update(surface.get(),GPU_DATA_UINT,packed.data());
 NativeMaterialGpuContext context;context.surface=surface.get();context.surface_size={width,height};context.surface_stride=sizeof(FillSurfacePoint)/4;context.object_to_world={2,0,0,0,0,3,0,0,0,0,4,0,3,-2,0,1};context.world_to_object={.5,0,0,0,0,1.f/3,0,0,0,0,.25,0,-1.5,2.f/3,0,1};context.bounds_max={1,1,2};context.object_random=.375;context.object_color={.2,.4,.7,1};
 QJsonArray cases;float max_error=0;int reads=0;
 const auto pixels=[&]{++reads;return static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));};
 for(const char *name:{"BTS_Coord_UV","BTS_Coord_Object","BTS_Coord_Generated","BTS_Coord_Normal","BTS_Geometry_Position","BTS_Geometry_Normal","BTS_Object_DisplacementScale","BTS_Object_Random","BTS_Object_Color"}){
  auto *tree=make_tree(name);NativeMaterialGpuProgram program(*tree,0,0);
  for(int tile:{1001,1002,1011}){context.tile=tile;program.dispatch(target.get(),{},context,{0,0,width,height});auto *actual=pixels();require(actual,"Native surface oracle read failed");if(tile==1001)std::fprintf(stderr,"BTS_NATIVE_CONTEXT_SAMPLE name=%s first=%.6g,%.6g,%.6g,%.6g mid=%.6g,%.6g,%.6g,%.6g\n",name,actual[4],actual[5],actual[6],actual[7],actual[320],actual[321],actual[322],actual[323]);const auto free=qScopeGuard([&]{MEM_delete(actual);});
   for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto &point=points[y*width+x];const float u=float((x+.5)/width),v=float((y+.5)/height);std::array<float,4> expected{0,0,0,1};const std::string_view attribute(name);
    if(attribute=="BTS_Coord_UV")expected={u+(tile-1001)%10,v+(tile-1001)/10,0,1};else if(attribute=="BTS_Coord_Object")expected={u,v,1,1};else if(attribute=="BTS_Coord_Generated")expected={u,v,.5,1};else if(attribute=="BTS_Coord_Normal"){const float length=std::hypot(1.2f,3.2f);expected={1.2f/length,0,3.2f/length,1};}else if(attribute=="BTS_Geometry_Position")expected={point.position[0],point.position[1],point.position[2],1};else if(attribute=="BTS_Geometry_Normal")expected={.6,0,.8,1};else if(attribute=="BTS_Object_DisplacementScale"){const float value=1/std::hypot(.3f,.2f);expected={value,value,value,1};}else if(attribute=="BTS_Object_Random")expected={.375,.375,.375,1};else expected=context.object_color;
    if(!point.valid)expected={};for(int c=0;c<4;++c){const float error=std::abs(actual[(y*width+x)*4+c]-expected[c]);if(error>max_error&&error>=2e-6)std::fprintf(stderr,"BTS_NATIVE_CONTEXT_MISMATCH name=%s tile=%d xy=%d,%d c=%d actual=%.9g expected=%.9g error=%.9g\n",name,tile,x,y,c,actual[(y*width+x)*4+c],expected[c],error);max_error=std::max(max_error,error);}
   }
  }cases.append(QString::fromUtf8(name));
 }
 require(max_error<2e-6,"Native geometry/UV context differs from oracle");
#include "NativeMaterialContextTest.inc"
std::uint64_t object_texel_checks=0;
#include "NativeMaterialObjectTableTest.inc"
#include "NativeMaterialTreeIsolationTest.inc"
#include "NativeMaterialRampTest.inc"
#include "NativeMaterialImageUploadTest.inc"
#include "NativeMaterialImageResourceTest.inc"
#include "NativeMaterialAmbientTest.inc"
#include "NativeMaterialGpuLibraryTest.inc"
#include "NativeMaterialMappingTest.inc"
#include "NativeMaterialProjectionTest.inc"
 // Explicit working-space conversion applies only to RGB output, once.
 {auto *tree=make_tree("BTS_Object_Color");NativeMaterialGpuProgram color(*tree,0,0);context.color_to_aces={.6130974f,.07019372f,.02061559f,.33952315f,.91635388f,.10956977f,.04737945f,.01345240f,.86981463f};color.dispatch(target.get(),{},context,{0,0,width,height});auto *actual=pixels();require(actual,"Native color conversion read failed");const auto free=qScopeGuard([&]{MEM_delete(actual);});for(int c=0;c<3;++c){float expected=0;for(int k=0;k<3;++k)expected+=context.color_to_aces[k*3+c]*context.object_color[k];require(std::abs(actual[4+c]-expected)<1e-6,"Native RGB working-space conversion failed");}context.color_to_aces={1,0,0,0,1,0,0,0,1};}
 // Native Fresnel uses supplied view and surface normal, not an artificial one.
 {auto *tree=bke::node_tree_add_tree(main.get(),"Native Fresnel context oracle","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;auto *fresnel=bke::node_add_node(nullptr,*tree,UString("ShaderNodeFresnel"));static_cast<bNodeSocketValueFloat *>(socket(*fresnel,"IOR",true).default_value)->value=1.5;auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel");bke::node_add_link(*tree,*fresnel,socket(*fresnel,"Fac",false),*aov,socket(*aov,"Value",true));bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*main,*tree);NativeMaterialGpuProgram program(*tree,0,2);
  const auto fresnel_value=[](double cosine){const double eta=1.5,g=std::sqrt(eta*eta-1+cosine*cosine),a=(g-cosine)/(g+cosine),b=(cosine*(g+cosine)-1)/(cosine*(g-cosine)+1);return .5*a*a*(1+b*b);};
  FillSurfaceAtlas real_surface;real_surface.extent=2;TextureSet real_units;FillProjection tri_context;tri_context.mode=1;
  for(bool projected:{false,true})for(bool perspective:{false,true}){context.projection=projected?native_material_projection(tri_context,&real_surface,real_units):nullptr;context.perspective=perspective;program.dispatch(target.get(),{},context,{0,0,width,height});auto *actual=pixels();require(actual,"Native Fresnel oracle read failed");const auto free=qScopeGuard([&]{MEM_delete(actual);});for(int i=0;i<width*height;++i){if(!points[i].valid)continue;double cosine=.8;if(perspective){std::array<double,3> delta{};double length=0;for(int c=0;c<3;++c){delta[c]=context.camera_position[c]-points[i].position[c];length+=delta[c]*delta[c];}cosine=std::abs((.6*delta[0]+.8*delta[2])/std::sqrt(length));}require(std::abs(actual[i*4]-fresnel_value(cosine))<2e-6,"Native Fresnel does not follow the supplied view");}}
  bool rejected=false;try{auto missing=context;missing.surface=nullptr;program.dispatch(target.get(),{},missing,{0,0,width,height});}catch(const std::invalid_argument &){rejected=true;}require(rejected,"View-dependent native graph accepted missing surface");context.perspective=false;context.projection.reset();
 }
 auto *noise_tree=make_tree("BTS_Coord_Object",true);NativeMaterialGpuProgram noise(*noise_tree,1,2);std::array<std::array<float,4>,1> parameter{{{1,1,1,1}}};noise.dispatch(target.get(),parameter,context,{0,0,width,height});auto *base=pixels();require(base,"Native noise oracle read failed");const auto free_base=qScopeGuard([&]{MEM_delete(base);});float low=1,high=0;for(int i=0;i<width*height;++i)if(points[i].valid){low=std::min(low,base[i*4]);high=std::max(high,base[i*4]);}require(high-low>.05,"Native noise ignored varying geometry");
 const auto recompiles=GPU_pass_global_compilation_count();QElapsedTimer timer;double submit=0;
 for(float value:{-.75f,.25f,1.5f,4.f}){parameter[0]={value,value,value,1};timer.start();noise.dispatch(target.get(),parameter,context,{0,0,width,height});submit=std::max(submit,timer.nsecsElapsed()/1e6);auto *actual=pixels();require(actual,"Native parameter oracle read failed");const auto free=qScopeGuard([&]{MEM_delete(actual);});for(int i=0;i<width*height;++i)require(std::abs(actual[i*4]-base[i*4]*value)<1e-6,"Native surface parameter changed node evaluation");}
 const float sentinel[4]{-9,-8,-7,-6};GPU_texture_clear(target.get(),GPU_DATA_FLOAT,sentinel);noise.dispatch(target.get(),parameter,context,{3,2,8,4});auto *dirty=pixels();require(dirty,"Native region oracle read failed");const auto free_dirty=qScopeGuard([&]{MEM_delete(dirty);});for(int y=0;y<height;++y)for(int x=0;x<width;++x)for(int c=0;c<4;++c){const float expected=x>=3&&x<11&&y>=2&&y<6?(c==3?1:base[(y*width+x)*4]*4):sentinel[c];require(std::abs(dirty[(y*width+x)*4+c]-expected)<1e-6,"Native region wrote clean pixels or missed dirty pixels");}
 bool missing=false;try{auto empty=context;empty.surface=nullptr;noise.dispatch(target.get(),parameter,empty,{0,0,width,height});}catch(const std::invalid_argument &){missing=true;}require(missing,"Missing native geometry was silently replaced");
 require(GPU_pass_global_compilation_count()==recompiles,"Native program parameter update recompiled a shader");
 return {{"fill_projection",projection_report},{"linked_mapping",mapping_report},{"attributes",cases},{"tiles",QJsonArray{1001,1002,1011}},{"max_error",max_error},{"diagnostic_readbacks",reads},{"parameter_submit_ms",submit},{"shader_recompiles",0},{"borrowed_geometry",true},{"dirty_region_preserved",true},{"varying_native_noise",true},{"fresnel_view_context",true},{"baked_ao_context",true},{"native_object_view_context",true},{"multi_object_owner_table",true},{"multi_object_texel_checks",qint64(object_texel_checks)},{"linked_ramp_runtime_copy",true},{"dynamic_ramp_instances",true},{"dynamic_ramp_stop_bounds",QJsonArray{1,32}},{"dynamic_ramp_submit_ms",ramp_submit_ms},{"preparation_queue_latest_wins",true},{"prepared_before_activation",true},{"preparation_step_ms",preparation_step_ms},{"tree_worker_isolation",tree_worker_isolation},{"shared_native_cpu_image",true},{"shared_native_gpu_image",true},{"bounded_image_upload",true},{"image_upload_steps",image_upload_steps},{"image_upload_step_ms",image_upload_step_ms},{"shared_library_instances",true},{"color_space_conversion",true},{"missing_geometry_rejected",true}};
}
}
