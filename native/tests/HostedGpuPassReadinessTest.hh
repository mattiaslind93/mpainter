#pragma once
// Explicit GPUPass lifecycle/query/shared-compute diagnostic. This replaces the
// test pass's codegen base with static compute info: no material appearance or
// generated material-source lifetime claim, and no EEVEE async policy change.
#include "BlenderHost.hh"
#include "QtSharedContext.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_node_types.h"
#include "GPU_compute.hh"
#include "GPU_context_hosted.hh"
#include "GPU_material.hh"
#include "GPU_pass.hh"
#include "GPU_pass_hosted.hh"
#include "GPU_shader.hh"
#include "GPU_shader_hosted_diagnostics.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "gpu_shader_create_info.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <array>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace bts::hosted_gpupass_test {
using namespace blender;
inline void require(bool okay, const char *message)
{
  if (!okay)
    throw std::runtime_error(message);
}
struct Lowering {
  const GPUShaderCreateInfo *compute = nullptr;
  int callbacks = 0;
  bool actual_surface = false, serialized_surface = false;
};
inline void codegen(void *thunk, GPUMaterial *material, GPUCodegenOutput *output)
{
  auto &state = *static_cast<Lowering *>(thunk);
  ++state.callbacks;
  state.actual_surface = GPU_material_has_surface_output(material);
  state.serialized_surface = !output->surface.serialized.empty();
  // Only the base is assigned: GPUPass retains its real GPUCodegenCreateInfo
  // allocation/derived members and owns it through finalize or cache teardown.
  // Static compute resources, interface names and dependencies outlive GPU_exit.
  *reinterpret_cast<gpu::shader::ShaderCreateInfo *>(output->create_info) =
      *reinterpret_cast<const gpu::shader::ShaderCreateInfo *>(state.compute);
}
inline bNodeTree *source_tree()
{
  auto *tree = bke::node_tree_add_tree(
      nullptr, "Hosted GPUPass private surface", "ShaderNodeTree");
  require(tree, "Cannot allocate private shader tree");
  const auto cleanup = qScopeGuard([&] {
    if (tree)
      BKE_id_free_ex(nullptr,
                     &tree->id,
                     LIB_ID_FREE_NO_MAIN | LIB_ID_FREE_NO_USER_REFCOUNT | LIB_ID_FREE_NO_DEG_TAG,
                     false);
  });
  auto *emission = bke::node_add_node(nullptr, *tree, UString("ShaderNodeEmission"));
  auto *output = bke::node_add_node(nullptr, *tree, UString("ShaderNodeOutputMaterial"));
  require(emission && output, "Cannot create private surface nodes");
  output->flag |= NODE_DO_OUTPUT;
  auto *from = bke::node_find_enabled_output_socket(*emission, "Emission");
  auto *to = bke::node_find_enabled_input_socket(*output, "Surface");
  require(from && to, "Private surface sockets missing");
  bke::node_add_link(*tree, *emission, *from, *output, *to);
  BKE_ntree_update_without_main(*tree);
  auto *result = tree;
  tree = nullptr;
  return result;
}
inline int run(BlenderHost &host, const QString &directory)
{
  bool paused = false, positive = false;
  QString error;
  bNodeTree *tree = nullptr;
  ListBaseT<LinkData> materials{};
  GPUPass *pass = nullptr;
  QJsonObject proof{{"kind", "actual GPUPass cache lifecycle/query/shared compute"},
                    {"native_pid", qint64(QCoreApplication::applicationPid())},
                    {"renderer", QString::fromStdString(host.renderer())},
                    {"EEVEE_async_policy_enabled", false},
                    {"material_appearance_claim", false},
                    {"generated_material_source_lifetime_claim", false},
                    {"latency_claim", false}};
  QElapsedTimer elapsed;
  elapsed.start();
  const auto free_tree = [&] {
    if (tree) {
      BKE_id_free_ex(nullptr,
                     &tree->id,
                     LIB_ID_FREE_NO_MAIN | LIB_ID_FREE_NO_USER_REFCOUNT | LIB_ID_FREE_NO_DEG_TAG,
                     false);
      tree = nullptr;
    }
  };
  const auto free_materials = [&] {
    GPU_material_free(&materials);
    pass = nullptr;
    free_tree();
  };
  try {
    const bool requested = qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1;
    const bool enabled = GPU_context_hosted_shader_workers_enabled();
    proof["worker_opt_in"] = requested;
    proof["provider_predicate"] = enabled;
    if (!requested) {
      require(!enabled, "Default provider predicate must be false");
      proof["default_false_control"] = true;
    }
    else {
      require(
          enabled,
          "Hosted worker provider unavailable or real driver workaround requires owner compilation");
      const auto owner = reinterpret_cast<std::uintptr_t>(QThread::currentThreadId());
      // Diagnostic-only setup drains ALL startup phases, not just the initially
      // submitted pair. Flush begins/polls later authoring/derived phases on the
      // owner only after the existing CPU compiler query says no work is active.
      // No document, ordinary UI policy or EEVEE submission path is changed.
      QEventLoop setup_loop;
      QTimer setup_poll;
      setup_poll.setInterval(10);
      bool setup_ready = false;
      std::uint64_t setup_flushes = 0, setup_cpu_wait_ticks = 0;
      QObject::connect(&setup_poll, &QTimer::timeout, &setup_loop, [&] {
        try {
          require(elapsed.elapsed() < 120000, "Host startup setup deadline exceeded");
          if (GPU_shader_compiler_has_pending_work()) {
            ++setup_cpu_wait_ticks;
            return;
          }
          if (host.texture_updates_pending()) {
            host.flush_texture_updates();
            ++setup_flushes;
            return;
          }
          host.with_gpu([&] { GPU_pass_cache_update(); });
          require(!GPU_shader_compiler_has_pending_work() && !host.texture_updates_pending() &&
                      !GPU_pass_cache_has_pending_compilation(),
                  "Setup did not drain all Host/cache phases");
          setup_ready = true;
          setup_poll.stop();
          setup_loop.quit();
        }
        catch (const std::exception &e) {
          error = QString::fromUtf8(e.what());
          setup_poll.stop();
          setup_loop.quit();
        }
        catch (...) {
          error = "Unknown startup-drain diagnostic error";
          setup_poll.stop();
          setup_loop.quit();
        }
      });
      setup_poll.start();
      setup_loop.exec();
      require(setup_ready && error.isEmpty(), "Host startup phases were not fully drained");
      proof["host_startup_fully_drained_before_pass"] = true;
      proof["setup_owner_flushes"] = qint64(setup_flushes);
      proof["setup_CPU_wait_ticks"] = qint64(setup_cpu_wait_ticks);
      const auto contexts_before = shared_shader_context_stats();
      const auto jobs_before = GPU_hosted_shader_worker_compilation_count();
      proof["worker_compilations_before"] = qint64(jobs_before);
      proof["context_activations_before"] = qint64(contexts_before.worker_activations);
      proof["first_context_thread_before"] = QString::number(contexts_before.first_worker_thread,
                                                             16);
      const auto isolated_started = elapsed.elapsed();
      const auto *compute = GPU_shader_create_info_get("compositor_write_output");
      require(compute, "Static copy CreateInfo missing");
      Lowering first{compute};
      std::uintptr_t first_pass = 0;
      GPU_shader_compiler_pause();
      paused = true;
      host.with_gpu([&] {
        tree = source_tree();
        auto result = GPU_material_from_nodetree(nullptr,
                                                 tree,
                                                 &materials,
                                                 "Hosted GPUPass copy",
                                                 GPU_MAT_EEVEE,
                                                 0xb7514301u,
                                                 true,
                                                 codegen,
                                                 &first);
        require(result.material && result.errors.is_empty(), "Private surface lowering failed");
        require(first.callbacks == 1 && first.actual_surface && first.serialized_surface,
                "Actual surface lowering did not reach callback");
        pass = GPU_material_get_pass(result.material);
        require(pass, "Actual GPUPass missing");
        first_pass = reinterpret_cast<std::uintptr_t>(pass);
        require(GPU_pass_status(pass) == GPU_PASS_QUEUED && GPU_pass_shader_get(pass) == nullptr,
                "New pass was not queued");
        require(GPU_pass_cache_has_pending_compilation(), "CPU cache query missed queued pass");
      });
      proof["actual_surface_codegen_callbacks"] = first.callbacks;
      proof["actual_queued_pass"] = true;
      GPU_shader_compiler_resume();
      paused = false;
      QEventLoop loop;
      QTimer poll;
      poll.setInterval(10);
      std::uint64_t ticks = 0;
      bool ready = false;
      QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        // CPU-only callback: no Host.with_gpu, context activation, shader finalize,
        // draw, texture readback or GL calls while a compiler worker is active.
        ++ticks;
        if (elapsed.elapsed() - isolated_started > 60000) {
          error = "Actual GPUPass readiness deadline exceeded";
          poll.stop();
          loop.quit();
          return;
        }
        if (GPU_shader_compiler_has_pending_work())
          return;
        ready = GPU_pass_status(pass) == GPU_PASS_QUEUED && GPU_pass_shader_get(pass) == nullptr &&
                GPU_pass_cache_has_pending_compilation();
        if (!ready)
          error = "CPU query lost ready-but-unfinalized pass";
        poll.stop();
        loop.quit();
      });
      poll.start();
      loop.exec();
      require(error.isEmpty() && ready, "Real ready-but-unfinalized state was not observed");
      proof["CPU_poll_ticks"] = qint64(ticks);
      proof["CPU_poll_GL_calls"] = 0;
      proof["ready_unfinalized_observed"] = ready;
      const auto stats = shared_shader_context_stats();
      const auto jobs_after = GPU_hosted_shader_worker_compilation_count();
      require(!host.texture_updates_pending(),
              "Host submitted unrelated startup phases in the isolated pass window");
      require(jobs_after == jobs_before + 1,
              "Isolated pass did not complete exactly one actual worker compilation");
      require(stats.worker_activations && stats.first_worker_thread &&
                  stats.first_worker_thread != owner,
              "No actual non-GUI compiler context");
      const bool persistent_reuse = stats.created == contexts_before.created &&
                                    stats.worker_activations ==
                                        contexts_before.worker_activations &&
                                    stats.first_worker_thread ==
                                        contexts_before.first_worker_thread;
      proof["worker_compilations_after"] = qint64(jobs_after);
      proof["isolated_worker_compilation_delta"] = qint64(jobs_after - jobs_before);
      proof["contexts_created_before"] = qint64(contexts_before.created);
      proof["contexts_created_after"] = qint64(stats.created);
      proof["context_activations_after"] = qint64(stats.worker_activations);
      proof["persistent_context_reuse_observed"] = persistent_reuse;
      proof["first_context_thread"] = QString::number(stats.first_worker_thread, 16);
      proof["GUI_thread"] = QString::number(owner, 16);
      host.with_gpu([&] {
        const auto before = GPU_pass_global_compilation_count();
        GPU_pass_cache_update();
        require(GPU_pass_status(pass) == GPU_PASS_SUCCESS && GPU_pass_shader_get(pass),
                "Owner cache update did not finalize successfully");
        require(GPU_pass_global_compilation_count() == before + 1 &&
                    !GPU_pass_cache_has_pending_compilation(),
                "Owner finalization/query did not drain exactly one pass");
        auto *shader = GPU_pass_shader_get(pass);
        constexpr std::array<std::array<float, 4>, 4> pixels{{{{.25f, -.5f, 2, 1}},
                                                              {{-.25f, .5f, 4, .75f}},
                                                              {{1, -2, .125f, 0}},
                                                              {{.5f, .25f, -.75f, 1}}}};
        auto *input = GPU_texture_create_2d("GPUPass copy input",
                                            2,
                                            2,
                                            1,
                                            gpu::TextureFormat::SFLOAT_32_32_32_32,
                                            GPU_TEXTURE_USAGE_GENERAL,
                                            pixels.front().data());
        auto *output = GPU_texture_create_2d("GPUPass copy result",
                                             2,
                                             2,
                                             1,
                                             gpu::TextureFormat::SFLOAT_16_16_16_16,
                                             GPU_TEXTURE_USAGE_GENERAL,
                                             nullptr);
        const auto release = qScopeGuard([&] {
          GPU_texture_unbind_all();
          if (output)
            GPU_texture_image_unbind(output);
          GPU_shader_unbind();
          if (input)
            GPU_texture_free(input);
          if (output)
            GPU_texture_free(output);
        });
        require(input && output, "Copy control texture allocation failed");
        GPU_shader_bind(shader);
        const int lower[2]{0, 0}, upper[2]{1, 1};
        GPU_shader_uniform_2iv(shader, "lower_bound", lower);
        GPU_shader_uniform_2iv(shader, "upper_bound", upper);
        const int sampler = GPU_shader_get_sampler_binding(shader, "input_tx");
        require(sampler >= 0, "Actual copy sampler binding missing");
        GPU_texture_bind(input, sampler);
        GPU_texture_image_bind(output, 0);
        GPU_compute_dispatch(shader, 1, 1, 1);
        GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
        auto *raw = static_cast<float *>(GPU_texture_read(output, GPU_DATA_FLOAT, 0));
        require(raw, "Copy readback missing");
        const auto free = qScopeGuard([&] { MEM_delete(raw); });
        for (int p = 0; p < 4; ++p)
          for (int c = 0; c < 4; ++c)
            require(raw[p * 4 + c] == pixels[p][c], "Actual GPUPass copy output differs");
        proof["owner_finalized"] = true;
        proof["shared_compute_exact_components"] = 16;
        free_materials();
      });
      Lowering second{compute};
      GPU_shader_compiler_pause();
      paused = true;
      host.with_gpu([&] {
        tree = source_tree();
        auto result = GPU_material_from_nodetree(nullptr,
                                                 tree,
                                                 &materials,
                                                 "Hosted orphan GPUPass",
                                                 GPU_MAT_COMPOSITOR,
                                                 0xb7514302u,
                                                 true,
                                                 codegen,
                                                 &second);
        require(result.material && result.errors.is_empty() && second.callbacks == 1 &&
                    second.actual_surface && second.serialized_surface,
                "Orphan actual surface lowering failed");
        auto *orphan = GPU_material_get_pass(result.material);
        require(orphan && reinterpret_cast<std::uintptr_t>(orphan) != first_pass,
                "Distinct pass was not created");
        require(GPU_pass_status(orphan) == GPU_PASS_QUEUED, "Orphan pass not queued");
        free_materials();  // Cache owns GPUPass/CreateInfo after list/tree/GPUMaterial die.
        require(GPU_pass_status(orphan) == GPU_PASS_QUEUED &&
                    GPU_pass_cache_has_pending_compilation(),
                "Cache lost orphan after source-owner release");
        proof["distinct_queued_passes"] = 2;
        proof["orphan_retained_after_material_list_tree_free"] = true;
      });
      GPU_shader_compiler_resume();
      paused = false;
      // Terminal wait only, before any shutdown owner GL. Leave ready orphan
      // unfinalized; real GPU_exit owns cache cancellation/destruction.
      GPU_shader_compiler_wait_for_all();
      require(GPU_pass_cache_has_pending_compilation(),
              "Ready orphan was not retained until terminal cache cleanup");
      const auto orphan_jobs = GPU_hosted_shader_worker_compilation_count();
      require(orphan_jobs == jobs_after + 1,
              "Orphan did not complete exactly one actual worker compilation");
      proof["orphan_worker_compilation_delta"] = qint64(orphan_jobs - jobs_after);
      proof["orphan_ready_unfinalized_before_GPU_exit"] = true;
      positive = true;
    }
  }
  catch (const std::exception &e) {
    if (error.isEmpty())
      error = QString::fromUtf8(e.what());
  }
  catch (...) {
    error = "Unknown GPUPass diagnostic failure";
  }
  // Error and normal terminal paths both resume, join, then retire GPU-owned
  // list/tree on the owner, before the real cache/provider/root-context teardown.
  if (paused) {
    GPU_shader_compiler_resume();
    paused = false;
  }
  try {
    GPU_shader_compiler_wait_for_all();
    host.with_gpu(free_materials);
    host.shutdown();
  }
  catch (...) {
    qFatal("GPUPass diagnostic terminal cleanup failed");
  }
  const auto stats = shared_shader_context_stats();
  const bool disposed = stats.live == 0 && stats.pending == 0 &&
                        stats.created == stats.disposed_on_gui &&
                        stats.worker_releases >= stats.worker_activations;
  if (!disposed && error.isEmpty())
    error = "Hosted workers were not joined/disposed on GUI";
  proof["positive_lifecycle_complete"] = positive;
  proof["contexts_created"] = qint64(stats.created);
  proof["disposed_on_GUI"] = qint64(stats.disposed_on_gui);
  proof["live_contexts_after_join"] = qint64(stats.live);
  proof["pending_GUI_retirements"] = qint64(stats.pending);
  proof["join_and_owner_disposal_complete"] = disposed;
  proof["elapsed_ms"] = elapsed.elapsed();
  proof["passed"] = error.isEmpty();
  proof["error"] = error;
  QFile file(directory + "/hosted-gpupass-readiness.json");
  require(file.open(QIODevice::WriteOnly), "Cannot write GPUPass proof");
  file.write(QJsonDocument(proof).toJson());
  std::printf("BTS_HOSTED_GPUPASS_READINESS passed=%d positive=%d disposed=%d\n",
              int(error.isEmpty()),
              int(positive),
              int(disposed));
  std::fflush(stdout);
  return error.isEmpty() ? 0 : 1;
}
}  // namespace bts::hosted_gpupass_test
