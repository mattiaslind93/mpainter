#include "LayerSourceChannels.hh"
#include "NativeMaterialParameters.hh"
#include "ResourceBudget.hh"
#include "FlattenLayer.hh"
#include "AnchorEvaluationScope.hh"
#include "TextureMemory.hh"
#include "FlattenMaskRaster.hh"
#include "FolderPrefix.hh"
#include <algorithm>
namespace bts {
TextureSet precompose_capture_source(const TextureSet &source,int below){
 const auto &layers=evaluation_layers(source);
 if(below<1||below>int(layers.size())||layers[below-1].depth!=0)throw std::invalid_argument("Pre-compose requires a top-level insertion above at least one layer");
 TextureSet result=source;result.instance_layers.reset();result.instance_hierarchies.clear();result.layers.assign(layers.begin(),layers.begin()+below);
 const int frozen=precompose_start(result.layers);for(int i=0;i<frozen;++i)result.layers[i].visible=false;
 TextureLayer group;group.kind=LayerKind::Folder;group.name="Pre-compose";group.channels.fill(true);group.ao_enabled=true;group.displacement.enabled=true;group.displacement.blending=LayerBlend::Normal;group.blending.fill(LayerBlend::Passthrough);group.ao_blending=LayerBlend::Passthrough;group.displacement.blending=LayerBlend::Passthrough;
 for(auto &u:group.users){u.enabled=true;u.blending=LayerBlend::Passthrough;}
 for(auto &l:result.layers){if(l.depth==0)l.parent=group.id;++l.depth;}
 result.selected_layer=group.id;result.selected_mask=false;result.selected_geometry=false;result.selected_effect={};result.layers.push_back(std::move(group));
 TextureDocument::validate_hierarchy(result);return result;
}
void install_precompose_layer(TextureSet &set,TextureLayer cache,int index,bool update){
 if(index<1||index>int(set.layers.size())||set.layers[index-1].depth!=0)throw std::invalid_argument("Pre-compose insertion changed");
 cache.name="Pre-compose";cache.precompose_below=true;cache.kind=LayerKind::Fill;cache.depth=0;cache.parent={};cache.mask.reset();cache.visible=true;cache.fill.mode=6;cache.fill.filtering=2;cache.fill.bump_bias=-set.bump_offset();cache.opacity.fill(1);cache.blending.fill(LayerBlend::Normal);cache.ao_opacity=1;cache.ao_blending=LayerBlend::Normal;
 for(auto &u:cache.users){u.opacity=1;u.blending=LayerBlend::Normal;}cache.displacement.opacity=1;cache.displacement.blending=LayerBlend::Normal;
 if(update){if(index==int(set.layers.size())||!set.layers[index].precompose_below)throw std::invalid_argument("Pre-compose cache no longer exists");cache.id=set.layers[index].id;set.layers[index]=std::move(cache);}
 else set.layers.insert(set.layers.begin()+index,std::move(cache));
 set.selected_layer=set.layers[index].id;set.selected_mask=false;set.selected_geometry=false;set.selected_effect={};set.instance_layers.reset();TextureDocument::validate_hierarchy(set);
}
std::uint64_t flatten_safe_ram_bytes(){const auto available=texture_available_ram_bytes();return available/2;}
FlattenPlan prepare_flatten_layer(const TextureSet &source,const QUuid &id){
 const auto &layers=evaluation_layers(source);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==id;});
 if(found==layers.end()||!found->visible)throw std::invalid_argument("Select a visible layer or group to flatten");
 int first=int(found-layers.begin());if(found->kind==LayerKind::Folder)while(first>0&&layers[first-1].depth>found->depth)--first;
 bool content=false;for(auto i=layers.begin()+first;i!=found+1;++i){if(i->instance)throw std::invalid_argument("Flatten an instance at its source");content|=i->visible&&(i->kind==LayerKind::Fill||!i->pixels.empty()||!i->ao_pixels.empty()||!i->displacement.pixels.empty()||!i->effects.empty()||std::any_of(i->users.begin(),i->users.end(),[](const auto &u){return !u.pixels.empty();}));}
 if(!content)throw std::invalid_argument("No visible texture data in the selected layer/group");
 std::array<bool,15> contextual{};
 for(int c=0;c<15;++c){const auto mode=c<5?found->blending[c]:c==5?found->ao_blending:c==14?found->displacement.blending:found->users[c-6].blending;contextual[c]=mode==LayerBlend::Passthrough&&(found->kind==LayerKind::Folder||std::any_of(found->effects.begin(),found->effects.end(),[&](const auto &e){return e.enabled&&(e.kind==ContentEffectKind::Paint||e.channel==c);}));}
 FlattenedLayer result;result.source=id;result.layer=*found;auto &flat=result.layer;flat.id=QUuid::createUuid();flat.name+=" flattened";flat.kind=LayerKind::Fill;flat.instance.reset();flat.effects.clear();flat.fill={};flat.fill.alpha_mask.fill(true);flat.fill.mode=6;flat.fill.filtering=2;flat.values.base_color={1,1,1,1};flat.pixels={};flat.ao_pixels={};flat.strokes.reset();flat.author_resolution=0;
 for(auto &user:flat.users){user.pixels={};user.strokes.reset();user.author_resolution=0;}flat.displacement.pixels={};flat.displacement.strokes.reset();flat.displacement.author_resolution=0;
 for(int c=0;c<15;++c)if(contextual[c]){if(c<5){flat.opacity[c]=1;flat.blending[c]=LayerBlend::Replace;}else if(c==5){flat.ao_opacity=1;flat.ao_blending=LayerBlend::Replace;}else{auto &channel=c==14?flat.displacement:flat.users[c-6];channel.opacity=1;channel.blending=LayerBlend::Replace;}}
 if(flat.mask){flat.mask->effects.clear();flat.mask->pixels={};flat.mask->strokes.reset();flat.mask->background=0;flat.mask->author_resolution=source.texture_resolution;}
 std::array<bool,15> enabled{};for(int c=0;c<5;++c)enabled[c]=source.channels[c];enabled[5]=source.ao_channel;for(int c=0;c<8;++c)enabled[c+6]=source.users[c].present;enabled[14]=source.displacement.channel;
 // An isolated native source owns only its prepared channels. Do not turn an
 // unavailable Bump/Normal path into a raw-source query. Groups still capture
 // their combined channels, including ordinary children's internal normals.
 if(found->kind==LayerKind::Fill&&found->fill.native_material){
  const auto &channels=found->fill.native_material->channels;
  if(channels.empty())throw std::invalid_argument("Native layer capture requires prepared channel capabilities");
  for(int c=0;c<15;++c)if(std::find(channels.begin(),channels.end(),c)==channels.end()){
   if(enabled[c])std::printf("BTS_NATIVE_CAPTURE_CHANNEL_OMITTED channel=%d reason=unavailable_native_source\n",c);
   enabled[c]=false;
   if(c<5)flat.channels[c]=false;else if(c==5)flat.ao_enabled=false;else if(c==14)flat.displacement.enabled=false;else flat.users[c-6].enabled=false;
  }
 }

 int mask_canvas=0;for(int tile:source.tiles)mask_canvas=std::max({mask_canvas,source.width(tile),source.rows(tile)});if(flat.mask)flat.mask->author_resolution=mask_canvas;
 std::uint64_t canvas_pixels=0;for(int tile:source.tiles){const auto extent=std::uint64_t(std::max(source.width(tile),source.rows(tile)));canvas_pixels+=extent*extent;}
 int mask_count=0;for(int i=first;i<int(found-layers.begin());++i)mask_count+=layers[i].mask.has_value();
 if(std::any_of(contextual.begin(),contextual.end(),[](bool value){return value;})){int begin=first;while(begin>0&&layers[begin-1].depth>=found->depth)--begin;for(int i=begin;i<int(found-layers.begin());++i)mask_count+=layers[i].mask.has_value();}
 const auto own_mask_pixels=flat.mask?std::uint64_t(mask_canvas)*mask_canvas*source.tiles.size():0;
 const auto bytes=16*(source.evaluation_texels()*std::count(enabled.begin(),enabled.end(),true)+canvas_pixels*mask_count+own_mask_pixels);
 return {std::move(result),enabled,contextual,mask_canvas,bytes};
}

std::optional<FlattenedLayer> flatten_layer_pixels(const TextureSet &source,const QUuid &id,const std::atomic_bool &cancel){
 AnchorEvaluationScope anchor_sources(source);
 const auto &layers=evaluation_layers(source);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==id;});
 if(found==layers.end())throw std::invalid_argument("Selected layer is missing");
 if(!found->visible)throw std::invalid_argument("Cannot flatten a hidden layer");
 int last=int(found-layers.begin()),first=last;
 if(found->kind==LayerKind::Folder)while(first>0&&layers[first-1].depth>found->depth)--first;
 TextureSet content=source;content.instance_layers.reset();content.instance_hierarchies.clear();content.layers.assign(layers.begin()+first,layers.begin()+last+1);
 const bool has_content=std::any_of(content.layers.begin(),content.layers.end(),[](const auto &layer){return layer.visible&&(layer.kind==LayerKind::Fill||!layer.pixels.empty()||!layer.ao_pixels.empty()||!layer.displacement.pixels.empty()||!layer.effects.empty()||std::any_of(layer.users.begin(),layer.users.end(),[](const auto &user){return !user.pixels.empty();}));});
 if(!has_content)throw std::invalid_argument("No visible texture data in the selected layer/group");
 const int depth=found->depth;for(auto &layer:content.layers){if(layer.instance)throw std::invalid_argument("Flatten an instance at its source");layer.depth-=depth;if(layer.id==id)layer.parent={};}
 auto &root=content.layers.back();root.opacity.fill(1);root.blending.fill(LayerBlend::Normal);root.channels=layer_source_channels(root.channels,root.kind==LayerKind::Fill&&bool(root.fill.native_material));root.ao_opacity=1;root.ao_blending=LayerBlend::Normal;root.ao_enabled=true;root.mask.reset();
 for(auto &user:root.users){user.opacity=1;user.blending=LayerBlend::Normal;user.enabled=true;}root.displacement.opacity=1;root.displacement.blending=LayerBlend::Normal;root.displacement.enabled=true;
 std::array<bool,15> contextual{};bool needs_context=false;
 for(int c=0;c<15;++c){const auto mode=c<5?found->blending[c]:c==5?found->ao_blending:c==14?found->displacement.blending:found->users[c-6].blending;contextual[c]=mode==LayerBlend::Passthrough&&(found->kind==LayerKind::Folder||std::any_of(found->effects.begin(),found->effects.end(),[&](const auto &effect){return effect.enabled&&(effect.kind==ContentEffectKind::Paint||effect.channel==c);}));needs_context|=contextual[c];}
 std::optional<TextureSet> backdrop;
 if(needs_context){backdrop=source;backdrop->instance_layers.reset();backdrop->layers=layers;backdrop->layers.at(last).mask.reset();}
 auto plan=prepare_flatten_layer(source,id);auto result=std::move(plan.result);auto &flat=result.layer;const auto &enabled=plan.enabled;
 const int mask_canvas=plan.mask_canvas;const auto budget=flatten_safe_ram_bytes();
 if(plan.snapshot_bytes>budget)throw std::length_error("Flatten snapshot requires "+std::to_string((plan.snapshot_bytes+1048575)/1048576)+" MiB, safe available RAM budget "+std::to_string((budget+1048575)/1048576)+" MiB");
 std::printf("BTS_GPU_CPU_REFERENCE reason=flatten-export-oracle required_bytes=%llu budget_bytes=%llu\n",(unsigned long long)plan.snapshot_bytes,(unsigned long long)budget);
 bool nonempty=false,mask_background_chosen=false;
 for(int tile:source.tiles){
  if(cancel)return {};
  if(found->geometry.type==1&&!found->geometry.includes_tile(tile))continue;
  const int width=source.width(tile),height=source.rows(tile);const auto count=std::size_t(width)*height;
  // Resolve masks in the original stack so external anchors and Compare inputs
  // retain their exact context. Only the group's own mask stays separate.
  const auto snapshot_masks=[&](TextureSet &target){for(auto &layer:target.layers)if(layer.id!=id&&layer.mask){
   const int canvas=std::max(width,height);std::vector<float> pixels(std::size_t(canvas)*canvas*4);for(int y=0;y<canvas;++y){if(cancel)return false;for(int x=0;x<canvas;++x){const auto offset=(std::size_t(y)*canvas+x)*4;pixels[offset]=TextureDocument::mask_value(source,layer.id,x*width/canvas,y*height/canvas,tile);pixels[offset+3]=1;}}
   const bool on=layer.mask->enabled;layer.mask=LayerMask{};layer.mask->enabled=on;layer.mask->background=0;layer.mask->pixels.assign_channel(canvas,2,pixels,tile);
  }return true;};
  if(!snapshot_masks(content)||(backdrop&&!snapshot_masks(*backdrop)))return {};
  std::vector<float> mask(flat.mask?std::size_t(mask_canvas)*mask_canvas*4:0);
  for(int c=0;c<15;++c)if(enabled[c])result.images[c][tile]={width,height,std::make_shared<std::vector<FillTexture::Pixel>>(count)};
  for(int y=0;y<height;++y){if(cancel)return {};for(int x=0;x<width;++x){
   const auto index=std::size_t(y)*width+x;const auto isolated=TextureDocument::evaluate(content,x,y,tile);const auto context=backdrop?folder_prefix_composite(*backdrop,id,x,y,tile):UniformComposite{};
   for(int c=0;c<15;++c)if(enabled[c]){
    const auto &value=contextual[c]?context:isolated;const auto &v=value.authored;FillTexture::Pixel p{};
    if(c==0)p=v.base_color;
    else if(c==3)p={v.normal[0]*.5f+.5f,v.normal[1]*.5f+.5f,v.normal[2]*.5f+.5f,1};
    else if(c<6){const float scalar=c==1?v.metallic:c==2?v.roughness:c==4?v.bump:v.ambient_occlusion;p={scalar,scalar,scalar,1};}
    if(c<6)p[3]=c==5?value.ao_coverage:value.coverage[c];
    else{const auto &input=contextual[c]?*backdrop:content;const auto scalar=contextual[c]?(c==14?folder_prefix_displacement(input,id,x,y,tile):folder_prefix_user(input,id,c-6,x,y,tile)):(c==14?TextureDocument::composite_displacement(input,x,y,tile):TextureDocument::composite_user(input,c-6,x,y,tile));p={scalar[0],scalar[0],scalar[0],scalar[1]};}
    bool on=c<5?flat.channels[c]&&flat.blending[c]!=LayerBlend::Disabled:c==5?flat.ao_enabled&&flat.ao_blending!=LayerBlend::Disabled:c==14?flat.displacement.enabled&&flat.displacement.blending!=LayerBlend::Disabled:flat.users[c-6].enabled&&flat.users[c-6].blending!=LayerBlend::Disabled;
    nonempty|=on&&p[3]>0;(*result.images[c].at(tile).pixels)[index]=p;
   }

  }}
  if(flat.mask){for(int y=0;y<mask_canvas;++y){if(cancel)return {};for(int x=0;x<mask_canvas;++x){const auto index=(std::size_t(y)*mask_canvas+x)*4;mask[index]=TextureDocument::mask_value(source,id,x*width/mask_canvas,y*height/mask_canvas,tile);mask[index+3]=1;}}assign_flattened_mask(*flat.mask,mask_canvas,tile,mask,mask_background_chosen);}
 }
 if(!nonempty)throw std::invalid_argument("No visible texture data in the selected layer/group");
 return result;
}
void install_flattened_layer(TextureSet &set,TextureLayer layer,const QUuid &source){
 const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &value){return value.id==source;});
 if(found==set.layers.end())throw std::invalid_argument("Flatten source no longer exists");
 const auto index=found-set.layers.begin();found->visible=false;set.selected_layer=layer.id;set.selected_mask=false;set.selected_geometry=false;set.selected_effect={};set.layers.insert(set.layers.begin()+index+1,std::move(layer));TextureDocument::validate_hierarchy(set);
}
}
