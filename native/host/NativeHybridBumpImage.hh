#pragma once
#include "BKE_idprop.hh"
#include "DNA_image_types.h"
namespace bts {
// Private runtime image copies only; original source IDs/archives remain intact.
inline constexpr char native_hybrid_bump_property[]="bts_private_hybrid_bump_v1";
inline int native_hybrid_bump_image(const blender::Image &image){
 const auto *property=blender::IDP_GetPropertyFromGroup_null(image.id.properties,native_hybrid_bump_property);
 return property&&property->type==blender::IDP_INT?property->data.val:0;
}
}
