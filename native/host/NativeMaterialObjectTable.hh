#pragma once
#include "ResourceBudget.hh"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace bts {
struct NativeMaterialObject {
  std::uint32_t owner=0;
  std::array<float,16> object_to_world{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  std::array<float,16> world_to_object=object_to_world;
  std::array<float,3> bounds_min{},bounds_max{1,1,1};
  std::array<float,4> color{1,1,1,1};float random=0;
};
// std430 float4 rows. Owner keys are bit-cast, never converted to float: Blender
// session IDs must remain exact above 2^24 and across the complete uint32 range.
class NativeMaterialObjectTable {
 public:
  static constexpr int stride=12;
  using Row=std::array<float,4>;
  explicit NativeMaterialObjectTable(std::vector<NativeMaterialObject> objects){
    const auto rows=resource_product(objects.size(),stride);
    if(rows>std::uint64_t(std::numeric_limits<int>::max()))throw std::length_error("Native object table exceeds shader signed-index storage");
    require_texture_ram(resource_sum(resource_product(objects.size(),sizeof(NativeMaterialObject)),resource_product(rows,sizeof(Row))),"Native object context table");rows_.reserve(std::size_t(rows));
    std::sort(objects.begin(),objects.end(),[](const auto &a,const auto &b){return a.owner<b.owner;});
    std::uint32_t previous=0;
    const auto finite=[](const auto &values){for(float value:values)if(!std::isfinite(value))return false;return true;};
    for(const auto &object:objects){
      if(!object.owner||object.owner==previous||!finite(object.object_to_world)||!finite(object.world_to_object)||!finite(object.bounds_min)||!finite(object.bounds_max)||!finite(object.color)||!std::isfinite(object.random))throw std::invalid_argument("Invalid native material object table");
      previous=object.owner;rows_.push_back({std::bit_cast<float>(object.owner),object.random,0,0});
      for(const auto &matrix:{object.object_to_world,object.world_to_object})for(int c=0;c<4;++c)rows_.push_back({matrix[c*4],matrix[c*4+1],matrix[c*4+2],matrix[c*4+3]});
      rows_.push_back({object.bounds_min[0],object.bounds_min[1],object.bounds_min[2],0});rows_.push_back({object.bounds_max[0],object.bounds_max[1],object.bounds_max[2],0});rows_.push_back(object.color);
    }
    hash_=1469598103934665603ULL;for(const auto &row:rows_)for(float value:row){hash_^=std::bit_cast<std::uint32_t>(value);hash_*=1099511628211ULL;}
  }
  int size()const{return int(rows_.size()/stride);}
  const std::vector<Row> &rows()const{return rows_;}
  std::uint64_t hash()const{return hash_;}
 private:
  std::vector<Row> rows_;std::uint64_t hash_;
};
}
