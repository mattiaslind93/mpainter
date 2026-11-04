#include "TextureDocument.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main(){SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Bake";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Bake"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
 const QJsonObject prefs{{"project_mesh",false},{"enabled",QJsonArray{false,true,false,false,true,false,false}},{"resolution",512},{"height",512},{"use_cage",true},{"cage_file","/example/cage.obj"},{"high_files",QJsonArray{"/example/high.obj"}},{"tiles",QJsonObject{{document.set(1).id.toString(),QJsonArray{}}}}};document.set_bake_configuration(prefs);const TextureSetBinding binding{document.set(1).id,2,"Bake"};auto json=document.to_json();auto loaded=TextureDocument::from_json(json,std::span(&binding,1));assert(loaded.bake_configuration()==prefs);json.remove("bake_configuration");assert(TextureDocument::from_json(json,std::span(&binding,1)).bake_configuration().isEmpty());
 for(const auto &bad:std::vector<QJsonObject>{{{"project_mesh",1}},{{"resolution",123}},{{"enabled",QJsonArray{true}}},{{"rays",0}},{{"tiles",QJsonObject{{"x",QJsonArray{999}}}}}}){bool rejected=false;try{document.set_bake_configuration(bad);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);assert(document.bake_configuration()==prefs);}
 std::cout<<"Bake preferences preserve explicit false, sources, maps and empty tile selection; legacy defaults and validation passed\n";
}
