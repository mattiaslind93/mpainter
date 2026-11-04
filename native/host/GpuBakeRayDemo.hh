#pragma once
#include "BlenderHost.hh"
#include "GpuBakeRays.hh"
#include "../tests/gpu_bake_ray_reference.hh"
#include "../tests/NativeBakeSamplesValidation.hh"
#include "ResourceIO.hh"
#include "BKE_main.hh"
#include "BKE_scene.hh"
#include "BKE_idprop.hh"
#include "DNA_scene_types.h"
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <bit>
namespace bts {
inline QJsonObject verify_gpu_bake_bvh(){
 const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 std::vector<bake_bvh::Triangle> triangles;
 for(int y=0;y<20;++y)for(int x=0;x<20;++x){const float a=x-10,b=y-10;triangles.push_back({{{a,b,1},{a+1,b,1},{a+1,b+1,1}}});triangles.push_back({{{a,b,1},{a+1,b+1,1},{a,b+1,1}}});}
 std::vector<FillSurfacePoint> small(31);
 for(std::size_t i=0;i<small.size();++i){auto &p=small[i];p.valid=i!=3;p.position={float(int(i%5)-2)*.1f,float(int(i%7)-3)*.1f,0};p.normal={i%2?.2f:0.f,i%3?.1f:0.f,1};}small[5].normal={0,0,0};
 int exact=0;
 for(bool inward:{false,true}){
  const auto reference=legacy_gpu_bake_cosine_occlusion(triangles,small,16,4,inward),actual=gpu_bake_cosine_occlusion(triangles,small,16,4,inward);
  require(reference.size()==actual.size(),"GPU BVH changed output size");
  for(std::size_t i=0;i<actual.size();++i){require(std::bit_cast<std::uint32_t>(actual[i])==std::bit_cast<std::uint32_t>(reference[i]),"GPU BVH changed the original sample/intersection/accumulation result");++exact;}
 }
 std::vector<FillSurfacePoint> points(2049);for(std::size_t i=0;i<points.size();++i){points[i].valid=true;points[i].normal={i%2?.2f:-.15f,i%3?.1f:0.f,1};points[i].position={float(int(i%31)-15)*.01f,float(int(i%29)-14)*.01f,0};}
 const auto old_work=resource_product(resource_product(triangles.size(),points.size()),8);require(old_work>8000000,"GPU fixture did not exceed the former total-work cap");
 bool old_rejected=false;try{legacy_gpu_bake_cosine_occlusion(triangles,points,8,4);}catch(const std::length_error &){old_rejected=true;}require(old_rejected,"Legacy GPU fixture did not reproduce the old rejection");
 GpuBakeRayStats stats;QElapsedTimer timer;timer.start();const auto actual=gpu_bake_cosine_occlusion(triangles,points,8,4,false,&stats);const auto gpu_ms=timer.elapsed();require(stats.batches>1,"GPU fixture did not exercise global sampling seeds across batches");
 double maximum_error=0;
 for(std::size_t i=0;i<points.size();++i){const auto expected=bake_cosine_occlusion(triangles,points[i].position,points[i].normal,8,4,std::uint32_t(i));maximum_error=std::max(maximum_error,std::abs(double(actual[i])-expected));}
 require(maximum_error<2e-6,"GPU BVH differs from independent CPU triangle ray reference");
 std::vector<bake_bvh::Triangle> overlap(2048,bake_bvh::Triangle{{{-20,-20,1},{20,-20,1},{0,20,1}}});std::vector<FillSurfacePoint> one(1);one[0].valid=true;
 GpuBakeRayStats resumed;const auto continued=gpu_bake_cosine_occlusion(overlap,one,1,4,false,&resumed),brute=legacy_gpu_bake_cosine_occlusion(overlap,one,1,4);
 require(resumed.dispatches>1&&std::bit_cast<std::uint32_t>(continued[0])==std::bit_cast<std::uint32_t>(brute[0]),"Resumed overlapping BVH traversal changed the original result");
 bool low_ram=false;{TextureRamQueryScope query(+[]()->std::uint64_t{return 1024;});try{gpu_bake_cosine_occlusion(triangles,points,8,4);}catch(const std::length_error &){low_ram=true;}}require(low_ram,"GPU bake skipped actual RAM admission");
 return {{"triangles",int(triangles.size())},{"points",int(points.size())},{"rays",8},{"former_triangle_ray_tests",qint64(old_work)},{"legacy_reject_reproduced",old_rejected},{"original_gpu_reference_bit_exact_samples",exact},{"cpu_max_error",maximum_error},{"gpu_ms",gpu_ms},{"batches",qint64(stats.batches)},{"peak_batch_rays",qint64(stats.peak_batch_rays)},{"overlap_continuation_dispatches",qint64(resumed.dispatches)},{"continuation_bit_exact",true},{"real_low_ram_rejected",low_ram},{"no_global_work_cap",true}};
}
inline QJsonObject verify_resource_manifest_bounds(){
 using namespace blender;const auto require=[](bool value,const char *message){if(!value)throw std::runtime_error(message);};
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);auto *scene=BKE_scene_add(main.get(),"BC manifest fixture");
 std::vector<StudioAsset> assets;assets.reserve(4097);
 for(int i=0;i<4097;++i){StudioAsset asset;asset.scope=AssetScope::Project;asset.name="Manifest resource "+QString::number(i);asset.filename="source.bin";asset.usages={"texture"};asset.bytes="x";assets.push_back(std::move(asset));}
 pack_project_assets(*main,*scene,assets);auto loaded=read_project_assets(*scene);require(loaded.size()==4097,"Project resources were limited to4096");loaded.clear();
 auto *root=IDP_EnsureProperties(&scene->id);
 const auto *before=IDP_GetPropertyFromGroup(root,"bts_resource_images");bool write_low_ram=false;
 {TextureRamQueryScope query(+[]()->std::uint64_t{return 1024;});try{pack_project_assets(*main,*scene,assets);}catch(const std::length_error &){write_low_ram=true;}}
 require(write_low_ram&&before==IDP_GetPropertyFromGroup(root,"bts_resource_images"),"Manifest write admission changed the existing resource references");
 std::unique_ptr<Main,decltype(&BKE_main_free)> empty(BKE_main_new(),BKE_main_free);auto *empty_scene=BKE_scene_add(empty.get(),"BC rejected initial manifest");bool initial_low_ram=false;
 {TextureRamQueryScope query(+[]()->std::uint64_t{return 1024;});try{pack_project_assets(*empty,*empty_scene,{assets[0],assets[1],assets[2],assets[3]});}catch(const std::length_error &){initial_low_ram=true;}}
 require(initial_low_ram&&empty->images.is_empty(),"Rejected resource manifest left new packed images allocated");
 QByteArray json(R"({"version":2,"resources":[]})");json.append(QByteArray(64*1024*1024,' '));
 IDP_ReplaceInGroup(root,IDP_NewString(json.constData(),"bts_resources"));require(read_project_assets(*scene).empty(),"Manifest larger than64MiB was rejected");
 bool low_ram=false;{TextureRamQueryScope query(+[]()->std::uint64_t{return 1024;});try{read_project_assets(*scene);}catch(const std::length_error &){low_ram=true;}}require(low_ram,"Manifest decode skipped actual RAM admission");
 return {{"resources_roundtripped",4097},{"encoded_manifest_bytes",qint64(json.size())},{"real_low_ram_rejected",low_ram},{"manifest_write_low_ram_rejected",write_low_ram},{"references_retained_on_rejection",true},{"new_payloads_released_on_rejection",true},{"idproperty_signed_int_length_preserved",true}};
}
inline int run_gpu_bake_ray_demo(BlenderHost &host,const QString &directory){
 if(qEnvironmentVariableIsSet("BTS_BL_BAKE_SAMPLES_TEST"))return run_bl_bake_samples_validation(host,directory);
 QJsonObject ray;host.with_gpu([&]{ray=verify_gpu_bake_bvh();});const auto manifest=verify_resource_manifest_bounds();
 QJsonObject proof{{"renderer",QString::fromStdString(host.renderer())},{"visible_windows",0},{"gpu_bake",ray},{"resource_manifest",manifest}};
 QFile file(directory+"/bc-gpu-bake-bvh.json");if(!file.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot write GPU bake BVH proof");file.write(QJsonDocument(proof).toJson());
 std::printf("BTS_GPU_BAKE_BVH_PASS work=13113600 original_reference_bit_exact=62 resources=4097 renderer=%s\n",host.renderer().c_str());return 0;
}
}
