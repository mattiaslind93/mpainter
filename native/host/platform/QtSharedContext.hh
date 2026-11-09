#pragma once
#include <cstdint>
class QOpenGLContext;
namespace bts {
struct SharedShaderContextStats {
  std::uint64_t created = 0, worker_activations = 0, worker_releases = 0, queued_retirements = 0,
                disposed_on_gui = 0, live = 0, pending = 0;
  std::uintptr_t first_worker_thread = 0;
};
void install_shared_ghost_context(QOpenGLContext *root, bool shader_workers = false);
// Worker disposal only queues released Qt wrappers. Call after compiler join,
// on GUI, while the share root and QGuiApplication still exist.
void drain_shared_shader_contexts() noexcept;
SharedShaderContextStats shared_shader_context_stats();
}  // namespace bts
