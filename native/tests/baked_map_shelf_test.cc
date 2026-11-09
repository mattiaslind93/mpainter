// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakedMapShelf.hh"
#include "BakedMapPresentation.hh"
#include "MaskFillMapping.hh"
#include "FillTextureJSON.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
  std::vector<float> rgba(32*32*4,0.f);for(std::size_t i=0;i<rgba.size();i+=4){rgba[i]=i==0?.25f:.75f;rgba[i+3]=1;}
  auto pixels=std::make_shared<MeshMapPixels>(32,std::move(rgba));MeshMap map;map.name="Ambient occlusion Plane";map.tiles.emplace(1001,MeshMapPlane{32,pixels->hash,pixels,32});
  const auto bytes=encode_btsmap(map);assert(is_btsmap(bytes));
  const auto id=QUuid::createUuid();const auto decoded=decode_btsmap(id.toString(QUuid::WithoutBraces),bytes,"Ambient occlusion Plane");
  assert(decoded->raw&&decoded->width==32&&decoded->pixels->front()[0]==.25f&&(*decoded->pixels)[1][0]==.75f);
  assert(baked_shelf_id(id,3)=="baked:"+id.toString(QUuid::WithoutBraces)+":3");
  bool rejected=false;try{decode_btsmap(id.toString(QUuid::WithoutBraces),QByteArray("nope"),"x");}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
  // Reproduce the old first-tile mask route, then exercise explicit UDIM lookup.
  std::vector<float> other(pixels->rgba.begin(),pixels->rgba.end());for(std::size_t i=0;i<other.size();i+=4)other[i]=.125f;
  auto second=std::make_shared<MeshMapPixels>(32,std::move(other));map.tiles.emplace(1002,MeshMapPlane{32,second->hash,second,32});
  const auto sequence=decode_btsmap(id.toString(QUuid::WithoutBraces),encode_btsmap(map),"AO two tiles");assert(sequence->tiles&&sequence->tiles->size()==2);
  assert(sequence->tiles->at(1001)->hash==sequence->tiles->at(1002)->hash); // Legacy serialized bundle hash is unchanged.
  assert(sequence->tiles->at(1001)->storage_identity()!=sequence->tiles->at(1002)->storage_identity());
  MaskFillMapping mapping;mapping.projection.texture=sequence;
  const auto repeated=mask_fill_sample(mapping,1,16,16,32,32,1002);assert(repeated==.75f);
  mapping.projection.mode=6;assert(read_mask_fill_mapping(mask_fill_mapping_json(mapping)).projection.mode==6);assert(mask_fill_sample(mapping,1,16,16,32,32,1001)==.75f);assert(mask_fill_sample(mapping,1,16,16,32,32,1002)==.125f);
  const auto again=decode_btsmap(id.toString(QUuid::WithoutBraces),encode_btsmap(map),"AO two tiles");for(int tile:{1001,1002})assert(sequence->tiles->at(tile)->pixels==again->tiles->at(tile)->pixels);
  const auto json=fill_texture_json(sequence);const auto metadata=read_fill_texture_json(json);assert(metadata->tiles->at(1001)->storage_identity()==sequence->tiles->at(1001)->storage_identity());assert(metadata->tiles->at(1002)->storage_identity()==sequence->tiles->at(1002)->storage_identity());assert(fill_texture_json(metadata)==json);const auto bound=bind_fill_texture_descriptor(*metadata,*sequence);assert(bound->tiles->at(1002)->pixels==sequence->tiles->at(1002)->pixels);
  const auto item=present_baked_map(id,"Default",3,map);assert(item.name==QString::fromUtf8("Default – Ambient Occlusion"));assert(!item.thumbnail.isNull()&&item.description.contains("1001, 1002")&&item.description.contains("32 × 32"));assert(item.thumbnail.pixelColor(32,32).red()>150);const auto before=item.thumbnail;map.tiles[1001]=map.tiles.at(1002);const auto changed=present_baked_map(id,"Default",3,map);assert(changed.thumbnail!=before);assert(changed.thumbnail.pixelColor(32,32).red()<50);
  // Declared sizes are bounded by the encoded payload before allocation.
  const auto fails=[&](const QByteArray &input){try{decode_btsmap(id.toString(QUuid::WithoutBraces),input,"payload validation");}catch(const std::exception &){return true;}return false;};
  // Keep identity valid so malformed-payload cases cannot pass for the wrong reason.
  assert(!fails(bytes));
  auto invalid=bytes;const std::uint32_t huge=std::numeric_limits<std::uint32_t>::max();std::memcpy(invalid.data()+8,&huge,4);assert(fails(invalid));
  invalid=bytes;const std::int32_t wide=std::numeric_limits<std::int32_t>::max();std::memcpy(invalid.data()+16,&wide,4);assert(fails(invalid));
  assert(fails(bytes.left(bytes.size()-1)));assert(fails(bytes+QByteArray("x")));
  invalid=encode_btsmap(map);const std::int32_t duplicate=1001;const qsizetype second_header=12+12+qsizetype(32*32*4*sizeof(float));std::memcpy(invalid.data()+second_header,&duplicate,4);assert(fails(invalid));
  {TextureRamQueryScope memory(+[]()->std::uint64_t{return 0;});assert(!fails(bytes));bool denied=false;try{encode_btsmap(map);}catch(const std::length_error &error){denied=std::string(error.what()).find("available")!=std::string::npos;}assert(!denied);}
  std::cout<<"Baked UDIM mask: legacy first-tile reproduction, correct1002, distinct GPU/RAM identity, repeated-decode sharing, unchanged legacy JSON and live named previews PASS\n";
  std::cout<<"Baked shelf map roundtrip passed\n";
}
