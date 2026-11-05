#include "StrokeArchive.hh"
#include "StrokeReproject.hh"
#include "CloneStroke.hh"
#include "PaintRecipe.hh"
#include "EvaluationScope.hh"
#include "TextureMemory.hh"
#include <QJsonDocument>
#include <QJsonArray>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace bts;
using Clock=std::chrono::steady_clock;
static std::uint64_t rss(){std::ifstream file("/proc/self/status");std::string line;while(std::getline(file,line))if(line.starts_with("VmRSS:"))return std::stoull(line.substr(6))*1024;return 0;}
static std::uint64_t old_charge(const std::shared_ptr<const StrokeHistory> &history){
 PaintMetadataScope metadata;std::uint64_t pages=0;
 const auto count=[&](const StrokeRaster &raster){for(int c=0;c<5;++c)pages+=raster.paint.page_count(c);pages+=raster.ao.page_count(2);};
 if(history->baseline)count(*history->baseline);for(const auto &op:history->operations)count(*op->ink);
 return pages*64*64*16;
}
int main(){
 {
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="History";scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"History"}};TextureDocument doc({},imported,scene);
  TextureDocument::set_dimensions(doc.set(1),8192,8192,true);assert(doc.set(1).texture_resolution==8192&&doc.set(1).rows()==8192);
  const std::array<TextureSetBinding,1> bindings{{{doc.set(1).id,1,"History"}}};auto restored=TextureDocument::from_json(doc.to_json(),bindings);assert(restored.set(1).texture_resolution==8192&&restored.set(1).rows()==8192);
  // Logical resizing allocates no dense channel images. Native GPU targets
  // and spatial atlases admit their actual buffers when the owner creates them.
  {TextureRamQueryScope metadata_only(+[]()->std::uint64_t{return 1024*1024;});TextureDocument::set_dimensions(doc.set(1),8192,8192,true);assert(doc.set(1).texture_resolution==8192&&doc.set(1).rows()==8192);auto large=TextureDocument::from_json(doc.to_json(),bindings);assert(large.set(1).texture_resolution==8192);}

 }
 // Crossing the old operation cap must not silently rebase away old marks.
 {StrokeRaster empty;std::shared_ptr<const StrokeHistory> history;
  for(int i=0;i<5001;++i)history=append_stroke(history,empty,empty,empty,true,false,1);
  assert(history->operations.size()==5001&&replay_strokes(*history,128).paint.empty());
  const auto restored=read_stroke_history_json(stroke_history_json(history,empty));assert(restored->declared_operations==5001);
 }
 QJsonArray results;
 for(int size:{4096,8192}){
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="History";scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"History"}};TextureDocument doc({},imported,scene);auto &set=doc.set(1);set.texture_resolution=size;auto &layer=set.layers[0];layer.author_resolution=size;
  StrokeRaster current;PaintPixels::Stamp initial{{.5,.5},{.004,0},{0,.004}};initial.values.base_color={.8f,.3f,.1f,1};initial.values.bump=-.001f;initial.hardness=.8;current.paint.stamp(size,initial);
  auto history=append_stroke({}, {},current,current,true,false,1);
  const auto start_materialized=PaintPixels::recipe_materializations();QJsonArray tools;
  {
   PaintMetadataScope metadata;PaintRecordingScope recording;
   for(const char *tool:{"paint","smudge","clone","erase"}){
    const auto bytes_before=allocated_history_bytes(),rss_before=rss(),charged_before=old_charge(history);std::vector<double> durations;
    for(int stroke=0;stroke<200;++stroke){
     const auto start=Clock::now();const auto before=current;auto stamp=initial;stamp.axis_x={4./size,0};stamp.axis_y={0,4./size};stamp.center={.499+double(stroke%13)/size,.499+double(stroke%17)/size};stamp.values.base_color={.1f+float(stroke%7)/10,.6f,.2f,1};stamp.flow=.7;
     if(std::string_view(tool)=="smudge"){
      SmudgeStroke smudge(before.paint,size);smudge.dab(stamp,{1001},.4f);stamp.center[0]+=3./size;smudge.dab(stamp,{1001},.4f);smudge.publish(.8f);current.paint=smudge.pixels();history=append_smudge(history,before,current,true);
     }else{
      if(std::string_view(tool)=="clone"){CloneStroke clone(before.paint,size,initial,true);stamp=clone.dab(stamp);}
      StrokeRaster ink;const auto bounds=ink.paint.stamp(size,stamp);const bool erase=std::string_view(tool)=="erase";current.paint.over_stroke(before.paint,ink.paint,.6f,bounds,erase);history=append_stroke(history,before,current,ink,true,erase,.6f);
     }
     layer.pixels=current.paint;layer.strokes=history;limit_stroke_history(doc);
     durations.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());
    }
    std::sort(durations.begin(),durations.end());assert(durations[190]<=33);
    tools.append(QJsonObject{{"tool",tool},{"strokes",200},{"p95_record_ms",durations[190]},{"max_record_ms",durations.back()},{"new_cpu_page_bytes",double(allocated_history_bytes()-bytes_before)},{"rss_growth_bytes",double(rss())-double(rss_before)},{"old_virtual_charge_bytes",double(old_charge(history)-charged_before)}});
   }
   assert(PaintPixels::recipe_materializations()==start_materialized);
   assert(old_charge(history)>384ULL*1024*1024);
   assert(history->operations.size()>=801);assert(layer.strokes==history);
   // Even an explicit zero budget queues disk spill, never resets the history.
   limit_stroke_history(doc,0);const auto spill=wait_history_spill();assert(spill&&spill->error.empty());assert(layer.strokes==history&&history->operations.size()>=801);
  }
  // Explicit CPU oracle/export boundary. No raster evaluation occurred above.
  const auto expected=current.paint.tile(1001).materialized();std::array<std::uint64_t,5> hashes;for(int c=0;c<5;++c)hashes[c]=expected.hash(c);
  limit_stroke_history(doc,0);const auto spill=wait_history_spill();assert(spill&&spill->error.empty()&&spill->written>0);assert(spill->resident_after<spill->resident_before);
  const auto replay=replay_strokes(*history,size);for(int c=0;c<5;++c)assert(replay.paint.hash(c)==hashes[c]);
  UvTransferMap map;map.resolution=size;auto &samples=map.tiles[1001];samples.resize(std::size_t(size)*size);
  const int x0=size/2-96,y0=size/2-96;for(int y=y0;y<y0+192;++y)for(int x=x0;x<x0+192;++x){auto &sample=samples[std::size_t(y)*size+x];sample.valid=true;sample.source={float((x+.5)/size),float((y+.5)/size)};}
  StrokeRaster flat;flat.paint=current.paint;assert(spatial_history(history,flat));
  auto [moved,transferred]=transfer_stroke_history(history,flat,map,{1001});
  assert(transferred->operations.size()==history->operations.size());assert(transferred->operations.front()->ink!=history->operations.front()->ink);
  for(int c=0;c<5;++c)assert(moved.paint.hash(c)==hashes[c]);
  results.append(QJsonObject{{"resolution",size},{"channels",5},{"tools",tools},{"operations_retained",int(history->operations.size())},{"old_virtual_charge_bytes",double(old_charge(history))},{"unique_page_bytes",double(allocated_history_bytes())},{"spill_written_bytes",double(spill->written)},{"resident_before",double(spill->resident_before)},{"resident_after",double(spill->resident_after)},{"lost_operations",0},{"replay_exact",true},{"uv_identity_exact",true},{"interactive_cpu_materializations",0}});
 }
 std::cout<<QJsonDocument(results).toJson().constData();
}
