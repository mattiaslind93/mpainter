#include "ExportPadding.hh"
#include "MeshBakeFilter.hh"
#include <stdexcept>
#include <algorithm>
namespace bts {
std::vector<float> pad_texture_export(std::span<const float> rgba,std::span<const unsigned char> coverage,int source,int output,ExportPadding mode,int width,const std::array<float,3> &background,int normal_role,int source_height,int output_height){
  if(!source_height)source_height=source;if(!output_height)output_height=output;
  const auto count=std::size_t(source)*source_height;if(rgba.size()!=count*4||coverage.size()!=count||mode==ExportPadding::None||int(mode)<0||int(mode)>4||width<0||width>256)throw std::invalid_argument("Invalid export padding source");
  std::vector<float> associated(rgba.begin(),rgba.end());for(std::size_t i=0;i<count;++i){const float alpha=coverage[i]?1:0;for(int c=0;c<3;++c)associated[i*4+c]*=alpha;associated[i*4+3]=alpha;}
  auto result=filter_mesh_bake(std::move(associated),source,source_height,output,output_height,mode==ExportPadding::Infinite?std::max(output,output_height):width,mode==ExportPadding::Diffusion,normal_role);
  for(std::size_t i=0;i<result.size();i+=4){const float alpha=result[i+3];if(alpha>1e-12f){for(int c=0;c<3;++c)result[i+c]/=alpha;}else for(int c=0;c<3;++c)result[i+c]=mode==ExportPadding::Transparent?0:background[c];if(mode!=ExportPadding::Transparent)result[i+3]=1;}
  return result;
}
}
