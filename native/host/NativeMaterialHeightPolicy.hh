#pragma once
#include <QString>
#include <array>
#include <cmath>
#include <stdexcept>

namespace bts {
// Instance interpretation only. This is neither a source capability nor a
// reconstruction of Blender's derivative Bump response.
enum class NativeHeightPolicy {None,RawAuthoredFloatV1};
inline void validate_native_height_policy(NativeHeightPolicy policy){
 if(policy!=NativeHeightPolicy::None&&policy!=NativeHeightPolicy::RawAuthoredFloatV1)throw std::invalid_argument("Unknown native Height interpretation");
}
inline QString native_height_policy_name(NativeHeightPolicy policy){
 switch(policy){
  case NativeHeightPolicy::None:return "none";
  case NativeHeightPolicy::RawAuthoredFloatV1:return "authored-height-raw-v1";
 }
 throw std::invalid_argument("Unknown native Height interpretation");
}
inline NativeHeightPolicy read_native_height_policy(const QString &value){
 if(value=="none")return NativeHeightPolicy::None;
 if(value=="authored-height-raw-v1")return NativeHeightPolicy::RawAuthoredFloatV1;
 throw std::invalid_argument("Unknown native Height interpretation");
}
inline void require_native_height_origin(NativeHeightPolicy policy,float origin){
 if(policy==NativeHeightPolicy::None)return; // Ordinary/legacy fast path.
 validate_native_height_policy(policy);
 if(origin!=.5f)throw std::invalid_argument("Raw authored Height requires canonical 0.5-neutral Bump storage; this legacy stack retains its original interpretation");
}
inline void require_native_height_fill(NativeHeightPolicy policy,float origin,int projection,bool artist_normal){
 if(projection<0||projection>6)throw std::invalid_argument("Invalid native material projection");
 require_native_height_origin(policy,origin);if(policy==NativeHeightPolicy::None)return;
 // All mapped samples use the same authored scalar H. This does not admit
 // Blender's derivative Bump response or relax the source eligibility checks.
 if(artist_normal)throw std::invalid_argument("Raw authored Height does not enable an artist Normal channel");
}
inline void require_native_height_image_replacement(NativeHeightPolicy policy){
 if(policy!=NativeHeightPolicy::None)throw std::invalid_argument("Disable raw authored Height before replacing or resetting native images; the new source requires separate Height validation");
}
struct NativeHeightColorContext {QString name;std::array<float,9> scene_linear_to_xyz{};};
inline void require_native_height_color_context(const NativeHeightColorContext &live,const NativeHeightColorContext &pending,const NativeHeightColorContext &active){
 if(live.name.isEmpty()||pending.name!=live.name||active.name!=live.name)throw std::invalid_argument("Raw authored Height Open requires the same named working space as the current project");
 bool nonzero=false;
 for(std::size_t i=0;i<live.scene_linear_to_xyz.size();++i){
  const float a=live.scene_linear_to_xyz[i],b=pending.scene_linear_to_xyz[i],c=active.scene_linear_to_xyz[i];
  // Exact native float storage. Name equality alone is insufficient: native
  // file adoption chooses its global compiler context from this matrix.
  if(!std::isfinite(a)||!std::isfinite(b)||!std::isfinite(c)||a!=b||a!=c)throw std::invalid_argument("Raw authored Height Open requires identical recorded and active working-space matrices; the previous project is retained");
  nonzero=nonzero||a!=0;
 }
 if(!nonzero)throw std::invalid_argument("Raw authored Height Open has no explicit working-space matrix");
}
struct NativeHeightRequirements {
 bool images=false,global_luminance=false,global_blackbody=false,global_wavelength=false,view=false,ao=false;
 // Set only by inspection of the actual isolated AOV: one raw single-file
 // Image Color -> RGB Separate Color Red -> FLOAT Height, UV coordinates only.
 bool scalar_image=false;
};
inline void validate_native_height_requirements(const NativeHeightRequirements &requirements){
 if(requirements.images&&!requirements.scalar_image)throw std::invalid_argument("Image-backed raw Height requires a single Non-Color Image through RGB Separate Color Red, with UV coordinates; Alpha, implicit color conversion, custom image graphs and UDIM sources are unavailable");
 if(requirements.global_luminance)throw std::invalid_argument("Raw authored Height does not support process-global RGB luminance conversion");
 if(requirements.global_blackbody)throw std::invalid_argument("Raw authored Height does not support process-global Blackbody color conversion");
 if(requirements.global_wavelength)throw std::invalid_argument("Raw authored Height does not support process-global Wavelength color conversion");
 if(requirements.view)throw std::invalid_argument("Raw authored Height currently excludes view-dependent scalar functions");
 if(requirements.ao)throw std::invalid_argument("Raw authored Height currently excludes baked Ambient Occlusion dependencies");
}
}
