#include "MaskEffectPath.hh"
#include "TextureDocument.hh"
#include <cstdio>
using namespace bts;
namespace {
int checks=0;
int budget_queries=0;
std::uint64_t counted_ram(){++budget_queries;return std::numeric_limits<std::uint64_t>::max();}
void require(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
MaskEffect node(MaskEffectKind kind){MaskEffect effect;effect.kind=kind;return effect;}
MaskEffect group(std::vector<MaskEffect> children){auto effect=node(MaskEffectKind::Group);effect.group_effects=std::make_shared<const std::vector<MaskEffect>>(std::move(children));return effect;}
}
int main(){try{
  const auto first=node(MaskEffectKind::Fill),levels=node(MaskEffectKind::Levels),paint=node(MaskEffectKind::Paint),filter=node(MaskEffectKind::Filter);
  const auto sibling=group({filter}),inner=group({levels,paint}),outer=group({first,inner,sibling}),other=group({node(MaskEffectKind::Fill)});
  std::vector<MaskEffect> effects{outer,other};const auto before=effects;
  require(find_mask_effect_path(effects,paint.id)==std::optional<MaskEffectPath>{{0,1,1}},"Nested UUID has wrong storage path");
  require(find_mask_effect(effects,paint.id)==&effects[0].group_effects->at(1).group_effects->at(1),"Const lookup changed nested identity");
  const auto entries=mask_effect_entries(effects);const std::vector<QUuid> expected{outer.id,first.id,inner.id,levels.id,paint.id,sibling.id,filter.id,other.id,other.group_effects->front().id};
  require(entries.size()==expected.size(),"Preorder omitted nested items");
  for(std::size_t i=0;i<expected.size();++i){require(entries[i].effect->id==expected[i],"Storage preorder changed");require(entries[i].depth+1==entries[i].path.size(),"Display depth does not match path");}
  const auto reversed=mask_effect_entries(effects,true);
  const std::vector<QUuid> reverse_expected{other.id,other.group_effects->front().id,outer.id,sibling.id,filter.id,inner.id,paint.id,levels.id,first.id};
  for(std::size_t i=0;i<reverse_expected.size();++i)require(reversed[i].effect->id==reverse_expected[i],"Reverse sibling display traversal changed");
  require(effects[0].group_effects==before[0].group_effects,"Read-only traversal detached ancestors");
  require(!edit_mask_effect(effects,QUuid::createUuid()),"Missing UUID unexpectedly resolved");
  require(!edit_mask_effect(effects,paint.id,MaskEffectKind::Levels),"Wrong-kind mutation accepted");
  require(!edit_mask_effect_siblings(effects,{0,88,1}),"Invalid path accepted");
  require(effects[0].group_effects==before[0].group_effects&&effects[1].group_effects==before[1].group_effects,"Failed lookup detached a Group");
  auto *edited=edit_mask_effect(effects,paint.id,MaskEffectKind::Paint);require(edited,"Nested Paint mutation missing");edited->paint.resolution=256;
  require(effects[0].group_effects!=before[0].group_effects,"Selected ancestor was not detached");
  require(effects[0].group_effects->at(1).group_effects!=before[0].group_effects->at(1).group_effects,"Nested ancestor was not detached");
  require(effects[0].group_effects->at(2).group_effects==before[0].group_effects->at(2).group_effects,"Unchanged cousin Group lost sharing");
  require(effects[1].group_effects==before[1].group_effects,"Unchanged root sibling Group lost sharing");
  require(find_mask_effect(before,paint.id)->paint.resolution==paint.paint.resolution,"Mutation changed Undo/preset snapshot");
  auto non_group=node(MaskEffectKind::Fill);non_group.group_effects=outer.group_effects;
  auto null_group=node(MaskEffectKind::Group),empty_group=group({});
  std::vector<MaskEffect> malformed{non_group,null_group,empty_group};
  require(!find_mask_effect(malformed,paint.id),"Traversal entered a non-Group payload");
  require(mask_effect_entries(malformed).size()==3,"Null or empty Group guard changed");
  require(!edit_mask_effect_siblings(malformed,{0,0}),"Mutation descended through wrong kind");
  require(!edit_mask_effect_siblings(malformed,{1,0}),"Mutation descended through null Group");
  auto paint_b=node(MaskEffectKind::Paint);std::vector<MaskEffect> bulk{group({paint,paint_b}),other};const auto bulk_snapshot=bulk;
  const auto writable=edit_mask_effects_of_kind(bulk,MaskEffectKind::Paint);
  require(writable.size()==2,"Bulk Paint traversal missed a leaf");
  require(writable[0]==&bulk[0].group_effects->at(0)&&writable[1]==&bulk[0].group_effects->at(1),"Bulk visitor returned unstable pointers");
  writable[0]->paint.resolution=128;writable[1]->paint.resolution=256;
  require(bulk[0].group_effects->at(0).paint.resolution==128&&bulk[0].group_effects->at(1).paint.resolution==256,"Bulk leaf mutation invalidated an earlier reference");
  require(bulk[1].group_effects==bulk_snapshot[1].group_effects,"Bulk visitor detached a Group with no Paint");
  require(bulk_snapshot[0].group_effects->at(0).paint.resolution==paint.paint.resolution,"Bulk edit changed immutable snapshot");
  TextureSet set;set.layers.front().mask=LayerMask{};set.layers.front().mask->effects=bulk_snapshot;
  const auto shared=set.layers.front().mask->effects[0].group_effects;
  int count=0;visit_effect_paint(set,[&](QUuid,QUuid,bool,const EffectPaint &){++count;});
  require(count==2&&set.layers.front().mask->effects[0].group_effects==shared,"Const callback on mutable document detached owners");
  EffectPaint *saved=nullptr;count=0;
  visit_effect_paint(set,[&](QUuid,QUuid,bool,EffectPaint &p){if(!saved)saved=&p;p.resolution=128*(++count);});
  require(count==2&&saved==&find_mask_effect(set.layers.front().mask->effects,paint.id)->paint,"Archive visitor invalidated earlier mutable leaf reference");
  require(saved->resolution==128&&find_mask_effect(set.layers.front().mask->effects,paint_b.id)->paint.resolution==256,"Mutable recursive Paint visitor omitted a leaf");
  require(set.layers.front().mask->effects[1].group_effects==bulk_snapshot[1].group_effects,"Recursive Paint visitor detached unrelated Group");
  auto stripped=bulk_snapshot;erase_mask_effects_of_kind(stripped,MaskEffectKind::Paint);
  require(stripped[0].group_effects->empty(),"Recursive Paint erasure omitted children");
  require(stripped[1].group_effects==bulk_snapshot[1].group_effects,"Recursive Paint erasure detached unchanged sibling");
  require(bulk_snapshot[0].group_effects->size()==2,"Recursive Paint erasure changed source");
  {
    TextureRamQueryScope memory(counted_ram);budget_queries=0;
    const auto rows=mask_effect_entries(before);require(!rows.empty()&&budget_queries==1,"Traversal queried available RAM per row");
    budget_queries=0;auto one=before;edit_mask_effect(one,paint.id,MaskEffectKind::Paint);require(budget_queries==1,"Typed edit queried available RAM per ancestor");
    budget_queries=0;auto all=before;edit_mask_effects_of_kind(all,MaskEffectKind::Paint);require(budget_queries==1,"Bulk edit queried available RAM per leaf");
    const std::vector<MaskEffect> empty,flat{first,levels,paint,filter};
    budget_queries=0;require(!find_mask_effect(empty,paint.id)&&budget_queries==0,"Empty const lookup queried RAM");
    budget_queries=0;require(find_mask_effect(flat,paint.id)==&flat[2]&&budget_queries==0,"Flat const lookup queried RAM");
    budget_queries=0;require(!find_mask_effect(flat,QUuid::createUuid())&&budget_queries==0,"Missing flat lookup queried RAM");
    auto null_group=node(MaskEffectKind::Group);const std::vector<MaskEffect> empty_groups{null_group,group({})};
    budget_queries=0;require(find_mask_effect(empty_groups,null_group.id)==&empty_groups[0]&&budget_queries==0,"Empty Group lookup queried RAM");
    const auto is_paint=[](const MaskEffect &effect){return effect.kind==MaskEffectKind::Paint;};
    budget_queries=0;require(!any_active_mask_effect(empty,is_paint)&&budget_queries==0,"Empty dependency lookup queried RAM");
    budget_queries=0;require(any_active_mask_effect(flat,is_paint)&&budget_queries==0,"Flat dependency lookup queried RAM");
    budget_queries=0;require(!any_active_mask_effect(empty_groups,is_paint)&&budget_queries==0,"Empty Group dependency queried RAM");
    budget_queries=0;require(find_mask_effect(before,paint.id)&&budget_queries==1,"Nested const lookup lost one guarded snapshot");
    budget_queries=0;require(any_active_mask_effect(before,is_paint)&&budget_queries==1,"Nested dependency lookup lost one guarded snapshot");
    const std::vector<MaskEffect> shadowed{group({first}),first};
    budget_queries=0;require(find_mask_effect(shadowed,first.id)==&shadowed[0].group_effects->at(0)&&budget_queries==1,"Nested lookup changed DFS preorder");
  }
  std::printf("MASK_EFFECT_PATH PASS checks=%d ancestor_only_cow=1 const_ownership_changes=0\n",checks);return 0;
}catch(const std::exception &error){std::fprintf(stderr,"MASK_EFFECT_PATH FAILED: %s\n",error.what());return 1;}}
