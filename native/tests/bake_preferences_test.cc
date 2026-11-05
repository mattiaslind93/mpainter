#include "TextureDocument.hh"
#include "SurfaceBakeSettings.hh"
#include <QJsonArray>
#include <cassert>
#include <iostream>
using namespace bts;
int main(){SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Bake";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials={{1,"Bake"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
 const QJsonObject prefs{{"curvature_radius",.125},{"curvature_scale",3.0},{"ao_distance",2.0},{"thickness_distance",.25},{"normal_detail",.5},{"ray_offset",.0001},{"denoise",false},{"project_mesh",false},{"enabled",QJsonArray{false,true,false,false,true,false,false}},{"resolution",512},{"height",512},{"use_cage",true},{"cage_file","/example/cage.obj"},{"high_files",QJsonArray{"/example/high.obj"}},{"tiles",QJsonObject{{document.set(1).id.toString(),QJsonArray{}}}}};document.set_bake_configuration(prefs);const TextureSetBinding binding{document.set(1).id,2,"Bake"};auto json=document.to_json();auto loaded=TextureDocument::from_json(json,std::span(&binding,1));assert(loaded.bake_configuration()==prefs);json.remove("bake_configuration");assert(TextureDocument::from_json(json,std::span(&binding,1)).bake_configuration().isEmpty());
 for(const auto &bad:std::vector<QJsonObject>{{{"project_mesh",1}},{{"resolution",123}},{{"enabled",QJsonArray{true}}},{{"rays",0}},{{"tiles",QJsonObject{{"x",QJsonArray{999}}}}}}){bool rejected=false;try{document.set_bake_configuration(bad);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);assert(document.bake_configuration()==prefs);}
 MeshBakeSettings bake;read_surface_bake_settings(bake,loaded.bake_configuration());
 assert(bake.curvature_radius==.125f&&bake.curvature_scale==3&&bake.ao_distance==2&&bake.thickness_distance==.25f&&bake.normal_detail==0&&!bake.denoise);
 auto &set=document.set(1);TextureDocument::add_displacement_channel(set);set.displacement.allocated=true;set.displacement.enabled=true;set.displacement.strength=-.2f;set.displacement.subdivision=2;
 TextureDocument::add_fill(set);set.layers.back().displacement.enabled=true;set.layers.back().displacement.value=.75f;
 bake.use_project_mesh=true;capture_bake_channels(bake,document,scene);
 assert(bake.displacement.contains(1)&&bake.displacement.at(1).strength==-.2f&&bake.displacement.at(1).subdivision==2);
 assert(std::abs(bake.displacement.at(1).samples.at(1001).at(64*128+64)-.75f)<1e-6f);
 scene.evaluated_subdivision=true;capture_bake_channels(bake,document,scene);assert(bake.displacement.at(1).subdivision==0);
 set.disabled=true;capture_bake_channels(bake,document,scene);assert(bake.displacement.empty());
 std::cout<<"Bake preferences preserve explicit false, sources, maps and empty tile selection; legacy defaults and validation passed\n";
}
