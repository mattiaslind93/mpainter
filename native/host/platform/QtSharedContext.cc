// SPDX-License-Identifier: GPL-3.0-or-later
#include "QtSharedContext.hh"
#include <QOffscreenSurface>
#include <QCoreApplication>
#include <QOpenGLContext>
#include <QThread>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "GHOST_Context.hh"
#include "GPU_context_hosted.hh"
namespace blender {
void BTS_system_gpu_context_callbacks_set(GHOST_IContext *(*)(), void (*)(GHOST_IContext *));
}
namespace bts {
namespace {
QOpenGLContext *share_context = nullptr;
std::mutex retirement_mutex;
SharedShaderContextStats shader_stats;
// Cycles display and opt-in compiler contexts move between worker and UI threads.
// Creating/destroying the offscreen surface stays on the Qt GUI thread.
class SharedContext final : public GHOST_Context {
  std::unique_ptr<QOffscreenSurface> surface;
  std::unique_ptr<QOpenGLContext> context;
  bool shader_worker = false;

 public:
  SharedContext *retired_next = nullptr;  // Intrusive: worker retirement allocates nothing.
  explicit SharedContext(bool worker = false) : GHOST_Context({}), shader_worker(worker)
  {
    surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(share_context->format());
    surface->create();
    context = std::make_unique<QOpenGLContext>();
    context->setFormat(share_context->format());
    context->setShareContext(share_context);
    if (!surface->isValid() || !context->create() ||
        !QOpenGLContext::areSharing(context.get(), share_context))
      throw std::runtime_error("Cannot share the Qt render context");
    context->moveToThread(nullptr);
    if (shader_worker) {
      std::lock_guard lock(retirement_mutex);
      ++shader_stats.created;
      ++shader_stats.live;
    }
  }
  ~SharedContext() override
  {
    if (shader_worker) {
      if (QThread::currentThread() != QCoreApplication::instance()->thread())
        qFatal("Shader Qt surface destroyed outside GUI");
      context->moveToThread(QThread::currentThread());
      std::lock_guard lock(retirement_mutex);
      ++shader_stats.disposed_on_gui;
      --shader_stats.live;
    }
    if (QOpenGLContext::currentContext() == context.get())
      context->doneCurrent();
  }
  GHOST_TSuccess activateDrawingContext() override
  {
    if (!context->thread())
      context->moveToThread(QThread::currentThread());
    if (context->thread() != QThread::currentThread() || !context->makeCurrent(surface.get()))
      return GHOST_kFailure;
    active_context_ = this;
    if (shader_worker && QThread::currentThread() != QCoreApplication::instance()->thread()) {
      std::lock_guard lock(retirement_mutex);
      ++shader_stats.worker_activations;
      if (!shader_stats.first_worker_thread)
        shader_stats.first_worker_thread = reinterpret_cast<std::uintptr_t>(
            QThread::currentThreadId());
    }
    return GHOST_kSuccess;
  }
  GHOST_TSuccess releaseDrawingContext() override
  {
    context->doneCurrent();
    context->moveToThread(nullptr);
    active_context_ = nullptr;
    if (shader_worker && QThread::currentThread() != QCoreApplication::instance()->thread()) {
      std::lock_guard lock(retirement_mutex);
      ++shader_stats.worker_releases;
    }
    return GHOST_kSuccess;
  }
  GHOST_TSuccess initializeDrawingContext() override
  {
    return activateDrawingContext();
  }
  GHOST_TSuccess releaseNativeHandles() override
  {
    return GHOST_kSuccess;
  }
  GHOST_TSuccess swapBufferAcquire() override
  {
    return GHOST_kSuccess;
  }
  GHOST_TSuccess swapBufferRelease() override
  {
    return GHOST_kSuccess;
  }
  unsigned int getDefaultFramebuffer() override
  {
    return 0;
  }
};
SharedContext *retired_shader_contexts = nullptr;
GHOST_IContext *create()
{
  try {
    return new SharedContext;
  }
  catch (const std::exception &e) {
    std::fprintf(stderr, "BTS_SHARED_CONTEXT_FAILED %s\n", e.what());
    return nullptr;
  }
}
void dispose(GHOST_IContext *context)
{
  delete context;
}
GHOST_IContext *create_shader()
{
  try {
    return new SharedContext(true);
  }
  catch (const std::exception &e) {
    std::fprintf(stderr, "BTS_SHADER_SHARED_CONTEXT_FAILED %s\n", e.what());
    return nullptr;
  }
}
void dispose_shader(GHOST_IContext *context)
{
  auto *shared = static_cast<SharedContext *>(context);
  if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
    delete shared;
    return;
  }
  // GPUSecondaryContext has already discarded its GPU wrapper and released the
  // native context. GUI can be joining this worker: never wait for GUI here.
  std::lock_guard lock(retirement_mutex);
  shared->retired_next = retired_shader_contexts;
  retired_shader_contexts = shared;
  ++shader_stats.queued_retirements;
  ++shader_stats.pending;
}
}  // namespace
void drain_shared_shader_contexts() noexcept
{
  {
    std::lock_guard lock(retirement_mutex);
    if (!retired_shader_contexts)
      return;
  }
  if (QThread::currentThread() != QCoreApplication::instance()->thread())
    qFatal("Shader context retirement requires GUI");
  SharedContext *head;
  {
    std::lock_guard lock(retirement_mutex);
    head = retired_shader_contexts;
    retired_shader_contexts = nullptr;
    shader_stats.pending = 0;
  }
  while (head) {
    auto *next = head->retired_next;
    delete head;
    head = next;
  }
}
SharedShaderContextStats shared_shader_context_stats()
{
  std::lock_guard lock(retirement_mutex);
  return shader_stats;
}
void install_shared_ghost_context(QOpenGLContext *root, bool shader_workers)
{
  if (!root) {
    const auto stats = shared_shader_context_stats();
    if (stats.live || stats.pending)
      qFatal("Shader contexts survived compiler join/GUI drain");
  }
  share_context = root;
  blender::BTS_system_gpu_context_callbacks_set(root ? create : nullptr, root ? dispose : nullptr);
  blender::GPU_context_hosted_callbacks_set(root && shader_workers ? create_shader : nullptr,
                                            root && shader_workers ? dispose_shader : nullptr);
}
}  // namespace bts
