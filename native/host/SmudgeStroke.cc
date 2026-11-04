#include "TextureExtent.hh"
#include "SmudgeStroke.hh"
#include "ResourceAvailability.hh"
#include "SmudgeCapture.hh"
#include "PaintRecording.hh"
#include <cmath>
#include <limits>
namespace bts {
namespace {
using V2=std::array<double,2>;using V3=std::array<double,3>;
struct Contact{V2 uv;V3 world{};float fade=1;};
void include(PaintPixels::Bounds &a,PaintPixels::Bounds b){if(b.empty())return;if(a.empty())a=b;else{a.x0=std::min(a.x0,b.x0);a.y0=std::min(a.y0,b.y0);a.x1=std::max(a.x1,b.x1);a.y1=std::max(a.y1,b.y1);}}
PaintPixels::Pixel sample(const TilePaintPixels &raster,int channel,int resolution,V2 uv){
  const double x=uv[0]*resolution-.5,y=uv[1]*resolution-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));
  const auto at=[&](int x,int y){const int u=int(std::floor(double(x)/resolution)),v=int(std::floor(double(y)/resolution));if(u<0||u>=10||v<0||v>=100)return PaintPixels::Pixel{};return raster.sample(channel,x-u*resolution,y-v*resolution,1001+u+v*10);};
  PaintPixels::Pixel result;for(int c=0;c<4;++c)result[c]=std::lerp(std::lerp(at(ix,iy)[c],at(ix+1,iy)[c],float(x-ix)),std::lerp(at(ix,iy+1)[c],at(ix+1,iy+1)[c],float(x-ix)),float(y-iy));return result;
}
std::optional<Contact> contact(const PaintPixels::Stamp &stamp,double u,double v){
  if(!stamp.surface)return Contact{{stamp.center[0]+u*stamp.axis_x[0]+v*stamp.axis_y[0],stamp.center[1]+u*stamp.axis_x[1]+v*stamp.axis_y[1]}};
  const auto &surface=*stamp.surface;std::optional<Contact> best;double nearest=std::numeric_limits<double>::infinity();
  for(const auto &triangle:surface.triangles){
    if(surface.cull&&!accepts_brush_normal(triangle.normal,surface.view_direction,surface.cull_angle))continue;
    std::array<V2,3> coordinates=triangle.paint_coordinates;
    if(!surface.unwrapped)for(int i=0;i<3;++i){coordinates[i]={};for(int c=0;c<3;++c){const double delta=triangle.position[i][c]-surface.center[c];coordinates[i][0]+=delta*surface.tangent[c];coordinates[i][1]+=delta*surface.bitangent[c];}}
    const auto a=coordinates[0],b=coordinates[1],c=coordinates[2];const double x=b[0]-a[0],y=b[1]-a[1],dx=c[0]-a[0],dy=c[1]-a[1],det=x*dy-y*dx;if(std::abs(det)<1e-20)continue;
    const double px=u*surface.radius-a[0],py=v*surface.radius-a[1],w1=(px*dy-py*dx)/det,w2=(x*py-y*px)/det,w0=1-w1-w2;if(std::min({w0,w1,w2}) < -1e-8)continue;
    Contact hit{};for(int axis=0;axis<2;++axis)hit.uv[axis]=w0*triangle.uv[0][axis]+w1*triangle.uv[1][axis]+w2*triangle.uv[2][axis];double depth=0;
    for(int axis=0;axis<3;++axis){hit.world[axis]=w0*triangle.position[0][axis]+w1*triangle.position[1][axis]+w2*triangle.position[2][axis];depth+=(hit.world[axis]-surface.center[axis])*surface.normal[axis]/surface.radius;}
    if(std::abs(depth)>1&&(!surface.unwrapped||triangle.bounded_depth))continue;
    if(std::abs(depth)<nearest){nearest=std::abs(depth);if(surface.planar_fade)hit.fade=float(std::clamp(1-std::abs(depth),0.,1.));best=hit;}
  }
  return best;
}
int payload_side(const PaintPixels::Stamp &stamp,int resolution){
  const double width=2*std::max(std::hypot(stamp.axis_x[0],stamp.axis_x[1]),std::hypot(stamp.axis_y[0],stamp.axis_y[1]))*resolution;
  int side=std::clamp(int(std::ceil(width)),16,128);if(stamp.surface&&!stamp.surface->triangles.empty())side=std::max(4,std::min(side,int(std::sqrt(32.*1024*1024/stamp.surface->triangles.size()))));return side;
}
std::shared_ptr<SmudgePayload> capture(const TilePaintPixels &pixels,const PaintPixels::Stamp &stamp,int resolution){
  auto payload=std::make_shared<SmudgePayload>();const int side=payload_side(stamp,resolution);payload->side=side;payload->pickup.resize(side*side);
  for(int c=0;c<5;++c)if(stamp.channels[c])payload->channels[c].resize(side*side);
  for(int y=0;y<side;++y)for(int x=0;x<side;++x){const double u=2*(x+.5)/side-1,v=2*(y+.5)/side-1;const auto point=contact(stamp,u,v);if(!point)continue;const auto index=std::size_t(y)*side+x;
    float weight=stamp.alpha.sample(u,v,stamp.hardness)*point->fade;if(stamp.stencil)weight*=stamp.surface?stamp.stencil->sample_world(point->world):stamp.stencil->sample_uv(point->uv);payload->pickup[index]=weight;
    for(int c=0;c<5;++c)if(stamp.channels[c])payload->channels[c][index]=sample(pixels,c,resolution,point->uv);
  }
  return payload;
}
void patches(const TilePaintPixels &before,const TilePaintPixels &after,TilePaintPixels &erase,TilePaintPixels &paint){
  std::set<int> tiles;for(const auto *raster:{&before,&after})for(const auto &[tile,pixels]:raster->tiles())tiles.insert(tile);
  for(int tile:tiles){const int resolution=after.tile(tile).resolution()?after.tile(tile).resolution():before.tile(tile).resolution();std::set<std::pair<int,std::uint32_t>> pages;
    for(const auto *raster:{&before,&after})raster->tile(tile).visit_pages([&](int channel,std::uint32_t key,std::span<const PaintPixels::Pixel>){pages.emplace(channel,key);});
    for(const auto &[channel,key]:pages){std::array<PaintPixels::Pixel,4096> erased{},painted{};bool changed=false;
      for(int y=0;y<64;++y)for(int x=0;x<64;++x){const int px=int(key%65536)*64+x,py=int(key/65536)*64+y;const auto old=before.sample(channel,px,py,tile),value=after.sample(channel,px,py,tile);if(old==value)continue;changed=true;erased[y*64+x]={0,0,0,1};painted[y*64+x]=value;}
      if(changed){erase.assign_page(resolution,channel,key,erased,tile);if(std::any_of(painted.begin(),painted.end(),[](const auto &pixel){return pixel[3]>0;}))paint.assign_page(resolution,channel,key,painted,tile);}
    }
  }
}
}
const SmudgePayload &SmudgeCapture::evaluated()const{
 if(adjustment)throw std::logic_error("Adjustment payload requires explicit GPU persistence capture");
 std::call_once(once_,[this]{++local_paint_materializations;PaintRecordingScope cpu(false);result_=capture(source,footprint,resolution);});return *result_;
}
const SmudgePayload &SmudgeCarry::evaluated()const{
 std::call_once(once_,[this]{++local_paint_materializations;PaintRecordingScope cpu(false);const auto &now=current->evaluated();auto result=std::make_shared<SmudgePayload>(now);for(int c=0;c<5;++c)if(!result->channels[c].empty())for(int y=0;y<result->side;++y)for(int x=0;x<result->side;++x){const auto i=std::size_t(y)*result->side+x;const auto carried=previous->sample(c,2*(x+.5)/result->side-1,2*(y+.5)/result->side-1);const float weight=pickup*now.pickup[i];for(int k=0;k<4;++k)result->channels[c][i][k]=std::lerp(carried[k],now.channels[c][i][k],weight);}result_=std::move(result);});return *result_;
}
std::shared_ptr<SmudgePayload> capture_brush_payload(const TilePaintPixels &pixels,const PaintPixels::Stamp &stamp,int resolution,AdjustmentPaintSource adjustment){

 if(!record_gpu_paint){if(adjustment)throw std::logic_error("Adjustment sources require GPU authoring");return capture(pixels,stamp,resolution);}
 auto result=std::make_shared<SmudgePayload>();result->side=payload_side(stamp,resolution);result->recipe=std::make_shared<SmudgeCapture>(pixels,stamp,resolution,std::move(adjustment));return result;
}
SmudgeStroke::SmudgeStroke(const TilePaintPixels &before,int resolution,AdjustmentPaintSource adjustment):before_(before),working_(before),published_(before),resolution_(resolution),adjustment_(std::move(adjustment)){if(!valid_sparse_texture_extent(resolution)||(before.resolution()&&before.resolution()!=resolution))throw std::invalid_argument("Invalid smudge authoring resolution");}
PaintPixels::Bounds SmudgeStroke::dab(const PaintPixels::Stamp &input,const std::set<int> &tiles,float pickup){
  if(!std::isfinite(input.flow)||input.flow<0||input.flow>1||!std::isfinite(input.hardness)||input.hardness<0||input.hardness>1)throw std::invalid_argument("Invalid smudge brush");
  for(const auto &axis:{input.center,input.axis_x,input.axis_y})for(double value:axis)if(!std::isfinite(value)||std::abs(value)>10000)throw std::invalid_argument("Invalid smudge footprint");
  if(input.surface&&(!std::isfinite(input.surface->radius)||input.surface->radius<=0||input.surface->radius>1e12))throw std::invalid_argument("Invalid smudge surface");
  if(!std::isfinite(pickup)||pickup<0||pickup>1)throw std::invalid_argument("Invalid smudge pickup");
  const auto current=capture_brush_payload(working_,input,resolution_,adjustment_);if(!payload_){payload_=current;return {};}
  if(record_gpu_paint){bool any=false;for(int c=0;c<5;++c)if(input.channels[c])any|=current->has_ink(c)||payload_->has_ink(c);if(!any){payload_=current;return {};}}
  auto stamp=input;stamp.smudge=payload_;stamp.projected.reset();const auto bounds=working_.stamp(resolution_,stamp,tiles);include(touched_,bounds);
  if(record_gpu_paint){auto next=std::make_shared<SmudgePayload>();next->side=current->side;next->recipe=std::make_shared<SmudgeCarry>(current,payload_,pickup);payload_=std::move(next);return bounds;}
  auto next=std::make_shared<SmudgePayload>(*current);for(int c=0;c<5;++c)if(!next->channels[c].empty())for(int y=0;y<next->side;++y)for(int x=0;x<next->side;++x){const auto index=std::size_t(y)*next->side+x;const auto carried=payload_->sample(c,2*(x+.5)/next->side-1,2*(y+.5)/next->side-1);const float mix=pickup*next->pickup[index];for(int component=0;component<4;++component)next->channels[c][index][component]=std::lerp(carried[component],current->channels[c][index][component],mix);}
  payload_=std::move(next);return bounds;
}
PaintPixels::Bounds SmudgeStroke::publish(float opacity){return published_.mix_stroke(before_,working_,opacity,touched_);}
std::shared_ptr<const StrokeHistory> append_smudge(const std::shared_ptr<const StrokeHistory> &previous,const StrokeRaster &before,const StrokeRaster &after,bool spatial){
  if(record_gpu_paint){
    if(same_stroke_storage(before,after))return previous;
    StrokeRaster erase{TilePaintPixels::difference(before.paint,after.paint,true),TilePaintPixels::difference(before.ao,after.ao,true)};
    StrokeRaster paint{TilePaintPixels::difference(before.paint,after.paint,false),TilePaintPixels::difference(before.ao,after.ao,false)};
    auto cleared=before;const auto bounds=stroke_bounds(erase);cleared.paint.over_stroke(before.paint,erase.paint,1,bounds,true);cleared.ao.over_stroke(before.ao,erase.ao,1,bounds,true);
    auto history=append_stroke(previous,before,cleared,erase,spatial,true,1);return append_stroke(history,cleared,after,paint,spatial,false,1);
  }
  if(stroke_hashes(before)==stroke_hashes(after))return previous;
  StrokeRaster erase,paint;patches(before.paint,after.paint,erase.paint,paint.paint);patches(before.ao,after.ao,erase.ao,paint.ao);auto cleared=before;const auto bounds=stroke_bounds(erase);cleared.paint.over_stroke(before.paint,erase.paint,1,bounds,true);cleared.ao.over_stroke(before.ao,erase.ao,1,bounds,true);
  auto history=append_stroke(previous,before,cleared,erase,spatial,true,1);if(stroke_hashes(cleared)!=stroke_hashes(after))history=append_stroke(history,cleared,after,paint,spatial,false,1);return history;
}
}
