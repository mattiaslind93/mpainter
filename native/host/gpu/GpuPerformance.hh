#pragma once
#include "Performance.hh"
#include "GPU_host_metrics.hh"
#include <epoxy/gl.h>
#include <array>
namespace bts {
// Query objects belong to Blender's GL context. Never wait for an unavailable result.
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
