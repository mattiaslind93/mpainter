#include "TextureDocument.hh"
#include "ExportConfiguration.hh"
#include "CompareMaskPanel.hh"
#include "LayerChannelCommand.hh"
#include <QApplication>
#include <QCheckBox>
#include <cassert>
#include <set>
#include <iostream>
int main(int argc,char **argv){
 QApplication app(argc,argv);using namespace bts;
 std::set<MaterialIdColor> unique(material_id_palette.begin(),material_id_palette.end());assert(unique.size()==30);
 SceneSnapshot scene;MaterialSummary summary;summary.session_id=1;summary.name="Features";summary.uniform_editable=true;scene.materials={summary};ImportSummary imported;imported.materials={{1,"Features"}};TextureDocument document({},imported,scene);auto &set=document.set(1);set.texture_resolution=128;set.channels.fill(true);set.precision.fill(ChannelPrecision::RGB32F);
 TextureDocument::add_fill(set);const auto bottom=set.selected_layer;auto &a=TextureDocument::layer(set,bottom);a.values.bump=.4;a.values.base_color={.1,.2,.8,1};
 TextureDocument::add_fill(set);const auto top=set.selected_layer;auto &b=TextureDocument::layer(set,top);b.values.bump=.6;b.values.base_color={.9,.2,.1,1};
 assert(TextureDocument::layer(set,bottom).material_id!=b.material_id); // Compare by IDs below; vector insertion invalidates references.
 const auto red=material_id_rgb(*b.material_id),blue=material_id_rgb(*TextureDocument::layer(set,bottom).material_id);
 assert(TextureDocument::material_id_pixel(set,20,30)==red);
 TextureDocument::add_compare_mask(set);auto &effect=b.mask->effects.front();effect.compare_width=.8;
 assert(std::abs(TextureDocument::mask_value(set,top,20,30)-.75)<1e-5);
 effect.compare_offset=.3;assert(TextureDocument::material_id_pixel(set,20,30)==blue);
 effect.compare_invert=true;assert(TextureDocument::material_id_pixel(set,20,30)==red);
 effect.compare_invert=false;effect.compare_offset=.2;effect.compare_ramp={0,.1f,.9f,.4f,1};
 assert(std::abs(TextureDocument::mask_value(set,top,20,30)-.9)<1e-5);
 const auto saved=mask_effects_json(b.mask->effects);assert(mask_effects_json(read_mask_effects(saved))==saved);
 auto invalid=saved;auto bad=invalid[0].toObject();bad["ramp"]=QJsonArray{0,.5,1};invalid[0]=bad;bool rejected=false;try{read_mask_effects(invalid);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 b.mask->enabled=false;assert(TextureDocument::material_id_pixel(set,20,30)==red);b.visible=false;assert(TextureDocument::material_id_pixel(set,20,30)==blue);b.visible=true;
 b.opacity.fill(.49);assert(TextureDocument::material_id_pixel(set,20,30)==blue);b.opacity.fill(1);
 const auto copy_effect=effect;
 // Normal is a usable authored texture channel, with the same undoable transition as other channels.
 set.channels[3]=false;b.channels[3]=false;assert(set_layer_channel(set,top,3,true));assert(set.channels[3]&&b.channels[3]);
 TextureDocument::add_folder(set);const auto folder=set.selected_layer;auto &f=TextureDocument::layer(set,folder);f.material_id=MaterialIdColor{13,71,197};f.material_id_override_children=true;
 TextureDocument::layer(set,top).parent=folder;TextureDocument::validate_hierarchy(set);
 assert(TextureDocument::material_id_pixel(set,20,30)==material_id_rgb(*f.material_id));f.visible=false;assert(TextureDocument::material_id_pixel(set,20,30)==blue);f.visible=true;f.material_id_override_children=false;assert(TextureDocument::material_id_pixel(set,20,30)==red);
 const auto metadata=document.to_json();const std::array bindings{TextureSetBinding{set.id,1,set.name}};assert(TextureDocument::from_json(metadata,bindings).to_json()==metadata);
 CompareMaskPanel panel(nullptr);auto copy=copy_effect;panel.synchronize(&set,&copy);MaskEffect published;panel.changed=[&](const MaskEffect &e){published=e;};panel.findChild<QDoubleSpinBox *>("compareOffset")->setValue(.35);assert(std::abs(published.compare_offset-.35)<1e-6);panel.findChild<QCheckBox *>("compareInvert")->setChecked(true);assert(published.compare_invert);
 ExportConfiguration config;config.directory="/tmp";config.presets={material_id_preset()};config.preset=config.presets[0].id;const auto json=export_configuration_json(config);assert(export_configuration_json(read_export_configuration(json,{}))==json);
 std::cout<<"Compare ramp/offset/invert, controls, serialization, Material ID coverage/folders/palette/preset, Normal channel passed\n";
}
