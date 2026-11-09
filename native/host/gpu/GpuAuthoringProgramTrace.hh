// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GPU_shader.hh"
#include <array>
#include <cstdint>
namespace bts {
// Explicit diagnostic only. No observer, allocations or test dispatches normally.
struct GpuAuthoringProgramTrace {
  std::array<blender::gpu::Shader *, 14> shaders{};
  std::array<std::uint64_t, 14> dispatches{};
  std::uint64_t synchronous_compilations = 0, asynchronous_submissions = 0;
  bool unknown = false;
  static inline thread_local GpuAuthoringProgramTrace *active = nullptr;
};
inline void trace_authoring_compilation()
{
  if (auto *trace = GpuAuthoringProgramTrace::active)
    ++trace->synchronous_compilations;
}
inline void trace_authoring_submission()
{
  if (auto *trace = GpuAuthoringProgramTrace::active)
    ++trace->asynchronous_submissions;
}
inline void trace_authoring_dispatch(blender::gpu::Shader *shader)
{
  if (auto *trace = GpuAuthoringProgramTrace::active) {
    for (int i = 0; i < 14; ++i)
      if (trace->shaders[i] == shader) {
        ++trace->dispatches[i];
        return;
      }
    trace->unknown = true;
  }
}
}  // namespace bts
