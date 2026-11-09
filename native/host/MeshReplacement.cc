// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshReplacement.hh"
#include "TextureDocument.hh"
#include "BKE_collection.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_remap.hh"
#include "BKE_main.hh"
#include "BKE_idprop.hh"
#include "DNA_camera_types.h"
#include "BKE_material.hh"
#include "BLI_set.hh"
#include "BLI_listbase.h"
#include <algorithm>
#include "DNA_ID.h"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include <set>
namespace bts {
using namespace blender;
MeshReplacement prepare_mesh_replacement(Main &main,const TextureDocument &document,const std::string &path,bool cameras,const AutoUnwrapOptions &unwrap,const UsdImportOptions &usd,const UnitSettings &units,const BlendMeshSelection &blend){
  MeshReplacement result;result.pending=PendingMeshProject::from_scene(main,path,cameras,{},usd,blend);result.pending->prepare_uvs(unwrap);result.summary=result.pending->summary();result.source=result.pending->snapshot();
  if(result.summary.uv_state!=ImportedUvState::AllPolygonMeshes)throw std::invalid_argument("Replacement meshes must have usable UV maps");
  result.authored=std::make_unique<TextureDocument>(document);result.authored->set_import_cameras(cameras);result.authored->set_auto_unwrap(unwrap);result.authored->set_usd_options(usd);result.authored->set_blend_options(is_blend_library(path)?blend:BlendMeshSelection{});result.authored->set_source_units(result.summary.source_units);auto sets=document.texture_sets();std::set<std::uint32_t> active;for(auto &set:sets){if(!set.disabled)active.insert(set.material_session_uid);set.disabled=true;}TextureDocument fresh(document.settings(),result.summary,result.source);
  std::set<std::uint32_t> matched;for(const auto &material:result.summary.materials){auto found=sets.end();for(auto candidate=sets.begin();candidate!=sets.end();++candidate){if(matched.contains(candidate->material_session_uid))continue;Material *native=nullptr;for(Material &value:main.materials)if(value.id.session_uid==candidate->material_session_uid)native=&value;if(native&&material.name==native->id.name+2){if(found==sets.end()||active.contains(candidate->material_session_uid))found=candidate;}}
    if(!material.udim_valid)throw std::invalid_argument("Replacement UV tile coordinates are invalid");
    if(found==sets.end()){sets.push_back(fresh.set(material.session_uid));found=sets.end()-1;}if(material.tiles!=found->tiles)throw std::invalid_argument("Replacement UV tile numbers must match the Texture Set");
    found->disabled=false;matched.insert(found->material_session_uid);result.materials[material.session_uid]=found->material_session_uid;
  }result.authored->stage_texture_sets(std::move(sets));result.authored->set_units(units);
  return result;
}
void adopt_mesh_replacement(Main &main,Scene &scene,MeshReplacement &replacement){
  Set<ID *> before,retired,keep;std::map<Mesh *,int> mesh_users;std::map<Camera *,int> camera_users;FOREACH_SCENE_OBJECT_BEGIN(&scene,object){const auto *group=IDP_GetProperties(&object->id);const auto *marker=group?IDP_GetPropertyFromGroup(group,"bts_imported_camera"):nullptr;const auto *scene_marker=group?IDP_GetPropertyFromGroup(group,"bts_imported_scene_object"):nullptr;if(object->type==OB_EMPTY&&scene_marker&&scene_marker->type==IDP_INT&&IDP_int_get(scene_marker)){retired.add(&object->id);}else if(object->type==OB_MESH){retired.add(&object->id);++mesh_users[reinterpret_cast<Mesh *>(object->data)];}else if(object->type==OB_CAMERA&&marker&&marker->type==IDP_INT&&IDP_int_get(marker)){retired.add(&object->id);++camera_users[reinterpret_cast<Camera *>(object->data)];}}FOREACH_SCENE_OBJECT_END;
  for(Scene &other:main.scenes)if(&other!=&scene){FOREACH_SCENE_OBJECT_BEGIN(&other,object){if(retired.contains(&object->id))throw std::invalid_argument("Cross-scene shared objects are not supported by mesh replacement");}FOREACH_SCENE_OBJECT_END;}
  for(auto [mesh,users]:mesh_users)if(mesh->id.us==users)retired.add(&mesh->id);for(auto [camera,users]:camera_users)if(camera->id.us==users)retired.add(&camera->id);
  ID *id;FOREACH_MAIN_ID_BEGIN(&main,id){before.add(id);}FOREACH_MAIN_ID_END;
  bool committed=false;
  try{
    auto &staged=replacement.pending->merge_into(main);std::map<std::uint32_t,Material *> existing;for(Material &material:main.materials)for(const auto &[incoming,current]:replacement.materials)if(material.id.session_uid==current)existing[current]=&material;
    std::vector<Object *> objects;FOREACH_SCENE_OBJECT_BEGIN(&staged,object){if(object->type!=OB_MESH&&object->type!=OB_CAMERA&&object->type!=OB_EMPTY)throw std::invalid_argument("Replacement contains unsupported objects");objects.push_back(object);}FOREACH_SCENE_OBJECT_END;
    std::map<Object *,std::vector<Material *>> slots;for(auto *object:objects)for(short slot=1;slot<=object->totcol;++slot){auto *material=BKE_object_material_get(object,slot);if(!material||!replacement.materials.contains(material->id.session_uid))throw std::runtime_error("Replacement material binding is invalid");const auto current=replacement.materials.at(material->id.session_uid);if(!existing.contains(current))throw std::runtime_error("Existing Texture Set material disappeared");slots[object].push_back(existing.at(current));if(current==material->id.session_uid)keep.add(&material->id);}
    for(auto *object:objects){for(short slot=1;slot<=object->totcol;++slot)BKE_object_material_assign(&main,object,slots.at(object).at(slot-1),slot,BKE_MAT_ASSIGN_OBDATA);if(!BKE_collection_object_add(&main,scene.master_collection,object))throw std::runtime_error("Cannot link replacement mesh");keep.add(&object->id);if(object->data)keep.add(object->data);}

    // No source-file parsing or application validation remains beyond this destructive boundary.
    committed=true;BKE_id_multi_delete(&main,retired,{.extra_remapping_flags=ID_REMAP_FORCE_UI_POINTERS});
    for(auto *object:objects)if(replacement.source.object_names.contains(object->id.session_uid))BKE_id_rename(main,object->id,replacement.source.object_names.at(object->id.session_uid));
    for(auto *object:objects)if(replacement.summary.camera_names.contains(object->id.session_uid))BKE_id_rename(main,object->id,replacement.summary.camera_names.at(object->id.session_uid));
    Set<ID *> garbage;FOREACH_MAIN_ID_BEGIN(&main,id){if(!before.contains(id)&&!keep.contains(id))garbage.add(id);}FOREACH_MAIN_ID_END;BKE_id_multi_delete(&main,garbage,{.extra_remapping_flags=ID_REMAP_FORCE_UI_POINTERS});
  }catch(...){if(!committed){Set<ID *> garbage;FOREACH_MAIN_ID_BEGIN(&main,id){if(!before.contains(id))garbage.add(id);}FOREACH_MAIN_ID_END;BKE_id_multi_delete(&main,garbage,{.extra_remapping_flags=ID_REMAP_FORCE_UI_POINTERS});}throw;}
}
}
