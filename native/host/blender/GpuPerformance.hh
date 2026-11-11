// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Performance.hh"
#include "GPU_host_metrics.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_storage_buffer_poll.hh"
#include <epoxy/gl.h>
#include <array>
namespace bts {
// Query objects belong to Blender's GL context. Never wait for an unavailable result.
#ifdef __APPLE__
// A tiny storage snapshot carries Metal's asynchronous command completion event.
// GPU timestamp metrics are unavailable here; never report CPU time as GPU time.
class GpuPerformance {
  blender::gpu::StorageBuf *completion_ = nullptr;
  bool pending_ = false;

 public:
  explicit GpuPerformance(const char * = "gpu_render") {}
  bool compute_ready()
  {
    if (!pending_)
      return true;
    unsigned int value = 0;
    if (!blender::GPU_storagebuf_read_if_ready(completion_, &value))
      return false;
    pending_ = false;
    return true;
  }
  void submitted()
  {
    if (!completion_) {
      unsigned int zero = 0;
      completion_ = blender::GPU_storagebuf_create_ex(
          sizeof(zero), &zero, blender::GPU_USAGE_DYNAMIC, "Frame completion");
    }
    blender::GPU_storagebuf_sync_to_host(completion_);
    pending_ = true;
  }
  void poll() {}
  void begin() {}
  void end() {}
  void free()
  {
    if (completion_)
      blender::GPU_storagebuf_free(completion_);
    completion_ = nullptr;
    pending_ = false;
  }
};
#else
class GpuPerformance {
  struct Slot {
    GLuint ids[2]{};
    bool pending = false;
    std::string flow;
  };
  std::string phase_;
  std::array<Slot, 32> slots_{};
  Slot *active_ = nullptr;
  GLsync previous_frame_ = nullptr;

 public:
  explicit GpuPerformance(const char *phase = "gpu_render") : phase_(phase) {}
  bool compute_ready()
  {
    if (!previous_frame_)
      return true;
    const auto status = glClientWaitSync(previous_frame_, 0, 0);
    return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
  }
  void submitted()
  {
    if (previous_frame_)
      glDeleteSync(previous_frame_);
    previous_frame_ = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
  }
  void poll()
  {
    if (!performance::enabled())
      return;
    for (auto &slot : slots_)
      if (slot.pending) {
        GLint ready = 0;
        glGetQueryObjectiv(slot.ids[1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready)
          continue;
        GLuint64 start = 0, end = 0;
        glGetQueryObjectui64v(slot.ids[0], GL_QUERY_RESULT, &start);
        glGetQueryObjectui64v(slot.ids[1], GL_QUERY_RESULT, &end);
        performance::record({slot.flow, phase_, double(end - start) / 1e6, 0, 0, false});
        slot.pending = false;
      }
  }
  void begin()
  {
    if (!performance::enabled())
      return;
    poll();
    for (auto &slot : slots_)
      if (!slot.pending) {
        if (!slot.ids[0])
          glGenQueries(2, slot.ids);
        slot.flow = performance::flow();
        active_ = &slot;
        glQueryCounter(slot.ids[0], GL_TIMESTAMP);
        return;
      }
  }
  void end()
  {
    if (active_) {
      glQueryCounter(active_->ids[1], GL_TIMESTAMP);
      active_->pending = true;
      active_ = nullptr;
    }
  }
  void free()
  {
    if (previous_frame_) {
      glDeleteSync(previous_frame_);
      previous_frame_ = nullptr;
    }
    if (!performance::enabled())
      return;
    end();
    poll();
    for (auto &slot : slots_)
      if (slot.ids[0]) {
        glDeleteQueries(2, slot.ids);
        slot = {};
      }
  }
};
#endif
inline QJsonObject gpu_transfer_snapshot()
{
  using namespace blender::gpu::host_metrics;
  QJsonObject out;
  const char *names[] = {"upload", "readback", "finish", "mipmap"};
  for (int k = 0; k < Count; ++k)
    out[names[k]] = QJsonObject{{"calls", qint64(counters[k].calls.load())},
                                {"bytes", qint64(counters[k].bytes.load())},
                                {"cpu_wall_ms", counters[k].ns.load() / 1e6}};
  return out;
}
}  // namespace bts
