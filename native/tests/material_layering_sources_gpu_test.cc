// SPDX-License-Identifier: GPL-3.0-or-later
// Explicit owner-context backend diagnostics; no artist map/Normal UI.
#include "MaterialLayeringSourcesGpuTap.hh"
#include "MaterialLayeringCyclesSourceProbe.hh"
#include "BlenderHost.hh"
#include "MaterialLayerShader.hh"
#include "MaterialLayeringSources.hh"
#include "GpuLayerPreview.hh"
#include "GpuPerformance.hh"
#include "BKE_global.hh"
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BLI_math_matrix.hh"
#include "IMB_imbuf.hh"
#include "GPU_pass.hh"
#include "opencolorio/OCIO_matrix.hh"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <exception>
#include "MaterialLayeringSourcesHostTest.hh"
namespace bts {namespace {
using namespace blender;using namespace pd149_sources_test;
using Buffer=std::unique_ptr<ImBuf,decltype(&IMB_freeImBuf)>;
using MainOwner=std::unique_ptr<Main,decltype(&BKE_main_free)>;
constexpr int width=8,height=4;
constexpr std::array<float,3> masks{.25f,.5f,.125f};
std::shared_ptr<const FillTexture> source(int w,int h,bool raw,char tag){
 auto value=std::make_shared<FillTexture>();value->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();value->hash=std::string(64,tag);value->name="Private source "+std::string(1,tag);value->raw=raw;value->width=w;value->height=h;auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(std::size_t(w)*h);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto &p=(*pixels)[std::size_t(y)*w+x];if(tag=='a')p={-.25f+.2f*x,70000.f,-3,0};else if(tag=='b')p={.1f+.2f*x+.05f*y,-9,7,0};else if(tag=='c')p={-2.25f+.5f*x+.25f*y,70000.f,5,0};else p={-.1f+.07f*x,.3f+.04f*y,.2f+.02f*x+.03f*y,0};}value->pixels=std::move(pixels);value->validate();return value;
}
// Independent repeating bilinear reference: alpha is deliberately ignored.
std::array<double,3> sample(const FillTexture &s,const LayerMaterialInput &input,double u,double v){
 const double angle=input.rotation*3.14159265358979323846/180,c=std::cos(angle),n=std::sin(angle),a=u*input.tiling[0],b=v*input.tiling[1];u=c*a-n*b+input.offset[0];v=n*a+c*b+input.offset[1];u-=std::floor(u);v-=std::floor(v);const double x=u*s.width-.5,y=v*s.height-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;std::array<double,3> result{};
 for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const int xx=((ix+dx)%s.width+s.width)%s.width,yy=((iy+dy)%s.height+s.height)%s.height;const auto &p=(*s.pixels)[std::size_t(yy)*s.width+xx];const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);for(int k=0;k<3;++k)result[k]+=weight*p[k];}return result;
}
// This NEW internal-Normal diagnostic models the observed NVIDIA RTX filtering
// weights. It is not a universal OpenGL accuracy contract. The ideal sampler
// above and every existing Color/scalar/Bump comparison remain unchanged.
std::array<double,3> sample_internal_normal_q8(const FillTexture &s,const LayerMaterialInput &input,double u,double v){
 const double angle=input.rotation*3.14159265358979323846/180,c=std::cos(angle),n=std::sin(angle),a=u*input.tiling[0],b=v*input.tiling[1];u=c*a-n*b+input.offset[0];v=n*a+c*b+input.offset[1];u-=std::floor(u);v-=std::floor(v);const double x=u*s.width-.5,y=v*s.height-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=std::floor((x-ix)*256+.5)/256,fy=std::floor((y-iy)*256+.5)/256;std::array<double,3> result{};
 for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){const int xx=((ix+dx)%s.width+s.width)%s.width,yy=((iy+dy)%s.height+s.height)%s.height;const auto &pixel=(*s.pixels)[std::size_t(yy)*s.width+xx];const double weight=(dx?fx:1-fx)*(dy?fy:1-fy);for(int k=0;k<3;++k)result[k]+=weight*pixel[k];}return result;
}
std::array<double,3> multiply(const float3x3 &m,const std::array<double,3> &v){std::array<double,3> out{};for(int row=0;row<3;++row)for(int column=0;column<3;++column)out[row]+=double(m[column][row])*v[column];return out;}
std::array<float,9> uniform_matrix(const float3x3 &m){std::array<float,9> out{};for(int column=0;column<3;++column)for(int row=0;row<3;++row)out[column*3+row]=m[column][row];return out;}
struct State {
 MainOwner main{BKE_main_new(),BKE_main_free};GpuLayerPreview pool;MaterialLayeringSources sources;
 std::array<TextureSet,2> sets;std::array<Material *,2> materials{};std::array<MaterialLayerShader,2> shaders;
 std::shared_ptr<const FillTexture> data=source(8,4,true,'a'),rough=source(4,2,true,'b'),height_map=source(2,4,true,'c'),color=source(8,4,false,'d');
 std::unique_ptr<MaterialLayeringCyclesSourceProbe> reader;Buffer save_header{nullptr,IMB_freeImBuf};std::weak_ptr<const std::vector<FillTexture::Pixel>> retired_pixels;std::vector<std::array<float,4>> retained_expected;
 int stage=0,ticks=0;QJsonObject proof;std::uint64_t original_allocations=0;Image *shared_image=nullptr;gpu::Texture *shared_texture=nullptr;bool cleaned=false;
 std::array<std::shared_ptr<const FillTexture>,3> normal_masks;
 std::shared_ptr<const FillTexture> normal_geometry;
 QJsonArray normal_rows;
 State(){
  require(main&&G_MAIN,"Source test requires initialized Host and private Main");main->colorspace=G_MAIN->colorspace;
  for(int set=0;set<2;++set){auto *material=BKE_material_add(main.get(),set?"Private source set B":"Private source set A");require(material&&material->nodetree,"Private material missing");materials[set]=material;sets[set].material_session_uid=material->id.session_uid;sets[set].texture_resolution=8;
   auto &tree=*material->nodetree;auto *surface=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled")),*output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));require(surface&&output,"Canonical private surface missing");BKE_ntree_update_after_single_tree_change(*main,tree);bke::node_add_link(tree,*surface,pd149_test::output(*surface,"BSDF"),*output,pd149_test::input(*output,"Surface"));
   auto &model=sets[set].material_layering;model.enabled=true;model.mask_slots={0,1,2};for(int i=0;i<3;++i){model.mask_roots[i]=QUuid::createUuid();const float p[4]={masks[i],masks[i],masks[i],1};auto *image=native_generated_image(main.get(),2,2,("Private opacity "+std::to_string(set)+"/"+std::to_string(i)).c_str(),32,1,IMA_GENTYPE_BLANK,p,false,true,false);require(image,"Private opacity Image allocation failed");image->flag|=IMA_HIGH_BITDEPTH;prepare_native_gpu_image(image);GPU_texture_clear(image->runtime->gputexture[TEXTARGET_2D][0],GPU_DATA_FLOAT,p);auto *node=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexImage"));require(node,"Opacity sampler missing");STRNCPY(node->name,("__BTS_UserPixels"+std::to_string(i)).c_str());node->id=&image->id;id_us_plus(node->id);}
   for(int i=0;i<4;++i){model.materials[i].values.base_color={.1f+.05f*i,.2f+.03f*i,.3f+.02f*i,1};model.materials[i].values.metallic=.2f*i;model.materials[i].values.roughness=.15f+.2f*i;model.materials[i].values.ambient_occlusion=.25f+.2f*i;model.materials[i].values.bump=0;}
   for(int channel:{1,2,5})model.materials[0].textures[channel]=data;model.validate();BKE_ntree_update_after_single_tree_change(*main,tree);
  }
  proof={{"renderer_required","actual owner GL"},{"scope","private backend; maps UI remains disabled"},{"two_persistence_representations",true},{"cold_open_id_reuse_not_tested",true},{"physical_oom_tested",false},{"performance_claim",false}};
 }
 std::array<const MaterialLayering *,2> models()const{return {&sets[0].material_layering,&sets[1].material_layering};}
 bool prepare(){pool.begin_frame();const auto group=models();return sources.prepare(*main,pool,group);}
 // Separate internal/legacy Normal prerequisite. These CPU fixtures are the
 // independently retained inputs, not samples read back from the apply graph.
 void initialize_internal_normal(){
  auto normal=std::make_shared<FillTexture>(*source(4,4,true,'d'));
  auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(16);
  for(int y=0;y<4;++y)for(int x=0;x<4;++x)(*pixels)[std::size_t(y)*4+x]={.25f+.05f*x,.35f+.04f*y,.85f-.02f*x+.01f*y,float((x+y)%3)*.5f};
  normal->pixels=std::move(pixels);normal->validate();data=normal;
  for(int slot=0;slot<3;++slot){
   auto mask=std::make_shared<FillTexture>(*source(2,2,true,char('a'+slot)));auto values=std::make_shared<std::vector<FillTexture::Pixel>>(4);
   const float levels[3][4]{{.125f,.75f,.375f,.875f},{.875f,.25f,.625f,.125f},{.25f,.5f,.875f,.375f}};
   for(int p=0;p<4;++p)(*values)[p]={levels[slot][p],levels[slot][p],levels[slot][p],1};mask->pixels=std::move(values);mask->validate();normal_masks[slot]=mask;
  }
  auto geometry=std::make_shared<FillTexture>(*source(2,2,true,'e'));auto bases=std::make_shared<std::vector<FillTexture::Pixel>>();
  *bases={{{.35f,.55f,.9f,0}},{{.6f,.45f,.85f,1}},{{.4f,.65f,.95f,.25f}},{{.55f,.35f,.875f,.75f}}};geometry->pixels=std::move(bases);geometry->validate();normal_geometry=geometry;
  for(int set=0;set<2;++set){
   auto &model=sets[set].material_layering;for(auto &input:model.materials){input.textures.fill({});input.values.bump=.5f;}
   model.materials[0].textures[3]=data;model.materials[1].textures[3]=data;
   model.materials[2].values.normal={.2f,-.3f,.9f};model.materials[3].values.normal={-.15f,.25f,.95f};
   auto &tree=*materials[set]->nodetree;
   for(int slot=0;slot<3;++slot){auto *node=bke::node_find_node_by_name(tree,("__BTS_UserPixels"+std::to_string(slot)).c_str());require(node&&node->id,"Normal fixture opacity sampler missing");auto *image=reinterpret_cast<Image *>(node->id);GPU_texture_update(image->runtime->gputexture[TEXTARGET_2D][0],GPU_DATA_FLOAT,normal_masks[slot]->pixels->data());BKE_image_gpu_override_updated(image);}
   const float flat[4]={.5,.5,1,1};auto *image=native_generated_image(main.get(),2,2,("Private authored Normal "+std::to_string(set)).c_str(),32,1,IMA_GENTYPE_BLANK,flat,false,true,false);require(image,"Internal Normal base allocation failed");image->flag|=IMA_HIGH_BITDEPTH;image->alpha_mode=IMA_ALPHA_CHANNEL_PACKED;prepare_native_gpu_image(image);GPU_texture_update(image->runtime->gputexture[TEXTARGET_2D][0],GPU_DATA_FLOAT,normal_geometry->pixels->data());BKE_image_gpu_override_updated(image);
   auto *node=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexImage"));require(node,"Internal Normal base sampler missing");STRNCPY(node->name,"__BTS_AuthoredNormalPixels");node->id=&image->id;id_us_plus(node->id);BKE_ntree_update_after_single_tree_change(*main,tree);
  }
  proof={{"scope","Internal/legacy encoded RNM input before NormalMap; not final tangent/surface response"},{"reference_model","Independent nearest-1/256 bilinear fractional weights, observed NVIDIA RTX model; ideal retained separately"},{"universal_OpenGL_filter_precision_claim",false},{"artist_Normal_UI",false},{"Normal_export",false},{"Bump_grayscale_unchanged",true},{"persistence_tested",false},{"performance_claim",false}};
 }
 // Reference uses algebra over source values, never production nodes, helper
 // weight functions or GPU readback as expected values.
 std::array<double,3> internal_normal_reference(const MaterialLayering &model,bool directx,double u,double v,bool q8)const{
  const auto source_sample=[&](const FillTexture &source,const LayerMaterialInput &input){return q8?sample_internal_normal_q8(source,input,u,v):sample(source,input,u,v);};
  LayerMaterialInput identity;std::array<double,3> opacity;for(int i=0;i<3;++i)opacity[i]=std::clamp(source_sample(*normal_masks[i],identity)[0],0.,1.);
  const auto a=opacity[0],b=opacity[1],c=opacity[2];const std::array<double,4> weights{(1-a)*(1-b)*(1-c),a*(1-b)*(1-c),b*(1-c),c};std::array<double,3> detail{};
  for(int i=0;i<4;++i){const auto &input=model.materials[i];std::array<double,3> vector;
   if(input.textures[3]){auto encoded=source_sample(*input.textures[3],input);if(directx)encoded[1]=1-encoded[1];for(int k=0;k<3;++k)vector[k]=2*encoded[k]-1;}
   else for(int k=0;k<3;++k)vector[k]=input.values.normal[k];
   const double radians=-input.rotation*3.14159265358979323846/180,co=std::cos(radians),si=std::sin(radians),x=vector[0],y=vector[1];vector[0]=(co*x-si*y)*(input.tiling[0]<0?-1 : 1);vector[1]=(si*x+co*y)*(input.tiling[1]<0?-1 : 1);
   for(int k=0;k<3;++k)detail[k]+=weights[i]*vector[k];
  }
  auto base=source_sample(*normal_geometry,identity);for(auto &component:base)component=2*component-1;
  const std::array<double,3> t{base[0],base[1],base[2]+1},n{-detail[0],-detail[1],detail[2]};const double ratio=(t[0]*n[0]+t[1]*n[1]+t[2]*n[2])/std::max(t[2],1e-6);std::array<double,3> encoded;
  for(int k=0;k<3;++k)encoded[k]=(t[k]*ratio-n[k])*.5+.5;return encoded;
 }
 void internal_normal_case(int which){
  const bool directx=which%2,mirrored=which>=2;
  for(int set=0;set<2;++set){auto &model=sets[set].material_layering;
   model.materials[0].tiling=mirrored?std::array<double,2>{-1,2}:std::array<double,2>{1,1};model.materials[0].rotation=mirrored?37:0;model.materials[0].offset={.125,-.25};
   model.materials[1].tiling=mirrored?std::array<double,2>{1,-2}:std::array<double,2>{2,1};model.materials[1].rotation=mirrored?-29:90;model.materials[1].offset={-.25,.125};
   model.materials[2].rotation=set?45:0;model.materials[2].tiling={set?-1.:1.,1};model.materials[3].rotation=set?-90:0;model.materials[3].tiling={1,set?-1.:1.};
   const QJsonValue uploads=gpu_transfer_snapshot()["upload"];require(prepare(),"Normal transform/convention edit lost source readiness");require(gpu_transfer_snapshot()["upload"]==uploads,"Normal transform/convention edit uploaded source pixels");const auto images=sources.images(model);require(bool(images),"Internal Normal source not ready");shaders[set].apply(*main,*materials[set],model,1,1,directx,*images);
   require((*images)[0][3]==shared_image&&(*images)[1][3]==shared_image&&shared_image->runtime->gputexture[TEXTARGET_2D][0]==shared_texture,"Convention/transform edit replaced shared Normal allocation");
   SourceGpuTap tap(*materials[set],Output::InternalNormal,width,height);const auto compilations=GPU_pass_global_compilation_count();
   for(int tile:{1001,1002}){const auto actual=tap.sample(tile);double maximum=0,ideal_maximum=0;std::uint64_t nonfinite=0,components=0;QJsonObject worst,ideal_worst;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const double u=(x+.5)/width+(tile-1001),v=(y+.5)/height;const auto expected=internal_normal_reference(model,directx,u,v,true),ideal=internal_normal_reference(model,directx,u,v,false);const auto &pixel=actual[std::size_t(y)*width+x];
     const auto witness=[&](int component,QJsonValue error){QJsonArray rgba,reference,ideal_reference;for(float value:pixel)rgba.append(std::isfinite(value)?QJsonValue(value):QJsonValue(std::isnan(value)?"NaN":value<0?"-Infinity":"+Infinity"));for(double value:expected)reference.append(value);for(double value:ideal)ideal_reference.append(value);return QJsonObject{{"case",which},{"set",set},{"material_uid",qint64(materials[set]->id.session_uid)},{"directx",directx},{"mirrored",mirrored},{"tile",tile},{"x",x},{"y",y},{"uv",QJsonArray{u,v}},{"actual_rgba",rgba},{"expected_encoded_RNM",reference},{"ideal_expected_encoded_RNM",ideal_reference},{"component",component},{"absolute_error",error},{"comparison_model","NVIDIA_observed_q8"}};};
     for(int k=0;k<3;++k)require(std::isfinite(expected[k])&&std::isfinite(ideal[k]),"Internal Normal CPU reference is not finite");
     for(int k=0;k<4;++k)if(!std::isfinite(pixel[k])){if(!nonfinite)worst=witness(k,"nonfinite GPU value");++nonfinite;}
     for(int k=0;k<3;++k){++components;if(!std::isfinite(pixel[k]))continue;const double error=std::abs(double(pixel[k])-expected[k]),ideal_error=std::abs(double(pixel[k])-ideal[k]);if(error>maximum||worst.isEmpty()){maximum=std::max(maximum,error);if(!nonfinite)worst=witness(k,error);}if(ideal_error>ideal_maximum||ideal_worst.isEmpty()){ideal_maximum=std::max(ideal_maximum,ideal_error);ideal_worst=witness(k,ideal_error);ideal_worst["comparison_model"]="ideal_bilinear";}}
    }
    normal_rows.append(QJsonObject{{"case",which},{"set",set},{"directx",directx},{"mirrored",mirrored},{"tile",tile},{"components",qint64(components)},{"maximum_abs_error",maximum},{"maximum_ideal_abs_error",ideal_maximum},{"nonfinite_gpu_values",qint64(nonfinite)},{"worst",worst},{"ideal_worst",ideal_worst}});proof["rows"]=normal_rows;proof["tolerance"]=3e-5;
    require(!nonfinite,"Internal Normal graph produced nonfinite GPU values");require(maximum<=3e-5,"Internal Normal encoded RNM oracle exceeded unchanged 3e-5");require(GPU_pass_global_compilation_count()==compilations,"Unchanged internal Normal sampling recompiled the tap");
   }
  }
  require(pool.texture_resource_allocations()==original_allocations,"Normal convention/transform allocated another source");
 }
 bool advance_internal_normal(){
  ++ticks;proof["last_stage"]=stage;std::printf("PD149_INTERNAL_NORMAL_STAGE stage=%d tick=%d\n",stage,ticks);std::fflush(stdout);
  if(stage==0){initialize_internal_normal();++stage;return false;}
  if(stage==1){if(!prepare())return false;const auto a=sources.images(sets[0].material_layering),b=sources.images(sets[1].material_layering);require(a&&b,"Internal Normal source bindings missing");shared_image=(*a)[0][3];require(shared_image&&(*a)[1][3]==shared_image&&(*b)[0][3]==shared_image&&(*b)[1][3]==shared_image,"Same raw Normal source was duplicated");shared_texture=shared_image->runtime->gputexture[TEXTARGET_2D][0];require(shared_texture,"Internal Normal source allocation missing");original_allocations=pool.texture_resource_allocations();require(original_allocations==1,"Internal Normal source was repacked/duplicated");proof["sharing"]=QJsonObject{{"sets",2},{"references",4},{"source_allocations",1},{"image_aliases",1},{"width",4},{"height",4}};++stage;return false;}
  internal_normal_case(stage-2);++stage;return stage==6;
 }
 void bind(){for(int i=0;i<2;++i){const auto images=sources.images(sets[i].material_layering);require(bool(images),"Complete source bindings missing");shaders[i].apply(*main,*materials[i],sets[i].material_layering,1,1,false,*images);}}
 void check_alias(){
  const auto a=sources.images(sets[0].material_layering),b=sources.images(sets[1].material_layering);require(a&&b,"Shared source aliases unavailable");shared_image=(*a)[0][1];require(shared_image&&(*a)[0][2]==shared_image&&(*a)[0][5]==shared_image&&(*b)[0][1]==shared_image&&(*b)[0][2]==shared_image&&(*b)[0][5]==shared_image,"Inputs/sets duplicated the source Image");
  auto lease=pool.retain_texture_resource(*data);require(bool(lease),"Shared source lease missing");shared_texture=lease->texture;require(lease->width==8&&lease->height==4&&shared_image->runtime->gputexture[TEXTARGET_2D][0]==shared_texture,"Alias does not use original pool dimensions/allocation");
  const QJsonValue uploads=gpu_transfer_snapshot()["upload"];const auto allocations=pool.texture_resource_allocations();auto cached=BKE_image_get_gpu_material_texture(shared_image,nullptr,true);require(cached.texture&&*cached.texture==shared_texture&&pool.texture_resource_allocations()==allocations&&gpu_transfer_snapshot()["upload"]==uploads,"Ordinary BKE getter uploaded/replaced shared source");
 }
 void numerical(bool rec709){
  main->colorspace.scene_linear_to_xyz=rec709?math::invert(ocio::XYZ_TO_REC709):ocio::ACESCG_TO_XYZ;STRNCPY(main->colorspace.scene_linear_name,rec709?"Linear Rec.709":"ACEScg");bind();
  // Independent direct multiplication of pinned standard XYZ matrices, rather
  // than reading the production-generated nodes or their uniform values.
  const float3x3 from_ap1=rec709?ocio::XYZ_TO_REC709*ocio::ACESCG_TO_XYZ:float3x3::identity();const float3x3 to_ap1=math::invert(ocio::ACESCG_TO_XYZ)*main->colorspace.scene_linear_to_xyz;
  const double a=masks[0],b=masks[1],c=masks[2];const std::array<double,4> w{(1-a)*(1-b)*(1-c),a*(1-b)*(1-c),b*(1-c),c};double max_error=0;std::uint64_t components=0;
  struct OutputError {double maximum=0;std::uint64_t components=0,nonfinite=0;bool have_finite=false;QJsonObject worst;};
  std::array<OutputError,12> output_errors;QJsonObject global_worst;std::uint64_t nonfinite_gpu=0;bool have_finite=false;
  const auto json_number=[](double value)->QJsonValue{if(std::isfinite(value))return value;if(std::isnan(value))return QString("NaN");return QString(std::signbit(value)?"-Infinity":"+Infinity");};
  const auto observe=[&](int set,Output which,int tile,int x,int y,bool canonical,const std::array<float,4> &actual,const std::array<double,3> &expected){
   const int slot=set*6+(which==Output::Color?(canonical?1:0):int(which)+1);auto &row=output_errors[slot];
   const auto witness=[&](int component,double error,bool invalid){
    const char *names[]={"Color","Metallic","Roughness","AO","Height"};QJsonArray gpu,reference;for(float value:actual)gpu.append(json_number(value));for(double value:expected)reference.append(json_number(value));
    return QJsonObject{{"scene_space",main->colorspace.scene_linear_name},{"set",set},{"material_uid",qint64(sets[set].material_session_uid)},{"output",names[int(which)]},{"tile",tile},{"x",x},{"y",y},{"uv",QJsonArray{(x+.5)/width+(tile-1001),(y+.5)/height}},{"comparison_domain",which==Output::Color?(canonical?"canonical_AP1":"scene_linear"):"data"},{"actual_rgba",gpu},{"expected_rgb",reference},{"component",component},{"absolute_error",invalid?QJsonValue("nonfinite GPU value"):json_number(error)},{"nonfinite_gpu",invalid}};
   };
   for(int component=0;component<4;++component)if(!std::isfinite(actual[component])){
    ++row.nonfinite;++nonfinite_gpu;if(row.nonfinite==1)row.worst=witness(component,0,true);if(nonfinite_gpu==1)global_worst=witness(component,0,true);
   }
   for(int component=0;component<3;++component){
    ++row.components;++components;if(!std::isfinite(actual[component]))continue;
    const double error=std::abs(double(actual[component])-expected[component]);
    if(!row.have_finite||error>row.maximum){row.maximum=error;row.have_finite=true;if(!row.nonfinite)row.worst=witness(component,error,false);}
    if(!have_finite||error>max_error){max_error=error;have_finite=true;if(!nonfinite_gpu)global_worst=witness(component,error,false);}
   }
  };
  for(int set=0;set<2;++set){const auto &model=sets[set].material_layering;
   for(Output which:{Output::Color,Output::Metallic,Output::Roughness,Output::AO,Output::Height}){
    SourceGpuTap tap(*materials[set],which,width,height);const auto compiled=GPU_pass_global_compilation_count();
    for(int tile:{1001,1002}){const auto pixels=tap.sample(tile);const auto canonical=which==Output::Color?tap.sample(tile,uniform_matrix(to_ap1)):std::vector<std::array<float,4>>{};
     for(int y=0;y<height;++y)for(int x=0;x<width;++x){std::array<double,3> expected{};double ao=0;for(int i=0;i<4;++i){const auto &input=model.materials[i];const double u=(x+.5)/width+(tile-1001),v=(y+.5)/height;double a=input.values.ambient_occlusion;if(input.textures[5])a=std::clamp(sample(*input.textures[5],input,u,v)[0],0.,1.);ao+=w[i]*a;
       if(which==Output::Color){std::array<double,3> value{input.values.base_color[0],input.values.base_color[1],input.values.base_color[2]};if(input.textures[0])value=multiply(from_ap1,sample(*input.textures[0],input,u,v));for(int c=0;c<3;++c)expected[c]+=w[i]*value[c];}
       else if(which!=Output::AO){const int channel=which==Output::Metallic?1:which==Output::Roughness?2:4;double value=channel==1?input.values.metallic:channel==2?input.values.roughness:input.values.bump;if(input.textures[channel])value=sample(*input.textures[channel],input,u,v)[0];if(channel!=4)value=std::clamp(value,0.,1.);for(auto &c:expected)c+=w[i]*value;}}
      if(which==Output::Color)for(auto &c:expected)c*=ao;else if(which==Output::AO)expected.fill(ao);const auto index=std::size_t(y)*width+x;
      observe(set,which,tile,x,y,false,pixels[index],expected);if(which==Output::Color){const auto target=multiply(to_ap1,expected);observe(set,which,tile,x,y,true,canonical[index],target);}}
     require(GPU_pass_global_compilation_count()==compiled,"Sampling an unchanged source graph recompiled the tap");}
   }
  }
  QJsonArray per_output;for(int set=0;set<2;++set)for(int slot=0;slot<6;++slot){const auto &row=output_errors[set*6+slot];const char *names[]={"Color","Color","Metallic","Roughness","AO","Height"};per_output.append(QJsonObject{{"set",set},{"output",names[slot]},{"comparison_domain",slot<2?(slot==1?"canonical_AP1":"scene_linear"):"data"},{"components",qint64(row.components)},{"maximum_abs_error",row.maximum},{"nonfinite_gpu_values",qint64(row.nonfinite)},{"worst",row.worst}});}
  // Store evidence before either assertion so the first failed oracle is useful.
  proof[rec709?"rec709":"acescg"]=QJsonObject{{"components",qint64(components)},{"maximum_abs_error",max_error},{"nonfinite_gpu_values",qint64(nonfinite_gpu)},{"per_output_errors",per_output},{"global_worst",global_worst},{"tolerance",3e-5},{"alpha_ignored",true},{"scalar_R_only",true},{"signed_height_before_derivatives",true}};
  require(nonfinite_gpu==0,"Source graph produced nonfinite GPU values");
  require(max_error<=3e-5,"Source graph numerical oracle failed at unchanged 3e-5 tolerance");
 }
 void pressure(){
  GpuLayerPreview local;const auto cleanup=qScopeGuard([&]{local.reset();GpuLayerPreview::collect_retired();});auto a=source(2,2,true,'a'),b=source(2,2,true,'b'),c=source(1,1,true,'c'),large=source(4,4,true,'e');
  const auto ready=[&](const FillTexture &value){const std::array<const FillTexture *,1> group{&value};local.begin_frame();return local.prepare_texture_resources(group);};require(ready(*a)==GpuPreviewUpdate::Updated&&ready(*b)==GpuPreviewUpdate::Updated,"Tiny pressure setup did not finish");auto lease=local.retain_texture_resource(*a);require(bool(lease),"Pressure lease missing");auto *handle=lease->texture;
  const auto previous_free=texture_free_vram.load();const auto previous_valid=texture_vram_query_valid.load();const auto restore=qScopeGuard([&]{texture_free_vram=previous_free;texture_vram_query_valid=previous_valid;++texture_vram_query_generation;texture_memory_refresh_needed=true;});texture_free_vram=0;texture_vram_query_valid=true;++texture_vram_query_generation;
  require(ready(*c)==GpuPreviewUpdate::Updated&&!local.texture_resource_ready(*b)&&local.texture_resource_ready(*a),"LRU pressure evicted a lease or failed to evict the unleased source");auto c_lease=local.retain_texture_resource(*c);require(bool(c_lease),"Second pressure lease missing");require(ready(*large)==GpuPreviewUpdate::Unsupported&&local.texture_resource_ready(*a)&&local.texture_resource_ready(*c)&&lease->texture==handle,"All-leased admission corrupted resident resources");proof["lease_pressure"]=QJsonObject{{"synthetic_report",true},{"physical_OOM",false},{"leased_preserved",true},{"unleased_evicted",true},{"excess_rejected",true},{"fresh_driver_query_requested",true}};
 }
 bool advance(){
  ++ticks;proof["last_stage"]=stage;std::printf("PD149_SOURCE_GPU_STAGE stage=%d tick=%d\n",stage,ticks);std::fflush(stdout);
  if(stage==0){if(!prepare())return false;require(pool.texture_resource_allocations()==1,"Same interpretation allocated more than one source");check_alias();original_allocations=pool.texture_resource_allocations();++stage;return false;}
  if(stage==1){auto &input=sets[1].material_layering.materials[0];input.tiling={2,-1};input.offset={.25,.5};input.rotation=90;require(prepare(),"Transform-only preparation lost readiness");check_alias();require(pool.texture_resource_allocations()==original_allocations,"Transform-only edit allocated a source");proof["sharing"]=QJsonObject{{"sets",2},{"references",6},{"source_allocations",1},{"image_aliases",1},{"width",8},{"height",4},{"transform_allocations",0}};for(auto &set:sets){auto &i=set.material_layering.materials[0];i.textures[0]=color;i.textures[2]=rough;i.textures[4]=height_map;}++stage;return false;}
  if(stage==2){if(!prepare())return false;require(pool.texture_resource_allocations()==4,"Original-size maps duplicated/packed source storage");const auto images=sources.images(sets[0].material_layering);require(images&&(*images)[0][1]!=(*images)[0][2]&&(*images)[0][2]->runtime->gputexture[TEXTARGET_2D][0]&&GPU_texture_width((*images)[0][2]->runtime->gputexture[TEXTARGET_2D][0])==4&&GPU_texture_height((*images)[0][2]->runtime->gputexture[TEXTARGET_2D][0])==2,"Different scalar dimensions were packed/resampled");++stage;return false;}
  if(stage==3){numerical(false);++stage;return false;}if(stage==4){numerical(true);++stage;return false;}if(stage==5){pressure();++stage;return false;}
  if(stage==6){
   reader=std::make_unique<MaterialLayeringCyclesSourceProbe>(*shared_image);require(reader->immutable_snapshot(),"Real Cycles loader did not retain immutable snapshot");retained_expected=*data->pixels;require(reader->read().pixels==retained_expected,"Real Cycles loader initial pixels differ");save_header.reset(BKE_image_hosted_source_snapshot(shared_image,1001));require(save_header&&save_header->float_data()==data->pixels->front().data(),"Shared save-header view copied pixels");retired_pixels=data->pixels;
   for(auto &set:sets){set.material_layering.enabled=false;set.material_layering.active_mask=-1;}bind();sources.retain_only({});require(!shared_image->runtime->gputexture[TEXTARGET_2D][0]&&!BKE_image_has_loaded_ibuf(shared_image),"Inactive alias/cache survived detachment");for(auto &set:sets)for(auto &input:set.material_layering.materials)input.textures.fill({});data.reset();rough.reset();height_map.reset();color.reset();pool.reset();GpuLayerPreview::collect_retired();require(!retired_pixels.expired()&&reader->read().pixels==retained_expected,"Detachment invalidated a live Cycles reader");require(save_header->float_data()[0]==retained_expected[0][0]&&save_header->float_data()[3]==0,"Detached save-header reader changed");reader->release_on_worker();reader.reset();require(!retired_pixels.expired(),"Cycles reader release invalidated a still-live save header");save_header.reset();++stage;return false;
  }
  if(!retired_pixels.expired())return false;proof["retirement"]=QJsonObject{{"actual_Cycles_loader",true},{"worker_loader_destruction",true},{"Image_Main_alive_during_reads",true},{"detached_alias_and_CPU_cache",true},{"final_decoded_owner_released",true},{"save_header_only_here",true},{"actual_PartialWrite_worker_pack","separate CPU fixture"}};return true;
 }
 void cleanup(){if(cleaned)return;reader.reset();save_header.reset();sources.reset();pool.reset();GpuLayerPreview::collect_retired();main.reset();cleaned=true;}
 ~State(){if(!cleaned)qFatal("Source diagnostic escaped owner cleanup");}
};
}
int run_material_layering_sources_gpu_test(BlenderHost &host,const QString &directory,const QString &project){
 if(qEnvironmentVariableIsSet("BTS_PD149_SOURCE_HOST_MODE"))return run_material_layering_source_host_test(host,directory,project);
 const bool internal_normal=qEnvironmentVariableIsSet("BTS_PD149_INTERNAL_NORMAL");
 std::unique_ptr<State> state;QJsonObject result{{"kind","pd149-private-source-binding-GPU-diagnostic"},{"renderer",QString::fromStdString(host.renderer())},{"native_pid",qint64(QCoreApplication::applicationPid())},{"visible_windows",0}};QString error;QElapsedTimer elapsed;elapsed.start();QEventLoop loop;QTimer timer;timer.setInterval(1);bool busy=false;
 const auto cleanup=qScopeGuard([&]{if(state){try{host.with_gpu([&]{state->cleanup();state.reset();});}catch(...){qFatal("Cannot retire source diagnostic under its GPU owner");}}});
 try{host.with_gpu([&]{state=std::make_unique<State>();});QObject::connect(&timer,&QTimer::timeout,&loop,[&]{if(busy)return;const auto guard=qScopeGuard([&]{busy=false;});busy=true;try{require(elapsed.elapsed()<60000,"Source diagnostic deadline exceeded");bool done=false;host.with_gpu([&]{done=internal_normal?state->advance_internal_normal():state->advance();});if(done){timer.stop();loop.quit();}}catch(const std::exception &e){error=QString::fromUtf8(e.what());timer.stop();loop.quit();}catch(...){error="Unknown source diagnostic failure";timer.stop();loop.quit();}});timer.start();loop.exec();}catch(const std::exception &e){error=QString::fromUtf8(e.what());}catch(...){error="Unknown source diagnostic startup failure";}
 if(internal_normal)result["kind"]="pd149-internal-Normal-before-NormalMap-diagnostic";
 if(state)result["evidence"]=state->proof;result["passed"]=error.isEmpty();result["error"]=error;result["diagnostic_elapsed_ms"]=elapsed.elapsed();
 // Cleanup is part of the verdict and occurs before the headless Host shutdown.
 if(state){try{host.with_gpu([&]{state->cleanup();state.reset();});}catch(...){qFatal("Cannot retire source diagnostic before Host shutdown");}}result["owner_cleanup_complete"]=true;
 QFile file(directory+"/material-layering-sources-gpu.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write source diagnostic receipt");file.write(QJsonDocument(result).toJson());return error.isEmpty()?0:1;
}
}
