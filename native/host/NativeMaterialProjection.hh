#pragma once
#include "GpuLayerParameters.hh"
#include <algorithm>
#include <cmath>
#include <span>
#include <tuple>

namespace bts {
// This is a Fill coordinate adapter, separate from the immutable source graph
// and the real surface/view/object context. It never contains source pixels.
enum class NativeMaterialProjectionMode : int {
  UV=0, Triplanar=1, Planar=2, Spherical=3, Cylindrical=4, Warp=5, MatchTile=6
};
struct NativeMaterialProjection {
  using Row=std::array<float,4>;
  static constexpr std::size_t header_rows=1;
  static constexpr std::size_t base_rows=header_rows+std::size(GpuLayerParameters::fields);
  static constexpr std::size_t row_capacity=header_rows+std::tuple_size_v<decltype(GpuLayerParameters::values)>;
  using Rows=std::array<Row,row_capacity>;

  NativeMaterialProjectionMode mode=NativeMaterialProjectionMode::UV;
  bool identity_uv=true;
  GpuLayerParameters parameters;

  std::size_t row_count()const {
    return mode==NativeMaterialProjectionMode::Warp?row_capacity:base_rows;
  }
  const Row &parameter(std::string_view name)const {
    for(std::size_t i=0;i<std::size(GpuLayerParameters::fields);++i)
      if(name==GpuLayerParameters::fields[i].name)return parameters.values[i];
    throw std::logic_error("Unknown native Fill projection parameter");
  }
  void validate()const {
    const auto index=int(mode);
    if(index<0||index>6)throw std::invalid_argument("Invalid native Fill projection mode");
    for(std::size_t i=0;i<row_count()-header_rows;++i)
      for(float value:parameters.values[i])
        if(!std::isfinite(value))throw std::invalid_argument("Non-finite native Fill projection parameters");
    const auto integer=[](float value,int lo,int hi){return value>=lo&&value<=hi&&std::floor(value)==value;};
    if(parameter("projection_mode")[0]!=index||!integer(parameter("projection_wrap")[0],0,3)||!integer(parameter("source_filter")[0],0,2))
      throw std::invalid_argument("Inconsistent native Fill projection options");
    if(identity_uv&&(index!=0||parameter("projection_wrap")[0]!=0||parameter("map_u")!=Row{1,0,0,0}||parameter("map_v")!=Row{0,1,0,0}))
      throw std::invalid_argument("Inconsistent native Fill identity mapping");
    if(index>=1&&index<=5)for(int c=0;c<3;++c)if(parameter("inverse_denominator")[c]==0)
      throw std::invalid_argument("Singular native Fill projection frame");
    if(index==5&&(!integer(parameter("warp_info")[0],2,9)||!integer(parameter("warp_info")[1],2,9)||parameter("warp_info")[2]<=0))
      throw std::invalid_argument("Invalid native Fill Warp grid");
  }
  Rows encoded()const {
    validate();Rows result{};
    result[0]={1,float(int(mode)),identity_uv?1.f:0.f,0};
    std::copy_n(parameters.values.begin(),row_count()-header_rows,result.begin()+header_rows);
    return result;
  }
};
}
