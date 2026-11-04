#pragma once
// Pending native GPU integration: include from an explicit diagnostic owner only.
#include "GpuBakeRays.hh"
#include "gpu_bake_ray_reference.hh"
#include "BakeSamplesFixture.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <bit>
namespace bts {
inline QJsonObject verify_bl_gpu_bake_samples(){
 const auto require=[](bool ok,const char *message){if(!ok)throw std::runtime_error(message);};
 const auto triangles=bake_samples_fixture::triangles();std::vector<FillSurfacePoint> points(4);
 for(std::size_t i=0;i<points.size();++i){points[i].valid=true;points[i].position=bake_samples_fixture::positions[i];points[i].normal={0,0,1};}
 QJsonArray rows;std::uint64_t compared=0;
 for(bool inward:{false,true}){
  std::vector<float> actual256,actual512;
  for(int rays:{16,17,128,256,511,512}){
   const auto maximum=bake_samples_fixture::maximum(inward);GpuBakeRayStats stats;
   const auto actual=gpu_bake_cosine_occlusion(triangles,points,rays,maximum,inward,&stats);
   const auto old=legacy_gpu_bake_cosine_occlusion(triangles,points,rays,maximum,inward);
   const auto expected=bake_samples_fixture::reference(rays,inward);
   require(actual.size()==points.size()&&old.size()==points.size(),"BL bake changed output extent");
   require(stats.batches==1&&stats.dispatches>0&&stats.peak_batch_rays==points.size()*std::uint64_t(rays),"BL actual GPU ray count differs from selected count");
   QJsonArray values,wanted;double error=0;
   for(std::size_t i=0;i<actual.size();++i){require(std::isfinite(actual[i])&&actual[i]>0&&actual[i]<1,"BL fixture must be genuinely partially occluded");require(std::bit_cast<std::uint32_t>(actual[i])==std::bit_cast<std::uint32_t>(old[i]),"BL BVH differs from original independent GPU reference");error=std::max(error,std::abs(double(actual[i])-expected[i]));values.append(actual[i]);wanted.append(expected[i]);++compared;}
   require(error<2e-6,"BL GPU ray output differs from independent CPU reference"); // Existing GPU bake diagnostic bound.
   if(rays==256)actual256=actual;if(rays==512)actual512=actual;
   rows.append(QJsonObject{{"inward",inward},{"rays",rays},{"values",values},{"expected",wanted},{"maximum_cpu_error",error},{"batches",qint64(stats.batches)},{"dispatches",qint64(stats.dispatches)},{"peak_batch_rays",qint64(stats.peak_batch_rays)},{"legacy_gpu_bit_exact",true}});
  }
  double changed=0;for(std::size_t i=0;i<points.size();++i)changed=std::max(changed,std::abs(double(actual512[i])-actual256[i]));require(changed>2e-6,"BL512 was silently replaced by256");
 }
 for(int rays:{0,513}){
  bool current=false,old=false;try{gpu_bake_cosine_occlusion(triangles,points,rays,2);}catch(const std::invalid_argument &){current=true;}
  try{legacy_gpu_bake_cosine_occlusion(triangles,points,rays,2);}catch(const std::invalid_argument &){old=true;}
  require(current&&old,"BL invalid GPU ray count admitted");
 }
 return {{"cases",rows},{"compared_points",qint64(compared)},{"triangles",int(triangles.size())},{"points",int(points.size())},{"bound",2e-6},{"actual_GPU",true},{"scope","Explicit bounded ray-kernel diagnostic only; UI/job consumption, Save/Open and dialog transactions are separate gates"}};
}
}
