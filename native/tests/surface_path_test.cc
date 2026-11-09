// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeHistory.hh"
#include <QJsonValue>
#include <QUuid>
#include "SurfacePathPaint.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
  auto path=std::make_shared<SurfacePath>();path->id="c0a80001-1234-4000-8000-000000000001";path->points={{{-.5,0,0},{0,0,1},{.25,.5},1},{{.5,0,0},{0,0,1},{.75,.5},1}};path->brush.diameter=10;path->brush.spacing=20;path->validate();
  const auto points=sample_surface_path(*path,2,[](const auto &p){return std::optional(p);});assert(points.size()>=25&&points.size()<=27);for(std::size_t i=1;i<points.size();++i)assert(std::abs(points[i].position[0]-points[i-1].position[0]-.04)<1e-8);
  auto long_path=*path;long_path.brush.diameter=.1;long_path.brush.spacing=1;
  const auto many=sample_surface_path(long_path,2,[](const auto &point){return std::optional(point);});
  assert(many.size()>16384);assert(std::abs(many.back().position[0]-.5)<.0001);
  StrokeRaster base,ink,later;PaintPixels::Stamp stamp{{.5,.5},{.1,0},{0,.1}};stamp.channels={true,false,false,false,false};stamp.hardness=1;stamp.values.base_color={1,0,0,1};ink.paint.stamp(128,stamp);auto history=append_surface_path({},base,ink,path,128);assert(has_surface_paths(history));auto painted=replay_strokes(*history,128);assert(painted.paint.sample(0,64,64)[0]>.99);
  stamp.values.base_color={0,0,1,1};stamp.axis_x={.03,0};stamp.axis_y={0,.03};later.paint.stamp(128,stamp);auto after=painted;after.paint.over_stroke(painted.paint,later.paint,1,stroke_bounds(later));history=append_stroke(history,painted,after,later,false,false,1);
  auto hidden=std::make_shared<SurfacePath>(*path);hidden->visible=false;auto changed=edit_surface_path(history,path->id,hidden,std::make_shared<StrokeRaster>(ink),128);auto invisible=replay_strokes(*changed,128);assert(invisible.paint.sample(0,64,64)[2]>.99&&invisible.paint.sample(0,73,64)[3]==0);assert(replay_strokes(*history,128).paint.sample(0,73,64)[0]>.99);
  auto deleted=edit_surface_path(history,path->id,{}, {},128);assert(!has_surface_paths(deleted)&&replay_strokes(*deleted,128).paint.sample(0,64,64)[2]>.99);
  bool rejected=false;auto wrong=painted;wrong.paint={};try{append_stroke(history,wrong,wrong,{},false,false,1);}catch(const std::length_error &){rejected=true;}assert(rejected);
  std::shared_ptr<const StrokeHistory> many_paths;for(int i=0;i<140;++i){auto entry=std::make_shared<SurfacePath>(*path);entry->id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();entry->visible=false;many_paths=append_surface_path(many_paths,{}, {},entry,128);}assert(many_paths->operations.size()==140);
  std::cout<<"Surface path validation/world arc spacing, retained operation metadata, visibility/delete replay preserve later ink and COW source, mismatch rejection passed\n";
}
