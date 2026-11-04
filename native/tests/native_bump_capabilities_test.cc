#include "BlenderHost.hh"
#include "NativeMaterialBump.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialApply.hh"
#include "NativeMaterialMultiObjectDemo.hh"
#include "BKE_material.hh"
#include "BKE_main.hh"
#include "BKE_node_tree_update.hh"
#include "BLO_writefile.hh"
#include "DNA_material_types.h"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QApplication>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QScopeGuard>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <iostream>
using namespace bts;
namespace {
void require(bool b,const char *message){if(!b)throw std::runtime_error(message);}
void wait(const auto &ready){QElapsedTimer timer;timer.start();while(!ready()){QCoreApplication::processEvents();QThread::msleep(1);require(timer.elapsed()<120000,"Native Bump timeout");}}
auto await(auto future){wait([&]{return future.wait_for(std::chrono::seconds(0))==std::future_status::ready;});return future.get();}
auto &socket(blender::bNode &node,const char *name,bool input){return native_object_demo::socket(node,name,input);}
void fixture(const QString &path){
 using namespace blender;using bke::blendfile::PartialWriteContext;
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);IMB_colormanagement_working_space_init_default(main.get());std::vector<ID *> ids;
 for(const auto *name:{"Unlinked","NoHeight","NoHeightChain","ActiveHeight","CustomNormal","ActiveCustomNormal"}){
  auto *material=BKE_material_add(main.get(),name);auto &tree=*material->nodetree;auto *pbr=bke::node_add_node(nullptr,tree,UString("ShaderNodeBsdfPrincipled"));auto *output=bke::node_add_node(nullptr,tree,UString("ShaderNodeOutputMaterial"));bke::node_add_link(tree,*pbr,socket(*pbr,"BSDF",false),*output,socket(*output,"Surface",true));
  if(std::string_view(name)!="Unlinked"){
   auto *bump=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));static_cast<bNodeSocketValueFloat *>(socket(*bump,"Height",true).default_value)->value=17;static_cast<bNodeSocketValueFloat *>(socket(*bump,"Strength",true).default_value)->value=.25f;
   bke::node_add_link(tree,*bump,socket(*bump,"Normal",false),*pbr,socket(*pbr,"Normal",true));
   if(std::string_view(name)=="NoHeightChain"){
    auto *inner=bke::node_add_node(nullptr,tree,UString("ShaderNodeBump"));auto *reroute=bke::node_add_node(nullptr,tree,UString("NodeReroute"));bke::node_add_link(tree,*inner,socket(*inner,"Normal",false),*reroute,*static_cast<bNodeSocket *>(reroute->inputs.first));bke::node_add_link(tree,*reroute,*static_cast<bNodeSocket *>(reroute->outputs.first),*bump,socket(*bump,"Normal",true));
   }
   if(std::string_view(name).starts_with("Active")){auto *noise=bke::node_add_node(nullptr,tree,UString("ShaderNodeTexNoise"));bke::node_add_link(tree,*noise,socket(*noise,"Fac",false),*bump,socket(*bump,"Height",true));}
   if(std::string_view(name).find("Custom")!=std::string_view::npos){auto *normal=bke::node_add_node(nullptr,tree,UString("ShaderNodeNormalMap"));bke::node_add_link(tree,*normal,socket(*normal,"Normal",false),*bump,socket(*bump,"Normal",true));}
  }
  BKE_ntree_update_after_single_tree_change(*main,tree);ids.push_back(&material->id);
 }
 PartialWriteContext writer(*main);for(auto *id:ids)writer.id_add(id,{PartialWriteContext::ADD_DEPENDENCIES|PartialWriteContext::MAKE_LOCAL|PartialWriteContext::SET_FAKE_USER});ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto release=qScopeGuard([&]{BKE_reports_free(&reports);});require(writer.write(path.toUtf8().constData(),G_FILE_COMPRESS,BLO_WRITE_PATH_REMAP_NONE,reports),"Cannot write private Bump fixture");
}
QJsonArray source_state(const NativeMaterialGraph &graph){QJsonArray state;for(const auto &node:graph.material->nodetree->nodes){QJsonArray sockets;for(const auto &input:node.inputs){QJsonObject value{{"socket",input.identifier}};if(input.type==blender::SOCK_FLOAT&&input.default_value)value["value"]=static_cast<const blender::bNodeSocketValueFloat *>(input.default_value)->value;if(input.link){value["from_node"]=input.link->fromnode->name;value["from_socket"]=input.link->fromsock->identifier;}sockets.append(value);}state.append(QJsonObject{{"node",node.name},{"type",node.idname},{"custom1",node.custom1},{"inputs",sockets}});}return state;}
const auto &bump(const NativeMaterialGraph &graph){const auto it=std::find_if(graph.channels.begin(),graph.channels.end(),[](const auto &c){return c.channel==4;});require(it!=graph.channels.end(),"Bump capability missing");return *it;}
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);QApplication app(argc,argv);
 try{
  require(argc==3,"Expected private output and explicit source manifest");const QString out=argv[1];QDir().mkpath(out);BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");struct Close{BlenderHost &host;~Close(){host.shutdown();}}close{host};QJsonArray actual,synthetic;QFile manifest(argv[2]);require(manifest.open(QIODevice::ReadOnly),"Missing explicit source manifest");
  {using namespace blender;bNode a{},b{};STRNCPY(a.idname,"NodeReroute");STRNCPY(b.idname,"NodeReroute");bNodeSocket input_a{},input_b{},normal{};a.inputs.first=&input_a;b.inputs.first=&input_b;bNodeLink root{},to_a{},to_b{};root.fromnode=&a;normal.link=&root;require(!native_bump_capability(normal).neutral,"Unconnected reroute was invented as a geometry normal");to_b.fromnode=&b;input_a.link=&to_b;to_a.fromnode=&a;input_b.link=&to_a;const auto cyclic=native_bump_capability(normal);require(!cyclic.neutral&&cyclic.reason.find("cyclic")!=std::string::npos,"Cyclic Normal chain was admitted");}
  for(const auto &entry:QJsonDocument::fromJson(manifest.readAll()).array()){
   const auto test=entry.toObject();const auto graph=await(host.prepare_material_graph(test["file"].toString(),test["material"].toString(),std::make_shared<std::atomic_bool>(false)));const auto &channel=bump(*graph);const auto name=test["material"].toString();const bool neutral=name=="Particle-Sprinkle";require(bool(channel.tree)==neutral,"Actual source Bump capability changed");const QString expected=name.contains("mClay")?"active Bump Height and a custom Normal":name=="Particle-Confetti"?"active Bump Height requires":neutral?QString{}:"Normal Map source has no authored scalar Height";require(neutral||channel.error.contains(expected),"Actual source diagnostic lost provenance");require(graph->original_inputs_preserved,"Classification edited source graph");actual.append(QJsonObject{{"material",name},{"neutral",neutral},{"error",channel.error},{"source_preserved",true}});
  }
  const auto library=out+"/bump-source.blend";await(std::async(std::launch::async,[&]{fixture(library);}).share());std::vector<PreparedBlendMaterial> sources;
  for(const auto *name:{"Unlinked","NoHeight","NoHeightChain","ActiveHeight","CustomNormal","ActiveCustomNormal"}){
   auto source=await(host.import_native_material(library,name,std::make_shared<std::atomic_bool>(false)));const bool neutral=std::string_view(name)=="Unlinked"||std::string_view(name).starts_with("NoHeight");require(bool(bump(*source.native_graph).tree)==neutral,"Synthetic Bump classification differs");require(native_material_channel_supported(source.material,4)==neutral,"Asset capability contradicts native graph");require(!native_material_channel_supported(source.material,3),"Internal Normal became advertised");
   if(!neutral){const auto error=bump(*source.native_graph).error;require(error.contains(std::string_view(name)=="CustomNormal"?"custom Normal Map":std::string_view(name)=="ActiveCustomNormal"?"active Bump Height and a custom Normal":"active Bump Height requires"),"Synthetic source diagnostic lost cause");}
   const auto source_before=source_state(*source.native_graph);if(std::string_view(name)!="Unlinked"){bool literal=false;for(const auto &n:source.native_graph->material->nodetree->nodes)if(STREQ(n.idname,"ShaderNodeBump"))for(const auto &input:n.inputs)if(STREQ(input.identifier,"Height")&&static_cast<const blender::bNodeSocketValueFloat *>(input.default_value)->value==17)literal=true;require(literal,"Classification modified ignored source Height default");}const auto original=native_material_description_json(source.native_graph->description);auto restored=await(host.prepare_material_resource(source.material,std::make_shared<std::atomic_bool>(false),source.textures));require(source_before==source_state(*restored)&&source_before==source_state(*source.native_graph),"Packed source roundtrip modified source Bump defaults or links");require(restored->original_inputs_preserved&&original==native_material_description_json(restored->description),"Packed source roundtrip changed native interface or caps");require(source.native_graph->original_inputs_preserved,"Source preparation mutated original graph");synthetic.append(QJsonObject{{"material",name},{"neutral",neutral},{"error",bump(*source.native_graph).error},{"packed_source_caps_roundtrip",true}});sources.push_back(std::move(source));
  }
  int gpu_checks=0;bool project_roundtrip=false;
  if(!qEnvironmentVariableIsSet("BTS_NATIVE_BUMP_CPU_ONLY")){
   QOpenGLContext context;context.setFormat(format);require(context.create(),"GPU context failed");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(context.makeCurrent(&surface),"GPU owner activation failed");QOpenGLFramebufferObject framebuffer(128,128,QOpenGLFramebufferObject::CombinedDepthStencil);host.initialize_gpu(context,surface,framebuffer.handle());
   const auto shutdown=qScopeGuard([&]{sources.clear();host.shutdown();});
   for(const auto &source:sources)if(bump(*source.native_graph).tree){host.with_gpu([&]{using namespace blender;const auto original=source_state(*source.native_graph);NativeMaterialGpuProgram program(*bump(*source.native_graph).tree,source.native_graph->parameters.size(),4);auto *target=GPU_texture_create_2d("Native neutral Bump oracle",17,9,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr);require(target,"Native Bump target allocation failed");const auto release=qScopeGuard([&]{GPU_texture_free(target);});program.update_ramps(source.native_graph->description);NativeMaterialGpuContext context;program.dispatch(target,source.native_graph->parameters,context,{0,0,17,9});auto *pixels=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));require(pixels,"Native Bump readback failed");const auto free=qScopeGuard([&]{MEM_delete(pixels);});for(int i=0;i<17*9;++i)for(int c=0;c<4;++c){require(pixels[i*4+c]==(c==3?1.f:.5f),"Native no-op Bump is not exact .5 grayscale");++gpu_checks;}require(original==source_state(*source.native_graph),"Native Bump GPU execution modified encoded source defaults or links");});}
   const auto geometry=out+"/geometry.blend";await(std::async(std::launch::async,[&]{native_object_demo::fixture(geometry);}).share());NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;options.blend.objects={"AW_Object_A","AW_Object_B"};const auto imported=host.new_project(geometry.toStdString(),qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(),&options);TextureDocument document(options,imported,host.scene_snapshot());auto &set=document.set(document.texture_sets().front().material_session_uid);auto &source=sources[2];AssetCatalog catalog(out+"/catalog");catalog.add(source.material);const auto id=catalog.retain_project(source.material);const auto asset=*std::find_if(catalog.assets().begin(),catalog.assets().end(),[&](const auto &a){return a.id==id;});host.install_native_material(id,asset.hash(),source.native_graph);const auto layer=apply_native_material_asset(set,asset);require(TextureDocument::layer(set,layer).channels[4]&&!TextureDocument::layer(set,layer).channels[3],"Native Fill did not retain neutral Bump/hidden Normal policy");set.precision[4]=ChannelPrecision::L32F;
   const InspectionMaterial inspection{set.material_session_uid,set.base_values,{}};host.set_selected_material(set.material_session_uid);host.set_inspection(InspectionMode::Material,std::span(&inspection,1));host.apply_texture_set(set,set.texture_resolution);wait([&]{host.flush_texture_updates();return !host.texture_updates_pending();});
   const auto before=native_material_fill_json(TextureDocument::layer(set,layer).fill);const auto project=out+"/bump-project.blend";const auto frame=[&]{host.flush_texture_updates();QCoreApplication::processEvents();};host.begin_save_project(document,project.toStdString(),catalog.project_assets());wait([&]{frame();host.poll_save_project();return !host.save_project_pending();});require(QFile::rename(library,library+".hidden"),"Cannot hide private source before Open");const auto unhide=qScopeGuard([&]{QFile::rename(library+".hidden",library);});document=host.open_project(project.toStdString());const auto resources=host.project_resources();const auto stored=std::find_if(resources.begin(),resources.end(),[&](const auto &a){return a.id==id;});require(stored!=resources.end(),"Packed source missing after Open");auto reopened=await(host.prepare_material_resource(*stored,std::make_shared<std::atomic_bool>(false),resources));require(bump(*reopened).tree&&reopened->description.channels==source.native_graph->description.channels,"Open changed native no-op Bump capabilities");auto &opened=document.set(document.texture_sets().front().material_session_uid);require(native_material_fill_json(TextureDocument::layer(opened,layer).fill)==before&&TextureDocument::layer(opened,layer).channels[4]&&!TextureDocument::layer(opened,layer).channels[3],"Open changed authored Bump/Normal bindings");project_roundtrip=true;
  }
  QFile result(out+"/verified.json");require(result.open(QIODevice::WriteOnly),"Cannot save proof");result.write(QJsonDocument(QJsonObject{{"actual",actual},{"synthetic",synthetic},{"gpu_components",gpu_checks},{"neutral",.5},{"project_caps_source_free_save_open",project_roundtrip},{"source_preserved",true},{"invalid_reroute_chains_rejected",true},{"normal_artist_export_unchanged",true},{"cpu_native_composition",false}}).toJson());std::cout<<"PASS native Bump capabilities\n";return 0;
 }catch(const std::exception &e){std::cerr<<"NATIVE_BUMP_FAIL "<<e.what()<<"\n";return 1;}
}
