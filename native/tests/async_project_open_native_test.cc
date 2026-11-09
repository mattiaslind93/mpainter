// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "BlenderHost.hh"
#include "OwnerWorkerJob.hh"
#include "ProjectIO.hh"
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "SmartMaterialPreset.hh"
#include "FillTextureIO.hh"
#include "FillTextureJSON.hh"
#include "NumericLocale.hh"
#include "BKE_global.hh"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextStream>
#include <QElapsedTimer>
#include <QThread>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <cmath>
#include <cstdio>
using namespace bts;
namespace {
void require(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
QStringList policy(){return {"spsm:policy:bitmap-roughness=raw-r-ignore-a","spsm:policy:bitmap-bytes=little-endian","spsm:policy:bitmap-rows=first-row-v0","spsm:policy:bitmap-alpha=data-components","spsm:policy:bitmap-uv=projection0-centered-repeat-factors","spsm:policy:bitmap-sampling=hq-repeat"};}
}
int main(int argc,char **argv){
 QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);QSurfaceFormat::setDefaultFormat(format);initialize_numeric_locale();QApplication app(argc,argv);
 try{
  require(argc==3,"Expected OUTPUT_DIRECTORY and private copied Wood SPSM fixture");const auto directory=QString::fromLocal8Bit(argv[1]),input=QString::fromLocal8Bit(argv[2]);QDir().mkpath(directory);
  require(QFileInfo(input).absoluteFilePath().startsWith(QFileInfo(directory).absoluteFilePath()+"/"),"Fixture must be the verifier's own private input copy");
  qputenv("BTS_RESOURCE_CACHE",(directory+"/first-cache").toUtf8());configure_spsm_source_hydration();
  BlenderHost host(QCoreApplication::applicationFilePath().toStdString(),"");QOpenGLContext context;context.setFormat(format);require(context.create(),"Offscreen GL context failed");QOffscreenSurface surface;surface.setFormat(context.format());surface.create();require(surface.isValid()&&context.makeCurrent(&surface),"Offscreen activation failed");QOpenGLFramebufferObject target(256,256,QOpenGLFramebufferObject::CombinedDepthStencil);require(target.isValid(),"Framebuffer failed");struct Close{BlenderHost &h;~Close(){h.shutdown();}}close{host};host.initialize_gpu(context,surface,target.handle());
  require(QString::fromStdString(host.renderer()).contains("NVIDIA",Qt::CaseInsensitive),"Hardware native GPU renderer required");
  const auto mesh=directory+"/quad.obj";{QFile file(mesh);require(file.open(QIODevice::WriteOnly),"Cannot write private mesh");file.write("o OpenFixture\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");}
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;options.absolute_mesh_path=mesh.toStdString();
  const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/default-studio.hdr");
  const auto create=[&]{auto summary=host.new_project(mesh.toStdString(),environment.toStdString(),&options);return TextureDocument(options,summary,host.scene_snapshot());};
  auto document=create();
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,256,256};QMatrix4x4 view,projection;view.translate(-.5f,-.5f,-5);projection.ortho(-.6f,.6f,-.6f,.6f,.01f,100.f);std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());frame.uv_center={.5,.5};frame.uv_extent=1.2;
  const auto publish=[&](bool preload=false,InspectionMode mode=InspectionMode::Material){auto &set=document.set(document.texture_sets().front().material_session_uid);host.set_selected_material(set.material_session_uid);host.prepare_paint_texture(set);const InspectionMaterial inspected{set.material_session_uid,set.base_values,{},nullptr,128};host.set_inspection(mode,std::span(&inspected,1));std::vector<std::shared_ptr<const FillTexture>> textures;for(const auto &layer:set.layers)for(int channel=0;channel<15;++channel)if(auto texture=fill_channel_texture(layer.fill,channel))textures.push_back(texture);QElapsedTimer elapsed;elapsed.start();while(preload&&!host.prepare_texture_resources(textures)){context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<60000,"Source GPU upload timed out");}host.apply_texture_set(set,128);do{context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);QCoreApplication::processEvents();require(elapsed.elapsed()<60000,"Native GPU publication timed out");}while(host.texture_updates_pending());};
  std::vector<std::array<int,2>> positions{{7,13},{31,23},{73,91},{113,45}};std::vector<std::array<float,4>> before;QJsonValue descriptor;QString source_hash;const auto project=directory+"/packed-bitmap.blend";
  {
   StudioAsset source;source.filename="used_varnish_wood.spsm";source.name="Packed Wood source";source.id=QUuid::createUuid();source.scope=AssetScope::Project;source.usages={"smartmaterial"};source.smart_material=true;source.search_terms=policy();QFile file(input);require(file.open(QIODevice::ReadOnly),"Cannot read private fixture");source.bytes=file.readAll();source_hash=source.hash();
   source=std::async(std::launch::async,[source]()mutable{hydrate_asset_presets(source);return source;}).get();require(bool(source.material_preset),"Explicit bitmap source did not hydrate");
   const TextureLayer *wood=nullptr;for(const auto &layer:source.material_preset->set.layers)if(layer.name=="Wood_Worn_base"&&fill_channel_texture(layer.fill,2))wood=&layer;require(wood,"Wood bitmap branch missing");auto &set=document.set(document.texture_sets().front().material_session_uid);set.layers={*wood};set.layers[0].parent={};set.layers[0].depth=0;set.layers[0].visible=true;set.layers[0].channels={false,false,true,false,false};set.layers[0].mask.reset();set.selected_layer=set.layers[0].id;set.precision[2]=ChannelPrecision::L32F;descriptor=fill_texture_json(fill_channel_texture(set.layers[0].fill,2));
   publish(true);before=host.diagnostic_gpu_pixels(set.material_session_uid,2,positions);float lo=before.front()[0],hi=lo;for(const auto &pixel:before){lo=std::min(lo,pixel[0]);hi=std::max(hi,pixel[0]);require(pixel[3]>.99f,"Bitmap branch did not cover its GPU target");}require(hi-lo>1e-5f,"Bitmap probes are constant/background, not a source witness");host.save_project(document,project.toStdString(),{source});
  }
  // Retire every strong source/pixel owner before the cold Open. Neither source
  // path nor first cache remains reachable; the new root changes bitmap UUIDs.
  document=create();publish();require(QFile::rename(input,input+".held"),"Cannot hide own input copy");require(QDir(directory+"/first-cache").removeRecursively(),"Cannot remove own first cache");qputenv("BTS_RESOURCE_CACHE",(directory+"/second-cache").toUtf8());
  const auto old_json=document.to_json();auto *old_main=blender::G.main;const auto old_id=document.id();
  bool baseline=false;try{auto rejected=PendingProject::read(project.toStdString());}catch(const std::exception &error){std::printf("BTS_ASYNC_OPEN_BASELINE %s\n",error.what());baseline=QString::fromUtf8(error.what()).contains("Packed fill texture is missing");}require(baseline,"GUI cold Open did not reproduce unresolved bitmap defect");require(blender::G.main==old_main&&document.to_json()==old_json,"Baseline rejection changed live project");
  OwnerWorkerJob<std::unique_ptr<PendingProject>> opener;int beats=0,ready=0,errors=0,stages=0,old_draws=0;QString last_error,draw_error;bool main_untouched=true;QTimer heartbeat;heartbeat.setInterval(1);QObject::connect(&heartbeat,&QTimer::timeout,[&]{++beats;main_untouched&=blender::G.main==old_main;if(beats%10==0&&draw_error.isEmpty())try{context.makeCurrent(&surface);target.bind();host.draw(std::span(&frame,1),256,256);++old_draws;}catch(const std::exception &error){draw_error=QString::fromUtf8(error.what());}});heartbeat.start();
  const auto wait=[&]{QElapsedTimer elapsed;elapsed.start();while(opener.pending()){QCoreApplication::processEvents();QThread::msleep(1);require(elapsed.elapsed()<120000,"Async Open timed out");require(main_untouched&&draw_error.isEmpty(),"Worker changed/broke the drawing old scene");}};
  const auto failed=[&](const QString &text){++errors;last_error=text;};
  const auto read=[&](auto cancel,auto progress){return PendingProject::prepare(project.toStdString(),cancel,progress);};
  opener.start([&](auto cancel,auto progress){return PendingProject::prepare(project.toStdString(),cancel,[&](const QString &text){progress(text);if(text.contains("Restoring packed"))cancel->store(true);});},[]{return true;},[&](auto){++ready;},failed);wait();require(ready==0&&errors==0&&document.id()==old_id&&blender::G.main==old_main,"Cancelled Open changed old scene");
  int revision=0;const int requested_revision=revision;
  opener.start(read,[&]{return revision==requested_revision;},[&](auto){++ready;},failed);QTimer::singleShot(0,[&]{++revision;});wait();require(ready==0&&errors==1&&last_error.contains("changed")&&document.to_json()==old_json,"Stale Open was adopted");
  opener.start([&](auto cancel,auto progress){return PendingProject::prepare((directory+"/absent.blend").toStdString(),cancel,progress);},[]{return true;},[&](auto pending){pending->validate();++ready;},failed);wait();require(errors==2&&ready==0&&blender::G.main==old_main,"Failed Open changed old Main");
  opener.start([&](auto cancel,auto progress){return PendingProject::prepare(project.toStdString(),cancel,[&](const QString &text){progress(text);if(text.contains("Validating project metadata"))throw std::runtime_error("Injected detached validation failure");});},[]{return true;},[&](auto pending){pending->validate();++ready;},failed);wait();require(errors==3&&ready==0&&last_error.contains("Injected")&&blender::G.main==old_main,"Failed detached graph changed old Main");
  const int start_beats=beats;std::vector<StudioAsset> restored;bool cache_uuid_rebound=false;std::weak_ptr<const PreparedSpsmSource> restored_source;
  opener.start(read,[&]{return document.id()==old_id;},[&](auto pending){pending->validate();require(blender::G.main==old_main,"Main changed before adoption");require(pending->resources.size()==1&&pending->resources[0].hash()==source_hash,"Packed source identity changed");auto texture=fill_channel_texture(pending->document->texture_sets().front().layers.front().fill,2);require(texture&&texture->pixels&&fill_texture_json(texture)==descriptor,"Cross-cache packed descriptor/binding changed");for(const auto &layer:pending->resources[0].material_preset->set.layers)if(auto source_texture=fill_channel_texture(layer.fill,2);source_texture&&source_texture->hash==texture->hash)cache_uuid_rebound|=source_texture->asset!=texture->asset;require(cache_uuid_rebound,"New cache did not prove saved descriptor UUID rebinding");restored_source=prepared_spsm_source(pending->resources[0]);restored=pending->resources;heartbeat.stop();document=host.adopt_project(std::move(pending),project.toStdString());++ready;},failed,[&](const QString &){++stages;});wait();require(ready==1&&errors==3&&beats-start_beats>1&&stages>0&&old_draws>2,"Open did not deliver asynchronously");
  publish();auto after=host.diagnostic_gpu_pixels(document.texture_sets().front().material_session_uid,2,positions);double maximum=0;for(std::size_t i=0;i<before.size();++i)for(int c=0;c<4;++c)maximum=std::max(maximum,std::abs(double(before[i][c])-after[i][c]));require(maximum<1e-6,"Packed bitmap GPU pixels changed");publish(false,InspectionMode::Roughness);auto inspected_pixels=host.diagnostic_gpu_pixels(document.texture_sets().front().material_session_uid,2,positions);for(std::size_t i=0;i<before.size();++i)for(int c=0;c<4;++c)require(std::abs(before[i][c]-inspected_pixels[i][c])<1e-6,"Native Roughness inspection changed restored bitmap values");
  opener.shutdown();restored.clear();document=create();publish();require(restored_source.expired(),"Source owners survived retirement before cold batch Open");qputenv("BTS_RESOURCE_CACHE",(directory+"/third-cache").toUtf8());document=host.open_project(project.toStdString());auto batch_texture=fill_channel_texture(document.texture_sets().front().layers.front().fill,2);require(batch_texture&&batch_texture->pixels&&fill_texture_json(batch_texture)==descriptor,"Batch worker Open lost packed bitmap binding");publish();auto batch=host.diagnostic_gpu_pixels(document.texture_sets().front().material_session_uid,2,positions);double batch_error=0;for(std::size_t i=0;i<before.size();++i)for(int c=0;c<4;++c)batch_error=std::max(batch_error,std::abs(double(before[i][c])-batch[i][c]));require(batch_error<1e-6,"Cold batch worker Open changed GPU pixels");
  QFile output(directory+"/verified.json");require(output.open(QIODevice::WriteOnly),"Cannot write proof");output.write(QJsonDocument(QJsonObject{{"baseline_unresolved_bitmap",baseline},{"timer_heartbeats",beats},{"open_heartbeats",beats-start_beats},{"progress_deliveries",stages},{"old_scene_native_draws",old_draws},{"detached_validation_failure_preserved",true},{"cancel_preserved_old_main",true},{"failure_preserved_old_main",true},{"stale_revision_rejected",true},{"new_cache_root",true},{"cache_uuid_rebound",cache_uuid_rebound},{"original_input_missing",true},{"material_mode_native_binding",true},{"roughness_inspection_exact",true},{"gpu_max_error",maximum},{"batch_worker_open",true},{"batch_gpu_max_error",batch_error},{"cpu_composite",0}}).toJson());std::puts("BTS_ASYNC_PROJECT_OPEN PASS");return 0;
 }catch(const std::exception &error){std::fprintf(stderr,"BTS_ASYNC_PROJECT_OPEN_FAILED %s\n",error.what());return 1;}
}
