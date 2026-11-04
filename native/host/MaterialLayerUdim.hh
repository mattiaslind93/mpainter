#pragma once
#include "MaterialLayering.hh"
#include "TextureResource.hh"
#include <limits>
namespace bts {
// New material authoring requires the per-file carrier representation. The
// shared decoder still accepts BTSUDIM1 bundles for ordinary Fill layers.
inline void require_material_layer_udim_originals(const FillTexture &source,const std::vector<TextureSourceFile> &files){
 if(!source.tiles)return;
 if(files.empty())throw std::invalid_argument("UDIM material inputs require original per-tile files; legacy bundled Shelf images remain available to ordinary Fill layers");
 validate_texture_sources(files);
 if(files.size()!=source.tiles->size())throw std::invalid_argument("UDIM original file count differs from its decoded members");
 for(const auto &file:files){const auto found=source.tiles->find(file.tile);if(found==source.tiles->end()||found->second->hash!=file.sha256.toStdString())throw std::invalid_argument("UDIM original tile/checksum differs from its decoded member");}
}
// Full destination UV tile rectangles, not a scan of mesh pixels. Rotation uses
// their bounding boxes and can conservatively reject holes that the mesh never
// samples. Reflected upper endpoints are included because they are attained at
// an included lower destination edge. Positive upper extents are exclusive: [0,2) needs 1001/1002, not 1003.
inline std::set<int> material_layer_udim_footprint(const LayerMaterialInput &input,const std::set<int> &destination){
 std::set<int> result;
 if(destination.empty())throw std::invalid_argument("UDIM source coverage requires destination UV tiles");
 for(int number:destination){
  const double u=(number-1001)%10,v=(number-1001)/10;
  std::array<double,2> low{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::infinity()},high{-low[0],-low[1]};
  for(int corner=0;corner<4;++corner){const auto p=layered_material_uv(input,{u+(corner&1),v+((corner>>1)&1)});for(int c=0;c<2;++c){low[c]=std::min(low[c],p[c]);high[c]=std::max(high[c],p[c]);}}
  // Exact zero-rotation transforms use the same float socket values as the
  // native graph. For rotation, a conservative float arithmetic envelope keeps
  // barely crossed tile boundaries from being silently omitted by CPU math.
  if(input.rotation!=0){for(int c=0;c<2;++c){const double error=32*std::numeric_limits<float>::epsilon()*std::max({1.,std::abs(low[c]),std::abs(high[c])});low[c]-=error;high[c]+=error;}}
  if(!std::isfinite(low[0])||!std::isfinite(low[1])||low[0]<0||low[1]<0||high[0]>10||high[1]>900)throw std::invalid_argument("Mapped UDIM footprint leaves the supported positive UDIM domain");
  const int x0=int(std::floor(low[0])),y0=int(std::floor(low[1]));
  const bool reflected_x=input.rotation==0&&input.tiling[0]<0,reflected_y=input.rotation==0&&input.tiling[1]<0;
  const int x1=reflected_x?int(std::floor(high[0])):int(std::ceil(high[0]))-1,y1=reflected_y?int(std::floor(high[1])):int(std::ceil(high[1]))-1;
  if(x1>=10||y1>=900)throw std::invalid_argument("Mapped UDIM endpoint leaves the supported source domain");
  for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const int tile=1001+x+10*y;if(tile>9999)throw std::invalid_argument("Mapped UDIM footprint exceeds the source tile domain");result.insert(tile);}
 }
 return result;
}
inline void require_material_layer_udim_coverage(const LayerMaterialInput &input,const std::set<int> &destination){
 bool sequence=false;for(const auto &source:input.textures)sequence|=source&&bool(source->tiles);if(!sequence)return;
 auto mapping=input;for(int c=0;c<2;++c){mapping.tiling[c]=float(input.tiling[c]);mapping.offset[c]=float(input.offset[c]);}
 const auto needed=material_layer_udim_footprint(mapping,destination);
 for(int channel:{0,1,2,4,5})if(const auto &source=input.textures[channel];source&&source->tiles){
  if(source->raw!=(channel!=0))throw std::invalid_argument("UDIM source interpretation differs from its material role");
  for(int tile:needed)if(!source->tiles->contains(tile))throw std::invalid_argument("Mapped material source tile "+std::to_string(tile)+" is missing; reduce SRT or provide the complete source footprint");
 }
}
}
