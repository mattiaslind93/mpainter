#include "SpsmMaterialImport.hh"
#include "SpsmNativeSemantics.hh"
#include "ResourceBudget.hh"
#include <cmath>
#include <limits>
#include <map>

namespace bts { namespace {
using namespace spsm;
namespace sem = spsm::semantic;
constexpr std::array<std::uint64_t,6> channel_bits{1,8192,128,4194304,2,8388608};
struct Budget {
  std::uint64_t used=0,limit;
  explicit Budget(SpsmImportOptions o):limit(o.memory_budget_bytes?
    std::min(o.memory_budget_bytes,ResourceBudget{}.ram()):ResourceBudget{}.ram()){}
  void add(std::uint64_t n){used=resource_sum(used,n);if(used>limit)throw Error("SPSM native preparation exceeds available RAM budget");}
  template<class T> void items(std::size_t n=1){add(resource_product(resource_product(n,sizeof(T)),2));}
};
struct Compiler {
  PreparedSpsm result;
  Budget budget;
  std::size_t pi;
  const Preset &p;
  const sem::PresetInspection &ir;
  std::map<NodeId,const sem::Source *> sources;
  std::vector<QUuid> ids;
  std::vector<unsigned char> seen;
  std::vector<std::size_t> action_todo;
  std::vector<unsigned char> action_seen;
  SpsmImportOptions options;
  explicit Compiler(sem::Inspection &&inspection,SpsmImportOptions options):
    result(),budget(options),pi(inspection.primary_preset.value_or(std::numeric_limits<std::size_t>::max())),
    p(check_primary(inspection,pi)),ir(inspection.presets.at(pi)),options(std::move(options)) {
    budget.items<PreparedSpsm>();budget.items<sem::Inspection>();
    result.source=std::make_shared<const sem::Inspection>(std::move(inspection));
    budget.items<TextureLayer>(ir.layers.size());budget.items<SpsmLayerBinding>(ir.layers.size());
    budget.items<QUuid>(ir.layers.size());budget.items<unsigned char>(ir.layers.size());
    budget.items<SpsmActionBinding>(ir.actions.size());
    budget.items<std::size_t>(ir.actions.size());action_todo.reserve(ir.actions.size());
    budget.items<unsigned char>(ir.actions.size());action_seen.resize(ir.actions.size());
    ids.resize(ir.layers.size());seen.resize(ir.layers.size());
    result.layers.reserve(ir.layers.size());result.actions.reserve(ir.actions.size());
    for(auto &id:ids)id=QUuid::createUuid();
    for(const auto &s:ir.sources){budget.add(sizeof(std::pair<const NodeId,const sem::Source *>)+4*sizeof(void*));sources.emplace(s.source,&s);}
    result.material.set.layers.clear();result.material.set.bump_origin=.5f;
    result.material.set.layers.reserve(ir.layers.size());
    result.material.set.displacement.channel=false;result.material.set.ao_channel=false;result.material.set.channels[3]=false;
  }
  static const Preset &check_primary(const sem::Inspection &i,std::size_t pi){
    if(pi>=i.source().presets.size()||pi>=i.presets.size())throw Error("SPSM has no primary /preset.bin stream");
    if(i.presets[pi].roots.size()!=1)throw Error("SPSM primary stream needs one known layer root");
    return i.source().presets[pi];
  }
  const Node &node(NodeId id)const{if(id>=p.nodes.size())throw Error("Invalid SPSM preparation node");return p.nodes[id];}
  std::optional<NodeId> field(NodeId id,std::string_view name)const{
    if(const auto *o=std::get_if<Object>(&node(id).value))for(const auto &f:o->fields)if(f.name==name)return f.value;return {};
  }
  std::optional<double> number(std::optional<NodeId> id)const{
    if(!id)return {};const auto &v=node(*id).value;
    if(const auto *x=std::get_if<FloatValues>(&v);x&&x->count==1)return x->values[0];
    if(const auto *x=std::get_if<IntValues>(&v);x&&x->count==1)return x->values[0];
    if(const auto *x=std::get_if<std::uint64_t>(&v))return double(*x);
    if(const auto *x=std::get_if<std::int64_t>(&v))return double(*x);return {};
  }
  std::optional<std::uint64_t> bits(std::optional<NodeId> id)const{
    if(!id)return {};const auto &v=node(*id).value;
    if(const auto *x=std::get_if<std::uint64_t>(&v))return *x;
    if(const auto *x=std::get_if<std::int64_t>(&v);x&&*x>=0)return std::uint64_t(*x);
    if(const auto *x=std::get_if<IntValues>(&v);x&&x->count==1&&x->values[0]>=0)return std::uint64_t(x->values[0]);return {};
  }
  bool boolean(std::optional<NodeId> id,bool fallback)const{
    if(!id)return fallback;if(const auto *v=std::get_if<bool>(&node(*id).value))return *v;
    throw Error("SPSM boolean has an invalid type");
  }
  float unit(std::optional<NodeId> id,float fallback)const{
    if(!id)return fallback;const auto v=number(id);if(!v||!std::isfinite(*v)||*v<0||*v>1)throw Error("SPSM opacity/mask value is outside [0,1]");return float(*v);
  }
  void diagnostic(NodeId id,SpsmDiagnosticKind kind,std::string_view code,std::string_view detail,std::optional<std::size_t> preset={}){
    budget.items<SpsmImportDiagnostic>();result.diagnostics.push_back({preset.value_or(pi),id,kind,code,detail});
    if(kind!=SpsmDiagnosticKind::SourceMetadata)result.partial=true;
  }
  void omit(NodeId id,std::string_view code,std::string_view detail){diagnostic(id,SpsmDiagnosticKind::Omission,code,detail);}
  void assumption(NodeId id,std::string_view code,std::string_view detail){diagnostic(id,SpsmDiagnosticKind::Assumption,code,detail);}
  static bool enabled_channel(const TextureLayer &l,int c){return c==5?l.ao_enabled:l.channels.at(c);}
  static void enable(TextureLayer &l,int c,bool yes){if(c==5)l.ao_enabled=yes;else l.channels.at(c)=yes;}
  static float &opacity(TextureLayer &l,int c){return c==5?l.ao_opacity:l.opacity.at(c);}
  static LayerBlend &blend(TextureLayer &l,int c){return c==5?l.ao_blending:l.blending.at(c);}
  std::optional<LayerBlend> mapped_blend(std::uint64_t mode,int channel,bool folder){
    // Named public Shader API constants; their identity with DataBlending is an
    // explicit inference, never a cast between source/native ordinals.
    if(channel==4){switch(mode){case 0:return LayerBlend::Disabled;case 1:return LayerBlend::Replace;case 2:return LayerBlend::Normal;case 10:return LayerBlend::SignedAddition;case 25:if(folder)return LayerBlend::Passthrough;return {};default:return {};}}
    if(channel!=0&&mode>=19&&mode<=22)return {}; // Color-component operations require RGB.
    switch(mode){case 0:return LayerBlend::Disabled;case 1:return LayerBlend::Replace;case 2:return LayerBlend::Normal;case 3:return LayerBlend::Darken;case 4:return LayerBlend::Multiply;case 7:return LayerBlend::Lighten;case 8:return LayerBlend::Screen;case 10:return LayerBlend::LinearDodge;case 11:return LayerBlend::Overlay;case 12:return LayerBlend::SoftLight;case 13:return LayerBlend::HardLight;case 14:return LayerBlend::VividLight;case 15:return LayerBlend::LinearLight;case 16:return LayerBlend::PinLight;case 17:return LayerBlend::Difference;case 18:return LayerBlend::Exclusion;case 19:return LayerBlend::Tint;case 20:return LayerBlend::Saturation;case 21:return LayerBlend::Color;case 22:return LayerBlend::Value;case 23:return LayerBlend::Divide;case 24:return LayerBlend::InverseDivide;case 25:if(folder)return LayerBlend::Passthrough;return {};case 29:return LayerBlend::Subtract;case 30:return LayerBlend::InverseSubtract;case 31:return LayerBlend::SignedAddition;default:return {};}
  }
  bool blending(const std::vector<sem::Blending> &rows,int c,float &op,LayerBlend &mode,bool folder,bool initial_fill=false){
    bool okay=true,found=false;
    for(const auto &row:rows){const auto channels=bits(row.channel_types);if(!channels){omit(row.source,"invalid-blend-channels","Blend channel mask cannot be interpreted");okay=false;continue;}if(!(*channels&channel_bits[c]))continue;
      if(found){omit(row.source,"duplicate-blend-channel","Overlapping blend rows are not collapsed");okay=false;}found=true;
      const auto raw=bits(row.source_mode);const auto mapped=raw?mapped_blend(*raw,c,folder):std::nullopt;
      const bool identity=raw&&(*raw==1||*raw==2||(c==4&&*raw==10)||(c==5&&*raw==4));
      if(!mapped||(initial_fill&&!identity)){omit(row.source,"unsupported-blend-mode","Source blend mode has no supported native/initial-fill contract for this channel");okay=false;}else mode=*mapped;
      op=unit(row.opacity,1);
    }
    return okay;
  }
  void layer_blending(const sem::Layer &src,TextureLayer &out){
    for(int c=0;c<6;++c){float op=1;auto mode=LayerBlend::Normal;if(!blending(src.blending,c,op,mode,src.group))enable(out,c,false);else{opacity(out,c)=op;blend(out,c)=mode;if(mode==LayerBlend::Disabled)enable(out,c,false);}}
  }
  void skip_action(std::size_t index,const QUuid &layer,std::string_view code,std::string_view reason){
    action_todo.clear();action_todo.push_back(index);
    while(!action_todo.empty()){const auto i=action_todo.back();action_todo.pop_back();if(action_seen.at(i)++)throw Error("Repeated SPSM action in preparation tree");const auto &a=ir.actions.at(i);result.actions.push_back({a.source,layer,false});omit(a.source,code,reason);
      for(const auto s:a.sources)omit(s,"skipped-source","Source binding belongs to a skipped action; preserved in retained inspection");
      for(auto child:a.children)action_todo.push_back(child);
    }
  }
  std::optional<std::array<float,3>> color(const sem::Source &s){
    if(!s.color)return {};const auto *values=std::get_if<FloatValues>(&node(*s.color).value);if(!values||values->count!=3)return {};
    std::array<float,3> v{values->values[0],values->values[1],values->values[2]};for(float f:v)if(!std::isfinite(f))return {};return v;
  }
  bool geometry(const sem::Layer &src){
    bool okay=true;
    for(const auto ref:src.geometry){const auto &f=sem::source_field(*result.source,pi,ref);
      if(f.name=="enabledMeshList"||f.name=="enabledUVTileList"){
        const auto *a=std::get_if<Array>(&node(f.value).value);if(!a||!a->elements.empty()){omit(f.value,"geometry-selection","Source geometry/UV selections have no verified destination binding");okay=false;}
      }else if(f.name=="enabledMeshDefault"||f.name=="enabledUVTileDefault"){
        if(!boolean(f.value,true)){omit(f.value,"geometry-default","Excluding destination geometry is not lowered");okay=false;}
      }else if(f.name=="geometryMaskType"){
        // A selection type is immaterial when both defaults include all and lists are empty.
        diagnostic(f.value,SpsmDiagnosticKind::SourceMetadata,"geometry-mask-type","Original selection enum retained; only all-geometry selections are lowered");
      }else boolean(f.value,true);
    }return okay;
  }
  bool fill(const sem::Action &action,TextureLayer &out){
    std::array<unsigned char,6> bound{};bool any=false;
    const auto action_bits=bits(field(action.source,"channelTypes"));
    if(!action_bits){omit(action.source,"fill-channel-mask","Missing or invalid fill channel mask");return false;}
    for(const auto id:action.sources){const auto found=sources.find(id);if(found==sources.end()){omit(id,"unknown-source","Source is not a known source record");continue;}const auto &s=*found->second;
      const auto mask=bits(field(id,"channelTypes"));if(!mask){omit(id,"source-channel-mask","Missing or invalid source channel mask");continue;}
      if(!*mask){omit(id,"auxiliary-content-source","Auxiliary source mask zero is not native Base Color");continue;}
      std::uint64_t handled=0;
      for(int c=0;c<6;++c)if((*mask&channel_bits[c])&&(*action_bits&channel_bits[c])){
        handled|=channel_bits[c];if(bound[c]++){enable(out,c,false);omit(id,"duplicate-source-channel","Multiple bindings for one content channel are not overwritten");continue;}
        if(!enabled_channel(out,c)){omit(id,"disabled-layer-channel","Layer channel is disabled or its blend mode cannot be translated");continue;}
        if(s.kind!=sem::Source::Kind::Uniform){enable(out,c,false);omit(id,"nonuniform-source","Bitmap/procedural/unknown source is retained and this binding is skipped");continue;}
        const auto value=color(s);if(!value){enable(out,c,false);omit(id,"uniform-value","Uniform must contain exactly three finite float components");continue;}
        float action_opacity=1;auto action_mode=LayerBlend::Normal;if(!blending(action.blending,c,action_opacity,action_mode,false,true)){enable(out,c,false);continue;}
        // A single initial fill starts from transparent content: Normal coverage
        // factors compose into the owning channel, without inventing an effect.
        opacity(out,c)*=unit(s.opacity,1)*action_opacity;
        if(c!=0&&c!=3&&((*value)[0]!=(*value)[1]||(*value)[1]!=(*value)[2])){enable(out,c,false);omit(id,"nonscalar-uniform","Scalar channel has unequal RGB components; extraction is unverified");continue;}
        if((c==1||c==2||c==5)&&((*value)[0]<0||(*value)[0]>1)){enable(out,c,false);omit(id,"native-scalar-domain","Source scalar is outside the native serialized [0,1] domain; no clamping applied");continue;}
        switch(c){
          case 0:{if(!options.uniform_rgb_to_scene_linear){enable(out,c,false);omit(id,"uniform-color-transform-missing","No explicit host source-to-scene color transform; Base Color binding skipped");continue;}const auto rgb=options.uniform_rgb_to_scene_linear(*value);bool fits=true;for(float v:rgb){if(!std::isfinite(v))throw Error("SPSM host color transform produced a nonfinite value");if(std::abs(v)>65504)fits=false;}if(!fits){enable(out,c,false);omit(id,"native-color-domain","Converted color exceeds native serialized color range; no clamping applied");continue;}out.values.base_color={rgb[0],rgb[1],rgb[2],1};assumption(id,"uniform-color-policy","Source RGB interpreted by explicit host source-color policy; source colorimetry remains unverified");break;}
          case 1:out.values.metallic=(*value)[0];break;
          case 2:out.values.roughness=(*value)[0];break;
          case 3:enable(out,c,false);omit(id,"normal-convention","Source encoded tangent normal convention is unverified; internal native normal stays neutral");continue;
          case 4:{TextureSet legacy;legacy.bump_origin=0;const float displayed=legacy.bump_display((*value)[0]);if(std::abs(displayed)>65504){enable(out,c,false);omit(id,"native-bump-domain","Rebased Bump exceeds native serialized range; no clamping applied");continue;}out.values.bump=displayed;assumption(id,"signed-bump-policy","Source signed height is rebased from zero to native Bump neutral 0.5; displacement remains disabled");break;}
          case 5:out.values.ambient_occlusion=(*value)[0];result.material.set.ao_channel=true;break;
        }any=true;
      }
      if(*mask&~handled)omit(id,"unsupported-source-channels","Unbound workflow/unknown/inactive source channel bits are preserved without assigning native slots");
    }
    for(int c=0;c<6;++c)if(!bound[c])enable(out,c,false);
    // A spatial mapping cannot affect an untextured constant. Do not pretend to
    // translate its enum/transform fields; selection/culling can affect coverage.
    for(const auto key:{"backfaceCulling","depthCulling","cylinderHideCaps"})if(boolean(field(action.source,key),false)){
      if(bits(field(action.source,"projection"))==0)assumption(*field(action.source,key),"uv-culling-policy","Source projection 0 inferred UV; spatial culling toggles treated as inactive on constant UV fills");
      else{omit(action.source,"fill-culling","Enabled source culling on nonzero/unknown projection has no verified coverage translation");return false;}
    }
    if(const auto sel=field(action.source,"selection")){const auto *blob=std::get_if<Blob>(&node(*sel).value);if(!blob||blob->size){omit(*sel,"fill-selection","Nonempty source fill selection is not lowered");return false;}}
    assumption(action.source,"constant-projection-policy","Projection/wrap/filter transforms do not sample a constant; original mapping retained without enum conversion");
    return any&&([&]{for(int c=0;c<6;++c)if(enabled_channel(out,c))return true;return false;})();
  }
  bool mask(const sem::Layer &src,TextureLayer &out){
    const bool active=boolean(src.mask_enabled,true);out.mask=LayerMask{};out.mask->enabled=active;out.mask->background=unit(src.mask_initial,1);
    bool okay=true;
    for(auto ai:src.mask_actions){const auto &a=ir.actions.at(ai);if(!active||!boolean(a.enabled,true)){skip_action(ai,out.id,"inactive-mask-action","Disabled source mask/action is retained without evaluation");continue;}
      // Scalar background is the verified initial subset. Never replace an
      // unsupported computed mask with its initial value on a visible layer.
      skip_action(ai,out.id,"pending-mask-action","Mask action retained for native semantic lowering");okay=false;
    }return okay;
  }
  TextureLayer lower_layer(std::size_t index,int depth){
    const auto &src=ir.layers.at(index);TextureLayer out;out.id=ids[index];out.depth=depth;if(src.parent)out.parent=ids.at(*src.parent);
    out.kind=src.group?LayerKind::Folder:LayerKind::Fill;out.channels.fill(true);out.ao_enabled=true;out.values.bump=.5f;out.displacement.enabled=false;for(auto &u:out.users)u.enabled=false;
    if(src.label){if(const auto *label=std::get_if<std::string>(&node(*src.label).value)){if(label->find('\0')!=std::string::npos)throw Error("SPSM layer label contains NUL, which native project metadata cannot store");budget.add(label->size());out.name=*label;}else throw Error("Invalid SPSM layer label");}
    out.visible=boolean(src.enabled,true);bool okay=geometry(src);layer_blending(src,out);
    if(boolean(field(src.source,"gammaCompensation"),false))assumption(src.source,"gamma-compensation","Source gamma compensation retained as metadata; native scene-linear Base Color remains active under the reported source-color policy");
    if(src.group){if(!src.actions.empty()){for(auto a:src.actions)skip_action(a,out.id,"folder-content-actions","Native Folder cannot evaluate source content actions");okay=false;}}
    else{
      std::vector<std::size_t> active;budget.items<std::size_t>(src.actions.size());for(auto a:src.actions)if(boolean(ir.actions.at(a).enabled,true))active.push_back(a);else skip_action(a,out.id,"inactive-content-action","Disabled source action retained without evaluation");
      bool initial=false;
      for(const auto ai:active){const auto &a=ir.actions.at(ai);
        if(!initial&&a.kind==sem::ActionKind::Fill&&a.children.empty()){
          if(action_seen.at(ai)++)throw Error("Repeated SPSM action in preparation tree");
          initial=true;result.initial_fill_actions[src.source]=a.source;const bool lowered=fill(a,out);result.actions.push_back({a.source,out.id,lowered});
          if(!lowered){omit(a.source,"empty-supported-fill","Initial Fill has no uniform contribution; retained for native semantic lowering");okay=false;}
        }else{skip_action(ai,out.id,"pending-content-action","Ordered action retained for native semantic lowering");okay=false;}
      }
      if(!initial){out.channels.fill(false);out.ao_enabled=false;okay=false;}
    }
    if(!mask(src,out))okay=false;
    if(!okay)omit(src.source,"partial-native-layer","Authored visibility and supported base Fill retained; unsupported subactions are reported independently");
    result.layers.push_back({src.source,out.id,!okay});return out;
  }
  PreparedSpsm run(){
    assumption(ir.layers.at(ir.roots[0]).source,"stack-order-policy","Source subStack.items retained bottom-to-top; native folders emitted after children (observed Dirty Plastic Base/Splats/Dirt/Edges order)");
    assumption(ir.layers.at(ir.roots[0]).source,"blend-policy","Named public Shader API blend constants explicitly mapped to native operations; DataBlending enum identity and appearance remain inferred");
    for(std::size_t si=0;si<result.source->presets.size();++si){for(const auto &f:result.source->presets[si].findings)if(f.capability!=sem::Capability::NativePrimitive)diagnostic(f.source,SpsmDiagnosticKind::SourceMetadata,f.code,f.detail,si);if(si!=pi)diagnostic(result.source->source().presets[si].root,SpsmDiagnosticKind::Omission,"alternate-preset-stream","Alternate preset stream retained independently, not applied or merged",si);}
    struct Frame{std::size_t index;int depth;bool finish;};budget.items<Frame>(resource_sum(ir.layers.size(),ir.layers.size()));std::vector<Frame> todo{{ir.roots[0],0,false}};
    while(!todo.empty()){auto f=todo.back();todo.pop_back();if(f.finish){result.material.set.layers.push_back(lower_layer(f.index,f.depth));continue;}
      if(seen.at(f.index)++)throw Error("Repeated SPSM layer in preparation tree");todo.push_back({f.index,f.depth,true});const auto &l=ir.layers.at(f.index);
      if(!l.group&&!l.children.empty())throw Error("SPSM color layer unexpectedly owns child layers");if(f.depth==std::numeric_limits<int>::max()&&!l.children.empty())throw Error("SPSM hierarchy exceeds native depth representation");
      for(auto it=l.children.rbegin();it!=l.children.rend();++it)todo.push_back({*it,f.depth+1,false});
    }
    for(std::size_t i=0;i<seen.size();++i)if(!seen[i])throw Error("SPSM layer exists outside primary root hierarchy");
    for(std::size_t i=0;i<action_seen.size();++i)if(!action_seen[i]){budget.items<SpsmActionBinding>();result.actions.push_back({ir.actions[i].source,{},false});omit(ir.actions[i].source,"unbound-source-action","Source action is outside the applied layer/action tree and remains retained only");}
    if(result.material.set.layers.back().kind!=LayerKind::Folder)throw Error("SPSM material root must be a Folder");
    result.material.set.selected_layer=result.material.set.layers.back().id;budget.add(result.material.set.layers.back().name.size());result.material.set.name=result.material.set.layers.back().name;
    return std::move(result);
  }
};
} // namespace
std::string PreparedSpsm::diagnostic_path(const SpsmImportDiagnostic &d)const{return spsm::semantic::source_path(*source,d.preset,d.source);}
PreparedSpsm prepare_spsm_material(spsm::semantic::Inspection &&inspection,SpsmImportOptions options){auto result=Compiler(std::move(inspection),options).run();if(options.native_actions)lower_spsm_native_actions(result,options.uniform_rgb_to_scene_linear);return result;}
PreparedSpsm prepare_spsm_material(const std::filesystem::path &path,SpsmImportOptions options){auto inspection=spsm::semantic::inspect_archive(spsm::read_archive(path,{options.memory_budget_bytes}),{options.memory_budget_bytes});return prepare_spsm_material(std::move(inspection),std::move(options));}
SmartMaterialPreset spsm_material_for_destination(const PreparedSpsm &prepared,const TextureSet &destination){
  const auto origin=destination.bump_origin;
  if(origin!=0&&origin!=.5f)throw spsm::Error("Unsupported destination Bump origin");
  const auto &source=prepared.material.set;
  if(source.bump_origin!=.5f)throw spsm::Error("SPSM preparation is not in canonical Bump coordinates");
  std::uint64_t bytes=resource_product(source.layers.size(),sizeof(TextureLayer));
  for(const auto &layer:source.layers){
    bytes=resource_sum(bytes,layer.name.size());bytes=resource_sum(bytes,resource_product(layer.effects.size(),sizeof(LevelsEffect)));
    if(layer.mask)bytes=resource_sum(bytes,resource_product(layer.mask->effects.size(),sizeof(MaskEffect)));
    if(origin==.5f)continue;
    if(layer.kind!=LayerKind::Fill&&layer.kind!=LayerKind::Folder)throw spsm::Error("Legacy SPSM Bump adaptation supports only uniform Fill/Folder layers");
    // Inspect only metadata. Even page_count() can evaluate a deferred recipe;
    // this narrow subset rejects any primary paint storage without touching it.
    if(!layer.pixels.tiles().empty()||layer.strokes||layer.instance||layer.quick_overlay)
      throw spsm::Error("Legacy SPSM Bump adaptation does not support paint, recipes, instances or previews");
    if(fill_channel_bound(layer.fill,4)||layer.fill.native_material||layer.fill.atom||layer.fill.bump_bias!=0)
      throw spsm::Error("Legacy SPSM Bump texture/anchor/procedural adaptation is not implemented");
    for(const auto &effect:layer.effects){if(effect.kind==ContentEffectKind::Paint||effect.kind==ContentEffectKind::Anchor)throw spsm::Error("Legacy SPSM Bump material-dependent content-effect adaptation is not implemented");if(effect.channel==4){if(effect.kind!=ContentEffectKind::Fill||effect.source_coverage)throw spsm::Error("Legacy SPSM Bump supports constant Fill effects only");if(effect.blending!=LayerBlend::Normal&&effect.blending!=LayerBlend::Replace&&effect.blending!=LayerBlend::SignedAddition&&effect.blending!=LayerBlend::Disabled)throw spsm::Error("Legacy SPSM Bump effect blend adaptation is not implemented");for(float value:effect.fill_value)if(!std::isfinite(value)||std::abs(value-.5f)>65504)throw spsm::Error("Legacy SPSM Bump effect constant exceeds native range");}}
    if(layer.mask&&layer.mask->channel_dependent())throw spsm::Error("Legacy SPSM Bump adaptation cannot translate material-dependent masks");
    switch(layer.blending[4]){
      case LayerBlend::Normal:case LayerBlend::Replace:case LayerBlend::SignedAddition:case LayerBlend::Disabled:break;
      case LayerBlend::Passthrough:if(layer.kind==LayerKind::Folder)break;[[fallthrough]];
      default:throw spsm::Error("Legacy SPSM Bump blend adaptation is not implemented");
    }
    if(layer.kind==LayerKind::Fill&&(!std::isfinite(layer.values.bump)||std::abs(layer.values.bump-.5f)>65504))
      throw spsm::Error("Legacy SPSM Bump constant exceeds native serialized range");
  }
  if(origin==0&&(!std::isfinite(source.base_values.bump)||std::abs(source.base_values.bump-.5f)>65504))throw spsm::Error("Legacy SPSM Bump base exceeds native serialized range");
  require_texture_ram(resource_product(bytes,2),"SPSM destination preparation");
  auto result=prepared.material;
  if(origin==.5f)return result;
  result.set.bump_origin=0;result.set.base_values.bump-=.5f;
  for(auto &layer:result.set.layers){if(layer.kind==LayerKind::Fill)layer.values.bump-=.5f;if(layer.blending[4]==LayerBlend::SignedAddition)layer.blending[4]=LayerBlend::LinearDodge;for(auto &effect:layer.effects)if(effect.channel==4&&effect.kind==ContentEffectKind::Fill){for(float &value:effect.fill_value)value-=.5f;if(effect.blending==LayerBlend::SignedAddition)effect.blending=LayerBlend::LinearDodge;}}
  return result;
}
} // namespace bts
