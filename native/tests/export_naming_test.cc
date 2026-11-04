#include "ExportNaming.hh"
#include <cassert>
#include <iostream>
int main(){
  using bts::export_output_name;
  assert(export_output_name("$mesh/$textureSet_BaseColor","/assets/two_materials.obj","SphereSurface","Base_Color",1001,"png")=="two_materials/SphereSurface_color.png");
  assert(export_output_name("$mesh_$textureSet_$channel_$udim","/assets/model.test.obj","A/B: C","Normal_OpenGL",1012,"exr")=="model_test_A_B__C_normal_opengl_1012.exr");
  assert(export_output_name("$textureSet_Height","Hero.obj","MixedCaseSet","Height",1001,"png")=="MixedCaseSet_bump.png");
  assert(export_output_name("$mesh_$textureSet_ORM","HeroMesh.obj","MixedCaseSet","Packed",1001,"png")=="HeroMesh_MixedCaseSet_orm.png");
  assert(export_output_name("$textureSet_$channel","Hero.obj","MixedCaseSet","My Custom Channel",1001,"png")=="MixedCaseSet_my_custom_channel.png");
  using bts::export_convention_pattern;
  assert(export_output_name("$mesh_$textureSet_$channel_$udim","HeroMesh.obj","MixedCaseSet","base_color",1002,"exr",false,true)=="HeroMesh_MixedCaseSet_base_color_1002.exr");
  assert(export_convention_pattern("$mesh_$textureSet_color_$udim","color","base_color",true)=="$mesh_$textureSet_base_color_$udim");
  assert(export_convention_pattern("$textureSet_base_color.$udim","color","base_color",true)=="$textureSet_base_color.$udim");
  assert(export_convention_pattern("$textureSet_metallic","metallic","base_metalness",true)=="$textureSet_base_metalness");
  assert(export_convention_pattern("$textureSet_roughness","roughness","specular_roughness",true)=="$textureSet_specular_roughness");
  assert(export_convention_pattern("$textureSet_CustomOutput","color","base_color",true)=="$textureSet_custom_output");
  assert(export_convention_pattern("$textureSet_Base_Color","color","base_color",false)=="$textureSet_color");
  assert(export_output_name("$textureSet_$channel","Hero.obj","MixedCaseSet","Bump",1001,"exr",false,true)=="MixedCaseSet_bump.exr");
  assert(export_output_name("$textureSet_$channel","Hero.obj","MixedCaseSet","Displacement",1001,"exr",false,true)=="MixedCaseSet_displacement.exr");
  for(const auto &pattern:{"../bad","/absolute","$project","ok/../bad","a//b","C:/out","bad\\name","a/./b","out?"}){bool rejected=false;try{export_output_name(pattern,"mesh.obj","set","Roughness",1001,"png");}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
  const QString long_component(600,'x');assert(export_output_name(long_component,"mesh.obj","set","Bump",1001,"exr")==long_component+".exr");
  assert(export_output_name("nested/"+long_component+"/$channel_$udim","mesh.obj","set","Bump",1002,"exr",true).endsWith("/bump_<UDIM>.exr"));
  {bts::TextureRamQueryScope low(+[]()->std::uint64_t{return 100000;});bool denied=false;try{export_output_name(QString("$mesh").repeated(100),QString(2000,'x')+".obj","set","Bump",1001,"exr");}catch(const std::length_error &){denied=true;}assert(denied);}
  std::cout<<"Output naming tokens and relative path checks passed\n";
}
