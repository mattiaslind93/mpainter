#pragma once
#include "LayerInstance.hh"
#include "MaterialId.hh"
#include "ShaderInstances.hh"
#include "MaterialLayering.hh"
#include "SkewField.hh"
#include "ProjectSettings.hh"
#include "GeometryMask.hh"
#include "CustomChannel.hh"
#include "Displacement.hh"
#include "ChannelPrecision.hh"
#include "LayerBlend.hh"
#include "LayerMask.hh"
#include "MaskEffectPath.hh"
#include "LayerEffect.hh"
#include "StrokeHistory.hh"
#include "FillProjection.hh"
#include "MeshMaps.hh"
#include <optional>
#include <type_traits>
#include <stdexcept>
#include <utility>
#include "TilePaintPixels.hh"
#include "MeshImport.hh"
#include "SceneSnapshot.hh"
#include "QuickMaskOverlay.hh"
#include <array>
#include <vector>
#include <QUuid>
#include <QJsonObject>
namespace bts {
inline constexpr int texture_document_schema=79;
enum class AoMixing { Multiply,Replace };
enum class BumpMethod { SmoothSobel,Sharp };
enum class NormalMixing { Combine,Replace };
enum class LayerKind { Paint, Fill, Folder };
struct TextureLayer {
  std::shared_ptr<const QuickMaskOverlay> quick_overlay; // Session preview only, never serialized.
  std::optional<LayerInstance> instance;
  GeometryMask geometry;
  std::vector<LevelsEffect> effects;
  std::array<CustomLayer,custom_channel_count> users;
  CustomLayer displacement=[] {CustomLayer value;value.enabled=false;value.value=0;return value;}();
  int author_resolution=0;
  LayerKind kind = LayerKind::Paint;
  FillProjection fill;
  MaterialValues values=new_base_values();
  QUuid parent;
  int depth=0; // Validated postorder: children immediately precede their folder.
  std::array<bool, 5> channels{true,true,true,true,true};
  QUuid id = QUuid::createUuid();
  std::string name = "Layer 1";
  bool visible = true;
  std::optional<MaterialIdColor> material_id;
  bool material_id_override_children=false;
  bool precompose_below = false;
  std::array<float,5> opacity{1,1,1,1,1};
  std::array<LayerBlend,5> blending{LayerBlend::Normal,LayerBlend::Normal,LayerBlend::Normal,LayerBlend::NormalDetail,LayerBlend::Normal};
  TilePaintPixels pixels;
  std::shared_ptr<const StrokeHistory> strokes;
  TilePaintPixels ao_pixels; // Scalar channel2, separate from the five legacy material channels.
  bool ao_enabled=true;float ao_opacity=1;LayerBlend ao_blending=LayerBlend::Normal;
  std::optional<LayerMask> mask;
};
// A typed boundary marks an explicitly interpreted raw linear Color source.
// It never changes the shared pixel descriptor or scalar channel interpretation.
inline const LevelsEffect *raw_linear_color_source(const TextureLayer &layer){
  const LevelsEffect *boundary=nullptr;
  for(const auto &effect:layer.effects){
    if(effect.kind==ContentEffectKind::LinearColorTransform){
      if(boundary||effect.channel!=0||!effect.linear_color||effect.opacity!=1||effect.blending!=LayerBlend::Normal)
        throw std::invalid_argument("Invalid raw linear Color boundary");
      effect.linear_color->validate();boundary=&effect;
    }
  }
  if(!boundary)return nullptr;
  if(layer.blending[0]==LayerBlend::Passthrough)
    throw std::invalid_argument("Raw linear Color cannot pass through a canonical backdrop; choose a Color blend after conversion");
  if(layer.kind!=LayerKind::Fill||!layer.fill.texture||!layer.fill.texture->raw||layer.fill.alpha_mask[0]||
     layer.fill.atom||layer.fill.native_material||!layer.fill.material_resource.empty()||!layer.fill.anchors[0].source.isNull())
    throw std::invalid_argument("Linear Color requires a raw RGB Fill with explicit ignored alpha");
  for(const auto &effect:layer.effects){
    if(&effect==boundary)break;
    if(effect.enabled&&(effect.kind==ContentEffectKind::Paint||effect.kind==ContentEffectKind::Anchor||
       (effect.channel==0&&effect.kind!=ContentEffectKind::Levels&&!(effect.kind==ContentEffectKind::Fill&&effect.initialize_content&&effect.source_coverage))))
      throw std::invalid_argument("Only source-space Color Levels may precede the linear Color boundary");
  }
  return boundary;
}
struct InstanceHierarchyNode {QUuid set,layer;QString label;int depth=0;};
struct TextureSet {
  ShaderParameters shader_parameters; // Runtime mirror; persistent identity lives in the document registry.
  mutable std::map<QUuid,std::vector<InstanceHierarchyNode>> instance_hierarchies;
  mutable std::shared_ptr<const std::vector<TextureLayer>> instance_layers;
  mutable std::map<QUuid,QString> instance_problems;
  mutable std::map<std::uint32_t,std::string> geometry_names;
  mutable std::map<int,std::shared_ptr<const GeometryOwnershipTile>> geometry_ownership;
  bool selected_geometry=false;
  std::array<CustomChannel,custom_channel_count> users=default_custom_channels();
  QUuid id = QUuid::createUuid();
  std::uint32_t material_session_uid = 0; // Runtime binding only, not a persistent identity.
  std::string name;
  bool disabled=false;
  DisplacementSettings displacement;
  SkewField skew;
  mutable std::array<double,2> native_per_uv{1,1};
  double cm_per_native=1;
  bool tangent_per_fragment=false;
  std::set<int> tiles{1001};
  MeshMaps mesh_maps;
  bool padding_3d=true;
  bool ao_channel=false;AoMixing ao_mixing=AoMixing::Multiply;
  NormalMixing normal_mixing=NormalMixing::Combine;
  BumpMethod bump_method=BumpMethod::SmoothSobel;
  NormalMapFormat normal_format=NormalMapFormat::OpenGL; // Runtime mirror of document format.
  mutable std::shared_ptr<const FillSurfaceAtlas> projection_geometry;
  mutable std::map<int,std::shared_ptr<const FillSurfaceAtlas>> projection_tiles;
  std::map<int,int> tile_resolutions;
  std::array<ChannelPrecision,6> precision=default_channel_precision;
  int texture_resolution=512,texture_height=0;
  bool size_linked=true;
  int width(int tile=0) const {const auto found=tile_resolutions.find(tile);return found==tile_resolutions.end()?texture_resolution:found->second;}
  int rows(int tile=0) const {const auto found=tile_resolutions.find(tile);return found==tile_resolutions.end()?(texture_height?texture_height:texture_resolution):found->second;}
  std::uint64_t evaluation_texels() const {std::uint64_t count=0;for(int tile:tiles)count+=std::uint64_t(width(tile))*rows(tile);return count;}
  // Private compatibility coordinate: old stacks retain their signed-zero data,
  // filter/blend arithmetic and history. Artist values and final maps always
  // use 0.5 neutral. This is not a user-selectable Bump midlevel.
  float bump_origin=.5f;
  float bump_offset()const{return .5f-bump_origin;}
  float bump_display(float stored)const{return stored+bump_offset();}
  float bump_storage(float displayed)const{return displayed-bump_offset();}
  MaterialValues base_values=new_base_values();
  bool uniform_editable = false;
  std::array<bool, 5> channels{true, true, true, true, true};
  std::vector<TextureLayer> layers{[] {TextureLayer layer;layer.material_id=material_id_palette[0];return layer;}()};
  QUuid selected_layer = layers.front().id;
  bool selected_mask=false;
  QUuid selected_effect;
  int next_folder_number = 1;
  int next_fill_number = 1;
  int next_paint_number = 2;
  MaterialLayering material_layering;
};
inline const std::vector<TextureLayer> &evaluation_layers(const TextureSet &set){return set.instance_layers?*set.instance_layers:set.layers;}
template<class Set,class Visitor> void visit_effect_paint(Set &set,Visitor &&visit){
  for(auto &layer:set.layers){
    for(auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Paint)visit(layer.id,effect.id,false,effect.paint);
    if(!layer.mask)continue;
    if constexpr(std::is_const_v<Set>||std::is_invocable_v<Visitor,QUuid,QUuid,bool,const EffectPaint &>){
      for(const auto &entry:mask_effect_entries(std::as_const(layer.mask->effects)))if(entry.effect->kind==MaskEffectKind::Paint)visit(layer.id,entry.effect->id,true,entry.effect->paint);
    }else{
      for(auto *effect:edit_mask_effects_of_kind(layer.mask->effects,MaskEffectKind::Paint))visit(layer.id,effect->id,true,effect->paint);
    }
  }
}
struct UniformComposite {
  MaterialValues material;
  MaterialValues authored;
  std::array<float, 5> coverage{};
  float ao_coverage=0;
};
struct MaskAnchorInfo {QUuid layer,id;QString name;int layer_index=0;bool enabled=true,mask=true;};
struct TextureSetBinding { QUuid set_id;std::uint32_t material_uid;std::string name; };
// Only a visible top-level cache can replace a complete stack prefix.
inline int precompose_start(const std::vector<TextureLayer> &layers){
 for(int i=int(layers.size())-1;i>=0;--i)
  if(layers[i].visible&&layers[i].precompose_below&&!layers[i].instance&&layers[i].depth==0)return i;
 return 0;
}
inline bool layer_is_cached(const TextureSet &set,const QUuid &id){
 const int first=precompose_start(set.layers);
 for(int i=0;i<first;++i)if(set.layers[i].id==id)return true;
 return false;
}
inline void require_uncached_layer(const TextureSet &set,const QUuid &id){
 if(layer_is_cached(set,id))throw std::invalid_argument("Layer is cached by Pre-compose. Hide the cache or choose Unfreeze / Release Pre-compose to edit it.");
}
// Queries into a frozen prefix must still see its original live data.
inline int precompose_evaluation_start(const std::vector<TextureLayer> &layers,const QUuid *stop=nullptr,int target=-1,const QUuid *query=nullptr){
 const int first=precompose_start(layers);
 if(target>=0&&target<first)return 0;
 for(int i=0;i<first;++i){const auto &l=layers[i];if(query&&l.id==*query)return 0;
  if(stop){for(const auto &e:l.effects)if(e.id==*stop)return 0;
   if(l.mask)for(const auto &e:mask_effects_recursive(l.mask->effects))if(e.id==*stop)return 0;}
 }
 return first;
}
class TextureDocument {
 public:
  explicit TextureDocument(const NewProjectOptions &, const ImportSummary &, const SceneSnapshot &);
  TextureSet &set(std::uint32_t material_uid);
  static MaterialValues output(const TextureSet &);
  static int authoring_resolution(const TextureSet &,const TextureLayer &,bool mask=false);
  static void set_resolution(TextureSet &,int);
  static void add_scattering_channel(TextureSet &);
  static void add_user_channel(TextureSet &,int);
  static void rename_user_channel(TextureSet &,int,const std::string &);
  static std::array<float,2> composite_user(const TextureSet &,int slot,int x,int y,int tile=1001,const QUuid *stop=nullptr,std::array<float,3> *input=nullptr);
  static void add_displacement_channel(TextureSet &);
  static std::array<float,4> fill_sample(const TextureSet &,const TextureLayer &,int channel,int x,int y,int tile=1001);
  static float projection_coverage(const TextureSet &,const TextureLayer &,int x,int y,int tile);
  static std::array<float,2> composite_displacement(const TextureSet &,int x=-1,int y=-1,int tile=1001,const QUuid *stop=nullptr,std::array<float,3> *input=nullptr);
  static float displacement_value(const TextureSet &set,int x,int y,int tile=1001){const auto value=composite_displacement(set,x,y,tile);return set.displacement.midlevel*(1-value[1])+value[0]*value[1];}
  static void set_dimensions(TextureSet &,int width,int height,bool linked);
  static void set_tile_resolution(TextureSet &,int tile,int resolution);
  static std::array<float,3> material_id_pixel(const TextureSet &,int x,int y,int tile=1001);
  static UniformComposite composite(const TextureSet &, int x=-1, int y=-1,int tile=1001);
  static std::array<float,3> effect_input(const TextureSet &,const QUuid &,int x,int y,int tile=1001);
  static UniformComposite evaluate_channels(const TextureSet &,const std::array<bool,6> &,int x,int y,int tile=1001);
  static UniformComposite evaluate(const TextureSet &,int x=-1,int y=-1,int tile=1001);
  static bool has_pixels(const TextureSet &);
  static TextureLayer &layer(TextureSet &, const QUuid &);
  static void refresh_geometry_names(const TextureSet &,const SceneSnapshot &);
  static bool geometry_includes(const TextureSet &,const TextureLayer &,int x,int y,int tile=1001);
  static void set_geometry_mesh(TextureSet &,const QUuid &,const std::string &,bool);
  static void set_geometry_all(TextureSet &,const QUuid &,bool);
  static const MaterialValues &property_values(const TextureSet &);
  static MaterialValues &edit_values(TextureSet &);
  static void add_fill(TextureSet &);
  static QUuid add_instance(TextureSet &,const QUuid &source_set,const QUuid &source_layer,const std::string &source_name);
  void resolve_instances() const;
  static void add_paint(TextureSet &);
  static void add_folder(TextureSet &);
  static void add_mask(TextureSet &,float background);
  static void move_effect(TextureSet &,int offset);
  static EffectPaint *paint_effect(TextureSet &);
  static const EffectPaint *paint_effect(const TextureSet &);
  static void add_paint_effect(TextureSet &);
  static void add_filter_effect(TextureSet &,int channel=0);
  static void add_compare_mask(TextureSet &);
  static AnchorSample anchor_sample(const AnchorSampleContext &,const AnchorReference &);
  static std::array<float,4> content_anchor_source(const TextureSet &,const QUuid &,int,int,int,int=1001);
  static std::array<float,2> scalar_anchor_source(const TextureSet &,const QUuid &,const QUuid &,int,int,int,int=1001);
  static QString anchor_reference_problem(const TextureSet &,const QUuid &,const AnchorReference &);
  static QString mask_effect_anchor_reference_problem(const TextureSet &,const QUuid &consumer,const QUuid &effect,const AnchorReference &);
  static void assign_fill_anchor(TextureSet &,int,const AnchorReference &,const QUuid &source_set={});
  static bool anchor_dependent(const TextureLayer &);
  static void assign_mask_anchor_reference(TextureSet &,const QUuid &,const AnchorReference &,const QUuid &source_set={});
  static void add_mask_anchor(TextureSet &);
  static std::vector<MaskAnchorInfo> mask_anchors(const TextureSet &);
  static std::optional<MaskAnchorInfo> anchor_point(const TextureSet &,const QUuid &);
  static QString mask_anchor_problem(const TextureSet &,const QUuid &consumer,const QUuid &source);
  static void assign_mask_anchor(TextureSet &,const QUuid &effect,const QUuid &source,const QUuid &source_set={});
  static void rename_mask_anchor(TextureSet &,const QUuid &,const QString &);
  static float mask_value(const TextureSet &,const QUuid &,int x,int y,int tile=1001);
  static MaskCompareInputs comparison_inputs(const TextureSet &,const QUuid &,int x,int y,int tile=1001);
  static QByteArray mask_dependency_signature(const TextureSet &);
  static void apply_mask_preset(TextureSet &,const QUuid &,const LayerMask &,bool replace);
  static LayerMask copy_mask(const TextureSet &,const QUuid &);
  static void paste_mask(TextureSet &,const QUuid &,const LayerMask &);
  static void group_layers(TextureSet &,const std::vector<QUuid> &);
  static void delete_layers(TextureSet &,const std::vector<QUuid> &);
  static void remove_effects(TextureSet &,const std::vector<std::pair<QUuid,QUuid>> &);
  static void remove_masks(TextureSet &,const std::vector<QUuid> &);
  static void move_layer_relative(TextureSet &,const QUuid &,const QUuid &target,bool above);
  static void validate_hierarchy(TextureSet &);
  static void move_layer(TextureSet &, const QUuid &, int offset);
  static void move_layer_to(TextureSet &,const QUuid &,int target);
  const ShaderInstances &shader_instances()const{return shader_instances_;}
  void set_shader_instances(ShaderInstances);
  void preserve_shader_instances(ShaderInstances);
  const QJsonObject &bake_configuration() const {return bake_configuration_;}
  void set_bake_configuration(const QJsonObject &);
  const QJsonObject &export_configuration() const {return export_configuration_;}
  void set_export_configuration(const QJsonObject &);
  QJsonObject to_json() const;
  QJsonObject edit_state() const; // Structural metadata/COW identity; never file data.
  static TextureDocument from_json(const QJsonObject &,std::span<const TextureSetBinding>);
  const std::vector<TextureSet> &texture_sets() const { return sets_; }
  void set_units(const UnitSettings &);
  void set_source_units(const SourceUnits &);
  void set_usd_options(const UsdImportOptions &options){settings_.usd=options;}
  void set_blend_options(const BlendMeshSelection &options){options.validate();settings_.blend=options;}
  void set_auto_unwrap(const AutoUnwrapOptions &options){settings_.auto_unwrap=options;}
  void set_import_cameras(bool enabled){settings_.import_cameras=enabled;}
  void set_tangent_per_fragment(bool enabled){settings_.tangent_per_fragment=enabled;for(auto &set:sets_)set.tangent_per_fragment=enabled;}
  void set_normal_map_format(NormalMapFormat format){if(format!=NormalMapFormat::OpenGL&&format!=NormalMapFormat::DirectX)throw std::invalid_argument("Invalid normal convention");settings_.normal_map_format=format;for(auto &set:sets_)set.normal_format=format;}
  void replace_mesh_source(const std::string &path){settings_.absolute_mesh_path=path;for(auto &set:sets_)set.projection_geometry.reset();}
  void stage_texture_sets(std::vector<TextureSet> sets);
  const QUuid &id() const { return id_; }
  const NewProjectOptions &settings() const { return settings_; }
 private:
  TextureDocument() = default;
  QUuid id_ = QUuid::createUuid();
  NewProjectOptions settings_;
  ShaderInstances shader_instances_;
  std::set<QUuid> shader_set_ids()const{std::set<QUuid> ids;for(const auto &set:sets_)ids.insert(set.id);return ids;}
  QJsonObject export_configuration_,bake_configuration_;
  std::vector<TextureSet> sets_;
};
}
