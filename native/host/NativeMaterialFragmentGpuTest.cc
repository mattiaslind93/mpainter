#include "NativeMaterialFragmentGpuTest.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "NativeMaterialGraph.hh"
#include "GpuLayerPreview.hh"
#include "NormalVector.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "../tests/NativeMaterialFragmentGpuFixture.hh"
#include "../tests/NativeMaterialFragmentNormalMapFixture.hh"
#include "../tests/NativeMaterialFragmentResidentFixture.hh"
#include "../tests/NativeFragmentRetainedImageContextTest.hh"
#include "draw_shader_shared.hh"
#include "GPU_pass.hh"
#include "GPU_framebuffer.hh"
#include "GPU_state.hh"
#include <epoxy/gl.h>
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QScopeGuard>
#include <future>
#include <chrono>
#include <algorithm>
#include <QJsonArray>
#include <cmath>
#include <cstdio>
namespace bts {
NativeFragmentGpuFixtures prepare_native_fragment_gpu_fixtures(){
 auto work=std::async(std::launch::async,[]{using namespace blender;std::atomic_bool cancel=false;NativeFragmentGpuFixtures result;
  for(const auto &[width,invert]:std::array<std::pair<float,bool>,3>{{{.5f,false},{.5f,true},{4,false}}})result.bumps.push_back(prepare_native_fragment_normal(native_fragment_gpu_test::source(width,invert),cancel));
  result.geometry=prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,true),cancel);result.named=prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"named"),cancel);result.ramp=prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"uv",true),cancel);
  result.unsupported.emplace_back("linked Filter Width",prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"uv",false,true),cancel));
  result.unsupported.emplace_back("Normal Map",prepare_native_fragment_normal(native_fragment_normal_test::fixture(native_fragment_normal_test::Kind::NormalMap),cancel));
  result.unsupported.emplace_back("UV/object/view binding",result.named);
  result.unsupported.emplace_back("captured resident owner context",prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"object"),cancel));
  result.unsupported.emplace_back("view context",prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"view"),cancel));
  result.unsupported.emplace_back("image source",prepare_native_fragment_normal(native_fragment_gpu_test::source(.5f,false,false,"image"),cancel));
  // Conditional physical-context sources are kept in the same owned packet;
  // the original five checks retain their source cohort. First six retain the
  // original phase1 rejection cohort; these five have separate phase2 checks.
  for(int kind=0;kind<5;++kind){auto original=native_fragment_normal_map_test::source(kind==1,kind==2,kind==3,kind==4?SHD_SPACE_WORLD:SHD_SPACE_TANGENT);const auto before=native_fragment_normal_test::source_state(*original.material->nodetree);auto owner=prepare_native_fragment_normal(std::move(original),cancel);native_raw_height_test::require(before==native_fragment_normal_test::source_state(*owner.source->material->nodetree),"NormalMap preparation mutated original source controls/links");
   int normal_maps=0;for(const auto &node:owner.tree->nodes)if(STREQ(node.idname,"ShaderNodeNormalMap")){const auto &storage=*static_cast<const NodeShaderNormalMap *>(node.storage);native_raw_height_test::require(STREQ(storage.uv_map,"FixtureUV")&&storage.space==(kind==4?SHD_SPACE_WORLD:SHD_SPACE_TANGENT)&&storage.convention==(kind==1?SHD_NORMAL_MAP_CONVENTION_DIRECTX:SHD_NORMAL_MAP_CONVENTION_OPENGL)&&storage.base==SHD_NORMAL_MAP_BASE_ORIGINAL&&node.custom1==(kind==3?1:0),"Private NormalMap lost exact original UV/space/convention/base/custom1");++normal_maps;}native_raw_height_test::require(normal_maps==1,"NormalMap fixture lost its actual prepared chain");
   native_raw_height_test::require(owner.source->channels.size()==6&&std::find(owner.source->description.channels.begin(),owner.source->description.channels.end(),3)==owner.source->description.channels.end()&&owner.source->description.height_policy==NativeHeightPolicy::None,"NormalMap preparation changed default source channels/policy");
   const std::array<int,6> defaults{0,1,2,4,5,14};for(std::size_t i=0;i<defaults.size();++i)native_raw_height_test::require(owner.source->channels[i].channel==defaults[i],"Default native channel slot changed");
   bool strength=false,color=false;for(std::size_t i=0;i<owner.source->parameters.size();++i){const auto &p=owner.source->description.parameters[i];if(p.name=="Normal Strength")strength=owner.source->parameters[i][0]==.65f;if(p.name=="Normal Color")color=owner.source->parameters[i]==std::array<float,4>{.2f,.7f,1,1};}native_raw_height_test::require(strength&&color,"NormalMap Strength/Color source controls changed");result.unsupported.emplace_back("phase2:"+std::to_string(kind),std::move(owner));
  }
  QTemporaryDir directory;native_raw_height_test::require(directory.isValid(),"Cannot create image Normal GPU fixture");
  const auto image_path=directory.filePath("normal.exr");native_fragment_retained_image_test::write_image(image_path);
  for(bool bump:{false,true}){
   auto source=native_fragment_retained_image_test::fixture(image_path);
   if(bump){auto &tree=*source.material->nodetree;auto &texture=native_fragment_retained_image_test::image_node(tree);bNode *pbr=nullptr;for(auto &node:tree.nodes)if(STREQ(node.idname,"ShaderNodeBsdfPrincipled"))pbr=&node;native_raw_height_test::require(pbr,"Image Bump fixture lost Principled");auto &normal=native_fragment_retained_image_test::socket(*pbr,"Normal",true);bke::node_remove_link(&tree,*normal.link);auto *node=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));bke::node_add_link(tree,texture,native_fragment_retained_image_test::socket(texture,"Color",false),*node,native_fragment_retained_image_test::socket(*node,"Height",true));bke::node_add_link(tree,*node,native_fragment_retained_image_test::socket(*node,"Normal",false),*pbr,normal);BKE_ntree_update_after_single_tree_change(*source.main,tree);}
   auto graph=finish_native_material_source(std::move(source),cancel,nullptr,NativeWorldNormalSurfaceContract::PainterUndisplaced);prepare_optional_native_world_normal(graph,cancel);prepare_native_world_normal_images(graph,cancel);native_raw_height_test::require(graph->world_normal_tree&&graph->world_normal_error.isEmpty(),"Image Normal GPU fixture preparation failed");result.image_normals.push_back({graph,graph->world_normal_tree});
  }
  return result;});
 if(work.wait_for(std::chrono::seconds(120))!=std::future_status::ready)throw std::runtime_error("Prepared fragment fixture worker deadline");auto result=work.get();result.resident=native_fragment_resident_test::prepare();return result;
}
namespace {
using native_fragment_gpu_test::require;
struct Frame{const char *name;std::array<float,3>T,B;};
struct HeldState {
 blender::gpu::FrameBuffer *framebuffer=blender::GPU_framebuffer_active_get();
 blender::GPUBlend blend=blender::GPU_blend_get();blender::GPUDepthTest depth=blender::GPU_depth_test_get();blender::GPUFaceCullTest cull=blender::GPU_face_culling_get();blender::GPUStencilTest stencil=blender::GPU_stencil_test_get();blender::GPUWriteMask write=blender::GPU_write_mask_get();
 std::array<int,4> viewport{},scissor{};int front=0;bool scissor_on=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
 HeldState(){blender::GPU_viewport_size_get_i(viewport.data());blender::GPU_scissor_get(scissor.data());glGetIntegerv(GL_FRONT_FACE,&front);}
 bool operator==(const HeldState &)const=default;
 void restore()const{using namespace blender;if(framebuffer)GPU_framebuffer_bind(framebuffer);else GPU_framebuffer_restore();GPU_viewport(viewport[0],viewport[1],viewport[2],viewport[3]);GPU_scissor(scissor[0],scissor[1],scissor[2],scissor[3]);GPU_scissor_test(scissor_on);GPU_front_facing(front==GL_CW);GPU_blend(blend);GPU_depth_test(depth);GPU_face_culling(cull);GPU_stencil_test(stencil);GPU_write_mask(write);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB);else glDisable(GL_FRAMEBUFFER_SRGB);GPU_apply_state();}
};

std::vector<NativeFragmentVertex> vertices(int size,const Frame &frame){
 const float corners[6][2]{{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};std::vector<NativeFragmentVertex> result(6);const float shift=size==128?-1.f/256:0;
 for(int i=0;i<6;++i){auto &v=result[i];v.raster_uv={corners[i][0],corners[i][1]};v.uv={corners[i][0]+shift,corners[i][1]+shift};for(int c=0;c<3;++c)v.position[c]=frame.T[c]*v.uv[0]+frame.B[c]*v.uv[1];}return result;
}
std::vector<std::array<float,4>> values(const PreparedNativeFragmentNormal &owner,float strength,float distance){
 auto result=owner.source->parameters;for(std::size_t i=0;i<result.size();++i){const auto &p=owner.source->description.parameters[i];if(p.name=="Strength")result[i]={strength,strength,strength,1};if(p.name=="Distance")result[i]={distance,distance,distance,1};}return result;
}
std::vector<float> draw(NativeMaterialFragmentGpuProgram &program,int size,const Frame &frame,std::span<const std::array<float,4>> parameters,std::span<const NativeMaterialMapping> mappings={},const NativeFragmentFacingGeometry *provided=nullptr,const NativeFragmentPhysicalContext *physical=nullptr){
 using namespace blender;std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> target(GPU_texture_create_2d("Prepared fragment world-vector diagnostic",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_ATTACHMENT|GPU_TEXTURE_USAGE_HOST_READ|GPU_TEXTURE_USAGE_SHADER_READ,nullptr),GPU_texture_free);require(bool(target),"Prepared fragment target allocation failed");const auto ordinary=provided?std::vector<NativeFragmentVertex>{}:vertices(size,frame);const auto &mesh=provided?provided->vertices:ordinary;
 // Nondefault held native FBO/state must survive this owner call exactly.
 GPU_apply_state();const HeldState original;const auto restore=qScopeGuard([&]{original.restore();});gpu::FrameBuffer *held_raw=nullptr;GPU_framebuffer_ensure_config(&held_raw,{GPU_ATTACHMENT_NONE,GPU_ATTACHMENT_TEXTURE(target.get())});std::unique_ptr<gpu::FrameBuffer,decltype(&GPU_framebuffer_free)> held(held_raw,GPU_framebuffer_free);require(bool(held),"Held-state diagnostic FBO allocation failed");GPU_framebuffer_bind(held.get());GPU_viewport(7,11,29,31);GPU_scissor(3,5,17,19);GPU_scissor_test(false);GPU_front_facing(false);GPU_blend(GPU_BLEND_ALPHA_PREMULT);GPU_depth_test(GPU_DEPTH_LESS);GPU_face_culling(GPU_CULL_BACK);GPU_apply_state();HeldState expected;expected.scissor_on=true;expected.front=GL_CW;
 // Deliberately leave these two logical changes pending. A raw GL snapshot
 // before flushing them loses the caller's state even if ordinary tests pass.
 GPU_scissor_test(true);GPU_front_facing(true);
 if(!parameters.empty()){bool refused=false;try{program.draw(target.get(),mesh,{},mappings,physical);}catch(const std::invalid_argument &){refused=true;}require(refused,"Invalid fragment row packet was not rejected");GPU_apply_state();require(HeldState{}==expected,"Fragment validation throw lost pending caller state");GPU_scissor_test(false);GPU_front_facing(false);GPU_apply_state();GPU_scissor_test(true);GPU_front_facing(true);}
 program.draw(target.get(),mesh,parameters,mappings,physical);GPU_apply_state();require(HeldState{}==expected,"Fragment draw did not restore held FBO/viewport/scissor/front/blend/depth/cull/stencil/write/sRGB state");auto *rgba=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(rgba,"Prepared fragment diagnostic readback missing");const auto release=qScopeGuard([&]{MEM_delete(rgba);});return {rgba,rgba+size*size*4};
}
double compare(const std::vector<float>&pixels,int size,const Frame &frame,float strength,float distance,float width,bool invert,bool geometry=false,double u_scale=1,double u_offset=0){
 double maximum=0;for(int y=6;y<size-6;++y)for(int x=6;x<size-6;++x){if(std::abs(x-y)<6)continue;const double u=(x+.5)/size+(size==128?-1./256:0),slope=(invert?-1.:1.)*distance*(2*u_scale*u_scale*u+2*u_scale*u_offset+u_scale*u_scale*width/size),length=std::sqrt(1+slope*slope);double n[3]{-slope/length*strength,0,1-strength+strength/length};if(geometry){n[0]=0;n[2]=1;}const double norm=std::sqrt(n[0]*n[0]+n[2]*n[2]);for(int c=0;c<4;++c){const double expected=c==3?1:frame.T[c]*(n[0]/norm)+(c==2?n[2]/norm:0);const double actual=pixels[(y*size+x)*4+c],error=std::abs(actual-expected);if(!std::isfinite(actual)||error>3e-5){char message[512];std::snprintf(message,sizeof(message),"Prepared fragment UV oracle size=%d x=%d y=%d component=%d expected=%.12g actual=%.12g error=%.12g bound=3e-5",size,x,y,c,expected,actual,error);throw std::runtime_error(message);}maximum=std::max(maximum,error);}}return maximum;
}

std::vector<std::array<float,4>> normal_map_values(const PreparedNativeFragmentNormal &owner,float strength,std::array<float,3> color={.2f,.7f,1}){
 auto rows=values(owner,.35f,.3f);bool found_strength=false,found_color=false;
 for(std::size_t i=0;i<rows.size();++i){const auto &p=owner.source->description.parameters[i];if(p.name=="Normal Strength"){rows[i]={strength,strength,strength,1};found_strength=true;}if(p.name=="Normal Color"){rows[i]={color[0],color[1],color[2],1};found_color=true;}}
 require(found_strength&&found_color,"NormalMap exposed parameter identities missing");return rows;
}
// Independent pixel barycentrics and vector equations; no production shader
// helper/output is used to form an expected result.
double compare_normal_map(const std::vector<float>&pixels,int size,const NativeFragmentFacingGeometry &mesh,const NativeFragmentPhysicalContext &context,float strength,std::array<float,3> color,bool directx,bool nested,bool normalize_ni_reference=false){
 using namespace native_fragment_normal_map_test;double maximum=0;
 for(int y=6;y<size-6;++y)for(int x=6;x<size-6;++x){if(std::abs(x-y)<6)continue;const double px=(x+.5)/size,py=(y+.5)/size;bool found=false;
  for(const auto &batch:mesh.batches)for(unsigned int first=batch.first;first<batch.first+batch.count;first+=3){const std::array<NativeFragmentVertex,3> v{mesh.vertices[first],mesh.vertices[first+1],mesh.vertices[first+2]};const double ax=v[0].raster_uv[0],ay=v[0].raster_uv[1],bx=v[1].raster_uv[0],by=v[1].raster_uv[1],cx=v[2].raster_uv[0],cy=v[2].raster_uv[1];const double det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);const double a=((by-cy)*(px-cx)+(cx-bx)*(py-cy))/det,b=((cy-ay)*(px-cx)+(ax-cx)*(py-cy))/det,c=1-a-b;if(std::min({a,b,c})<1e-8)continue;
   const auto [t,raw_ni]=interpolate(v,{a,b,c},context.world_to_object);const bool ccw=det>0,front=ccw!=batch.invert_front_face;auto ni=normalize_ni_reference?unit(raw_ni):raw_ni;
   auto expected_normal=expected(t,ni,V{color[0],color[1],color[2]},strength,front,(context.object_infos->flag&blender::OBJECT_NEGATIVE_SCALE)!=0,directx);
   if(nested){
    // P=(u,v,0), H=u²; width=.5, distance=.3, Bump strength=.35.
    // Evaluate the pinned Bump equation independently in double precision.
    const V dx{1./size,0,0},dy{0,1./size,0};const auto n=unit(expected_normal),rx=cross(dy,n),ry=cross(n,dx);const double determinant=dx[0]*rx[0]+dx[1]*rx[1]+dx[2]*rx[2];const double dh=2*px*.5/size+.25/(size*size),distance=front?.3:-.3;V perturbed{};for(int k=0;k<3;++k)perturbed[k]=.5*std::abs(determinant)*n[k]-distance*(determinant<0?-1.:1.)*dh*rx[k];perturbed=unit(perturbed);for(int k=0;k<3;++k)perturbed[k]=.65*n[k]+.35*perturbed[k];expected_normal=unit(perturbed);(void)ry;
   }
   for(int component=0;component<4;++component){const double wanted=component==3?1:expected_normal[component],actual=pixels[(y*size+x)*4+component],error=std::abs(actual-wanted);require(std::isfinite(actual),"Nonfinite NormalMap GPU output");maximum=std::max(maximum,error);if(!normalize_ni_reference&&error>3e-5){char text[512];std::snprintf(text,sizeof(text),"NormalMap independent oracle x=%d y=%d component=%d expected=%.12g actual=%.12g error=%.12g bound=3e-5",x,y,component,wanted,actual,error);throw std::runtime_error(text);}}
   found=true;break;
  }require(found,"NormalMap oracle failed to identify an interior triangle");
 }return maximum;
}
QJsonObject verify_normal_map_bindings(const NativeFragmentGpuFixtures &fixtures){
 using namespace blender;using namespace native_fragment_normal_map_test;
 const int metadata_checks=verify_metadata();require(metadata_checks>30,"NormalMap metadata witnesses missing");
 NativeFragmentLayout layout;layout.uv_name="FixtureUV";layout.tangent_stream=true;
 const auto &ordinary=fixtures.unsupported[6].second,&directx=fixtures.unsupported[7].second,&nested=fixtures.unsupported[8].second;
 NativeMaterialFragmentGpuProgram program(ordinary,layout),dx_program(directx,layout),nested_program(nested,layout);require(program.diagnostics().normal_maps==1&&nested_program.diagnostics().active_bumps==1,"NormalMap/combined source classification differs");
 const auto compiled=GPU_pass_global_compilation_count();const Frame frame{"normal-map",{1,0,0},{0,1,0}};
 struct Case {const char *name;float strength=.65f;int shape=0;bool negative=false,front=true,directx=false,neutral=false;};
 const std::array cases={Case{"strength0",0},Case{"strength.65",.65f},Case{"strength1",1},Case{"strength1.4",1.4f},Case{"rotated",.65f,1},Case{"mirroredUV",.65f,2},Case{"handedness",.65f,3},Case{"negativeFlag",.65f,0,true},Case{"mirrorAndNegative",.65f,2,true},Case{"physicalBack",.65f,0,false,false},Case{"backAndNegative",.65f,0,true,false},Case{"nonuniformMatrix",.65f,4},Case{"varyingNi",.65f,5},Case{"DirectX",.65f,0,false,true,true},Case{"neutral",.65f,0,false,true,false,true}};
 // Geometry/facing preparation happens once per case, before parameter edits.
 std::vector<NativeFragmentFacingGeometry> meshes;for(const auto &c:cases){auto v=vertices(64,frame);for(auto &p:v){p.tangent={1,0,0,1};if(c.shape==1){p.tangent={0,1,0,1};p.uv={p.raster_uv[1],1-p.raster_uv[0]};}if(c.shape==2){p.uv[0]=1-p.uv[0];p.tangent={-1,0,0,-1};}if(c.shape==3)p.tangent[3]=-1;if(c.shape==4)p.tangent={1,1,0,1};if(c.shape==5){const float x=p.raster_uv[0];p.initial_normal={.6f*x,0,1-.2f*x};p.normal=p.initial_normal;p.tangent={1-.2f*x,0,-.6f*x,1};}}const std::array<unsigned char,2> front{c.front,c.front};meshes.push_back(prepare_native_fragment_facing(v,front));}
 QJsonArray results;double maximum=0,raw_ni_discriminator=0;int reads=0;std::vector<std::vector<float>> outputs;
 for(std::size_t i=0;i<cases.size();++i){const auto &c=cases[i];std::printf("BTS_PREPARED_NORMAL_MAP_CASE name=%s\n",c.name);std::fflush(stdout);ObjectInfos info{};info.flag=c.negative?OBJECT_NEGATIVE_SCALE:eObjectInfoFlag(0);NativeFragmentPhysicalContext physical;physical.object_infos=&info;physical.facing_batches=meshes[i].batches;if(c.shape==4)physical.world_to_object={0,-.5f,0,1,0,0,0,0,2};const std::array<float,3> color=c.neutral?std::array<float,3>{.5f,.5f,1}:std::array<float,3>{.2f,.7f,1};auto &selected=c.directx?dx_program:program;const auto &owner=c.directx?directx:ordinary;
  auto pixels=draw(selected,64,frame,normal_map_values(owner,c.strength,color),{},&meshes[i],&physical);++reads;const auto error=compare_normal_map(pixels,64,meshes[i],physical,c.strength,color,c.directx,false);maximum=std::max(maximum,error);if(c.shape==5)raw_ni_discriminator=compare_normal_map(pixels,64,meshes[i],physical,c.strength,color,false,false,true);results.append(QJsonObject{{"case",c.name},{"maximum_error",error}});outputs.push_back(std::move(pixels));
 }
 for(std::size_t i=0;i<meshes[1].vertices.size();++i){const auto &a=meshes[1].vertices[i],&b=meshes[9].vertices[i];require(a.raster_uv==b.raster_uv&&a.uv==b.uv&&a.position==b.position&&a.tangent==b.tangent&&a.initial_normal==b.initial_normal,"Physical-facing witness changed UV/geometry instead of facing alone");}
 require(raw_ni_discriminator>.001,"Varying-Ni GPU fixture does not distinguish incorrect normalization");require(outputs[1]!=outputs[7]&&outputs[1]!=outputs[9]&&outputs[1]!=outputs[11],"Scale flag, physical facing or matrix change made no causal GPU change");
 // Unclipped 2x2 triangle: pixel(0,0) has binary-exact bary(.5,.25,.25).
 // All corner tangents are unit; do not fabricate zero object tangents, whose
 // stock safe_normalize fallback is +X. This isolates the exact sentinel branch.
 std::array<NativeFragmentVertex,3> sentinel{};const float xy[3][2]{{0,0},{1,0},{0,1}};for(int i=0;i<3;++i){sentinel[i].raster_uv={xy[i][0],xy[i][1]};sentinel[i].uv=sentinel[i].raster_uv;sentinel[i].position={xy[i][0],xy[i][1],0};sentinel[i].tangent={i==0?1.f:-1.f,0,0,1};sentinel[i].initial_normal=i!=0?std::array<float,3>{.6f,0,.8f}:std::array<float,3>{0,0,1};}const std::array<unsigned char,1> back{0};const auto sentinel_mesh=prepare_native_fragment_facing(sentinel,back);ObjectInfos sentinel_info{};sentinel_info.flag=OBJECT_NEGATIVE_SCALE;NativeFragmentPhysicalContext sentinel_context;sentinel_context.object_infos=&sentinel_info;sentinel_context.facing_batches=sentinel_mesh.batches;
 const auto sentinel_pixels=draw(program,2,frame,normal_map_values(ordinary,.65f),{},&sentinel_mesh,&sentinel_context);++reads;const auto [zero_tangent,raw_ni]=interpolate(sentinel,{.5,.25,.25},sentinel_context.world_to_object);require(zero_tangent==T{0,0,0,1},"Sentinel witness was not exact after per-corner transformation");const auto normalized_ni=unit(raw_ni);double sentinel_error=0,normalized_error=0;for(int c=0;c<4;++c){sentinel_error=std::max(sentinel_error,std::abs(sentinel_pixels[0*4+c]-(c==3?1:raw_ni[c])));normalized_error=std::max(normalized_error,std::abs(sentinel_pixels[0*4+c]-(c==3?1:normalized_ni[c])));}
 std::printf("BTS_NORMAL_MAP_SENTINEL size=2 xy=0,0 bary=.5,.25,.25 cpu_tangent=%.12g,%.12g,%.12g,%.12g actual=%.12g,%.12g,%.12g,%.12g raw_Ni=%.12g,%.12g,%.12g normalized_Ni=%.12g,%.12g,%.12g raw_error=%.12g normalized_error=%.12g\n",zero_tangent[0],zero_tangent[1],zero_tangent[2],zero_tangent[3],sentinel_pixels[0*4],sentinel_pixels[0*4+1],sentinel_pixels[0*4+2],sentinel_pixels[0*4+3],raw_ni[0],raw_ni[1],raw_ni[2],normalized_ni[0],normalized_ni[1],normalized_ni[2],sentinel_error,normalized_error);std::fflush(stdout);
 require(sentinel_error<=3e-5,"NormalMap sentinel did not preserve raw interpolated Ni; see existing-readback vector diagnostic");require(std::abs(raw_ni[0]*raw_ni[0]+raw_ni[2]*raw_ni[2]-1)>.01,"Sentinel Ni accidentally unit length");
 // Only after all basic equations pass, exercise actual NormalMap→Bump chain.
 for(bool front:{true,false}){const auto v=vertices(64,frame);auto tangent_v=v;for(auto &p:tangent_v)p.tangent={1,0,0,1};const std::array<unsigned char,2> facing{front,front};const auto mesh=prepare_native_fragment_facing(tangent_v,facing);ObjectInfos info{};NativeFragmentPhysicalContext context;context.object_infos=&info;context.facing_batches=mesh.batches;const auto pixels=draw(nested_program,64,frame,normal_map_values(nested,.65f),{},&mesh,&context);++reads;const auto error=compare_normal_map(pixels,64,mesh,context,.65f,{.2f,.7f,1},false,true);maximum=std::max(maximum,error);results.append(QJsonObject{{"case",front?"nestedBumpFront":"nestedBumpBack"},{"maximum_error",error}});}
 QJsonArray rejected;for(const auto &[index,text]:std::array<std::pair<int,const char *>,2>{{{9,"alternate derivative basis"},{10,"tangent space"}}}){bool refused=false;QString reason;try{NativeMaterialFragmentGpuProgram invalid(fixtures.unsupported[index].second,layout);}catch(const std::invalid_argument&e){reason=QString::fromUtf8(e.what());refused=std::string_view(e.what()).find(text)!=std::string_view::npos;}require(refused,"NormalMap unsupported mode did not reject specifically");rejected.append(QJsonObject{{"reason",reason}});}
 auto wrong=layout;wrong.uv_name="AnotherUV";bool name_rejected=false;try{NativeMaterialFragmentGpuProgram invalid(ordinary,wrong);}catch(const std::invalid_argument&e){name_rejected=std::string_view(e.what()).find("exact named")!=std::string_view::npos;}require(name_rejected,"Mismatched tangent UV was accepted");
 // Target/row packet is valid: each missing physical component must reject.
 std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> target(GPU_texture_create_2d("NormalMap missing-binding diagnostic",8,8,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_ATTACHMENT,nullptr),GPU_texture_free);require(bool(target),"NormalMap rejection target missing");const auto rows=normal_map_values(ordinary,.65f);ObjectInfos info{};NativeFragmentPhysicalContext empty;empty.object_infos=&info;bool no_context=false,no_facing=false,no_info=false;
 try{program.draw(target.get(),meshes[0].vertices,rows);}catch(const std::invalid_argument&e){no_context=std::string_view(e.what()).find("context missing")!=std::string_view::npos;}
 try{program.draw(target.get(),meshes[0].vertices,rows,{},&empty);}catch(const std::invalid_argument&e){no_facing=std::string_view(e.what()).find("physical-facing")!=std::string_view::npos;}
 empty.object_infos=nullptr;empty.facing_batches=meshes[0].batches;try{program.draw(target.get(),meshes[0].vertices,rows,{},&empty);}catch(const std::invalid_argument&e){no_info=std::string_view(e.what()).find("ObjectInfos")!=std::string_view::npos;}
 require(no_context&&no_facing&&no_info,"NormalMap missing physical context did not fail closed");require(GPU_pass_global_compilation_count()==compiled,"NormalMap dynamic rows/matrix/flags/facing recompiled shaders");require(reads==18,"NormalMap actual readback count differs");
 return {{"complete",true},{"cases",results},{"readbacks",reads},{"maximum_error",maximum},{"tolerance",3e-5},{"metadata_checks",metadata_checks},{"original_and_private_controls_preserved",true},{"raw_Ni_discriminator",raw_ni_discriminator},{"sentinel_single_triangle_raw_Ni",true},{"physical_facing_independent_of_UV",true},{"negative_scale_flag_causal",true},{"dynamic_no_recompile",true},{"wrong_UV_rejected",name_rejected},{"missing_context_rejected",no_context&&no_facing&&no_info},{"unsupported",rejected},{"production_admission",false}};
}

// Actual captured geometry is the input contract here. Expected pixels use
// independent barycentrics and vector equations, never a CPU shader evaluator.
struct ResidentWitness {double maximum=0;std::size_t samples=0;std::array<std::size_t,2> owners{};};
ResidentWitness compare_resident(const std::vector<float>&pixels,int size,const NativeFragmentGeometry &geometry,int tile,const char *uv_name,float strength,bool bump=false){
 using namespace native_fragment_normal_map_test;ResidentWitness proof;
 const double ox=(tile-1001)%10,oy=(tile-1001)/10;
 for(int y=6;y<size-6;++y)for(int x=6;x<size-6;++x){
  const double px=ox+(x+.5)/size,py=oy+(y+.5)/size;bool covered=false,ambiguous=false;V wanted{};std::size_t owner=0;
  for(const auto &r:geometry.ranges()){
   const auto &stream=geometry.streams()[r.stream];if(r.tile!=tile||!std::any_of(stream.requests.begin(),stream.requests.end(),[&](const auto &q){return q.kind==NativeFragmentUvKind::Named&&q.name==uv_name;}))continue;
   for(std::size_t first=r.first_index;first<std::size_t(r.first_index)+r.index_count;first+=3){
    const auto index=geometry.indices();const auto verts=geometry.vertices(r.stream);const std::array<NativeFragmentVertex,3> v{verts[index[first]],verts[index[first+1]],verts[index[first+2]]};
    const double ax=v[0].raster_uv[0],ay=v[0].raster_uv[1],bx=v[1].raster_uv[0],by=v[1].raster_uv[1],cx=v[2].raster_uv[0],cy=v[2].raster_uv[1],det=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy);
    const auto weights=[&](double u,double z){const double a=((by-cy)*(u-cx)+(cx-bx)*(z-cy))/det,b=((cy-ay)*(u-cx)+(ax-cx)*(z-cy))/det;return std::array<double,3>{a,b,1-a-b};};const auto bary=weights(px,py);if(std::min({bary[0],bary[1],bary[2]})<0)continue;
    // Exclude a fixed six-texel distance from REAL triangle edges, including
    // sharp face seams and helper-lane extrapolation, not an assumed diagonal.
    for(int corner=0;corner<3;++corner){const int a=(corner+1)%3,b=(corner+2)%3;const double dx=v[a].raster_uv[0]-v[b].raster_uv[0],dy=v[a].raster_uv[1]-v[b].raster_uv[1];if(bary[corner]*std::abs(det)/std::hypot(dx,dy)*size<6)ambiguous=true;}
    owner=stream.object;std::array<float,9> matrix{};for(int col=0;col<3;++col)for(int row=0;row<3;++row)matrix[col*3+row]=geometry.objects()[owner].world_to_object[col*4+row];
    const auto [t,ni]=interpolate(v,bary,matrix);const bool front=(det>0)!=r.invert_front_face;
    if(!bump)wanted=expected(t,ni,{.2f,.7f,1},strength,front,(geometry.object_infos(owner).flag&blender::OBJECT_NEGATIVE_SCALE)!=0,false);
    else {
     V n{},dx{},dy{};double u=0,du=0,dv=0;const auto wx=weights(px+1./size,py),wy=weights(px,py+1./size);
     for(int c=0;c<3;++c)for(int k=0;k<3;++k){n[c]+=bary[k]*v[k].normal[c];dx[c]+=(wx[k]-bary[k])*v[k].position[c];dy[c]+=(wy[k]-bary[k])*v[k].position[c];}
     for(int k=0;k<3;++k){u+=bary[k]*v[k].uv[0];du+=(wx[k]-bary[k])*v[k].uv[0];dv+=(wy[k]-bary[k])*v[k].uv[0];}
     n=unit(n);const auto rx=cross(dy,n),ry=cross(n,dx);double d=0;for(int c=0;c<3;++c)d+=dx[c]*rx[c];const double hx=(u+.5*du)*(u+.5*du)-u*u,hy=(u+.5*dv)*(u+.5*dv)-u*u;
     for(int c=0;c<3;++c)wanted[c]=.5*std::abs(d)*n[c]-(front?.3:-.3)*(d<0?-1.:1.)*(hx*rx[c]+hy*ry[c]);wanted=unit(wanted);for(int c=0;c<3;++c)wanted[c]=(1-strength)*n[c]+strength*wanted[c];wanted=unit(wanted);
    }covered=true;
   }
  }
  if(!covered||ambiguous)continue;++proof.samples;if(owner<proof.owners.size())++proof.owners[owner];
  for(int c=0;c<4;++c){const double expected=c==3?1:wanted[c],actual=pixels[(y*size+x)*4+c],error=std::abs(actual-expected);if(!std::isfinite(actual)||error>3e-5){char message[512];std::snprintf(message,sizeof(message),"Resident %s tile=%d owner=%zu x=%d y=%d c=%d expected=%.12g actual=%.12g error=%.12g bound=3e-5",bump?"H=u2":"NormalMap",tile,owner,x,y,c,expected,actual,error);throw std::runtime_error(message);}proof.maximum=std::max(proof.maximum,error);}
 }
 require(proof.samples>32,"Resident independent oracle has no sufficient interior coverage");return proof;
}
QJsonObject verify_resident_geometry(const NativeFragmentGpuFixtures &fixtures){
 using namespace blender;require(bool(fixtures.resident),"Resident geometry fixture missing");const auto &f=*fixtures.resident;require(f.source_main_destroyed&&f.cancelled_preparation_rejected,"Resident source lifetime/cancel witness missing");
 NativeFragmentLayout named;named.uv_name="NamedUV";named.tangent_stream=true;named.resident_raster_origin=true;auto render=named;render.uv_name="RenderUV";
 NativeMaterialFragmentGpuProgram program(f.named,named),render_program(f.render,render),bump(f.bump,named);const auto compiled=GPU_pass_global_compilation_count();
 GPU_apply_state();const HeldState caller;const auto restore_caller=qScopeGuard([&]{caller.restore();});GPU_viewport(7,11,29,31);GPU_scissor(3,5,17,19);GPU_front_facing(false);GPU_scissor_test(false);GPU_apply_state();HeldState upload_state;upload_state.front=GL_CW;upload_state.scissor_on=true;std::array<int,3> upload_bindings{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&upload_bindings[0]);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&upload_bindings[1]);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&upload_bindings[2]);GPU_front_facing(true);GPU_scissor_test(true);
 NativeMaterialFragmentGpuGeometry resident(f.original);GPU_apply_state();std::array<int,3> after_upload{};glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&after_upload[0]);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&after_upload[1]);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&after_upload[2]);require(HeldState{}==upload_state&&after_upload==upload_bindings,"Resident creation lost held/pending state or caller VAO/buffers");
 require(resident.diagnostics().indices==f.original->indices().size(),"Resident GPU IBO contains extra indices (primitive/index count mismatch)");
 require(resident.diagnostics().objects==2&&resident.diagnostics().streams==4&&resident.diagnostics().vertex_uploads==4&&resident.diagnostics().index_uploads==1,"Resident upload did not use exact two-owner/four-stream/shared-IBO contract");
 using Target=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;std::array<Target,3> targets{Target(nullptr,GPU_texture_free),Target(nullptr,GPU_texture_free),Target(nullptr,GPU_texture_free)};constexpr int size=128;int reads=0;double maximum=0;QJsonArray checks;
 for(auto &t:targets){t.reset(GPU_texture_create_2d("Resident raw-world diagnostic",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_ATTACHMENT|GPU_TEXTURE_USAGE_HOST_READ|GPU_TEXTURE_USAGE_SHADER_READ,nullptr));require(bool(t),"Resident diagnostic target allocation failed");}
 const auto read=[&](gpu::Texture *t){auto *raw=static_cast<float *>(GPU_texture_read(t,GPU_DATA_FLOAT,0));require(raw,"Resident diagnostic readback missing");const auto release=qScopeGuard([&]{MEM_delete(raw);});++reads;return std::vector<float>(raw,raw+size*size*4);};
 const auto evaluate=[&](NativeMaterialFragmentGpuProgram &p,const NativeMaterialFragmentGpuGeometry &g,gpu::Texture *t,int tile,const auto &rows){
  GPU_apply_state();const HeldState original;const auto restore=qScopeGuard([&]{original.restore();});gpu::FrameBuffer *held_raw=nullptr;GPU_framebuffer_ensure_config(&held_raw,{GPU_ATTACHMENT_NONE,GPU_ATTACHMENT_TEXTURE(t)});std::unique_ptr<gpu::FrameBuffer,decltype(&GPU_framebuffer_free)> held(held_raw,GPU_framebuffer_free);require(bool(held),"Resident held-state FBO allocation failed");GPU_framebuffer_bind(held.get());GPU_viewport(7,11,29,31);GPU_scissor(3,5,17,19);GPU_blend(GPU_BLEND_ALPHA_PREMULT);GPU_front_facing(false);GPU_scissor_test(false);GPU_apply_state();HeldState expected;expected.front=GL_CW;expected.scissor_on=true;GPU_front_facing(true);GPU_scissor_test(true);
  bool denied=false;try{p.draw_tile(t,g,tile,{});}catch(const std::invalid_argument &){denied=true;}require(denied,"Resident invalid rows admitted");GPU_apply_state();require(HeldState{}==expected,"Resident validation lost held state");GPU_front_facing(true);GPU_scissor_test(true);p.draw_tile(t,g,tile,rows);GPU_apply_state();require(HeldState{}==expected,"Resident tile draw lost held state");return read(t);
 };
 const auto compare=[&](const auto &pixels,const NativeFragmentGeometry &g,int tile,const char *uv,float strength,bool is_bump=false){const auto proof=compare_resident(pixels,size,g,tile,uv,strength,is_bump);maximum=std::max(maximum,proof.maximum);checks.append(QJsonObject{{"tile",tile},{"source_uv",uv},{"bump",is_bump},{"samples",double(proof.samples)},{"maximum_error",proof.maximum}});return proof;};
 const auto rows=normal_map_values(f.named,.65f);std::array<std::vector<float>,3> original;
 for(int i=0;i<3;++i){original[i]=evaluate(program,resident,targets[i].get(),1001+i,rows);const auto proof=compare(original[i],*f.original,1001+i,"NamedUV",.65f);require(proof.owners[i==2?1:0]>32,"Resident second-object/UDIM oracle is vacuous");require(original[i][3]==0,"Resident clear did not preserve uncovered transparent texels");}
 require(resident.diagnostics().object_uploads==2,"Resident actual per-owner ObjectInfos were not uploaded");const auto uploads=resident.diagnostics();
 const auto rendered=evaluate(render_program,resident,targets[1].get(),1002,normal_map_values(f.render,.65f));compare(rendered,*f.original,1002,"RenderUV",.65f);require(rendered!=original[1],"Distinct source render/named UV tangent made no output change");
 for(int tile:{1001,1002}){const auto pixels=evaluate(bump,resident,targets[1].get(),tile,values(f.bump,1,.3f));compare(pixels,*f.original,tile,"NamedUV",1,true);}
 const auto edited=evaluate(program,resident,targets[1].get(),1002,normal_map_values(f.named,1));compare(edited,*f.original,1002,"NamedUV",1);require(edited!=original[1],"Resident parameter edit made no causal output change");
 NativeMaterialFragmentGpuGeometry fresh(f.original);const auto clean=evaluate(program,fresh,targets[1].get(),1002,normal_map_values(f.named,1));require(clean==edited,"Resident cached parameter edit differs from fresh geometry");
 const auto reset=evaluate(program,resident,targets[1].get(),1002,rows);require(reset==original[1],"Resident parameter Undo differs from original pixels");for(int i:{0,2})require(read(targets[i].get())==original[i],"Editing1002 touched another UDIM output");
 {TextureRamQueryScope tiny(+[]()->std::uint64_t{return 1;});NativeMaterialFragmentGpuGeometry accepted(f.revised);}GPU_apply_state();require(HeldState{}==upload_state,"Geometry upload changed caller state");
 NativeMaterialFragmentGpuGeometry revised(f.revised);const auto changed=evaluate(program,revised,targets[1].get(),1002,rows);compare(changed,*f.revised,1002,"NamedUV",.65f);require(changed!=original[1]&&revised.diagnostics().revision==42,"Actual authored geometry revision made no causal output change");
 const auto retained=evaluate(program,resident,targets[1].get(),1002,rows);require(retained==original[1],"Cancelled/refused/replaced geometry changed retained original");
 require(resident.diagnostics().vertex_uploads==uploads.vertex_uploads&&resident.diagnostics().index_uploads==uploads.index_uploads&&resident.diagnostics().object_uploads==uploads.object_uploads,"Resident parameters/tile edits re-uploaded immutable geometry");require(GPU_pass_global_compilation_count()==compiled,"Resident physical/tile/parameter changes recompiled programs");require(reads==13,"Resident actual readback count changed");
 return {{"complete",true},{"checks",checks},{"readbacks",reads},{"maximum_error",maximum},{"tolerance",3e-5},{"objects",2},{"streams",4},{"vertex_uploads",double(uploads.vertex_uploads)},{"index_uploads",double(uploads.index_uploads)},{"gpu_index_count",double(uploads.indices)},{"expected_index_count",double(f.original->indices().size())},{"object_uploads",double(uploads.object_uploads)},{"source_main_destroyed",true},{"source_and_target_UV_separate",true},{"retained_after_cancel_budget_replacement",true},{"cached_fresh_edit_and_Undo_exact",true},{"unrelated_tiles_unchanged",true},{"creation_VAO_buffers_held_pending_state_and_validation_restored",true},{"no_geometry_upload_per_edit",true},{"dynamic_no_recompile",true},{"production_admission",false}};
}

QJsonObject verify_projected_native_normal_gpu(const NativeFragmentGpuFixtures &fixtures){
 using namespace blender;const auto &fixture=*fixtures.resident;NativeMaterialFragmentGpuGeometry geometry(fixture.original);
 NativeFragmentLayout layout;layout.uv_name="NamedUV";layout.tangent_stream=true;layout.resident_raster_origin=true;layout.tangent_output=true;
 NativeMaterialFragmentGpuProgram plain(fixture.named,layout);layout.fill_projection=true;
 NativeMaterialFragmentGpuProgram projected(fixture.named,layout),bump(fixture.bump,layout);
 const auto compilations=GPU_pass_global_compilation_count();constexpr int size=128;
 std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> target(GPU_texture_create_2d("Projected native Normal diagnostic",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 require(bool(target),"Projected Normal target allocation failed");
 TextureSet set;FillSurfaceAtlas atlas;atlas.extent=1;FillProjection fill;
 const auto render=[&](NativeMaterialFragmentGpuProgram &program,const auto &rows,const NativeMaterialProjection *projection){program.draw_tile(target.get(),geometry,1001,rows,{},false,nullptr,nullptr,projection);auto *raw=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(raw,"Projected Normal readback missing");std::vector<float> pixels(raw,raw+size*size*4);MEM_delete(raw);for(float value:pixels)require(std::isfinite(value),"Projected native Normal produced nonfinite output");return pixels;};
 const auto rows=normal_map_values(fixture.named,.65f);const auto identity=native_material_projection(fill,&atlas,set);
 const auto unprojected=render(plain,rows,nullptr),original=render(projected,rows,identity.get());require(original==unprojected,"Identity Fill changed native NormalMap output");
 fill.rotation=90;const auto rotated_projection=native_material_projection(fill,&atlas,set);const auto rotated=render(projected,rows,rotated_projection.get());require(rotated!=original,"UV Fill rotation did not rotate the NormalMap tangent basis");
 fill.rotation=0;fill.linked=false;fill.scale={-1,1};const auto mirrored_projection=native_material_projection(fill,&atlas,set);require(render(projected,rows,mirrored_projection.get())!=original,"Mirrored UV Fill retained the original NormalMap tangent basis");
 fill=FillProjection{};fill.mode=1;const auto triplanar=native_material_projection(fill,&atlas,set);require(render(projected,rows,triplanar.get())!=original,"Triplanar Fill retained the original NormalMap tangent basis");
 require(render(projected,rows,identity.get())==original,"Native Normal projection reset changed original output");
 const auto heights=values(fixture.bump,1,.3f);const auto original_bump=render(bump,heights,identity.get());require(render(bump,heights,rotated_projection.get())!=original_bump,"Projected Bump retained original UV derivatives");require(render(bump,heights,identity.get())==original_bump,"Projected Bump reset changed original output");
 require(GPU_pass_global_compilation_count()==compilations,"Native Normal Fill edits recompiled shaders");
 return {{"complete",true},{"identity_exact",true},{"uv_rotation_mirror_triplanar",true},{"bump_derivatives",true},{"reset_exact",true},{"dynamic_no_recompile",true}};
}
QJsonObject verify_native_normal_stack_gpu(const NativeFragmentGpuFixtures &fixtures){
 using namespace blender;
 constexpr int size=128;
 NativeFragmentLayout tangent;tangent.tangent_output=true;
 NativeMaterialFragmentGpuProgram program(fixtures.bumps[0],tangent);
 const Frame frame{"normal-stack",{1,0,0},{0,1,0}};
 const auto a=draw(program,size,frame,values(fixtures.bumps[0],1,.3f));
 const auto b=draw(program,size,frame,values(fixtures.bumps[0],.25f,.3f));
 // The same tangent perturbation must survive rotated and mirrored surfaces.
 for(const auto &basis:{Frame{"rotated",{0,1,0},{-1,0,0}},Frame{"mirrored",{-1,0,0},{0,1,0}}}){
  const auto rotated=draw(program,size,basis,values(fixtures.bumps[0],1,.3f));
  for(int y=4;y<size-4;++y)for(int x=4;x<size-4;++x)for(int c=0;c<4;++c)require(std::abs(rotated[(y*size+x)*4+c]-a[(y*size+x)*4+c])<3e-5,"Target tangent conversion depends on world orientation");
 }
 GpuLayerPreview preview;
 auto graph=fixtures.bumps[0].source;graph->world_normal_tree=fixtures.bumps[0].tree;graph->world_normal_perturbed=true;
 preview.install_native_material("normal-stack-fixture","normal-stack-fixture",graph);
 TextureSet set;set.texture_resolution=size;set.channels[3]=false;set.precision[3]=ChannelPrecision::RGB32F;set.precision[4]=ChannelPrecision::L32F;
 TextureLayer first,second;first.kind=second.kind=LayerKind::Fill;first.fill.material_resource=second.fill.material_resource="normal-stack-fixture";first.fill.native_material=second.fill.native_material=std::make_shared<const NativeMaterialDescription>(graph->description);first.opacity[4]=.6f;second.opacity[4]=.4f;set.layers={first,second};
 const auto texture=[&](const std::vector<float> &pixels){return std::shared_ptr<gpu::Texture>(GPU_texture_create_2d("Normal stack fixture",size,size,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,pixels.data()),GPU_texture_free);};
 preview.set_native_normal(set.id,first.id,1001,texture(a));preview.set_native_normal(set.id,second.id,1001,texture(b));
 auto output=texture(a);
 const auto render=[&]{for(int i=0;i<1000;++i){preview.begin_frame();const auto status=preview.update(set,6,output.get(),1001);if(status==GpuPreviewUpdate::Updated){auto *raw=static_cast<float *>(GPU_texture_read(output.get(),GPU_DATA_FLOAT,0));std::vector<float> result(raw,raw+size*size*4);MEM_delete(raw);return result;}require(status!=GpuPreviewUpdate::Unsupported,"Native normal layer stack rejected");}throw std::runtime_error("Native normal layer stack stalled");};
 const auto mixed=render();double max_error=0;
 for(int y=4;y<size-4;++y)for(int x=4;x<size-4;++x){const int i=(y*size+x)*4;std::array<float,3> expected;for(int c=0;c<3;++c)expected[c]=(a[i+c]*2-1)*.6f*.6f+(b[i+c]*2-1)*.4f+(c==2?.24f:0);const auto n=normalized_normal(expected);for(int c=0;c<3;++c){const auto error=std::abs(mixed[i+c]-(n[c]*.5f+.5f));max_error=std::max(max_error,double(error));require(error<3e-5,"Two native normal layers lost opacity/composition");}}
 set.layers[1].visible=false;const auto hidden=render();require(hidden!=mixed,"Hiding native Normal layer had no effect");
 set.layers[1].visible=true;require(render()==mixed,"Normal layer restore differs");
 set.layers[1].opacity[4]=0;require(render()==hidden,"Zero native normal opacity differs from hidden");
 set.layers[1].opacity[4]=.4f;set.layers[1].mask=LayerMask{};set.layers[1].mask->background=0;require(render()==hidden,"Black mask did not suppress normal layer");
 preview.reset(true);GpuLayerPreview::collect_retired(true);
 return {{"complete",true},{"layers",2},{"maximum_error",max_error},{"rotated_mirrored",true},{"opacity_visibility_mask",true},{"scalar_conversion_in_viewport",false},{"fill_projection",verify_projected_native_normal_gpu(fixtures)}};
}

}
QJsonObject verify_native_fragment_normal_gpu(const NativeFragmentGpuFixtures &fixtures){
 // A focused, small-texture entry point for resident geometry regressions.
 // The broader shader admission suite remains available without this selector.
 if(qEnvironmentVariableIsSet("BTS_NATIVE_FRAGMENT_RESIDENT_ONLY")){
  auto proof=verify_resident_geometry(fixtures);
  proof["scope"]="Resident indexed geometry: actual IBO count, pixels, cache, Undo and GPU state";
  return proof;
 }
 require(fixtures.image_normals.size()==2,"Image-backed NormalMap/Bump GPU fixtures missing");
 for(const auto &source:fixtures.image_normals){NativeMaterialFragmentGpuProgram program(source,native_world_normal_source_request(source));require(program.diagnostics().image_context,"NormalMap/Bump GPU regression did not compile its image sampler");}
 using namespace blender;require(fixtures.bumps.size()==3,"Prepared fragment source cohort missing");const std::array frames={Frame{"ordinary",{1,0,0},{0,1,0}},Frame{"mirrored",{-1,0,0},{0,1,0}},Frame{"rotated",{0,1,0},{-1,0,0}}};
 struct Variant{const char *name;int source;float strength,distance,width;bool invert;};const std::array variants={Variant{"strength0",0,0,.3f,.5f,false},Variant{"strength1",0,1,.3f,.5f,false},Variant{"strength_partial",0,.35f,.3f,.5f,false},Variant{"distance0",0,1,0,.5f,false},Variant{"invert",1,1,.3f,.5f,true},Variant{"wide",2,1,.3f,4,false}};
 std::vector<std::unique_ptr<NativeMaterialFragmentGpuProgram>> programs;for(const auto &owner:fixtures.bumps)programs.push_back(std::make_unique<NativeMaterialFragmentGpuProgram>(owner));const auto compiles=GPU_pass_global_compilation_count();QJsonArray cases;double maximum=0;std::array<double,2> aligned{};int reads=0;
 for(int size:{64,128})for(const auto &frame:frames)for(const auto &v:variants){auto &program=*programs[v.source];std::printf("BTS_PREPARED_FRAGMENT_CASE frame=%s size=%d variant=%s\n",frame.name,size,v.name);std::fflush(stdout);const auto pixels=draw(program,size,frame,values(fixtures.bumps[v.source],v.strength,v.distance));++reads;const auto error=compare(pixels,size,frame,v.strength,v.distance,v.width,v.invert);maximum=std::max(maximum,error);if(std::string_view(frame.name)=="ordinary"&&v.source==2)aligned[size==64?0:1]=pixels[((size==64?15:31)*size+(size==64?31:63))*4];cases.append(QJsonObject{{"frame",frame.name},{"size",size},{"variant",v.name},{"maximum_error",error},{"active_bumps",program.diagnostics().active_bumps},{"codegen_invoked",program.diagnostics().codegen_invoked},{"material_functions",program.diagnostics().material_functions}});}
 require(GPU_pass_global_compilation_count()==compiles,"Prepared fragment dynamic controls recompiled shader");require(std::abs(aligned[0]-aligned[1])>.001,"Prepared fragment fixture lost the finite UV-pixel resolution witness");
 NativeMaterialFragmentGpuProgram geometry(fixtures.geometry);const auto gp=draw(geometry,64,frames[0],values(fixtures.geometry,1,.3));++reads;maximum=std::max(maximum,compare(gp,64,frames[0],1,.3,.5,false,true));require(geometry.diagnostics().active_bumps==0,"Geometry-only Normal classified as active Bump");
 NativeFragmentLayout named_layout;named_layout.uv_name="FixtureUV";NativeMaterialFragmentGpuProgram named(fixtures.named,named_layout);const auto np=draw(named,64,frames[0],values(fixtures.named,1,.3));++reads;maximum=std::max(maximum,compare(np,64,frames[0],1,.3,.5,false));
 // Dynamic Mapping is applied to the retained UV source before H=u², not to
 // output normals. An independent derivative distinguishes that order.
 auto mapping_values=values(fixtures.bumps[0],1,.3);std::vector<NativeMaterialMapping> mappings(mapping_values.size());bool mapped=false;
 for(std::size_t i=0;i<mappings.size();++i)if(fixtures.bumps[0].source->description.parameters[i].mapping){mappings[i].scale[0]=2;mappings[i].offset[0]=.125;mapped=true;}
 require(mapped&&programs[0]->diagnostics().mapping_rows,"Prepared fixture lost retained Mapping rows");const auto mapping_compiles=GPU_pass_global_compilation_count();const auto mp=draw(*programs[0],64,frames[0],mapping_values,mappings);++reads;maximum=std::max(maximum,compare(mp,64,frames[0],1,.3,.5,false,false,2,.125));
 const auto mapping_reset=draw(*programs[0],64,frames[0],mapping_values);++reads;maximum=std::max(maximum,compare(mapping_reset,64,frames[0],1,.3,.5,false));require(mapping_reset!=mp&&GPU_pass_global_compilation_count()==mapping_compiles,"Dynamic Mapping made no change or recompiled");
 require(fixtures.unsupported.size()==11,"Prepared phase1+phase2 source packet differs");QJsonArray rejected;for(const auto &[expected,owner]:std::span(fixtures.unsupported).first(6)){bool refused=false;QString reason;try{NativeMaterialFragmentGpuProgram unsupported(owner);}catch(const std::invalid_argument&e){reason=QString::fromUtf8(e.what());refused=std::string_view(e.what()).find(expected)!=std::string_view::npos;}require(refused,"Unsupported fragment context did not fail with its specific admission reason");rejected.append(QJsonObject{{"expected",QString::fromStdString(expected)},{"reason",reason}});}
 NativeMaterialFragmentGpuProgram ramp(fixtures.ramp);auto original=fixtures.ramp.source->description,edited=original;for(auto &p:edited.parameters)if(!p.ramp.isEmpty()){auto stops=p.ramp["stops"].toArray();for(int i=0;i<stops.size();++i){auto stop=stops[i].toObject();stop["color"]=QJsonArray{.75,.75,.75,1};stops[i]=stop;}p.ramp["stops"]=stops;}
 const auto rv=values(fixtures.ramp,1,.3);const auto before=draw(ramp,64,frames[0],rv);++reads;maximum=std::max(maximum,compare(before,64,frames[0],.5,.3,.5,false));const auto ramp_compiles=GPU_pass_global_compilation_count();ramp.update_ramps(edited);const auto after=draw(ramp,64,frames[0],rv);++reads;maximum=std::max(maximum,compare(after,64,frames[0],.75,.3,.5,false));require(before!=after,"Prepared ramp update made no causal Normal change");ramp.update_ramps(original);const auto reset=draw(ramp,64,frames[0],rv);++reads;require(reset==before&&GPU_pass_global_compilation_count()==ramp_compiles,"Prepared ramp reset changed cached output or recompiled");
 const auto normal_map_proof=verify_normal_map_bindings(fixtures);const auto resident_proof=verify_resident_geometry(fixtures);return {{"image_normal_programs",int(fixtures.image_normals.size())},{"native_normal_stack",verify_native_normal_stack_gpu(fixtures)},{"resident_geometry",resident_proof},{"normal_map_bindings",normal_map_proof},{"complete",true},{"scope","Prepared Principled Normal API: retained image NormalMap/Bump shader compilation and image-free UV-pixel/world-vector rendering"},{"cases",cases},{"main_cases",36},{"diagnostic_readbacks",reads},{"maximum_error",maximum},{"tolerance",3e-5},{"unsupported",rejected},{"ramp_causal_reset_no_recompile",true},{"pinned_ramp_lut_format","RGBA16F"},{"ramp_edit_strength",.75},{"ramp_fixture_choice",".5 and .75 are exact in pinned half-float LUT and float32; not a general ramp precision oracle"},{"mapping_causal_reset_no_recompile",true},{"held_native_state_restored",true},{"pending_front_scissor_restored",true},{"validation_throw_state_preserved",true},{"same_uv_world_x_64",aligned[0]},{"same_uv_world_x_128",aligned[1]},{"production_admission",false},{"normal_map_supported",false}};
}
}
