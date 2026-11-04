#pragma once
#include "BrushSettings.hh"
#include "MaterialLayerUdim.hh"
#include "PaintRecording.hh"
#include "MaterialLayeringStacks.hh"
namespace bts {
// Authoring remains limited to constants and Color/Metal/Rough/Bump/AO maps.
// Native processor admission is a separate owner check before publication.
inline void require_uniform_material_layering(const TextureSet &set) {
 const PaintMetadataScope metadata; // Eligibility never rasterizes deferred GPU paint recipes.
 if(set.disabled||!set.uniform_editable)throw std::invalid_argument("Four-material mode requires an editable ordinary Principled target");
 if(set.shader_parameters.openpbr)throw std::invalid_argument("Disable four-material mode before changing to OpenPBR");
 if(set.bump_origin!=.5f)throw std::invalid_argument("Uniform four-material mode requires canonical Bump origin 0.5");
 if(set.mesh_maps[0])throw std::invalid_argument("Baked Normal is not yet supported by uniform four-material mode");
 const std::array<float,3> flat{0,0,1};
 if(set.base_values.normal!=flat||set.base_values.bump!=.5f)throw std::invalid_argument("Main Normal/Bump must remain neutral in this bounded mode");
 for(const auto &material:set.material_layering.materials){
  material.validate();
  require_material_layer_udim_coverage(material,set.tiles);
  if(material.textures[3])throw std::invalid_argument("Input Normal maps remain internal and are not available in this editor");
  if(material.values.normal!=flat||material.values.bump!=.5f)throw std::invalid_argument("Input Normal must remain flat and uniform Bump must remain neutral 0.5");
 }
 if(set.material_layering.enabled&&set.material_layering.allocated()){
  const auto groups=material_stack_membership(set);
  for(int which=0;which<3;++which)if(std::none_of(set.layers.begin(),set.layers.end(),[&](const auto &layer){return groups.at(layer.id)==which&&layer.id!=set.material_layering.mask_roots[which];}))
   throw std::invalid_argument("Keep at least one layer in each opacity stack; add a replacement before deleting its last layer");
 }
 for(const auto &layer:set.layers){
  // Conservative metadata gate, including retained inactive data: no pixel scan
  // or claim about a derivative equivalence is needed to reject this scope.
  if(layer.fill.native_material||layer.pixels.page_count(3)||layer.pixels.page_count(4)||fill_channel_bound(layer.fill,3)||fill_channel_bound(layer.fill,4))
   throw std::invalid_argument("Retained Main native/Normal/Bump sources require a later layering contract");
  // Paint values are last-brush metadata; its source is the recorded pages above.
  // Uniform Fill values contribute only through their enabled source channel.
  if(layer.kind==LayerKind::Fill&&((layer.channels[3]&&layer.values.normal!=flat)||(layer.channels[4]&&layer.values.bump!=.5f)))throw std::invalid_argument("Enabled Main Fill Normal/Bump values must be neutral for this mode");
  for(const auto &effect:layer.effects)if(effect.channel==3||effect.channel==4)throw std::invalid_argument("Retained Main Normal/Bump effects are not yet supported by this mode");
 }
}
inline std::array<MaterialValues,4> uniform_material_layering_values(const TextureSet &set) {
 std::array<MaterialValues,4> values;for(int i=0;i<4;++i)values[i]=set.material_layering.materials[i].values;
 if(!set.material_layering.allocated())values[0]=set.base_values;return values;
}
inline void normalize_uniform_material_selection(TextureSet &set) {
 const auto &model=set.material_layering;if(!model.enabled||model.active_mask<0)return;const int which=model.active_mask;const auto groups=material_stack_membership(set);
 const auto visible=[&](const QUuid &id){return groups.contains(id)&&groups.at(id)==which&&id!=model.mask_roots[which];};
 if(!visible(set.selected_layer)){
  auto next=model.mask_selections[which];if(!visible(next)){next={};for(auto it=set.layers.rbegin();it!=set.layers.rend();++it)if(visible(it->id)){next=it->id;break;}}
  if(next.isNull())throw std::invalid_argument("Keep at least one layer in each opacity stack; add a replacement before deleting its last layer");
  set.selected_layer=next;set.selected_mask=false;set.selected_effect={};set.selected_geometry=false;
 }
 set.material_layering.mask_selections[which]=set.selected_layer;
}
inline std::array<LayerMaterialInput,4> material_layering_inputs(const TextureSet &set) {
 auto inputs=set.material_layering.materials;
 if(!set.material_layering.allocated())inputs[0].values=set.base_values;
 return inputs;
}
inline TextureSet prepare_material_layering_inputs(const TextureSet &source,bool enabled,const std::array<LayerMaterialInput,4> &inputs) {
 auto candidate=source;candidate.material_layering.materials=inputs;
 if(enabled)require_uniform_material_layering(candidate);
 // Editing inputs must not turn the retained Main view into an opacity edit.
 if(source.material_layering.enabled!=enabled)configure_material_layering(candidate,enabled);
 // First allocation establishes Main defaults. Restore the explicit buffer.
 candidate.material_layering.materials=inputs;
 if(enabled){normalize_uniform_material_selection(candidate);require_uniform_material_layering(candidate);}
 validate_material_substacks(candidate);return candidate;
}
inline TextureSet prepare_uniform_material_layering(const TextureSet &source,bool enabled,const std::array<MaterialValues,4> &values) {
 auto inputs=material_layering_inputs(source);for(int i=0;i<4;++i)inputs[i].values=values[i];
 return prepare_material_layering_inputs(source,enabled,inputs);
}
inline int material_layering_channel_context(const TextureSet &set,int ordinary) {
 if(!set.material_layering.enabled||set.material_layering.active_mask<0)return ordinary;
 const int slot=set.material_layering.mask_slots.at(set.material_layering.active_mask);
 if(slot<0||slot>=custom_channel_count)throw std::invalid_argument("Opacity substack has no shader-owned channel");
 return 6+slot;
}
inline bool material_layering_main_readonly(const TextureSet &set) {
 return set.material_layering.enabled&&set.material_layering.active_mask<0;
}
inline void require_material_layering_edit_target(const TextureSet &set) {
 if(material_layering_main_readonly(set))throw std::invalid_argument("Main is retained read-only while four-material mode is enabled; select an opacity stack or disable the mode");
}
inline void require_material_layering_workflow(const TextureDocument &document,const ShaderInstances &candidate) {
 for(const auto &set:document.texture_sets())if(set.material_layering.enabled&&candidate.for_set(set.id).parameters.openpbr)throw std::invalid_argument("Disable four-material mode before assigning an OpenPBR shader workflow");
}
struct MaterialLayeringBrushRoute {
 struct Channels {std::array<bool,5> channels;std::array<bool,8> users;bool ao,displacement;};
 std::optional<Channels> main;QUuid owner;
 void restore(BrushSettings &brush) {if(main){brush.channels=main->channels;brush.users=main->users;brush.ao=main->ao;brush.displacement=main->displacement;main.reset();owner={};}}
 void synchronize(BrushSettings &brush,const TextureSet *set) {
  const bool mask=set&&set->material_layering.enabled&&set->material_layering.active_mask>=0;
  if(!mask){restore(brush);return;}
  if(main&&owner!=set->id)restore(brush);
  const int slot=set->material_layering.mask_slots.at(set->material_layering.active_mask);
  if(slot<0||slot>=custom_channel_count)throw std::invalid_argument("Opacity substack has no shader-owned channel");
  if(!main){main=Channels{brush.channels,brush.users,brush.ao,brush.displacement};owner=set->id;}
  brush.channels.fill(false);brush.users.fill(false);brush.users[slot]=true;brush.ao=false;brush.displacement=false;
 }
};
}
