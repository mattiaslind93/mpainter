#include "EeveeCompletionSnapshot.hh"

#include "GPU_context.hh"
#include "GPU_viewport.hh"
#include "draw_view_data.hh"
#include "eevee_instance.hh"

namespace bts::detail {
EeveeCompletionSnapshot capture_eevee_completion(blender::GPUViewport &viewport) noexcept
{
  using namespace blender;
  EeveeCompletionSnapshot result;
  if (!GPU_context_active_get()) {
    result.status = EeveeCompletionStatus::NoGpuContext;
    return result;
  }
  result.active_view = GPU_viewport_active_view_get(&viewport);
  auto **data = GPU_viewport_data_get(&viewport);
  if (!data || !*data || result.active_view < 0 || result.active_view > 1 ||
      !(*data)->view_data[result.active_view]) {
    result.status = EeveeCompletionStatus::NoViewportData;
    return result;
  }
  auto *engine = (*data)->view_data[result.active_view]->eevee.instance;
  if (!engine) {
    result.status = EeveeCompletionStatus::NoEngine;
    return result;
  }
  // This slot is created only by eevee::Engine::create_instance. Its owner is
  // this retained viewport. Never access Instance::draw_ctx: that draw scope
  // has already ended, while the instance and its value fields remain alive.
  // Do not gate on engine->used: release_data() already cleared it via
  // DRW_view_data_reset before ED_view3d_draw_offscreen returned.
  const auto &instance = *static_cast<const eevee::Instance *>(engine);
  if (!instance.is_viewport()) {
    result.status = EeveeCompletionStatus::NotViewport;
    return result;
  }
  const auto &sampling = instance.sampling;
  result.sample_index = sampling.sample_index();
  result.sample_count = sampling.sample_count();
  result.sampling_finished = sampling.finished();
  result.viewport_sampling_finished = sampling.finished_viewport();
  result.interactive = sampling.interactive_mode();
  result.reset_pending = sampling.is_reset();
  result.needed_shaders_loaded = instance.is_loaded(instance.needed_shaders);
  result.render_extent_valid = instance.film.is_valid_render_extent();
  result.loaded_shader_groups = std::uint64_t(instance.loaded_shaders);
  result.needed_shader_groups = std::uint64_t(instance.needed_shaders);
  result.queued_shaders = instance.materials.queued_shaders_count;
  result.queued_textures = instance.materials.queued_textures_count;
  result.queued_optimizations = instance.materials.queued_optimize_shaders_count;
  result.image_render = instance.is_image_render;
  result.playback = instance.is_playback;
  result.navigating = instance.is_navigating;
  result.painting = instance.is_painting;
  result.transforming = instance.is_transforming;
  result.status = EeveeCompletionStatus::Captured;
  return result;
}
}
