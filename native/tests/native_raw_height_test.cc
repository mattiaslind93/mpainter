// Phase-one scalar-function proof only: no native Bump-response/UI policy.
#include "BlenderHost.hh"
#include "NativeMaterialRawHeightFixture.hh"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialSource.hh"
#include "BlendMaterial.hh"
#include "NativeMaterialDependencies.hh"
#include "FillProjection.hh"
#include "BLI_map.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QScopeGuard>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <future>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace bts;
using namespace bts::native_raw_height_test;
namespace {
auto work(auto function){auto future=std::async(std::launch::async,std::move(function));QElapsedTimer clock;clock.start();while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){QCoreApplication::processEvents();QThread::msleep(1);require(clock.elapsed()<120000,"Raw Height worker proof timed out");}return future.get();}
QJsonObject provenance(const NativeRawHeightProvenance &p){return {{"bump_node",QString::fromStdString(p.bump_node)},{"height_node",QString::fromStdString(p.height_node)},{"height_socket",QString::fromStdString(p.height_socket)},{"unconsumed_strength",p.strength},{"unconsumed_distance",p.distance},{"unconsumed_filter_width",p.filter_width},{"unconsumed_invert",p.invert},{"bypassed_no_height_nodes",p.bypass_nodes}};}
blender::bNodeTree *independent_confetti_noise(NativeMaterialGraph &graph){
 using namespace blender;const bNode *noise=nullptr;std::set<const bNodeTree *> seen;int found=0;
 const auto input=[](const bNode &node,const char *name)->const bNodeSocket *{for(const auto &socket:node.inputs)if(STREQ(socket.identifier,name))return &socket;return nullptr;};
 const auto visit=[&](auto &&self,const bNodeTree &tree)->void{if(!seen.insert(&tree).second)return;for(const auto &node:tree.nodes){if(STREQ(node.idname,"ShaderNodeGroup")&&node.id)self(self,*reinterpret_cast<const bNodeTree *>(node.id));if(!STREQ(node.idname,"ShaderNodeBump"))continue;const auto *height=input(node,"Height"),*normal=input(node,"Normal");if(height&&normal&&height->link&&!normal->link&&STREQ(height->link->fromnode->idname,"ShaderNodeTexNoise")&&STREQ(height->link->fromsock->identifier,"Fac")){noise=height->link->fromnode;++found;}}};visit(visit,*graph.material->nodetree);require(found==1&&noise,"Confetti oracle needs one original Noise/Fac Height source");
 for(const auto &in:noise->inputs)require(!in.link,"Independent Confetti oracle does not guess linked Noise inputs");
 auto *tree=bke::node_tree_add_tree(graph.main.get(),"Independent saved Noise scalar reference","ShaderNodeTree");tree->flag|=NTREE_IS_GPU_SHADER_INTERNAL;Map<const bNodeSocket *,bNodeSocket *> sockets;
 // Native copy retains the actual saved Noise storage, including dimensions,
 // noise type and normalization flags which the old JSON audit did not dump.
 auto *copy=bke::node_copy_with_mapping(tree,*noise,0,std::nullopt,std::nullopt,sockets);require(copy,"Cannot clone saved Confetti Noise");require(copy->storage&&noise->storage&&std::memcmp(copy->storage,noise->storage,sizeof(NodeTexNoise))==0,"Independent reference changed saved Noise storage");copy->parent=nullptr;
 auto *generated=bke::node_add_node(nullptr,*tree,UString("ShaderNodeAttribute"));STRNCPY(static_cast<NodeShaderAttribute *>(generated->storage)->name,"BTS_Coord_Generated");auto *aov=bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputAOV"));STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name,"BTS_Channel");
 bke::node_add_link(*tree,*generated,socket(*generated,"Vector",false),*copy,socket(*copy,"Vector",true));bke::node_add_link(*tree,*copy,socket(*copy,"Fac",false),*aov,socket(*aov,"Value",true));bke::node_add_node(nullptr,*tree,UString("ShaderNodeOutputMaterial"));BKE_ntree_update_after_single_tree_change(*graph.main,*tree);return tree;
}
struct Actual {QString name;PreparedNativeRawHeight prepared;blender::bNodeTree *reference=nullptr;QJsonObject source;};
DetachedNativeMaterial imported_source(const QString &file,const QString &name,const std::atomic_bool &cancel){
 using namespace blender;std::unique_ptr<Main,decltype(&BKE_main_free)> reference(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(reference.get());require(reference->colorspace.scene_linear_name[0],"Fixture reference Main has no recorded working space");
 // These are original historical source libraries, not modern packed-native
 // archives. Use ordinary NEW-import color setup rather than SKIP_ALL archive
 // restoration, which cannot infer a missing historical FileGlobal field.
 auto graph=prepare_native_material_graph(*reference,file,name,cancel);require(graph.use_count()==1&&graph->image_resources.empty(),"Test source is not a uniquely owned, unpublished CPU graph");require(std::string(graph->main->colorspace.scene_linear_name)=="ACEScg","Ordinary import did not establish recorded canonical working space");DetachedNativeMaterial detached;detached.material=graph->material;detached.main=std::move(graph->main);return detached;
}
}
#ifndef BTS_NATIVE_RAW_HEIGHT_NO_MAIN
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);QApplication app(argc,argv);
 try {
  require(argc==3,"Expected private output and explicit six-source manifest");const QString output=argv[1];QDir().mkpath(output);BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");struct Close{BlenderHost &host;~Close(){host.shutdown();}}close{host};std::atomic_bool cancel=false;
  require(!blender::GPU_context_active_get(),"Raw Height CPU preflight unexpectedly owns GPU context");const auto before_compile=blender::GPU_pass_global_compilation_count();
  {using namespace blender;bNode a{},b{},pbr{};STRNCPY(a.idname,"NodeReroute");STRNCPY(b.idname,"NodeReroute");bNodeSocket in_a{},in_b{},out_a{},out_b{},normal{};a.inputs.first=&in_a;b.inputs.first=&in_b;bNodeLink root{},to_a{},to_b{};const auto edge=[](bNodeLink &link,bNode &from,bNodeSocket &out,bNode &to,bNodeSocket &in){link.fromnode=&from;link.fromsock=&out;link.tonode=&to;link.tosock=&in;link.flag=NODE_LINK_VALID;in.link=&link;};edge(root,a,out_a,pbr,normal);edge(to_a,b,out_b,a,in_a);edge(to_b,a,out_a,b,in_b);require(!classify_native_raw_height(normal).eligible&&classify_native_raw_height(normal).reason.find("cyclic")!=std::string::npos,"Cyclic Normal reroute was admitted");in_a.link=nullptr;require(!classify_native_raw_height(normal).eligible,"Dangling Normal reroute was admitted");}
  std::vector<PreparedNativeRawHeight> synthetic;QJsonArray cpu,actual_report;std::vector<Actual> actual;
  const auto build=[&](Kind kind,float constant=.5f){return work([&]{return prepare_native_raw_height(fixture(kind,constant),cancel);});};
  for(Kind kind:{Kind::Affine,Kind::Constant,Kind::NoHeight,Kind::CustomNormal,Kind::ColorOutput,Kind::DoubleBump,Kind::LinkedStrength,Kind::Invert,Kind::NormalInHeight,Kind::InactiveHeight,Kind::UnrelatedInactive,Kind::MutedBump}){
   auto original=work([&]{return finish_native_material_source(fixture(kind),cancel);});const auto original_state=source_state(*original->material->nodetree);const auto original_metadata=native_material_description_json(original->description);const bool eligible=kind==Kind::Affine||kind==Kind::Constant;bool rejected=false;QString reason;
   try{auto prepared=build(kind);require(eligible,"Unsupported raw Height fixture was accepted");require(prepared.source->description.channels==original->description.channels&&native_material_description_json(prepared.source->description)==original_metadata,"Alternate Height changed default source metadata/caps");require(source_state(*prepared.source->material->nodetree)==original_state,"Alternate Height changed original nested source links/defaults");require(!bump(*prepared.source).tree,"Proof accidentally enabled native Bump capability");require(prepared.provenance.strength==.25f&&prepared.provenance.distance==1&&prepared.provenance.filter_width==1,"Raw Height lost original unconsumed controls");synthetic.push_back(std::move(prepared));}
   catch(const std::invalid_argument &e){rejected=true;reason=QString::fromUtf8(e.what());}
   require(rejected!=eligible,"Raw Height eligibility differs from fixture contract");require(native_material_description_json(original->description)==original_metadata&&source_state(*original->material->nodetree)==original_state,"Rejected raw proof changed ordinary prepared source");bool color_available=false;for(const auto &channel:original->channels)if(channel.channel==0&&channel.tree)color_available=true;require(color_available,"Conservative raw Height rejection broke ordinary Color preparation");if(kind==Kind::MutedBump)require(!reason.contains("inactive"),"Muted node was confused with an inactive edge");cpu.append(QJsonObject{{"kind",int(kind)},{"eligible",eligible},{"reason",reason},{"default_caps_unchanged",true},{"ordinary_color_available",true},{"ordinary_bump_available",bool(bump(*original).tree)}});
  }
  for(float value:{-.25f,0.f,.2f,.5f,1.f,3.5f,65000.f})synthetic.push_back(build(Kind::Constant,value));
  auto cancelled=std::atomic_bool(true);bool cancelled_before=false;try{work([&]{return prepare_native_raw_height(fixture(),cancelled);});}catch(const std::runtime_error &){cancelled_before=true;}require(cancelled_before,"Cancelled Height preparation was not rejected");
  bool owner_rejected=false;try{prepare_native_raw_height(fixture(),cancel);}catch(const std::logic_error &){owner_rejected=true;}require(owner_rejected,"Height preparation accepted QApplication owner thread");
  QFile cases(argv[2]);require(cases.open(QIODevice::ReadOnly),"Missing explicit source manifest");const auto entries=QJsonDocument::fromJson(cases.readAll()).array();require(entries.size()==6,"Expected the existing six actual source groups");
  for(const auto &entry:entries){const auto test=entry.toObject();const auto file=test["file"].toString(),name=test["material"].toString();auto original=work([&]{return finish_native_material_source(imported_source(file,name,cancel),cancel);});const auto metadata=native_material_description_json(original->description),state=source_state(*original->material->nodetree);const bool eligible=name=="Particle-Confetti";bool rejected=false;QString reason;QJsonObject details;
   try{auto prepared=work([&]{return prepare_native_raw_height(imported_source(file,name,cancel),cancel);});require(eligible,"Unexpected actual Height source was accepted");require(metadata==native_material_description_json(prepared.source->description)&&state==source_state(*prepared.source->material->nodetree),"Actual raw Height changed original source/caps");require(!bump(*prepared.source).tree,"Actual source Bump capability was activated");details=provenance(prepared.provenance);auto *reference=work([&]{return independent_confetti_noise(*prepared.source);});actual.push_back({name,std::move(prepared),reference,state});}
   catch(const std::invalid_argument &e){rejected=true;reason=QString::fromUtf8(e.what());}
   require(rejected!=eligible,"Actual Height classification differs from bounded contract");actual_report.append(QJsonObject{{"material",name},{"eligible",eligible},{"reason",reason},{"provenance",details},{"default_metadata_and_source_preserved",true}});
  }
  require(!blender::GPU_context_active_get()&&blender::GPU_pass_global_compilation_count()==before_compile,"Raw Height worker/CPU stage created GPU state");
  QJsonObject proof{{"phase",1},{"cpu",cpu},{"actual",actual_report},{"cancel_and_owner_rejected",true},{"default_caps_unchanged",true},{"source_preserved",true},{"ui_or_persistent_policy_activated",false},{"cpu_interactive_composition",false},{"neutral_convention",.5},{"height_interpretation","raw H only; no Strength/Distance/filter/invert scaling"}};
  if(!qEnvironmentVariableIsSet("BTS_RAW_HEIGHT_CPU_ONLY")){
   using namespace blender;QOpenGLContext context;context.setFormat(format);require(context.create(),"Raw Height context creation failed");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(context.makeCurrent(&surface),"Raw Height owner activation failed");QOpenGLFramebufferObject framebuffer(128,128,QOpenGLFramebufferObject::CombinedDepthStencil);host.initialize_gpu(context,surface,framebuffer.handle());
   host.with_gpu([&]{
    constexpr int width=23,height=13;using Texture=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;Texture target(GPU_texture_create_2d("Raw scalar Height proof",width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);require(bool(target),"Height proof target allocation failed");
    std::vector<FillSurfacePoint> points(width*height);NativeMaterialObject a,b;a.owner=0xffffffffu;a.object_to_world={2,0,0,0,0,3,0,0,0,0,4,0,3,-2,0,1};a.world_to_object={.5,0,0,0,0,1.f/3,0,0,0,0,.25,0,-1.5,2.f/3,0,1};a.bounds_max={1,1,2};b.owner=0x1000001u;b.object_to_world={4,0,0,0,0,2,0,0,0,0,.5,0,-2,1,2,1};b.world_to_object={.25,0,0,0,0,.5,0,0,0,0,2,0,.5,-.5,-4,1};b.bounds_min={-1,-1,-1};b.bounds_max={2,3,4};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto &point=points[y*width+x];point.position={float(2*(x+.5)/width+3),float(3*(y+.5)/height-2),4};point.normal={0,0,1};point.valid=x!=0;point.match_object=x<width/2?a.owner:b.owner;}
    const auto bytes=points.size()*sizeof(FillSurfacePoint);const int words=int((bytes+15)/16);std::vector<unsigned int> packed(words*4);std::memcpy(packed.data(),points.data(),bytes);Texture atlas(GPU_texture_create_2d_array("Raw Height two-owner proof",words,1,1,1,gpu::TextureFormat::UINT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);require(bool(atlas),"Height surface allocation failed");GPU_texture_update(atlas.get(),GPU_DATA_UINT,packed.data());NativeMaterialGpuContext dispatch;dispatch.surface=atlas.get();dispatch.surface_size={width,height};dispatch.surface_stride=sizeof(FillSurfacePoint)/4;dispatch.objects=std::make_shared<const NativeMaterialObjectTable>(std::vector{a,b});
    std::uint64_t analytic=0,native_checks=0;double maximum=0,native_maximum=0;QJsonArray constant_checks;
    for(std::size_t index=0;index<synthetic.size();++index){auto &prepared=synthetic[index];const auto before=source_state(*prepared.source->material->nodetree);NativeMaterialGpuProgram program(*prepared.tree,prepared.source->parameters.size(),4);const auto compiles=GPU_pass_global_compilation_count();
     for(int pass=0;pass<(index==0?2:1);++pass)for(int tile:{1001,1002}){dispatch.tile=tile;auto parameters=prepared.source->parameters;double bias=.5,gain=1;const bool affine=index==0;if(affine){bias=pass?-.2:.5;gain=pass?2:1;for(std::size_t i=0;i<prepared.source->description.parameters.size();++i){const auto &p=prepared.source->description.parameters[i];if(p.name=="Bias")parameters[i]={float(bias),float(bias),float(bias),1};if(p.name=="Gain")parameters[i]={float(gain),float(gain),float(gain),1};}}
      program.dispatch(target.get(),parameters,dispatch,{0,0,width,height});auto *values=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(values,"Height analytic readback failed");const auto release=qScopeGuard([&]{MEM_delete(values);});const double constant=index<2?.5:std::array{-.25f,0.f,.2f,.5f,1.f,3.5f,65000.f}[index-2];
      for(int y=0;y<height;++y)for(int x=0;x<width;++x){double scalar=constant;if(affine){const auto &object=x<width/2?a:b;double generated[3]{};for(int row=0;row<3;++row){double local=object.world_to_object[12+row];for(int c=0;c<3;++c)local+=double(object.world_to_object[c*4+row])*points[y*width+x].position[c];generated[row]=(local-object.bounds_min[row])/double(object.bounds_max[row]-object.bounds_min[row]);}scalar=bias+gain*(double(.2f)*generated[0]+double(-.3f)*generated[1]+double(.4f)*generated[2]);}for(int c=0;c<4;++c){const double expected=!points[y*width+x].valid?0:c==3?1:scalar;const double error=std::abs(values[(y*width+x)*4+c]-expected);maximum=std::max(maximum,error);require(std::isfinite(values[(y*width+x)*4+c])&&error<3e-5,"Raw authored Height differs from independent analytic H");++analytic;}}
     }
     require(GPU_pass_global_compilation_count()==compiles&&source_state(*prepared.source->material->nodetree)==before,"Height parameter edits recompiled or changed original source");if(index>=2)constant_checks.append(std::array{-.25f,0.f,.2f,.5f,1.f,3.5f,65000.f}[index-2]);
    }
    for(auto &source:actual){NativeMaterialGpuProgram program(*source.prepared.tree,source.prepared.source->parameters.size(),4),reference(*source.reference,0,4);double range_min=1e30,range_max=-1e30;for(int tile:{1001,1002}){dispatch.tile=tile;program.dispatch(target.get(),source.prepared.source->parameters,dispatch,{0,0,width,height});auto *got=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(got,"Confetti raw Height readback missing");const auto release=qScopeGuard([&]{MEM_delete(got);});reference.dispatch(target.get(),{},dispatch,{0,0,width,height});auto *expected=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));require(expected,"Independent saved Noise readback missing");const auto free=qScopeGuard([&]{MEM_delete(expected);});for(int i=0;i<width*height;++i){if(points[i].valid){range_min=std::min(range_min,double(got[i*4]));range_max=std::max(range_max,double(got[i*4]));}for(int c=0;c<4;++c){const double error=std::abs(got[i*4+c]-expected[i*4+c]);native_maximum=std::max(native_maximum,error);require(std::isfinite(got[i*4+c])&&error<3e-5,"Confetti Height differs from independent saved Noise scalar AOV");++native_checks;}}}require(range_max-range_min>1e-3,"Confetti scalar Height has no varying texel witness");require(source_state(*source.prepared.source->material->nodetree)==source.source,"Confetti GPU proof changed original source");}
    proof["gpu"]=QJsonObject{{"analytic_components",qint64(analytic)},{"maximum_analytic_error",maximum},{"native_confetti_components",qint64(native_checks)},{"maximum_native_error",native_maximum},{"fixed_bound",3e-5},{"owners",2},{"tiles",QJsonArray{1001,1002}},{"signed_and_hdr_constants",constant_checks},{"per_edit_shader_recompiles",0},{"scalar_transport","H,H,H,1; .2 remains .2, not native derivative Bump output"}};
   });
   synthetic.clear();actual.clear();host.shutdown();
  }
  QFile result(output+"/verified.json");require(result.open(QIODevice::WriteOnly),"Cannot write raw Height proof");result.write(QJsonDocument(proof).toJson());std::cout<<"PASS native raw authored Height phase1\n";return 0;
 }catch(const std::exception &e){std::cerr<<"RAW_HEIGHT_FAIL "<<e.what()<<"\n";return 1;}
}
#endif
