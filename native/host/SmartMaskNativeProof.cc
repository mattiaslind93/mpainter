#include "SmartMaskNativeProof.hh"
#include "BlenderHost.hh"
#include "OrbitCamera.hh"
#include "BuiltinSmartMasks.hh"
#include "SmartMaskProjectWitness.hh"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <chrono>
#include <thread>
namespace bts {
int run_smart_mask_cold_open_proof(BlenderHost &host,const QString &project,const QString &directory){
 if(project.isEmpty()||qEnvironmentVariableIsSet("BTS_VERIFY_GPU_STACK"))throw std::runtime_error("Cold smart-mask proof requires a packed project and no GPU self-test/catalog warmup");
 const auto before=builtin_smart_mask_rcc_decodes();if(before!=0)throw std::runtime_error("Cold smart-mask process decoded bundled inputs before Open");
 QFile expected_file(project+".smart-mask-witness.json");if(!expected_file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read independent pre-Save smart-mask witness");
 const auto expected=QJsonDocument::fromJson(expected_file.readAll()).object();
 auto document=host.open_project(project.toStdString());
 auto proof=smart_mask_proof::validate(document,expected); // Refs, data and identity first.
 const auto after_open=builtin_smart_mask_rcc_decodes();if(after_open!=0)throw std::runtime_error("Cold Open used bundled PNG fallback");
 const auto resources=host.project_resources();std::set<std::string> packed_hashes;
 for(const auto &asset:resources)if(asset.scope==AssetScope::Project&&asset.smart_mask)for(const auto &effect:mask_effects_recursive(asset.smart_mask->effects))if(effect.mapping.projection.texture){const auto &image=*effect.mapping.projection.texture;if(!image.pixels)throw std::runtime_error("Packed smart-mask bank did not hydrate image data");packed_hashes.insert(image.hash);}
 if(packed_hashes.size()!=8)throw std::runtime_error("Cold Open has no packed Project resource bank for all eight image inputs");
 proof["rcc_decodes_before"]=qint64(before);proof["rcc_decodes_after_open"]=qint64(after_open);proof["packed_source_hashes"]=int(packed_hashes.size());
 QFile data_output(directory+"/smart-mask-cold-data.json");if(!data_output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write cold smart-mask data proof");data_output.write(QJsonDocument(proof).toJson());data_output.close();
 // The ordinary workspace reapplies authored sets after Open. The isolated
 // headless proof must schedule the same production GPU path before probing.
 host.rebind_texture_sets(document);
 std::set<std::uint32_t> materials;for(const auto &set:document.texture_sets())for(const auto layer:expected["layers"].toArray())if(set.id==QUuid(layer.toObject()["set"].toString()))materials.insert(set.material_session_uid);
 // As in ViewportHeadlessDemo, real EEVEE frames bind the ordinary material
 // images. Rebinding alone deliberately leaves unobserved channels deferred.
 const auto scene=host.scene_snapshot();OrbitCamera camera;
 camera.frame_bounds({scene.minimum[0],scene.minimum[1],scene.minimum[2]},
                     {scene.maximum[0],scene.maximum[1],scene.maximum[2]},{1000,700});
 ViewFrame frame;frame.pane={ViewKind::Scene,0,0,1000,700};
 const auto view=camera.view_matrix(),projection=camera.projection_matrix({1000,700});
 std::copy_n(view.constData(),16,frame.view.begin());std::copy_n(projection.constData(),16,frame.projection.begin());
 const auto clip=camera.clipping_range();frame.clipping={clip.first,clip.second};
 int drawn_frames=0;
 for(const auto uid:materials){host.set_selected_material(uid);for(int i=0;i<8;++i){host.draw(std::span<const ViewFrame>(&frame,1),1000,700);++drawn_frames;QCoreApplication::processEvents();}}
 proof["normal_offscreen_frames"]=drawn_frames;
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
 while(host.texture_updates_pending()){host.flush_texture_updates();QCoreApplication::processEvents();if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Cold smart-mask GPU preparation timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 double maximum=0;for(const auto uid:materials)maximum=std::max(maximum,host.verify_gpu_texture_stack(uid));
 const auto after_gpu=builtin_smart_mask_rcc_decodes();if(after_gpu!=0)throw std::runtime_error("Cold smart-mask GPU phase used bundled PNG fallback");
 proof["rcc_decodes_before"]=qint64(before);proof["rcc_decodes_after_open"]=qint64(after_open);proof["rcc_decodes_after_gpu"]=qint64(after_gpu);proof["packed_source_hashes"]=int(packed_hashes.size());proof["native_gpu_maximum_error"]=maximum;
 QFile output(directory+"/smart-mask-cold-open.json");if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write cold smart-mask proof");output.write(QJsonDocument(proof).toJson());
 std::printf("BTS_SMART_MASK_COLD_OPEN_PASS bindings=16 shared_images=8 rcc_decodes=0 native_gpu_maximum=%.9g\n",maximum);return 0;
}
}
