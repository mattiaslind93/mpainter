// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
// Reuse only source-inspection/context helpers; never execute the old oracle.
#pragma push_macro("main")
#undef main
#define main spsm_scalar_levels_native_previous_main
#include "spsm_bitmap_levels_native_test.cc"
#pragma pop_macro("main")
#include "IMB_colormanagement.hh"
#include <QScopeGuard>
#include <QCryptographicHash>
namespace {
struct RGBOracle {
 spsm::bitmap::RawImage image;
 std::array<Curve,3> curves;
 std::array<float,2> scale;
 std::string input;
 float at(int x,int y,int c) const {
  const auto &h=*image.encoded->header;x=(x%int(h.width)+int(h.width))%int(h.width);y=(y%int(h.height)+int(h.height))%int(h.height);
  const auto *p=image.bytes->data()+std::uint64_t(y)*h.pitch+std::uint64_t(x)*8+c*2;
  return blender::math::half_to_float(std::uint16_t(p[0]|(std::uint16_t(p[1])<<8)));
 }
 float sample(double u,double v,int c)const {
  const auto &h=*image.encoded->header;u-=std::floor(u);v-=std::floor(v);const double x=u*h.width-.5,y=v*h.height-.5;const int ix=std::floor(x),iy=std::floor(y);
  return std::lerp(std::lerp(at(ix,iy,c),at(ix+1,iy,c),float(x-ix)),std::lerp(at(ix,iy+1,c),at(ix+1,iy+1,c),float(x-ix)),float(y-iy));
 }
 std::array<float,3> raw(int x,int y,int tile,int coordinates=0)const {
  std::array<float,3> value{};
  for(int c=0;c<3;++c)for(double dx:{.25,.75})for(double dy:{.25,.75}){
   double u=((x+dx)/128+(tile-1001)%10-.5)*scale[0]+.5,v=((y+dy)/128+(tile-1001)/10-.5)*scale[1]+.5;
   // The native owner uploads float affine coefficients derived from exact
   // source endpoints. This independent formula uses source scale and tile,
   // never fill_uv_map or captured/fitted coordinates. The tested GL dot uses
   // separate multiply/add rounding; FMA is diagnostic only, never accepted.
   if(coordinates){const float px=float((x+dx)/128),py=float((y+dy)/128),ax=float((double((tile-1001)%10)-.5)*scale[0]+.5),ay=float((double((tile-1001)/10)-.5)*scale[1]+.5);u=coordinates==1?float(float(scale[0]*px)+ax):std::fma(scale[0],px,ax);v=coordinates==1?float(float(scale[1]*py)+ay):std::fma(scale[1],py,ay);}
   value[c]+=sample(u,v,c)*.25f;
  }
  return value;
 }
 std::array<float,3> interpret(std::array<float,3> raw)const {
  std::array<float,4> value{0,0,0,1};
  for(int c=0;c<3;++c){const auto &e=curves[c];double t=(double(raw[c])-e.low)/(double(e.high)-e.low);if(t>0)t=std::pow(t,1/double(e.gamma));t=e.out_low+(double(e.out_high)-e.out_low)*t;t=std::clamp(t,-65504.,65504.);if(e.clamp)t=std::clamp(t,0.,1.);value[c]=float(t);}
  blender::IMB_colormanagement_transform_v4(value.data(),input.c_str(),"ACEScg");return {value[0],value[1],value[2]};
 }
 std::array<float,3> result(int x,int y,int tile,int coordinates=0)const{return interpret(raw(x,y,tile,coordinates));}

};
RGBOracle rgb_oracle(const PreparedSpsm &prepared,const TextureLayer &leaf,const char *dataset,const char *input){
 RGBOracle out;out.input=input;const auto &inspection=*prepared.source;const auto pi=*inspection.primary_preset;const auto &p=inspection.source().presets[pi];const auto &ir=inspection.presets[pi];
 spsm::NodeId id=0;bool bound=false;for(const auto &binding:prepared.layers)if(binding.native_layer==leaf.id){id=binding.source;bound=true;}require(bound,"Raw Color source binding absent");bool curve=false,mapping=false;
 for(const auto &layer:ir.layers)if(layer.source==id)for(auto ai:layer.actions){const auto &a=ir.actions[ai];
  if(a.kind==spsm::semantic::ActionKind::Fill){const auto uv=field(p,a.source,"uvTransformation");const auto &v=std::get<spsm::FloatValues>(p.nodes[field(p,uv,"scaleFactors")].value);require(v.count==2&&!mapping,"Expected one source Fill UV mapping");out.scale={v.values[0],v.values[1]};mapping=true;}
  if(a.kind!=spsm::semantic::ActionKind::Levels||integer(p.nodes[field(p,a.source,"channelTypes")].value)!=1)continue;require(!curve,"Expected one actual Color Levels action");curve=true;const auto levels=field(p,a.source,"levels");
  const auto triple=[&](std::string_view key){const auto &v=std::get<spsm::FloatValues>(p.nodes[field(p,levels,key)].value);require(v.count==3,"Source RGB Levels shape changed");return v.values;};const auto low=triple("inputMinimum"),high=triple("inputMaximum"),gamma=triple("gamma"),out_low=triple("outputMinimum"),out_high=triple("outputMaximum");const bool clamp=integer(p.nodes[field(p,levels,"clamp")].value)==1;
  for(int c=0;c<3;++c)out.curves[c]={low[c],high[c],gamma[c],out_low[c],out_high[c],clamp};
 }
 require(curve&&mapping,"Raw Color Levels/UV mapping absent");const auto image=spsm::bitmap::describe_image(inspection.source(),dataset);out.image=spsm::bitmap::decode(spsm::bitmap::load_encoded(image));require(image.header->version=="v2"&&image.header->width==4096&&image.header->pitch==32768,"Actual Color record format changed");return out;
}
template<class T>T owner_wait(std::future<T> task){QElapsedTimer elapsed;elapsed.start();while(task.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<180000,"Source worker timed out");}return task.get();}
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);initialize_numeric_locale();QApplication app(argc,argv);
 try{
  std::setbuf(stdout,nullptr);const bool cold_only=argc==4&&std::string_view(argv[3])=="--cold-only";require(argc==3||cold_only||(argc==4&&std::string_view(argv[3])=="--cpu-map-diagnostic"),"Expected private output and copied Wood SPSM");const auto directory=QString::fromLocal8Bit(argv[1]),input=QString::fromLocal8Bit(argv[2]);require(QFileInfo(input).absoluteFilePath().startsWith(QFileInfo(directory).absoluteFilePath()+"/"),"Only own copied input may be hidden");QDir().mkpath(directory);qputenv("BTS_RESOURCE_CACHE",(directory+"/first-cache").toUtf8());configure_spsm_source_hydration();
  if(argc==4&&std::string_view(argv[3])=="--cpu-map-diagnostic"){
   BlenderHost cpu_host(QCoreApplication::applicationFilePath().toStdString(),"");const auto close=qScopeGuard([&]{cpu_host.shutdown();});
   auto original=owner_wait(std::async(std::launch::async,[&]{return prepare_spsm_material(std::filesystem::path(input.toStdString()));}));
   auto oracle=owner_wait(std::async(std::launch::async,[&]{return rgb_oracle(original,named(original.material,"Wood_Worn_base"),"/resources/ebd7c2b2e295ba73b056493abb203c0236ab183d.image","Linear Rec.709");}));
   for(int tile:{1001,1002})for(auto xy:{std::array{9,117},std::array{25,81},std::array{57,39}}){const auto exact=oracle.result(xy[0],xy[1],tile),separate=oracle.result(xy[0],xy[1],tile,1),fma=oracle.result(xy[0],xy[1],tile,2);for(int c=0;c<3;++c)std::printf("BTS_COLOR_CPU_MAP tile=%d x=%d y=%d c=%d double=%.9g f32_separate=%.9g f32_fma=%.9g\n",tile,xy[0],xy[1],c,exact[c],separate[c],fma[c]);}
   return 0;
  }
  BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");QOpenGLContext context;context.setFormat(format);require(context.create(),"GL context failed");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(surface.isValid()&&context.makeCurrent(&surface),"Offscreen activation failed");QOpenGLFramebufferObject target(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);require(target.isValid(),"Framebuffer failed");const auto close=qScopeGuard([&]{host.shutdown();});host.initialize_gpu(context,surface,target.handle());require(QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive),"Native NVIDIA GPU required");
  const auto mesh=directory+"/quad.obj";{QFile file(mesh);require(file.open(QIODevice::WriteOnly),"Private mesh failed");file.write("o ColorFixture\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 2 0\nvt 2 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");}
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;options.absolute_mesh_path=mesh.toStdString();const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/default-studio.hdr");
  const auto create=[&]{const auto summary=host.new_project(mesh.toStdString(),environment.toStdString(),&options);return TextureDocument(options,summary,host.scene_snapshot());};auto document=create();
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,256,256};QMatrix4x4 view,projection;view.translate(-.5f,-.5f,-5);projection.ortho(-.6f,.6f,-.6f,.6f,.01f,100.f);std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());frame.uv_center={1,.5};frame.uv_extent=2.2;
  const auto publish=[&]{auto &set=document.set(document.texture_sets().front().material_session_uid);host.set_selected_material(set.material_session_uid);host.prepare_paint_texture(set);const InspectionMaterial inspected{set.material_session_uid,set.base_values,{},nullptr,128};host.set_inspection(InspectionMode::Material,std::span(&inspected,1));std::vector<std::shared_ptr<const FillTexture>> textures;for(const auto &layer:set.layers)for(int c:{0,2})if(auto texture=fill_channel_texture(layer.fill,c))textures.push_back(texture);QElapsedTimer elapsed;elapsed.start();while(!textures.empty()&&!host.prepare_texture_resources(textures)){context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<180000,"Source GPU preparation timed out");}host.apply_texture_set(set,128);do{context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<180000,"Native Color publication timed out");}while(host.texture_updates_pending());};
  const std::vector<std::array<int,2>> probes{{0,0},{7,13},{31,23},{73,91},{113,45},{25,81},{63,64},{127,127},{9,117},{106,3},{57,39},{2,69}};
  QJsonArray cases;double maximum=0,raw_maximum=0,ideal_double_maximum=0,reopened_error=0;unsigned components=0;QString source_hash;QJsonArray saved_effects;QJsonValue saved_color,saved_rough;std::map<int,std::vector<std::array<float,4>>> before;std::weak_ptr<const PreparedSpsmSource> old_preparation;const auto project=directory+"/packed-color.blend";
  if(!cold_only){
   StudioAsset original;original.filename="used_varnish_wood.spsm";original.name="Original explicit native RGB Wood";original.scope=AssetScope::Project;original.usages={"smartmaterial"};original.smart_material=true;{QFile file(input);require(file.open(QIODevice::ReadOnly),"Private source read failed");original.bytes=file.readAll();}source_hash=original.hash();std::shared_ptr<const FillTexture> prior_color,prior_rough;
   for(const char *choice:{"native-linear-rec709-rgb-levels-ignore-a","native-acescg-rgb-levels-ignore-a"}){
    std::printf("BTS_COLOR_STAGE worker choice=%s\n",choice);auto source=original;source.search_terms=policy();source.search_terms.append(QString("spsm:policy:bitmap-color=")+choice);source=owner_wait(std::async(std::launch::async,[source]()mutable{hydrate_asset_presets(source);return source;}));const auto prepared=prepared_spsm_source(source);require(prepared&&source.material_preset&&!prepared->prepared.appearance_verified,"Explicit source failed or claimed appearance");const auto &ancestor=named(*source.material_preset,"Wood_Color_Roughness_base");require(ancestor.mask&&ancestor.mask->enabled&&ancestor.mask->background==0,"Original ancestor0 changed");
    for(const auto &[name,dataset]:{std::pair{"Wood_Worn_base","/resources/ebd7c2b2e295ba73b056493abb203c0236ab183d.image"},std::pair{"Wood_Darken_base","/resources/1d94222725eb74ea0f4eeea214ad69b835e9a99f.image"}}){
     std::printf("BTS_COLOR_STAGE oracle leaf=%s choice=%s\n",name,choice);const auto &leaf=named(*source.material_preset,name);require(leaf.visible&&leaf.channels[0]&&leaf.channels[2]&&raw_linear_color_source(leaf)&&leaf.fill.texture!=fill_channel_texture(leaf.fill,2),"Selected actual leaf/bindings missing");auto oracle=owner_wait(std::async(std::launch::async,[&]{return rgb_oracle(prepared->prepared,leaf,dataset,QString(choice).contains("rec709")?"Linear Rec.709":"ACEScg");}));
     auto &set=document.set(document.texture_sets().front().material_session_uid);require(set.tiles==std::set<int>{1001,1002},"Two native tiles absent");set.layers={leaf};auto &isolated=set.layers[0];isolated.parent={};isolated.depth=0;isolated.opacity[0]=isolated.opacity[2]=1;isolated.blending[0]=isolated.blending[2]=LayerBlend::Normal;require(isolated.mask&&isolated.mask->background==1,"Leaf mask differs");set.selected_layer=isolated.id;set.precision[0]=ChannelPrecision::RGB32F;set.precision[2]=ChannelPrecision::L32F;const auto full_effects=isolated.effects;
     // Capture the identical source/mapping/filter/output in F32. Only this
     // detached diagnostic effect list changes: remove Color Levels and use
     // the disabled interpretation boundary's explicitly tested identity path.
     std::erase_if(isolated.effects,[](const auto &effect){return effect.channel==0&&effect.kind!=ContentEffectKind::LinearColorTransform;});
     for(auto &effect:isolated.effects)if(effect.kind==ContentEffectKind::LinearColorTransform)effect.enabled=false;
     publish();std::map<int,std::vector<std::array<float,4>>> raw_packet;
     double raw_error=0;
     for(int tile:set.tiles){auto captured=host.diagnostic_gpu_pixels(set.material_session_uid,0,probes,tile);for(std::size_t i=0;i<probes.size();++i){const auto expected=oracle.raw(probes[i][0],probes[i][1],tile,1);require(captured[i][3]>.999f,"Raw F32 packet has no coverage");for(int c=0;c<3;++c)raw_error=std::max(raw_error,std::abs(double(captured[i][c])-expected[c]));}raw_packet.emplace(tile,std::move(captured));}
     require(raw_error<3e-5,"GPU raw F32 sampler differs from independent source-derived F32 affine/bilinear oracle");raw_maximum=std::max(raw_maximum,raw_error);
     isolated.effects=full_effects;publish();QJsonArray samples;double error=0,ideal_error=0;std::array<double,3> lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};
     for(int tile:set.tiles){const auto actual=host.diagnostic_gpu_pixels(set.material_session_uid,0,probes,tile);for(std::size_t i=0;i<probes.size();++i){const auto &captured=raw_packet.at(tile)[i];const auto raw_expected=oracle.raw(probes[i][0],probes[i][1],tile,1),expected=oracle.interpret({captured[0],captured[1],captured[2]}),ideal=oracle.result(probes[i][0],probes[i][1],tile);samples.append(QJsonObject{{"tile",tile},{"x",probes[i][0]},{"y",probes[i][1]},{"raw_f32_expected",QJsonArray{raw_expected[0],raw_expected[1],raw_expected[2]}},{"raw_f32_gpu",QJsonArray{captured[0],captured[1],captured[2]}},{"levels_ocio_expected",QJsonArray{expected[0],expected[1],expected[2]}},{"full_gpu",QJsonArray{actual[i][0],actual[i][1],actual[i][2]}},{"ideal_double_mapping_expected",QJsonArray{ideal[0],ideal[1],ideal[2]}}});require(actual[i][3]>.999f,"Isolated Color has no coverage");for(int c=0;c<3;++c){error=std::max(error,std::abs(double(actual[i][c])-expected[c]));ideal_error=std::max(ideal_error,std::abs(double(actual[i][c])-ideal[c]));lo[c]=std::min(lo[c],double(expected[c]));hi[c]=std::max(hi[c],double(expected[c]));++components;}}if(QString(choice).contains("acescg")&&QString(name)=="Wood_Darken_base")before[tile]=actual;}
     require((hi[0]-lo[0]>1e-4||hi[1]-lo[1]>1e-4||hi[2]-lo[2]>1e-4)&&error<3e-5,"GPU Levels/named OCIO differs from independent analytic evaluation of the captured F32 raw packet");maximum=std::max(maximum,error);ideal_double_maximum=std::max(ideal_double_maximum,ideal_error);cases.append(QJsonObject{{"leaf",name},{"chosen_input",choice},{"dataset",dataset},{"components",72},{"raw_f32_affine_bilinear_error",raw_error},{"levels_named_ocio_on_f32_packet_error",error},{"ideal_double_mapping_output_error",ideal_error},{"isolated_coverage_override",true},{"samples",samples}});std::printf("BTS_COLOR_GPU leaf=%s choice=%s components=72 raw_error=%.9g levels_ocio_error=%.9g ideal_double_error=%.9g\n",name,choice,raw_error,error,ideal_error);

     if(QString(choice).contains("acescg")&&QString(name)=="Wood_Darken_base"){saved_effects=effects_json(isolated.effects);saved_color=fill_texture_json(isolated.fill.texture);saved_rough=fill_texture_json(fill_channel_texture(isolated.fill,2));old_preparation=prepared;host.save_project(document,project.toStdString(),{source});}
    }
   }
  }
  if(!cold_only){
  // Completed stage oracle checkpoint: retain full raw/processed samples and
  // exact pre-Open F32 packets before fixture cleanup or packed-file hydration.
  QJsonArray before_json;for(const auto &[tile,pixels]:before){QJsonArray values;for(const auto &pixel:pixels)values.append(QJsonArray{pixel[0],pixel[1],pixel[2],pixel[3]});before_json.append(QJsonObject{{"tile",tile},{"pixels",values}});}
  QFile packed(project);require(packed.open(QIODevice::ReadOnly),"Cannot hash completed packed project");QCryptographicHash packed_hash(QCryptographicHash::Sha256);require(packed_hash.addData(&packed),"Packed project hash failed");packed.close();
  QFile checkpoint(directory+"/stage-checkpoint.json");require(checkpoint.open(QIODevice::WriteOnly),"Cannot preserve completed stage proof");checkpoint.write(QJsonDocument(QJsonObject{{"binding",qEnvironmentVariable("BTS_COLOR_PROOF_BINDING")},{"source_sha256",source_hash},{"project_sha256",QString::fromLatin1(packed_hash.result().toHex())},{"cases",cases},{"components",int(components)},{"raw_f32_affine_bilinear_maximum_error",raw_maximum},{"levels_named_ocio_on_f32_packet_maximum_error",maximum},{"ideal_double_mapping_output_maximum_error",ideal_double_maximum},{"before",before_json},{"effects",saved_effects},{"color_descriptor",saved_color},{"roughness_descriptor",saved_rough}}).toJson());checkpoint.close();
  }else{
   QFile checkpoint(directory+"/stage-checkpoint.json");require(checkpoint.open(QIODevice::ReadOnly),"Missing completed stage checkpoint");QJsonParseError error;const auto state=QJsonDocument::fromJson(checkpoint.readAll(),&error).object();require(error.error==QJsonParseError::NoError&&!state.isEmpty()&&!qEnvironmentVariable("BTS_COLOR_PROOF_BINDING").isEmpty()&&state["binding"].toString()==qEnvironmentVariable("BTS_COLOR_PROOF_BINDING"),"Stage checkpoint production/oracle identity differs");
   QFile packed(project);require(packed.open(QIODevice::ReadOnly),"Missing checkpoint-bound packed project");QCryptographicHash hash(QCryptographicHash::Sha256);require(hash.addData(&packed)&&QString::fromLatin1(hash.result().toHex())==state["project_sha256"].toString(),"Packed project checkpoint SHA differs");
   cases=state["cases"].toArray();components=state["components"].toInt();maximum=state["levels_named_ocio_on_f32_packet_maximum_error"].toDouble();raw_maximum=state["raw_f32_affine_bilinear_maximum_error"].toDouble();ideal_double_maximum=state["ideal_double_mapping_output_maximum_error"].toDouble();source_hash=state["source_sha256"].toString();saved_effects=state["effects"].toArray();saved_color=state["color_descriptor"];saved_rough=state["roughness_descriptor"];
   require(cases.size()==4&&components==288&&maximum<3e-5&&raw_maximum<3e-5,"Incomplete completed stage checkpoint");for(const auto entry:state["before"].toArray()){const auto object=entry.toObject();auto &pixels=before[object["tile"].toInt()];for(const auto value:object["pixels"].toArray()){const auto rgba=value.toArray();require(rgba.size()==4,"Malformed checkpoint pixel");pixels.push_back({float(rgba[0].toDouble()),float(rgba[1].toDouble()),float(rgba[2].toDouble()),float(rgba[3].toDouble())});}}require(before.size()==2&&before.at(1001).size()==probes.size()&&before.at(1002).size()==probes.size(),"Incomplete checkpoint pixel packets");
  }
  if(!cold_only){document=create();publish();require(old_preparation.expired(),"Strong Source owner survived cold Open boundary");require(QFile::rename(input,input+".held"),"Cannot hide own input");require(QDir(directory+"/first-cache").removeRecursively(),"Cannot clear own first cache");}
  require(!QFileInfo::exists(input)&&QFileInfo::exists(input+".held"),"Cold Open original is not hidden");const auto cold_cache=directory+(cold_only?"/restart-cache":"/second-cache");require(!cold_only||!QDir(cold_cache).exists(),"Restart resource cache was not empty");qputenv("BTS_RESOURCE_CACHE",cold_cache.toUtf8());
  auto pending=owner_wait(std::async(std::launch::async,[&]{return PendingProject::prepare(project.toStdString());}));pending->validate();require(pending->resources.size()==1&&pending->resources[0].hash()==source_hash&&pending->resources[0].search_terms.contains("spsm:policy:bitmap-color=native-acescg-rgb-levels-ignore-a")&&pending->resources[0].search_terms.contains("spsm:policy:bitmap-roughness=raw-r-ignore-a"),"Packed original source SHA/policy changed");const auto &restored=pending->document->texture_sets().front().layers.front();require(effects_json(restored.effects)==saved_effects&&fill_texture_json(restored.fill.texture)==saved_color&&fill_texture_json(fill_channel_texture(restored.fill,2))==saved_rough,"Cold packed source descriptors/effects changed");const auto cold_source=prepared_spsm_source(pending->resources[0]);require(cold_source&&cold_source->cache_path.startsWith(cold_cache+"/"),"Packed Open reused the removed first resource-cache root");const auto &ancestor=named(*pending->resources[0].material_preset,"Wood_Color_Roughness_base");require(ancestor.mask&&ancestor.mask->background==0,"Cold source ancestry0 changed");document=host.adopt_project(std::move(pending),project.toStdString());publish();for(int tile:{1001,1002}){const auto actual=host.diagnostic_gpu_pixels(document.texture_sets().front().material_session_uid,0,probes,tile);for(std::size_t i=0;i<actual.size();++i)for(int c=0;c<4;++c)reopened_error=std::max(reopened_error,std::abs(double(actual[i][c])-before.at(tile)[i][c]));}require(reopened_error<1e-6,"Cold packed Open changed GPU Color");
  QFile output(directory+(cold_only?"/verified-restart.json":"/verified.json"));require(output.open(QIODevice::WriteOnly),"Cannot save Color proof");output.write(QJsonDocument(QJsonObject{{"source_sha256",source_hash},{"cases",cases},{"components",int(components)},{"raw_components",int(components)},{"precision","RGB32F"},{"tiles",QJsonArray{1001,1002}},{"levels_named_ocio_on_f32_packet_maximum_error",maximum},{"raw_f32_affine_bilinear_maximum_error",raw_maximum},{"ideal_double_mapping_output_maximum_error",ideal_double_maximum},{"tested_affine_rounding","separate-f32-multiply-add"},{"fma_is_diagnostic_only",true},{"fixed_bound",3e-5},{"cold_open_maximum_error",reopened_error},{"source_hidden_new_cache_packed_open",true},{"fresh_process_restart",cold_only},{"original_ancestor0_retained",true},{"isolated_leaf_coverage_override",true},{"source_appearance_verified",false}}).toJson());std::puts("BTS_SPSM_COLOR_GPU PASS");return 0;
 }catch(const std::exception &error){std::fprintf(stderr,"BTS_SPSM_COLOR_GPU_FAILED %s\n",error.what());return 1;}
}
