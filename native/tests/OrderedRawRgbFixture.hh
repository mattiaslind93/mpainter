#pragma once
#include "TextureDocument.hh"
#include "BakedMapShelf.hh"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <bit>
#include <iostream>

namespace ordered_rgb_test {
using namespace bts;
inline void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
inline constexpr std::array<FillTexture::Pixel,4> raw_pixels{{
 {-.5f,.125f,2.f,3.f},{.25f,.75f,.0625f,-2.f},
 {1.5f,-.625f,.5f,0.f},{.875f,.03125f,1.75f,7.f}}};
inline QByteArray encoded(){
 QByteArray bytes("BTSMAP1",8);const std::uint32_t count=1;const std::int32_t shape[3]{1001,2,2};
 bytes.append(reinterpret_cast<const char *>(&count),4);bytes.append(reinterpret_cast<const char *>(shape),12);
 bytes.append(reinterpret_cast<const char *>(raw_pixels.data()),sizeof(raw_pixels));return bytes;
}
inline std::shared_ptr<const FillTexture> texture(QUuid id=QUuid("f589f62f-f774-49b0-9ab3-d81516da9176")){
 return decode_btsmap(id.toString(QUuid::WithoutBraces),encoded(),"Independent raw RGBA fixture");
}
inline std::shared_ptr<const LinearColorTransform> fixed_matrix(){
 auto transform=std::make_shared<LinearColorTransform>();transform->input=LinearColorInput::Rec709;transform->input_space="Linear Rec.709";
 transform->to_aces={.6130974f,.0701942f,.0206156f,.3395231f,.9163539f,.1095698f,.0473795f,.0134519f,.8698146f};return transform;
}
inline void configure(TextureSet &set,std::shared_ptr<const LinearColorTransform> matrix=fixed_matrix(),
                      std::shared_ptr<const FillTexture> source=texture()){
 set.texture_resolution=128;set.texture_height=128;set.tiles={1001,1002};set.channels={true,false,true,false,false};set.ao_channel=false;
 set.precision.fill(ChannelPrecision::RGB32F);set.precision[2]=ChannelPrecision::L32F;set.base_values.base_color={.07f,.12f,.19f,1};
 set.layers.clear();TextureLayer bottom;bottom.kind=LayerKind::Fill;bottom.name="Ordinary canonical base";bottom.channels={true,false,false,false,false};bottom.ao_enabled=false;bottom.values.base_color={.1875f,.4375f,.6875f,1};set.layers.push_back(bottom);
 TextureLayer top;top.kind=LayerKind::Fill;top.name="Explicit raw linear RGB";top.channels={true,false,true,false,false};top.ao_enabled=false;
 top.values.base_color={.6875f,1.25f,.375f,1};top.opacity[0]=.625f;top.blending[0]=LayerBlend::Multiply;
 top.fill.texture=source;fill_channel_texture(top.fill,2)=source;top.fill.alpha_mask.fill(false);
 LevelsEffect first;first.channel=0;first.input_low=.03125f;first.input_high=.9375f;first.gamma=1.375f;first.output_low=-.125f;first.output_high=1.5f;
 first.rgb_curves[0].gamma=.8125f;first.rgb_curves[1].input_low=-.0625f;first.rgb_curves[1].output_high=1.125f;first.rgb_curves[2].gamma=1.625f;
 LevelsEffect second;second.channel=0;second.input_low=-.0625f;second.input_high=1.25f;second.gamma=.875f;second.output_low=-.03125f;second.output_high=1.375f;
 LevelsEffect convert;convert.kind=ContentEffectKind::LinearColorTransform;convert.channel=0;convert.linear_color=std::move(matrix);
 top.effects={first,second,convert};set.layers.push_back(top);set.selected_layer=top.id;set.selected_effect=convert.id;set.selected_mask=false;
 require(raw_linear_color_source(set.layers.back()),"Raw source boundary was not retained");
}
inline TextureDocument document(){
 SceneSnapshot scene;MaterialSummary material;material.session_id=1;material.name="Ordered RGB";material.uniform_editable=true;scene.materials.push_back(material);
 ImportSummary imported;imported.materials.push_back({1,material.name,{1001,1002}});NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;options.uv_workflow=UvWorkflow::PreserveTiles;
 TextureDocument document(options,imported,scene);configure(document.set(1));return document;
}
// Independent bilinear/repeat sampler over the literal source components. Data
// A is deliberately outside 0..1 and must never enter coverage or RGB filtering.
inline std::array<double,3> sample(int x,int y,int tile){
 std::array<double,3> sum{};
 for(double dx:{.25,.75})for(double dy:{.25,.75}){
  double u=(x+dx)/128+(tile-1001)%10,v=(y+dy)/128+(tile-1001)/10;u-=std::floor(u);v-=std::floor(v);
  const double px=u*2-.5,py=v*2-.5;const int ix=int(std::floor(px)),iy=int(std::floor(py));const double fx=px-ix,fy=py-iy;
  for(int c=0;c<3;++c){double value=0;for(int j=0;j<2;++j)for(int i=0;i<2;++i){const int tx=((ix+i)%2+2)%2,ty=((iy+j)%2+2)%2;value+=raw_pixels[ty*2+tx][c]*(i?fx:1-fx)*(j?fy:1-fy);}sum[c]+=value*.25;}
 }
 return sum;
}
inline double curve(const LevelCurve &curve,double value){
 double normalized=(value-curve.input_low)/(double(curve.input_high)-curve.input_low);
 if(normalized>0)normalized=std::pow(normalized,1/double(curve.gamma));
 return std::clamp(double(curve.output_low)+(double(curve.output_high)-curve.output_low)*normalized,-65504.,65504.);
}
inline std::array<double,3> levels(const TextureLayer &layer,std::array<double,3> value){
 for(const auto &effect:layer.effects){if(effect.kind==ContentEffectKind::LinearColorTransform)break;
  if(effect.enabled&&effect.kind==ContentEffectKind::Levels&&effect.channel==0)for(int c=0;c<3;++c){value[c]=curve(effect.rgb_curves[c],curve(effect,value[c]));if(effect.clamp)value[c]=std::clamp(value[c],0.,1.);}}
 return value;
}
inline double quantize(double value,ChannelPrecision precision){
 if(precision==ChannelPrecision::SRGB8){const double v=std::clamp(value,0.,1.),encoded=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;const double q=std::floor(encoded*255+.5)/255;return q<=.04045?q/12.92:std::pow((q+.055)/1.055,2.4);}
 if(precision==ChannelPrecision::RGB8)return std::floor(std::clamp(value,0.,1.)*255+.5)/255;
 if(precision==ChannelPrecision::RGB16)return std::floor(std::clamp(value,0.,1.)*65535+.5)/65535;
 if(precision==ChannelPrecision::RGB16F){value=std::clamp(value,-65504.,65504.);const double magnitude=std::abs(value);if(magnitude==0)return value;const double step=std::ldexp(1.,magnitude<std::ldexp(1.,-14)?-24:int(std::floor(std::log2(magnitude)))-10);return std::nearbyint(value/step)*step;}
 return float(value);
}
template<class Convert> inline std::array<double,3> expected(const TextureSet &set,int x,int y,int tile,const Convert &convert,bool evaluated=true){
 const auto &top=set.layers.back();auto value=convert(levels(top,sample(x,y,tile)));
 const double opacity=top.opacity[0]*(top.mask&&top.mask->enabled?top.mask->background:1);
 for(int c=0;c<3;++c){value[c]*=top.values.base_color[c];double below=set.layers.front().values.base_color[c];if(evaluated)below=quantize(below,set.precision[0]);
  const double blended=top.blending[0]==LayerBlend::Multiply?below*value[c]:value[c];value[c]=below*(1-opacity)+blended*opacity;}
 return value;
}
inline std::array<double,3> matrix(const LinearColorTransform &transform,std::array<double,3> value){
 std::array<double,3> result{};for(int r=0;r<3;++r)for(int c=0;c<3;++c)result[r]+=transform.to_aces[c*3+r]*value[c];return result;
}
}
