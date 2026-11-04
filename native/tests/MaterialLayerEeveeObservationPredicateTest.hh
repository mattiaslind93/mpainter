#pragma once
#include "MaterialLayerEeveeObservation.hh"
#include <QJsonArray>
// Included by the existing SourcePixels NoGL dispatcher after its local,
// always-active assert definition. No CMake target or native engine access.
namespace bts::pd149_eevee_observation {
inline QJsonObject verify_value_predicate()
{
 EeveeCompletionSnapshot ready;
 ready.status=EeveeCompletionStatus::Captured;ready.host_draw=7;ready.target_generation=3;
 ready.width=640;ready.height=480;ready.active_view=0;ready.sample_index=64;ready.sample_count=64;
 ready.sampling_finished=true;ready.viewport_sampling_finished=true;
 ready.needed_shaders_loaded=true;ready.render_extent_valid=true;
 assert(terminal_sampling_state(ready));int positive=1,negative=0;QJsonArray rejected;
 const auto reject=[&](const char *name,const auto &edit){auto changed=ready;edit(changed);assert(!terminal_sampling_state(changed));rejected.append(name);++negative;};
 for(const auto status:{EeveeCompletionStatus::NoSceneDraw,EeveeCompletionStatus::Deferred,
     EeveeCompletionStatus::OtherBackend,EeveeCompletionStatus::NoGpuContext,
     EeveeCompletionStatus::NoViewportData,EeveeCompletionStatus::NoEngine,
     EeveeCompletionStatus::NotViewport,EeveeCompletionStatus::DrawFailed})
  reject(status_name(status),[&](auto &s){s.status=status;});
 reject("unknown_status",[](auto &s){s.status=static_cast<EeveeCompletionStatus>(999);});
 reject("missing_draw",[](auto &s){s.host_draw=0;});
 reject("missing_target",[](auto &s){s.target_generation=0;});
 reject("zero_width",[](auto &s){s.width=0;});reject("negative_width",[](auto &s){s.width=-1;});
 reject("zero_height",[](auto &s){s.height=0;});reject("negative_height",[](auto &s){s.height=-1;});
 reject("missing_view",[](auto &s){s.active_view=-1;});reject("out_of_range_view",[](auto &s){s.active_view=2;});
 reject("missing_sample_budget",[](auto &s){s.sample_count=0;});
 reject("viewport_unfinished",[](auto &s){s.viewport_sampling_finished=false;});
 reject("interactive",[](auto &s){s.interactive=true;});reject("reset_pending",[](auto &s){s.reset_pending=true;});
 reject("needed_shader_unready",[](auto &s){s.needed_shaders_loaded=false;});
 reject("invalid_render_extent",[](auto &s){s.render_extent_valid=false;});
 reject("queued_shader",[](auto &s){s.queued_shaders=1;});reject("queued_texture",[](auto &s){s.queued_textures=1;});
 reject("invalid_negative_shader_queue",[](auto &s){s.queued_shaders=-1;});
 reject("invalid_negative_texture_queue",[](auto &s){s.queued_textures=-1;});
 // Both valid stereo-view indices use the same value contract.
 auto other_view=ready;other_view.active_view=1;assert(terminal_sampling_state(other_view));++positive;
 // The viewport counter and sample_index are different quantities; this pure
 // predicate must not invent an exact per-pixel sample-count requirement.
 auto different_counter=ready;different_counter.sample_index=1;different_counter.sampling_finished=false;
 assert(terminal_sampling_state(different_counter));++positive;
 // Pending optimization is reported, not conflated with initial shader/texture
 // availability or evidence that all GPU/film work has completed.
 auto optimizing=ready;optimizing.queued_optimizations=17;assert(terminal_sampling_state(optimizing));++positive;
 const auto values=snapshot_json(optimizing);assert(values["queued_optimizations"].toDouble()==17);
 assert(!values.contains("GPU_complete")&&!values.contains("film_complete"));
 return {{"positive_cases",positive},{"negative_cases",negative},{"rejected",rejected},
  {"actual_actor_value_predicate",true},{"pending_optimizations_reported",true},
  {"phase_draw_Paint_identity_tested_here",false},{"engine_or_GPU_completion_tested_here",false}};
}
}
