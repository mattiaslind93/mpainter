#pragma once
// CPU admission and the exact production queue/scheduler, without a GPU owner.
#include "NativeMaterialContext.hh"
#include "NativeMaterialViewContext.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bts::native_fragment_view_context_test {
inline void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
template<class Operation>inline void rejects_view(const Operation &operation,const char *message){
 bool rejected=false;
 try{operation();}catch(const NativeFragmentContextError &error){rejected=error.code==NativeFragmentContextCode::View;}
 require(rejected,message);
}
inline bool same_payload(const NativeMaterialViewContext &a,const NativeMaterialViewContext &b){
 return a.valid==b.valid&&a.position==b.position&&a.forward==b.forward;
}
}

namespace bts {
inline QJsonObject verify_native_fragment_view_context(){
 using namespace native_fragment_view_context_test;
 using Matrix=std::array<float,16>;
 const Matrix initial_view{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-5,1};
 const Matrix orbit_view{0,0,1,0, 0,1,0,0, -1,0,0,0, 0,0,-5,1};
 const Matrix perspective{1,0,0,0, 0,1,0,0, 0,0,-1.02f,-1, 0,0,-.2f,0};
 const Matrix orthographic{.5f,0,0,0, 0,.5f,0,0, 0,0,-.2f,0, 0,0,-1,1};
 NativeMaterialGpuContext bound;
 require(!bound.view.valid,"Default context invented a real camera");
 rejects_view([&]{bound.view.validate();},"Default context did not reject a missing view");
 rejects_view([&]{NativeMaterialViewContext{}.signature();},"Default camera acquired an output signature");
 bind_native_material_view(initial_view,perspective,bound);
 const auto initial=bound.view;
 require(initial.position==std::array<float,4>{0,0,5,1}&&initial.forward==std::array<float,4>{0,0,1,0},"Actual inverse-view binder lost initial camera position/direction");
 require(bound.camera_position==std::array<float,3>{0,0,5}&&bound.camera_forward==std::array<float,3>{0,0,1}&&bound.perspective,"Actual binder did not publish matching legacy camera fields");
 bind_native_material_view(orbit_view,perspective,bound);
 const auto orbit=bound.view;
 require(orbit.position==std::array<float,4>{5,0,0,1}&&orbit.forward==std::array<float,4>{1,0,0,0},"Actual inverse-view binder lost orbit camera position/direction");
 bind_native_material_view(initial_view,perspective,bound);
 require(same_payload(bound.view,initial)&&bound.view.signature()==initial.signature(),"Actual camera restore did not restore the exact payload/signature");
 // A rejected bind must publish none of its candidate camera fields.
 const auto reject_bind=[&](const Matrix &view,const Matrix &projection){
  const auto before=bound.view;const auto position=bound.camera_position,forward=bound.camera_forward;const bool is_perspective=bound.perspective;
  rejects_view([&]{bind_native_material_view(view,projection,bound);},"Invalid real matrices were not a typed view rejection");
  require(same_payload(bound.view,before)&&bound.camera_position==position&&bound.camera_forward==forward&&bound.perspective==is_perspective,"Rejected camera bind partially changed the previous valid context");
 };
 auto invalid=initial_view;invalid[0]=std::numeric_limits<float>::quiet_NaN();reject_bind(invalid,perspective);
 invalid=perspective;invalid[5]=std::numeric_limits<float>::infinity();reject_bind(initial_view,invalid);
 invalid=initial_view;invalid[0]=0;reject_bind(invalid,perspective);
 invalid=perspective;invalid[0]=0;reject_bind(initial_view,invalid);
 invalid=initial_view;invalid[3]=.5f;reject_bind(invalid,perspective);
 invalid=perspective;invalid[3]=1;reject_bind(initial_view,invalid);
 auto bad_payload=initial;bad_payload.forward={0,0,0,0};rejects_view([&]{bad_payload.validate();},"Degenerate camera forward was admitted");
 bad_payload=initial;bad_payload.position[3]=2;rejects_view([&]{bad_payload.validate();},"Invalid camera projection marker was admitted");
 bad_payload=initial;bad_payload.forward[3]=1;rejects_view([&]{bad_payload.validate();},"Reserved camera lane was admitted");
 auto tiny=initial;tiny.forward={1e-25f,0,0,0};tiny.validate();
 // No arbitrary near-zero epsilon is substituted for a finite nonzero vector.
 require(tiny.signature()!=initial.signature(),"Finite nonzero direction lost its actual output identity");
 auto fov_clip=perspective;fov_clip[0]=2;fov_clip[5]=3;fov_clip[10]=-1.04f;fov_clip[14]=-.4f;
 const auto zoom=capture_native_material_view(initial_view,fov_clip);
 require(zoom.signature()==initial.signature(),"FOV/clip-only change rebaked unchanged perspective Incoming");
 const auto ortho=capture_native_material_view(orbit_view,orthographic);
 require(ortho.position==std::array<float,4>{5,0,0,0}&&ortho.forward==std::array<float,4>{1,0,0,0},"Actual orthographic binder lost projection/direction");
 auto pan_view=orbit_view;pan_view[14]=-7;
 auto ortho_zoom=orthographic;ortho_zoom[0]=.25f;ortho_zoom[5]=.125f;ortho_zoom[10]=-.1f;ortho_zoom[14]=-.5f;
 const auto pan=capture_native_material_view(pan_view,ortho_zoom);
 require(pan.position==std::array<float,4>{7,0,0,0}&&pan.signature()==ortho.signature(),"Orthographic pan/FOV/clip changed a direction-only output signature");

 // Drive the same callback-connected route used by MaterialInspection. These
 // are real retained job objects; no worker or renderer is started by the test.
 NativeWorldNormalContextRoute route;
 int view_provider_calls=0,object_provider_calls=0,geometry_provider_calls=0,output_calls=0;
 std::uint64_t geometry_revision=27;
 std::shared_ptr<NativeWorldNormalGeometryJob> job;
 bool output_active=false;std::size_t completed=0;constexpr std::size_t tile_count=4;
 std::string output_signature;
 const auto observe_owners=[&]{++object_provider_calls;return !job;};
 const auto invalidate_geometry=[&]{route.invalidate_owners();++geometry_revision;if(job)job->cancelled=true;job.reset();};
 const auto missing_geometry=[&]{return !job;};
 const auto capture_geometry=[&]{++geometry_provider_calls;job=std::make_shared<NativeWorldNormalGeometryJob>();};
 const auto prepare_output=[&]{
  ++output_calls;const auto signature=route.view.signature();const bool dirty=signature!=output_signature;
  route.capture_view();output_active=true;
  if(dirty){output_signature=signature;completed=0;}return dirty;
 };
 const auto queue=[&](bool view_only){route.view.get().validate();return route.queue(view_only,observe_owners,invalidate_geometry,prepare_output,missing_geometry,capture_geometry);};
 require(route.view.notify(true,[&]{++view_provider_calls;return initial;}),"Production observer did not bootstrap the real camera");
 require(queue(false)&&object_provider_calls==1&&geometry_provider_calls==1&&job,"Production route failed to connect initial owner/capture providers");
 const auto retained_job=job;const auto retained_revision=geometry_revision;
 object_provider_calls=geometry_provider_calls=output_calls=0;
 const auto unchanged_generation=route.view.generation();view_provider_calls=0;
 require(!route.view.notify(false,[&]{++view_provider_calls;return orbit;})&&view_provider_calls==0,"Unchanged notification invoked the camera provider");
 require(!route.view.notify(true,[&]{++view_provider_calls;return initial;})&&route.view.generation()==unchanged_generation,"Equal camera notification changed output generation");
 require(route.view.notify(true,[&]{return orbit;})&&route.view.generation()==unchanged_generation+1&&queue(true),"Production orbit event did not queue changed output");
 require(route.view.notify(true,[&]{return initial;})&&route.view.generation()==unchanged_generation+2&&queue(true)&&route.queued_view().signature()==initial.signature(),"Production camera restore failed");
 require(route.view.notify(true,[&]{return ortho;})&&route.view.generation()==unchanged_generation+3&&queue(true),"Projection transition failed to invalidate view output");
 const auto ortho_generation=route.view.generation();const int before_pan_outputs=output_calls;
 require(!route.view.notify(true,[&]{return pan;})&&route.view.generation()==ortho_generation,"Orthographic pan changed production output generation");
 require(same_payload(route.view.get(),pan)&&output_calls==before_pan_outputs,"Orthographic observer lost latest full pose or queued an unchanged output");
 require(!route.view.notify(true,[&]{return capture_native_material_view(pan_view,orthographic);}),"Orthographic FOV/clip-only update changed output generation");
 require(route.view.notify(true,[&]{return initial;})&&queue(true),"Perspective recovery failed");
 const auto before_zoom=route.view.generation();
 require(!route.view.notify(true,[&]{return zoom;})&&route.view.generation()==before_zoom,"Perspective FOV/clip-only update changed output generation");
 require(object_provider_calls==0&&geometry_provider_calls==0&&geometry_revision==retained_revision&&job==retained_job&&!job->cancelled,"Camera orbit/restore/ortho called a geometry owner or changed its retained job/revision");

 // Eight consecutive camera changes must not reset an active four-tile batch.
 // Both physical sides consume one immutable queued view for each generation.
 int tiles_advanced=0,side_snapshots=0,coalesced_requeues=0;
 output_calls=0;
 for(int generation=0;generation<2;++generation){
  const auto queued=route.queued_view();const auto queued_generation=route.queued_view_generation();
  require(completed==0,"New output generation did not start at its first tile");
  for(std::size_t tile=0;tile<tile_count;++tile){
   auto moving=initial_view;moving[12]=-float(1+generation*int(tile_count)+int(tile));
   const auto latest=capture_native_material_view(moving,perspective);
   require(route.view.notify(true,[&]{return latest;}),"Changed camera frame was lost by the observer");
   require(!NativeWorldNormalContextRoute::view_update_ready(true,output_active,completed,tile_count),"Camera event restarted an unfinished multi-tile generation");
   for(int side=0;side<2;++side){(void)side;require(same_payload(route.queued_view(),queued)&&route.queued_view_generation()==queued_generation,"Queued camera snapshot changed between tiles or physical sides");++side_snapshots;}
   ++completed;++tiles_advanced;
  }
  require(NativeWorldNormalContextRoute::view_update_ready(true,output_active,completed,tile_count),"Completed generation did not release the coalesced latest camera");
  require(queue(true)&&same_payload(route.queued_view(),route.view.get()),"Coalesced camera requeue did not capture the latest actual view");++coalesced_requeues;
 }
 require(tiles_advanced==8&&side_snapshots==16&&coalesced_requeues==2&&output_calls==2,"Camera-every-frame scheduler starved or duplicated tiles");
 require(!NativeWorldNormalContextRoute::view_update_ready(false,false,0,0)&&NativeWorldNormalContextRoute::view_update_ready(true,false,0,tile_count),"Idle/clean view scheduling changed");
 require(object_provider_calls==0&&geometry_provider_calls==0&&geometry_revision==retained_revision&&job==retained_job&&!job->cancelled,"Multi-tile camera updates changed geometry ownership");

 const auto before_invalid_generation=route.view.generation();
 require(route.view.notify(true,[]{return NativeMaterialViewContext{};}),"Invalid camera did not change observer availability");
 rejects_view([&]{queue(true);},"Invalid production view reached the owner/output route");
 output_active=false;
 require(!NativeWorldNormalContextRoute::producer_pending(output_active,bool(job))&&job==retained_job&&!job->cancelled,"Invalid view left a parked geometry job busy or cancelled it");
 const auto invalid_generation=route.view.generation();
 require(invalid_generation==before_invalid_generation+1&&!route.view.notify(true,[]{return NativeMaterialViewContext{};})&&route.view.generation()==invalid_generation,"Repeated invalid camera did not settle");
 require(route.view.notify(true,[&]{return initial;})&&queue(true),"Valid camera did not recover the parked output");
 require(NativeWorldNormalContextRoute::producer_pending(output_active,bool(job))&&job==retained_job&&geometry_revision==retained_revision&&!job->cancelled,"Valid view did not resume the same retained geometry job/revision");
 require(object_provider_calls==0&&geometry_provider_calls==0,"Invalid/recovered camera queried geometry owners");
 // A view event cannot repair a failed geometry capture every frame. Actual
 // owner/source events still retain the explicit retry route.
 job.reset();output_active=false;output_signature.clear();output_calls=0;
 require(!queue(true)&&output_calls==0&&object_provider_calls==0&&geometry_provider_calls==0&&geometry_revision==retained_revision,"Camera-only event retried missing failed geometry");
 require(queue(false)&&object_provider_calls==1&&geometry_provider_calls==1&&job,"Actual owner/source event lost the explicit geometry retry");

 // A real geometry event while camera admission is parked must survive until
 // valid-view recovery; unlike a failed capture, it owns one explicit retry.
 require(route.view.notify(true,[]{return NativeMaterialViewContext{};}),"Pending owner test did not park the camera");
 invalidate_geometry();output_active=false;output_signature.clear();object_provider_calls=geometry_provider_calls=output_calls=0;
 const auto pending_owner_revision=geometry_revision;
 rejects_view([&]{queue(true);},"Invalid camera reached pending owner capture");
 require(object_provider_calls==0&&geometry_provider_calls==0&&!NativeWorldNormalContextRoute::producer_pending(output_active,bool(job)),"Parked owner revision queried providers or stayed busy");
 require(route.view.notify(true,[&]{return initial;})&&queue(true)&&object_provider_calls==1&&geometry_provider_calls==1&&job,"Camera recovery lost the real pending owner capture");
 require(geometry_revision==pending_owner_revision+1,"Owner recovery did not execute its actual owner validation");
 object_provider_calls=geometry_provider_calls=0;
 require(route.view.notify(true,[&]{return orbit;})&&queue(true)&&object_provider_calls==0&&geometry_provider_calls==0,"Recovered owner was queried again during ordinary orbit");

 const auto fields=native_fragment_push_fields(true,true,true,true,true);
 const auto count_field=[&](const char *name){return std::count_if(fields.begin(),fields.end(),[&](const auto &field){return field.type==NativeFragmentPushType::Float4&&std::string_view(field.name)==name;});};
 require(count_field("native_view_position")==1&&count_field("native_view_forward")==1,"Canonical compiler declaration lost its two packed camera float4 fields");
 require(native_fragment_push_bytes(fields)==128,"Production aligned worst-case push declaration exceeds or changed its 128-byte budget");
 for(bool tangent:{false,true})for(bool object:{false,true})for(bool side:{false,true})for(bool raster:{false,true})for(bool view:{false,true})require(native_fragment_push_bytes(native_fragment_push_fields(tangent,object,side,raster,view))<=128,"A canonical fragment push combination exceeds the declared budget");
 return {{"complete",true},{"scope","Actual CPU camera matrix binder and production callback route/scheduler; no shader/render/GPU execution or independent backend-layout proof"},{"matrix_orbit_restore_ortho",true},{"view_generation_exact_changed_restore",true},{"default_view_typed_rejected",true},{"invalid_matrix_atomic_rejections",6},{"invalid_payload_typed_rejections",3},{"finite_nonzero_forward_admitted",true},{"FOV_clip_output_unchanged",true},{"ortho_pan_output_unchanged",true},{"latest_ortho_pose_retained",true},{"camera_object_provider_calls",0},{"camera_geometry_provider_calls",0},{"geometry_revision_unchanged",true},{"retained_job_identity",true},{"parked_invalid_view_not_pending",true},{"valid_view_resumes_retained_job",true},{"invalid_camera_notification_settles",true},{"tiles_per_generation",int(tile_count)},{"camera_changes_each_frame",8},{"completed_tiles",tiles_advanced},{"physical_side_snapshots",side_snapshots},{"coalesced_requeues",coalesced_requeues},{"queued_view_stable_per_generation",true},{"no_tile_starvation",true},{"failed_geometry_camera_retry_calls",0},{"source_retry_object_provider_calls",1},{"source_retry_geometry_provider_calls",1},{"pending_owner_invalid_view_recovery",true},{"pending_owner_recovery_object_provider_calls",1},{"pending_owner_recovery_geometry_provider_calls",1},{"recovered_orbit_provider_calls",0},{"declared_push_float4_view_fields",2},{"declared_aligned_push_bytes",128}};
}
}
