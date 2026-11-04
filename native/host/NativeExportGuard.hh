#pragma once
#include "TextureDocument.hh"
#include "SpatialOracleScope.hh"
#include "ResourceStack.hh"
#include "NativeMaterialParameters.hh"
#include <functional>
#include <map>
#include <set>
namespace bts {
// Metadata reachability only. Never evaluates a native shader on the CPU.
inline bool texture_export_needs_native_capture(const TextureSet &set,int tile)
{
 if(spatial_oracle_unavailable(set))return true; // CPU export cannot silently omit neighborhood effects.
 const auto &layers=evaluation_layers(set);
 if(std::none_of(layers.begin(),layers.end(),[](const auto &l){return bool(l.fill.native_material);}))return false;
 using Channels=std::array<bool,15>;
 const auto channel_on=[](const TextureLayer &l,int c){return c<5?l.channels[c]:c==5?l.ao_enabled:c==14?l.displacement.enabled:l.users[c-6].enabled;};
 const auto blend=[](const TextureLayer &l,int c){return c<5?l.blending[c]:c==5?l.ao_blending:c==14?l.displacement.blending:l.users[c-6].blending;};
 const auto opacity=[](const TextureLayer &l,int c){return c<5?l.opacity[c]:c==5?l.ao_opacity:c==14?l.displacement.opacity:l.users[c-6].opacity;};
 const auto included=[&](const TextureLayer &l){return l.geometry.type==1?l.geometry.includes_tile(tile):l.geometry.default_include||!l.geometry.meshes.empty();};
 Channels available{};for(int c=0;c<15;++c)available[c]=c<5?set.channels[c]:c==5?set.ao_channel:c==14?set.displacement.channel:set.users[c-6].present;
 require_texture_ram(resource_product(layers.size(),sizeof(Channels)+sizeof(QUuid)+8*sizeof(void *)),"Native export dependency metadata");
 std::map<QUuid,Channels> active;
 // Parents follow children in the validated postorder, including expanded instances.
 for(auto i=layers.rbegin();i!=layers.rend();++i){auto on=available;if(!i->parent.isNull()){const auto p=active.find(i->parent);if(p==active.end())throw std::invalid_argument("Invalid export layer hierarchy");on=p->second;}
  const bool masked_out=i->mask&&i->mask->enabled&&i->mask->background==0&&i->mask->pixels.empty()&&i->mask->effects.empty();
  for(int c=0;c<15;++c)on[c]=on[c]&&i->visible&&included(*i)&&!masked_out&&channel_on(*i,c)&&blend(*i,c)!=LayerBlend::Disabled&&opacity(*i,c)>0;
  active.emplace(i->id,on);
  // An enabled but unavailable/legacy native source is not safe to replace with
  // ordinary default values either. Only wholly inactive native rows pass.
  if(i->fill.native_material&&std::any_of(on.begin(),on.end(),[](bool v){return v;}))return true;
 }
 struct Point{const TextureLayer *layer=nullptr;bool mask=false;bool enabled=false;};
 std::uint64_t point_count=0;for(const auto &l:layers){for(const auto &e:l.effects)point_count=resource_sum(point_count,e.kind==ContentEffectKind::Anchor);if(l.mask)for(const auto &e:l.mask->effects)point_count=resource_sum(point_count,e.kind==MaskEffectKind::Anchor);}
 require_texture_ram(resource_product(point_count,sizeof(Point)+sizeof(QUuid)+8*sizeof(void *)+15*(sizeof(QUuid)+sizeof(int)+sizeof(bool)+8*sizeof(void *))),"Native export anchor dependency metadata");
 std::map<QUuid,Point> points;
 for(const auto &l:layers){for(const auto &e:l.effects)if(e.kind==ContentEffectKind::Anchor)points[e.id]={&l,false,e.enabled};if(l.mask)for(const auto &e:l.mask->effects)if(e.kind==MaskEffectKind::Anchor)points[e.id]={&l,true,e.enabled};}
 std::set<std::pair<QUuid,int>> visiting;std::map<std::pair<QUuid,int>,bool> complete;
 std::function<bool(const AnchorReference &)> reference;
 std::function<bool(const TextureLayer &,int,bool,bool)> dependencies;
 const auto generator=[&](const MeshGeneratorSettings &g){for(const auto &ref:g.input_anchors)if(reference(ref))return true;return false;};
 reference=[&](const AnchorReference &ref){if(ref.source.isNull())return false;const auto p=points.find(ref.source);if(p==points.end()||!p->second.enabled)return false;const auto key=std::pair{ref.source,ref.channel};if(const auto cached=complete.find(key);cached!=complete.end())return cached->second;if(!visiting.insert(key).second)return true;require_resource_stack(sizeof(TextureLayer)*4,"Native export anchor traversal");const bool native=dependencies(*p->second.layer,ref.channel,p->second.mask,true);visiting.erase(key);complete[key]=native;return native;};
 dependencies=[&](const TextureLayer &l,int c,bool mask,bool raw){
  if(!mask){
   // Raw content anchors ignore the producer's visibility, opacity and channel
   // switches. Folder anchors include their isolated subtree, not its ancestors.
   if(raw&&l.fill.native_material)return true;
   if(raw&&l.kind==LayerKind::Folder){
    const auto end=std::find_if(layers.begin(),layers.end(),[&](const auto &v){return v.id==l.id;});auto first=end;while(first!=layers.begin()&&(first-1)->depth>l.depth)--first;
    std::map<QUuid,bool> branch{{l.id,true}};
    for(auto i=end;i!=first;){--i;const auto parent=branch.find(i->parent);if(parent==branch.end())throw std::invalid_argument("Invalid raw export anchor hierarchy");const bool on=parent->second&&i->visible&&included(*i)&&channel_on(*i,c)&&blend(*i,c)!=LayerBlend::Disabled&&opacity(*i,c)>0;branch[i->id]=on;if(on&&(i->fill.native_material||dependencies(*i,c,false,false)))return true;}
   }
   if(c>=0&&c<15&&reference(l.fill.anchors[c]))return true;
   for(const auto &e:l.effects)if(e.enabled&&e.kind==ContentEffectKind::Generator&&e.channel==c&&e.opacity>0&&e.blending!=LayerBlend::Disabled&&generator(e))return true;
  }
  if(l.mask&&(mask||(!raw&&l.mask->enabled)))for(const auto &e:l.mask->effects)if(e.enabled){
   if(e.kind==MaskEffectKind::Fill&&e.opacity>0&&e.blending!=LayerBlend::Disabled&&reference({e.anchor_source,e.anchor_channel,e.anchor_component,e.anchor_coverage}))return true;
   if(e.kind==MaskEffectKind::Generator&&e.opacity>0&&e.blending!=LayerBlend::Disabled&&generator(e))return true;
   // Compare's raw layer/below inputs can bypass a producer folder's visibility.
   // Retain rejection rather than guess their native value in a mask anchor.
   if(mask&&e.kind==MaskEffectKind::Compare&&(e.left<2||e.right<2))for(const auto &p:layers)if(p.fill.native_material)return true;
  }
  return false;
 };
 for(const auto &l:layers){const auto &on=active.at(l.id);for(int c=0;c<15;++c)if(on[c]&&dependencies(l,c,false,false))return true;}
 return false;
}
}
