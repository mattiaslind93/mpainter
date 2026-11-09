// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshMapImport.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main(){
  for(int role=0;role<7;++role){const auto name=bts::mesh_map_import_name("/own/set_with_underscores_"+std::string(bts::mesh_map_suffixes[role])+".png");assert(name&&name->role==role&&name->set_name=="set_with_underscores"&&name->tile==1001);}
  assert(!bts::mesh_map_import_name("CubeSurface_normal.png")&&!bts::mesh_map_import_name("CubeSurface_Normal_base.png"));
  assert(bts::mesh_map_import_name("Set_normal_base.1002.exr")->tile==1002&&bts::mesh_map_import_name("Set_normal_base_1003.tif")->tile==1003);
  bts::SceneSnapshot scene;bts::MaterialSummary material;material.session_id=1;material.name="CubeSurface";material.uniform_editable=true;scene.materials.push_back(material);bts::ImportSummary imported;imported.materials={{1,"CubeSurface"}};bts::TextureDocument doc({},imported,scene);auto before=doc;
  auto pixels=std::make_shared<bts::MeshMapPixels>(32,std::vector<float>(32*32*4,1));const bts::MeshMapPlane plane{32,pixels->hash,pixels};bts::MeshMapImports maps;maps.maps={{{"WrongSet","WrongSet_curvature",4,1001},plane},{{"CubeSurface","CubeSurface_normal_base",0,1002},plane}};assert(bts::assign_utility_maps(doc,maps).size()==2&&doc.to_json()==before.to_json());
  maps.maps={{{"CubeSurface","CubeSurface_normal_base",0,1001},plane},{{"CubeSurface","last image wins",0,1001},plane}};assert(bts::assign_utility_maps(doc,maps).empty());const auto &set=doc.set(1);assert(set.mesh_maps[0]&&set.mesh_maps[0]->name=="last image wins"&&set.mesh_maps[0]->tiles.at(1001).pixels==pixels);assert(set.layers.size()==1&&set.layers[0].pixels.empty()&&!before.set(1).mesh_maps[0]);for(int role=1;role<7;++role)assert(!set.mesh_maps[role]);
  std::puts("mesh-map import: seven canonical names, legacy/case rejection, UDIM tokens, unmatched set/tile preservation, last-wins assignment and independent empty paint stack PASS");
}
