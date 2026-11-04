#pragma once
#include "DiagnosticSpatialFilter.hh"
#include "GpuLayerPreview.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <chrono>
#include <thread>
namespace bts::gpu_neighborhood_test {
using namespace blender;
using diagnostic_spatial::Plane;using diagnostic_spatial::Pixel;using diagnostic_spatial::Domain;
using Image=std::unique_ptr<blender::gpu::Texture,decltype(&GPU_texture_free)>;
struct Renderer {GpuLayerPreview preview;std::map<std::array<int,4>,Image> targets;};
inline Plane render(Renderer &renderer,const TextureSet &set,int channel,int tile=1001){
 auto &preview=renderer.preview;
 const int width=set.width(tile),height=set.rows(tile);const std::array key{channel,tile,width,height};auto found=renderer.targets.find(key);if(found==renderer.targets.end())found=renderer.targets.emplace(key,Image(GPU_texture_create_2d("Neighborhood production oracle",width,height,1,blender::gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free)).first;auto &target=found->second;if(!target)throw std::runtime_error("Neighborhood target allocation failed");
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);for(;;){preview.begin_frame();preview.poll_programs();const auto result=preview.update(set,channel,target.get(),tile);if(result==GpuPreviewUpdate::Updated)break;if(result==GpuPreviewUpdate::Unsupported)throw std::runtime_error("Production neighborhood pipeline rejected fixture");if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("Production neighborhood pipeline timed out: "+preview.take_program_error());QCoreApplication::processEvents();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 auto *pixels=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));if(!pixels)throw std::runtime_error("Neighborhood diagnostic readback failed");Plane result{width,height,{}};result.pixels.resize(std::size_t(width)*height);std::copy_n(reinterpret_cast<const Pixel *>(pixels),result.pixels.size(),result.pixels.begin());MEM_delete(pixels);return result;
}
inline Plane prefix(Plane input,Domain domain){for(auto &pixel:input.pixels){if(domain==Domain::Mask)pixel={pixel[0],0,0,1};else if(pixel[3]>0)for(int c=0;c<3;++c)pixel[c]/=pixel[3];}return input;}
inline Plane output(Plane input,Domain domain){for(auto &pixel:input.pixels){if(domain==Domain::Mask){const float value=std::clamp(pixel[0],0.f,1.f);pixel={value,value,value,1};}else for(int c=0;c<3;++c)pixel[c]*=pixel[3];}return input;}
inline double compare(const Plane &actual,const Plane &expected,const char *name,double tolerance=3e-5){
 if(actual.width!=expected.width||actual.height!=expected.height)throw std::runtime_error("Neighborhood plane dimensions differ");double maximum=0;for(std::size_t i=0;i<actual.pixels.size();++i)for(int c=0;c<4;++c){const double delta=std::abs(double(actual.pixels[i][c])-expected.pixels[i][c]);maximum=std::max(maximum,delta);if(!std::isfinite(actual.pixels[i][c])||delta>tolerance)throw std::runtime_error(std::string("Neighborhood ")+name+" mismatch x="+std::to_string(i%actual.width)+" y="+std::to_string(i/actual.width)+" c="+std::to_string(c)+" actual="+std::to_string(actual.pixels[i][c])+" expected="+std::to_string(expected.pixels[i][c]));}return maximum;
}
inline std::vector<float> source(int size,bool scalar,int seed=0){std::vector<float> result(std::size_t(size)*size*4);for(int y=0;y<size;++y)for(int x=0;x<size;++x){const auto at=(std::size_t(y)*size+x)*4;const float alpha=scalar?1.f:(x+y+seed)%7==0?0.f:.25f+.25f*((x/3+y/2+seed)%4),red=.125f+.75f*((x*7+y*3+seed)%17)/16.f;const Pixel value{red,scalar?red:.0625f+.875f*((x+y*11+seed)%19)/18.f,scalar?red:1-red*.75f,alpha};for(int c=0;c<3;++c)result[at+c]=value[c]*alpha;result[at+3]=alpha;}return result;}
inline TextureSet fixture(int width,int height,Domain domain,int author=32){TextureSet set;set.texture_resolution=width;set.texture_height=height;set.size_linked=width==height;set.channels.fill(false);set.channels[domain==Domain::Scalar?2:0]=true;set.precision.fill(ChannelPrecision::RGB32F);set.precision[2]=ChannelPrecision::L32F;set.base_values.base_color={0,0,0,1};set.base_values.roughness=0;auto &layer=set.layers.front();layer.kind=LayerKind::Paint;layer.channels.fill(false);layer.channels[domain==Domain::Scalar?2:0]=true;
 if(domain==Domain::Mask){layer.mask=LayerMask{};layer.mask->background=0;MaskEffect paint;paint.kind=MaskEffectKind::Paint;paint.paint.resolution=author;paint.paint.pixels.assign_channel(author,2,source(author,true));layer.mask->effects={paint};set.selected_mask=true;}else layer.pixels.assign_channel(author,domain==Domain::Scalar?2:0,source(author,domain==Domain::Scalar));return set;}
inline void append(TextureSet &set,const FilterSettings &filter,Domain domain,LayerBlend blend,float opacity){auto &layer=set.layers.front();if(domain==Domain::Mask){MaskEffect effect;effect.kind=MaskEffectKind::Filter;effect.filter=filter;effect.blending=blend;effect.opacity=opacity;layer.mask->effects.push_back(effect);}else{LevelsEffect effect;effect.kind=ContentEffectKind::Filter;effect.channel=domain==Domain::Scalar?2:0;effect.filter=filter;effect.blending=blend;effect.opacity=opacity;layer.effects.push_back(effect);}}
inline int channel(Domain domain){return domain==Domain::Mask?15:domain==Domain::Scalar?2:0;}
inline FilterSettings settings(int resource){FilterSettings filter;filter.resource=resource;filter.radius=resource==7?1.6f:resource==8?2.4f:1.7f;filter.hue=.137f;filter.value_scale=1.35f;return filter;}
inline QJsonObject run(){
 QJsonArray records;double maximum=0;int cases=0;const auto record=[&](const char *name,double error){maximum=std::max(maximum,error);++cases;records.append(QJsonObject{{"case",name},{"maximum_error",error}});};
 // Each baseline is a readback of the production source prefix. Only the
 // neighborhood operation is predicted by the independent CPU reference.
 for(const auto extent:{std::array{32,16},std::array{16,32}})for(auto domain:{Domain::RGB,Domain::Scalar,Domain::Mask})for(int resource=7;resource<=10;++resource)for(const auto mix:{std::pair{LayerBlend::Normal,1.f},std::pair{LayerBlend::Overlay,.37f},std::pair{LayerBlend::Multiply,.63f}}){
  auto set=fixture(extent[0],extent[1],domain);Renderer preview;const auto input=prefix(render(preview,set,channel(domain)),domain);const auto filter=settings(resource);append(set,filter,domain,mix.first,mix.second);const auto expected=output(diagnostic_spatial::filter(input,filter,domain,mix.first,mix.second),domain);record("independent-kernel",compare(render(preview,set,channel(domain)),expected,"kernel"));
 }
 for(auto domain:{Domain::RGB,Domain::Scalar,Domain::Mask}){
  auto set=fixture(32,16,domain);Renderer preview;auto expected=prefix(render(preview,set,channel(domain)),domain);for(int resource:{8,9,7,10}){const auto filter=settings(resource);append(set,filter,domain,LayerBlend::Normal,.73f);expected=diagnostic_spatial::filter(expected,filter,domain,LayerBlend::Normal,.73f);}record("ordered-chain",compare(render(preview,set,channel(domain)),output(expected,domain),"ordered chain"));
 }
 // Nested mask Group children start from zero and resolve/clamp before their
 // parent's blend. The parent background remains independent.
 {auto set=fixture(32,16,Domain::Mask);Renderer preview;auto child=prefix(render(preview,set,15),Domain::Mask);auto effects=set.layers.front().mask->effects;for(int resource:{8,9}){const auto filter=settings(resource);MaskEffect effect;effect.kind=MaskEffectKind::Filter;effect.filter=filter;effect.opacity=.81f;effects.push_back(effect);child=diagnostic_spatial::filter(child,filter,Domain::Mask,LayerBlend::Normal,.81f);}Plane parent=child;for(auto &pixel:parent.pixels)pixel={.6f,0,0,1};MaskEffect inner;inner.kind=MaskEffectKind::Group;inner.opacity=.37f;inner.blending=LayerBlend::Multiply;inner.group_effects=std::make_shared<const std::vector<MaskEffect>>(effects);auto middle=diagnostic_spatial::group(parent,child,LayerBlend::Multiply,.37f);MaskEffect fill;fill.kind=MaskEffectKind::Fill;fill.fill=.6f;MaskEffect outer;outer.kind=MaskEffectKind::Group;outer.opacity=.63f;outer.blending=LayerBlend::Screen;outer.group_effects=std::make_shared<const std::vector<MaskEffect>>(std::vector{fill,inner});set.layers.front().mask->background=.6f;set.layers.front().mask->effects={outer};record("nested-group",compare(render(preview,set,15),output(diagnostic_spatial::group(parent,middle,LayerBlend::Screen,.63f),Domain::Mask),"nested group"));}
 // One set contains differently sized UDIMs. Every spatial sample must clamp
 // within its source tile, regardless of which tile was evaluated previously.
 for(auto domain:{Domain::RGB,Domain::Mask}){auto set=fixture(16,8,domain);set.tiles={1001,1002};set.tile_resolutions[1002]=32;auto &layer=set.layers.front();if(domain==Domain::Mask)layer.mask->effects.front().paint.pixels.assign_channel(32,2,source(32,true,11),1002);else layer.pixels.assign_channel(32,0,source(32,false,11),1002);Renderer preview;std::map<int,Plane> expected;for(int tile:set.tiles)expected.emplace(tile,prefix(render(preview,set,channel(domain),tile),domain));for(int resource:{9,7}){const auto filter=settings(resource);append(set,filter,domain,LayerBlend::Normal,1);for(auto &[tile,plane]:expected)plane=diagnostic_spatial::filter(plane,filter,domain);}for(int tile:{1002,1001})record("different-udim-extents",compare(render(preview,set,channel(domain),tile),output(expected.at(tile),domain),"tile clamp"));}
 // A single edited author page ends at x=255. Neighbor kernels can affect
 // x>=256: cached output must match a fresh production recomputation, then Undo.
 for(auto domain:{Domain::RGB,Domain::Mask})for(int resource=7;resource<=10;++resource){
  auto set=fixture(512,512,domain,512);append(set,settings(resource),domain,LayerBlend::Normal,1);const auto before=set;Renderer cached;const auto baseline=render(cached,set,channel(domain));
  PaintPixels::Stamp ink{{255.5/512,.5},{.49/512,0},{0,.49}};ink.channels={true,false,true,false,false};ink.values.base_color={.95f,.025f,.8f,1};ink.values.roughness=.97f;ink.flow=1;ink.hardness=1;
  auto &layer=set.layers.front();auto &pixels=domain==Domain::Mask?layer.mask->effects.front().paint.pixels:layer.pixels;const int plane=domain==Domain::Mask?2:0;
  const auto versions=[&]{std::map<std::uint32_t,std::uint64_t> result;pixels.tile(1001).visit_page_versions(plane,[&](auto page,auto revision){result[page]=revision;});return result;};const auto old_versions=versions();const auto bounds=pixels.stamp(512,ink);
  if(bounds.empty()||bounds.x0!=255||bounds.x1!=256||versions()==old_versions)throw std::runtime_error("Neighborhood cache fixture did not mutate only the left side of the 256 boundary");
  Renderer fresh;const auto reference=render(fresh,set,channel(domain));double witness=0;int witness_x=-1,witness_y=-1;for(int y=0;y<reference.height;++y)for(int x=256;x<reference.width;++x)for(int c=0;c<3;++c){const auto at=std::size_t(y)*reference.width+x;const double difference=std::abs(double(reference.pixels[at][c])-baseline.pixels[at][c]);if(difference>witness){witness=difference;witness_x=x;witness_y=y;}}
  if(witness<=1e-5)throw std::runtime_error("Neighborhood cache fixture has no changed output witness right of x=256 for filter "+std::to_string(resource));
  records.append(QJsonObject{{"case","boundary-mutation-witness"},{"filter",resource},{"domain",int(domain)},{"stamp_x0",bounds.x0},{"stamp_x1",bounds.x1},{"source_revision_changed",true},{"output_x",witness_x},{"output_y",witness_y},{"output_change",witness}});
  record("cached-edit-at-256",compare(render(cached,set,channel(domain)),reference,"cached edit"));set=before;Renderer undo;record("cached-undo-at-256",compare(render(cached,set,channel(domain)),render(undo,set,channel(domain)),"cached undo"));
 }

 return {{"cases",cases},{"maximum_error",maximum},{"records",records},{"scope","Independent production GPU neighborhood kernel math, ordering, group boundaries and cached edit/undo; whole-material CPU parity is separate"},{"painter_pixel_parity_claimed",false}};
}
}
