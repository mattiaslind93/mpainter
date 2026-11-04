#include <QCoreApplication>
#include "BlenderHost.hh"
#include "ProjectIO.hh"
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "FillTextureJSON.hh"
#include "NumericLocale.hh"
#include "BLI_math_half.hh"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QThread>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <future>
#include <set>
#include <cmath>
#include <cstdio>

using namespace bts;
namespace {
void require(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
QStringList policy(){return {"spsm:policy:bitmap-roughness=raw-r-ignore-a","spsm:policy:bitmap-bytes=little-endian","spsm:policy:bitmap-rows=first-row-v0","spsm:policy:bitmap-alpha=data-components","spsm:policy:bitmap-uv=projection0-centered-repeat-factors","spsm:policy:bitmap-sampling=hq-repeat","spsm:policy:levels=native-scalar"};}
const TextureLayer &named(const SmartMaterialPreset &p,const char *name){for(const auto &l:p.set.layers)if(l.name==name)return l;throw std::runtime_error("Expected source layer missing");}
spsm::NodeId field(const spsm::Preset &p,spsm::NodeId id,std::string_view name){for(const auto &f:std::get<spsm::Object>(p.nodes.at(id).value).fields)if(f.name==name)return f.value;throw std::runtime_error("Expected raw source field missing");}
std::uint64_t integer(const spsm::Value &v){if(auto n=std::get_if<std::uint64_t>(&v))return *n;if(auto n=std::get_if<std::int64_t>(&v);n&&*n>=0)return *n;if(auto n=std::get_if<spsm::IntValues>(&v);n&&n->count==1&&n->values[0]>=0)return n->values[0];throw std::runtime_error("Expected source integer");}
struct Curve {float low,high,gamma,out_low,out_high;bool clamp;};
Curve source_curve(const PreparedSpsm &prepared,const TextureLayer &leaf){
 const auto &inspection=*prepared.source;const auto pi=*inspection.primary_preset;
 const auto &p=inspection.source().presets.at(pi);const auto &ir=inspection.presets.at(pi);
 spsm::NodeId layer=0;bool found=false;
 for(const auto &b:prepared.layers)if(b.native_layer==leaf.id){require(!found,"Duplicate native source binding");layer=b.source;found=true;}
 require(found,"Source leaf binding missing");
 for(const auto &l:ir.layers)if(l.source==layer)for(auto ai:l.actions){const auto &a=ir.actions.at(ai);if(a.kind!=spsm::semantic::ActionKind::Levels)continue;
  if(integer(p.nodes.at(field(p,a.source,"channelTypes")).value)!=128)continue;
  const auto levels=field(p,a.source,"levels");
  const auto scalar=[&](std::string_view key){const auto &v=std::get<spsm::FloatValues>(p.nodes.at(field(p,levels,key)).value);require(v.count==3&&v.values[0]==v.values[1]&&v.values[1]==v.values[2],"Actual scalar source curve changed");return v.values[0];};
  return {scalar("inputMinimum"),scalar("inputMaximum"),scalar("gamma"),scalar("outputMinimum"),scalar("outputMaximum"),integer(p.nodes.at(field(p,levels,"clamp")).value)==1};
 }
 throw std::runtime_error("Actual source Roughness Levels absent");
}
// Deliberately independent of native Fill sampling and Levels helpers. Only the
// exact image resource is read: no procedural package or compiled graph payload.
struct RawOracle {
 spsm::bitmap::RawImage image;Curve curve;FillProjection projection;
 float at(int x,int y)const{
  const auto &h=*image.encoded->header;
  x=(x%int(h.width)+int(h.width))%int(h.width);y=(y%int(h.height)+int(h.height))%int(h.height);
  const auto off=std::uint64_t(y)*h.pitch+std::uint64_t(x)*8;
  const auto *p=image.bytes->data()+off;
  return blender::math::half_to_float(std::uint16_t(p[0]|(std::uint16_t(p[1])<<8)));
 }
 float sample(double u,double v)const{
  const auto &h=*image.encoded->header;u-=std::floor(u);v-=std::floor(v);
  const auto x=u*h.width-.5,y=v*h.height-.5;const int ix=std::floor(x),iy=std::floor(y);
  return std::lerp(std::lerp(at(ix,iy),at(ix+1,iy),float(x-ix)),std::lerp(at(ix,iy+1),at(ix+1,iy+1),float(x-ix)),float(y-iy));
 }
 float raw(int x,int y)const{
  float value=0;for(double dx:{.25,.75})for(double dy:{.25,.75}){
   const auto u=((x+dx)/128-.5)*projection.scale[0]+.5,v=((y+dy)/128-.5)*projection.scale[1]+.5;
   value+=sample(u,v)*.25f;
  }return std::clamp(value,0.f,1.f);
 }
 float result(int x,int y,bool clamp)const{
  double value=(double(raw(x,y))-curve.low)/(double(curve.high)-curve.low);
  if(value>0)value=std::pow(value,1/double(curve.gamma));
  value=curve.out_low+(double(curve.out_high)-curve.out_low)*value;
  value=std::clamp(value,-65504.,65504.);if(clamp)value=std::clamp(value,0.,1.);
  return float(value);
 }
};
std::vector<std::array<int,2>> positions(const RawOracle &oracle){
 std::array<int,2> low{0,0},high{0,0};float lo=oracle.result(0,0,false),hi=lo;
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){auto v=oracle.result(x,y,false);if(v<lo){lo=v;low={x,y};}if(v>hi){hi=v;high={x,y};}}
 require(hi-lo>1e-5f,"Actual Darken oracle is constant/background");
 std::vector<std::array<int,2>> result;const auto add=[&](std::array<int,2> p){if(std::find(result.begin(),result.end(),p)==result.end())result.push_back(p);};add(low);add(high);
 for(auto p:{std::array{7,13},std::array{31,23},std::array{73,91},std::array{113,45},std::array{0,0},std::array{25,81},std::array{63,64},std::array{127,127},std::array{9,117},std::array{106,3},std::array{57,39},std::array{2,69}})if(result.size()<12)add(p);
 require(result.size()==12,"Twelve distinct asymmetric probes missing");return result;
}
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);initialize_numeric_locale();QApplication app(argc,argv);
 try{
  const bool signed_only=argc==4&&std::string_view(argv[3])=="--signed-only";require(argc==3||signed_only,"Expected OUTPUT_DIRECTORY and private copied Wood SPSM fixture");const auto directory=QString::fromLocal8Bit(argv[1]),input=QString::fromLocal8Bit(argv[2]);QDir().mkpath(directory);
  require(QFileInfo(input).absoluteFilePath().startsWith(QFileInfo(directory).absoluteFilePath()+"/"),"Fixture must be the verifier's own private copy");
  qputenv("BTS_RESOURCE_CACHE",(directory+"/first-cache").toUtf8());configure_spsm_source_hydration();
  BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");QOpenGLContext context;context.setFormat(format);require(context.create(),"Offscreen GL context failed");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(surface.isValid()&&context.makeCurrent(&surface),"Offscreen activation failed");QOpenGLFramebufferObject target(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);require(target.isValid(),"Framebuffer failed");struct Close{BlenderHost &h;~Close(){h.shutdown();}}close{host};host.initialize_gpu(context,surface,target.handle());
  require(QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive),"Hardware native GPU renderer required");
  const auto mesh=directory+"/quad.obj";{QFile file(mesh);require(file.open(QIODevice::WriteOnly),"Cannot write private mesh");file.write("o DarkenFixture\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");}
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;options.absolute_mesh_path=mesh.toStdString();
  const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/default-studio.hdr");
  const auto create=[&]{auto summary=host.new_project(mesh.toStdString(),environment.toStdString(),&options);return TextureDocument(options,summary,host.scene_snapshot());};auto document=create();
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,256,256};QMatrix4x4 view,projection;view.translate(-.5f,-.5f,-5);projection.ortho(-.6f,.6f,-.6f,.6f,.01f,100.f);std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());frame.uv_center={.5,.5};frame.uv_extent=1.2;
  const auto publish=[&](bool preload=false){auto &set=document.set(document.texture_sets().front().material_session_uid);host.set_selected_material(set.material_session_uid);host.prepare_paint_texture(set);const InspectionMaterial inspected{set.material_session_uid,set.base_values,{},nullptr,128};host.set_inspection(InspectionMode::Material,std::span(&inspected,1));std::vector<std::shared_ptr<const FillTexture>> textures;for(const auto &layer:set.layers)if(auto texture=fill_channel_texture(layer.fill,2))textures.push_back(texture);QElapsedTimer elapsed;elapsed.start();while(preload&&!host.prepare_texture_resources(textures)){context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<60000,"Source GPU upload timed out");}host.apply_texture_set(set,128);do{context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();require(elapsed.elapsed()<60000,"Native GPU publication timed out");}while(host.texture_updates_pending());};
  std::vector<std::array<int,2>> probes;std::vector<std::array<float,4>> before;std::vector<float> reference;QJsonValue descriptor;QJsonArray effect_json;QString source_hash;double cpu_error=0,gpu_error=0,clamp_error=0,signed_cpu_error=0,signed_gpu_error=0,signed_clamp_error=0;int negative=0,signed_negative=0;bool clamp_witness=false,signed_clamp_witness=false;std::weak_ptr<const PreparedSpsmSource> original_preparation;const auto project=directory+"/packed-darken-levels.blend";
  {
   StudioAsset source;source.filename="used_varnish_wood.spsm";source.name="Packed original Wood";source.id=QUuid::createUuid();source.scope=AssetScope::Project;source.usages={"smartmaterial"};source.smart_material=true;source.search_terms=policy();QFile file(input);require(file.open(QIODevice::ReadOnly),"Cannot read private fixture");source.bytes=file.readAll();source_hash=source.hash();file.close();
   source=std::async(std::launch::async,[source]()mutable{hydrate_asset_presets(source);return source;}).get();require(bool(source.material_preset),"Explicit source did not hydrate");const auto prepared=prepared_spsm_source(source);require(prepared&&!prepared->prepared.appearance_verified,"Source appearance must remain unverified");original_preparation=prepared;
   const auto &ancestor=named(*source.material_preset,"Wood_Color_Roughness_base");require(ancestor.mask&&ancestor.mask->enabled&&ancestor.mask->background==0&&ancestor.mask->effects.empty(),"Original zero ancestor mask was changed");
   const auto &dark=named(*source.material_preset,"Wood_Darken_base");require(dark.visible&&dark.channels[2]&&dark.effects.size()==1&&dark.mask&&dark.mask->enabled&&dark.mask->background==1&&dark.opacity[2]==1&&dark.blending[2]==LayerBlend::Normal,"Actual independent Darken leaf contract changed");
   RawOracle oracle;oracle.curve=source_curve(prepared->prepared,dark);oracle.projection=dark.fill;const auto image=spsm::bitmap::describe_image(prepared->prepared.source->source(),"/resources/44a7fb2d1313c31d50caf61e5b2e5550f2297482.image");oracle.image=spsm::bitmap::decode(spsm::bitmap::load_encoded(image));require(image.header->version=="v2"&&image.header->pitch==image.header->width*8,"Actual raw-half image layout changed");
   auto &set=document.set(document.texture_sets().front().material_session_uid);set.layers={dark};set.layers[0].parent={};set.layers[0].depth=0;set.selected_layer=dark.id;set.precision[2]=ChannelPrecision::L32F;
   descriptor=fill_texture_json(fill_channel_texture(set.layers[0].fill,2));effect_json=effects_json(set.layers[0].effects);probes=positions(oracle);
   for(auto xy:probes){const auto expected=oracle.result(xy[0],xy[1],oracle.curve.clamp);reference.push_back(expected);negative+=expected<0;const auto cpu=TextureDocument::composite(set,xy[0],xy[1]);cpu_error=std::max(cpu_error,std::abs(double(cpu.authored.roughness)-expected));require(std::abs(cpu.authored.roughness-expected)<3e-6&&cpu.coverage[2]>.999f,"CPU/raw-half Levels oracle differs");}
   publish(true);before=host.diagnostic_gpu_pixels(set.material_session_uid,2,probes);
   for(std::size_t i=0;i<probes.size();++i){gpu_error=std::max(gpu_error,std::abs(double(before[i][0])-reference[i]));require(before[i][3]>.999f,"Isolated GPU target has no coverage");std::printf("BTS_DARKEN_GPU_ORACLE x=%d y=%d raw=%.9g expected=%.9g gpu=%.9g\n",probes[i][0],probes[i][1],oracle.raw(probes[i][0],probes[i][1]),reference[i],before[i][0]);}require(gpu_error<3e-5,"Native GPU differs from raw-half/source Levels formula");
   // Separate test-only destination variant, never a source clamp claim.
   set.layers[0].effects[0].clamp=true;publish();auto clamped=host.diagnostic_gpu_pixels(set.material_session_uid,2,probes);for(std::size_t i=0;i<probes.size();++i){const auto expected=oracle.result(probes[i][0],probes[i][1],true);clamp_error=std::max(clamp_error,std::abs(double(clamped[i][0])-expected));clamp_witness|=std::abs(expected-reference[i])>1e-5f;}require(clamp_error<3e-5,"Explicit test-only native clamp1 GPU differs");
   // Deliberately NOT source semantics: move only this detached test curve's
   // input low to .7 so the same actual bitmap witnesses signed intermediates.
   const auto actual_curve=oracle.curve;oracle.curve.low=.7f;
   set.layers[0].effects[0].input_low=.7f;set.layers[0].effects[0].clamp=false;
   std::vector<float> signed_reference;
   for(auto xy:probes){const auto expected=oracle.result(xy[0],xy[1],false);signed_reference.push_back(expected);signed_negative+=expected<0;const auto cpu=TextureDocument::composite(set,xy[0],xy[1]);signed_cpu_error=std::max(signed_cpu_error,std::abs(double(cpu.authored.roughness)-expected));}
   require(signed_negative>0&&signed_negative<int(probes.size())&&signed_cpu_error<3e-6,"Test-only curve lacks signed CPU/formula witness");
   publish();const auto signed_pixels=host.diagnostic_gpu_pixels(set.material_session_uid,2,probes);
   for(std::size_t i=0;i<probes.size();++i){signed_gpu_error=std::max(signed_gpu_error,std::abs(double(signed_pixels[i][0])-signed_reference[i]));std::printf("BTS_DARKEN_TEST_ONLY_SIGNED x=%d y=%d input_low=.7 expected=%.9g gpu=%.9g\n",probes[i][0],probes[i][1],signed_reference[i],signed_pixels[i][0]);}
   require(signed_gpu_error<3e-5,"Test-only signed GPU curve differs from raw-half oracle");
   set.layers[0].effects[0].clamp=true;publish();const auto signed_clamped=host.diagnostic_gpu_pixels(set.material_session_uid,2,probes);
   for(std::size_t i=0;i<probes.size();++i){const auto expected=oracle.result(probes[i][0],probes[i][1],true);signed_clamp_error=std::max(signed_clamp_error,std::abs(double(signed_clamped[i][0])-expected));signed_clamp_witness|=std::abs(expected-signed_reference[i])>1e-5f;}
   require(signed_clamp_witness&&signed_clamp_error<3e-5,"Test-only signed clamp1 GPU witness differs");
   oracle.curve=actual_curve;set.layers[0].effects=dark.effects;publish();auto original_again=host.diagnostic_gpu_pixels(set.material_session_uid,2,probes);for(std::size_t i=0;i<before.size();++i)for(int c=0;c<4;++c)require(std::abs(original_again[i][c]-before[i][c])<1e-6,"Restoring source clamp0 changed original GPU values");
   require(effects_json(set.layers[0].effects)==effect_json&&ancestor.mask->background==0&&source.hash()==source_hash,"Test variant mutated source provenance/effects/mask");
   if(signed_only){QFile proof(directory+"/verified-signed.json");require(proof.open(QIODevice::WriteOnly),"Cannot write signed witness");proof.write(QJsonDocument(QJsonObject{{"source_sha256",source_hash},{"actual_source_negative_samples",negative},{"actual_source_gpu_raw_error",gpu_error},{"test_only_input_low",.7},{"test_only_signed_negative_samples",signed_negative},{"test_only_signed_cpu_raw_error",signed_cpu_error},{"test_only_signed_gpu_raw_error",signed_gpu_error},{"test_only_clamp1_gpu_raw_error",signed_clamp_error},{"test_only_clamp1_witness",signed_clamp_witness},{"original_effect_and_gpu_restored",true},{"original_ancestor_zero_mask_retained",true},{"appearance_verified",false}}).toJson());std::puts("BTS_SPSM_DARKEN_SIGNED_TEST_ONLY PASS");return 0;}
   host.save_project(document,project.toStdString(),{source});
  }
  // Cold source-free packed Open: no hidden strong source/pixel owner remains.
  document=create();publish();require(original_preparation.expired(),"Strong original preparation survived before cold Open");require(QFile::rename(input,input+".held"),"Cannot hide own private input");require(QDir(directory+"/first-cache").removeRecursively(),"Cannot remove own private first cache");qputenv("BTS_RESOURCE_CACHE",(directory+"/second-cache").toUtf8());
  auto pending=std::async(std::launch::async,[&]{return PendingProject::prepare(project.toStdString());}).get();pending->validate();require(pending->resources.size()==1&&pending->resources[0].hash()==source_hash&&pending->resources[0].search_terms.contains("spsm:policy:levels=native-scalar"),"Packed original source/policy changed");const auto &reopened=pending->document->texture_sets().front().layers.front();require(effects_json(reopened.effects)==effect_json&&fill_texture_json(fill_channel_texture(reopened.fill,2))==descriptor,"Packed effects/texture descriptor changed");const auto &original_ancestor=named(*pending->resources[0].material_preset,"Wood_Color_Roughness_base");require(original_ancestor.mask&&original_ancestor.mask->enabled&&original_ancestor.mask->background==0,"Cold source preparation lost original ancestor0");
  document=host.adopt_project(std::move(pending),project.toStdString());publish();const auto after=host.diagnostic_gpu_pixels(document.texture_sets().front().material_session_uid,2,probes);double reopened_error=0;for(std::size_t i=0;i<before.size();++i)for(int c=0;c<4;++c)reopened_error=std::max(reopened_error,std::abs(double(before[i][c])-after[i][c]));require(reopened_error<1e-6,"Cold packed Open changed native GPU Levels pixels");
  QJsonArray points;for(std::size_t i=0;i<probes.size();++i)points.append(QJsonObject{{"x",probes[i][0]},{"y",probes[i][1]},{"expected",reference[i]},{"gpu",before[i][0]}});QFile output(directory+"/verified.json");require(output.open(QIODevice::WriteOnly),"Cannot write native proof");output.write(QJsonDocument(QJsonObject{{"source_sha256",source_hash},{"isolated_leaf","Wood_Darken_base"},{"original_ancestor_zero_mask_retained",true},{"leaf_fields_preserved",true},{"precision","L32F"},{"resolution",128},{"samples",points},{"negative_intermediate_samples",negative},{"explicit_test_only_clamp1_witness",clamp_witness},{"test_only_signed_input_low",.7},{"test_only_signed_negative_samples",signed_negative},{"test_only_signed_cpu_raw_error",signed_cpu_error},{"test_only_signed_gpu_raw_error",signed_gpu_error},{"test_only_clamp1_gpu_raw_error",signed_clamp_error},{"test_only_clamp1_witness",signed_clamp_witness},{"cpu_raw_formula_error",cpu_error},{"gpu_raw_formula_error",gpu_error},{"gpu_clamp1_error",clamp_error},{"source_free_cold_packed_open",true},{"effects_descriptor_sha_preserved",true},{"reopened_gpu_error",reopened_error},{"appearance_verified",false}}).toJson());std::puts("BTS_SPSM_DARKEN_LEVELS_GPU PASS");return 0;
 }catch(const std::exception &error){std::fprintf(stderr,"BTS_SPSM_DARKEN_LEVELS_GPU_FAILED %s\n",error.what());return 1;}
}
