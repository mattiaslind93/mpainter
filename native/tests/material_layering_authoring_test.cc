#include "MaterialLayeringAuthoring.hh"
#include "MaterialLayeringIO.hh"
#include "DocumentEditCommit.hh"
#include "TextureLoadController.hh"
#include "MaterialLayerBumpReference.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main(){
 TextureSet set;set.uniform_editable=true;set.texture_resolution=128;set.bump_origin=.5f;
 set.layers.clear();TextureLayer original;original.name="Retained Main";set.selected_layer=original.id;set.layers.push_back(original);
 std::array<MaterialValues,4> values;for(int i=0;i<4;++i){values[i].base_color={float(i)/4,.125f,.75f,1};values[i].roughness=float(i)/4;}
 const auto enabled=prepare_uniform_material_layering(set,true,values);assert(!set.material_layering.allocated()&&set.layers.size()==1);
 assert(enabled.material_layering.enabled&&enabled.layers.size()==7&&enabled.layers[0].id==original.id);
 auto candidate=enabled;BrushSettings brush;const auto main_channels=brush.channels;const auto main_users=brush.users;const bool main_displacement=brush.displacement;
 MaterialLayeringBrushRoute route;
 for(int which=0;which<3;++which){select_material_substack(candidate,which);route.synchronize(brush,&candidate);const int slot=candidate.material_layering.mask_slots[which];const int context=material_layering_channel_context(candidate,3);assert(context==6+which&&context!=3);assert(brush.users[slot]&&!brush.ao&&!brush.displacement);for(int c=0;c<5;++c)assert(!brush.channels[c]);for(int i=0;i<8;++i)assert(brush.users[i]==(i==slot));
  TextureDocument::add_paint(candidate);const auto &paint=TextureDocument::layer(candidate,candidate.selected_layer);assert(paint.parent==candidate.material_layering.mask_roots[which]);assert(paint.users[slot].enabled);for(int c=0;c<5;++c)assert(!paint.channels[c]);
  TextureDocument::add_fill(candidate);const auto &fill=TextureDocument::layer(candidate,candidate.selected_layer);assert(fill.kind==LayerKind::Fill&&fill.users[slot].enabled);for(int i=0;i<8;++i)assert(fill.users[i].enabled==(i==slot));validate_material_substacks(candidate);
  auto display=material_layering_display(candidate);assert(display.layers.size()==3&&!display.material_layering.enabled);for(const auto &row:display.layers)assert(row.id!=original.id&&row.id!=candidate.material_layering.mask_roots[which]);
 }
 brush.diameter=31;select_material_substack(candidate,-1);route.synchronize(brush,&candidate);assert(brush.channels==main_channels&&brush.users==main_users&&brush.displacement==main_displacement&&brush.diameter==31);
 auto another=candidate;another.id=QUuid::createUuid();select_material_substack(another,1);route.synchronize(brush,&another);assert(brush.users[another.material_layering.mask_slots[1]]);route.synchronize(brush,nullptr);assert(brush.channels==main_channels&&brush.users==main_users);
 bool rejected_main=false;try{require_material_layering_edit_target(candidate);}catch(const std::invalid_argument &){rejected_main=true;}assert(rejected_main&&material_layering_channel_context(candidate,2)==2);select_material_substack(candidate,0);require_material_layering_edit_target(candidate);select_material_substack(candidate,-1);
 SceneSnapshot scene;MaterialSummary surface;surface.session_id=1;surface.name="Surface";surface.uniform_editable=true;scene.materials.push_back(surface);ImportSummary imported;imported.materials.push_back({1,"Surface"});NewProjectOptions options;options.resolution=DocumentResolution::Pixels128;TextureDocument document(options,imported,scene);document.set(1).material_layering.enabled=true;auto workflow=document.shader_instances();auto params=workflow.for_set(document.set(1).id).parameters;params.openpbr.emplace();workflow.parameters(workflow.assignments.at(document.set(1).id),params);bool workflow_rejected=false;try{require_material_layering_workflow(document,workflow);}catch(const std::invalid_argument &){workflow_rejected=true;}assert(workflow_rejected&&!document.set(1).shader_parameters.openpbr);document.set(1).material_layering.enabled=false;require_material_layering_workflow(document,workflow);
 // Actual deferred Color-only GPU recording: last-brush Bump metadata is
 // inert on Paint, and eligibility must never call recipe_->evaluated().
 auto recorded=document;auto &recorded_set=recorded.set(1);recorded_set.layers=set.layers;recorded_set.selected_layer=set.selected_layer;auto &color_paint=recorded_set.layers.front();color_paint.kind=LayerKind::Paint;color_paint.channels={true,false,false,false,false};color_paint.values.bump=0;
 PaintPixels::Stamp color_stamp;color_stamp.center={.5,.5};color_stamp.axis_x={.15,0};color_stamp.axis_y={0,.15};color_stamp.channels={true,false,false,false,false};color_stamp.values.base_color={.8f,.2f,.1f,1};color_stamp.values.bump=0;
 {PaintRecordingScope recording;const auto bounds=color_paint.pixels.stamp(128,color_stamp);assert(!bounds.empty());}
 const auto recipe=color_paint.pixels.tile(1001).recipe();assert(recipe);const auto versions=color_paint.pixels.tile(1001).page_versions();assert(!versions[0].empty()&&versions[3].empty()&&versions[4].empty());
 const auto before_metadata=recorded.edit_state();const auto materializations=PaintPixels::recipe_materializations();
 {GpuPersistenceScope no_cpu;auto accepted=prepare_uniform_material_layering(recorded.set(1),true,values);assert(accepted.material_layering.enabled);const auto &retained=TextureDocument::layer(accepted,color_paint.id);assert(retained.values.bump==0&&retained.pixels.tile(1001).recipe()==recipe&&retained.pixels.tile(1001).page_versions()==versions);
  auto edited=recorded;edited.set(1)=accepted;const auto after_metadata=edited.edit_state();const QJsonObject original_layer=before_metadata["texture_sets"].toArray()[0].toObject()["layers"].toArray()[0].toObject();QJsonObject expected_layer=original_layer;auto expected_users=expected_layer["users"].toArray();for(int slot:accepted.material_layering.mask_slots){assert(!recorded.set(1).users[slot].allocated);auto user=expected_users[slot].toObject();user["enabled"]=false;expected_users[slot]=user;}expected_layer["users"]=expected_users;bool identical=false;for(const auto &row:after_metadata["texture_sets"].toArray()[0].toObject()["layers"].toArray())if(row.toObject()["id"]==original_layer["id"]){assert(row.toObject()==expected_layer);identical=true;}assert(identical);}
 assert(recorded.edit_state()==before_metadata&&PaintPixels::recipe_materializations()==materializations);
 for(int channel:{3,4}){auto uniform_fill=set;auto &layer=uniform_fill.layers.front();layer.kind=LayerKind::Fill;layer.channels.fill(false);layer.channels[channel]=true;if(channel==3)layer.values.normal={1,0,0};else layer.values.bump=0;bool denied=false;try{require_uniform_material_layering(uniform_fill);}catch(const std::invalid_argument &){denied=true;}assert(denied);layer.channels[channel]=false;require_uniform_material_layering(uniform_fill);}
 auto live=std::make_unique<TextureDocument>(document);const auto original_document=live->to_json();auto staged=std::make_unique<TextureDocument>(*live);staged->set(1)=prepare_uniform_material_layering(live->set(1),true,values);struct Entry{std::unique_ptr<TextureDocument> state;std::uint32_t uid;};std::vector<Entry> undo,redo;auto history=prepare_document_commit(undo,1);publish_document_commit(live,staged,undo,redo,std::move(history),[]()noexcept{});assert(undo.size()==1&&redo.empty()&&live->set(1).material_layering.enabled&&undo[0].state->to_json()==original_document);const auto edited_document=live->to_json();std::swap(live,undo[0].state);assert(live->to_json()==original_document);std::swap(live,undo[0].state);assert(live->to_json()==edited_document);
 const auto retained_roots=candidate.material_layering.mask_roots;const auto retained_size=candidate.layers.size();const auto disabled=prepare_uniform_material_layering(candidate,false,values);assert(!disabled.material_layering.enabled&&disabled.material_layering.mask_roots==retained_roots&&disabled.layers.size()==retained_size&&disabled.selected_layer==original.id);for(int slot:disabled.material_layering.mask_slots)assert(disabled.users[slot].allocated&&!disabled.users[slot].present);
 auto empty_stack=enabled;select_material_substack(empty_stack,0);TextureDocument::delete_layers(empty_stack,{empty_stack.selected_layer});bool last_rejected=false;try{require_uniform_material_layering(empty_stack);}catch(const std::invalid_argument &e){last_rejected=std::string(e.what()).find("at least one")!=std::string::npos;}assert(last_rejected&&enabled.layers.size()==7);
 auto repaired=enabled;select_material_substack(repaired,0);TextureDocument::add_paint(repaired);const auto removed=repaired.selected_layer;TextureDocument::delete_layers(repaired,{removed});normalize_uniform_material_selection(repaired);assert(repaired.selected_layer==enabled.material_layering.mask_selections[0]&&repaired.material_layering.main_selection==enabled.material_layering.main_selection);const auto displayed=material_layering_display(repaired);assert(std::any_of(displayed.layers.begin(),displayed.layers.end(),[&](const auto &layer){return layer.id==repaired.selected_layer;}));TextureDocument::add_fill(repaired);normalize_uniform_material_selection(repaired);assert(TextureDocument::layer(repaired,repaired.selected_layer).users[repaired.material_layering.mask_slots[0]].enabled);validate_material_substacks(repaired);
 auto no_slots=set;for(auto &c:no_slots.users)c.allocated=true;bool rejected=false;try{prepare_uniform_material_layering(no_slots,true,values);}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&!no_slots.material_layering.allocated()&&no_slots.layers.size()==1);
 auto bad=set;bad.mesh_maps[0]=MeshMap{};rejected=false;try{prepare_uniform_material_layering(bad,true,values);}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&!bad.material_layering.enabled);
 auto altered=values;altered[2].normal={1,0,0};rejected=false;try{prepare_uniform_material_layering(set,true,altered);}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&!set.material_layering.allocated());
 auto protected_set=enabled;protected_set.layers.erase(protected_set.layers.end()-1);rejected=false;try{validate_material_substacks(protected_set);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 // Four independent repeating sources are buffered without touching the
 // retained input document or evaluating its recorded paint pages.
 auto mapped_inputs=material_layering_inputs(set);std::array<std::shared_ptr<const FillTexture>,4> maps;
 for(int role=0;role<4;++role){auto source=std::make_shared<FillTexture>();source->asset=std::string(36,char('a'+role));source->hash=std::string(64,char('a'+role));source->name="Owned repeating source";source->raw=role!=0;source->width=source->height=2;source->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(4,FillTexture::Pixel{.25f,.5f,.75f,0});maps[role]=source;}
 const int roles[]{0,1,2,5};for(int input=0;input<4;++input){for(int role=0;role<4;++role)mapped_inputs[input].textures[roles[role]]=maps[role];mapped_inputs[input].tiling={1.+input,2.+input};mapped_inputs[input].offset={.125*input,-.25*input};mapped_inputs[input].rotation=15*input;}
 const auto original_set=material_layering_json(set.material_layering);const auto mapped=prepare_material_layering_inputs(set,true,mapped_inputs);assert(material_layering_json(set.material_layering)==original_set);
 for(int input=0;input<4;++input){assert(mapped.material_layering.materials[input].tiling==mapped_inputs[input].tiling&&mapped.material_layering.materials[input].offset==mapped_inputs[input].offset&&mapped.material_layering.materials[input].rotation==mapped_inputs[input].rotation);for(int role=0;role<4;++role)assert(mapped.material_layering.materials[input].textures[roles[role]]==maps[role]);}
 auto main_view=mapped;select_material_substack(main_view,-1);const auto main_before=material_layering_json(main_view.material_layering);const auto same_main=prepare_material_layering_inputs(main_view,true,mapped_inputs);assert(same_main.selected_layer==main_view.selected_layer&&same_main.material_layering.active_mask==-1&&material_layering_json(same_main.material_layering)==main_before);
 const auto same=prepare_material_layering_inputs(mapped,true,mapped_inputs);assert(material_layering_json(same.material_layering)==material_layering_json(mapped.material_layering));const auto maps_disabled=prepare_material_layering_inputs(mapped,false,mapped_inputs);assert(!maps_disabled.material_layering.enabled&&maps_disabled.material_layering.materials[2].textures[0]==maps[0]);
 for(int forbidden:{3}){auto denied=mapped_inputs;denied[1].textures[forbidden]=maps[1];bool error=false;try{prepare_material_layering_inputs(set,true,denied);}catch(const std::invalid_argument&){error=true;}assert(error&&material_layering_json(set.material_layering)==original_set);}
 {auto denied=mapped_inputs;denied[0].tiling[0]=0;bool error=false;try{prepare_material_layering_inputs(set,true,denied);}catch(const std::invalid_argument&){error=true;}assert(error);}
 {auto denied=mapped_inputs;denied[1].textures[0]=maps[1];bool error=false;try{prepare_material_layering_inputs(set,true,denied);}catch(const std::invalid_argument&){error=true;}assert(error);}
 // Raw source UDIM coverage is independent of the destination tile number.
 // Exact upper endpoints must not require a spurious next tile.
 {auto first=std::make_shared<FillTexture>(*maps[1]),second=std::make_shared<FillTexture>(*maps[1]);first->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();second->asset=first->asset;second->hash=std::string(64,'e');auto sequence=std::make_shared<FillTexture>(*first);sequence->hash=std::string(64,'f');sequence->tiles=std::make_shared<const std::map<int,std::shared_ptr<const FillTexture>>>(std::map<int,std::shared_ptr<const FillTexture>>{{1001,first},{1002,second}});
  auto input=LayerMaterialInput{};for(int c:{1,2,4,5})input.textures[c]=sequence;
  assert(material_layer_udim_footprint(input,{1001,1002})==(std::set<int>{1001,1002}));require_material_layer_udim_coverage(input,{1001,1002});
  auto original=set;original.tiles={1001,1002};auto inputs=material_layering_inputs(original);inputs[0]=input;const auto accepted=prepare_material_layering_inputs(original,true,inputs);for(int c:{1,2,4,5})assert(accepted.material_layering.materials[0].textures[c]==sequence);assert(!original.material_layering.enabled);
  auto shifted=input;shifted.offset={1,0};assert(material_layer_udim_footprint(shifted,{1001})==std::set<int>{1002});require_material_layer_udim_coverage(shifted,{1001});
  // Reflected maximum is attained on the included lower UV boundary. Require
  // that endpoint too; this conservative rule is not a silent wrap fallback.
  auto mirrored=input;mirrored.tiling={-1,1};mirrored.offset={2,0};assert(material_layer_udim_footprint(mirrored,{1001})==(std::set<int>{1002,1003}));
  for(int negative=0;negative<5;++negative){auto bad=input;bool error=false;
   if(negative==0)bad.offset={1,0};if(negative==1)bad.offset={-1,0};if(negative==2)bad.rotation=45;if(negative==3)bad.textures[0]=sequence;if(negative==4)bad.textures[3]=sequence;
   try{bad.validate();require_material_layer_udim_coverage(bad,{1001,1002});}catch(const std::invalid_argument&){error=true;}assert(error);
  }
  const auto json=fill_texture_json(sequence);const auto restored=read_fill_texture_json(json);assert(restored->tiles&&restored->tiles->size()==2&&fill_texture_json(restored)==json&&restored->tiles->at(1002)->hash==second->hash);
  auto retained=prepare_material_layering_inputs(accepted,false,inputs);assert(!retained.material_layering.enabled&&retained.material_layering.materials[0].textures[4]==sequence);
 }

 // Bump is a raw R source only; Main's existing height/Normal contract is unchanged.
 {auto bumped=mapped_inputs;for(auto &input:bumped)input.textures[4]=maps[1];
  const auto accepted=prepare_material_layering_inputs(set,true,bumped);
  for(const auto &input:accepted.material_layering.materials)assert(input.textures[4]==maps[1]&&input.values.bump==.5f&&input.values.normal==(std::array<float,3>{0,0,1}));
  auto cleared=bumped;for(auto &input:cleared)input.textures[4].reset();const auto neutral=prepare_material_layering_inputs(accepted,true,cleared);for(const auto &input:neutral.material_layering.materials)assert(!input.textures[4]&&input.values.bump==.5f);
  for(int negative=0;negative<4;++negative){auto inputs=bumped;auto main=set;
   if(negative==0)inputs[0].textures[4]=maps[0]; // Color interpretation is never a height source.
   if(negative==1)inputs[0].values.bump=.25f; // No new uniform-height contract.
   if(negative==2)main.base_values.bump=.25f;
   if(negative==3){main.layers[0].kind=LayerKind::Fill;main.layers[0].channels[4]=true;main.layers[0].values.bump=.25f;}
   bool denied=false;try{prepare_material_layering_inputs(main,true,inputs);}catch(const std::invalid_argument&){denied=true;}assert(denied);
  }
  const auto retained=prepare_material_layering_inputs(accepted,false,bumped);assert(!retained.material_layering.enabled&&retained.material_layering.materials[0].textures[4]==maps[1]);
 }
 // Derivative reference derives the world-space metric: scaling/rotating the
 // actual plane must change the response, and constant .5 must remain flat.
 {using namespace pd149_bump_test;const std::array<Vector,3> p{{{4,5,6},{6,5,6},{4,8,6}}};const std::array<std::array<double,2>,3> uv{{{0,0},{1,0},{0,1}}};const auto metric=plane(p,uv);
  assert(metric.du==(Vector{2,0,0})&&metric.dv==(Vector{0,3,0}));const auto flat=response(metric,0,0,.001,1);assert(flat==(Vector{0,0,1}));
  const auto expected=normalized(Vector{-.032,-.016,1}),actual=response(metric,64,48,.001,1);for(int c=0;c<3;++c)assert(std::abs(actual[c]-expected[c])<1e-12);
  assert(response(metric,64,48,.001,0)==flat);
  const std::array<Vector,3> rotated{{{4,5,6},{4,7,6},{4,5,9}}};const auto turned=plane(rotated,uv);const auto turned_response=response(turned,64,48,.001,1);const auto turned_expected=normalized(Vector{1,-.032,-.016});for(int c=0;c<3;++c)assert(std::abs(turned_response[c]-turned_expected[c])<1e-12);
 }
 TextureLoadTarget buffered;buffered.document=document.id();buffered.set=document.set(1).id;buffered.buffer=QUuid::createUuid();buffered.material_input=0;buffered.channel=0;auto another_slot=buffered;another_slot.material_input=1;assert(!buffered.same_slot(another_slot));another_slot=buffered;another_slot.buffer=QUuid::createUuid();assert(!buffered.same_slot(another_slot));another_slot=buffered;assert(buffered.same_slot(another_slot));
 std::cout<<"Uniform layering detached edits, mask-only Paint/Fill and brush routing, Main restoration, protected roots and atomic rejection PASS\n";
}
