#include "TextureDocument.hh"
#include "GpuLayerEffects.hh"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace bts;
namespace {
void close(float actual,float expected){assert(std::abs(actual-expected)<2e-6f);}
void rejects_content(QJsonObject object){bool rejected=false;try{read_effects(QJsonArray{object});}catch(const std::invalid_argument &){rejected=true;}assert(rejected);}
}
int main(){
 LevelsEffect levels;levels.channel=0;levels.output_low=1;levels.output_high=0;
 const std::array<float,3> input{.2f,.4f,.8f};
 auto legacy=effects_json({levels})[0].toObject();assert(!legacy.contains("opacity")&&!legacy.contains("blending"));const auto old=read_effects(QJsonArray{legacy}).front();assert(old.opacity==1&&old.blending==LayerBlend::Normal);assert(effects_json({old})[0].toObject()==legacy);
 const auto full=apply_levels({levels},0,input);for(int c=0;c<3;++c)close(full[c],1-input[c]);
 levels.opacity=.25f;levels.blending=LayerBlend::Multiply;float coverage=.63f;const auto mixed=apply_content_effects({levels},0,input,coverage,-1,-1,1001,128,128,ChannelPrecision::RGB32F);for(int c=0;c<3;++c)close(mixed[c],input[c]*.75f+(1-input[c])*input[c]*.25f);close(coverage,.63f);
 const auto encoded=effects_json({levels});const auto decoded=read_effects(encoded).front();assert(decoded.blending==LayerBlend::Multiply&&decoded.opacity==.25f);assert(effects_json({decoded})==encoded);
 levels.opacity=0;assert(apply_levels({levels},0,input)==input);levels.opacity=1;levels.blending=LayerBlend::Disabled;assert(apply_levels({levels},0,input)==input);levels.blending=LayerBlend::Normal;assert(apply_levels({levels},0,input)==full);
 levels.channel=2;levels.opacity=.4f;levels.blending=LayerBlend::LinearDodge;const auto scalar=apply_levels({levels},2,{.2f,.2f,.2f});close(scalar[0],.2f*.6f+(.8f+.2f)*.4f);
 for(const auto &value:{QJsonValue(-.1),QJsonValue(1.1),QJsonValue("0.5"),QJsonValue(QJsonValue::Null)}){auto invalid=legacy;invalid["opacity"]=value;rejects_content(invalid);}for(const auto &value:{QJsonValue(-1),QJsonValue(int(layer_blend_names.size())),QJsonValue(.5),QJsonValue("Multiply")}){auto invalid=legacy;invalid["blending"]=value;rejects_content(invalid);}
 TextureSet set;auto &layer=set.layers.front();layer.kind=LayerKind::Fill;layer.mask=LayerMask{};auto &mask=*layer.mask;mask.background=.3f;MaskEffect effect;effect.kind=MaskEffectKind::Levels;effect.levels.id=effect.id;effect.levels.channel=2;effect.levels.output_low=1;effect.levels.output_high=0;mask.effects={effect};close(mask.value(),.7f);const auto old_mask=mask_effects_json(mask.effects);assert(!old_mask[0].toObject()["levels"].toObject().contains("opacity"));assert(read_mask_effects(old_mask).front().opacity==1);
 mask.effects[0].opacity=.4f;mask.effects[0].blending=LayerBlend::Multiply;close(mask.value(),.3f*.6f+(.7f*.3f)*.4f);const auto packed=mask_effects_json(mask.effects);const auto restored=read_mask_effects(packed);assert(restored.front().opacity==.4f&&restored.front().blending==LayerBlend::Multiply);assert(mask_effects_json(restored)==packed);auto reopened=mask;reopened.effects=restored;close(reopened.value(),mask.value());
 std::vector<GpuEffectRow> rows;gpu_append_mask_effects(rows,layer,set);assert(rows.size()==1);close(rows[0][0][2],.4f);assert(int(rows[0][0][1])==int(LayerBlend::Multiply));mask.effects[0].blending=LayerBlend::Disabled;close(mask.value(),.3f);mask.effects[0].blending=LayerBlend::Normal;mask.effects[0].opacity=0;close(mask.value(),.3f);
 // The same metadata survives the full document codec, not just a preset.
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Surface";material.uniform_editable=true;scene.materials={material};ImportSummary imported;imported.materials={{1,"Surface"}};TextureDocument document({},imported,scene);auto &document_set=document.set(1);document_set.layers.front()=layer;document_set.selected_layer=layer.id;document_set.layers.front().effects={decoded};const auto json=document.to_json();const std::vector<TextureSetBinding> bindings{{document_set.id,1,document_set.name}};assert(TextureDocument::from_json(json,bindings).to_json()==json);
 std::puts("Levels mix: independent content/mask formulas, zero/disabled/default identity, strict optional fields, legacy migration, GPU metadata and full document roundtrip PASS");
}
