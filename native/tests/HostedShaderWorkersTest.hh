#pragma once
// Explicit startup diagnostic only; never changes ordinary draw/compile callers.
#include "BlenderHost.hh"
#include "QtSharedContext.hh"
#include "GPU_capabilities.hh"
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>

namespace bts {
inline int test_hosted_shader_workers(BlenderHost &host, const QString &directory)
{
  using namespace blender;
  const auto require = [](bool okay, const char *message) {
    if (!okay)
      throw std::runtime_error(message);
  };
  const auto owner = reinterpret_cast<std::uintptr_t>(QThread::currentThreadId());
  AsyncCompilationHandle handle = 0;
  std::unique_ptr<gpu::Shader, decltype(&GPU_shader_free)> shader{nullptr, GPU_shader_free};
  std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)> input{nullptr, GPU_texture_free},
      output{nullptr, GPU_texture_free};
  QJsonObject proof{
      {"kind", "hosted ShaderCompiler worker/shared-result/join diagnostic"},
      {"native_pid", qint64(QCoreApplication::applicationPid())},
      {"renderer", QString::fromStdString(host.renderer())},
      {"scope", "Opt-in prerequisite only; existing sync callers and EEVEE unchanged"},
      {"latency_claim", false}};
  QString error;
  QElapsedTimer elapsed;
  elapsed.start();
  QEventLoop loop;
  QTimer poll;
  poll.setInterval(16);
  bool busy = false, verified = false;
  const auto cleanup = [&] {
    host.with_gpu([&] {
      // Cancellation can wait for an already-running compile. This is terminal
      // cleanup, not a claim of nonblocking interactive shader cancellation.
      if (handle)
        GPU_shader_async_compilation_cancel(handle);
      shader.reset();
      input.reset();
      output.reset();
    });
  };
  try {
    require(qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1,
            "Worker diagnostic requires explicit hosted worker opt-in");
    host.with_gpu([&] {
      require(!GPU_use_main_context_workaround(),
              "Driver requires main-context compilation; worker path unavailable");
      // This existing static CreateInfo, including source dependencies, stays alive
      // until GPU_exit. No stack-local Python CreateInfo is submitted to a worker.
      const auto *info = GPU_shader_create_info_get("compositor_write_output");
      require(info, "Existing compositor copy CreateInfo unavailable");
      handle = GPU_shader_async_compilation(info);
      require(handle != 0, "Async shader submission failed");
      proof["shader_info"] = "compositor_write_output";
      proof["worker_limit"] = GPU_max_parallel_compilations();
    });
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (busy)
        return;
      busy = true;
      const auto guard = qScopeGuard([&] { busy = false; });
      try {
        require(elapsed.elapsed() < 60000, "Hosted worker diagnostic deadline exceeded");
        bool done = false;
        host.with_gpu([&] {
          if (!GPU_shader_async_compilation_is_ready(handle))
            return;
          shader.reset(GPU_shader_async_compilation_finalize(handle));
          require(bool(shader), "Worker shader compilation failed");
          const auto stats = shared_shader_context_stats();
          require(stats.worker_activations > 0 && stats.first_worker_thread &&
                      stats.first_worker_thread != owner,
                  "No actual non-GUI shared compiler worker activation");
          proof["worker_thread"] = QString::number(stats.first_worker_thread, 16);
          proof["GUI_thread"] = QString::number(owner, 16);
          proof["worker_activations_before_dispatch"] = qint64(stats.worker_activations);
          constexpr std::array<std::array<float, 4>, 4> pixels{{{{.25f, -.5f, 2, 1}},
                                                                {{-.25f, .5f, 4, .75f}},
                                                                {{1, -2, .125f, 0}},
                                                                {{.5f, .25f, -.75f, 1}}}};
          input.reset(GPU_texture_create_2d("Hosted worker shared input",
                                            2,
                                            2,
                                            1,
                                            gpu::TextureFormat::SFLOAT_32_32_32_32,
                                            GPU_TEXTURE_USAGE_GENERAL,
                                            pixels.front().data()));
          output.reset(GPU_texture_create_2d("Hosted worker shared result",
                                             2,
                                             2,
                                             1,
                                             gpu::TextureFormat::SFLOAT_16_16_16_16,
                                             GPU_TEXTURE_USAGE_GENERAL,
                                             nullptr));
          require(input && output, "Shared-result texture allocation failed");
          GPU_shader_bind(shader.get());
          const auto unbind = qScopeGuard([&] {
            GPU_texture_unbind_all();
            GPU_texture_image_unbind(output.get());
            GPU_shader_unbind();
          });
          const int lower[2]{0, 0}, upper[2]{1, 1};
          GPU_shader_uniform_2iv(shader.get(), "lower_bound", lower);
          GPU_shader_uniform_2iv(shader.get(), "upper_bound", upper);
          GPU_texture_bind(input.get(), GPU_shader_get_sampler_binding(shader.get(), "input_tx"));
          GPU_texture_image_bind(output.get(), 0);
          GPU_compute_dispatch(shader.get(), 1, 1, 1);
          GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
          auto *raw = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
          require(raw, "Shared-result readback failed");
          const auto free = qScopeGuard([&] { MEM_delete(raw); });
          for (int p = 0; p < 4; ++p)
            for (int c = 0; c < 4; ++c)
              require(raw[p * 4 + c] == pixels[p][c],
                      "Worker-compiled shared shader produced incorrect GPU result");
          proof["shared_GPU_result_exact_components"] = 16;
          proof["static_CreateInfo_lifetime_through_finalize"] = true;
          done = verified = true;
        });
        if (done) {
          poll.stop();
          loop.quit();
        }
      }
      catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
        poll.stop();
        loop.quit();
      }
      catch (...) {
        error = "Unknown hosted worker diagnostic failure";
        poll.stop();
        loop.quit();
      }
    });
    poll.start();
    loop.exec();
  }
  catch (const std::exception &e) {
    error = QString::fromUtf8(e.what());
  }
  catch (...) {
    error = "Unknown hosted worker startup diagnostic failure";
  }
  if (!verified && error.isEmpty())
    error = "Hosted worker diagnostic ended before shared-result verification";
  try {
    cleanup();
    host.shutdown();
  }
  catch (...) {
    qFatal("Hosted worker diagnostic owner shutdown failed");
  }
  const auto stats = shared_shader_context_stats();
  const bool retired = stats.created > 0 && stats.disposed_on_gui == stats.created &&
                       stats.live == 0 && stats.pending == 0 &&
                       stats.worker_releases >= stats.worker_activations;
  proof["contexts_created"] = qint64(stats.created);
  proof["contexts_disposed_on_GUI"] = qint64(stats.disposed_on_gui);
  proof["worker_releases"] = qint64(stats.worker_releases);
  proof["queued_retirements"] = qint64(stats.queued_retirements);
  proof["live_contexts_after_join"] = qint64(stats.live);
  proof["pending_GUI_retirements"] = qint64(stats.pending);
  proof["join_and_owner_disposal_complete"] = retired;
  if (!retired && error.isEmpty())
    error = "Compiler join did not retire all hosted Qt contexts";
  proof["passed"] = error.isEmpty();
  proof["error"] = error;
  proof["diagnostic_elapsed_ms"] = elapsed.elapsed();
  QFile file(directory + "/hosted-shader-workers.json");
  require(file.open(QIODevice::WriteOnly), "Cannot write hosted shader worker proof");
  file.write(QJsonDocument(proof).toJson());
  std::printf("BTS_HOSTED_SHADER_WORKER_TEST passed=%d created=%llu disposed_GUI=%llu live=%llu\n",
              int(error.isEmpty()),
              static_cast<unsigned long long>(stats.created),
              static_cast<unsigned long long>(stats.disposed_on_gui),
              static_cast<unsigned long long>(stats.live));
  std::fflush(stdout);
  return error.isEmpty() ? 0 : 1;
}
}  // namespace bts
