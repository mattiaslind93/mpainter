#include "NativeWorldNormalShader.hh"
#include "TextureDocument.hh"
#include "NativeGpuImages.hh"
#include "BKE_idprop.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bts {
using namespace blender;
namespace {
constexpr std::array names{
    "__BTS_NativeWorldPixels0","__BTS_NativeWorldPixels1",
    "__BTS_NativeWorldGeometry","__BTS_NativeWorldSide",
    "__BTS_NativeWorldUnpremultiply","__BTS_NativeWorldDirection",
    "__BTS_NativeWorldEnabled","__BTS_NativeWorldCoverage",
    "__BTS_NativeWorldMix","__BTS_NativeWorldFinal"};
constexpr std::array types{
    "ShaderNodeTexImage","ShaderNodeTexImage","ShaderNodeNewGeometry",
    "ShaderNodeMixRGB","ShaderNodeVectorMath","ShaderNodeVectorMath",
    "ShaderNodeValue","ShaderNodeMath","ShaderNodeMixRGB","ShaderNodeVectorMath"};
constexpr auto owner_key="bts_native_world_normal_owner";
void require(bool condition,const char *reason)
{
  if(!condition)throw std::invalid_argument(std::string("Native world normal: ")+reason);
}
bool local(const ID &id)
{
  return !ID_IS_LINKED(&id)&&!ID_IS_OVERRIDE_LIBRARY(&id)&&
      !(id.tag&(ID_TAG_NO_MAIN|ID_TAG_LOCALIZED|ID_TAG_COPIED_ON_EVAL|ID_TAG_MISSING));
}
void owner(const Main &main,const bNodeTree &tree,const TextureSet &set,
           const bNode &legacy,const bNode &surface)
{
  require(!set.id.isNull()&&!set.tiles.empty(),"texture-set identity or tiles missing");
  for(int tile:set.tiles)require(tile>=1001&&set.width(tile)>0&&set.rows(tile)>0,"invalid texture-set tile dimensions");
  const Material *material=nullptr;
  for(const Material &candidate:main.materials)if(candidate.nodetree==&tree){
    require(!material,"shader tree shared by multiple materials");material=&candidate;
  }
  require(material&&local(material->id),"shader material is not local to Main");
  // Fresh embedded trees deliberately carry NO_MAIN. Their enclosing Material,
  // rather than Main.nodetrees, owns them; copied/restored trees must retain the
  // same explicit ownership relationship. NO_MAIN remains forbidden for Images.
  require((tree.id.flag&ID_FLAG_EMBEDDED_DATA)&&
          BKE_id_owner_get(const_cast<ID *>(&tree.id),false)==&material->id&&
          !ID_IS_LINKED(&tree.id)&&!ID_IS_OVERRIDE_LIBRARY(&tree.id)&&
          !(tree.id.tag&(ID_TAG_LOCALIZED|ID_TAG_COPIED_ON_EVAL|ID_TAG_MISSING)),
          "shader tree is not embedded in its owner material");
  require(material->id.session_uid==set.material_session_uid,"material binding changed");
  bool have_legacy=false,have_surface=false;
  for(const bNode &node:tree.nodes){have_legacy|=&node==&legacy;have_surface|=&node==&surface;}
  require(have_legacy&&have_surface&&&legacy!=&surface,"shader endpoints are not in the owner tree");
  require(STREQ(legacy.idname,"ShaderNodeBump")&&STREQ(surface.idname,"ShaderNodeBsdfPrincipled")&&
          !(legacy.flag&NODE_MUTED)&&!(surface.flag&NODE_MUTED),"legacy or Principled endpoint changed");
}
const bNodeSocket &socket(const bNode &node,bool output,const char *identifier,int type)
{
  const auto &sockets=output?node.outputs:node.inputs;
  const bNodeSocket *found=nullptr;
  for(const bNodeSocket &candidate:sockets)if(STREQ(candidate.identifier,identifier)){
    require(!found,"duplicate socket identifier");found=&candidate;
  }
  require(found&&found->type==type&&!(found->flag&SOCK_UNAVAIL),"socket type or availability changed");
  return *found;
}
bNodeSocket &socket(bNode &node,bool output,const char *identifier,int type)
{
  return const_cast<bNodeSocket &>(socket(std::as_const(node),output,identifier,type));
}
std::string marker(const TextureSet &set,int side)
{
  return set.id.toString(QUuid::WithoutBraces).toStdString()+":"+std::to_string(side);
}
std::array<const bNode *,10> nodes(const bNodeTree &tree)
{
  std::array<const bNode *,10> result{};
  for(const bNode &node:tree.nodes)for(std::size_t i=0;i<names.size();++i)if(STREQ(node.name,names[i])){
    require(!result[i],"duplicate private node name");result[i]=&node;
    require(STREQ(node.idname,types[i])&&!(node.flag&NODE_MUTED),"private node type or mute changed");
  }
  return result;
}
void image(const Main &main,const bNodeTree &tree,const TextureSet &set,
           const bNode &texture,const Image *wanted,int side)
{
  const Image *owned=nullptr;for(const Image &candidate:main.images)if(&candidate==wanted)owned=&candidate;
  require(owned&&local(owned->id),"image is not owned by Main");
  require(texture.id==&owned->id&&owned->id.properties,"private image identity changed");
  const auto *property=IDP_GetPropertyFromGroup(owned->id.properties,owner_key);
  const auto expected_marker=marker(set,side);
  require(property&&property->type==IDP_STRING&&property->len==int(expected_marker.size()+1)&&
          IDP_string_get(property)&&IDP_string_get(property)[property->len-1]=='\0'&&
          expected_marker==IDP_string_get(property),"image ownership marker changed");
  const bool packed=owned->source==IMA_SRC_FILE&&owned->packedfiles.first;
  require((owned->source==IMA_SRC_GENERATED||owned->source==IMA_SRC_TILED||packed)&&
          ((owned->source==IMA_SRC_TILED)==(set.tiles!=std::set<int>{1001})),"image source or tiling changed");
  require(owned->type==IMA_TYPE_IMAGE||owned->type==IMA_TYPE_UV_TEST,"image type changed");
  require(owned->alpha_mode==IMA_ALPHA_CHANNEL_PACKED&&(owned->flag&IMA_HIGH_BITDEPTH),"signed image precision or alpha convention changed");
  require(STREQ(owned->colorspace_settings.name,IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA)),"world image is not Non-Color");
  std::size_t count=0;std::set<int> seen;
  for(const ImageTile &tile:owned->tiles){++count;
    require(set.tiles.contains(tile.tile_number)&&seen.insert(tile.tile_number).second&&
            tile.gen_x==set.width(tile.tile_number)&&tile.gen_y==set.rows(tile.tile_number)&&
            tile.gen_depth==32&&(tile.gen_flag&IMA_GEN_FLOAT),"image tile shape or Float32 storage changed");
  }
  require(count==set.tiles.size(),"image tile membership changed");
  // An ID marker alone cannot authorize using another Binding's image node.
  for(const Material &material:main.materials)if(material.nodetree)
    for(const bNode &node:material.nodetree->nodes)if(node.id==&owned->id)
      require(material.nodetree==&tree&&&node==&texture,"private image aliased by another shader node");
  require(texture.storage,"image sampler storage missing");
  const auto &settings=*static_cast<const NodeTexImage *>(texture.storage);
  require(settings.projection==SHD_PROJ_FLAT&&settings.interpolation==SHD_INTERP_LINEAR&&
          settings.extension==SHD_IMAGE_EXTENSION_REPEAT&&settings.projection_blend==0,
          "image sampling settings changed");
}
struct Edge {const bNode *from;const bNodeSocket *out;const bNode *to;const bNodeSocket *in;};
std::vector<Edge> edges(const std::array<const bNode *,10> &n,const bNode &legacy,const bNode &surface)
{
  std::vector<Edge> result;
  const auto add=[&](const bNode &a,const char *out,int ot,const bNode &b,const char *in,int it){
    result.push_back({&a,&socket(a,true,out,ot),&b,&socket(b,false,in,it)});
  };
  add(*n[2],"Backfacing",SOCK_FLOAT,*n[3],"Fac",SOCK_FLOAT);
  add(*n[0],"Color",SOCK_RGBA,*n[3],"Color1",SOCK_RGBA);
  add(*n[1],"Color",SOCK_RGBA,*n[3],"Color2",SOCK_RGBA);
  add(*n[3],"Color",SOCK_RGBA,*n[4],"Vector",SOCK_VECTOR);
  add(*n[0],"Alpha",SOCK_FLOAT,*n[4],"Vector_001",SOCK_VECTOR);
  add(*n[4],"Vector",SOCK_VECTOR,*n[5],"Vector",SOCK_VECTOR);
  add(*n[0],"Alpha",SOCK_FLOAT,*n[7],"Value",SOCK_FLOAT);
  add(*n[6],"Value",SOCK_FLOAT,*n[7],"Value_001",SOCK_FLOAT);
  add(*n[7],"Value",SOCK_FLOAT,*n[8],"Fac",SOCK_FLOAT);
  add(legacy,"Normal",SOCK_VECTOR,*n[8],"Color1",SOCK_RGBA);
  add(*n[5],"Vector",SOCK_VECTOR,*n[8],"Color2",SOCK_RGBA);
  add(*n[8],"Color",SOCK_RGBA,*n[9],"Vector",SOCK_VECTOR);
  add(*n[9],"Vector",SOCK_VECTOR,surface,"Normal",SOCK_VECTOR);
  return result;
}
}

void validate_native_world_normal_shader(const Main &main,const bNodeTree &tree,const TextureSet &set,
    std::array<Image *,2> images,const bNode &legacy,const bNode &surface)
{
  owner(main,tree,set,legacy,surface);const auto n=nodes(tree);
  require(std::all_of(n.begin(),n.end(),[](auto *p){return p;}),"incomplete private topology");
  for(std::size_t i=2;i<n.size();++i)require(!n[i]->id,"unexpected private node ID");
  require(images[0]!=images[1],"front and back image IDs alias");
  for(int side=0;side<2;++side)image(main,tree,set,*n[side],images[side],side);
  require(n[3]->custom1==MA_RAMP_BLEND&&n[3]->custom2==0&&
          n[8]->custom1==MA_RAMP_BLEND&&n[8]->custom2==0,"world blend operation changed");
  require(n[4]->custom1==NODE_VECTOR_MATH_DIVIDE&&n[5]->custom1==NODE_VECTOR_MATH_NORMALIZE&&
          n[9]->custom1==NODE_VECTOR_MATH_NORMALIZE&&n[7]->custom1==NODE_MATH_MULTIPLY&&
          n[7]->custom2==0,"world vector or coverage operation changed");
  const auto &enable=socket(*n[6],true,"Value",SOCK_FLOAT);
  require(enable.default_value,"enable uniform storage missing");
  const float value=static_cast<const bNodeSocketValueFloat *>(enable.default_value)->value;
  require(std::isfinite(value)&&(value==0||value==1),"enable uniform is not zero or one");
  const auto expected=edges(n,legacy,surface);std::vector<bool> seen(expected.size());
  for(const bNodeLink &link:tree.links){
    const bool private_edge=std::find(n.begin(),n.end(),link.fromnode)!=n.end()||
                            std::find(n.begin(),n.end(),link.tonode)!=n.end();
    const bool normal_input=link.tosock==&socket(surface,false,"Normal",SOCK_VECTOR);
    if(!private_edge&&!normal_input)continue;
    auto found=std::find_if(expected.begin(),expected.end(),[&](const Edge &e){return
      e.from==link.fromnode&&e.out==link.fromsock&&e.to==link.tonode&&e.in==link.tosock;});
    require(found!=expected.end(),"unexpected private shader link");
    const auto index=std::size_t(found-expected.begin());
    require(!seen[index]&&(link.flag&NODE_LINK_VALID)&&!(link.flag&NODE_LINK_MUTED)&&
            (link.tosock->flag&SOCK_IS_LINKED),"duplicate, invalid or muted private link");seen[index]=true;
  }
  require(std::all_of(seen.begin(),seen.end(),[](bool p){return p;}),"private shader link missing");
  for(const bNode *node:n)for(const bNodeSocket &input:node->inputs){
    const bool linked=std::any_of(expected.begin(),expected.end(),[&](const Edge &e){return e.in==&input;});
    require(linked==bool(input.flag&SOCK_IS_LINKED),"private socket link state changed");
  }
  // No socket links are allowed beyond the exact topology, including sampler
  // Vector inputs. Stock safe_divide produces zero for zero coverage; signed
  // RGB is unassociated once, normalized, then coverage is applied exactly once.
}

void install_native_world_normal_shader(Main &main,bNodeTree &tree,const TextureSet &set,
    std::array<Image *,2> &images,bNode *&enabled,bNode &legacy,bNode &surface)
{
  owner(main,tree,set,legacy,surface);const auto prior=nodes(tree);
  if(std::any_of(prior.begin(),prior.end(),[](auto *p){return p;})){
    require(prior[0]&&prior[1],"partial private topology");
    auto adopted=images;
    for(int side=0;side<2;++side)if(!adopted[side])
      for(Image &candidate:main.images)if(prior[side]->id==&candidate.id)adopted[side]=&candidate;
    validate_native_world_normal_shader(main,tree,set,adopted,legacy,surface);
    require(!enabled||enabled==prior[6],"enable Binding handle changed");
    images=adopted;enabled=const_cast<bNode *>(prior[6]);return;
  }
  require(!images[0]&&!images[1]&&!enabled,"orphan private Binding handles");
  auto &normal=socket(surface,false,"Normal",SOCK_VECTOR);
  const auto &old_output=socket(legacy,true,"Normal",SOCK_VECTOR);
  bNodeLink *old=nullptr;
  for(bNodeLink &link:tree.links)if(link.tosock==&normal){
    require(!old&&link.fromnode==&legacy&&link.fromsock==&old_output,"legacy Normal input has another source");old=&link;
  }
  require(old,"legacy Normal link missing before installation");
  std::array<bNode *,10> created{};std::array<Image *,2> allocated{};bool retargeted=false;
  try {
    for(std::size_t i=0;i<created.size();++i){
      created[i]=bke::node_add_node(nullptr,tree,UString(types[i]));
      if(!created[i])throw std::bad_alloc();BLI_strncpy(created[i]->name,names[i],sizeof(created[i]->name));
    }
    const float zero[4]{};const bool tiled=set.tiles!=std::set<int>{1001};
    for(int side=0;side<2;++side){
      allocated[side]=native_generated_image(&main,set.width(*set.tiles.begin()),set.rows(*set.tiles.begin()),
          (std::string("BTS Native World ")+std::to_string(side)).c_str(),32,true,IMA_GENTYPE_BLANK,zero,false,true,tiled);
      if(!allocated[side])throw std::bad_alloc();auto &target=*allocated[side];
      target.flag|=IMA_HIGH_BITDEPTH;target.alpha_mode=IMA_ALPHA_CHANNEL_PACKED;
      IDP_AddToGroup(IDP_EnsureProperties(&target.id),IDP_NewString(marker(set,side).c_str(),owner_key));
      for(int number:set.tiles){auto *tile=BKE_image_get_tile(&target,number);
        if(!tile)tile=BKE_image_add_tile(&target,number,nullptr);if(!tile)throw std::bad_alloc();
        tile->gen_x=set.width(number);tile->gen_y=set.rows(number);tile->gen_depth=32;
        tile->gen_type=IMA_GENTYPE_BLANK;tile->gen_flag|=IMA_GEN_FLOAT|IMA_GEN_TILE;std::copy_n(zero,4,tile->gen_color);
      }
      if(!set.tiles.contains(1001))BKE_image_remove_tile(&target,BKE_image_get_tile(&target,1001));
      created[side]->id=&target.id;id_us_plus(&target.id);
      auto &settings=*static_cast<NodeTexImage *>(created[side]->storage);
      settings.projection=SHD_PROJ_FLAT;settings.interpolation=SHD_INTERP_LINEAR;
      settings.extension=SHD_IMAGE_EXTENSION_REPEAT;settings.projection_blend=0;
    }
    for(int i:{3,8}){created[i]->custom1=MA_RAMP_BLEND;created[i]->custom2=0;}
    created[4]->custom1=NODE_VECTOR_MATH_DIVIDE;
    created[5]->custom1=created[9]->custom1=NODE_VECTOR_MATH_NORMALIZE;
    created[7]->custom1=NODE_MATH_MULTIPLY;created[7]->custom2=0;
    static_cast<bNodeSocketValueFloat *>(socket(*created[6],true,"Value",SOCK_FLOAT).default_value)->value=0;
    std::array<const bNode *,10> n{};std::copy(created.begin(),created.end(),n.begin());
    const auto expected=edges(n,legacy,surface);
    for(std::size_t i=0;i+1<expected.size();++i){const auto &e=expected[i];
      bke::node_add_link(tree,*const_cast<bNode *>(e.from),*const_cast<bNodeSocket *>(e.out),
                        *const_cast<bNode *>(e.to),*const_cast<bNodeSocket *>(e.in));
    }
    // Retain the original link allocation so failure can restore the old graph
    // without allocating a replacement link while handling an allocation error.
    old->fromnode=created[9];old->fromsock=const_cast<bNodeSocket *>(expected.back().out);retargeted=true;
    BKE_ntree_update_tag_link_changed(&tree);BKE_ntree_update_after_single_tree_change(main,tree);
    validate_native_world_normal_shader(main,tree,set,allocated,legacy,surface);
    images=allocated;enabled=created[6];
  }
  catch(...){
    if(retargeted){old->fromnode=&legacy;old->fromsock=const_cast<bNodeSocket *>(&old_output);BKE_ntree_update_tag_link_changed(&tree);}
    for(auto *node:created)if(node)bke::node_remove_node(&main,tree,*node,true);
    for(auto *target:allocated)if(target)BKE_id_free(&main,target);
    throw;
  }
}
}
