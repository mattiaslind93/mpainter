#pragma once
#include "SkewCorrection.hh"
#include "TilePaintPixels.hh"
namespace bts {
struct SkewSettings {
  bool enabled=true,edge_protection=true,vectors=true;
  double edge_distance=.02,edge_contrast=0,vector_length=.05,vector_opacity=.8;
  int vector_density=16;
  void validate() const {
    if(!std::isfinite(edge_distance)||edge_distance<0||edge_distance>1||!std::isfinite(edge_contrast)||std::abs(edge_contrast)>1||!std::isfinite(vector_length)||vector_length<.00001||vector_length>10||!std::isfinite(vector_opacity)||vector_opacity<0||vector_opacity>1||vector_density<1||vector_density>128)throw std::invalid_argument("Invalid skew correction settings");
  }
  bool operator==(const SkewSettings &) const=default;
};
// Separate sparse authoring channel; absent pixels are red/uncorrected (zero). This field
// never aliases a layer mask. Copies share COW pages with ordinary document Undo snapshots.
struct SkewField {
  struct TileDescriptor {std::size_t pages=0;std::uint64_t hash=0;};
  TilePaintPixels pixels;
  std::map<int,TileDescriptor> pending_tiles;
  bool loaded=true;
  SkewSettings settings;
  int resolution=256;
  float sample(int tile,double u,double v) const {
    if(!loaded)throw std::logic_error("Skew field authoring data is not loaded");
    if(!settings.enabled||tile<1001||tile>2000||!std::isfinite(u)||!std::isfinite(v)||resolution<1)return 0;
    const double x=std::clamp(u,0.,1.)*resolution-.5,y=std::clamp(v,0.,1.)*resolution-.5;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
    const auto at=[&](int a,int b){return pixels.sample(2,std::clamp(a,0,resolution-1),std::clamp(b,0,resolution-1),tile)[0];};
    return std::clamp(float(std::lerp(std::lerp(double(at(ix,iy)),double(at(ix+1,iy)),fx),std::lerp(double(at(ix,iy+1)),double(at(ix+1,iy+1)),fx),fy)),0.f,1.f);
  }
  float effective(int tile,double u,double v,double edge_distance,double mesh_extent) const {
    const float weight=sample(tile,u,v);return settings.edge_protection?skew_edge_weight(weight,edge_distance,settings.edge_distance*mesh_extent,settings.edge_contrast):weight;
  }
};
}
