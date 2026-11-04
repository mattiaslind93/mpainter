#include "TextureDocument.hh"
#include "SubdivisionPolicy.hh"
#include "TextureMemory.hh"
#include "SurfacePathIO.hh"
#include "EffectPaintIO.hh"
#include "StrokeArchive.hh"
#include <QIODevice>
#include <cassert>
#include <iostream>
#include <limits>
using namespace bts;
int main(){
  assert(edge_length_subdivision(0,1./64)==0);assert(edge_length_subdivision(1,.5)==1);assert(edge_length_subdivision(1,.25)==2);assert(edge_length_subdivision(1,1./64)==6);assert(edge_length_subdivision(10,1e-6)==11);assert(edge_length_subdivision(.125,.125)==0);assert(edge_length_subdivision(.125,.13)==0);assert(edge_length_subdivision(1e308,1e-6)==11);
  for(auto values:std::array<std::array<double,2>,3>{{{-1,1},{1,0},{1,std::numeric_limits<double>::infinity()}}}){bool invalid=false;try{edge_length_subdivision(values[0],values[1]);}catch(const std::invalid_argument &){invalid=true;}assert(invalid);}

  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Displacement";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Displacement"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);auto &set=document.set(1);
  for(int mode:{0,1,2}){auto settings=DisplacementSettings{};settings.mode=mode;assert(read_displacement(displacement_json(settings)).mode==mode);}auto bad_mode=displacement_json(DisplacementSettings{});bad_mode["mode"]=3;bool rejected=false;try{read_displacement(bad_mode);}catch(const std::exception &){rejected=true;}assert(rejected);
  assert(DisplacementSettings{}.strength==1);auto explicit_zero=displacement_json(DisplacementSettings{});explicit_zero["strength"]=0;assert(read_displacement(explicit_zero).strength==0);
  auto signed_document=document;auto &signed_set=signed_document.set(1);TextureDocument::add_displacement_channel(signed_set);signed_set.displacement.midlevel=0;auto &signed_layer=signed_set.layers.front();PaintPixels::Stamp signed_dab{{.5,.5},{.15,0},{0,.15}};signed_dab.channels={false,false,true,false,false};signed_dab.hardness=1;signed_dab.values.roughness=-.75;signed_layer.displacement.pixels.stamp(128,signed_dab);assert(TextureDocument::displacement_value(signed_set,64,64)==-.75f);signed_dab.values.roughness=.75;signed_layer.displacement.pixels.stamp(128,signed_dab);assert(TextureDocument::displacement_value(signed_set,64,64)==.75f);
  TextureSet old_folder;old_folder.layers[0].kind=LayerKind::Folder;TextureDocument::add_displacement_channel(old_folder);assert(old_folder.layers[0].displacement.enabled&&old_folder.layers[0].displacement.blending==LayerBlend::Passthrough);
  assert(!set.displacement.channel&&!set.displacement.allocated);TextureDocument::add_displacement_channel(set);assert(set.displacement.channel&&set.layers.front().displacement.enabled);
  assert(TextureDocument::displacement_value(set,64,64)==0);
  TextureDocument::add_fill(set);auto &added_fill=set.layers.back();assert(!added_fill.displacement.enabled&&added_fill.displacement.value==set.displacement.midlevel);assert(std::abs(TextureDocument::displacement_value(set,64,64))<1e-6);added_fill.displacement.enabled=true;assert(std::abs(TextureDocument::displacement_value(set,64,64)-set.displacement.midlevel)<1e-6);set.displacement.midlevel=0;added_fill.displacement.value=set.displacement.midlevel;assert(std::abs(TextureDocument::displacement_value(set,64,64))<1e-6);set.displacement.midlevel=.5f;added_fill.displacement.value=.8;assert(std::abs(TextureDocument::displacement_value(set,64,64)-.8)<1e-6);
  set.layers.back().values.bump=-.5;assert(std::abs(TextureDocument::displacement_value(set,64,64)-.8)<1e-6);
  TextureDocument::add_paint(set);auto &paint=set.layers.back();assert(!paint.displacement.enabled);paint.displacement.enabled=true;paint.displacement.author_resolution=128;PaintPixels::Stamp dab{{.5,.5},{.15,0},{0,.15}};dab.channels={false,false,true,false,false};dab.hardness=1;dab.values.roughness=.2;paint.displacement.pixels.stamp(128,dab);TextureDocument::add_mask(set,.5);
  assert(std::abs(TextureDocument::displacement_value(set,64,64)-.5)<1e-6);paint.displacement.blending=LayerBlend::Multiply;assert(std::abs(TextureDocument::displacement_value(set,64,64)-.48)<1e-6);paint.visible=false;assert(std::abs(TextureDocument::displacement_value(set,64,64)-.8)<1e-6);paint.visible=true;
  auto before=document;const auto previous=paint.displacement.pixels.hash(2);dab.values.roughness=1;paint.displacement.pixels.stamp(128,dab);assert(before.set(1).layers.back().displacement.pixels.hash(2)==previous&&paint.displacement.pixels.hash(2)!=previous);
  const StrokeRaster ink{paint.displacement.pixels,{}};paint.displacement.strokes=append_stroke({}, {},ink,ink,true,false,1);
  set.displacement.mode=2;set.displacement.strength=.4;set.displacement.midlevel=.25;assert(set.displacement.bounds()>=.3);
  const auto metadata=document.to_json();assert(metadata["schema"].toInt()==bts::texture_document_schema);const std::array<TextureSetBinding,1> bindings{{{set.id,1,set.name}}};auto restored=TextureDocument::from_json(metadata,bindings);
  for(int i=0;i<int(set.layers.size());++i)restored.set(1).layers[i].displacement.pixels=set.layers[i].displacement.pixels;
  read_stroke_archive(restored,write_stroke_archive(document));assert(restored.to_json()==metadata);assert(restored.set(1).layers.back().displacement.strokes->loaded);assert(restored.set(1).layers.back().displacement.pixels.hash(2)==paint.displacement.pixels.hash(2));
  auto old=metadata;old["schema"]=46;auto legacy=TextureDocument::from_json(old,bindings);assert(!legacy.set(1).displacement.channel&&!legacy.set(1).displacement.allocated);for(const auto &layer:legacy.set(1).layers)assert(!layer.displacement.enabled&&layer.displacement.pixels.empty());
  SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.name="Separate channels";path.points={{{0,0,0},{0,0,1},{.2,.5},1},{{1,0,0},{0,0,1},{.8,.5},1}};path.brush.displacement_value=.9;path.brush.values.bump=-.125;const auto recipe=surface_path_json(path);assert(recipe["version"].toInt()==6);auto current=read_surface_path(recipe);assert(current->brush.displacement&&current->brush.displacement_value==.9f&&current->brush.values.bump==-.125f);current->validate();auto negative_path=path;negative_path.brush.displacement_value=-.9;const auto negative_recipe=surface_path_json(negative_path);assert(read_surface_path(negative_recipe)->brush.displacement_value==-.9f);auto old_recipe=recipe;old_recipe["version"]=2;auto old_brush=old_recipe["brush"].toObject();old_brush.remove("displacement");old_recipe["brush"]=old_brush;auto old_path=read_surface_path(old_recipe);assert(!old_path->brush.displacement&&old_path->brush.values.bump==-.125f);
  // A literal old ten-plane archive must remain readable after adding plane 10.
  auto old_effect=effect_paint_json(EffectPaint{});auto planes=old_effect["planes"].toArray();planes.removeLast();old_effect["planes"]=planes;auto effect=read_effect_paint(old_effect);QByteArray raw(40,'\0');QDataStream stream(&raw,QIODevice::ReadOnly);stream.setVersion(QDataStream::Qt_6_8);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);std::uint64_t decoded=0;read_effect_paint(stream,effect,{1001},decoded,1024*1024);assert(stream.atEnd()&&effect.loaded&&effect.displacement.empty()&&effect_paint_json(effect)["planes"].toArray().size()==11);
  // Legacy ten-plane effect paint with a retained v2 path must normalize safely,
  // including a nonzero signed legacy Bump brush value, while preserving pixels.
  EffectPaint old_painted;old_painted.resolution=128;dab.values.roughness=.2;old_painted.pixels.stamp(128,dab);auto definition=std::make_shared<SurfacePath>(path);definition->brush.displacement=false;
  old_painted.strokes=append_surface_path({}, {},{old_painted.pixels,{}},definition,128);
  auto descriptor=effect_paint_json(old_painted);auto old_planes=descriptor["planes"].toArray();old_planes.removeLast();descriptor["planes"]=old_planes;descriptor.remove("displacement_strokes");auto history=descriptor["strokes"].toObject();auto records=history["paths"].toArray();auto record=records[0].toObject();record["path"]=old_recipe;records[0]=record;history["paths"]=records;descriptor["strokes"]=history;
  QByteArray packed;QDataStream writer(&packed,QIODevice::WriteOnly);writer.setVersion(QDataStream::Qt_6_8);writer.setByteOrder(QDataStream::LittleEndian);writer.setFloatingPointPrecision(QDataStream::SinglePrecision);decoded=0;write_effect_paint(writer,old_painted,decoded,8*1024*1024);packed.chop(4); // Old format ends after User7; new final plane is empty.
  auto retained=read_effect_paint(descriptor);QDataStream reader(&packed,QIODevice::ReadOnly);reader.setVersion(QDataStream::Qt_6_8);reader.setByteOrder(QDataStream::LittleEndian);reader.setFloatingPointPrecision(QDataStream::SinglePrecision);decoded=0;read_effect_paint(reader,retained,{1001},decoded,8*1024*1024);assert(reader.atEnd()&&retained.pixels.hash(2)==old_painted.pixels.hash(2));assert(!retained.strokes->declared_paths.at(0)->brush.displacement&&retained.strokes->declared_paths.at(0)->brush.values.bump==-.125f);
  LevelsEffect overlay;overlay.kind=ContentEffectKind::Paint;overlay.paint.resolution=128;dab.values.roughness=.2;overlay.paint.displacement.stamp(128,dab);float coverage=1;const auto result=apply_content_effects({overlay},14,{.8,.8,.8},coverage,64,64,1001,128,128,ChannelPrecision::L32F);assert(std::abs(result[0]-.2)<1e-6&&coverage==1);
  // OS MemAvailable can change between two reads while other apps run. Exercise
  // the real budget policy against the existing deterministic query seam.
  std::uint64_t budget=0;
  {TextureRamQueryScope query([]()->std::uint64_t{return 16ULL*1024*1024*1024;});
   const auto previous_valid=texture_vram_query_valid.exchange(false);
   const auto previous_vram=texture_free_vram.exchange(2ULL*1024*1024*1024);
   budget=ResourceAvailability{}.ram();assert(budget==16ULL*1024*1024*1024);
   texture_vram_query_valid=true;assert(ResourceAvailability{}.vram()==2048ULL*1024*1024);
   texture_free_vram=512ULL*1024*1024;assert(ResourceAvailability{}.vram()==512ULL*1024*1024);
   texture_vram_query_valid=previous_valid;texture_free_vram=previous_vram;
  }
  std::cout<<"Separate displacement/bump data, scalar masks/blends/visibility/COW, schema47+legacy46, archive role10, pathv3+legacyv2, old ten-plane effects, and displacement effects passed; bake_budget_mib="<<(budget/1048576)<<"\n";
}
