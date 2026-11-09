// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "MeshMapHistory.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <QJsonArray>
#include <cassert>
#include <cstdio>
int main(){
  std::vector<float> rgba(128*128*4);for(std::size_t i=0;i<rgba.size();i+=4){rgba[i]=.12345671f;rgba[i+1]=.5;rgba[i+2]=1;rgba[i+3]=1;}auto first=std::make_shared<bts::MeshMapPixels>(128,rgba);auto equal=std::make_shared<bts::MeshMapPixels>(128,rgba);assert(first->hash==equal->hash);rgba[0]=.2;auto different=std::make_shared<bts::MeshMapPixels>(128,rgba);assert(first->hash!=different->hash);rgba[3]=1.1;bool rejected=false;try{bts::MeshMapPixels invalid(128,rgba);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
  bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="Mesh maps";material.uniform_editable=true;scene.materials.push_back(material);bts::ImportSummary imported;imported.materials={{1,"Mesh maps"}};bts::TextureDocument document({},imported,scene);auto &set=document.set(1);bts::MeshMap map;map.name="Normal Mesh maps";map.tiles.emplace(1001,bts::MeshMapPlane{128,first->hash,first});set.mesh_maps[0]=map;auto before=document;set.mesh_maps[0]->tiles[1001]={128,different->hash,different};assert(before.set(1).mesh_maps[0]->tiles[1001].pixels==first&&set.mesh_maps[0]->tiles[1001].pixels==different);
  const auto json=document.to_json();const std::array<bts::TextureSetBinding,1> bindings{{{set.id,1,"Mesh maps"}}};auto restored=bts::TextureDocument::from_json(json,bindings);assert(restored.to_json()==json&&!restored.set(1).mesh_maps[0]->tiles[1001].pixels);auto schema10=json;schema10["schema"]=10;assert(bts::TextureDocument::from_json(schema10,bindings).to_json()==json);auto legacy=json;legacy["schema"]=9;assert(!bts::TextureDocument::from_json(legacy,bindings).set(1).mesh_maps[0]);
  auto partial=json;auto partial_sets=partial["texture_sets"].toArray();auto partial_set=partial_sets[0].toObject();partial_set["tiles"]=QJsonArray{1001,1002};partial_sets[0]=partial_set;partial["texture_sets"]=partial_sets;partial["uv_workflow"]="preserve_tiles";auto partial_doc=bts::TextureDocument::from_json(partial,bindings);assert(partial_doc.set(1).tiles.size()==2&&partial_doc.set(1).mesh_maps[0]->tiles.size()==1&&partial_doc.to_json()==partial);
  std::vector<float> rectangle(64*32*4,0);bts::MeshMapPixels rectangular(64,rectangle,32);assert(rectangular.resolution==64&&rectangular.height==32);auto rectangle_doc=document;auto rectangle_pixels=std::make_shared<bts::MeshMapPixels>(64,std::move(rectangle),32);rectangle_doc.set(1).mesh_maps[0]->tiles[1001]={64,rectangle_pixels->hash,rectangle_pixels,32};const auto rectangle_json=rectangle_doc.to_json();assert(bts::TextureDocument::from_json(rectangle_json,bindings).to_json()==rectangle_json);
  auto large_metadata=json;auto sets=large_metadata["texture_sets"].toArray();auto resource=sets[0].toObject();auto maps=resource["mesh_maps"].toArray();auto giant=maps[0].toObject();auto planes=giant["tiles"].toArray();auto plane=planes[0].toObject();plane["resolution"]=4096;plane["height"]=4096;planes[0]=plane;giant["tiles"]=planes;maps[0]=maps[1]=maps[2]=giant;resource["mesh_maps"]=maps;sets[0]=resource;large_metadata["texture_sets"]=sets;rejected=false;try{bts::TextureDocument::from_json(large_metadata,bindings);}catch(const std::invalid_argument &){rejected=true;}assert(!rejected);
  // History invalidation follows the actual immutable GPU pixel key, not a
  // potentially stale serialized descriptor, and never materializes the data.
  bts::MeshMaps old_maps;old_maps[3]=map;auto next=old_maps;
  assert(bts::changed_mesh_map_roles(old_maps,next)==0); // COW owner
  next[3]->name="Only the shelf label changed";assert(bts::changed_mesh_map_roles(old_maps,next)==0);
  next[3]->tiles[1001].pixels=equal;assert(bts::changed_mesh_map_roles(old_maps,next)==0); // same cache identity
  next[3]->tiles[1001].hash="stale descriptor";assert(bts::changed_mesh_map_roles(old_maps,next)==0);
  next[3]->tiles[1001].pixels=different;assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  next=old_maps;next[3]->tiles[1001].pixels.reset();assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  auto unresolved=next;next[3]->tiles[1001].hash="new saved key";assert(bts::changed_mesh_map_roles(unresolved,next)==(1u<<3));
  assert(bts::changed_mesh_map_roles(unresolved,old_maps)==(1u<<3)); // resolved owner arrives
  next=old_maps;next[3]->tiles.emplace(1002,next[3]->tiles.at(1001));assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  assert(bts::changed_mesh_map_roles(next,old_maps)==(1u<<3));
  next=old_maps;next[0]=map;next[3].reset();assert(bts::changed_mesh_map_roles(old_maps,next)==((1u<<0)|(1u<<3)));
  next=old_maps;auto &rect=next[3]->tiles[1001];rect={64,rectangle_pixels->hash,rectangle_pixels,32};assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  next=old_maps;next[3]->tiles[1001].height=64;assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  // Dimensions are part of the real resource key even when equal-length
  // buffers have identical checksums and descriptors lie about their extent.
  std::vector<float> same_bits(64*32*4,0);auto wide=std::make_shared<bts::MeshMapPixels>(64,same_bits,32),tall=std::make_shared<bts::MeshMapPixels>(32,same_bits,64);assert(wide->hash==tall->hash);
  old_maps[3]->tiles[1001]={64,"same descriptor",wide,32};next=old_maps;next[3]->tiles[1001].pixels=tall;assert(bts::changed_mesh_map_roles(old_maps,next)==(1u<<3));
  std::puts("mesh maps: float-bit checksums, invalid coverage rejection, COW image retention, metadata placeholders/roundtrip, schema9/10 migration, rectangular/partial metadata and large baked-map metadata accepted without pixel allocation PASS");
}
