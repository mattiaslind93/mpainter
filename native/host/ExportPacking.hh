#pragma once
#include "ExportFormat.hh"
#include "MaterialValues.hh"
#include <string>
#include <stdexcept>
namespace bts {
enum class PackedSource {MixedAo,Roughness,Metallic,AuthoredAo,Bump,Zero,One,Displacement};
inline constexpr std::array<const char *,8> packed_source_names{"Mixed AO","Roughness","Metallic","Authored AO","Bump","Unassigned (0)","Constant 1","Displacement"};
struct PackedOutput {
  std::string name="$textureSet_orm";
  std::array<PackedSource,3> source{PackedSource::MixedAo,PackedSource::Roughness,PackedSource::Metallic};
  std::optional<ExportFormat> format;
};
inline float packed_scalar(PackedSource source,const MaterialValues &value,float mixed_ao,float displacement=.5f){
  switch(source){case PackedSource::MixedAo:return mixed_ao;case PackedSource::Roughness:return value.roughness;case PackedSource::Metallic:return value.metallic;case PackedSource::AuthoredAo:return value.ambient_occlusion;case PackedSource::Bump:return value.bump;case PackedSource::Zero:return 0;case PackedSource::One:return 1;case PackedSource::Displacement:return displacement;}
  throw std::invalid_argument("Invalid packed source");
}
inline std::array<float,3> packed_rgb(const PackedOutput &output,const MaterialValues &value,float mixed_ao,float displacement=.5f){
  return {packed_scalar(output.source[0],value,mixed_ao,displacement),packed_scalar(output.source[1],value,mixed_ao,displacement),packed_scalar(output.source[2],value,mixed_ao,displacement)};
}
}
