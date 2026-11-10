// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "EeveeCompletionSnapshot.hh"
#include <QJsonObject>

namespace bts::pd149_eevee_observation {
inline const char *status_name(EeveeCompletionStatus value)
{
  switch (value) {
    case EeveeCompletionStatus::NoSceneDraw:
      return "NoSceneDraw";
    case EeveeCompletionStatus::Deferred:
      return "Deferred";
    case EeveeCompletionStatus::OtherBackend:
      return "OtherBackend";
    case EeveeCompletionStatus::NoGpuContext:
      return "NoGpuContext";
    case EeveeCompletionStatus::NoViewportData:
      return "NoViewportData";
    case EeveeCompletionStatus::NoEngine:
      return "NoEngine";
    case EeveeCompletionStatus::NotViewport:
      return "NotViewport";
    case EeveeCompletionStatus::Captured:
      return "Captured";
    case EeveeCompletionStatus::DrawFailed:
      return "DrawFailed";
  }
  return "Unknown";
}
inline bool terminal_sampling_state(const EeveeCompletionSnapshot &s)
{
  // A value predicate only. The consumer must separately establish phase, actual
  // new Scene draw and a later completed Paint. This is not GPU/film completion.
  return s.status == EeveeCompletionStatus::Captured && s.host_draw > 0 &&
         s.target_generation > 0 && s.width > 0 && s.height > 0 && s.active_view >= 0 &&
         s.active_view <= 1 && s.sample_count > 0 && s.viewport_sampling_finished &&
         !s.interactive && !s.reset_pending && s.needed_shaders_loaded && s.render_extent_valid &&
         s.queued_shaders == 0 && s.queued_textures == 0;
}
inline QJsonObject snapshot_json(const EeveeCompletionSnapshot &s)
{
  return {{"status", status_name(s.status)},
          {"host_draw", QString::number(s.host_draw)},
          {"scene_target_generation", QString::number(s.target_generation)},
          {"width", s.width},
          {"height", s.height},
          {"active_view", s.active_view},
          {"sample_index", QString::number(s.sample_index)},
          {"sample_count", QString::number(s.sample_count)},
          {"sampling_finished", s.sampling_finished},
          {"viewport_sampling_finished", s.viewport_sampling_finished},
          {"interactive", s.interactive},
          {"reset_pending", s.reset_pending},
          {"needed_shaders_loaded", s.needed_shaders_loaded},
          {"render_extent_valid", s.render_extent_valid},
          {"loaded_shader_groups", QString::number(s.loaded_shader_groups)},
          {"needed_shader_groups", QString::number(s.needed_shader_groups)},
          {"queued_shaders", qint64(s.queued_shaders)},
          {"queued_textures", qint64(s.queued_textures)},
          {"queued_optimizations", qint64(s.queued_optimizations)},
          {"image_render", s.image_render},
          {"playback", s.playback},
          {"navigating", s.navigating},
          {"painting", s.painting},
          {"transforming", s.transforming}};
}
}  // namespace bts::pd149_eevee_observation
