#pragma once
#include "BrushSettings.hh"
#include "BrushJitterStamp.hh"
#include "StrokeDirection.hh"
#include "StrokeSpacing.hh"
#include "StrokeSpline.hh"
#include "LazyMouse.hh"
#include <string>
#include <vector>
namespace bts {
// A fixed zoomed UV reference, not scene paint/clone/smudge transport. The
// saved brush is never changed: only emitted preview stamps use neutral ink.
struct BrushThumbnailStroke {
 std::vector<PaintPixels::Stamp> stamps;
 float opacity=1;
 static constexpr double uv_extent=.5;
};
struct BrushThumbnailSourceKey {
 std::string parameters;
 std::array<std::shared_ptr<const AlphaBitmap>,3> images;
 bool operator==(const BrushThumbnailSourceKey &)const=default;
};
inline BrushThumbnailSourceKey brush_thumbnail_source_key(const BrushSettings &b){
 BrushThumbnailSourceKey key;const auto add=[&](const auto &v){key.parameters.append(reinterpret_cast<const char *>(&v),sizeof(v));};
 for(float v:{b.diameter,b.spacing,b.flow,b.stroke_opacity,b.hardness,b.angle,b.minimum_size,b.minimum_flow,b.lazy_distance})add(v);
 for(bool v:{b.pressure_size,b.pressure_flow,b.follow_path,b.lazy_mouse})add(v);
 for(float v:b.jitter.values())add(v);
 const auto alpha=[&](const BrushAlpha &a){add(a.kind);add(a.inverted);add(a.uniform);add(a.dynamic.cycle);add(a.dynamic.random_start);add(a.dynamic.roundness);add(a.dynamic.width);add(a.dynamic.phase);};
 const auto stencil=[&](const BrushStencil &s){add(bool(s.resource));if(s.resource)alpha(*s.resource);add(s.tiling);add(s.offset);add(s.scale);add(s.angle);};
 alpha(b.alpha);key.images[0]=b.alpha.bitmap;stencil(b.stencil);if(b.stencil.resource)key.images[1]=b.stencil.resource->bitmap;
 add(bool(b.projected));if(b.projected){stencil(b.projected->transform);add(b.projected->weave);add(b.projected->density);if(b.projected->transform.resource)key.images[2]=b.projected->transform.resource->bitmap;}
 return key;
}
inline BrushThumbnailStroke brush_thumbnail_stroke(const BrushSettings &brush){
 const auto finite=[](float v){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite brush thumbnail setting");};
 for(float v:{brush.diameter,brush.spacing,brush.flow,brush.stroke_opacity,brush.hardness,brush.angle,brush.lazy_distance})finite(v);
 if(brush.diameter<=0||brush.spacing<0||brush.flow<0||brush.flow>100||brush.stroke_opacity<0||brush.stroke_opacity>100||brush.hardness<0||brush.hardness>1||brush.lazy_distance<0)throw std::invalid_argument("Invalid brush thumbnail setting");
 brush.jitter.validate();brush.alpha.dynamic.validate();brush.stencil.validate();if(brush.projected)brush.projected->validate();
 BrushThumbnailStroke result;result.opacity=brush.stroke_opacity/100;
 constexpr double pane=512,pi=3.14159265358979323846;constexpr std::uint64_t seed=0x4254534252534855ULL;
 JitterSampler jitter_sampler(seed);DynamicStrokeSampler dynamic(seed);StrokeDirection direction;StrokeSpacing spacing;StrokeSpline spline;LazyMouse lazy;
 auto stencil=std::make_shared<StencilMapping>();stencil->stencil=brush.stencil;stencil->world=false;stencil->uv_center={.5,.5};stencil->uv_extent=1;stencil->viewport_aspect=1;
 std::shared_ptr<ProjectionMapping> projected;if(brush.projected){projected=std::make_shared<ProjectionMapping>();projected->mapping=*stencil;projected->source=*brush.projected;projected->source.values.base_color={1,1,1,1};}
 StrokeSample previous;bool initialized=false,first=true;
 const auto advance=[&](const StrokeSample &sample){
  const auto from=previous;const double dx=sample.position[0]-from.position[0],dy=sample.position[1]-from.position[1],squared=dx*dx+dy*dy;
  const auto emit=[&](StrokeSpacing::Point point){
   const double t=first||squared<1e-12?1:std::clamp(((point[0]-from.position[0])*dx+(point[1]-from.position[1])*dy)/squared,0.,1.);
   const float pressure=float(std::lerp(from.pressure,sample.pressure,t)),diameter=brush.diameter_at(pressure);
   const auto jitter=first&&brush.follow_path?JitterDab{}:jitter_sampler.sample(brush.jitter);
   if(diameter<=0)return .5;
   const double radius=diameter/200/BrushThumbnailStroke::uv_extent,diameter_pixels=2*radius*pane;
   PaintPixels::Stamp stamp{{point[0]/pane+jitter.position[0]*radius,1-point[1]/pane-jitter.position[1]*radius},{radius*jitter.size,0},{0,radius*jitter.size}};
   const auto heading=direction.uv(stamp.center);
   if(!brush.follow_path||heading){
    stamp.alpha=brush.alpha;if(stamp.alpha.kind==AlphaKind::DynamicOutline)stamp.alpha.dynamic.phase=dynamic.phase(brush.alpha.dynamic);
    stamp.hardness=brush.hardness;stamp.flow=brush.flow_at(pressure)/100*jitter.flow;
    rotate_brush_stamp(stamp,brush.angle*pi/180+(brush.follow_path?*heading:0)+jitter.angle);
    stamp.values.base_color={1,1,1,1};stamp.channels={true,false,false,false,false};if(brush.stencil.resource)stamp.stencil=stencil;if(projected)stamp.projected=projected;
    result.stamps.push_back(std::move(stamp));
   }
   return std::max(.5,diameter_pixels*brush.spacing/100);
  };
  if(!initialized){spacing.reset(sample.position,emit(sample.position));initialized=true;}else spacing.advance(sample.position,emit);
  previous=sample;first=false;
 };
 std::vector<StrokeSample> input;input.reserve(25);
 for(int i=0;i<=24;++i){const double t=double(i)/24;StrokeSample sample{{pane*(.12+.76*t),pane*(.52+.16*std::sin(2*pi*t))},.18+.82*std::sin(pi*t),{}};if(i==0)lazy.reset(sample.position);else if(brush.lazy_mouse)sample.position=lazy.follow(sample.position,brush.lazy_distance/100*pane);input.push_back(sample);}
 spline.reset(input.front());advance(input.front());for(std::size_t i=1;i<input.size();++i)if(auto curve=spline.append(input[i]))curve->flatten(advance);if(auto tail=spline.tail())tail->flatten(advance);
 return result;
}
}
