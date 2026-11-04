#include "SubsurfacePreviewIO.hh"
#include "BKE_idprop.hh"
#include "DNA_scene_types.h"
#include "DEG_depsgraph.hh"
#include <string_view>
namespace bts {using namespace blender;
SubsurfacePreview subsurface_preview(const Scene &scene){SubsurfacePreview result;for(const auto *field:{"bts_subsurface_enabled","bts_subsurface_samples"}){auto *p=scene.id.properties?IDP_GetPropertyFromGroup(scene.id.properties,field):nullptr;if(!p)continue;if(p->type!=IDP_INT)throw std::invalid_argument("Invalid subsurface preview property");const int value=IDP_int_get(p);if(std::string_view(field)=="bts_subsurface_enabled"){if(value!=0&&value!=1)throw std::invalid_argument("Invalid subsurface preview toggle");result.enabled=value;}else result.samples=value;}result.validate();return result;}
void set_subsurface_preview(Main &main,Scene &scene,SubsurfacePreview value){value.validate();auto *properties=IDP_EnsureProperties(&scene.id);IDP_ReplaceInGroup(properties,IDP_NewInt(value.enabled,"bts_subsurface_enabled"));IDP_ReplaceInGroup(properties,IDP_NewInt(value.samples,"bts_subsurface_samples"));DEG_id_tag_update_ex(&main,&scene.id,ID_RECALC_PARAMETERS|ID_RECALC_SYNC_TO_EVAL);}
}
