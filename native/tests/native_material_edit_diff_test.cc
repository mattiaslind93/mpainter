// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialParameters.hh"
#define layer_edit_channels native_edit_diff_under_test
#include "LayerEditDiff.hh"
#undef layer_edit_channels
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
TextureDocument fixture(){
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Native edits";material.uniform_editable=true;scene.materials.push_back(material);
 ImportSummary imported;imported.materials={{1,"Native edits"}};NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);
 auto &set=document.set(1);TextureDocument::add_fill(set);auto &layer=TextureDocument::layer(set,set.selected_layer);layer.fill.atom=false;layer.channels={true,true,true,false,true};layer.ao_enabled=true;set.ao_channel=true;set.displacement.channel=true;set.displacement.allocated=true;layer.displacement.enabled=true;
 auto inputs=std::make_shared<NativeMaterialDescription>();inputs->material="Inputs";inputs->surface_type="ShaderNodeBsdfPrincipled";inputs->channels={0,1,2,4,5,14};NativeMaterialParameter scalar;scalar.identifier=scalar.name="Roughness";scalar.value[0]=.25;inputs->parameters.push_back(scalar);layer.fill.native_material=inputs;layer.fill.material_resource=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();return document;
}
CompositeChannels diff(const TextureDocument &a,const TextureDocument &b){const auto &old=a.texture_sets().front(),&next=b.texture_sets().front();return native_edit_diff_under_test(old,next,edit_set_state(a.edit_state(),old.id),edit_set_state(b.edit_state(),next.id));}
void all_supported(const CompositeChannels &flags){for(int c=0;c<17;++c)assert(flags[c]==(c==0||c==1||c==2||c==3||c==4||c==5||c==16));}
}
int main(){
 auto original=fixture();const auto source=TextureDocument::layer(original.set(1),original.set(1).selected_layer).fill.native_material;
 assert(diff(original,original)==CompositeChannels{});
 for(int kind=0;kind<4;++kind){auto edited=original;auto &fill=TextureDocument::layer(edited.set(1),edited.set(1).selected_layer).fill;auto inputs=std::make_shared<NativeMaterialDescription>(*fill.native_material);
  if(kind==0)inputs->parameters[0].value[0]=.75;
  if(kind==1){auto &p=inputs->parameters[0];p.type=NativeParameterType::Vector;p.value={.2,.4,.8,0};}
  if(kind==2)inputs->parameters[0].ramp={{"stops",QJsonArray{QJsonObject{{"position",.5},{"color",QJsonArray{1,0,0,1}}}}}};
  if(kind==3)inputs->warnings={"Changed metadata"};
  fill.native_material=inputs;all_supported(diff(original,edited));all_supported(diff(edited,original));assert(source->parameters[0].value[0]==.25&&source->parameters[0].ramp.isEmpty());
 }
 {auto edited=original;auto &fill=TextureDocument::layer(edited.set(1),edited.set(1).selected_layer).fill;fill.offset={.2,.3};all_supported(diff(original,edited));}
 // Scalar channel snapshots do not describe Normal-only parameter use. With
 // the independent Bump shading gate enabled, descriptor edits conservatively
 // invalidate Bump4 and its derived Normal3; disabling the gate excludes both.
 for(bool shading_gate:{false,true}){auto edited=original;auto &set=edited.set(1);auto &layer=TextureDocument::layer(set,set.selected_layer);layer.channels[4]=shading_gate;auto inputs=std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);inputs->channels={2};inputs->parameters[0].value[0]=.75;layer.fill.native_material=inputs;auto before=edited;const auto bump=layer.values.bump;const auto normal=layer.values.normal;auto changed=std::make_shared<NativeMaterialDescription>(*inputs);changed->parameters[0].value[0]=.5;layer.fill.native_material=changed;const auto flags=diff(before,edited);for(int c=0;c<17;++c)assert(flags[c]==(c==2||(shading_gate&&(c==3||c==4))));assert(layer.channels[4]==shading_gate&&!layer.channels[3]&&layer.values.bump==bump&&layer.values.normal==normal&&layer.fill.native_material->channels==std::vector<int>{2}&&layer.fill.native_material->height_policy==NativeHeightPolicy::None&&inputs->parameters[0].value[0]==.75&&source->parameters[0].value[0]==.25);}
 {auto edited=original;auto &set=edited.set(1);auto &layer=TextureDocument::layer(set,set.selected_layer);auto inputs=std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);inputs->channels.clear();inputs->parameters[0].value[0]=.75;layer.fill.native_material=inputs;all_supported(diff(original,edited));}
 {auto before=original;auto &set=before.set(1);set.channels={true,false,false,false,false};set.ao_channel=false;set.displacement.channel=false;auto after=before;auto &fill=TextureDocument::layer(after.set(1),after.set(1).selected_layer).fill;auto inputs=std::make_shared<NativeMaterialDescription>(*fill.native_material);inputs->parameters[0].value[0]=.5;fill.native_material=inputs;const auto flags=diff(before,after);for(int c=0;c<17;++c)assert(flags[c]==(c==0));}
 {auto after=original;auto &fill=TextureDocument::layer(after.set(1),after.set(1).selected_layer).fill;fill.manipulator=!fill.manipulator;fill.gizmo_size*=2;fill.linked=!fill.linked;assert(diff(original,after)==CompositeChannels{});}
 {auto before=original;TextureDocument::layer(before.set(1),before.set(1).selected_layer).fill.native_material.reset();auto after=before;TextureDocument::layer(after.set(1),after.set(1).selected_layer).fill.offset={.2,.3};assert(diff(before,after)==CompositeChannels{});}
 {auto after=original;TextureDocument::layer(after.set(1),after.set(1).selected_layer).fill.native_material.reset();all_supported(diff(original,after));all_supported(diff(after,original));}
 std::cout<<"Native artist edit diff: descriptor/UV/legacy/active channels and independent Bump gate, COW, Undo/Redo direction and UI-only/nonnative noops PASS\n";
}
