#pragma once
#include "NativeMaterialProjectionWitness.hh"
#include "BlenderHost.hh"
#include "NativeMaterialSourceLoader.hh"
#include "OrbitCamera.hh"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <thread>
namespace bts {
// Explicit headless diagnostic. Validate retained bytes/parameters/pixels after
// normal Open and BEFORE the first explicit EEVEE draw/read below. Open itself
// may prepare native GPU resources; this does not claim no earlier GPU dispatch.
inline int run_native_projection_cold_proof(BlenderHost &host,const QString &directory){
 using projection_ui_proof::require;
 require(!qEnvironmentVariableIsSet("BTS_VERIFY_GPU_STACK"),"BJ cold proof must not warm self-tests/catalog");
 QFile expected_file(qEnvironmentVariable("BTS_BJ_PROJECTION_EXPECTED"));require(expected_file.open(QIODevice::ReadOnly),"BJ warm checkpoint manifest absent");
 const auto expected=QJsonDocument::fromJson(expected_file.readAll()).object();const auto checkpoints=expected["checkpoints"].toArray();
 require(checkpoints.size()==14,"BJ complete checkpoint matrix required");
 QJsonArray records;double maximum=0;int frames=0;
 for(const auto &value:checkpoints){const auto checkpoint=value.toObject();const auto project=checkpoint["project"].toString();
  QFile witness_file(project+".bj-witness.json");require(witness_file.open(QIODevice::ReadOnly),"BJ independent pre-Save data witness absent");
  const auto witness=QJsonDocument::fromJson(witness_file.readAll()).object();
  auto document=host.open_project(project.toStdString());
  auto data=projection_ui_proof::validate(document,host.project_resources(),witness);
  const auto scene=host.scene_snapshot();require(scene.mesh_objects==2&&scene.object_names.size()==2,"BJ cold actual owner membership differs");
  const TextureSet *target=nullptr;for(const auto &set:document.texture_sets())if(set.id==QUuid(checkpoint["set"].toString()))target=&set;
  require(target,"BJ cold persistent texture-set UUID absent");
  // A durable data record exists even if the later native GPU stage fails.
  data["checkpoint"]=checkpoint["name"];data["explicit_eevee_frames_before_validation"]=0;
  QFile data_file(directory+"/"+checkpoint["name"].toString()+"-data.json");require(data_file.open(QIODevice::WriteOnly),"BJ cold data receipt write");data_file.write(QJsonDocument(data).toJson());data_file.close();
  // Workspace Open restores missing native sources through this same loader.
  // Batch Open stages Raw Height/layered sources, but a None-only source is
  // deliberately lazy. Use only validated retained project bytes here.
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
  struct SourceHydration {StudioAsset asset;bool resident_before=false;bool ready=false;QString error;};
  std::map<QUuid,SourceHydration> hydration;
  const auto resources=host.project_resources();
  for(const auto &set:document.texture_sets())for(const auto &layer:evaluation_layers(set))if(layer.fill.native_material){
   const QUuid id(QString::fromStdString(layer.fill.material_resource));require(!id.isNull(),"BJ cold native source UUID absent");
   if(hydration.contains(id))continue;
   const auto found=std::find_if(resources.begin(),resources.end(),[&](const auto &asset){return asset.id==id;});
   require(found!=resources.end()&&is_native_material_asset(*found),"BJ cold retained native source absent");
   const bool resident=host.has_native_material(id);hydration.emplace(id,SourceHydration{*found,resident,resident,{}});
  }
  const auto hydration_summary=[&]{QJsonArray rows;for(const auto &[id,source]:hydration)rows.append(QJsonObject{{"id",id.toString()},{"hash",source.asset.hash()},{"resident_before",source.resident_before},{"loader_ready",source.ready},{"resident_after",host.has_native_material(id)},{"error",source.error}});return rows;};
  const auto write_hydration=[&]{QFile file(directory+"/"+checkpoint["name"].toString()+"-hydration.json");require(file.open(QIODevice::WriteOnly),"BJ cold source hydration receipt write");file.write(QJsonDocument(hydration_summary()).toJson());};
  write_hydration();
  NativeMaterialSourceLoader source_loader(host,nullptr,[]{},[](const QString &){});
  for(auto &[id,source]:hydration)if(!source.resident_before)source_loader.ensure(source.asset,resources,
   [&,id](const StudioAsset &ready){auto &entry=hydration.at(id);require(ready.id==id&&ready.hash()==entry.asset.hash(),"BJ cold loader source identity changed");entry.ready=true;write_hydration();},
   [&,id](const QString &error){hydration.at(id).error=error;write_hydration();});
  const auto require_hydration=[&]{for(const auto &[id,source]:hydration)if(!source.error.isEmpty())throw std::runtime_error(("BJ cold native source "+id.toString()+": "+source.error).toStdString());};
  host.rebind_texture_sets(document);host.set_selected_material(target->material_session_uid);
  OrbitCamera camera;camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},{scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700});
  ViewFrame frame;frame.pane={ViewKind::Scene,0,0,1000,700};const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());const auto clip=camera.clipping_range();frame.clipping={clip.first,clip.second};
  int checkpoint_frames=0;
  const auto queue_summary=[&]{const auto q=host.diagnostic_native_source_queues();return QJsonObject{{"stack_requests",q["stack_queued"].toObject().size()},{"uniform_requests",q["gpu_queued"].toObject().size()},{"pending_cpu",q["pending_cpu"]},{"running_cpu",q["running_cpu"]},{"native_gpu",q["bj_native_gpu"]}};};
  for(int i=0;i<8;++i){host.draw(std::span<const ViewFrame>(&frame,1),1000,700);++frames;++checkpoint_frames;QCoreApplication::processEvents();}
  const auto queued_after_initial_frames=queue_summary();const bool pending_after_initial_frames=host.texture_updates_pending();
  while(source_loader.pending()||host.texture_updates_pending()){
   require_hydration();
   // A workspace continues drawing while async bindings/programs settle.
   // Flush alone cannot provide the EEVEE binding/resume part of that path.
   host.draw(std::span<const ViewFrame>(&frame,1),1000,700);++frames;++checkpoint_frames;
   host.flush_texture_updates();QCoreApplication::processEvents();require(std::chrono::steady_clock::now()<deadline,"BJ cold native GPU preparation timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require_hydration();for(const auto &[id,source]:hydration)require(source.ready&&host.has_native_material(id),"BJ cold native source not resident after loader completion");write_hydration();
  int components=0;double error=0;
  const auto packets=checkpoint["gpu"].toArray();require(!packets.isEmpty(),"BJ warm GPU reference is empty");
  for(const auto &item:packets){const auto packet=item.toObject();require(QUuid(packet["set"].toString())==target->id,"BJ warm GPU packet owner differs");const auto points=packet["points"].toArray();require(points.size()==5,"BJ warm GPU point shape differs");std::array<std::array<int,2>,5> xy;
   for(int i=0;i<5;++i){const auto point=points[i].toObject()["xy"].toArray();require(point.size()==2,"BJ warm GPU coordinate absent");xy[i]={point[0].toInt(),point[1].toInt()};}
   const auto actual=host.diagnostic_gpu_pixels(target->material_session_uid,packet["channel"].toInt(),xy,packet["tile"].toInt());require(actual.size()==5,"BJ cold native GPU read unavailable");
   for(int i=0;i<5;++i){const auto want=points[i].toObject()["rgba"].toArray();require(want.size()==4,"BJ warm GPU RGBA absent");for(int c=0;c<4;++c){require(std::isfinite(actual[i][c])&&want[c].isDouble(),"BJ cold nonfinite/missing sample");error=std::max(error,std::abs(double(actual[i][c])-want[c].toDouble()));++components;}}
  }
  require(components>=120&&error<3e-6,"BJ cold native output differs from saved warm checkpoint");maximum=std::max(maximum,error);records.append(QJsonObject{{"checkpoint",checkpoint["name"]},{"data",data},{"gpu_components",components},{"maximum_error",error},{"tolerance",3e-6},{"normal_eevee_frames",checkpoint_frames},{"pending_after_initial_frames",pending_after_initial_frames},{"queued_after_initial_frames",queued_after_initial_frames},{"queued_when_ready",queue_summary()},{"source_hydration",hydration_summary()}});
 }
 QFile output(directory+"/native-projection-cold-data.json");require(output.open(QIODevice::WriteOnly),"BJ cold proof write");output.write(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"checkpoints",records},{"normal_eevee_frames",frames},{"maximum_error",maximum},{"reference","Same-binary saved GPU checkpoints; independent analytical projection math belongs to the kernel oracle"}}).toJson());output.close();
 std::printf("BTS_BJ_PROJECTION_COLD_DATA_PASS checkpoints=%lld frames=%d maximum=%.9g\n",static_cast<long long>(records.size()),frames,maximum);return 0;
}
}
