#pragma once
#include "NativeMaterialSourcePolicy.hh"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace bts {
// Two GPU float4 rows; validity is a CPU admission marker, never a default camera.
struct NativeMaterialViewContext {
 std::array<float,4> position{},forward{}; // position.w: perspective0/1; forward.w: reserved0
 bool valid=false;
 void validate()const{
  const auto reject=[](const char *why){throw NativeFragmentContextError(NativeFragmentContextCode::View,std::string("Native fragment requires a real validated view: ")+why);};
  if(!valid)reject("camera has not been captured");
  for(const auto *row:{&position,&forward})for(float value:*row)if(!std::isfinite(value))reject("camera is nonfinite");
  if((position[3]!=0&&position[3]!=1)||forward[3]!=0)reject("camera projection marker is invalid");
  const double length=double(forward[0])*forward[0]+double(forward[1])*forward[1]+double(forward[2])*forward[2];if(!(length>0)||!std::isfinite(length))reject("camera forward is degenerate");
 }
 std::string signature()const{validate();std::string result;result.reserve(7*sizeof(float));result.append(reinterpret_cast<const char *>(&position[3]),sizeof(float));if(position[3]!=0)for(int i=0;i<3;++i)result.append(reinterpret_cast<const char *>(&position[i]),sizeof(float));for(int i=0;i<3;++i)result.append(reinterpret_cast<const char *>(&forward[i]),sizeof(float));return result;}
};
// Production camera observation and queue route, independently CPU-testable.
// Queue providers are connected here, so a view-only event cannot enumerate
// owners or recapture an already retained geometry/job.
class NativeWorldNormalViewState {
 std::optional<NativeMaterialViewContext> view_;std::string signature_,error_;std::uint64_t generation_=0;
 public:
 template<class Provider>bool notify(bool changed,const Provider &provider){
  if(!changed)return false;
  try{auto candidate=provider();const auto signature=candidate.signature();const bool different=!view_||!error_.empty()||signature_!=signature;view_=candidate;signature_=signature;error_.clear();if(different)++generation_;return different;}
  catch(const std::exception &failure){const bool different=error_!=failure.what()||view_.has_value();view_.reset();signature_.clear();error_=failure.what();if(different)++generation_;return different;}
 }
 const NativeMaterialViewContext &get()const{if(!view_||!error_.empty())throw NativeFragmentContextError(NativeFragmentContextCode::View,error_.empty()?"Native fragment requires a real validated view: camera is unavailable":error_);return *view_;}
 const std::string &signature()const{get();return signature_;}
 std::uint64_t generation()const{return generation_;}
 const std::string &error()const{return error_;}
};
class NativeWorldNormalContextRoute {
 bool owners_valid_=false;std::optional<NativeMaterialViewContext> queued_view_;std::uint64_t queued_view_generation_=0;
 public:
 NativeWorldNormalViewState view;
 // A real owner/mesh revision remains pending even while view admission is parked.
 void invalidate_owners(){owners_valid_=false;}
 static bool producer_pending(bool output_active,bool has_geometry_job){return output_active&&has_geometry_job;}
 static bool view_update_ready(bool dirty,bool output_active,std::size_t completed,std::size_t total){return dirty&&(!output_active||completed>=total);}
 void capture_view(){const auto candidate=view.get();queued_view_=candidate;queued_view_generation_=view.generation();}
 const NativeMaterialViewContext &queued_view()const{if(!queued_view_)throw NativeFragmentContextError(NativeFragmentContextCode::View,"Native fragment queued view is unavailable");return *queued_view_;}
 std::uint64_t queued_view_generation()const{return queued_view_generation_;}

 template<class ObserveOwners,class InvalidateGeometry,class PrepareOutput,class GeometryMissing,class CaptureGeometry>
 bool queue(bool view_only,const ObserveOwners &observe,const InvalidateGeometry &invalidate,const PrepareOutput &prepare_output,const GeometryMissing &missing,const CaptureGeometry &capture){
  const bool bootstrap=!owners_valid_;
  if(!view_only||bootstrap){if(observe())invalidate();owners_valid_=true;}
  // A camera event cannot repair an earlier geometry failure. Retry that
  // capture only for an actual source/owner revision; initial setup is allowed.
  if(view_only&&!bootstrap&&missing())return false;
  const bool dirty=prepare_output();if(dirty&&missing())capture();return dirty;
 }
};
inline std::string_view native_fragment_view_source(){return "vec3 coordinate_incoming(vec3 P){return native_view_position.w!=0.0?normalize(native_view_position.xyz-P):native_view_forward.xyz;}\n";}
enum class NativeFragmentPushType {Float,Float2,Float3,Float4,Mat3,Mat4};
struct NativeFragmentPushField {NativeFragmentPushType type;const char *name;};
// The compiler consumes this same declaration list; CPU tests can validate its
// actual aligned worst-case layout without creating/compiling a GPU shader.
inline std::vector<NativeFragmentPushField> native_fragment_push_fields(bool tangent,bool object,bool side,bool raster,bool view){
 using T=NativeFragmentPushType;std::vector<NativeFragmentPushField> fields;
 if(tangent&&!object)fields.push_back({T::Mat3,"native_world_to_object"});
 if(object){fields.push_back({T::Mat4,"native_owner_world_to_object"});fields.push_back({T::Float3,"native_owner_location"});}
 if(side)fields.push_back({T::Float,"native_normal_side"});
 if(raster)fields.push_back({T::Float2,"native_raster_origin"});
 if(view){fields.push_back({T::Float4,"native_view_position"});fields.push_back({T::Float4,"native_view_forward"});}return fields;
}
inline std::size_t native_fragment_push_bytes(const std::vector<NativeFragmentPushField> &fields){
 std::size_t offset=0;for(const auto &field:fields){std::size_t alignment=16,size=0;switch(field.type){case NativeFragmentPushType::Float:alignment=4;size=4;break;case NativeFragmentPushType::Float2:alignment=8;size=8;break;case NativeFragmentPushType::Float3:size=12;break;case NativeFragmentPushType::Float4:size=16;break;case NativeFragmentPushType::Mat3:size=48;break;case NativeFragmentPushType::Mat4:size=64;break;}offset=(offset+alignment-1)&~(alignment-1);offset+=size;}return (offset+15)&~std::size_t(15);
}
}
