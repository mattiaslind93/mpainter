#include "OrderedRawRgbFixture.hh"
#include "LayerApi.hh"
#include "FillTexturePool.hh"
#include "LayerChannelCommand.hh"
#include "OrdinaryColorSource.hh"
#include <QCoreApplication>
#include <cassert>
#include <iostream>
using namespace bts;
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);using namespace ordered_rgb_test;auto doc=document();auto &set=doc.set(1);const auto original_pixels=set.layers.back().fill.texture->pixels;
 const auto source=*original_pixels;double maximum=0,order_difference=0,tint_difference=0;std::uint64_t checked=0;
 for(auto precision:{ChannelPrecision::SRGB8,ChannelPrecision::RGB8,ChannelPrecision::RGB16,ChannelPrecision::RGB16F,ChannelPrecision::RGB32F}){
  set.precision[0]=precision;
  for(int tile:{1001,1002})for(int y=0;y<128;y+=7)for(int x=0;x<128;x+=5){
   const auto &top=set.layers.back();const auto &transform=*top.effects.back().linear_color;
   const auto reference=expected(set,x,y,tile,[&](auto rgb){return matrix(transform,rgb);});
   const auto value=TextureDocument::evaluate(set,x,y,tile);
   for(int c=0;c<3;++c){maximum=std::max(maximum,std::abs(double(value.material.base_color[c])-reference[c]));assert(std::abs(double(value.material.base_color[c])-reference[c])<5e-6);++checked;}
   const auto raw=sample(x,y,tile),correct=matrix(transform,levels(top,raw)),reversed=levels(top,matrix(transform,raw));
   auto tinted=levels(top,raw);for(int c=0;c<3;++c)tinted[c]*=top.values.base_color[c];tinted=matrix(transform,tinted);
   for(int c=0;c<3;++c){order_difference=std::max(order_difference,std::abs(correct[c]-reversed[c]));tint_difference=std::max(tint_difference,std::abs(correct[c]*top.values.base_color[c]-tinted[c]));}
   const double rough=TextureDocument::evaluate(set,x,y,tile).material.roughness;assert(std::abs(rough-std::clamp(raw[0],0.,1.))<2e-6);
  }
 }
 assert(order_difference>.05&&tint_difference>.05&&*original_pixels==source);
 // Legacy Fill precision and tint still precede Levels when no typed raw
 // boundary exists. Use the same signed/HDR source to expose early narrowing.
 auto ordinary=set;ordinary.layers.back().effects.pop_back();
 for(auto precision:{ChannelPrecision::RGB8,ChannelPrecision::RGB16,ChannelPrecision::RGB16F,ChannelPrecision::RGB32F}){
  ordinary.precision[0]=precision;
  for(int y=0;y<128;y+=9)for(int x=0;x<128;x+=11){auto value=sample(x,y,1001);const auto &top=ordinary.layers.back();
   for(int c=0;c<3;++c)value[c]=quantize(value[c]*top.values.base_color[c],precision);value=levels(top,value);
   const auto actual=TextureDocument::evaluate(ordinary,x,y,1001);
   for(int c=0;c<3;++c){const double bottom=quantize(ordinary.layers.front().values.base_color[c],precision),expected=bottom*(1-top.opacity[0])+bottom*value[c]*top.opacity[0];assert(std::abs(actual.material.base_color[c]-expected)<5e-6);}
  }
 }
 const auto state=doc.to_json();std::vector<TextureSetBinding> bindings;for(const auto &s:doc.texture_sets())bindings.push_back({s.id,s.material_session_uid,s.name});
 auto reopened=TextureDocument::from_json(state,bindings);assert(reopened.to_json()==state);auto &restored=reopened.set(1).layers.back();
 restored.fill.texture=bind_fill_texture_descriptor(*restored.fill.texture,*set.layers.back().fill.texture);fill_channel_texture(restored.fill,2)=bind_fill_texture_descriptor(*fill_channel_texture(restored.fill,2),*set.layers.back().fill.texture);
 assert(restored.fill.texture->pixels==fill_channel_texture(restored.fill,2)->pixels&&restored.fill.texture->pixels==original_pixels);
 const auto reference=layer_api_reference(doc,set,set.layers.back(),"content",set.layers.back().effects.back().id);const QJsonObject request{{"reference",reference}};
 const auto info=layer_api_query(doc,"layers.info",request,{}).toObject();assert(info["kind"]=="linear_color_transform"&&!info["has_blending"].toBool());
 bool rejected=false;try{layer_api_edit(doc,"layers.set",{{"reference",reference},{"property","opacity"},{"channel",0},{"value",.5}},{});}catch(const std::invalid_argument &){rejected=true;}assert(rejected&&doc.to_json()==state);
 const auto fill_reference=layer_api_reference(doc,set,set.layers.back());rejected=false;
 try{layer_api_edit(doc,"layers.set_source",{{"reference",fill_reference},{"channel",0},{"source",QJsonArray{.5,.2,.7}}},{});}catch(const std::invalid_argument &){rejected=true;}
 assert(rejected&&doc.to_json()==state&&doc.set(1).layers.back().fill.texture->pixels==original_pixels);
 LayerApiResources resources;StudioAsset replacement;replacement.usages={"texture"};resources.asset=[&](const QUuid &,bool){return replacement;};resources.texture=[&](const StudioAsset &,bool raw){auto image=std::make_shared<FillTexture>(*doc.set(1).layers.back().fill.texture);image->raw=raw;return image;};
 rejected=false;try{layer_api_edit(doc,"layers.set_source",{{"reference",fill_reference},{"channel",0},{"source",replacement.id.toString()}},resources);}catch(const std::invalid_argument &){rejected=true;}
 assert(rejected&&doc.to_json()==state&&doc.set(1).layers.back().fill.texture->pixels==original_pixels);
 int source_reads=0;resources.asset=[&](const QUuid &,bool){++source_reads;return replacement;};
 for(const char *filename:{"canonical.png","canonical.exr","canonical.btsmap"}){
  replacement.filename=filename;rejected=false;
  try{layer_api_edit(doc,"layers.set_source",{{"reference",fill_reference},{"channel",0},{"source",replacement.id.toString()}},resources);}
  catch(const std::invalid_argument &error){rejected=std::string(error.what()).find("Remove the Linear RGB interpretation")!=std::string::npos;}
  assert(rejected&&source_reads==0&&doc.to_json()==state&&doc.set(1).layers.back().fill.texture->pixels==original_pixels);
 }
 auto disabled=doc.set(1).layers.back();disabled.effects.back().enabled=false;rejected=false;
 try{require_ordinary_color_source_replacement(disabled,0);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 require_ordinary_color_source_replacement(disabled,2);
 auto toggled=doc;const auto undo=toggled;const auto layer=toggled.set(1).layers.back().id;
 assert(set_layer_channel(toggled.set(1),layer,0,false));const auto &cleared=toggled.set(1).layers.back();
 assert(!cleared.fill.texture&&!raw_linear_color_source(cleared)&&fill_channel_texture(cleared.fill,2)->pixels==original_pixels&&toggled.set(1).selected_effect.isNull());
 const auto cleared_json=toggled.to_json();auto cleared_open=TextureDocument::from_json(cleared_json,bindings);assert(cleared_open.to_json()==cleared_json);
 toggled=undo;assert(toggled.to_json()==state&&toggled.set(1).layers.back().fill.texture->pixels==original_pixels&&raw_linear_color_source(toggled.set(1).layers.back()));
 layer_api_edit(toggled,"layers.set",{{"reference",fill_reference},{"property","active_channels"},{"value",QJsonArray{2}}},{});assert(!toggled.set(1).layers.back().fill.texture&&!raw_linear_color_source(toggled.set(1).layers.back())&&fill_channel_texture(toggled.set(1).layers.back().fill,2)->pixels==original_pixels);
 auto removed=doc;const auto remove_ref=layer_api_reference(removed,removed.set(1),removed.set(1).layers.back(),"content",removed.set(1).layers.back().effects.back().id);
 layer_api_edit(removed,"layers.remove",{{"reference",remove_ref}},{});assert(!raw_linear_color_source(removed.set(1).layers.back())&&removed.set(1).layers.back().fill.texture->pixels==original_pixels);
 auto independent=doc;LevelsEffect roughness;roughness.channel=2;roughness.gamma=1.75;independent.set(1).layers.back().effects.insert(independent.set(1).layers.back().effects.begin(),roughness);assert(raw_linear_color_source(independent.set(1).layers.back()));
 auto expect_rejected=[](auto work){bool failed=false;try{work();}catch(const std::invalid_argument &){failed=true;}assert(failed);};
 const auto transform=linear_color_transform_json(*set.layers.back().effects.back().linear_color);
 for(const char *field:{"matrix","target","input","raw_linear_source"}){auto invalid=transform;invalid.remove(field);expect_rejected([&]{read_linear_color_transform(invalid);});}
 auto bad=transform;bad["input"]="sRGB";expect_rejected([&]{read_linear_color_transform(bad);});bad=transform;bad["matrix"]=QJsonArray{1,0,0};expect_rejected([&]{read_linear_color_transform(bad);});
 auto invalid=set.layers.back();invalid.fill.alpha_mask[0]=true;expect_rejected([&]{raw_linear_color_source(invalid);});invalid=set.layers.back();invalid.effects.push_back(invalid.effects.back());expect_rejected([&]{raw_linear_color_source(invalid);});
 invalid=set.layers.back();invalid.blending[0]=LayerBlend::Passthrough;expect_rejected([&]{raw_linear_color_source(invalid);});
 expect_rejected([&]{layer_api_edit(doc,"layers.set",{{"reference",fill_reference},{"property","blending"},{"channel",0},{"value",int(LayerBlend::Passthrough)}},{});});assert(doc.to_json()==state);
 invalid=set.layers.back();LevelsEffect paint;paint.kind=ContentEffectKind::Paint;invalid.effects.insert(invalid.effects.begin(),paint);expect_rejected([&]{raw_linear_color_source(invalid);});
 std::cout<<"Ordered raw RGB CPU PASS components="<<checked<<" error="<<maximum<<" reversed_order="<<order_difference<<" reversed_tint="<<tint_difference<<"\n";
}
