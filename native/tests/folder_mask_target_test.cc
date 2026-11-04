#include "MaskTextureTarget.hh"
#include "DocumentEditCommit.hh"
#include "StrokeArchive.hh"
#include <cstdio>
using namespace bts;
namespace {
int checks=0;
void require(bool value,const char *why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F &&run,const char *why){bool failed=false;try{run();}catch(const std::exception &){failed=true;}require(failed,why);}
struct Fixture {
 std::unique_ptr<TextureDocument> document;QUuid folder;
 Fixture(){SceneSnapshot scene;MaterialSummary material;material.session_id=17;material.name="Folder mask route";material.uniform_editable=true;scene.materials={material};ImportSummary imported;imported.materials={{17,material.name}};document=std::make_unique<TextureDocument>(NewProjectOptions{},imported,scene);TextureDocument::add_folder(set());folder=set().selected_layer;}
 TextureSet &set(){return document->set(17);}
 TextureLayer &layer(){return TextureDocument::layer(set(),folder);}
};
void deferred_and_atomic(){
 Fixture f;const auto before=f.document->to_json();const auto count=f.set().layers.size();const auto target=texture_load_target(f.document->id(),f.set(),2,true,7);
 require(target.create_mask_fill&&!target.create_fill&&target.insertion.isNull()&&!target.effect.isNull()&&target.channel==0,"Folder image drop was routed to a color layer");
 require(f.document->to_json()==before&&!f.layer().mask,"Planning/cancel/failure created a mask or changed metadata");require(mask_texture_target_current(f.set(),target,7),"Fresh folder target rejected");
 require(!mask_texture_target_current(f.set(),target,8),"Edited document admitted stale mask creation");f.set().selected_geometry=true;require(!mask_texture_target_current(f.set(),target,7),"Geometry selection did not cancel pending mask load");f.set().selected_geometry=false;
 const auto repeated=texture_load_target(f.document->id(),f.set(),0,false,7);require(target.same_slot(repeated),"Repeated pending mask request did not replace/cancel its slot");
 auto candidate=std::make_unique<TextureDocument>(*f.document);auto &effect=install_mask_texture_target(candidate->set(17),target);auto image=std::make_shared<FillTexture>();image->asset=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();image->hash=std::string(64,'a');image->name="Folder bitmap fixture";image->width=image->height=2;image->raw=true;effect.mapping.projection.texture=image;
 struct Entry{std::unique_ptr<TextureDocument> state;std::uint32_t material_uid;};std::vector<Entry> undo,redo;auto prepared=prepare_document_commit(undo,17);publish_document_commit(f.document,candidate,undo,redo,std::move(prepared),[]()noexcept{});
 require(undo.size()==1&&redo.empty()&&undo.back().state->to_json()==before,"Mask+Fill+texture did not publish as one Undo");require(f.set().layers.size()==count&&f.layer().kind==LayerKind::Folder&&f.layer().mask->effects.size()==1,"Folder load created a color layer or extra effect");require(f.set().selected_mask&&f.set().selected_effect==target.effect&&find_mask_effect(f.layer().mask->effects,target.effect)->mapping.projection.texture==image,"Installed Fill selection/resource missing");
 const auto edited=f.document->to_json();redo.push_back({std::move(f.document),17});f.document=std::move(undo.back().state);undo.pop_back();require(f.document->to_json()==before&&!f.layer().mask,"Undo did not remove atomic mask load");undo.push_back({std::move(f.document),17});f.document=std::move(redo.back().state);redo.pop_back();require(f.document->to_json()==edited,"Redo changed mask target tree");
 const std::array<TextureSetBinding,1> binding{{{f.set().id,17,f.set().name}}};auto reopened=TextureDocument::from_json(edited,binding);require(reopened.to_json()==edited,"Folder bitmap mask JSON roundtrip changed metadata");
}
void selected_masks_and_nested(){
 Fixture f;TextureDocument::add_mask(f.set(),0);MaskEffect levels;levels.kind=MaskEffectKind::Levels;f.layer().mask->effects={levels};f.set().selected_effect=levels.id;
 auto target=texture_load_target(f.document->id(),f.set(),0,false,3);require(target.create_mask_fill,"Selected non-Fill mask effect did not plan a Fill");install_mask_texture_target(f.set(),target);require(f.layer().mask->background==0&&find_mask_effect(f.layer().mask->effects,levels.id),"Bitmap assignment reset existing mask content");
 auto current=texture_load_target(f.document->id(),f.set(),0,false,4);require(!current.create_mask_fill&&current.effect==target.effect,"Active Fill was not reused");require(mask_texture_target_current(f.set(),current,4),"Active Fill rejected");
 MaskEffect nested;nested.kind=MaskEffectKind::Fill;MaskEffect group;group.kind=MaskEffectKind::Group;group.group_effects=std::make_shared<const std::vector<MaskEffect>>(std::vector<MaskEffect>{nested});f.layer().mask->effects.push_back(group);f.set().selected_effect=nested.id;const auto original=f.layer().mask->effects.back().group_effects;
 current=texture_load_target(f.document->id(),f.set(),0,false,5);require(current.effect==nested.id&&!current.create_mask_fill,"Nested Fill target lost UUID");install_mask_texture_target(f.set(),current).fill=.2;require(original->front().fill!=.2f&&find_mask_effect(f.layer().mask->effects,nested.id)->fill==.2f,"Nested Fill mutation violated ancestor COW");
 edit_mask_effect(f.layer().mask->effects,nested.id)->anchor_source=QUuid::createUuid();require(!mask_texture_target_current(f.set(),current,5),"Anchor-backed Fill admitted stale replacement");target=texture_load_target(f.document->id(),f.set(),0,false,5);require(target.create_mask_fill,"Anchor-backed Fill source was silently replaced");
 f.set().selected_mask=false;f.set().selected_effect=QUuid();add_bitmap_mask_target(f.set());require(f.set().selected_mask&&!f.set().selected_effect.isNull()&&f.layer().mask->background==0,"Add bitmap did not select Fill or preserved background");
 f.layer().instance=LayerInstance{};rejects([&]{texture_load_target(f.document->id(),f.set(),0,false,6);},"Instance mask load admitted");rejects([&]{add_bitmap_mask_target(f.set());},"Instance bitmap creation admitted");rejects([&]{install_mask_texture_target(f.set(),target);},"Instance deferred install admitted");
}
void ordinary_routes(){
 Fixture f;auto &s=f.set();s.selected_layer=s.layers.front().id;s.selected_mask=false;s.selected_effect=QUuid();auto &layer=TextureDocument::layer(s,s.selected_layer);layer.kind=LayerKind::Paint;
 auto target=texture_load_target(f.document->id(),s,2,false,1);require(target.create_fill&&!target.create_mask_fill&&target.effect.isNull()&&target.channel==2,"Ordinary Paint image route regressed");layer.kind=LayerKind::Fill;target=texture_load_target(f.document->id(),s,1,false,1);require(!target.create_fill&&target.channel==1,"Ordinary Fill replacement regressed");target=texture_load_target(f.document->id(),s,1,true,1);require(target.create_fill&&!target.insertion.isNull(),"Explicit ordinary image insertion regressed");s.disabled=true;rejects([&]{texture_load_target(f.document->id(),s,0,false,1);},"Disabled set admitted image");s.disabled=false;layer.instance=LayerInstance{};rejects([&]{texture_load_target(f.document->id(),s,0,false,1);},"Ordinary instance source replacement admitted");
}
}
int main(){try{deferred_and_atomic();selected_masks_and_nested();ordinary_routes();std::printf("Folder mask target: %d checks PASS\n",checks);return 0;}catch(const std::exception &error){std::fprintf(stderr,"Folder mask target FAIL: %s\n",error.what());return 1;}}
