#include "SmartMaskProbe.hh"
#include "DocumentEditCommit.hh"
#include "MaskResourceBank.hh"
#include "SmartMaskProjectWitness.hh"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <cstdio>
using namespace bts;
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir directory;assert(directory.isValid());AssetCatalog catalog(directory.path());
 SceneSnapshot scene;MaterialSummary material;material.session_id=23;material.name="Mask generator model";material.uniform_editable=true;scene.materials={material};ImportSummary imported;imported.materials={{23,material.name}};
 auto document=std::make_unique<TextureDocument>(NewProjectOptions{},imported,scene);
 struct Entry {std::unique_ptr<TextureDocument> state;std::uint32_t material_uid;};std::vector<Entry> undo,redo;
 int applications=0,causal=0;std::vector<QUuid> witnessed_targets;
 for(const auto *name:smart_mask_probe::names){
  const auto &source=smart_mask_probe::asset(name);const auto retained=catalog.retain_project(source);assert(catalog.retain_project(source)==retained);
  const FillTexture *original=nullptr;for(const auto &e:source.smart_mask->effects)if(e.kind==MaskEffectKind::Fill)original=e.mapping.projection.texture.get();assert(original);
  for(bool folder:{false,true}){
   auto &set=document->set(23);if(folder)TextureDocument::add_folder(set);else TextureDocument::add_fill(set);
   const auto id=set.selected_layer;witnessed_targets.push_back(id);TextureDocument::apply_mask_preset(set,id,*source.smart_mask,true);auto &layer=TextureDocument::layer(set,id);
   assert(layer.kind==(folder?LayerKind::Folder:LayerKind::Fill)&&layer.mask&&set.selected_mask);
   for(const auto &e:layer.mask->effects)if(e.kind==MaskEffectKind::Generator)assert(e.triplanar_grunge&&e.surface_fallback);
   for(const auto &e:layer.mask->effects)if(e.kind==MaskEffectKind::Fill){assert(e.mapping.projection.mode==1&&e.mapping.projection.texture->pixels==original->pixels);}
   const auto before=document->to_json();auto candidate=std::make_unique<TextureDocument>(*document);
   for(auto &e:TextureDocument::layer(candidate->set(23),id).mask->effects)if(e.kind==MaskEffectKind::Fill)e.opacity=.31f;
   const auto after=candidate->to_json();auto commit=prepare_document_commit(undo,23);
   publish_document_commit(document,candidate,undo,redo,std::move(commit),[]()noexcept{});
   assert(document->to_json()==after);auto previous=std::move(undo.back().state);undo.pop_back();redo.push_back({std::move(document),23});document=std::move(previous);assert(document->to_json()==before);
   auto next=std::move(redo.back().state);redo.pop_back();undo.push_back({std::move(document),23});document=std::move(next);assert(document->to_json()==after);++applications;
  }
  TextureSet probe;probe.layers.front().mask=smart_mask_probe::causal_mask(*source.smart_mask);
  const int generator=source.smart_mask->effects.front().generator;
  for(const auto &influence:smart_mask_probe::influences(name)){
   smart_mask_probe::profile(probe,generator);smart_mask_probe::map(probe,influence.role,influence.stronger);
   const float stronger=TextureDocument::mask_value(probe,probe.layers.front().id,47,39,1001);
   smart_mask_probe::map(probe,influence.role,influence.weaker);
   const float weaker=TextureDocument::mask_value(probe,probe.layers.front().id,47,39,1001);
   std::printf("BN_CAUSAL mask=%s map=%s stronger=%.9g weaker=%.9g delta=%.9g\n",name,influence.label,stronger,weaker,stronger-weaker);std::fflush(stdout);
   assert(stronger>weaker+1e-4f);++causal;
  }
 }
 // Scalar channels must receive the same explicit 3D mask context as Color.
 // This independently evaluates the mask first, then checks each consumer's
 // coverage, with no reliance on Color as the scalar oracle.
 int scalar_cases=0;
 for(const char *name:smart_mask_probe::names)for(bool baked:{false,true}){
  const auto &source=smart_mask_probe::asset(name);TextureSet set;smart_mask_probe::profile(set,source.smart_mask->effects.front().generator);
  auto atlas=std::make_shared<FillSurfaceAtlas>(*set.projection_geometry);auto &points=atlas->tiles.at(1001);for(int y=0;y<32;++y)for(int x=0;x<32;++x){auto &point=points[y*32+x];point.position={(x+.5f)/16-1,(y+.5f)/16-1,.2f+float(x+y)/128};point.normal={.2f+x/64.f,.4f,.9f-y/64.f};}set.projection_geometry=std::move(atlas);if(!baked)set.mesh_maps={};
  set.users[0].present=set.users[0].allocated=true;set.users[0].precision=ChannelPrecision::L32F;set.displacement.channel=set.displacement.allocated=true;set.displacement.precision=ChannelPrecision::L32F;set.precision[0]=ChannelPrecision::RGB32F;set.cm_per_native=2.5;set.native_per_uv={2,3};
  auto &layer=set.layers.front();layer.kind=LayerKind::Fill;layer.values.base_color={.73f,.73f,.73f,1};layer.users[0].enabled=layer.displacement.enabled=true;layer.users[0].value=layer.displacement.value=.73f;layer.mask=smart_mask_probe::causal_mask(*source.smart_mask);
  MaskCompareInputs context;context.projection=set.projection_geometry.get();context.cm_per_native=set.cm_per_native;context.native_per_uv=set.native_per_uv;context.reference={&set,layer.id,0,0,1001,&TextureDocument::anchor_sample,context.projection};
  if(!baked){float low=1,high=0;for(int y=0;y<128;++y)for(int x=0;x<128;++x){const float value=source.smart_mask->value(x,y,1001,128,128,nullptr,&set.mesh_maps,&context);low=std::min(low,value);high=std::max(high,value);}std::printf("BN_FULL_FALLBACK preset=%s min=%.9g max=%.9g range=%.9g\n",name,low,high,high-low);std::fflush(stdout);assert(high-low>1e-4f);if(std::string_view(name)=="Leaks & Stains")assert(high-low>.1f);}
  int px=-1,py=-1;float mask=0,low=1,high=0;for(int y=4;y<128&&px<0;y+=8)for(int x=4;x<128;++x){const float value=layer.mask->value(x,y,1001,128,128,nullptr,&set.mesh_maps,&context);low=std::min(low,value);high=std::max(high,value);if(value>1e-4f&&value<.999f){px=x;py=y;mask=value;break;}}std::fprintf(stderr,"BN_SCALAR_FIXTURE preset=%s baked=%d min=%.9g max=%.9g xy=%d,%d\n",name,int(baked),low,high,px,py);assert(px>=0);
  const auto user=TextureDocument::composite_user(set,0,px,py),disp=TextureDocument::composite_displacement(set,px,py);std::array<bool,6>wanted{};wanted[0]=true;const auto color=TextureDocument::evaluate_channels(set,wanted,px,py,1001);
  assert(std::abs(user[1]-mask)<1e-6f&&std::abs(disp[1]-mask)<1e-6f&&std::abs(color.coverage[0]-mask)<1e-6f);assert(std::abs(user[0]-.73f)<1e-6f&&std::abs(disp[0]-.73f)<1e-6f);++scalar_cases;
 }
 assert(scalar_cases==16);std::puts("BN scalar mask context: eight recipes, User and Displacement vs direct mask, missing/installed maps PASS");
 // Both generator codecs and ordinary document metadata persist the new
 // explicit opt-ins; omitted legacy keys keep UV/strict defaults.
 LevelsEffect content;content.kind=ContentEffectKind::Generator;content.generator=8;content.triplanar_grunge=content.surface_fallback=true;const auto effects=read_effects(effects_json({content}));assert(effects.size()==1&&effects[0].triplanar_grunge&&effects[0].surface_fallback);
 std::vector<TextureSetBinding> bindings;for(const auto &set:document->texture_sets())bindings.push_back({set.id,set.material_session_uid,set.name});const auto reopened=TextureDocument::from_json(document->to_json(),bindings);
 for(const auto &set:reopened.texture_sets())for(const auto &layer:set.layers)if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Generator)assert(effect.triplanar_grunge&&effect.surface_fallback);
 const auto witness=smart_mask_proof::witness(*document,witnessed_targets);
 const auto verified=smart_mask_proof::validate(*document,witness);
 assert(verified["bindings"].toInt()==16&&verified["shared_image_storages"].toInt()==8);
 // Blender allocates new session UIDs on Open; persisted set/layer UUIDs own
 // the independent witness. This must not relax pixel or reference validation.
 auto rebound=*document;for(const auto &set:document->texture_sets())rebound.set(set.material_session_uid).material_session_uid+=100000;
 const auto rebound_verified=smart_mask_proof::validate(rebound,witness);assert(rebound_verified["bindings"].toInt()==16);
 auto changed=rebound;const auto target_id=QUuid(witness["layers"].toArray().first().toObject()["layer"].toString());bool changed_target=false;
 for(const auto &set:rebound.texture_sets())for(const auto &layer:set.layers)if(layer.id==target_id){auto &mask=*TextureDocument::layer(changed.set(set.material_session_uid),target_id).mask;mask.background=1-mask.background;changed_target=true;}
 bool changed_rejected=false;try{smart_mask_proof::validate(changed,witness);}catch(const std::runtime_error &){changed_rejected=true;}assert(changed_target&&changed_rejected);
 // JSON metadata alone cannot satisfy the cold packed-resource proof. This
 // must fail before a GPU probe can conceal an unresolved image dependency.
 bool unresolved_rejected=false;try{smart_mask_proof::validate(reopened,witness);}catch(const std::runtime_error &){unresolved_rejected=true;}assert(unresolved_rejected);
 const auto banks=canonical_mask_resource_banks(catalog.project_assets());std::set<std::string> hashes;
 for(const auto &bank:banks){assert(bank.scope==AssetScope::Project&&bank.smart_mask);const auto decoded=read_mask_preset(bank.bytes);for(const auto &e:decoded.effects)if(e.mapping.projection.texture){assert(e.mapping.projection.texture->pixels);hashes.insert(e.mapping.projection.texture->hash);}}
 assert(hashes.size()==8&&applications==16&&causal>=20);
 std::puts("mask generator model: all eight apply to Fill/Folder, shared pixels, native edit Undo/Redo, packed-bank codec and causal mesh-map responses PASS; actual Blender packed cold Open is a separate native gate");
}
