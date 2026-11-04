#include "TextureDocument.hh"
#include "SurfacePathIO.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
  const BrushSettings brush;assert((brush.values.base_color==std::array<float,4>{0,0,0,1}));
  TextureSet fresh;assert((fresh.base_values.base_color==std::array<float,4>{1,1,1,1}));
  TextureDocument::add_fill(fresh);assert((TextureDocument::edit_values(fresh).base_color==std::array<float,4>{1,1,1,1}));
  SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Saved color";material.values.base_color={.21,.32,.43,1};scene.materials.push_back(material);
  ImportSummary imported;imported.materials={{1,"Saved color"}};TextureDocument doc({},imported,scene);
  auto &set=doc.set(1);assert(set.base_values.base_color==material.values.base_color);
  TextureDocument::add_fill(set);auto &fill=TextureDocument::edit_values(set);fill.base_color={.5,.5,.5,1};
  auto old=doc.to_json();old["schema"]=44;
  const std::array<TextureSetBinding,1> bindings{{{set.id,1,"Saved color"}}};auto restored=TextureDocument::from_json(old,bindings);
  assert(restored.set(1).base_values.base_color==material.values.base_color);
  assert((TextureDocument::edit_values(restored.set(1)).base_color==std::array<float,4>{.5,.5,.5,1}));
  SurfacePath path;path.id="c0a80001-1234-4000-8000-000000000001";path.name="Old brush color";path.points={{{0,0,0},{0,0,1},{.2,.5},1},{{1,0,0},{0,0,1},{.8,.5},1}};path.brush.values.base_color={.5,.5,.5,1};
  assert(read_surface_path(surface_path_json(path))->brush.values.base_color==path.brush.values.base_color);
  std::cout<<"New brush black / base and fill white; imported explicit color and schema44 stored gray fill/brush retained\n";
}
