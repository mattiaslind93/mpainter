// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "WarpProjection.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 SceneSnapshot scene;ImportSummary imported;for(unsigned uid:{1,2}){MaterialSummary m;m.session_id=uid;m.name="Set"+std::to_string(uid);m.uniform_editable=true;scene.materials.push_back(m);imported.materials.push_back({uid,m.name});}NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);auto &source=document.set(1);TextureDocument::add_fill(source);auto &fill=TextureDocument::layer(source,source.selected_layer);fill.fill.mode=5;fill.fill=toggle_warp_geometry(fill.fill,true);
 auto &consumer=document.set(2);const auto instance=TextureDocument::add_instance(consumer,source.id,fill.id,"Warp source");document.resolve_instances();auto evaluated=std::make_shared<std::vector<TextureLayer>>(*consumer.instance_layers);auto projected=std::find_if(evaluated->begin(),evaluated->end(),[&](const auto &layer){return layer.id==instance;});assert(projected!=evaluated->end());auto cache=std::make_shared<WarpGeometryCache>();cache->material=2;cache->authored=warp_authored_grid(projected->fill);cache->offset=projected->fill.offset3;cache->rotation=projected->fill.rotation3;cache->scale=projected->fill.scale3;auto fitted=std::make_shared<WarpGrid>(*cache->authored);fitted->at(1,1).position[1]+=.1;fitted->align_tangents();cache->fitted.grid=fitted;projected->fill.warp_fit=cache;consumer.instance_layers=evaluated;const auto before=document.to_json();
 document.resolve_instances();const auto &resolved=TextureDocument::layer(const_cast<TextureSet &>(consumer),instance);(void)resolved;auto find=[&]()->const TextureLayer &{for(const auto &layer:evaluation_layers(consumer))if(layer.id==instance)return layer;throw std::runtime_error("Missing instance");};assert(find().fill.warp_fit==cache&&fill_warp_grid(find().fill)==fitted);assert(document.to_json()==before);
 fill.fill.offset3[0]+=.2;document.resolve_instances();assert(!find().fill.warp_fit);assert(fill_warp_grid(find().fill)==warp_authored_grid(fill.fill));
 std::cout<<"Warp instance consumer fit survives unchanged evaluation/export and is invalidated by author pose changes PASS\n";
}
