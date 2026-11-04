#include "NativeMaterialHostQueueDemo.hh"
#include "BlenderHost.hh"
#include "NativeMaterialSourceLoader.hh"
#include "NativeMaterialApply.hh"
#include "NativeMaterialMultiObjectDemo.hh"
#include "NativeMaterialCaptureDemo.hh"
#include "NativeMaterialThumbnailTest.hh"
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>
namespace bts {
QJsonObject verify_native_host_material_queue(BlenderHost &host,const std::function<void()> &frame,const std::function<QImage()> &capture)
{
 if(qEnvironmentVariableIsSet("BTS_NATIVE_THUMBNAIL_TEST"))return {{"native_thumbnails",verify_native_material_thumbnails(host,frame,capture)}};
 if(qEnvironmentVariableIsSet("BTS_NATIVE_CAPTURE_TEST"))return {{"native_capture",verify_native_capture(host,frame)}};
 QFile file(qEnvironmentVariable("BTS_NATIVE_MATERIAL_CASES"));if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Native host queue requires an explicit source manifest");const auto cases=QJsonDocument::fromJson(file.readAll()).array();if(cases.isEmpty())throw std::runtime_error("Native host queue source manifest is empty");const auto source=cases.first().toObject();
 auto prepared=host.import_native_material(source["file"].toString(),source["material"].toString(),std::make_shared<std::atomic_bool>(false)).get();
 for(int i=0;i<8;++i)frame();
 NativeMaterialSourceLoader loader(host,nullptr,[]{},[](const QString &){});int completed=0;QString failure;
 loader.prepared(prepared,[&](const StudioAsset &asset){if(asset.id!=prepared.material.id)throw std::runtime_error("Prepared import changed identity");++completed;},[&](const QString &message){failure=message;});
 auto state=host.native_material_status(prepared.material.id);if(!state.pending||state.ready)throw std::runtime_error("Native host did not schedule pending preparation");QElapsedTimer elapsed;elapsed.start();double maximum_frame=0;int frames=0;
 while(loader.pending()){QElapsedTimer clock;clock.start();frame();maximum_frame=std::max(maximum_frame,clock.nsecsElapsed()/1e6);QCoreApplication::processEvents(QEventLoop::AllEvents,1);state=host.native_material_status(prepared.material.id);if(++frames>3000)throw std::runtime_error("Native host preparation queue did not drain");}
 if(completed!=1||!state.ready||!failure.isEmpty())throw std::runtime_error(("Native host preparation failed: "+failure).toStdString());
 const auto alias=QUuid::createUuid();host.alias_native_material(prepared.material.id,alias);
 if(!host.has_native_material(alias))throw std::runtime_error("Project alias did not retain prepared GPU source");
 // Reopening has a fresh registry: exercise worker hydration and callback
 // coalescing under a new resource identity, not only the resident fast path.
 auto reopened=prepared.material;reopened.id=QUuid::createUuid();int restored=0;
 const auto ready=[&](const StudioAsset &asset){if(asset.id!=reopened.id)throw std::runtime_error("Hydration changed resource identity");++restored;};
 const auto failed=[&](const QString &message){failure=message;};
 loader.ensure(reopened,prepared.textures,ready,failed);loader.ensure(reopened,prepared.textures,ready,failed);
 int restore_frames=0;while(loader.pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,1);if(++restore_frames>5000)throw std::runtime_error("Native hydration did not drain");}
 if(restored!=2||!failure.isEmpty()||!host.has_native_material(reopened.id))throw std::runtime_error(("Native worker hydration failed: "+failure).toStdString());
 loader.ensure(reopened,{},ready,failed);if(restored!=3||loader.pending())throw std::runtime_error("Resident material was unnecessarily hydrated");
 auto cancelled=prepared;cancelled.material.id=QUuid::createUuid();loader.prepared(cancelled,[](const StudioAsset &){throw std::runtime_error("Cancelled native import published an asset");},failed);frame();loader.cancel(cancelled.material.id);
 if(loader.pending()||host.native_material_status(cancelled.material.id).pending||host.has_native_material(cancelled.material.id)||!host.has_native_material(alias))throw std::runtime_error("Native cancellation retired a published alias or retained pending work");
 QJsonObject activation;
 if(qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_ACTIVATION_TEST")){
  const auto drain=[&]{int count=0;while(loader.pending()||host.texture_updates_pending()){frame();QCoreApplication::processEvents(QEventLoop::AllEvents,1);if(++count>6000)throw std::runtime_error("Native activation did not drain");}if(!failure.isEmpty())throw std::runtime_error(failure.toStdString());};
  NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;
  const auto mesh=qEnvironmentVariableIsSet("BTS_NATIVE_MULTI_OBJECT_TEST")?qEnvironmentVariable("BTS_ROOT")+"/mpainter/fixtures/single_tile.obj":qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST")+"/native-single-object.obj";
  {QFile file(qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST")+"/native-single-object.obj");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot create native activation fixture");file.write("mtllib native-single-object.mtl\no NativeFixture\nv -1 -1 0\nv 1 -1 0\nv 1 1 0\nv -1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl NativeFixtureSurface\nf 1/1 2/2 3/3 4/4\n");}
  {QFile file(qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST")+"/native-single-object.mtl");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot create native activation material fixture");file.write("newmtl NativeFixtureSurface\nKd 1 1 1\nPr 0.5\nPm 0\n");}
  const auto environment=qEnvironmentVariable("BTS_ENVIRONMENT",QCoreApplication::applicationDirPath()+"/5.2/datafiles/studiolights/world/default-studio.hdr");const auto imported=host.new_project(mesh.toStdString(),environment.toStdString(),&options);const auto scene=host.scene_snapshot();TextureDocument document(options,imported,scene);auto &set=document.set(scene.materials.front().session_id);
  MeshMap ao;ao.name="Ambient occlusion";auto pixels=std::make_shared<MeshMapPixels>(32,std::vector<float>(32*32*4,1));ao.tiles[1001]={32,pixels->hash,pixels};set.mesh_maps[3]=ao;
  AssetCatalog catalog(qEnvironmentVariable("BTS_RESOURCE_CACHE")+"/activation-shelf");for(const auto &image:prepared.textures)catalog.add(image);catalog.add(prepared.material);const auto retained=catalog.retain_project(prepared.material);
  const auto resource=*std::find_if(catalog.assets().begin(),catalog.assets().end(),[&](const auto &asset){return asset.id==retained;});
  loader.ensure(resource,catalog.assets(),[](const auto &){},failed);drain();host.validate_native_material_target(resource.id,set);
  const auto layer=apply_native_material_asset(set,resource);host.set_selected_material(set.material_session_uid);host.apply_texture_set(set,128);drain();
  const auto before=host.diagnostic_gpu_pixel(set.material_session_uid,0,64,64,1001);if(!before||(*before)[3]<.99f)throw std::runtime_error("Native fill did not reach a host channel image");
  const auto saved_description=native_material_description_json(*TextureDocument::layer(set,layer).fill.native_material);
  bool export_guard=false;try{ExportRequest request;request.tiles=host.export_source(document,{set.id});export_file_list(request);}catch(const std::runtime_error &error){export_guard=std::string(error.what()).find("GPU capture")!=std::string::npos;}
  if(!export_guard)throw std::runtime_error("Native export silently entered the uniform CPU evaluator");
  const auto path=qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST")+"/native-activation.blend";host.begin_save_project(document,path.toStdString(),catalog.project_assets());int saves=0;while(host.save_project_pending()){frame();host.poll_save_project();QCoreApplication::processEvents(QEventLoop::AllEvents,1);if(++saves>6000)throw std::runtime_error("Native project save did not drain");}
  auto opened=host.open_project(path.toStdString());const auto resources=host.project_resources();const auto retained_source=std::find_if(resources.begin(),resources.end(),[&](const auto &asset){return asset.id==retained;});if(retained_source==resources.end())throw std::runtime_error("Native project resource was not packed");
  loader.ensure(*retained_source,resources,[](const auto &){},failed);drain();auto &restored_set=opened.set(opened.texture_sets().front().material_session_uid);host.validate_native_material_target(retained,restored_set);host.apply_texture_set(restored_set,128);drain();
  if(native_material_description_json(*TextureDocument::layer(restored_set,layer).fill.native_material)!=saved_description)throw std::runtime_error("Native project parameters changed on reopening");const auto after=host.diagnostic_gpu_pixel(restored_set.material_session_uid,0,64,64,1001);if(!after)throw std::runtime_error("Native reopened GPU sample is missing");double error=0;for(int c=0;c<4;++c)error=std::max(error,std::abs(double((*before)[c]-(*after)[c])));if(error>1e-6)throw std::runtime_error("Native project reopened pixels differ");
  activation={{"new_project_apply",true},{"source_objects",int(scene.mesh_objects)},{"gpu_image_sample",QJsonArray{(*before)[0],(*before)[1],(*before)[2],(*before)[3]}},{"packed_save_open",true},{"reopen_maximum_error",error},{"parameter_metadata_exact",true},{"cpu_export_blocked",true}};
 }
 QJsonObject multi_object;if(qEnvironmentVariableIsSet("BTS_NATIVE_MULTI_OBJECT_TEST"))multi_object=verify_native_multi_object_fill(host,frame);
 return {{"multi_object",multi_object},{"material",prepared.material.name},{"prepared_via_host_frames",true},{"frames",frames},{"wall_ms",elapsed.nsecsElapsed()/1e6},{"maximum_frame_ms",maximum_frame},{"maximum_prepare_step_ms",state.maximum_step_ms},{"ready",state.ready},{"worker_hydrate_callbacks",restored},{"worker_hydrate_frames",restore_frames},{"project_alias_shared",true},{"cancel_without_publication",true},{"visible_windows",0},{"document_activation",activation}};
}
}
