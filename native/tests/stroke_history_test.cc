#include "StrokeHistory.hh"
#include "PaintRecipe.hh"
#include <cassert>
#include <iostream>
int main(){using namespace bts;
 {
  StrokeRaster initial,ink,after;PaintPixels::Stamp dab{{.5,.5},{.1,0},{0,.1}};dab.flow=.3;
  const auto count=PaintPixels::recipe_materializations();std::shared_ptr<const StrokeHistory> history;
  {PaintRecordingScope recording;ink.paint.stamp(128,dab);after.paint.over_stroke(initial.paint,ink.paint,.7,{0,0,128,128});history=append_stroke({},initial,after,ink,true,false,.7);assert(history->matches(after));assert(!stroke_bounds(ink).empty());}
  assert(PaintPixels::recipe_materializations()==count);
  const auto reference=replay_strokes(*history,128);assert(stroke_hashes(reference)==history->final_hashes());assert(history->matches(after));
 }
StrokeRaster blank,ink;PaintPixels::Stamp stamp{{.5,.5},{.2,0},{0,.2}};stamp.channels={true,false,false,false,false};stamp.values.base_color={1,0,0,1};ink.paint.stamp(128,stamp);StrokeRaster after;after.paint.over_stroke(blank.paint,ink.paint,.5,{0,0,128,128});auto first=append_stroke({},blank,after,ink,true,false,.5);assert(first->operations.size()==1&&first->operations[0]->spatial);assert(stroke_hashes(replay_strokes(*first,128))==stroke_hashes(after));auto before=after;after.paint.over_stroke(before.paint,ink.paint,.25,{0,0,128,128},true);auto second=append_stroke(first,before,after,ink,false,true,.25);assert(first->operations.size()==1&&second->operations.size()==2&&!second->operations[1]->spatial);assert(stroke_hashes(replay_strokes(*second,128))==stroke_hashes(after));
 PaintPixels restored;ink.paint.tile(1001).visit_pages([&](int c,std::uint32_t key,std::span<const PaintPixels::Pixel> page){restored.assign_page(128,c,key,page);});assert(restored.hash(0)==ink.paint.hash(0));auto changed=before;changed.paint.stamp(128,stamp);auto reset=append_stroke(first,changed,after,ink,true,false,1);assert(reset->operations.size()==1);std::cout<<"Stroke origin, immutable history, opacity/erase replay and sparse page roundtrip passed\n";}
