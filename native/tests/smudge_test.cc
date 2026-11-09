// SPDX-License-Identifier: GPL-3.0-or-later
#include "SmudgeStroke.hh"
#include "SmudgeCapture.hh"
#include "SmudgeFootprintBounds.hh"
#include "PaintRecording.hh"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace bts;
int main(){
  TilePaintPixels source;PaintPixels::Stamp mark{{.25,.5},{.09,0},{0,.09}};mark.alpha.kind=AlphaKind::Plus;mark.values.base_color={1,1,1,1};mark.values.roughness=.8;mark.channels={true,false,true,false,false};source.stamp(128,mark);
  auto other=mark;other.center={.75,.85};other.values.base_color={.1,.2,.8,1};source.stamp(128,other);const auto before=source;const auto hash=source.hash(0);
  PaintPixels::Stamp dab{{.25,.5},{.065,0},{0,.065}};dab.channels=mark.channels;dab.flow=.5;dab.values.base_color={0,1,0,1};dab.values.roughness=.1;
  SmudgeStroke stroke(source,128);assert(stroke.dab(dab,{1001},.015).empty());assert(stroke.publish(1).empty()&&stroke.pixels().hash(0)==hash);
  for(int i=1;i<=125;++i){dab.center[0]=.25+i*.004;stroke.dab(dab,{1001},.015);}stroke.publish(1);const auto final=stroke.pixels();
  const auto tail=final.sample(0,76,64),rough=final.sample(2,76,64);assert(tail[3]>.01&&before.sample(0,76,64)[3]==0);for(int c=0;c<3;++c)assert(std::abs(tail[c]/tail[3]-1)<1e-5);assert(std::abs(rough[0]/rough[3]-.8)<1e-5);
  assert(final.hash(0)!=hash&&source.hash(0)==hash&&final.sample(0,96,109)==before.sample(0,96,109));stroke.publish(.5);assert(std::abs(stroke.pixels().sample(0,76,64)[3]-tail[3]*.5)<1e-7);stroke.publish(1);assert(stroke.pixels().hash(0)==final.hash(0));
  SmudgeStroke isolated(source,128);dab.center={.25,.5};dab.channels={true,false,false,false,false};isolated.dab(dab,{1001},.015);for(int i=1;i<=50;++i){dab.center[0]=.25+i*.004;isolated.dab(dab,{1001},.015);}isolated.publish(1);assert(isolated.pixels().hash(2)==source.hash(2));
  auto history=append_stroke({}, {},{before,{}},{before,{}},true,false,1);auto smudged=append_smudge(history,{before,{}},{final,{}},true);assert(smudged->operations.size()==history->operations.size()+2&&smudged->operations[0]==history->operations[0]&&smudged->operations[1]->erase&&!smudged->operations[2]->erase);assert(stroke_hashes(replay_strokes(*smudged,128))==stroke_hashes({final,{}}));assert(append_smudge(smudged,{final,{}},{final,{}},true)==smudged);
  SmudgeStroke world(source,128);dab.channels=mark.channels;dab.center={.25,.5};auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->radius=.13;
  PaintPixels::SurfaceTriangle a{{{{0,0},{1,0},{1,1}}},{{{-1,-1,0},{1,-1,0},{1,1,0}}},{}};
  PaintPixels::SurfaceTriangle b{{{{0,0},{1,1},{0,1}}},{{{-1,-1,0},{1,1,0},{-1,1,0}}},{}};surface->triangles={a,b};dab.surface=surface;
  for(int i=0;i<=125;++i){dab.center[0]=.25+i*.004;surface->center={2*dab.center[0]-1,0,0};world.dab(dab,{1001},.015);}world.publish(1);double error=0;for(int y=0;y<128;++y)for(int x=0;x<128;++x)for(int c=0;c<4;++c)error=std::max(error,std::abs(double(world.pixels().sample(0,x,y)[c]-final.sample(0,x,y)[c])));assert(error<2e-5);
  TilePaintPixels tiled;mark.center={.98,.5};tiled.stamp(128,mark,{1001,1002});SmudgeStroke udim(tiled,128);dab.surface.reset();dab.center={.98,.5};for(int i=0;i<=90;++i){dab.center[0]=.98+i*.004;udim.dab(dab,{1001,1002},.015);}udim.publish(1);assert(udim.pixels().sample(0,32,64,1002)[3]>.01);
  TilePaintPixels empty;SmudgeStroke no_ink(empty,128);for(int i=0;i<10;++i){dab.center={.25+i*.01,.5};no_ink.dab(dab,{1001},.015);}assert(no_ink.publish(1).empty()&&no_ink.pixels().empty());
  // Recording captures only immutable source handles; the CPU oracle is lazy,
  // exact, cached, and unaffected by later edits to source pages or geometry.
  dab.surface.reset();dab.center={.25,.5};dab.channels=mark.channels;
  auto original=capture_brush_payload(source,dab,128);
  std::shared_ptr<SmudgePayload> lazy;
  const auto reads=local_paint_materializations;
  {PaintRecordingScope recording;lazy=capture_brush_payload(source,dab,128);lazy->validate();assert(lazy->recipe&&lazy->channels[0].empty()&&local_paint_materializations==reads);}
  auto changed=mark;changed.center={.25,.5};changed.values.base_color={0,0,0,1};source.stamp(128,changed);
  for(int c:{0,2})for(int y=0;y<original->side;++y)for(int x=0;x<original->side;++x){const double u=2*(x+.5)/original->side-1,v=2*(y+.5)/original->side-1;assert(lazy->sample(c,u,v)==original->sample(c,u,v));}
  assert(local_paint_materializations==reads+1);
  dab.surface=surface;surface->center={-.5,0,0};
  auto surface_reference=capture_brush_payload(before,dab,128);
  {PaintRecordingScope recording;lazy=capture_brush_payload(before,dab,128);}
  surface->center={100,100,100};assert(lazy->evaluated().channels==surface_reference->channels);
  dab.surface.reset();dab.center={.75,.85};dab.axis_x={.13,0};dab.axis_y={0,.13};
  auto now=capture_brush_payload(before,dab,128);
  auto carried=std::make_shared<SmudgePayload>();carried->side=now->side;
  {PaintRecordingScope recording;auto current=capture_brush_payload(before,dab,128);carried->recipe=std::make_shared<SmudgeCarry>(current,lazy,.37f);}
  const auto &mixed=carried->evaluated();
  for(int c:{0,2})for(int y=0;y<now->side;++y)for(int x=0;x<now->side;++x){const auto i=y*now->side+x;const auto old=surface_reference->sample(c,2*(x+.5)/now->side-1,2*(y+.5)/now->side-1);for(int k=0;k<4;++k)assert(mixed.channels[c][i][k]==std::lerp(old[k],now->channels[c][i][k],.37f*now->pickup[i]));}
  // Releasing a cancelled long stroke must not recurse through carried payloads.
  {auto chain=carried;for(int i=0;i<20000;++i){auto next=std::make_shared<SmudgePayload>();next->side=16;next->recipe=std::make_shared<SmudgeCarry>(now,chain,.1f);chain=std::move(next);}}
  // Entire interactive gesture, opacity publication and pen-up history stay lazy.
  dab.surface.reset();dab.stencil.reset();dab.center={.25,.5};dab.axis_x={.065,0};dab.axis_y={0,.065};dab.channels=mark.channels;
  SmudgeStroke gpu_stroke(before,128),cpu_stroke(before,128);TilePaintPixels recorded_final;
  auto prior=append_stroke({}, {},{before,{}},{before,{}},true,false,1);std::shared_ptr<const StrokeHistory> recorded_history;
  for(int i=0;i<20;++i){dab.center={.25+i*.012,.5};cpu_stroke.dab(dab,{1001},.035);}
  cpu_stroke.publish(.63);
  const auto gesture_reads=local_paint_materializations;
  {PaintRecordingScope recording;
   for(int i=0;i<20;++i){dab.center={.25+i*.012,.5};gpu_stroke.dab(dab,{1001},.035);gpu_stroke.publish(.9);}
   gpu_stroke.publish(.63);recorded_final=gpu_stroke.pixels();recorded_history=append_smudge(prior,{before,{}},{recorded_final,{}},true);
   assert(recorded_history->operations.size()==prior->operations.size()+2&&local_paint_materializations==gesture_reads);
  }
  const auto export_materializations=PaintPixels::recipe_materializations();
  for(int c=0;c<5;++c)assert(recorded_final.hash(c)==cpu_stroke.pixels().hash(c));
  assert(PaintPixels::recipe_materializations()-export_materializations<=40);
  std::cout<<"Smudge CPU export recipe materializations="<<(PaintPixels::recipe_materializations()-export_materializations)<<" for 20 dabs\n";
  assert(stroke_hashes(replay_strokes(*recorded_history,128))==stroke_hashes({recorded_final,{}}));
  {PaintRecordingScope recording;SmudgeStroke nothing({},128);for(int i=0;i<20;++i){dab.center={.3+i*.005,.5};assert(nothing.dab(dab,{1001},.1).empty());}assert(nothing.publish(.8).empty()&&nothing.pixels().empty());}
  // Erased source pages and opacity zero preserve exact replacement semantics.
  TilePaintPixels wiped;auto diff=TilePaintPixels::difference(before,wiped,true);assert(!diff.empty());
  {PaintRecordingScope recording;gpu_stroke.publish(0);recorded_final=gpu_stroke.pixels();}
  for(int c=0;c<5;++c)assert(recorded_final.hash(c)==before.hash(c));
  // A giant coarse triangle cannot expand the source gather past the brush.
  auto coarse=std::make_shared<PaintPixels::SurfaceFootprint>();coarse->center={0,0,0};coarse->radius=.05;
  PaintPixels::SurfaceTriangle big;big.position={{{-100,-100,0},{100,-100,0},{0,100,0}}};for(int i=0;i<3;++i)big.uv[i]={.5+big.position[i][0]*.005,.5+big.position[i][1]*.005};coarse->triangles={big};dab.surface=coarse;
  const auto small=smudge_source_bounds(dab);assert(small.size()==1);for(const auto &b:small){assert(b[0]>=.4997&&b[1]>=.4997&&b[2]<=.5003&&b[3]<=.5003);}
  std::cout<<"Raw premultiplied smudge transport/tail, no material injection, channel isolation, gesture opacity, preserved spatial history with exact patch replay, world/UV error "<<error<<", UDIM and empty no-op passed\n";
}
