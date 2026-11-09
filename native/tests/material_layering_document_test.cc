// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialLayeringStacks.hh"
#include "TextureMemory.hh"
#include "MaterialLayeringIO.hh"
#include "MaterialLayerParameterIO.hh"
#include <cassert>
#include <iostream>
using namespace bts;
TextureDocument make_document(){SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Surface";material.uniform_editable=true;scene.materials.push_back(material);ImportSummary imported;imported.materials.push_back({1,"Surface"});NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;return TextureDocument(options,imported,scene);}
#include "MaterialLayerParameterCodecTest.inc"
int main(){
 verify_material_parameter_codec();
 auto document=make_document();auto &set=document.set(1);TextureDocument::add_fill(set);auto &normal=TextureDocument::layer(set,set.selected_layer);normal.values.base_color={.1,.2,.3,1};const auto main_id=normal.id;const auto ordinary=TextureDocument::output(set);const auto before=document.to_json();
 configure_material_layering(set,true);validate_material_substacks(set);assert(set.material_layering.enabled&&set.layers.size()==8&&TextureDocument::output(set)==ordinary);const auto roots=set.material_layering.mask_roots;const auto channels=set.material_layering.mask_slots;assert(set.material_layering.main_selection==main_id);
 TextureDocument::add_fill(set);const auto fill_id=set.selected_layer;auto &fill=TextureDocument::layer(set,fill_id);assert(fill.parent==roots[0]&&fill.depth==1);fill.users[channels[0]].value=.4;validate_material_substacks(set);assert(std::abs(TextureDocument::composite_user(set,channels[0],8,8)[0]-.4)<1e-6);assert(TextureDocument::composite_user(set,channels[1],8,8)[1]==0);assert(TextureDocument::output(set)==ordinary);
 select_material_substack(set,1);TextureDocument::add_fill(set);TextureDocument::layer(set,set.selected_layer).users[channels[1]].value=.8;select_material_substack(set,0);assert(set.selected_layer==fill_id);const auto view=material_layering_display(set);assert(view.layers.size()==2&&!view.channels[0]&&view.users[channels[0]].name=="Opacity"&&view.layers.front().depth==0);assert(!view.material_layering.allocated());
 auto grouped=set;std::vector<QUuid> mask_children;for(const auto &layer:view.layers)mask_children.push_back(layer.id);TextureDocument::group_layers(grouped,mask_children);validate_material_substacks(grouped);assert(std::abs(TextureDocument::composite_user(grouped,channels[0],8,8)[0]-.4)<1e-6);
 const auto encoded=document.to_json();auto decoded=TextureDocument::from_json(encoded,std::array{TextureSetBinding{set.id,1,"Surface"}});assert(decoded.to_json()==encoded);validate_material_substacks(decoded.set(1));
 configure_material_layering(set,false);assert(!set.material_layering.enabled&&set.selected_layer==main_id);configure_material_layering(set,true);assert(set.material_layering.mask_roots==roots&&set.material_layering.mask_slots==channels&&std::abs(TextureDocument::composite_user(set,channels[0],8,8)[0]-.4)<1e-6);
 select_material_substack(set,2);auto &paint=TextureDocument::layer(set,set.selected_layer);std::array<PaintPixels::Pixel,4096> data;data.fill({.75,0,0,1});paint.users[channels[2]].pixels.assign_page(128,2,0,data,1001);assert(std::abs(TextureDocument::composite_user(set,channels[2],8,8)[0]-precision_rgb({.75,.75,.75},set.users[channels[2]].precision)[0])<1e-6);assert(TextureDocument::composite_user(set,channels[2],90,90)[1]==0);assert(std::abs(TextureDocument::composite_user(set,channels[0],8,8)[0]-.4)<1e-6);
 select_material_substack(set,-1);TextureDocument::add_paint(set);for(int slot:channels)assert(!TextureDocument::layer(set,set.selected_layer).users[slot].enabled);validate_material_substacks(set);
 auto legacy=before;legacy["schema"]=62;auto entries=legacy["texture_sets"].toArray();auto entry=entries[0].toObject();entry.remove("material_layering");entries[0]=entry;legacy["texture_sets"]=entries;auto old=TextureDocument::from_json(legacy,std::array{TextureSetBinding{set.id,1,"Surface"}});assert(!old.set(1).material_layering.allocated());
 auto full=make_document();for(auto &channel:full.set(1).users)channel.allocated=true;const auto full_before=full.to_json();bool rejected=false;try{configure_material_layering(full.set(1),true);}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&full.to_json()==full_before);
 TextureRamQueryScope memory_pressure([]()->std::uint64_t{return 4ULL*1024*1024*1024;});
 auto limited=make_document();limited.set(1).texture_resolution=4096;const auto budget_before=limited.to_json();rejected=false;try{configure_material_layering(limited.set(1),true);}catch(const std::length_error &){rejected=true;}assert(!rejected&&limited.set(1).material_layering.enabled);
 std::cout<<"Three isolated shader opacity stacks, independent fills/paint, view selection, legacy codec, main data retention and atomic capacity/budget failure PASS\n";
}
