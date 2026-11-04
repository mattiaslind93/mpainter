#define BTS_IMAGE_VARIANT_NO_MAIN
#include "native_material_image_variant_test.cc"
#include "NativeMaterialImageJobs.hh"
#include "NativeMaterialImagePanel.hh"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QDeadlineTimer>
namespace {
NativeImageChangeRequest input(const QString &out){
 QDir().mkpath(out);const auto image=out+"/source.png",replacement=out+"/replacement.png",library=out+"/source.blend";
 QImage pixels(3,2,QImage::Format_RGBA8888);pixels.fill(QColor(224,32,96));require(pixels.save(image),"Cannot write source");pixels.fill(QColor(16,192,48));require(pixels.save(replacement),"Cannot write replacement");fixture(library,image,true,true);
 std::atomic_bool cancel=false;auto source=read_native_material_source(library,"Replacement-source",cancel);
 // Real top inputs include scalar, linked ramp and linked UV Mapping. The
 // Mapping feeds Tangent; image Color remains an independent nested branch.
 auto &root=*source.material->nodetree;bNode *top=nullptr;for(auto &node:root.nodes)if(STREQ(node.idname,"ShaderNodeGroup"))top=&node;require(top,"Missing top source group");auto &wrapper=*reinterpret_cast<bNodeTree *>(top->id);bNode *pbr=nullptr;for(auto &node:wrapper.nodes)if(STREQ(node.idname,"ShaderNodeBsdfPrincipled"))pbr=&node;require(pbr,"Missing grouped Principled source");
 const auto input_socket=[&](const char *name,const char *type){return std::string(wrapper.tree_interface.add_socket(name,"",type,NODE_INTERFACE_SOCKET_INPUT,nullptr)->identifier);};const auto mapping=input_socket("Mapping","NodeSocketVector"),metal=input_socket("Metallic","NodeSocketFloat"),rough=input_socket("Roughness","NodeSocketColor");auto &group_input=add(wrapper,"NodeGroupInput"),&coordinates=add(root,"ShaderNodeTexCoord"),&ramp=add(root,"ShaderNodeValToRGB");BKE_ntree_update(*source.main);
 for(auto *link=static_cast<bNodeLink *>(wrapper.links.first),*next=link;link;link=next){next=link->next;if(link->tonode==pbr&&STREQ(link->tosock->identifier,"Roughness"))bke::node_remove_link(&wrapper,*link);}
 bke::node_add_link(wrapper,group_input,socket(group_input,mapping.c_str(),false),*pbr,socket(*pbr,"Tangent",true));bke::node_add_link(wrapper,group_input,socket(group_input,metal.c_str(),false),*pbr,socket(*pbr,"Metallic",true));bke::node_add_link(wrapper,group_input,socket(group_input,rough.c_str(),false),*pbr,socket(*pbr,"Roughness",true));bke::node_add_link(root,coordinates,socket(coordinates,"UV",false),*top,socket(*top,mapping.c_str(),true));bke::node_add_link(root,ramp,socket(ramp,"Color",false),*top,socket(*top,rough.c_str(),true));BKE_ntree_update(*source.main);
 {
  bke::blendfile::PartialWriteContext writer(*source.main);writer.id_add(&source.material->id,{bke::blendfile::PartialWriteContext::ADD_DEPENDENCIES|bke::blendfile::PartialWriteContext::MAKE_LOCAL|bke::blendfile::PartialWriteContext::SET_FAKE_USER});ReportList reports{};BKE_reports_init(&reports,RPT_STORE);const auto clear=qScopeGuard([&]{BKE_reports_free(&reports);});require(writer.write(library.toUtf8().constData(),0,BLO_WRITE_PATH_REMAP_NONE,reports),"Cannot persist combined artist source fixture");
 }
 auto graph=finish_native_material_source(std::move(source),cancel);auto archive=archive_native_material(*graph,library,cancel,true);
 NativeImageChangeRequest request;request.source=native_material_asset("Replacement-source",archive.bytes,archive.description);request.resources=archive.textures;request.instance=graph->description;
 auto inventory=bts::native_material_image_bindings(*graph->material,cancel);request.binding=*std::find_if(inventory.bindings.begin(),inventory.bindings.end(),[](const auto &b){return b.groups.size()==3;});request.file=replacement;request.policy={NativeImageSourceSpace::Named,"sRGB"};bool mapped=false,scalar=false,has_ramp=false;for(auto &parameter:request.instance.parameters){if(parameter.mapping_transform_supported()){parameter.mapping_transform.offset={.2,-.3,.4};parameter.mapping_transform.rotation_xyz_radians={.1,.2,-.3};parameter.mapping_transform.scale={1.2,.8,1.1};mapped=true;}if(parameter.name=="Metallic"){parameter.value[0]=.625;scalar=true;}if(!parameter.ramp.isEmpty()){parameter.ramp["interpolation"]=int(COLBAND_INTERP_CONSTANT);has_ramp=true;}}require(mapped&&scalar&&has_ramp,"Combined source lacks real scalar/ramp/Mapping inputs");request.instance.validate();
 return request;
}
template<class F>void await(F done){QDeadlineTimer deadline(30000);while(!done()){require(!deadline.hasExpired(),"Native image jobs CPU deadline expired");QApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
}
int main(int argc,char **argv){
 QApplication app(argc,argv);std::setlocale(LC_NUMERIC,"C");std::cout<<std::unitbuf;
 try{
  require(argc==2,"Expected private output");BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");const auto close=qScopeGuard([&]{host.shutdown();});auto request=std::async(std::launch::async,[&]{return input(QString::fromLocal8Bit(argv[1]));}).get();
  if(qEnvironmentVariableIsSet("BTS_NATIVE_IMAGE_FIXTURE_ONLY")){
   QFile invalid(QString::fromLocal8Bit(argv[1])+"/invalid.png");require(invalid.open(QIODevice::WriteOnly),"Cannot write private invalid image");invalid.write("deliberately invalid encoded image");invalid.close();
   QJsonObject oracle{{"binding",native_image_binding_json(request.binding)},{"library_sha256",sha(load(QString::fromLocal8Bit(argv[1])+"/source.blend"))},{"source_sha256",sha(load(QString::fromLocal8Bit(argv[1])+"/source.png"))},{"replacement_sha256",sha(load(QString::fromLocal8Bit(argv[1])+"/replacement.png"))}};
   QJsonArray colors;for(auto rgb:std::array{std::array<float,4>{224/255.f,32/255.f,96/255.f,1},std::array<float,4>{16/255.f,192/255.f,48/255.f,1}}){IMB_colormanagement_transform_v4(rgb.data(),"sRGB","ACEScg");colors.append(QJsonArray{rgb[0],rgb[1],rgb[2]});}oracle["scene_colors"]=colors;QFile file(QString::fromLocal8Bit(argv[1])+"/artist-oracle.json");require(file.open(QIODevice::WriteOnly),"Cannot write artist oracle");file.write(QJsonDocument(oracle).toJson());std::cout<<"NATIVE_IMAGE_FIXTURE_PASS\n";return 0;
  }
  int prepared=0,published=0,discarded=0,errors=0,pings=0;bool current=true,ready=false,prepare_failure=false;std::map<QUuid,std::shared_ptr<NativeMaterialGraph>> staging;std::optional<NativeImageChange> last;
  QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{++pings;});heartbeat.start();
  NativeImageJobHooks hooks;
  hooks.prepare=[&](const QUuid &id,const QString &,auto graph){require(id!=request.source.id,"Staging reused published source UUID");++prepared;staging[id]=std::move(graph);if(prepare_failure)throw std::runtime_error("Injected owner prepare failure after registration");};
  hooks.status=[&](const QUuid &){NativeMaterialPreparationStatus state;state.pending=!ready;state.ready=ready;state.message="Test frame readiness";return state;};
  hooks.discard=[&](const QUuid &id){require(staging.erase(id)==1,"Candidate discarded twice or original discarded");++discarded;};
  hooks.publish=[&](NativeImageChange result,const QUuid &id){require(staging.contains(id),"GPU readiness owner missing");require(current,"Stale source published");++published;last=std::move(result);};hooks.redraw=[]{};hooks.error=[&](const QString &){++errors;};
  NativeMaterialImageJobs jobs(nullptr,hooks);NativeMaterialImageJobs::Availability inspected;int callbacks=0;
  const auto receive=[&](auto value){if(inspected)require(inspected.get()==value.get(),"Coalesced/cached inventory was recomputed");inspected=std::move(value);++callbacks;};const auto fail=[&](const QString &error){throw std::runtime_error(error.toStdString());};
  jobs.inspect(request.source,[](auto){throw std::runtime_error("Injected asynchronous target/notification failure");},fail);jobs.inspect(request.source,receive,fail);jobs.inspect(request.source,receive,fail);await([&]{return callbacks==2;});jobs.inspect(request.source,receive,fail);require(callbacks==3,"Cached inspection did not return immediately");require(inspected->bindings.size()==5,"Source inventory incomplete");require(errors==1,"Async owner notification escaped or was lost");errors=0;
  NativeMaterialImagePanel panel;panel.set_color_spaces({"sRGB","Non-Color"});panel.synchronize(inspected,false);auto *slot=panel.findChild<QComboBox *>("nativeImageSlot"),*space=panel.findChild<QComboBox *>("nativeImageSourceSpace");auto *file=panel.findChild<QPushButton *>("nativeImageFile"),*shelf=panel.findChild<QPushButton *>("nativeImageShelf");int choices=0;panel.requested=[&](const auto &binding,bool from_file,const auto &policy){require(binding.groups.size()==3&&from_file&&policy.source_space==NativeImageSourceSpace::OriginalDeclared,"Wrong exact panel binding/policy");++choices;};
  for(size_t i=0;i<inspected->bindings.size();++i){slot->setCurrentIndex(int(i));space->setCurrentIndex(1);if(!inspected->bindings[i].available())require(!file->isEnabled()&&!panel.findChild<QLabel *>("nativeImageStatus")->text().isEmpty(),"Unsupported row is offered without explanation");}
  const auto enabled=std::find_if(inspected->bindings.begin(),inspected->bindings.end(),[](const auto &row){return row.available();});slot->setCurrentIndex(int(enabled-inspected->bindings.begin()));space->setCurrentIndex(0);require(!file->isEnabled()&&!shelf->isEnabled(),"Unspecified source policy enabled replacement");space->setCurrentIndex(1);file->click();require(choices==1,"Native-specific file action missing");panel.set_busy(true);require(!file->isEnabled(),"Busy panel permits duplicate input");
  jobs.change(request,[&]{return current;});await([&]{return prepared==1;});require(published==0,"Published before GPU readiness");current=false;await([&]{return !jobs.pending();});require(discarded==1&&published==0,"Stale candidate was published or leaked");
  current=true;jobs.change(request,[&]{return current;});jobs.cancel();require(published==0,"Cancelled worker published");
  ready=true;jobs.change(request,[&]{return current;});await([&]{return published==1;});require(discarded==2&&staging.empty(),"Ready private alias not released");require(last&&last->prepared.asset.id!=request.source.id,"Original asset identity changed");for(size_t i=0;i<request.instance.parameters.size();++i)require(last->instance.parameters[i].value==request.instance.parameters[i].value&&last->instance.parameters[i].ramp==request.instance.parameters[i].ramp&&last->instance.parameters[i].mapping_transform==request.instance.parameters[i].mapping_transform,"Replacement discarded authored scalar/ramp/Mapping");
  auto unknown=request;unknown.policy={};jobs.change(unknown,[&]{return current;});await([&]{return !jobs.pending();});require(errors==1&&published==1,"Unknown policy changed document/catalog publication");
  auto reset=request;reset.reset_all=true;reset.source=last->prepared.asset;reset.resources=last->prepared.textures;reset.resources.push_back(request.source);reset.instance=last->instance;reset.instance.parameters.front().value[0]+=.125;
  const auto authored=reset.instance;const auto original_id=request.source.id;jobs.change(reset,[&]{return current;});await([&]{return published==2;});require(last->prepared.asset.id==original_id,"Reset did not restore FIRST original UUID");for(size_t i=0;i<authored.parameters.size();++i)require(last->instance.parameters[i].value==authored.parameters[i].value&&last->instance.parameters[i].ramp==authored.parameters[i].ramp&&last->instance.parameters[i].mapping_transform==authored.parameters[i].mapping_transform,"Reset discarded authored inputs");require(discarded==3&&staging.empty()&&pings>10,"Coordinator blocked heartbeat or retained a private alias");
  prepare_failure=true;jobs.change(request,[&]{return current;});await([&]{return !jobs.pending();});require(errors==2&&discarded==4&&staging.empty()&&published==2,"Partial owner preparation was not discarded atomically");
  std::cout<<"NATIVE_IMAGE_JOBS_PASS pings="<<pings<<" exact_inventory_cache=1 stale_cancel_atomic=1 reset_original=1\n";return 0;
 }catch(const std::exception &error){std::cerr<<"NATIVE_IMAGE_JOBS_FAIL "<<error.what()<<'\n';return 1;}
}
