#pragma once
#include <QString>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>
namespace blender {struct Image;struct bNodeTree;}
namespace bts {
class NativeMaterialImageTileSet;
struct NativeMaterialGraph;
enum class NativeTiledRepresentation {ArrayMapping,FirstTile2D};
enum class NativeTiledFormat {Float16Rgba,Float32Rgba};
struct NativeImageUploadRequest {blender::Image *image=nullptr;NativeTiledRepresentation representation=NativeTiledRepresentation::FirstTile2D;bool tiled=false;};
std::vector<NativeImageUploadRequest> native_material_image_upload_requests(const NativeMaterialGraph &,std::span<blender::bNodeTree *const> extra_roots={},bool ordinary_channels=true);
template<class Validate>void native_tiled_requests_validate(std::span<const NativeImageUploadRequest> requests,const Validate &validate){for(const auto &request:requests)if(request.tiled)validate(request);}
struct NativeTiledCaps {int texture_size=0,array_layers=0,map_width=0;};
struct NativeTiledRegion {int tile=0,x=0,y=0,width=0,height=0,layer=0;};
// Metadata only: no Image/Main, pixels or GPU ownership. Original Image order
// is authoritative; the planner never sorts or mutates it.
struct NativeTiledStoragePlan {
 NativeTiledRepresentation representation=NativeTiledRepresentation::ArrayMapping;
 NativeTiledFormat format=NativeTiledFormat::Float16Rgba;
 int width=0,height=0,layers=0,map_width=0,first_tile=0;
 std::vector<int> source_order;
 std::vector<NativeTiledRegion> regions;
 // Stock mapping layout: width float4 layer records, then width float4 UV rects.
 std::vector<std::array<float,4>> mapping;
 QString key,source_key,image_space;int image_alpha=0;bool high_precision=false;
 std::uint64_t texture_bytes=0,mapping_bytes=0;
};
struct NativeTiledStorageShape {
 int width=0,height=0,layers=0,dimensions=0;bool array=false;
 NativeTiledFormat format=NativeTiledFormat::Float16Rgba;bool supported_format=false;
 int map_width=0,map_layers=0,map_dimensions=0;bool map_array=false,map_float32_rgba=false;
};
inline bool native_tiled_storage_shape_valid(const NativeTiledStoragePlan &plan,const NativeTiledStorageShape &shape){
 const bool array=plan.representation==NativeTiledRepresentation::ArrayMapping;
 return shape.width==plan.width&&shape.height==plan.height&&shape.dimensions==2&&shape.array==array&&(!array||shape.layers==plan.layers)&&shape.supported_format&&shape.format==plan.format&&(!array||(shape.map_width==plan.map_width&&shape.map_layers==2&&shape.map_dimensions==1&&shape.map_array&&shape.map_float32_rgba));
}
NativeTiledStoragePlan native_tiled_storage_plan(const blender::Image &,
 const NativeMaterialImageTileSet &,NativeTiledRepresentation,NativeTiledCaps);
std::uint64_t native_tiled_mip_texels(int width,int height);
bool native_tiled_storage_source_valid(const blender::Image &,const NativeMaterialImageTileSet &,const NativeTiledStoragePlan &);
inline std::size_t native_image_sampler_slots(bool tiled_mapping){return tiled_mapping?2:1;}
inline bool native_tiled_default_user_tile(int tile){return tile==0;}
// One callback action per step. Pixel packets are source-strided RGBA float4;
// their conservative transfer bound is width*height*16, including X splitting.
// A whole mip generation is a separate GPU action, not a wall-time guarantee.
enum class NativeTiledPhase {Allocate,Pixels,Mips,AllocateMapping,Mapping,Publish,Done,Failed};
struct NativeTiledUploadAction {
 NativeTiledPhase phase=NativeTiledPhase::Allocate;
 int tile=0,source_x=0,source_y=0,width=0,height=0,target_x=0,target_y=0,layer=0;
 std::uint64_t transfer_bytes=0;
};
struct NativeTiledPacketView {const float *pixels=nullptr;int source_row_length=0,width=0,height=0;};
NativeTiledPacketView native_tiled_packet_view(const NativeMaterialImageTileSet &,const NativeTiledUploadAction &);
template<class Owner>class NativeTiledUploadState {
 NativeTiledStoragePlan plan_;
 Owner pending_{};
 NativeTiledPhase phase_=NativeTiledPhase::Allocate;
 std::size_t region_=0;
 int x_=0,y_=0,map_x_=0,map_row_=0;
 std::uint64_t transferred_=0;
 public:
 explicit NativeTiledUploadState(NativeTiledStoragePlan plan):plan_(std::move(plan)){if(plan_.regions.empty()||plan_.width<=0||plan_.height<=0||plan_.layers<=0||(plan_.representation==NativeTiledRepresentation::ArrayMapping&&(plan_.map_width<=0||plan_.mapping.size()!=std::size_t(plan_.map_width)*2)))throw std::invalid_argument("Native tiled upload plan is incomplete");for(const auto &r:plan_.regions)if(r.width<=0||r.height<=0||r.x<0||r.y<0||r.width>plan_.width||r.height>plan_.height||r.x>plan_.width-r.width||r.y>plan_.height-r.height||r.layer<0||r.layer>=plan_.layers)throw std::invalid_argument("Native tiled upload region is outside its storage");}
 const NativeTiledStoragePlan &plan()const{return plan_;}
 NativeTiledPhase phase()const{return phase_;}
 const Owner &pending()const{return pending_;}
 std::uint64_t transferred_bytes()const{return transferred_;}
 void cancel(){pending_=Owner{};phase_=NativeTiledPhase::Failed;}
 template<class Perform>bool step(std::size_t packet_bytes,const Perform &perform){
  if(packet_bytes<16)throw std::invalid_argument("Native tiled packet budget must hold one RGBA float pixel");
  if(phase_==NativeTiledPhase::Done)return true;
  if(phase_==NativeTiledPhase::Failed)throw std::logic_error("Native tiled upload transaction is not active");
  NativeTiledUploadAction action;action.phase=phase_;
  if(phase_==NativeTiledPhase::Pixels){
   const auto &region=plan_.regions.at(region_);action.tile=region.tile;action.source_x=x_;action.source_y=y_;action.target_x=region.x+x_;action.target_y=region.y+y_;action.layer=region.layer;
   const auto pixels=packet_bytes/16;action.width=int(std::min<std::size_t>(region.width-x_,pixels));
   action.height=x_==0&&action.width==region.width?int(std::min<std::size_t>(region.height-y_,pixels/std::size_t(action.width))):1;
   action.transfer_bytes=std::uint64_t(action.width)*action.height*16;
  }else if(phase_==NativeTiledPhase::Mapping){
   action.source_x=map_x_;action.source_y=map_row_;action.target_x=map_x_;action.target_y=map_row_;action.width=int(std::min<std::size_t>(plan_.map_width-map_x_,packet_bytes/16));action.height=1;action.transfer_bytes=std::uint64_t(action.width)*16;
  }
  try{perform(action,pending_);if(phase_!=NativeTiledPhase::Publish&&!pending_)throw std::logic_error("Native tiled upload action has no private owner");}
  catch(...){cancel();throw;}
  transferred_+=action.transfer_bytes;
  switch(phase_){
   case NativeTiledPhase::Allocate:phase_=NativeTiledPhase::Pixels;break;
   case NativeTiledPhase::Pixels:{const auto &r=plan_.regions.at(region_);x_+=action.width;if(x_==r.width){x_=0;y_+=action.height;}if(y_==r.height){y_=0;if(++region_==plan_.regions.size())phase_=NativeTiledPhase::Mips;}break;}
   case NativeTiledPhase::Mips:phase_=plan_.representation==NativeTiledRepresentation::ArrayMapping?NativeTiledPhase::AllocateMapping:NativeTiledPhase::Publish;break;
   case NativeTiledPhase::AllocateMapping:phase_=NativeTiledPhase::Mapping;break;
   case NativeTiledPhase::Mapping:map_x_+=action.width;if(map_x_==plan_.map_width){map_x_=0;if(++map_row_==2)phase_=NativeTiledPhase::Publish;}break;
   case NativeTiledPhase::Publish:pending_=Owner{};phase_=NativeTiledPhase::Done;break;
   default:break;
  }
  return phase_==NativeTiledPhase::Done;
 }
};
// Same complete-layout lookup used by GPU owners and CPU metadata/token tests.
// Validation also prunes expired/lost handles. It never acquires a partial pair.
template<class Owner>class NativeTiledOwnerCache {
 std::map<QString,std::vector<Owner>> entries_;
 public:
 template<class Valid>const Owner *find(const QString &key,const Valid &valid){
  const auto found=entries_.find(key);if(found==entries_.end())return nullptr;
  auto &owners=found->second;std::erase_if(owners,[&](const auto &owner){return !valid(owner);});if(owners.empty()){entries_.erase(found);return nullptr;}return &owners.front();
 }
 template<class Same>void remember(const QString &key,Owner owner,const Same &same){auto &owners=entries_[key];for(auto &old:owners)if(same(old,owner)){old=std::move(owner);return;}owners.push_back(std::move(owner));}
 template<class Alive>void prune(const Alive &alive){for(auto entry=entries_.begin();entry!=entries_.end();){std::erase_if(entry->second,[&](const auto &owner){return !alive(owner);});if(entry->second.empty())entry=entries_.erase(entry);else ++entry;}}
 void clear(){entries_.clear();}
};
}
