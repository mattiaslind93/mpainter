#include "ResourceStack.hh"
#include "TextureExtent.hh"
#include <QVarLengthArray>
#include "ResourceAvailability.hh"
#include "AnchorEvaluationScope.hh"
#include "NativeMaterialFill.hh"
#include "PaintRecording.hh"
#include "SpatialProjection.hh"
#include "SkewJSON.hh"
#include "BakeQuality.hh"
#include "RibbonIO.hh"
#include "FillTextureJSON.hh"
#include "EvaluationScope.hh"
#include "StrokeArchive.hh"
#include "ExportConfiguration.hh"
#include "TextureDocument.hh"
#include "ReferenceImage.hh"
#include <mutex>
#include "FolderEntryPlan.hh"
#include "FolderPrefix.hh"
#include "MaskEffectPath.hh"
#include "TextureMemory.hh"
#include "MaterialLayeringStacks.hh"
#include "MaterialLayeringIO.hh"
#include "FillSymmetryJSON.hh"
#include "WarpIO.hh"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <functional>

namespace {
std::mutex &reference_image_mutex() {
  static std::mutex m;
  return m;
}
std::map<QUuid, bts::ReferenceImageState> &reference_image_store() {
  static std::map<QUuid, bts::ReferenceImageState> store;
  return store;
}
}  // namespace

namespace bts {
ReferenceImageState document_reference_image(const TextureDocument &document) {
  std::lock_guard<std::mutex> lock(reference_image_mutex());
  const auto it = reference_image_store().find(document.id());
  return it != reference_image_store().end() ? it->second : ReferenceImageState{};
}
void set_document_reference_image(const TextureDocument &document, ReferenceImageState state) {
  std::lock_guard<std::mutex> lock(reference_image_mutex());
  reference_image_store()[document.id()] = std::move(state);
}
}  // namespace bts

namespace bts {
namespace {int next_layer_number(int &value){if(value==std::numeric_limits<int>::max())throw std::overflow_error("Layer name counter exceeds signed-int representation");return value++;}}
TextureDocument::TextureDocument(const NewProjectOptions &settings, const ImportSummary &import, const SceneSnapshot &snapshot)
    : settings_(settings)
{
  require_texture_resolution_limit(int(settings.resolution),"Project");
  settings_.source_units=import.source_units;validate_units(settings_.units,settings_.source_units);
  for (const auto &material : import.materials) {
    TextureSet set;set.channels[3]=false;set.skew.resolution=int(settings.resolution);set.texture_resolution=int(settings.resolution);set.normal_format=settings.normal_map_format;set.tangent_per_fragment=settings.tangent_per_fragment;
    set.cm_per_native=centimeters_per_native_unit(settings_.units,settings_.source_units);set.native_per_uv=native_length_per_uv(snapshot,material.session_uid);
    set.material_session_uid = material.session_uid;
    set.name = material.name;
    if(settings.uv_workflow==UvWorkflow::PreserveTiles)set.tiles=material.tiles;
    const auto native = std::find_if(snapshot.materials.begin(), snapshot.materials.end(),
        [&](const auto &entry) { return entry.session_id == material.session_uid; });
    if (native == snapshot.materials.end()) throw std::runtime_error("Imported Texture Set is missing from the native scene");
    set.base_values = native->values;
    set.uniform_editable = native->uniform_editable;refresh_geometry_names(set,snapshot);
    sets_.push_back(std::move(set));
  }
  set_shader_instances(ShaderInstances::defaults(id_,shader_set_ids()));
}
void TextureDocument::set_shader_instances(ShaderInstances value){value.validate_sets(shader_set_ids());shader_instances_=std::move(value);for(auto &set:sets_)set.shader_parameters=shader_instances_.for_set(set.id).parameters;}
void TextureDocument::preserve_shader_instances(ShaderInstances value){value.reconcile(shader_set_ids(),&shader_instances_);set_shader_instances(std::move(value));}
void TextureDocument::refresh_geometry_names(const TextureSet &set,const SceneSnapshot &snapshot){set.geometry_names.clear();const auto found=std::find_if(snapshot.materials.begin(),snapshot.materials.end(),[&](const auto &material){return material.session_id==set.material_session_uid;});if(found==snapshot.materials.end())return;const auto index=std::size_t(found-snapshot.materials.begin());for(const auto &triangle:snapshot.uv_triangles)if(triangle.material==index){const auto uid=std::uint32_t(triangle.triangle_id>>32);if(const auto name=snapshot.object_names.find(uid);name!=snapshot.object_names.end())set.geometry_names[uid]=name->second;}}
bool TextureDocument::geometry_includes(const TextureSet &set,const TextureLayer &layer,int x,int y,int tile){const auto &mask=layer.geometry;if(mask.type==1)return mask.includes_tile(tile);if(mask.meshes.empty())return mask.default_include;if(x<0||y<0)return mask.default_include;const auto found=set.geometry_ownership.find(tile);if(found==set.geometry_ownership.end()||!found->second||found->second->width!=set.width(tile)||found->second->height!=set.rows(tile))throw std::logic_error("Geometry mask requires the current native UV ownership atlas");const auto &atlas=*found->second;if(x>=atlas.width||y>=atlas.height)return false;const auto uid=atlas.owners.at(std::size_t(y)*atlas.width+x);const auto name=set.geometry_names.find(uid);return uid&&name!=set.geometry_names.end()&&mask.includes(name->second);}
void TextureDocument::set_geometry_mesh(TextureSet &set,const QUuid &id,const std::string &name,bool include){if(std::none_of(set.geometry_names.begin(),set.geometry_names.end(),[&](const auto &entry){return entry.second==name;}))throw std::invalid_argument("Mesh name is not in this Texture Set");auto &target=layer(set,id);if(target.instance)throw std::invalid_argument("Edit instance geometry at its source");auto &mask=target.geometry;mask.set(name,include);}
void TextureDocument::set_geometry_all(TextureSet &set,const QUuid &id,bool include){auto &target=layer(set,id);if(target.instance)throw std::invalid_argument("Edit instance geometry at its source");auto &mask=target.geometry;if(mask.type==1){mask.tiles_default_include=include;mask.tiles.clear();}else{mask.default_include=include;mask.meshes.clear();}}
void TextureDocument::set_units(const UnitSettings &units){validate_units(units,settings_.source_units);if(!units.internal&&!settings_.source_units.exact)throw std::invalid_argument("Reimport the mesh before overriding source units in this legacy project");settings_.units=units;for(auto &set:sets_)set.cm_per_native=centimeters_per_native_unit(units,settings_.source_units);}
void TextureDocument::set_source_units(const SourceUnits &units){validate_units(settings_.units,units);settings_.source_units=units;for(auto &set:sets_)set.cm_per_native=centimeters_per_native_unit(settings_.units,units);}
TextureSet &TextureDocument::set(std::uint32_t uid)
{
  for (auto &set : sets_) if (set.material_session_uid == uid) return set;
  throw std::runtime_error("Texture Set is not in this document");
}
namespace {
std::array<float,4> components(const MaterialValues &v, int channel)
{
  switch (channel) {
    case 0: return v.base_color;
    case 1: return {v.metallic,0,0,0};
    case 2: return {v.roughness,0,0,0};
    case 3: return {v.normal[0],v.normal[1],v.normal[2],0};
    case 4: return {v.bump,0,0,0};
    default: return {v.ambient_occlusion,0,0,0};
  }
}
void set_components(MaterialValues &v, int channel, const std::array<float,4> &c)
{
  switch (channel) {
    case 0: v.base_color=c; break;
    case 1: v.metallic=c[0]; break;
    case 2: v.roughness=c[0]; break;
    case 3: {
      const float length=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
      v.normal=length>1e-8f ? std::array<float,3>{c[0]/length,c[1]/length,c[2]/length} : std::array<float,3>{0,0,1};
      break;
    }
    case 4: v.bump=c[0]; break;
    case 5: v.ambient_occlusion=c[0]; break;
  }
}
QJsonObject values_json(const MaterialValues &v)
{
  return {{"color",QJsonArray{v.base_color[0],v.base_color[1],v.base_color[2],v.base_color[3]}},
          {"metallic",v.metallic},{"roughness",v.roughness},
          {"normal",QJsonArray{v.normal[0],v.normal[1],v.normal[2]}},{"bump",v.bump},{"ao",v.ambient_occlusion}};
}
}
TextureLayer &TextureDocument::layer(TextureSet &set, const QUuid &id)
{
  for (auto &layer : set.layers) if (layer.id == id) return layer;
  throw std::runtime_error("Layer is not in this Texture Set");
}
const MaterialValues &TextureDocument::property_values(const TextureSet &set)
{
  for (const auto &layer : set.layers)
    if (layer.id == set.selected_layer && layer.kind == LayerKind::Fill) return layer.values;
  return set.base_values;
}
MaterialValues &TextureDocument::edit_values(TextureSet &set)
{
  auto &selected = layer(set, set.selected_layer);
  if(selected.instance)throw std::invalid_argument("Edit instance content at its source");
  return selected.kind == LayerKind::Fill ? selected.values : set.base_values;
}
void TextureDocument::add_fill(TextureSet &set)
{
  const auto selected = std::find_if(set.layers.begin(), set.layers.end(),
      [&](const auto &layer) { return layer.id == set.selected_layer; });
  if (selected == set.layers.end()) throw std::runtime_error("Selected layer is missing");
  // A new fill stays off the height stack until its Displacement channel is enabled.
  // The neutral value is the scene midlevel, so enabling it does not jump the mesh.
  TextureLayer fill;fill.material_id=next_material_id(set.layers);fill.values.bump=set.bump_origin;fill.displacement.enabled=false;fill.displacement.value=set.displacement.midlevel;
  for(auto &user:fill.users)user.enabled=false;fill.kind = LayerKind::Fill;fill.parent=selected->parent;fill.depth=selected->depth;
  fill.name = "Fill layer " + std::to_string(next_layer_number(set.next_fill_number));
  prepare_material_layer_insert(set,fill);
  set.selected_layer = fill.id;set.selected_geometry=false;set.selected_mask=false;set.selected_effect=QUuid();
  set.layers.insert(fill.parent==selected->id?selected:selected+1, std::move(fill));
}
namespace {
int layer_index(const TextureSet &set,const QUuid &id) {
  for(int i=0;i<int(set.layers.size());++i)if(set.layers[i].id==id)return i;
  throw std::invalid_argument("Layer is missing");
}
int subtree_start(const TextureSet &set,int end) {
  int start=end;while(start>0&&set.layers[start-1].depth>set.layers[end].depth)--start;return start;
}
}
void TextureDocument::validate_hierarchy(TextureSet &set)
{
  // Derive depth from persistent parent IDs, then prove each subtree is contiguous.
  for(int i=int(set.layers.size())-1;i>=0;--i) {
    auto &entry=set.layers[i];entry.depth=0;
    if(!entry.parent.isNull()) {
      const int parent=layer_index(set,entry.parent);
      if(parent<=i||set.layers[parent].kind!=LayerKind::Folder)throw std::invalid_argument("Invalid layer parent or cyclic hierarchy");
      entry.depth=set.layers[parent].depth+1;

    }
  }
  std::vector<QUuid> parents;
  for(auto it=set.layers.rbegin();it!=set.layers.rend();++it) {
    if(it->depth&&(it->depth>int(parents.size())||parents[it->depth-1]!=it->parent))throw std::invalid_argument("Folder children are not contiguous");
    parents.resize(it->depth+1);parents[it->depth]=it->id;
  }
}
void TextureDocument::move_layer_relative(TextureSet &set,const QUuid &id,const QUuid &target,bool above)
{
  if(id==target)return;
  const int source=layer_index(set,id),destination=layer_index(set,target),start=subtree_start(set,source);
  if(destination>=start&&destination<=source)throw std::invalid_argument("Cannot move a folder into its own subtree");
  auto candidate=set;auto &layers=candidate.layers;
  const auto parent=layers[destination].parent;const int delta=layers[destination].depth-layers[source].depth;
  std::vector<TextureLayer> block(layers.begin()+start,layers.begin()+source+1);
  block.back().parent=parent;for(auto &entry:block)entry.depth+=delta;
  int insertion=above?destination+1:subtree_start(set,destination);
  layers.erase(layers.begin()+start,layers.begin()+source+1);if(insertion>source)insertion-=int(block.size());
  layers.insert(layers.begin()+insertion,block.begin(),block.end());validate_hierarchy(candidate);set=std::move(candidate);
}
void TextureDocument::move_layer(TextureSet &set,const QUuid &id,int offset)
{
  const int source=layer_index(set,id);const int direction=offset>0?1:-1;
  for(int target=source+direction;target>=0&&target<int(set.layers.size());target+=direction) {
    if(set.layers[target].parent==set.layers[source].parent){move_layer_relative(set,id,set.layers[target].id,offset>0);return;}
  }
}
void TextureDocument::move_layer_to(TextureSet &set,const QUuid &id,int target)
{
  if(target<0||target>=int(set.layers.size()))throw std::invalid_argument("Invalid layer drop destination");
  move_layer_relative(set,id,set.layers[target].id,target>layer_index(set,id));
}
void TextureDocument::add_folder(TextureSet &set)
{
  const int selected=layer_index(set,set.selected_layer);TextureLayer folder;folder.material_id=next_material_id(set.layers);folder.displacement.enabled=set.displacement.channel;folder.displacement.blending=LayerBlend::Passthrough;folder.kind=LayerKind::Folder;
  folder.name="Folder "+std::to_string(next_layer_number(set.next_folder_number));folder.parent=set.layers[selected].parent;folder.depth=set.layers[selected].depth;
  prepare_material_layer_insert(set,folder);
  set.selected_layer=folder.id;set.selected_geometry=false;set.selected_mask=false;set.selected_effect=QUuid();set.layers.insert(set.layers.begin()+selected+(folder.parent==set.layers[selected].id?0:1),std::move(folder));
}
void TextureDocument::group_layers(TextureSet &set,const std::vector<QUuid> &ids)
{
  if(ids.empty())return;
  auto candidate=set;std::vector<int> selected;
  for(const auto &id:ids){const int index=layer_index(candidate,id);if(std::find(selected.begin(),selected.end(),index)==selected.end())selected.push_back(index);}
  // A selected folder already contains its selected descendants.
  const auto requested=selected;
  std::erase_if(selected,[&](int index){auto parent=candidate.layers[index].parent;while(!parent.isNull()){const int ancestor=layer_index(candidate,parent);if(std::find(requested.begin(),requested.end(),ancestor)!=requested.end())return true;parent=candidate.layers[ancestor].parent;}return false;});
  std::sort(selected.begin(),selected.end());
  const auto parent=candidate.layers[selected.front()].parent;
  for(int index:selected)if(candidate.layers[index].parent!=parent)throw std::invalid_argument("Group layers from the same folder");
  TextureLayer folder;folder.material_id=next_material_id(candidate.layers);folder.displacement.enabled=set.displacement.channel;folder.displacement.blending=LayerBlend::Passthrough;folder.kind=LayerKind::Folder;folder.parent=parent;folder.depth=candidate.layers[selected.front()].depth;
  prepare_material_layer_insert(candidate,folder);
  folder.name="Folder "+std::to_string(next_layer_number(candidate.next_folder_number));std::vector<TextureLayer> children;
  const int last=selected.back();int insertion=last+1;
  for(int index:selected) {
    const int start=subtree_start(candidate,index);insertion-=index-start+1;
    for(int i=start;i<=index;++i){auto child=candidate.layers[i];++child.depth;if(i==index)child.parent=folder.id;children.push_back(std::move(child));}
  }
  for(auto it=selected.rbegin();it!=selected.rend();++it){const int start=subtree_start(candidate,*it);candidate.layers.erase(candidate.layers.begin()+start,candidate.layers.begin()+*it+1);}
  candidate.selected_layer=folder.id;candidate.selected_mask=false;children.push_back(std::move(folder));candidate.layers.insert(candidate.layers.begin()+insertion,children.begin(),children.end());
  validate_hierarchy(candidate);validate_material_substacks(candidate);set=std::move(candidate);
}
void TextureDocument::remove_masks(TextureSet &set,const std::vector<QUuid> &ids){
  for(const auto &id:ids){auto &target=layer(set,id);if(target.instance)throw std::invalid_argument("Edit instance masks at their source");target.mask.reset();if(set.selected_layer==id){set.selected_mask=false;set.selected_effect=QUuid();}}
}
void TextureDocument::remove_effects(TextureSet &set,const std::vector<std::pair<QUuid,QUuid>> &effects){
  for(const auto &[parent,id]:effects){
    auto &target=layer(set,parent);if(target.instance)throw std::invalid_argument("Edit instance effects at their source");
    std::erase_if(target.effects,[&](const auto &effect){return effect.id==id;});
    if(target.mask)if(const auto path=find_mask_effect_path(std::as_const(target.mask->effects),id)){
      const auto &removed=*find_mask_effect(std::as_const(target.mask->effects),id);
      if(removed.kind==MaskEffectKind::Group&&removed.group_effects&&find_mask_effect(*removed.group_effects,set.selected_effect))set.selected_effect=QUuid();
      auto *siblings=edit_mask_effect_siblings(target.mask->effects,*path);siblings->erase(siblings->begin()+path->back());
    }
    if(set.selected_effect==id)set.selected_effect=QUuid();
  }
}
void TextureDocument::delete_layers(TextureSet &set,const std::vector<QUuid> &ids){
  if(ids.empty())return;auto candidate=set;std::vector<int> selected;
  for(const auto &id:ids){const int index=layer_index(candidate,id);if(std::find(selected.begin(),selected.end(),index)==selected.end())selected.push_back(index);}
  const auto requested=selected;std::erase_if(selected,[&](int index){auto parent=candidate.layers[index].parent;while(!parent.isNull()){const int ancestor=layer_index(candidate,parent);if(std::find(requested.begin(),requested.end(),ancestor)!=requested.end())return true;parent=candidate.layers[ancestor].parent;}return false;});
  if(selected.empty())return;std::vector<char> doomed(candidate.layers.size());for(int index:selected){const int start=subtree_start(candidate,index);for(int i=start;i<=index;++i)doomed[i]=1;}
  if(std::all_of(doomed.begin(),doomed.end(),[](char value){return value;}))throw std::invalid_argument("A Texture Set keeps at least one layer");
  std::sort(selected.begin(),selected.end());for(auto it=selected.rbegin();it!=selected.rend();++it){const int start=subtree_start(candidate,*it);candidate.layers.erase(candidate.layers.begin()+start,candidate.layers.begin()+*it+1);}
  bool alive=false;for(const auto &entry:candidate.layers)alive|=entry.id==candidate.selected_layer;if(!alive){candidate.selected_layer=candidate.layers.back().id;candidate.selected_mask=false;candidate.selected_effect=QUuid();candidate.selected_geometry=false;}
  validate_hierarchy(candidate);validate_material_substacks(candidate);set=std::move(candidate);
}
void TextureDocument::add_paint(TextureSet &set)
{
  const auto selected=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &layer){return layer.id==set.selected_layer;});
  if (selected==set.layers.end()) throw std::runtime_error("Selected layer is missing");
  TextureLayer paint;paint.material_id=next_material_id(set.layers);paint.values.bump=set.bump_origin;paint.displacement.enabled=false;paint.displacement.value=set.displacement.midlevel;paint.parent=selected->parent;paint.depth=selected->depth;paint.name="Layer "+std::to_string(next_layer_number(set.next_paint_number));prepare_material_layer_insert(set,paint);set.selected_layer=paint.id;set.selected_geometry=false;set.selected_mask=false;set.selected_effect=QUuid();
  set.layers.insert(paint.parent==selected->id?selected:selected+1,std::move(paint));
}
void TextureDocument::add_mask(TextureSet &set,float background)
{
  if(!std::isfinite(background)||background<0||background>1)throw std::invalid_argument("Mask background outside 0..1");
  auto &selected=layer(set,set.selected_layer);
  if(selected.instance)throw std::invalid_argument("Edit instance masks at their source");
  selected.mask=LayerMask{};selected.mask->background=background;set.selected_geometry=false;set.selected_mask=true;set.selected_effect=QUuid();
}
LayerMask TextureDocument::copy_mask(const TextureSet &set,const QUuid &id){for(const auto &layer:set.layers)if(layer.id==id&&layer.mask)return *layer.mask;throw std::invalid_argument("Select a source mask");}
void TextureDocument::paste_mask(TextureSet &set,const QUuid &id,const LayerMask &source){
  auto &target=layer(set,id);if(target.instance)throw std::invalid_argument("Edit instance masks at their source");if(!target.mask)throw std::invalid_argument("Paste into mask requires a destination mask");
  for(const auto &[number,pixels]:source.pixels.tiles())if(!set.tiles.contains(number))throw std::invalid_argument("Destination Texture Set does not contain a copied UV tile");
  for(const auto &entry:mask_effect_entries(source.effects))if(entry.effect->kind==MaskEffectKind::Paint)for(const auto &[number,pixels]:entry.effect->paint.pixels.tiles())if(!set.tiles.contains(number))throw std::invalid_argument("Destination does not contain a painted effect UV tile");
  auto replacement=source;replacement.enabled=target.mask->enabled;renew_mask_effect_ids(replacement.effects);
  target.mask=std::move(replacement);set.selected_layer=id;set.selected_geometry=false;set.selected_mask=true;set.selected_effect=QUuid();
}
void TextureDocument::move_effect(TextureSet &set,int offset){
  if(offset!=1&&offset!=-1)throw std::invalid_argument("Invalid effect move");auto &selected=layer(set,set.selected_layer);
  if(selected.instance)throw std::invalid_argument("Edit instance effects at their source");
  const auto move=[&](auto &effects){const auto found=std::find_if(effects.begin(),effects.end(),[&](const auto &e){return e.id==set.selected_effect;});if(found==effects.end())throw std::invalid_argument("Select an effect to move");const auto index=found-effects.begin(),target=index+offset;if(target>=0&&target<std::ptrdiff_t(effects.size()))std::iter_swap(effects.begin()+index,effects.begin()+target);};
  if(set.selected_mask){
    if(!selected.mask)throw std::invalid_argument("Missing mask");
    const auto path=find_mask_effect_path(std::as_const(selected.mask->effects),set.selected_effect);
    if(!path)throw std::invalid_argument("Select an effect to move");
    const auto *siblings=&std::as_const(selected.mask->effects);
    for(std::size_t i=0;i+1<path->size();++i)siblings=siblings->at(path->at(i)).group_effects.get();
    const auto index=std::ptrdiff_t(path->back()),target=index+offset;
    if(target<0||target>=std::ptrdiff_t(siblings->size()))return;
    auto *edited=edit_mask_effect_siblings(selected.mask->effects,*path);
    std::iter_swap(edited->begin()+index,edited->begin()+target);
  }else move(selected.effects);
}
void TextureDocument::apply_mask_preset(TextureSet &set,const QUuid &id,const LayerMask &preset,bool replace){
  PaintMetadataScope metadata;
  for(const auto &[number,pixels]:preset.pixels.tiles())if(!set.tiles.contains(number))throw std::invalid_argument("Destination Texture Set does not contain a preset UV tile");
  for(const auto &entry:mask_effect_entries(preset.effects))if(entry.effect->kind==MaskEffectKind::Paint)for(const auto &[number,pixels]:entry.effect->paint.pixels.tiles())if(!set.tiles.contains(number))throw std::invalid_argument("Preset effect UV tile is missing");
  auto &target=layer(set,id);if(target.instance)throw std::invalid_argument("Edit instance masks at their source");
  LayerMask result=target.mask.value_or(preset);
  if(replace||!target.mask){const bool enabled=result.enabled;result=preset;result.enabled=enabled;renew_mask_effect_ids(result.effects);}
  else {
    // Authored ink is an ordinary GPU Paint stage. Reuse immutable pages/recipes
    // instead of sampling/rescaling every texel on the UI thread during a drop.
    const bool has_ink=!preset.pixels.empty();

    if(has_ink){MaskEffect ink;ink.kind=MaskEffectKind::Paint;ink.paint.pixels=preset.pixels;ink.paint.resolution=preset.author_resolution?preset.author_resolution:preset.pixels.resolution();result.effects.push_back(std::move(ink));}
    auto effects=preset.effects;renew_mask_effect_ids(effects);result.effects.insert(result.effects.end(),effects.begin(),effects.end());
  }
  target.mask=std::move(result);set.selected_layer=id;set.selected_geometry=false;set.selected_mask=true;set.selected_effect=QUuid();
}
static std::array<float,4> fill_sample_unadapted(const TextureSet &set,const TextureLayer &layer,int channel,int x,int y,int tile){
 if(layer.kind!=LayerKind::Fill||x<0||y<0)return {1,1,1,1};if(const auto &ref=layer.fill.anchors.at(channel);!ref.source.isNull())return anchor_components(TextureDocument::anchor_sample({&set,layer.id,x,y,tile,&TextureDocument::anchor_sample,set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get()},ref),ref);auto mapping=layer.fill;mapping.texture=fill_channel_texture(layer.fill,channel);mapping.sample_alpha=layer.fill.alpha_mask.at(channel);mapping.channel_textures={};mapping.atom=channel==0&&layer.fill.atom;mapping.native_per_uv=set.native_per_uv;mapping.cm_per_native=set.cm_per_native;
 if(mapping.mode==6){if(mapping.atom&&!mapping.texture){mapping.mode=0;mapping.scale={1,1};mapping.offset={};mapping.rotation=0;mapping.physical=false;const float value=fill_uv_sample(mapping,x,y,set.width(tile),1001,set.rows(tile));return {value,value,value,1};}return fill_tile_match_sample(mapping,x,y,set.width(tile),set.rows(tile),tile);}
 const bool resource=mapping.texture||mapping.atom;if(!resource&&mapping.mode<2)return {1,1,1,1};
 if(mapping.mode==0){if(mapping.texture)return fill_texture_uv_sample(mapping,x,y,set.width(tile),tile,set.rows(tile));const float value=fill_uv_sample(mapping,x,y,set.width(tile),tile,set.rows(tile));return {value,value,value,1};}
 const auto geometry=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;if(!geometry)return {};const auto found=geometry->tiles.find(tile);if(found==geometry->tiles.end())return {};const int height=geometry->height?geometry->height:geometry->resolution,gx=std::clamp(int((x+.5)*geometry->resolution/set.width(tile)),0,geometry->resolution-1),gy=std::clamp(int((y+.5)*height/set.rows(tile)),0,height-1);const auto &surface=found->second.at(std::size_t(gy)*geometry->resolution+gx);
 if(mapping.mode>=2)return fill_spatial_sample(mapping,surface,*geometry,resource);if(mapping.texture)return fill_texture_triplanar_sample(mapping,surface,geometry->extent);const float value=fill_triplanar_sample(mapping,surface,geometry->extent);return {value,value,value,surface.valid?1.f:0.f};
}
std::array<float,4> TextureDocument::fill_sample(const TextureSet &set,const TextureLayer &layer,int channel,int x,int y,int tile){
 auto sample=fill_sample_unadapted(set,layer,channel,x,y,tile);
 if(channel==4&&layer.kind==LayerKind::Fill&&fill_channel_texture(layer.fill,4))for(int c=0;c<3;++c)sample[c]+=layer.fill.bump_bias;
 return sample;
}
float TextureDocument::projection_coverage(const TextureSet &set,const TextureLayer &layer,int x,int y,int tile){if(layer.kind!=LayerKind::Fill||!fill_spatial_projection(layer.fill)||x<0||y<0)return 1;const auto geometry=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;if(!geometry)return 0;const auto found=geometry->tiles.find(tile);if(found==geometry->tiles.end())return 0;const int height=geometry->height?geometry->height:geometry->resolution,gx=std::clamp(int((x+.5)*geometry->resolution/set.width(tile)),0,geometry->resolution-1),gy=std::clamp(int((y+.5)*height/set.rows(tile)),0,height-1);auto mapping=layer.fill;mapping.cm_per_native=set.cm_per_native;return fill_spatial_sample(mapping,found->second.at(std::size_t(gy)*geometry->resolution+gx),*geometry,false)[3];}
bool TextureDocument::has_pixels(const TextureSet &set)
{
  return std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return bool(layer.quick_overlay)||layer.geometry.active()||(layer.kind==LayerKind::Fill&&(layer.fill.atom||fill_has_texture(layer.fill)||fill_spatial_projection(layer.fill)))||!layer.pixels.empty()||!layer.ao_pixels.empty()||!layer.displacement.pixels.empty()||!layer.effects.empty()||(layer.mask&&(!layer.mask->pixels.empty()||!layer.mask->effects.empty()));});
}
int TextureDocument::authoring_resolution(const TextureSet &set,const TextureLayer &layer,bool mask){if(mask){if(!layer.mask)throw std::invalid_argument("Missing mask authoring data");return layer.mask->author_resolution?layer.mask->author_resolution:layer.mask->pixels.resolution()?layer.mask->pixels.resolution():set.texture_resolution;}return layer.author_resolution?layer.author_resolution:layer.pixels.resolution()?layer.pixels.resolution():layer.ao_pixels.resolution()?layer.ao_pixels.resolution():set.texture_resolution;}
void TextureDocument::set_resolution(TextureSet &set,int resolution){set_dimensions(set,resolution,set.size_linked?resolution:set.rows(),set.size_linked);}
namespace {
void validate_dimension(int size){require_texture_resolution(size,"Texture Set",128,true);require_gpu_texture_extent(size,size,"Texture Set");}
}
void TextureDocument::set_dimensions(TextureSet &set,int width,int height,bool linked){validate_dimension(width);validate_dimension(height);if(linked)height=width;set.texture_resolution=width;set.texture_height=height;set.size_linked=linked;}
void TextureDocument::set_tile_resolution(TextureSet &set,int tile,int resolution){if(!set.tiles.contains(tile))throw std::invalid_argument("UV tile is not in this Texture Set");auto overrides=set.tile_resolutions;if(resolution){validate_dimension(resolution);overrides[tile]=resolution;}else overrides.erase(tile);set.tile_resolutions=std::move(overrides);}
namespace {
float comparison_scalar(int channel,const std::array<float,4> &value){if(channel==0)return value[0]*.2126f+value[1]*.7152f+value[2]*.0722f;if(channel==3){const auto n=normalized_normal({value[0],value[1],value[2]});return (n[0]*.5f+.5f)*.2126f+(n[1]*.5f+.5f)*.7152f+(n[2]*.5f+.5f)*.0722f;}return value[0];}
}
struct RawContentCapture {QUuid layer,point;int channel=0;std::array<float,4> *value=nullptr;};
static UniformComposite composite_impl(const TextureSet &set,int x,int y,int tile,bool evaluated,const QUuid *stop=nullptr,std::array<float,3> *input=nullptr,int target_layer=-1,float *target_mask=nullptr,MaskCompareInputs *target_inputs=nullptr,const std::array<bool,6> *requested=nullptr,const RawContentCapture *raw=nullptr,const QUuid *prefix_stop=nullptr,std::array<float,3> *material_id=nullptr)
{
  std::map<int,std::optional<MaterialIdColor>> ids;
  UniformComposite result;int output_depth=0;std::shared_ptr<MaskAnchorSamples> anchors;
  struct Accumulator {std::array<float,4> value{};float coverage=0;};
  struct Prepared {std::array<float,4> value{};float alpha=0,opacity=0,passthrough_coverage=0;LayerBlend mode=LayerBlend::Normal;bool active=false;};
  std::array<QVarLengthArray<Accumulator,18>,6> stacks;for(auto &stack:stacks){stack.resize(18);std::fill(stack.begin(),stack.end(),Accumulator{});}std::array<std::array<float,4>,6> bases{};
  for(int channel=0;channel<6;++channel){if(requested&&!(*requested)[channel])continue;bases[channel]=components(set.base_values,channel);if(evaluated){const auto q=precision_channel(set.base_values,channel,set.precision[channel]);if(channel==0)for(int c=0;c<3;++c)bases[channel][c]=q[c];else if(channel==3)for(int c=0;c<3;++c)bases[channel][c]=q[c]*2-1;else bases[channel][0]=q[0];}}
  const auto &layers=evaluation_layers(set);auto folder_entries=folder_entry_plan(layers);
  const int first=precompose_evaluation_start(layers,stop,target_layer,raw?&raw->layer:prefix_stop);
  while(!folder_entries.empty()&&folder_entries.back().first<first)folder_entries.pop_back();
  for(int layer_number=first;layer_number<int(layers.size());++layer_number){
    while(!folder_entries.empty()&&folder_entries.back().first==layer_number){
      const auto &folder=layers[folder_entries.back().folder];folder_entries.pop_back();if(material_id)ids[folder.depth+1].reset();
      for(int channel=0;channel<6;++channel){auto &stack=stacks[channel];if(folder.depth+1>=stack.size()){const auto old=stack.size();stack.resize(folder.depth+2);std::fill(stack.begin()+old,stack.end(),Accumulator{});}
        const auto mode=channel==5?folder.ao_blending:folder.blending[channel];
        stack[folder.depth+1]=mode==LayerBlend::Passthrough&&!(raw&&raw->layer==folder.id)?stack[folder.depth]:Accumulator{};
      }
    }
    const auto &layer=layers[layer_number];const bool linear_source=raw_linear_color_source(layer)!=nullptr;const std::array<float,3> linear_tint{layer.values.base_color[0],layer.values.base_color[1],layer.values.base_color[2]};const bool capturing=raw&&raw->layer==layer.id;const bool geometry_visible=TextureDocument::geometry_includes(set,layer,x,y,tile);std::array<Prepared,6> sources{};MaskCompareInputs context;context.layer_index=layer_number;context.anchors=anchors;context.projection=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get();context.cm_per_native=set.cm_per_native;context.native_per_uv=set.native_per_uv;context.reference={&set,layer.id,x,y,tile,&TextureDocument::anchor_sample,set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get()};
    for(int channel=0;channel<6;++channel){
      if((requested&&!(*requested)[channel])||(!capturing&&(channel==5?!set.ao_channel:!set.channels[channel])))continue;
      auto &stack=stacks[channel];if(layer.depth+1>=stack.size()){const auto old=stack.size();stack.resize(layer.depth+2);std::fill(stack.begin()+old,stack.end(),Accumulator{});}auto &destination=stack[layer.depth];auto &source=sources[channel];std::array<float,4> below;for(int c=0;c<4;++c)below[c]=destination.value[c]+bases[channel][c]*(1-destination.coverage);context.below[channel]=comparison_scalar(channel,below);
      Accumulator group;if(layer.kind==LayerKind::Folder){group=stacks[channel][layer.depth+1];stacks[channel][layer.depth+1]={};}
      source.mode=capturing?LayerBlend::Normal:channel==5?layer.ao_blending:layer.blending[channel];
      if(!capturing&&(!(channel==5?layer.ao_enabled:layer.channels[channel])||source.mode==LayerBlend::Disabled))continue;
      source.active=capturing||(layer.visible&&geometry_visible);source.opacity=capturing?1:std::clamp(channel==5?layer.ao_opacity:layer.opacity[channel],0.f,1.f);source.alpha=source.opacity;
      float &alpha=source.alpha;auto value=components(layer.values,channel);
      if(layer.kind==LayerKind::Fill&&(fill_channel_bound(layer.fill,channel)||(channel==0&&layer.fill.atom)||fill_spatial_projection(layer.fill))&&x>=0&&y>=0){
        const auto sampled=TextureDocument::fill_sample(set,layer,channel,x,y,tile);const bool textured=fill_channel_bound(layer.fill,channel);
        if(channel==0)for(int c=0;c<3;++c)value[c]=linear_source?sampled[c]:value[c]*sampled[c];
        else if(textured){if(channel==3){float length=0;for(int c=0;c<3;++c){value[c]=sampled[c]*2-1;length+=value[c]*value[c];}if(!layer.precompose_below){if(length>1e-20)for(int c=0;c<3;++c)value[c]/=std::sqrt(length);else value={0,0,1,1};}}else value[0]=!layer.precompose_below&&(channel==1||channel==2||channel==5)?std::clamp(sampled[0],0.f,1.f):sampled[0];}
        alpha*=sampled[3];
      }
      if(layer.kind==LayerKind::Folder){alpha*=group.coverage;for(int c=0;c<4;++c)value[c]=group.coverage>0?group.value[c]/group.coverage:0;}
      else if(layer.kind==LayerKind::Paint){const auto pixel=channel==0&&layer.quick_overlay?layer.quick_overlay->sample(x,y,set.width(tile),set.rows(tile),tile):x<0||y<0?PaintPixels::Pixel{}:(channel==5?layer.ao_pixels.sample_resampled(2,x,y,set.width(tile),tile,set.rows(tile)):layer.pixels.sample_resampled(channel,x,y,set.width(tile),tile,set.rows(tile)));alpha*=pixel[3];value={0,0,0,1};if(pixel[3]>0)for(int c=0;c<3;++c)value[c]=pixel[c]/pixel[3];}
      const bool contextual_folder=layer.kind==LayerKind::Folder&&source.mode==LayerBlend::Passthrough;
      if(evaluated&&!layer.precompose_below&&!(linear_source&&channel==0)&&!contextual_folder){std::array<float,3> rgb;if(channel==0)rgb={value[0],value[1],value[2]};else if(channel==3)for(int c=0;c<3;++c)rgb[c]=value[c]*.5f+.5f;else rgb.fill(value[0]);const auto q=precision_rgb(rgb,set.precision[channel]);if(channel==0)for(int c=0;c<3;++c)value[c]=q[c];else if(channel==3)for(int c=0;c<3;++c)value[c]=q[c]*2-1;else value[0]=q[0];}
      if(contextual_folder)source.passthrough_coverage=group.coverage;
      else if(source.mode==LayerBlend::Passthrough){const float source_alpha=source.opacity>0?alpha/source.opacity:0;source.passthrough_coverage=source_alpha+destination.coverage*(1-source_alpha);for(int c=0;c<4;++c)value[c]=source.passthrough_coverage>0?(value[c]*source_alpha+destination.value[c]*(1-source_alpha))/source.passthrough_coverage:0;}
      std::array<float,3> rgb=channel==0?std::array<float,3>{value[0],value[1],value[2]}:channel==3?std::array<float,3>{value[0]*.5f+.5f,value[1]*.5f+.5f,value[2]*.5f+.5f}:std::array<float,3>{value[0],value[0],value[0]};float effect_coverage=source.mode==LayerBlend::Passthrough?source.passthrough_coverage:(source.opacity>0?alpha/source.opacity:0);const AnchorSampleContext reference{&set,layer.id,x,y,tile,&TextureDocument::anchor_sample,set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get()};rgb=apply_content_effects(layer.effects,channel,rgb,effect_coverage,x,y,tile,set.width(tile),set.rows(tile),set.precision[channel],capturing?&raw->point:stop,input,&set.mesh_maps,&reference,linear_source&&channel==0?&linear_tint:nullptr);if(capturing&&channel==raw->channel){*raw->value={rgb[0],rgb[1],rgb[2],effect_coverage};return result;}if(source.mode==LayerBlend::Passthrough)source.passthrough_coverage=effect_coverage;else alpha=source.opacity*effect_coverage;if(channel==0)for(int c=0;c<3;++c)value[c]=rgb[c];else if(channel==3)for(int c=0;c<3;++c)value[c]=rgb[c]*2-1;else value[0]=rgb[0];
      source.value=value;context.layer[channel]=comparison_scalar(channel,value);
    }
    const float own_mask=layer.mask?layer.mask->value(x,y,tile,set.width(tile),set.rows(tile),set.mesh_maps[2]?&*set.mesh_maps[2]:nullptr,&set.mesh_maps,&context):1;
    anchors=context.anchors;
    if(layer_number==target_layer){if(target_mask)*target_mask=own_mask;if(target_inputs)*target_inputs=context;return result;}
    const float mask=layer.mask&&layer.mask->enabled?own_mask:1;
    if(material_id){
      float coverage=0;for(const auto &source:sources)if(source.active)coverage=std::max(coverage,(source.mode==LayerBlend::Passthrough?source.opacity*source.passthrough_coverage:source.alpha)*mask);
      auto id=layer.material_id.value_or(material_id_palette[layer_number%material_id_palette.size()]);
      if(layer.kind==LayerKind::Folder){const auto child=ids[layer.depth+1];ids[layer.depth+1].reset();if(!child)coverage=0;else if(!layer.material_id_override_children)id=*child;}
      // Deterministic threshold at pixel centers; soft masks never mix categorical codes.
      if(coverage>=.5f)ids[layer.depth]=id;
    }
    for(int channel=0;channel<6;++channel){auto &source=sources[channel];if(!source.active)continue;auto &stack=stacks[channel];if(layer.depth+1>=stack.size()){const auto old=stack.size();stack.resize(layer.depth+2);std::fill(stack.begin()+old,stack.end(),Accumulator{});}auto &destination=stack[layer.depth];const auto &value=source.value;float alpha=source.alpha;const auto mode=source.mode;
      if(mode==LayerBlend::Passthrough){const float weight=source.opacity*mask;for(int c=0;c<4;++c)destination.value[c]=std::lerp(destination.value[c],value[c]*source.passthrough_coverage,weight);destination.coverage=std::lerp(destination.coverage,source.passthrough_coverage,weight);continue;}
      if(mode==LayerBlend::Replace){for(int c=0;c<4;++c)destination.value[c]=value[c]*alpha*mask+destination.value[c]*(1-mask);destination.coverage=alpha*mask+destination.coverage*(1-mask);continue;}
      alpha*=mask;std::array<float,3> color_backdrop;for(int c=0;c<3;++c)color_backdrop[c]=destination.coverage>0?destination.value[c]/destination.coverage:0;const bool normal_vector=channel==3&&normal_blend_mode(mode);const auto color_blend=channel==0?blend_rgb(mode,{value[0],value[1],value[2]},color_backdrop):normal_vector?blend_normal(mode,{value[0],value[1],value[2]},destination.coverage>0?color_backdrop:NormalVector{0,0,1}):std::array<float,3>{};
      for(int c=0;c<4;++c){const float backdrop=destination.coverage>0?destination.value[c]/destination.coverage:0;const float blended=(channel==0||normal_vector)&&c<3?color_blend[c]:blend_value(mode,value[c],backdrop);destination.value[c]=(1-alpha)*destination.value[c]+alpha*((1-destination.coverage)*value[c]+destination.coverage*blended);}destination.coverage=alpha+destination.coverage*(1-alpha);
    }
    if(prefix_stop&&layer.id==*prefix_stop){output_depth=layer.depth;break;}
  }
  for(int channel=0;channel<6;++channel){if((requested&&!(*requested)[channel])||(channel==5?!set.ao_channel:!set.channels[channel]))continue;const auto &premultiplied=stacks[channel][output_depth].value;const float coverage=stacks[channel][output_depth].coverage;std::array<float,4> material{},authored{};for(int c=0;c<4;++c){material[c]=premultiplied[c]+bases[channel][c]*(1-coverage);authored[c]=coverage>0?premultiplied[c]/coverage:0;}set_components(result.material,channel,material);if(coverage>0){set_components(result.authored,channel,authored);if(prefix_stop&&channel==3)result.authored.normal={authored[0],authored[1],authored[2]};}if(channel==5)result.ao_coverage=coverage;else result.coverage[channel]=coverage;}
  if(set.channels[4]&&(!requested||(*requested)[4])){result.material.bump=set.bump_display(result.material.bump);if(result.coverage[4]>0)result.authored.bump=set.bump_display(result.authored.bump);}
  if(material_id)*material_id=ids[output_depth]?material_id_rgb(*ids[output_depth]):std::array<float,3>{};
  return result;
}
std::array<float,3> TextureDocument::material_id_pixel(const TextureSet &set,int x,int y,int tile){std::array<float,3> id{};composite_impl(set,x,y,tile,true,nullptr,nullptr,-1,nullptr,nullptr,nullptr,nullptr,nullptr,&id);return id;}
UniformComposite folder_prefix_composite(const TextureSet &set,const QUuid &id,int x,int y,int tile){return composite_impl(set,x,y,tile,true,nullptr,nullptr,-1,nullptr,nullptr,nullptr,nullptr,&id);}
UniformComposite TextureDocument::composite(const TextureSet &set,int x,int y,int tile){return composite_impl(set,x,y,tile,false);}
UniformComposite TextureDocument::evaluate(const TextureSet &set,int x,int y,int tile){return composite_impl(set,x,y,tile,true);}
UniformComposite TextureDocument::evaluate_channels(const TextureSet &set,const std::array<bool,6> &wanted,int x,int y,int tile){
 // Comparison/anchor masks consume material channels across the stack. Keep their
 // established full context; independent stacks can skip every unrequested channel.
 const bool coupled=std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return layer.mask&&layer.mask->channel_dependent();});
 return composite_impl(set,x,y,tile,true,nullptr,nullptr,-1,nullptr,nullptr,coupled?nullptr:&wanted);
}
std::array<float,3> TextureDocument::effect_input(const TextureSet &set,const QUuid &effect,int x,int y,int tile){std::array<float,3> input{};for(const auto &layer:set.layers)for(const auto &e:layer.effects)if(e.id==effect&&e.channel>=6){if(e.channel==displacement_effect_channel)composite_displacement(set,x,y,tile,&effect,&input);else composite_user(set,e.channel-6,x,y,tile,&effect,&input);return input;}composite_impl(set,x,y,tile,true,&effect,&input);return input;}
float TextureDocument::mask_value(const TextureSet &set,const QUuid &id,int x,int y,int tile){
  const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==id;});if(found==layers.end())throw std::invalid_argument("Evaluated mask layer is missing");const int index=int(found-layers.begin());const auto &layer=*found;if(!layer.mask)return 1;
  if(!layer.mask->channel_dependent()){MaskCompareInputs context;context.projection=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get();context.cm_per_native=set.cm_per_native;context.native_per_uv=set.native_per_uv;context.reference={&set,layer.id,x,y,tile,&TextureDocument::anchor_sample,set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get()};return layer.mask->value(x,y,tile,set.width(tile),set.rows(tile),set.mesh_maps[2]?&*set.mesh_maps[2]:nullptr,&set.mesh_maps,&context);}
  bool material_inputs=false;for(int i=0;i<=index;++i)if(layers[i].mask&&layers[i].mask->uses_material_channels()){material_inputs=true;break;}
  if(!material_inputs){std::shared_ptr<MaskAnchorSamples> anchors;float value=1;for(int i=0;i<=index;++i){const auto &current=layers[i];if(!current.mask)continue;MaskCompareInputs context;context.layer_index=i;context.anchors=anchors;context.projection=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile).get():set.projection_geometry.get();context.cm_per_native=set.cm_per_native;context.native_per_uv=set.native_per_uv;context.reference={&set,current.id,x,y,tile,&TextureDocument::anchor_sample};value=current.mask->value(x,y,tile,set.width(tile),set.rows(tile),set.mesh_maps[2]?&*set.mesh_maps[2]:nullptr,&set.mesh_maps,&context);anchors=context.anchors;}return value;}
  float value=1;composite_impl(set,x,y,tile,true,nullptr,nullptr,index,&value);return value;
}
MaskCompareInputs TextureDocument::comparison_inputs(const TextureSet &set,const QUuid &id,int x,int y,int tile){const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==id;});if(found==layers.end())throw std::invalid_argument("Evaluated layer is missing");MaskCompareInputs inputs;composite_impl(set,x,y,tile,true,nullptr,nullptr,int(found-layers.begin()),nullptr,&inputs);return inputs;}
QByteArray TextureDocument::mask_dependency_signature(const TextureSet &set){TextureDocument temporary;temporary.sets_.push_back(set);temporary.set_shader_instances(ShaderInstances::defaults(temporary.id_,temporary.shader_set_ids()));if(set.instance_layers)temporary.sets_.back().layers=*set.instance_layers;auto data=temporary.to_json()["texture_sets"].toArray()[0].toObject();data["evaluation_scale"]=QJsonArray{set.cm_per_native,set.native_per_uv[0],set.native_per_uv[1]};return QJsonDocument(data).toJson(QJsonDocument::Compact);}
EffectPaint *TextureDocument::paint_effect(TextureSet &set){
  auto &selected=layer(set,set.selected_layer);
  if(set.selected_mask){if(selected.mask)if(auto *effect=edit_mask_effect(selected.mask->effects,set.selected_effect,MaskEffectKind::Paint))return &effect->paint;}
  else for(auto &effect:selected.effects)if(effect.id==set.selected_effect&&effect.kind==ContentEffectKind::Paint)return &effect.paint;
  return nullptr;
}
const EffectPaint *TextureDocument::paint_effect(const TextureSet &set){
  const auto found=std::find_if(set.layers.begin(),set.layers.end(),[&](const auto &entry){return entry.id==set.selected_layer;});
  if(found==set.layers.end())throw std::runtime_error("Layer is not in this Texture Set");
  if(set.selected_mask){if(found->mask)if(const auto *effect=find_mask_effect(found->mask->effects,set.selected_effect);effect&&effect->kind==MaskEffectKind::Paint)return &effect->paint;}
  else for(const auto &effect:found->effects)if(effect.id==set.selected_effect&&effect.kind==ContentEffectKind::Paint)return &effect.paint;
  return nullptr;
}
void TextureDocument::add_filter_effect(TextureSet &set,int channel){set.selected_geometry=false;auto &selected=layer(set,set.selected_layer);if(set.selected_mask&&selected.mask){MaskEffect effect;effect.kind=MaskEffectKind::Filter;selected.mask->effects.push_back(effect);set.selected_effect=effect.id;}else{if(channel<0||channel>13)throw std::invalid_argument("Invalid filter target or full effect stack");LevelsEffect effect;effect.kind=ContentEffectKind::Filter;effect.channel=channel;selected.effects.push_back(effect);set.selected_effect=effect.id;}}
void TextureDocument::add_paint_effect(TextureSet &set){set.selected_geometry=false;auto &selected=layer(set,set.selected_layer);if(set.selected_mask&&selected.mask){MaskEffect effect;effect.kind=MaskEffectKind::Paint;selected.mask->effects.push_back(effect);set.selected_effect=effect.id;}else{LevelsEffect effect;effect.kind=ContentEffectKind::Paint;selected.effects.push_back(effect);set.selected_effect=effect.id;}}
std::vector<MaskAnchorInfo> TextureDocument::mask_anchors(const TextureSet &set){std::vector<MaskAnchorInfo> result;for(int i=0;i<int(set.layers.size());++i){const auto &layer=set.layers[i];if(layer.mask)for(const auto &entry:mask_effect_entries(layer.mask->effects)){const auto &effect=*entry.effect;if(effect.kind==MaskEffectKind::Anchor)result.push_back({layer.id,effect.id,effect.anchor_name,i,effect.enabled,true});}for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Anchor)result.push_back({layer.id,effect.id,effect.anchor_name,i,effect.enabled,false});}return result;}
std::optional<MaskAnchorInfo> TextureDocument::anchor_point(const TextureSet &set,const QUuid &id){const auto &layers=evaluation_layers(set);for(int i=0;i<int(layers.size());++i){const auto &layer=layers[i];if(layer.mask)if(const auto *effect=find_mask_effect(layer.mask->effects,id);effect&&effect->kind==MaskEffectKind::Anchor)return MaskAnchorInfo{layer.id,id,effect->anchor_name,i,effect->enabled,true};for(const auto &effect:layer.effects)if(effect.id==id&&effect.kind==ContentEffectKind::Anchor)return MaskAnchorInfo{layer.id,id,effect.anchor_name,i,effect.enabled,false};}return {};}
QString TextureDocument::mask_anchor_problem(const TextureSet &set,const QUuid &consumer,const QUuid &source){if(source.isNull())return {};const auto &layers=evaluation_layers(set);const auto target=std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==consumer;});if(target==layers.end())return "The consumer is missing from this Texture Set.";const auto anchor=anchor_point(set,source);if(!anchor)return "The anchor is missing from this Texture Set.";if(anchor->mask){const auto &producer=layers.at(anchor->layer_index);if(mask_effect_path_detail::has_nonempty_group(producer.mask->effects)){const auto path=find_mask_effect_path(producer.mask->effects,source);if(path&&path->size()>1)return "The anchor is inside an isolated mask Group. Expose an Anchor after the Group.";}}if(!anchor->enabled)return "The anchor is disabled.";if(anchor->layer_index>=target-layers.begin())return "The anchor must be on a lower layer.";return {};}

QString TextureDocument::anchor_reference_problem(const TextureSet &set,const QUuid &consumer,const AnchorReference &ref){const auto problem=mask_anchor_problem(set,consumer,ref.source);if(!problem.isEmpty()||ref.source.isNull())return problem;const auto anchor=anchor_point(set,ref.source);if(anchor&&!anchor->mask){if(ref.channel<0||ref.channel>14)return "The referenced channel is unavailable in this Texture Set.";const bool available=ref.channel<5?set.channels[ref.channel]:ref.channel==5?set.ao_channel:ref.channel==14?set.displacement.channel:set.users.at(ref.channel-6).present;if(!available)return "The referenced channel is unavailable in this Texture Set.";}return {};}
QString TextureDocument::mask_effect_anchor_reference_problem(const TextureSet &set,const QUuid &consumer,const QUuid &effect_id,const AnchorReference &ref){
  const auto problem=anchor_reference_problem(set,consumer,ref);if(!problem.isEmpty()||ref.source.isNull())return problem;
  const auto &layers=evaluation_layers(set);const auto layer=std::find_if(layers.begin(),layers.end(),[&](const auto &entry){return entry.id==consumer;});
  if(layer==layers.end()||!layer->mask)return "The consumer mask is missing from this Texture Set.";
  const auto path=find_mask_effect_path(layer->mask->effects,effect_id);
  if(!path)return "The consumer mask effect is missing from this Texture Set.";
  const auto *siblings=&layer->mask->effects;for(std::size_t i=0;i+1<path->size();++i)siblings=siblings->at(path->at(i)).group_effects.get();
  const auto kind=siblings->at(path->back()).kind;
  if(kind!=MaskEffectKind::Fill&&kind!=MaskEffectKind::Generator)return "Select a mask Fill or Generator effect.";
  if(path->size()==1)return {};
  if(kind==MaskEffectKind::Generator)return "Anchor inputs inside an isolated mask Group are unavailable. Use a top-level Generator effect.";
  const auto source=anchor_point(set,ref.source);
  if(source&&!source->mask)return "Content Anchor references inside an isolated mask Group are unavailable. Use a top-level Fill effect.";
  return {};
}

bool TextureDocument::anchor_dependent(const TextureLayer &layer){return fill_has_anchors(layer.fill)||std::any_of(layer.effects.begin(),layer.effects.end(),[](const auto &effect){return effect.enabled&&effect.kind==ContentEffectKind::Generator&&generator_has_anchors(effect);})||(layer.mask&&layer.mask->channel_dependent());}
void TextureDocument::assign_fill_anchor(TextureSet &set,int channel,const AnchorReference &ref,const QUuid &source_set){if(channel<0||channel>=15||ref.channel<0||ref.channel>14||ref.component<0||ref.component>4)throw std::invalid_argument("Invalid fill anchor channel");if(!source_set.isNull()&&source_set!=set.id)throw std::invalid_argument("Anchor references cannot cross Texture Sets");auto &selected=layer(set,set.selected_layer);if(selected.kind!=LayerKind::Fill||set.selected_mask||selected.instance)throw std::invalid_argument("Select an editable Fill layer");const auto problem=anchor_reference_problem(set,selected.id,ref);if(!problem.isEmpty())throw std::invalid_argument(problem.toStdString());fill_channel_texture(selected.fill,channel).reset();if(channel==0)selected.fill.atom=false;selected.fill.anchors[channel]=ref;}
void TextureDocument::assign_mask_anchor_reference(TextureSet &set,const QUuid &id,const AnchorReference &ref,const QUuid &source_set){if(!source_set.isNull()&&source_set!=set.id)throw std::invalid_argument("Anchor references cannot cross Texture Sets");if(ref.channel<0||ref.channel>14||ref.component<0||ref.component>4)throw std::invalid_argument("Invalid anchor channel extraction");const auto problem=mask_effect_anchor_reference_problem(set,set.selected_layer,id,ref);if(!problem.isEmpty())throw std::invalid_argument(problem.toStdString());auto &selected=layer(set,set.selected_layer);if(selected.mask)if(auto *effect=edit_mask_effect(selected.mask->effects,id,MaskEffectKind::Fill)){effect->anchor_source=ref.source;effect->anchor_channel=ref.channel;effect->anchor_component=ref.component;effect->anchor_coverage=ref.coverage;return;}throw std::invalid_argument("Select a mask Fill effect");}
void TextureDocument::assign_mask_anchor(TextureSet &set,const QUuid &id,const QUuid &source,const QUuid &source_set){assign_mask_anchor_reference(set,id,{source},source_set);}
void TextureDocument::add_mask_anchor(TextureSet &set){auto &selected=layer(set,set.selected_layer);if(set.selected_mask){if(!selected.mask)throw std::invalid_argument("Select a mask to expose an anchor");MaskEffect effect;effect.kind=MaskEffectKind::Anchor;effect.anchor_name=QString::fromStdString(selected.name)+" mask";selected.mask->effects.push_back(effect);set.selected_effect=effect.id;}else{LevelsEffect effect;effect.kind=ContentEffectKind::Anchor;effect.anchor_name=QString::fromStdString(selected.name);selected.effects.push_back(effect);set.selected_effect=effect.id;}}
void TextureDocument::rename_mask_anchor(TextureSet &set,const QUuid &id,const QString &name){const auto trimmed=name.trimmed();if(trimmed.isEmpty()||trimmed.contains(QChar::Null))throw std::invalid_argument("Anchor name must contain nonempty text without NUL");auto &selected=layer(set,set.selected_layer);if(selected.mask)if(auto *effect=edit_mask_effect(selected.mask->effects,id,MaskEffectKind::Anchor)){effect->anchor_name=trimmed;return;}for(auto &effect:selected.effects)if(effect.id==id&&effect.kind==ContentEffectKind::Anchor){effect.anchor_name=trimmed;return;}throw std::invalid_argument("Select an anchor point");}
std::array<float,4> TextureDocument::content_anchor_source(const TextureSet &set,const QUuid &id,int channel,int x,int y,int tile){
 const auto point=anchor_point(set,id);if(!point||point->mask||!point->enabled||channel<0||channel>14)return {};if(channel>=6){const auto value=scalar_anchor_source(set,point->layer,id,channel,x,y,tile);return {value[0],value[0],value[0],value[1]};}std::array<float,4> value{};RawContentCapture capture{point->layer,id,channel,&value};std::array<bool,6> wanted{};wanted.at(channel)=true;const bool coupled=std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return layer.mask&&std::any_of(layer.mask->effects.begin(),layer.mask->effects.end(),[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Compare&&(effect.left<2||effect.right<2);});});composite_impl(set,x,y,tile,true,nullptr,nullptr,-1,nullptr,nullptr,coupled?nullptr:&wanted,&capture);return value;
}
AnchorSample TextureDocument::anchor_sample(const AnchorSampleContext &context,const AnchorReference &ref){
 if(!context.set||ref.source.isNull()||ref.channel<0||ref.channel>14||!context.set->tiles.contains(context.tile))return {};const auto *scope=AnchorEvaluationScope::source();const auto &set=scope&&scope->id==context.set->id?*scope:*context.set;if(!anchor_reference_problem(set,context.consumer,ref).isEmpty())return {};const auto point=anchor_point(set,ref.source);if(!point)return {};
 if(!point->mask)return {content_anchor_source(set,point->id,ref.channel,context.x,context.y,context.tile),true,false};const auto &layers=evaluation_layers(set);const auto &source=*std::find_if(layers.begin(),layers.end(),[&](const auto &layer){return layer.id==point->layer;});auto inputs=comparison_inputs(set,point->layer,context.x,context.y,context.tile);inputs.reference={&set,point->layer,context.x,context.y,context.tile,&TextureDocument::anchor_sample};const float value=source.mask->value(context.x,context.y,context.tile,set.width(context.tile),set.rows(context.tile),set.mesh_maps[2]?&*set.mesh_maps[2]:nullptr,&set.mesh_maps,&inputs,&point->id);return {{value,value,value,1},true,true};
}
void TextureDocument::add_compare_mask(TextureSet &set){set.selected_geometry=false;add_mask(set,0);auto &selected=layer(set,set.selected_layer);selected.blending[4]=LayerBlend::Normal;MaskEffect effect;effect.kind=MaskEffectKind::Compare;effect.hardness=.9f;effect.compare_width=.1f;if(!set.channels[4]){effect.compare_channel=0;for(int c=0;c<5;++c)if(set.channels[c]){effect.compare_channel=c;break;}}selected.mask->effects.push_back(effect);set.selected_effect=effect.id;}
MaterialValues TextureDocument::output(const TextureSet &set) { return composite(set).material; }
void TextureDocument::set_export_configuration(const QJsonObject &configuration){std::vector<QUuid> ids;for(const auto &set:sets_)ids.push_back(set.id);const auto parsed=read_export_configuration(configuration,ids);export_configuration_=configuration.isEmpty()?QJsonObject{}:export_configuration_json(parsed);}
void TextureDocument::set_bake_configuration(const QJsonObject &data){
 const auto fail=[] {throw std::invalid_argument("Invalid saved mesh bake settings");};

 if(data.contains("map_options"))validate_map_bake_options(data["map_options"]);
 for(auto key:{"project_mesh","locked","diffusion","denoise","relative","average","ignore_backface","use_cage","match_names"})if(data.contains(key)&&!data[key].isBool())fail();
 const auto integer=[&](const char *key,int low,int high){if(!data.contains(key))return;const auto v=data[key];const double n=v.toDouble(-1);if(high==max_texture_resolution)require_texture_resolution_limit(n,"Bake settings");if(!v.isDouble()||n<low||n>high||n!=std::floor(n))fail();};
 integer("ui_mode",0,2);integer("auto_role",-1,6);integer("resolution",32,max_texture_resolution);integer("height",32,max_texture_resolution);integer("dilation",0,64);integer("rays",1,512);integer("sampling",1,8);
 for(auto key:{"resolution","height","sampling"})if(data.contains(key)){const int n=data[key].toInt();if(n&(n-1))fail();}
 for(auto key:{"frontal","rear","curvature_radius","curvature_scale","ao_distance","thickness_distance","ray_offset","normal_detail"})if(data.contains(key)&&(!data[key].isDouble()||!std::isfinite(data[key].toDouble())||data[key].toDouble()<0||data[key].toDouble()>10000))fail();
 if(data.contains("enabled")){const auto list=data["enabled"].toArray();if(!data["enabled"].isArray()||list.size()!=7)fail();for(auto v:list)if(!v.isBool())fail();}
 for(auto key:{"cage_file","low_marker","high_marker","ignore_marker"})if(data.contains(key)&&(!data[key].isString()||data[key].toString().contains(QChar::Null)))fail();
 if(data.contains("high_files")){if(!data["high_files"].isArray())fail();for(auto v:data["high_files"].toArray())if(!v.isString()||v.toString().contains(QChar::Null))fail();}
 if(data.contains("tiles")){if(!data["tiles"].isObject())fail();for(auto v:data["tiles"].toObject()){if(!v.isArray())fail();for(auto tile:v.toArray())if(!tile.isDouble()||tile.toInt()<1001||tile.toInt()>2000||tile.toDouble()!=tile.toInt())fail();}}
 bake_configuration_=data;
}
QJsonObject TextureDocument::edit_state()const{EditStateScope scope;return to_json();}
QJsonObject TextureDocument::to_json() const
{
  RibbonWriteScope ribbon_images;
  QJsonArray sets;
  for (const auto &set : sets_) {
    QJsonObject tile_resolutions;for(const auto &[tile,size]:set.tile_resolutions)tile_resolutions[QString::number(tile)]=size;
    QJsonArray layers, channels,precision;for(auto format:set.precision)precision.append(int(format));
    for (bool enabled : set.channels) channels.append(enabled);
    for (const auto &layer : set.layers) {
      (void)raw_linear_color_source(layer);
      QJsonArray layer_channels,pages,hashes,paint_tiles,opacity,blending;
      for(auto mode:layer.blending)blending.append(layer_blend_names[int(mode)]);
      for(float value:layer.opacity)opacity.append(value);
      for(const auto &[number,pixels]:layer.pixels.tiles()) {
        QJsonArray tile_pages,tile_hashes;
        for(int c=0;c<5;++c){tile_pages.append(qint64(pixels.page_count(c)));tile_hashes.append(QString::number(pixels.hash(c),16));}
        paint_tiles.append(QJsonObject{{"number",number},{"pages",tile_pages},{"hashes",tile_hashes}});
      }
      for (int channel=0;channel<5;++channel) {
        pages.append(qint64(layer.pixels.page_count(channel)));
        hashes.append(QString::number(layer.pixels.hash(channel),16));
      }
      for (bool enabled : layer.channels) layer_channels.append(enabled);
      QJsonValue mask;
      if(layer.mask) {
        QJsonArray tiles;
        for(const auto &[number,pixels]:layer.mask->pixels.tiles())tiles.append(QJsonObject{{"number",number},{"pages",qint64(pixels.page_count(2))},{"hash",QString::number(pixels.hash(2),16)}});
        mask=QJsonObject{{"effects",mask_effects_json(layer.mask->effects)},{"author_resolution",layer.mask->author_resolution?layer.mask->author_resolution:layer.mask->pixels.resolution()},{"strokes",stroke_history_json(layer.mask->strokes,{layer.mask->pixels,{}})},{"background",layer.mask->background},{"enabled",layer.mask->enabled},{"tiles",tiles}};
      }
      QJsonArray ao_tiles;for(const auto &[number,pixels]:layer.ao_pixels.tiles())ao_tiles.append(QJsonObject{{"number",number},{"pages",qint64(pixels.page_count(2))},{"hash",QString::number(pixels.hash(2),16)}});
      layers.append(QJsonObject{{"instance",layer_instance_json(layer.instance)},{"effects",effects_json(layer.effects)},{"users",custom_layers_json(layer.users)},{"displacement",custom_layer_json(layer.displacement)},{"author_resolution",layer.author_resolution?layer.author_resolution:std::max(layer.pixels.resolution(),layer.ao_pixels.resolution())},{"strokes",stroke_history_json(layer.strokes,{layer.pixels,layer.ao_pixels})},{"id", layer.id.toString(QUuid::WithoutBraces)},
          {"name", QString::fromStdString(layer.name)}, {"kind", layer.kind==LayerKind::Folder ? "folder" : layer.kind==LayerKind::Fill ? "fill" : "paint"}, {"parent",layer.parent.isNull()?QString{}:layer.parent.toString(QUuid::WithoutBraces)},
          {"visible", layer.visible}, {"precompose_below",layer.precompose_below}, {"channel_opacity", opacity},{"blending",blending}, {"coverage", layer.kind==LayerKind::Fill ? "full" : layer.pixels.empty() ? "empty" : "painted"},
          {"geometry_mask",geometry_mask_json(layer.geometry)},{"ao",QJsonObject{{"enabled",layer.ao_enabled},{"opacity",layer.ao_opacity},{"blending",QString::fromLatin1(layer_blend_names[int(layer.ao_blending)])},{"tiles",ao_tiles}}},{"fill_projection",QJsonObject{{"symmetry",fill_symmetry_json(layer.fill.symmetry)},{"material_resource",QString::fromStdString(layer.fill.material_resource)},{"native_material",native_material_fill_json(layer.fill)},{"scale",QJsonArray{layer.fill.scale[0],layer.fill.scale[1]}},{"offset",QJsonArray{layer.fill.offset[0],layer.fill.offset[1]}},{"rotation",layer.fill.rotation},{"linked",layer.fill.linked},{"manipulator",layer.fill.manipulator},{"atom",layer.fill.atom},{"texture",fill_texture_json(layer.fill.texture)},{"channel_textures",fill_channel_textures_json(layer.fill)},{"alpha_mask",fill_alpha_mask_json(layer.fill)},{"physical",layer.fill.physical},{"custom_size",layer.fill.custom_size},{"asset_size_cm",QJsonArray{layer.fill.asset_size_cm[0],layer.fill.asset_size_cm[1],layer.fill.asset_size_cm[2]}},{"custom_size_cm",QJsonArray{layer.fill.custom_size_cm[0],layer.fill.custom_size_cm[1],layer.fill.custom_size_cm[2]}},{"invert",layer.fill.invert},{"border_dots",layer.fill.border_dots},{"middle_dot",layer.fill.middle_dot},{"border_width",layer.fill.border_width},{"mode",layer.fill.mode},{"warp",warp_settings_json(layer.fill)},{"projection_options",QJsonObject{{"filtering",layer.fill.filtering},{"wrap",layer.fill.wrap},{"crop",layer.fill.shape_crop},{"depth",layer.fill.depth_culling},{"depth_hardness",layer.fill.depth_hardness},{"backface",layer.fill.backface_culling},{"backface_angle",layer.fill.backface_angle},{"backface_hardness",layer.fill.backface_hardness},{"cylinder_angle",layer.fill.cylinder_angle}}},{"hardness",layer.fill.hardness},{"offset3",QJsonArray{layer.fill.offset3[0],layer.fill.offset3[1],layer.fill.offset3[2]}},{"rotation3",QJsonArray{layer.fill.rotation3[0],layer.fill.rotation3[1],layer.fill.rotation3[2]}},{"scale3",QJsonArray{layer.fill.scale3[0],layer.fill.scale3[1],layer.fill.scale3[2]}},{"linked3",layer.fill.linked3},{"gizmo",layer.fill.gizmo},{"gizmo_size",layer.fill.gizmo_size}}},{"mask",mask},{"paint_pages",pages},{"paint_hashes",hashes},{"paint_tiles",paint_tiles},{"values",values_json(layer.values)},{"channels",layer_channels}});
      if(layer.material_id||layer.material_id_override_children){auto description=layers.last().toObject();if(layer.material_id)description["material_id"]=QJsonArray{(*layer.material_id)[0],(*layer.material_id)[1],(*layer.material_id)[2]};if(layer.material_id_override_children)description["material_id_folder"]=true;layers[layers.size()-1]=description;}
      if(fill_has_anchors(layer.fill)){auto description=layers.last().toObject();auto projection=description["fill_projection"].toObject();projection["anchors"]=fill_anchors_json(layer.fill);description["fill_projection"]=projection;layers.replace(layers.size()-1,description);}
    }
    QJsonArray maps;for(const auto &map:set.mesh_maps){if(!map){maps.append(QJsonValue());continue;}QJsonArray planes;for(const auto &[tile,plane]:map->tiles)planes.append(QJsonObject{{"number",tile},{"resolution",plane.resolution},{"height",plane.rows()},{"hash",QString::fromStdString(plane.hash)}});maps.append(QJsonObject{{"name",QString::fromStdString(map->name)},{"tiles",planes}});}
    QJsonArray tiles;for(int number:set.tiles)tiles.append(number);
    QJsonArray unloaded;for(int number:set.unloaded_tiles)unloaded.append(number);
    validate_material_substacks(set);
    sets.append(QJsonObject{{"bump_origin",set.bump_origin},{"material_layering",material_layering_json(set.material_layering)},{"skew_correction",skew_field_json(set.skew)},{"tile_resolutions",tile_resolutions},{"users",custom_channels_json(set.users)},{"displacement",displacement_json(set.displacement)},{"resolution",set.texture_resolution},{"height",set.rows()},{"size_linked",set.size_linked},{"precision",precision},{"id", set.id.toString(QUuid::WithoutBraces)},
        {"name", QString::fromStdString(set.name)}, {"uniform_editable",set.uniform_editable}, {"disabled",set.disabled}, {"loaded",set.loaded}, {"unloaded_tiles",unloaded}, {"mesh_maps",maps},{"padding_3d",set.padding_3d},{"ao_channel",set.ao_channel},{"ao_mixing",int(set.ao_mixing)},{"normal_mixing",int(set.normal_mixing)},{"bump_method",int(set.bump_method)}, {"tiles",tiles}, {"channels", channels}, {"layers", layers},
        {"selected_geometry",set.selected_geometry},{"selected_layer",set.selected_layer.toString(QUuid::WithoutBraces)},{"selected_mask",set.selected_mask},{"selected_effect",set.selected_effect.isNull()?QString{}:set.selected_effect.toString(QUuid::WithoutBraces)},
        {"next_folder_number",set.next_folder_number},{"next_paint_number",set.next_paint_number}, {"next_fill_number",set.next_fill_number}, {"base_values",values_json(set.base_values)}});
  }
  QJsonObject root{{"schema", texture_document_schema}, {"shader_instances",shader_instances_.to_json()}, {"ribbon_images",ribbon_images.images()}, {"id", id_.toString(QUuid::WithoutBraces)},
          {"source_mesh", QString::fromStdString(settings_.absolute_mesh_path)},
          {"blend_selection",settings_.blend.to_json()},
          {"resolution", int(settings_.resolution)},
          {"tangent_per_fragment",settings_.tangent_per_fragment},{"import_cameras",settings_.import_cameras},
          {"units",QJsonObject{{"internal",settings_.units.internal},{"custom_cm",settings_.units.custom_cm_per_file_unit},{"auto_physical",settings_.units.auto_physical},{"native_per_file",settings_.source_units.native_per_file_unit},{"cm_per_file",settings_.source_units.cm_per_file_unit},{"exact",settings_.source_units.exact}}},
          {"usd",QJsonObject{{"scope",QString::fromStdString(settings_.usd.scope)},{"frame",settings_.usd.frame},{"explicit_frame",settings_.usd.explicit_frame},{"subdivision",settings_.usd.subdivision},{"variants",[&]{QJsonObject result;for(const auto &[key,value]:settings_.usd.variants)result[QString::fromStdString(key)]=QString::fromStdString(value);return result;}()}}},
          {"auto_unwrap",QJsonObject{{"enabled",settings_.auto_unwrap.enabled},{"seams",int(settings_.auto_unwrap.seams)},{"islands",int(settings_.auto_unwrap.islands)},{"packing",int(settings_.auto_unwrap.packing)},{"margin",settings_.auto_unwrap.margin},{"hard_surface",settings_.auto_unwrap.hard_surface}}},
          {"normal_format", settings_.normal_map_format == NormalMapFormat::OpenGL ? "OpenGL" : "DirectX"},
          {"uv_workflow", settings_.uv_workflow == UvWorkflow::SingleTile ? "single_tile" : "preserve_tiles"},
          {"texture_sets", sets},{"export_configuration",export_configuration_},{"bake_configuration",bake_configuration_}};
  if (!edit_storage_identity) {
    const auto ref_img = document_reference_image(*this);
    if (ref_img.enabled || !ref_img.path.empty()) {
      root["reference_image"] = QJsonObject{
          {"enabled", ref_img.enabled},
          {"path", QString::fromStdString(ref_img.path)},
          {"center", QJsonArray{ref_img.center[0], ref_img.center[1]}},
          {"vertical_extent", ref_img.vertical_extent},
      };
    }
  }
  return root;
}
TextureDocument TextureDocument::from_json(const QJsonObject &root,std::span<const TextureSetBinding> bindings)
{
  const auto fail=[] {throw std::invalid_argument("Invalid or unsupported mPainter project metadata");};
  const auto number=[&](const QJsonValue &v,double low,double high) {
    if(!v.isDouble()||!std::isfinite(v.toDouble())||v.toDouble()<low||v.toDouble()>high)fail();
    return v.toDouble();
  };
  const auto integer=[&](const QJsonValue &v,int low,int high) {
    if(high==max_texture_resolution)require_texture_resolution_limit(v.toDouble(),"Project");const double n=number(v,low,high);if(n!=std::floor(n))fail();return int(n);
  };
  const auto page_count=[&](const QJsonValue &v,std::uint64_t low,std::uint64_t high) {
    const double n=number(v,double(low),double(high));if(n!=std::floor(n))fail();return std::uint64_t(n);
  };
  const ResourceAvailability text_budget;std::uint64_t text_bytes=0;
  const auto text=[&](const QJsonValue &v,qsizetype limit=std::numeric_limits<qsizetype>::max()) {
    if(!v.isString()||v.toString().size()>limit||v.toString().contains(QChar(0)))fail();
    text_bytes=resource_sum(text_bytes,resource_product(v.toString().size(),8));return v.toString();
  };
  std::vector<QUuid> ids;
  const auto id=[&](const QJsonValue &v) {
    const QUuid uuid(text(v,38));if(uuid.isNull()||std::find(ids.begin(),ids.end(),uuid)!=ids.end())fail();
    ids.push_back(uuid);return uuid;
  };
  const auto boolean=[&](const QJsonValue &v) {if(!v.isBool())fail();return v.toBool();};
  const auto channels=[&](const QJsonValue &v) {
    if(!v.isArray()||v.toArray().size()!=5)fail();std::array<bool,5> result;
    for(int c=0;c<5;++c)result[c]=boolean(v.toArray()[c]);return result;
  };
  const auto values=[&](const QJsonValue &v) {
    if(!v.isObject())fail();const auto o=v.toObject();MaterialValues result;
    const auto color=o["color"].toArray(),normal=o["normal"].toArray();
    if(color.size()!=4||normal.size()!=3)fail();
    for(int c=0;c<4;++c)result.base_color[c]=float(number(color[c],c==3?0:-65504,c==3?1:65504));
    for(int c=0;c<3;++c)result.normal[c]=float(number(normal[c],-1,1));
    result.metallic=float(number(o["metallic"],0,1));result.roughness=float(number(o["roughness"],0,1));
    result.bump=float(number(o.contains("bump")?o["bump"]:o["height"],-65504,65504));if(root["schema"].toInt()>=14)result.ambient_occlusion=float(number(o["ao"],0,1));return result;
  };
  integer(root["schema"],1,texture_document_schema);if(root["schema"].toInt()>=49&&!root["ribbon_images"].isObject())fail();RibbonReadScope ribbon_images(root["ribbon_images"].toObject());TextureDocument result;result.id_=id(root["id"]);
  result.settings_.absolute_mesh_path=text(root["source_mesh"]).toStdString();
  if(root["schema"].toInt()>=22){if(!root["units"].isObject())fail();const auto unit=root["units"].toObject();auto &options=result.settings_.units;auto &source=result.settings_.source_units;options.internal=boolean(unit["internal"]);options.custom_cm_per_file_unit=number(unit["custom_cm"],1e-12,1e12);options.auto_physical=boolean(unit["auto_physical"]);source.native_per_file_unit=number(unit["native_per_file"],1e-12,1e12);source.cm_per_file_unit=number(unit["cm_per_file"],1e-12,1e12);source.exact=boolean(unit["exact"]);validate_units(options,source);if(!options.internal&&!source.exact)fail();}else{const auto name=QString::fromStdString(result.settings_.absolute_mesh_path).toLower();for(auto suffix:{".usd",".usda",".usdc",".usdz",".dae"})if(name.endsWith(suffix))result.settings_.source_units={1,100,false};}
  const int resolution=integer(root["resolution"],128,max_texture_resolution);if((resolution&(resolution-1))!=0)fail();
  result.settings_.resolution=DocumentResolution(resolution);
  const auto normal=text(root["normal_format"],16),uv=text(root["uv_workflow"],32);
  if(normal!="OpenGL"&&normal!="DirectX")fail();if(uv!="single_tile"&&uv!="preserve_tiles")fail();
  result.settings_.normal_map_format=normal=="OpenGL"?NormalMapFormat::OpenGL:NormalMapFormat::DirectX;
  result.settings_.tangent_per_fragment=root["schema"].toInt()>=18?boolean(root["tangent_per_fragment"]):false;
  result.settings_.import_cameras=root["schema"].toInt()>=19?boolean(root["import_cameras"]):true;
  if(root["schema"].toInt()>=74)result.settings_.blend=BlendMeshSelection::from_json(root["blend_selection"]);
  if(root["schema"].toInt()>=21){if(!root["usd"].isObject())fail();const auto data=root["usd"].toObject();auto &usd=result.settings_.usd;usd.scope=text(data["scope"]).toStdString();if(usd.scope.empty()||usd.scope.front()!='/')fail();usd.frame=number(data["frame"],-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());usd.explicit_frame=boolean(data["explicit_frame"]);usd.subdivision=integer(data["subdivision"],0,11);if(!data["variants"].isObject())fail();const auto variants=data["variants"].toObject();for(auto entry=variants.begin();entry!=variants.end();++entry){const auto key=entry.key();if(!key.startsWith('/')||!key.contains('#'))fail();usd.variants.emplace(key.toStdString(),text(entry.value()).toStdString());}}
  if(root["schema"].toInt()>=20){if(!root["auto_unwrap"].isObject())fail();const auto uv=root["auto_unwrap"].toObject();auto &options=result.settings_.auto_unwrap;options.enabled=boolean(uv["enabled"]);options.seams=UvDataPolicy(integer(uv["seams"],0,1));options.islands=UvDataPolicy(integer(uv["islands"],0,1));options.packing=UvDataPolicy(integer(uv["packing"],0,1));options.margin=integer(uv["margin"],0,3);options.hard_surface=uv.contains("hard_surface")?boolean(uv["hard_surface"]):false;}
  result.settings_.uv_workflow=uv=="single_tile"?UvWorkflow::SingleTile:UvWorkflow::PreserveTiles;
  const auto sets=root["texture_sets"].toArray();if(sets.empty()||sets.size()!=int(bindings.size()))fail();
  std::vector<std::uint32_t> bound;
  std::size_t total_layers=0;
  for(const auto &entry:sets) {
    if(!entry.isObject())fail();const auto o=entry.toObject();TextureSet set;set.cm_per_native=centimeters_per_native_unit(result.settings_.units,result.settings_.source_units);set.texture_resolution=root["schema"].toInt()>=23?integer(o["resolution"],128,max_texture_resolution):resolution;if(set.texture_resolution&(set.texture_resolution-1))fail();if(root["schema"].toInt()>=24){set.texture_height=integer(o["height"],128,max_texture_resolution);set.size_linked=boolean(o["size_linked"]);if((set.rows()&(set.rows()-1))||(set.size_linked&&set.rows()!=set.texture_resolution))fail();}if(root["schema"].toInt()>=25){const auto formats=o["precision"].toArray();if(formats.size()!=6)fail();for(int c=0;c<6;++c)set.precision[c]=ChannelPrecision(integer(formats[c],0,8));}if(root["schema"].toInt()>=26)set.users=read_custom_channels(o["users"]);set.normal_format=result.settings_.normal_map_format;set.tangent_per_fragment=result.settings_.tangent_per_fragment;set.id=id(o["id"]);
    const auto binding=std::find_if(bindings.begin(),bindings.end(),[&](const auto &b){return b.set_id==set.id;});
    if(binding==bindings.end()||!binding->material_uid||std::find(bound.begin(),bound.end(),binding->material_uid)!=bound.end())fail();
    bound.push_back(binding->material_uid);set.material_session_uid=binding->material_uid;
    if(o.contains("loaded"))set.loaded=boolean(o["loaded"]);
    if(o.contains("unloaded_tiles")){if(!o["unloaded_tiles"].isArray())throw std::invalid_argument("Invalid unloaded tiles");for(const auto &v:o["unloaded_tiles"].toArray()){const int tile=v.toInt();if(!v.isDouble()||v.toDouble()!=tile||tile<1001||tile>2000||!set.unloaded_tiles.insert(tile).second)throw std::invalid_argument("Invalid unloaded tile");}}
    const auto stored_name=text(o["name"]);set.name=root["schema"].toInt()>=17?stored_name.toStdString():binding->name;set.disabled=root["schema"].toInt()>=17?boolean(o["disabled"]):false;set.uniform_editable=root["schema"].toInt()>=73?boolean(o["uniform_editable"]):true;
    if(root["schema"].toInt()>=2) {
      const auto tiles=o["tiles"].toArray();if(tiles.empty())fail();set.tiles.clear();
      for(const auto &tile:tiles)if(!set.tiles.insert(integer(tile,1001,2000)).second)fail();
      if(result.settings_.uv_workflow==UvWorkflow::SingleTile&&set.tiles!=std::set<int>{1001})fail();
    }
    for(int tile:set.unloaded_tiles)if(!set.tiles.contains(tile))fail();
    set.skew.resolution=set.texture_resolution;if(root["schema"].toInt()>=57)set.skew=read_skew_field_json(o["skew_correction"],set.tiles);
    if(root["schema"].toInt()>=27){if(!o["tile_resolutions"].isObject())fail();const auto overrides=o["tile_resolutions"].toObject();if(overrides.size()>int(set.tiles.size()))fail();for(auto entry=overrides.begin();entry!=overrides.end();++entry){bool valid=false;const int tile=entry.key().toInt(&valid);if(!valid||QString::number(tile)!=entry.key()||!set.tiles.contains(tile))fail();const int size=integer(entry.value(),128,max_texture_resolution);if(size&(size-1))fail();set.tile_resolutions.emplace(tile,size);}}
    // Parsing logical metadata does not allocate channel images; GPU/CPU allocators
    // admit the actual resident buffers when they are requested.
    if(root["schema"].toInt()>=7)set.padding_3d=boolean(o["padding_3d"]);
    if(root["schema"].toInt()>=12)set.normal_mixing=NormalMixing(integer(o["normal_mixing"],0,1));
    if(root["schema"].toInt()>=14){set.ao_channel=boolean(o["ao_channel"]);set.ao_mixing=AoMixing(integer(o["ao_mixing"],0,1));}
    if(root["schema"].toInt()>=13)set.bump_method=BumpMethod(integer((o.contains("bump_method")?o["bump_method"]:o["height_method"]),0,1));
    if(root["schema"].toInt()>=10){const auto maps=o["mesh_maps"].toArray();if(maps.size()!=7)fail();for(int role=0;role<7;++role){if(maps[role].isNull())continue;if(!maps[role].isObject())fail();const auto map=maps[role].toObject();MeshMap resource;resource.name=text(map["name"]).toStdString();const auto planes=map["tiles"].toArray();if(planes.isEmpty()||planes.size()>int(set.tiles.size()))fail();for(const auto &entry:planes){const auto plane=entry.toObject();const int number=integer(plane["number"],1001,2000),size=integer(plane["resolution"],32,max_texture_resolution),height=root["schema"].toInt()>=11?integer(plane["height"],32,max_texture_resolution):size;const auto hash=text(plane["hash"],16);if((size&(size-1))||(height&(height-1))||hash.size()!=16||QByteArray::fromHex(hash.toLatin1()).size()!=8||!set.tiles.contains(number))fail();if(!resource.tiles.emplace(number,MeshMapPlane{size,hash.toStdString(),{},height}).second)fail();}set.mesh_maps[role]=std::move(resource);}}
    if(root["schema"].toInt()>=47)set.displacement=read_displacement(o["displacement"]);
    set.channels=channels(o["channels"]);set.base_values=values(o["base_values"]);
    set.bump_origin=o.contains("bump_origin")?float(number(o["bump_origin"],0,.5)):0;if(set.bump_origin!=0&&set.bump_origin!=.5f)fail();
    if(root["schema"].toInt()>=4)set.next_folder_number=integer(o["next_folder_number"],1,std::numeric_limits<int>::max());
    set.next_fill_number=integer(o["next_fill_number"],1,std::numeric_limits<int>::max());set.next_paint_number=integer(o["next_paint_number"],2,std::numeric_limits<int>::max());
    const auto layers=o["layers"].toArray();total_layers+=layers.size();if(layers.empty())fail();set.layers.clear();
    for(const auto &item:layers) {
      if(!item.isObject())fail();const auto l=item.toObject();TextureLayer layer;if(root["schema"].toInt()>=30){layer.effects=read_effects(l["effects"]);for(const auto &effect:layer.effects)id(effect.id.toString(QUuid::WithoutBraces));}bool retained_paint=false;for(const auto &count:l["paint_pages"].toArray())retained_paint|=count.toInteger()>0;retained_paint|=!l["ao"].toObject()["tiles"].toArray().isEmpty()||l["strokes"].isObject();layer.author_resolution=root["schema"].toInt()>=23?integer(l["author_resolution"],0,max_texture_resolution):retained_paint?resolution:0;
      if(root["schema"].toInt()>=8){const auto p=l["fill_projection"].toObject();if(p.empty())fail();auto &f=layer.fill;const auto scale=p["scale"].toArray(),offset=p["offset"].toArray();if(scale.size()!=2||offset.size()!=2)fail();for(int i=0;i<2;++i){f.scale[i]=number(scale[i],-1000,1000);f.offset[i]=number(offset[i],-1000,1000);}f.rotation=number(p["rotation"],-360000,360000);f.linked=boolean(p["linked"]);f.manipulator=boolean(p["manipulator"]);f.atom=boolean(p["atom"]);if(root["schema"].toInt()>=48)f.texture=read_fill_texture_json(p["texture"]);f.invert=boolean(p["invert"]);f.border_dots=boolean(p["border_dots"]);f.middle_dot=boolean(p["middle_dot"]);f.border_width=number(p["border_width"],0,1);validate_fill_projection(f);}
      if(root["schema"].toInt()>=22){const auto p=l["fill_projection"].toObject();auto &f=layer.fill;f.physical=boolean(p["physical"]);f.custom_size=boolean(p["custom_size"]);const auto asset=p["asset_size_cm"].toArray(),custom=p["custom_size_cm"].toArray();if(asset.size()!=3||custom.size()!=3)fail();for(int i=0;i<3;++i){f.asset_size_cm[i]=number(asset[i],0,1e9);f.custom_size_cm[i]=number(custom[i],1e-6,1e9);}validate_fill_projection(f);}
      layer.fill.symmetry=read_fill_symmetry(l["fill_projection"].toObject()["symmetry"]);
      if(l["fill_projection"].toObject().contains("material_resource")){const auto value=l["fill_projection"].toObject()["material_resource"];if(!value.isString())fail();const auto text=value.toString();if(!text.isEmpty()){const QUuid id(text);if(id.isNull()||text.size()!=36)fail();layer.fill.material_resource=id.toString(QUuid::WithoutBraces).toStdString();}}
      read_native_material_fill(layer.fill,l["fill_projection"].toObject()["native_material"]);
      if(root["schema"].toInt()>=54)read_fill_channel_textures(layer.fill,l["fill_projection"].toObject()["channel_textures"]);
      read_fill_anchors(layer.fill,l["fill_projection"].toObject()["anchors"]);
      if(root["schema"].toInt()>=64)read_fill_alpha_mask(layer.fill,l["fill_projection"].toObject()["alpha_mask"]);else layer.fill.legacy_alpha_policy=true;
      if(root["schema"].toInt()>=51){const auto p=l["fill_projection"].toObject()["projection_options"].toObject();auto &f=layer.fill;f.filtering=integer(p["filtering"],0,2);f.wrap=integer(p["wrap"],0,3);f.shape_crop=boolean(p["crop"]);f.depth_culling=boolean(p["depth"]);f.depth_hardness=number(p["depth_hardness"],0,1);f.backface_culling=boolean(p["backface"]);f.backface_angle=number(p["backface_angle"],0,180);f.backface_hardness=number(p["backface_hardness"],0,1);if(root["schema"].toInt()>=53)f.cylinder_angle=number(p["cylinder_angle"],1,360);}
      if(root["schema"].toInt()>=9){const auto p=l["fill_projection"].toObject();auto &f=layer.fill;f.mode=integer(p["mode"],0,root["schema"].toInt()>=56?6:root["schema"].toInt()>=55?5:root["schema"].toInt()>=53?4:root["schema"].toInt()>=52?3:root["schema"].toInt()>=51?2:1);f.hardness=number(p["hardness"],0,1);f.linked3=boolean(p["linked3"]);f.gizmo=integer(p["gizmo"],0,2);f.gizmo_size=number(p["gizmo_size"],.01,10);const auto o=p["offset3"].toArray(),r=p["rotation3"].toArray(),s=p["scale3"].toArray();if(o.size()!=3||r.size()!=3||s.size()!=3)fail();for(int i=0;i<3;++i){f.offset3[i]=number(o[i],-1e6,1e6);f.rotation3[i]=number(r[i],-360000,360000);f.scale3[i]=number(s[i],-1000,1000);}validate_fill_projection(f);}
      if(root["schema"].toInt()>=55)read_warp_settings(layer.fill,l["fill_projection"].toObject()["warp"]);
      if(root["schema"].toInt()>=47)layer.displacement=read_custom_layer(l["displacement"],set.tiles);else layer.displacement.enabled=false;
      layer.id=id(l["id"]);layer.name=text(l["name"]).toStdString();
      const auto kind=text(l["kind"],16);if(kind!="paint"&&kind!="fill"&&(kind!="folder"||root["schema"].toInt()<4))fail();layer.kind=kind=="paint"?LayerKind::Paint:kind=="fill"?LayerKind::Fill:LayerKind::Folder;if(layer.fill.native_material&&layer.kind!=LayerKind::Fill)fail();if(root["schema"].toInt()>=26){layer.users=read_custom_layers(l["users"],set.tiles);if(layer.kind!=LayerKind::Paint)for(const auto &user:layer.users)if(user.strokes||user.author_resolution)fail();}else if(layer.kind==LayerKind::Fill)for(auto &user:layer.users)user.enabled=false;if(layer.kind!=LayerKind::Paint&&(layer.displacement.strokes||layer.displacement.author_resolution))fail();
      if(root["schema"].toInt()>=4){const auto parent=text(l["parent"],38);if(!parent.isEmpty()){layer.parent=QUuid(parent);if(layer.parent.isNull())fail();}}
      if(root["schema"].toInt()>=2) {
        if(!l["paint_tiles"].isArray())fail();std::set<int> seen;
        for(const auto &tile:l["paint_tiles"].toArray()) {
          const auto t=tile.toObject();const int number=integer(t["number"],1001,2000);
          if(layer.kind!=LayerKind::Paint||!set.tiles.contains(number)||!seen.insert(number).second)fail();
          const auto pages=t["pages"].toArray();if(pages.size()!=5)fail();
          for(const auto &count:pages)page_count(count,0,layer.author_resolution?sparse_texture_page_count(layer.author_resolution):0);
        }
      }
      if(l.contains("material_id")&&!l["material_id"].isNull()){
        if(!l["material_id"].isArray()||l["material_id"].toArray().size()!=3)throw std::invalid_argument("Invalid Material ID color");
        MaterialIdColor color{};for(int c=0;c<3;++c){const auto value=l["material_id"].toArray()[c];if(!value.isDouble()||value.toDouble()!=value.toInt(-1)||value.toInt()<0||value.toInt()>255)throw std::invalid_argument("Invalid Material ID color");color[c]=value.toInt();}layer.material_id=color;
      }
      if(root["schema"].toInt()<79&&!layer.material_id)layer.material_id=next_material_id(set.layers);
      if(l.contains("material_id_folder"))layer.material_id_override_children=boolean(l["material_id_folder"]);
      layer.values=values(l["values"]);layer.channels=channels(l["channels"]);layer.visible=boolean(l["visible"]);if(l.contains("precompose_below"))layer.precompose_below=boolean(l["precompose_below"]);
      if(root["schema"].toInt()<3)layer.opacity.fill(float(number(l["opacity"],0,1)));
      else {
        const auto opacity=l["channel_opacity"].toArray();if(opacity.size()!=5)fail();
        for(int c=0;c<5;++c)layer.opacity[c]=float(number(opacity[c],0,1));
      }
      if(root["schema"].toInt()>=5) {
        const auto modes=l["blending"].toArray();if(modes.size()!=5)fail();
        for(int c=0;c<5;++c){const auto name=text(modes[c],64);const auto found=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *mode){return name==mode;});if(found==layer_blend_names.end())fail();layer.blending[c]=LayerBlend(found-layer_blend_names.begin());}
      }
      if(root["schema"].toInt()>=6&&!l["mask"].isNull()) {
        if(!l["mask"].isObject())fail();const auto mask=l["mask"].toObject();layer.mask=LayerMask{};if(root["schema"].toInt()>=31){layer.mask->effects=read_mask_effects(mask["effects"]);for(const auto &entry:mask_effect_entries(layer.mask->effects))id(entry.effect->id.toString(QUuid::WithoutBraces));}layer.mask->author_resolution=root["schema"].toInt()>=23?integer(mask["author_resolution"],0,max_texture_resolution):(!mask["tiles"].toArray().isEmpty()||mask["strokes"].isObject())?resolution:0;
        layer.mask->background=float(number(mask["background"],0,1));layer.mask->enabled=boolean(mask["enabled"]);
        if(!mask["tiles"].isArray())fail();std::set<int> seen;
        for(const auto &tile:mask["tiles"].toArray()){const auto t=tile.toObject();const int tile_number=integer(t["number"],1001,2000);if(!set.tiles.contains(tile_number)||!seen.insert(tile_number).second)fail();page_count(t["pages"],1,sparse_texture_page_count(layer.mask->author_resolution));}
      }
      if(root["schema"].toInt()>=14){const auto ao=l["ao"].toObject();layer.ao_enabled=boolean(ao["enabled"]);layer.ao_opacity=float(number(ao["opacity"],0,1));const auto name=text(ao["blending"],64);const auto mode=std::find_if(layer_blend_names.begin(),layer_blend_names.end(),[&](const char *value){return name==value;});if(mode==layer_blend_names.end())fail();layer.ao_blending=LayerBlend(mode-layer_blend_names.begin());if(!ao["tiles"].isArray())fail();std::set<int> seen;for(const auto &entry:ao["tiles"].toArray()){const auto tile=entry.toObject();const int number=integer(tile["number"],1001,2000);if(layer.kind!=LayerKind::Paint||!set.tiles.contains(number)||!seen.insert(number).second)fail();page_count(tile["pages"],1,layer.author_resolution?sparse_texture_page_count(layer.author_resolution):0);text(tile["hash"],16);}}
      if(root["schema"].toInt()>=16){layer.strokes=read_stroke_history_json(l["strokes"]);if(layer.kind!=LayerKind::Paint&&layer.strokes)fail();if(layer.mask)layer.mask->strokes=read_stroke_history_json(l["mask"].toObject()["strokes"]);}
      layer.instance=read_layer_instance(l["instance"]);
      layer.geometry=read_geometry_mask(l["geometry_mask"]);
      (void)raw_linear_color_source(layer);
      set.layers.push_back(std::move(layer));
    }
    validate_hierarchy(set);
    if(root["schema"].toInt()>=63)set.material_layering=read_material_layering(o["material_layering"]);validate_material_substacks(set);
    if(!o["selected_geometry"].isUndefined())set.selected_geometry=boolean(o["selected_geometry"]);
    set.selected_layer=QUuid(text(o["selected_layer"],38));layer(set,set.selected_layer);
    if(root["schema"].toInt()>=6){set.selected_mask=boolean(o["selected_mask"]);if(set.selected_mask&&!layer(set,set.selected_layer).mask)fail();}
    if(root["schema"].toInt()>=30){if(!o["selected_effect"].isString())fail();const auto id=o["selected_effect"].toString();if(!id.isEmpty()){set.selected_effect=QUuid(id);const auto &selected=layer(set,set.selected_layer);const bool found=set.selected_mask?selected.mask&&find_mask_effect(selected.mask->effects,set.selected_effect):std::any_of(selected.effects.begin(),selected.effects.end(),[&](const auto &e){return e.id==set.selected_effect;});if(set.selected_effect.isNull()||!found)fail();}}
    visit_effect_paint(set,[&](QUuid,QUuid,bool mask,const EffectPaint &paint){const auto planes=effect_paint_json(paint)["planes"].toArray();for(int i=0;i<planes.size();++i)for(const auto &entry:planes[i].toArray()){const auto t=entry.toObject();if(!set.tiles.contains(t["number"].toInt()))fail();if(mask){if(i!=0)fail();const auto counts=t["pages"].toArray();for(int c=0;c<5;++c)if(c!=2&&counts[c].toInteger())fail();}}});
    if(set.selected_geometry&&(set.selected_mask||!set.selected_effect.isNull()))fail();
    result.sets_.push_back(std::move(set));
  }
  if(root["schema"].toInt()>=15){if(!root["export_configuration"].isObject())fail();result.set_export_configuration(root["export_configuration"].toObject());}
  if(root.contains("bake_configuration")){if(!root["bake_configuration"].isObject())fail();result.set_bake_configuration(root["bake_configuration"].toObject());}
  if(root.contains("reference_image")&&root["reference_image"].isObject()){
    const auto ref=root["reference_image"].toObject();
    ReferenceImageState ref_state;
    ref_state.enabled=ref["enabled"].toBool(false);
    ref_state.path=ref["path"].toString().toStdString();
    if(ref["center"].isArray()&&ref["center"].toArray().size()==2){
      const auto c=ref["center"].toArray();
      if(c[0].isDouble()&&c[1].isDouble()&&std::isfinite(c[0].toDouble())&&std::isfinite(c[1].toDouble()))
        ref_state.center={c[0].toDouble(),c[1].toDouble()};
    }
    if(ref["vertical_extent"].isDouble()&&std::isfinite(ref["vertical_extent"].toDouble())&&ref["vertical_extent"].toDouble()>0.0)
      ref_state.vertical_extent=std::clamp(ref["vertical_extent"].toDouble(),0.01,10000.0);
    set_document_reference_image(result,std::move(ref_state));
  }
  result.set_shader_instances(root["schema"].toInt()>=58||root.contains("shader_instances")?ShaderInstances::from_json(root["shader_instances"]):ShaderInstances::defaults(result.id_,result.shader_set_ids()));
  result.resolve_instances();return result;
}

}

namespace bts {
void TextureDocument::stage_texture_sets(std::vector<TextureSet> sets){std::set<QUuid> ids;std::set<std::uint32_t> uids;bool active=false;for(const auto &set:sets){if(set.id.isNull()||!set.material_session_uid||!ids.insert(set.id).second||!uids.insert(set.material_session_uid).second)throw std::invalid_argument("Invalid Texture Set assignment");active|=!set.disabled;}if(!active)throw std::invalid_argument("No active Texture Set");sets_=std::move(sets);auto shaders=shader_instances_;shaders.reconcile(shader_set_ids());set_shader_instances(std::move(shaders));}
}

namespace bts {
QUuid TextureDocument::add_instance(TextureSet &target,const QUuid &source_set,const QUuid &source_layer,const std::string &source_name){
  if(source_set.isNull()||source_layer.isNull())throw std::invalid_argument("Missing instance source identity");const auto retained_name=source_name;add_fill(target);auto &created=layer(target,target.selected_layer);created.instance=LayerInstance{source_set,source_layer};created.name=(QString::fromStdString(retained_name)+" instance").toStdString();for(int slot=0;slot<custom_channel_count;++slot)created.users[slot].enabled=target.users[slot].present;created.blending.fill(LayerBlend::Passthrough);created.ao_blending=LayerBlend::Passthrough;created.displacement.blending=LayerBlend::Passthrough;for(auto &user:created.users)user.blending=LayerBlend::Passthrough;return created.id;
}
void TextureDocument::resolve_instances() const{
  const ResourceAvailability budget;
  using Key=std::pair<QUuid,QUuid>;
  struct Expansion {std::vector<TextureLayer> layers;QString problem;};
  const auto find_set=[&](const QUuid &id)->const TextureSet *{for(const auto &set:sets_)if(set.id==id)return &set;return nullptr;};
  std::map<QUuid,std::shared_ptr<const std::vector<TextureLayer>>> previous_evaluation;
  for(const auto &set:sets_){if(set.instance_layers)previous_evaluation.emplace(set.id,set.instance_layers);set.instance_layers.reset();set.instance_problems.clear();set.instance_hierarchies.clear();}
  const auto disabled=[](const TextureLayer &source){auto copy=source;copy.kind=LayerKind::Paint;copy.visible=false;copy.pixels={};copy.ao_pixels={};copy.displacement.pixels={};copy.displacement.strokes.reset();copy.displacement.author_resolution=0;copy.effects.clear();copy.mask.reset();copy.instance.reset();for(auto &user:copy.users)user.pixels={};return copy;};
  const auto transplant=[&](Expansion &expansion,const TextureLayer &instance){
    if(expansion.layers.empty())return;const auto root=expansion.layers.back();const int offset=instance.depth-root.depth;
    std::map<QUuid,QUuid> ids,effects;for(const auto &copy:expansion.layers){ids[copy.id]=copy.id==root.id?instance.id:QUuid::createUuidV5(instance.id,copy.id.toByteArray());for(const auto &effect:copy.effects)effects[effect.id]=QUuid::createUuidV5(instance.id,effect.id.toByteArray());if(copy.mask)for(const auto &entry:mask_effect_entries(copy.mask->effects))effects[entry.effect->id]=QUuid::createUuidV5(instance.id,entry.effect->id.toByteArray());}
    for(auto &copy:expansion.layers){const auto old=copy.id;copy.id=ids.at(old);copy.parent=old==root.id?instance.parent:ids.at(copy.parent);copy.depth+=offset;copy.instance.reset();if(copy.depth<0){expansion.problem="Invalid negative instance hierarchy depth";return;}for(auto &ref:copy.fill.anchors)if(!ref.source.isNull()&&effects.contains(ref.source))ref.source=effects.at(ref.source);for(auto &effect:copy.effects){effect.id=effects.at(effect.id);for(auto &ref:effect.input_anchors)if(effects.contains(ref.source))ref.source=effects.at(ref.source);}if(copy.mask)for(auto *effect:edit_mask_effects(copy.mask->effects)){effect->id=effects.at(effect->id);effect->levels.id=effect->id;for(auto &ref:effect->input_anchors)if(effects.contains(ref.source))ref.source=effects.at(ref.source);if(!effect->anchor_source.isNull()&&effects.contains(effect->anchor_source))effect->anchor_source=effects.at(effect->anchor_source);}}
    auto &result=expansion.layers.back();result.name=instance.name;result.visible=result.visible&&instance.visible;for(int c=0;c<5;++c){result.opacity[c]*=instance.opacity[c];result.channels[c]=result.channels[c]&&instance.channels[c];}result.ao_opacity*=instance.ao_opacity;result.ao_enabled=result.ao_enabled&&instance.ao_enabled;for(int c=0;c<custom_channel_count;++c){result.users[c].opacity*=instance.users[c].opacity;result.users[c].enabled=result.users[c].enabled&&instance.users[c].enabled;}
  };
  std::function<Expansion(const TextureSet &,const QUuid &,const QUuid &,std::vector<Key>)> expand;
  expand=[&](const TextureSet &source,const QUuid &root_id,const QUuid &destination,std::vector<Key> stack)->Expansion{
    const Key key{source.id,root_id};if(std::find(stack.begin(),stack.end(),key)!=stack.end()){for(const auto &[set_id,layer_id]:stack)if(const auto *owner=find_set(set_id))owner->instance_problems[layer_id]="Cyclic layer instance dependency";return {{},"Cyclic layer instance dependency"};}
    require_resource_stack(sizeof(TextureLayer)*4,"Layer instance evaluation");stack.push_back(key);const auto found=std::find_if(source.layers.begin(),source.layers.end(),[&](const auto &layer){return layer.id==root_id;});if(found==source.layers.end()||source.disabled)return {{},"Instance source is unavailable"};
    const int root=int(found-source.layers.begin());int first=root;if(found->kind==LayerKind::Folder)while(first>0&&source.layers[first-1].depth>found->depth)--first;Expansion result;
    for(int i=first;i<=root;++i){const auto &original=source.layers[i];if(original.instance){const auto *owner=find_set(original.instance->source_set);if(!owner)return {{},"Instance source Texture Set is unavailable"};auto nested=expand(*owner,original.instance->source_layer,destination,stack);if(!nested.problem.isEmpty())return nested;transplant(nested,original);if(!nested.problem.isEmpty())return nested;result.layers.insert(result.layers.end(),nested.layers.begin(),nested.layers.end());}
      else{auto copy=original;if(source.id!=destination){copy.pixels={};copy.ao_pixels={};copy.strokes.reset();copy.displacement.pixels={};copy.displacement.strokes.reset();copy.displacement.author_resolution=0;for(auto &user:copy.users){user.pixels={};user.strokes.reset();}std::erase_if(copy.effects,[](const auto &effect){return effect.kind==ContentEffectKind::Paint;});if(copy.mask){copy.mask->pixels={};copy.mask->strokes.reset();erase_mask_effects_of_kind(copy.mask->effects,MaskEffectKind::Paint);}}result.layers.push_back(std::move(copy));}

    }return result;
  };
  for(const auto &set:sets_){if(std::none_of(set.layers.begin(),set.layers.end(),[](const auto &layer){return layer.instance.has_value();}))continue;auto evaluated=std::make_shared<std::vector<TextureLayer>>();
    for(const auto &original:set.layers){if(!original.instance){evaluated->push_back(original);continue;}const auto *source=find_set(original.instance->source_set);Expansion expanded=source?expand(*source,original.instance->source_layer,set.id,{}):Expansion{{},"Instance source Texture Set is unavailable"};if(expanded.problem.isEmpty())transplant(expanded,original);if(!expanded.problem.isEmpty()){set.instance_problems[original.id]=expanded.problem;evaluated->push_back(disabled(original));}else evaluated->insert(evaluated->end(),expanded.layers.begin(),expanded.layers.end());}
    // Preserve consumer-specific derived fits across resolution of unchanged
    // instances. Native owner code still checks its geometry generation before
    // using them; workers/export/flatten may then read the immutable fitted grid.
    if(const auto previous=previous_evaluation.find(set.id);previous!=previous_evaluation.end()){
      std::map<QUuid,const TextureLayer *> old_layers;for(const auto &layer:*previous->second)old_layers[layer.id]=&layer;
      const auto preserve=[&](const FillProjection &next,const FillProjection &old){if(!warp_geometry_enabled(next)||!old.warp_fit||old.warp_fit->material!=set.material_session_uid)return;auto candidate=next;candidate.cm_per_native=set.cm_per_native;candidate.native_per_uv=set.native_per_uv;candidate.warp_fit=old.warp_fit;if(warp_fit_pose_matches(candidate))next.warp_fit=old.warp_fit;};
      for(const auto &layer:*evaluated)if(const auto found=old_layers.find(layer.id);found!=old_layers.end()){const auto &old=*found->second;preserve(layer.fill,old.fill);if(layer.mask&&old.mask){std::map<QUuid,const MaskEffect *> effects;for(const auto &entry:mask_effect_entries(old.mask->effects))effects[entry.effect->id]=entry.effect;for(const auto &entry:mask_effect_entries(layer.mask->effects)){const auto &effect=*entry.effect;if(const auto prior=effects.find(effect.id);prior!=effects.end()&&effect.kind==MaskEffectKind::Fill&&prior->second->kind==MaskEffectKind::Fill)preserve(effect.mapping.projection,prior->second->mapping.projection);}}}
    }
    set.instance_layers=std::move(evaluated);
  }
  std::map<Key,std::vector<Key>> consumers;for(const auto &set:sets_)for(const auto &layer:set.layers)if(layer.instance)consumers[{layer.instance->source_set,layer.instance->source_layer}].push_back({set.id,layer.id});
  for(const auto &set:sets_)for(const auto &layer:set.layers){const Key selected{set.id,layer.id};if(!layer.instance&&!consumers.contains(selected))continue;Key root=selected;std::set<Key> seen;
    while(seen.insert(root).second){const auto *owner=find_set(root.first);if(!owner)break;const auto it=std::find_if(owner->layers.begin(),owner->layers.end(),[&](const auto &l){return l.id==root.second;});if(it==owner->layers.end()||!it->instance)break;root={it->instance->source_set,it->instance->source_layer};}
    std::vector<InstanceHierarchyNode> nodes;std::set<Key> visited;std::function<void(Key,int)> append=[&](Key key,int depth){if(!visited.insert(key).second)return;require_resource_stack(sizeof(InstanceHierarchyNode)*4,"Instance hierarchy UI");const auto *owner=find_set(key.first);QString label="Missing source";if(owner){const auto it=std::find_if(owner->layers.begin(),owner->layers.end(),[&](const auto &l){return l.id==key.second;});if(it!=owner->layers.end())label=QString::fromStdString(owner->name+" / "+it->name);}nodes.push_back({key.first,key.second,label,depth});if(const auto children=consumers.find(key);children!=consumers.end())for(const auto &child:children->second)append(child,depth+1);};append(root,0);set.instance_hierarchies[layer.id]=std::move(nodes);
  }

}
}
