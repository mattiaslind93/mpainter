#pragma once
#include "FillTexture.hh"
#include "MaterialValues.hh"
#include <QUuid>
#include <array>
#include <set>
#include <string>
namespace bts {
// Reusable material inputs remain in their own repeating texture space. Only
// the three authored opacity stacks use mesh UVs; no flattened color bake is
// required by this shader-time composition model.
struct LayerMaterialInput {
 std::string name="Uniform material",resource;
 MaterialValues values;
 std::array<std::shared_ptr<const FillTexture>,6> textures; // Color,Metal,Rough,Normal,Bump,AO.
 std::array<double,2> tiling{1,1},offset{};double rotation=0;
 void validate()const{
  if(name.empty())throw std::invalid_argument("Invalid material input identity");
  require_texture_ram(resource_product(resource_sum(name.size(),resource.size()),8),"Material input label and resource text");
  for(int c=0;c<4;++c)if(const auto v=values.base_color[c];!std::isfinite(v)||v<(c==3?0:-65504)||v>(c==3?1:65504))throw std::invalid_argument("Invalid material color");
  for(float v:{values.metallic,values.roughness,values.ambient_occlusion})if(!std::isfinite(v)||v<0||v>1)throw std::invalid_argument("Material scalar must lie in 0..1");
  for(float v:values.normal)if(!std::isfinite(v)||v< -1||v>1)throw std::invalid_argument("Invalid material normal");
  if(!std::isfinite(values.bump)||std::abs(values.bump)>65504||!std::isfinite(rotation)||std::abs(rotation)>360000)throw std::invalid_argument("Invalid material input value");
  for(int c=0;c<2;++c)if(!std::isfinite(tiling[c])||std::abs(tiling[c])<1e-6||std::abs(tiling[c])>1000||!std::isfinite(offset[c])||std::abs(offset[c])>1000)throw std::invalid_argument("Invalid material UV transform");
  for(int c=0;c<6;++c)if(textures[c]){textures[c]->validate();if(textures[c]->raw!=(c!=0)||(textures[c]->tiles&&c==3))throw std::invalid_argument("Normal sequences are unavailable; Color sequences require native linear-processor admission");}
 }
};
struct MaterialLayering {
 bool enabled=false;
 std::array<LayerMaterialInput,4> materials;
 std::array<int,3> mask_slots{-1,-1,-1};
 std::array<QUuid,3> mask_roots;
 int active_mask=-1; // -1 is the ordinary project layer stack.
 QUuid main_selection;
 std::array<QUuid,3> mask_selections;
 bool allocated()const{return !mask_roots[0].isNull();}
 void validate()const{
  for(const auto &material:materials)material.validate();if(active_mask< -1||active_mask>2||(!enabled&&active_mask!=-1))throw std::invalid_argument("Invalid material substack selection");
  const int root_count=int(std::count_if(mask_roots.begin(),mask_roots.end(),[](const auto &id){return !id.isNull();}));if(root_count!=0&&root_count!=3)throw std::invalid_argument("Incomplete shader-defined opacity stacks");std::set<QUuid> roots;std::set<int> channel_slots;
  for(int i=0;i<3;++i){if(mask_roots[i].isNull()){if(allocated()||mask_slots[i]!=-1)throw std::invalid_argument("Incomplete shader-defined opacity stacks");}else if(mask_slots[i]<0||mask_slots[i]>=8||!roots.insert(mask_roots[i]).second||!channel_slots.insert(mask_slots[i]).second)throw std::invalid_argument("Invalid shader-defined opacity stack identity");}
  if(enabled&&!allocated())throw std::invalid_argument("Material layering requires three opacity stacks");
 }
};
inline std::array<float,4> layered_material_weights(std::array<float,3> mask){for(auto &v:mask){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite material mask");v=std::clamp(v,0.f,1.f);}return {(1-mask[0])*(1-mask[1])*(1-mask[2]),mask[0]*(1-mask[1])*(1-mask[2]),mask[1]*(1-mask[2]),mask[2]};}
inline std::array<double,2> layered_material_uv(const LayerMaterialInput &material,std::array<double,2> uv){const double radians=material.rotation*3.14159265358979323846/180,c=std::cos(radians),s=std::sin(radians),x=uv[0]*material.tiling[0],y=uv[1]*material.tiling[1];return {c*x-s*y+material.offset[0],s*x+c*y+material.offset[1]};}
}
