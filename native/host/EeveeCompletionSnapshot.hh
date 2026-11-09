// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <type_traits>

namespace blender { struct GPUViewport; }
namespace bts {
enum class EeveeCompletionStatus {
  NoSceneDraw, Deferred, OtherBackend, NoGpuContext, NoViewportData,
  NoEngine, NotViewport, Captured, DrawFailed
};

// Copied immediately after the actual Scene draw; no Blender/GL owner escapes.
// Sampling state describes submitted work, not a GPU fence or presentation.
struct EeveeCompletionSnapshot {
  EeveeCompletionStatus status = EeveeCompletionStatus::NoSceneDraw;
  std::uint64_t host_draw = 0, target_generation = 0;
  int width = 0, height = 0, active_view = -1;
  std::uint64_t sample_index = 0, sample_count = 0;
  bool sampling_finished = false, viewport_sampling_finished = false;
  bool interactive = false, reset_pending = false;
  bool needed_shaders_loaded = false, render_extent_valid = false;
  std::uint64_t loaded_shader_groups = 0, needed_shader_groups = 0;
  std::int64_t queued_shaders = 0, queued_textures = 0, queued_optimizations = 0;
  bool image_render = false, playback = false, navigating = false;
  bool painting = false, transforming = false;
};
static_assert(std::is_trivially_copyable_v<EeveeCompletionSnapshot>);

namespace detail {
// Owner GPU context only, immediately after ED_view3d_draw_offscreen returns.
// Reads existing viewport data; never creates engines, polls workers or submits GL.
EeveeCompletionSnapshot capture_eevee_completion(blender::GPUViewport &) noexcept;
}
}
