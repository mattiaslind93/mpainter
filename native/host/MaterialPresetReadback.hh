#pragma once
#include "SmartMaterialPreset.hh"
#include "GpuProjectReadback.hh"
#include "MaterialInspection.hh"
#include "PaintRecording.hh"
#include <future>
namespace bts {
// Immutable authoring snapshot: transfer only recipe-backed sparse chunks from
// GPU, then encode on a worker. Never evaluate a brush recipe on the CPU.
class MaterialPresetReadback {
 struct Source {TilePaintPixels *destination;TilePaintPixels original;};
 struct Chunk {std::size_t source,index=0;int tile,channel,x,y,width,height;};
 SmartMaterialPreset preset_;
 std::vector<Source> sources_;
 std::vector<Chunk> chunks_;
 std::size_t next_=0;
 GpuProjectReadback readback_;
 std::future<QByteArray> writer_;
 void add(TilePaintPixels &pixels){
  if(!std::any_of(pixels.tiles().begin(),pixels.tiles().end(),[](const auto &p){return bool(p.second.recipe());}))return;
  const auto index=sources_.size();sources_.push_back({&pixels,pixels});
  for(const auto &[tile,plane]:pixels.tiles()){
   const auto versions=plane.page_versions();
   for(int channel=0;channel<5;++channel){std::set<std::pair<int,int>> blocks;for(const auto &[key,version]:versions[channel])blocks.emplace(int(key%65536)/4,int(key/65536)/4);
    for(const auto &[x,y]:blocks)chunks_.push_back({index,0,tile,channel,x*256,y*256,std::min(256,plane.resolution()-x*256),std::min(256,plane.resolution()-y*256)});
   }
  }
 }
 QByteArray encode(){
  GpuPersistenceScope no_cpu_recipes;
  std::vector<TilePaintPixels> canonical(sources_.size());
  for(const auto &chunk:chunks_){const auto &plane=sources_[chunk.source].original.tile(chunk.tile);const auto versions=plane.page_versions();const auto &image=readback_.images().at(chunk.index);
   for(int y=0;y<chunk.height;y+=64)for(int x=0;x<chunk.width;x+=64){const auto key=std::uint32_t((chunk.y+y)/64)*65536+std::uint32_t((chunk.x+x)/64);if(!versions[chunk.channel].contains(key))continue;
    std::array<PaintPixels::Pixel,64*64> page;
    for(int py=0;py<64;++py)for(int px=0;px<64;++px){const auto *pixel=image.pixels+(image.uniform?0:(std::size_t(y+py)*chunk.width+x+px)*4);std::copy_n(pixel,4,page[py*64+px].begin());}
    if(std::any_of(page.begin(),page.end(),[](const auto &pixel){return pixel[3]>0;}))canonical[chunk.source].assign_page(plane.resolution(),chunk.channel,key,page,chunk.tile);
   }
  }
  for(std::size_t i=0;i<sources_.size();++i)*sources_[i].destination=std::move(canonical[i]);
  auto result=write_material_preset(preset_);readback_.release_cpu();return result;
 }

 public:
 explicit MaterialPresetReadback(const TextureSet &source,const QUuid &folder):preset_(capture_material_folder(source,folder)){
  PaintMetadataScope metadata;
  const auto effect=[&](EffectPaint &paint){paint.strokes.reset();paint.user_strokes={};paint.displacement_strokes.reset();for(int i=0;i<11;++i)add(paint.plane(i));};
  for(auto &layer:preset_.set.layers){layer.strokes.reset();add(layer.pixels);add(layer.ao_pixels);layer.displacement.strokes.reset();add(layer.displacement.pixels);for(auto &user:layer.users){user.strokes.reset();add(user.pixels);}for(auto &entry:layer.effects)if(entry.kind==ContentEffectKind::Paint)effect(entry.paint);if(layer.mask){layer.mask->strokes.reset();add(layer.mask->pixels);for(auto *entry:edit_mask_effects_of_kind(layer.mask->effects,MaskEffectKind::Paint))effect(entry->paint);}}
 }
 ~MaterialPresetReadback(){if(writer_.valid())writer_.wait();}
 std::optional<QByteArray> poll(MaterialInspection &inspection){
  if(writer_.valid()){if(writer_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};return writer_.get();}
  const auto before=readback_.images().size();const bool ready=readback_.poll();
  const auto started=performance::Clock::now();inspection.begin_project_source_capture();GpuPersistenceScope no_cpu_recipes;PaintMetadataScope metadata;
  while(next_<chunks_.size()){
   auto &chunk=chunks_[next_];const auto &plane=sources_[chunk.source].original.tile(chunk.tile);chunk.index=readback_.images().size();
   const auto key="smart-material/"+std::to_string(reinterpret_cast<std::uintptr_t>(this))+"/"+std::to_string(chunk.source)+":"+std::to_string(chunk.tile)+":"+std::to_string(chunk.channel);
   if(!inspection.capture_project_source(key,plane,chunk.channel,chunk.tile,chunk.x,chunk.y,chunk.width,chunk.height,readback_))return {};
   ++next_;if(performance::ms(started)>=3)return {};
  }
  if(!ready||before!=readback_.images().size())return {};
  writer_=std::async(std::launch::async,[this]{return encode();});return {};
 }
};
}
