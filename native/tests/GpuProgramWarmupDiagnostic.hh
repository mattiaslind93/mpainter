#pragma once
// Explicit opt-in diagnostic; ordinary startup never constructs this object.
#include "BlenderHost.hh"
#include "GpuLayerPreview.hh"
#include "GpuDerivedShaders.hh"
#include "GpuDerivedWarmupNumeric.hh"
#include "GPU_shader.hh"
#include "CompositeStatus.hh"
#include "Performance.hh"
#include "QtSharedContext.hh"
#include "GPU_texture.hh"
#include "GPU_shader.hh"
#include "NewProjectDialog.hh"
#include "MEM_guardedalloc.h"
#include <QCoreApplication>
#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QLineEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPointer>
#include <QPaintEvent>
#include <QScopeGuard>
#include <QTimer>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>

namespace bts {
struct GpuProgramWarmupDiagnosticHooks {
  BlenderHost &host;
  std::function<bool()> gpu_ready, empty_view, no_document;
  std::function<std::uint64_t()> frames;
  std::function<QJsonObject()> early_project_state;
  std::function<void()> queue_unchanged_project_set;
};
class GpuProgramWarmupDiagnostic final : public QObject {
  using Image = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  GpuProgramWarmupDiagnosticHooks hooks_;
  QString path_;
  QTimer ping_, poll_;
  QElapsedTimer clock_;
  QJsonObject proof_{{"kind", "actual empty Workspace and two large worker programs"},
                     {"tolerance", 3e-5},
                     {"testing_stack", false},
                     {"latency_claim", false}};
  std::unique_ptr<GpuLayerPreview> preview_;
  Image rgba_{nullptr, blender::GPU_texture_free}, mask_{nullptr, blender::GPU_texture_free};
  std::unique_ptr<GpuDerivedWarmupNumeric> derived_numeric_;
  bool pair_numeric_done_ = false;
  TextureSet set_;
  int stage_ = 0;
  bool closed_ = false, busy_ = false, saw_pending_ = false;
  qint64 last_ping_ = 0, pending_ping_max_ = 0, all_ping_max_ = 0;
  int pings_ = 0, pending_pings_ = 0;
  qint64 ready_at_ = 0;
  std::uint64_t idle_frames_ = 0;
  int idle_polls_ = 0;
  QString early_mesh_;
  QPointer<QWidget> viewport_, root_;
  bool early_actions_ = false, initial_pending_closed_ = false;
  double last_ping_monotonic_ms_ = 0;
  int private_pending_skips_ = 0, early_action_stage_ = 0;
  qulonglong early_previous_generation_ = 0;
  QJsonArray early_action_times_;
  std::uint64_t guard_frames_before_ = 0;
  qint64 guard_counter_before_ = 0;
  double guard_posted_ms_ = 0;
  static double monotonic_ms()
  {
    return std::chrono::duration<double, std::milli>(performance::Clock::now().time_since_epoch())
        .count();
  }
  void close_initial_pending()
  {
    if (early_mesh_.isEmpty() || initial_pending_closed_ || !hooks_.early_project_state)
      return;
    const auto state = hooks_.early_project_state();
    if (state["deferred_executions"].toInt() == 0)
      return;
    const double boundary = state["execution_monotonic_ms"].toDouble();
    require(std::isfinite(boundary) && boundary >= last_ping_monotonic_ms_,
            "Invalid New execution timing boundary");
    pending_ping_max_ = std::max(pending_ping_max_,
                                 qint64(std::ceil(boundary - last_ping_monotonic_ms_)));
    initial_pending_closed_ = true;
    proof_["initial_pending_closed_at_deferred_execution"] = true;
  }
  template<class T> T *visible(const char *name)
  {
    require(root_, "Early New root window disappeared");
    T *result = nullptr;
    for (auto *item : root_->findChildren<T *>(QString::fromLatin1(name)))
      if (item->isVisible()) {
        require(!result, "Multiple visible early New controls");
        result = item;
      }
    require(result, "Early New control is not visible");
    return result;
  }
  int active_progress() const
  {
    int count = 0;
    if (root_)
      for (auto *item : root_->findChildren<QProgressDialog *>("newProjectProgramProgress"))
        count += item->isVisible();
    return count;
  }
  void open_new(int resolution)
  {
    require(blender::GPU_shader_compiler_has_pending_work(),
            "Compiler finished before early New event path");
    auto *action = root_ ? root_->findChild<QAction *>("newProject") : nullptr;
    require(action && action->isEnabled(), "New QAction unavailable");
    action->trigger();
    require(active_progress() == 0, "Opening New did not retire previous intent");
    auto *dialog = visible<QDialog>("newProjectDialog");
    auto *preset = dialog->findChild<QComboBox *>("projectTemplate");
    auto *mesh = dialog->findChild<QLineEdit *>("meshPath");
    auto *size = dialog->findChild<QComboBox *>("documentResolution");
    auto *create = dialog->findChild<QPushButton *>("createProject");
    require(preset && mesh && size && create, "New controls missing");
    const int template_index = preset->findData(":pbr"), size_index = size->findData(resolution);
    require(template_index >= 0 && size_index >= 0, "New fixture choices unavailable");
    preset->setCurrentIndex(template_index);
    mesh->setText(early_mesh_);
    size->setCurrentIndex(size_index);
    require(create->isEnabled(), "Synthetic New mesh rejected");
    require(hooks_.no_document() && hooks_.empty_view(), "Opening New changed the document");
  }
  void create_new()
  {
    require(blender::GPU_shader_compiler_has_pending_work(),
            "Compiler finished before early Create event path");
    auto *create = visible<QDialog>("newProjectDialog")->findChild<QPushButton *>("createProject");
    require(create && create->isEnabled(), "Early Create control unavailable");
    create->click();
    require(hooks_.no_document() && hooks_.empty_view(),
            "Early New changed the document before readiness");
    require(hooks_.early_project_state()["deferred_executions"].toInt() == 0,
            "Early New executed while compiler pending");
    require(active_progress() == 1, "Early New did not retain exactly one intent");
  }
  void early_new_action_step()
  {
    static constexpr const char *names[] = {"gpu_early_new_open_cancelled",
                                            "gpu_early_new_create_cancelled",
                                            "gpu_early_new_cancel",
                                            "gpu_early_new_open_superseded",
                                            "gpu_early_new_create_superseded",
                                            "gpu_early_new_open_latest",
                                            "gpu_early_new_create_latest"};
    require(early_action_stage_ >= 0 && early_action_stage_ < 7, "Invalid early New action stage");
    const auto start = performance::Clock::now();
    const auto cpu = performance::thread_ms();
    const auto *name = names[early_action_stage_];
    const auto record = [&](bool failed) {
      early_action_times_.append(QJsonObject{{"action", name},
                                             {"wall_ms", performance::ms(start)},
                                             {"cpu_ms", performance::thread_ms() - cpu},
                                             {"failed", failed}});
      proof_["early_new_action_times"] = early_action_times_;
    };
    try {
      performance::Scope timing(name);
      switch (early_action_stage_) {
        case 0:
          require(hooks_.no_document() && hooks_.empty_view(),
                  "Early New must start with no document");
          open_new(256);
          break;
        case 1:
          create_new();
          break;
        case 2: {
          auto *progress = visible<QProgressDialog>("newProjectProgramProgress");
          QPushButton *cancel = nullptr;
          for (auto *button : progress->findChildren<QPushButton *>())
            if (button->isVisible() && button->isEnabled())
              cancel = button;
          require(cancel, "New Cancel button unavailable");
          cancel->click();
          require(active_progress() == 0 && hooks_.no_document(),
                  "Cancel did not retire early New");
          proof_["early_new_cancelled"] = true;
          break;
        }
        case 3:
          open_new(512);
          break;
        case 4:
          create_new();
          early_previous_generation_ = visible<QProgressDialog>("newProjectProgramProgress")
                                           ->property("newGeneration")
                                           .toULongLong();
          break;
        case 5:
          open_new(128);
          break;
        case 6: {
          create_new();
          const auto latest = visible<QProgressDialog>("newProjectProgramProgress")
                                  ->property("newGeneration")
                                  .toULongLong();
          require(latest != early_previous_generation_,
                  "Superseding New did not replace generation");
          proof_["early_new_superseded"] = true;
          proof_["early_new_latest_resolution"] = 128;
          early_actions_ = true;
          break;
        }
      }
    }
    catch (...) {
      record(true);
      throw;
    }
    record(false);
    ++early_action_stage_;  // Yield: the next user action needs a new timer event.
  }
  bool early_new_ready()
  {
    close_initial_pending();
    if (hooks_.no_document())
      return false;
    const auto state = hooks_.early_project_state();
    proof_["early_project_state"] = state;
    require(early_actions_ && initial_pending_closed_ &&
                state["deferred_executions"].toInt() == 1 &&
                !state["execution_compiler_pending"].toBool(),
            "Early New did not execute exactly once after compiler readiness");
    require(!hooks_.empty_view() && state["resolution"].toInt() == 128 &&
                QFileInfo(state["mesh"].toString()).absoluteFilePath() == early_mesh_ &&
                state["sets"].toInt() > 0,
            "Latest New did not create the actual document/viewport");
    require(active_progress() == 0, "Executed early New left progress alive");
    require(pending_pings_ > 0 && proof_["pending_status_seen"].toBool(),
            "Early New did not observe pending pings/status");
    require(pending_ping_max_ <= 100, "Owner ping exceeded 100 ms before New readiness");
    double pair_max = 0;
    for (const auto &row : performance::snapshot()) {
      const auto phase = row.toObject();
      if (phase["phase"] == "gpu_stack_pair_poll")
        pair_max = std::max(pair_max, phase["wall_ms"].toDouble());
    }
    proof_["pair_poll_max_ms"] = pair_max;
    require(pair_max <= 50, "Owner pair poll exceeded 50 ms");
    proof_["early_project_created"] = true;
    proof_["early_project_observed_ms"] = clock_.elapsed();
    return true;
  }
  static void require(bool okay, const char *message)
  {
    if (!okay)
      throw std::runtime_error(message);
  }
  int pair_polls() const
  {
    int count = 0;
    for (const auto &v : performance::snapshot())
      if (v.toObject()["phase"] == "gpu_stack_pair_poll")
        ++count;
    return count;
  }
  void write()
  {
    QFile file(path_);
    require(file.open(QIODevice::WriteOnly), "Cannot write warmup diagnostic");
    file.write(QJsonDocument(proof_).toJson());
  }
  void verify_resource_owner_lifetime()
  {
    using namespace blender;
    // Source is destroyed before inspection, mirroring submission then queued work.
    auto copy = [](int index) {
      auto source = [&]() {
        switch (index) {
          case 0:
            return gpu_derived::coverage_info();
          case 1:
            return gpu_derived::presence_info();
          case 2:
            return gpu_derived::ao_info(gpu::TextureFormat::SFLOAT_32_32_32_32, false, "");
          case 3:
            return gpu_derived::normal_info(gpu::TextureFormat::SFLOAT_32_32_32_32, false, "");
          default:
            throw std::logic_error("Unknown resource lifetime builder");
        }
      }();
      // Explicit copy, not a move or reliance on NRVO; source dies before inspection.
      return gpu::shader::ShaderCreateInfo(source);
    };
    int checked = 0;
    for (int index = 0; index < 4; ++index) {
      auto info = copy(index);
      for (const auto *resources :
           {&info.pass_resources_, &info.batch_resources_, &info.geometry_resources_})
        for (const auto &resource : *resources) {
          require(resource.info_name.c_str() == gpu_derived::hosted_resource_owner &&
                      resource.info_name == "pyGPU_Shader",
                  "Queued resource namespace does not survive source destruction");
          ++checked;
        }
      for (const auto &variable : info.shared_variables_) {
        require(variable.info_name.c_str() == gpu_derived::hosted_resource_owner &&
                    variable.info_name == "pyGPU_Shader",
                "Queued shared-variable namespace does not survive source destruction");
        ++checked;
      }
    }
    require(checked > 16, "Resource namespace lifetime fixture is incomplete");
    proof_["resource_owner_lifetime_checks"] = checked;
  }
  void setup_numeric()
  {
    using namespace blender;
    verify_resource_owner_lifetime();
    preview_ = std::make_unique<GpuLayerPreview>();
    set_.texture_resolution = 64;
    set_.precision[0] = ChannelPrecision::RGB32F;
    set_.layers.front().kind = LayerKind::Fill;
    set_.layers.front().values.base_color = {.125f, .25f, .5f, 1};
    TextureDocument::add_fill(set_);
    auto &top = set_.layers.back();
    top.values.base_color = {.875f, .625f, .25f, 1};
    top.opacity[0] = .625f;
    top.mask.emplace();
    top.mask->background = 0;
    auto source = std::make_shared<FillTexture>();
    source->width = source->height = 4;
    source->raw = true;
    source->asset = "41874536-32de-40be-8b52-204142e3c108";
    source->name = "Worker mask fixture";
    source->hash = "d2a3833ee6335d6531c5c8f5bcdf366780a1604b761947de1bb3c7d7a6b0178b";
    auto pixels = std::make_shared<std::vector<FillTexture::Pixel>>();
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) {
        const float v = float((x + 2 * y) % 4 + 1) / 4;
        pixels->push_back({v, v, v, 1});
      }
    source->pixels = pixels;
    source->validate();
    MaskEffect effect;
    effect.kind = MaskEffectKind::Fill;
    effect.opacity = .75f;
    effect.mapping.filtering = 1;
    effect.mapping.projection.filtering = 2;
    effect.mapping.projection.texture = source;
    top.mask->effects.push_back(effect);
    set_.selected_layer = top.id;
    rgba_.reset(GPU_texture_create_2d("Worker RGBA oracle",
                                      64,
                                      64,
                                      1,
                                      gpu::TextureFormat::SFLOAT_32_32_32_32,
                                      GPU_TEXTURE_USAGE_GENERAL,
                                      nullptr));
    mask_.reset(GPU_texture_create_2d("Worker scalar oracle",
                                      64,
                                      64,
                                      1,
                                      gpu::TextureFormat::SFLOAT_32_32_32_32,
                                      GPU_TEXTURE_USAGE_GENERAL,
                                      nullptr));
    require(rgba_ && mask_, "Warmup diagnostic output allocation failed");
    // This new preview uses the same actual async pair, never self_test. It is
    // polled without begin_frame until its own pair is ready, just like the host.
    preview_->begin_frame();
  }
  bool pair_numeric()
  {
    using namespace blender;
    preview_->poll_programs();
    if (preview_->programs_pending())
      return false;
    const auto begin = performance::Clock::now();
    preview_->begin_frame();
    proof_["remaining_owner_warmup_max_ms"] = std::max(
        proof_["remaining_owner_warmup_max_ms"].toDouble(), performance::ms(begin));
    const auto color = preview_->update(set_, 0, rgba_.get()),
               mask = preview_->update(set_, 15, mask_.get());
    require(color != GpuPreviewUpdate::Unsupported && mask != GpuPreviewUpdate::Unsupported,
            "Large program numerical update rejected");
    if (color == GpuPreviewUpdate::Pending || mask == GpuPreviewUpdate::Pending)
      return false;
    auto *a = static_cast<float *>(GPU_texture_read(rgba_.get(), GPU_DATA_FLOAT, 0));
    auto *b = static_cast<float *>(GPU_texture_read(mask_.get(), GPU_DATA_FLOAT, 0));
    const auto free = qScopeGuard([&] {
      if (a)
        MEM_delete(a);
      if (b)
        MEM_delete(b);
    });
    require(a && b, "Warmup numerical readback failed");
    double color_error = 0, mask_error = 0;
    QJsonObject worst;
    std::uint64_t finite = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const int sx = x / 16, sy = y / 16;
        const double m = double((sx + 2 * sy) % 4 + 1) / 4 * .75, weight = m * .625;
        constexpr std::array<double, 3> under{.125, .25, .5}, over{.875, .625, .25};
        for (int c = 0; c < 4; ++c) {
          const auto index = (y * 64 + x) * 4 + c;
          require(std::isfinite(a[index]) && std::isfinite(b[index]),
                  "Nonfinite large program output");
          finite += 2;
          const double color_expected = c == 3 ? 1 : under[c] + (over[c] - under[c]) * weight,
                       mask_expected = c == 3 ? 1 : m;
          const double ce = std::abs(a[index] - color_expected),
                       me = std::abs(b[index] - mask_expected);
          if (std::max(ce, me) > std::max(color_error, mask_error))
            worst = {{"x", x},
                     {"y", y},
                     {"component", c},
                     {"rgba_actual", a[index]},
                     {"rgba_expected", color_expected},
                     {"mask_actual", b[index]},
                     {"mask_expected", mask_expected}};
          color_error = std::max(color_error, ce);
          mask_error = std::max(mask_error, me);
        }
      }
    proof_["RGBA_max_error"] = color_error;
    proof_["spatial_R32F_mask_max_error"] = mask_error;
    proof_["finite_components"] = qint64(finite);
    proof_["worst"] = worst;
    require(color_error <= 3e-5 && mask_error <= 3e-5,
            "Large worker program output differs from independent formula");
    return true;
  }
  bool numeric()
  {
    if (!pair_numeric_done_) {
      if (!pair_numeric())
        return false;
      pair_numeric_done_ = true;
    }
    if (!derived_numeric_)
      derived_numeric_ = std::make_unique<GpuDerivedWarmupNumeric>(set_);
    if (!derived_numeric_->advance(*preview_))
      return false;
    proof_["derived_outputs"] = derived_numeric_->result();
    return true;
  }
  void finish(QString error)
  {
    if (closed_)
      return;
    try {
      close_initial_pending();
    }
    catch (const std::exception &e) {
      if (error.isEmpty())
        error = QString::fromUtf8(e.what());
    }
    catch (...) {
      if (error.isEmpty())
        error = "Unknown New timing boundary failure";
    }
    proof_["diagnostic_stage"] = stage_;
    proof_["private_pending_skipped_polls"] = private_pending_skips_;
    if (!early_mesh_.isEmpty() && hooks_.early_project_state)
      proof_["early_project_state"] = hooks_.early_project_state();
    all_ping_max_ = std::max(all_ping_max_, clock_.elapsed() - last_ping_);
    proof_["passed"] = error.isEmpty();
    proof_["error"] = error;
    proof_["elapsed_ms"] = clock_.elapsed();
    proof_["pending_ping_max_ms"] = pending_ping_max_;
    proof_["all_phase_ping_max_ms"] = all_ping_max_;
    proof_["pings"] = pings_;
    proof_["pending_pings"] = pending_pings_;
    proof_["performance"] = performance::snapshot();
    retire();
    try {
      write();
    }
    catch (const std::exception &e) {
      std::fprintf(stderr, "BTS_GPU_WARMUP_PROOF_FAILED %s\n", e.what());
      error = QString::fromUtf8(e.what());
    }
    std::printf("BTS_GPU_PROGRAM_WARMUP_TEST passed=%d output=%s\n",
                int(error.isEmpty()),
                qPrintable(path_));
    std::fflush(stdout);
    QCoreApplication::exit(error.isEmpty() ? 0 : 1);
  }
  void advance()
  {
    if (closed_ || busy_)
      return;
    busy_ = true;
    const auto guard = qScopeGuard([&] { busy_ = false; });
    try {
      require(clock_.elapsed() < 180000, "Warmup diagnostic deadline exceeded");
      if (early_mesh_.isEmpty())
        require(hooks_.empty_view() && hooks_.no_document(),
                "Warmup diagnostic left actual empty Workspace");
      if (!hooks_.gpu_ready())
        return;
      require(pending_composite_error.empty(),
              "GPU stack reported an error during empty-view polling");
      if (!early_mesh_.isEmpty() && stage_ == 0) {
        if (!saw_pending_) {
          require(hooks_.no_document() && hooks_.empty_view(), "Early New did not start empty");
          if (!hooks_.host.texture_updates_pending() ||
              !blender::GPU_shader_compiler_has_pending_work())
            return;
          saw_pending_ = true;
          last_ping_ = clock_.elapsed();
          last_ping_monotonic_ms_ = monotonic_ms();
          proof_["pending_status_seen"] = hooks_.host.viewport_texture_status().contains(
              "Preparing GPU");
        }
        if (!early_actions_) {
          early_new_action_step();
          return;
        }
        if (!early_new_ready())
          return;
        ready_at_ = clock_.elapsed();
        stage_ = 1;
        return;
      }
      if (!early_mesh_.isEmpty() && stage_ == 1) {
        if (hooks_.host.texture_updates_pending() ||
            blender::GPU_shader_compiler_has_pending_work())
          return;
        require(hooks_.queue_unchanged_project_set && !hooks_.no_document() &&
                    !hooks_.empty_view(),
                "Nonempty guard stress needs the actual New document");
        // Requeue the existing set, without editing it, before starting this private
        // compiler pair. Both are real pending work when the Qt paint is posted.
        hooks_.queue_unchanged_project_set();
        hooks_.host.with_gpu([&] { setup_numeric(); });
        const bool host_pending = hooks_.host.texture_updates_pending(),
                   compiler_pending = blender::GPU_shader_compiler_has_pending_work();
        require(host_pending && compiler_pending && preview_->programs_pending(),
                "No real pending interval for nonempty paint delivery");
        guard_frames_before_ = hooks_.frames();
        guard_counter_before_ = hooks_.early_project_state()["nonempty_guard_waits"].toInteger();
        guard_posted_ms_ = monotonic_ms();
        proof_["nonempty_guard_stress"] = QJsonObject{
            {"kind", "explicit queued paint with unchanged Host set and private compiler work"},
            {"host_pending_before_post", host_pending},
            {"compiler_pending_before_post", compiler_pending},
            {"private_preview_pending_before_post", preview_->programs_pending()},
            {"nonempty_before_post", !hooks_.empty_view()},
            {"posted_monotonic_ms", guard_posted_ms_},
            {"frames_before", qint64(guard_frames_before_)},
            {"guard_counter_before", guard_counter_before_}};
        QCoreApplication::postEvent(
            viewport_, new QPaintEvent(viewport_->rect()), Qt::HighEventPriority);
        stage_ = 4;
        return;
      }
      if (!early_mesh_.isEmpty() && stage_ == 4) {
        const auto state = hooks_.early_project_state();
        const auto count = state["nonempty_guard_waits"].toInteger();
        if (count == guard_counter_before_) {
          require(hooks_.host.texture_updates_pending() &&
                      blender::GPU_shader_compiler_has_pending_work(),
                  "Queued nonempty paint was not guarded during real pending work");
          return;
        }
        const auto delivered = state["nonempty_guard_monotonic_ms"].toDouble();
        require(count > guard_counter_before_ && delivered >= guard_posted_ms_ &&
                    state["nonempty_guard_frame"].toInteger() == qint64(guard_frames_before_),
                "Queued paint did not reach the real pre-GL guard without a frame");
        auto stress = proof_["nonempty_guard_stress"].toObject();
        stress["delivered_monotonic_ms"] = delivered;
        stress["delivery_ms"] = delivered - guard_posted_ms_;
        stress["guard_counter_after"] = count;
        stress["frames_at_guard_delivery"] = state["nonempty_guard_frame"];
        proof_["nonempty_guard_stress"] = stress;
        proof_["nonempty_guard_observed"] = true;
        stage_ = 3;
        return;
      }
      if (stage_ == 0) {
        if (hooks_.host.texture_updates_pending()) {
          if (!saw_pending_)
            last_ping_ = clock_.elapsed();
          saw_pending_ = true;
          proof_["pending_status_seen"] = hooks_.host.viewport_texture_status().contains(
              "Preparing GPU");
          return;
        }
        require(saw_pending_ && pending_pings_ > 0,
                "No actual pre-document pending interval observed");
        require(pending_ping_max_ <= 100, "Owner ping exceeded 100 ms during actual pair pending");
        double pair_max = 0;
        for (const auto &row : performance::snapshot()) {
          const auto phase = row.toObject();
          if (phase["phase"] == "gpu_stack_pair_poll")
            pair_max = std::max(pair_max, phase["wall_ms"].toDouble());
        }
        proof_["pair_poll_max_ms"] = pair_max;
        require(pair_max <= 50, "Owner pair poll exceeded 50 ms");
        proof_["pending_to_ready_ms"] = clock_.elapsed();
        proof_["empty_frames_at_ready"] = qint64(hooks_.frames());
        proof_["no_owner_begin_frame_from_test_while_pending"] = true;
        ready_at_ = clock_.elapsed();
        stage_ = 1;
        return;
      }
      if (stage_ == 1) {
        if (clock_.elapsed() - ready_at_ < 100)
          return;
        idle_frames_ = hooks_.frames();
        idle_polls_ = pair_polls();
        require(idle_polls_ > 0, "No actual owner pair-poll callback observed");
        stage_ = 2;
        return;
      }
      if (stage_ == 2) {
        if (clock_.elapsed() - ready_at_ < 250)
          return;
        require(!hooks_.host.texture_updates_pending() && hooks_.frames() == idle_frames_ &&
                    pair_polls() == idle_polls_,
                "Empty view or pair poll remained active after ready");
        proof_["ready_to_idle_frames_stable"] = true;
        proof_["pair_poll_stopped"] = true;
        proof_["pair_poll_calls_before_idle"] = idle_polls_;
        hooks_.host.with_gpu([&] { setup_numeric(); });
        stage_ = 3;
        return;
      }
      // Private previews are not in Host's pending set: do not enter owner GL
      // merely to query their readiness while their workers are compiling.
      if (preview_ && preview_->programs_pending() &&
          blender::GPU_shader_compiler_has_pending_work())
      {
        ++private_pending_skips_;
        return;
      }
      bool done = false;
      hooks_.host.with_gpu([&] { done = numeric(); });
      if (done) {
        const auto stats = shared_shader_context_stats();
        require(stats.worker_activations > 0, "No actual compiler worker activation");
        proof_["worker_activations"] = qint64(stats.worker_activations);
        finish({});
      }
    }
    catch (const std::exception &e) {
      finish(QString::fromUtf8(e.what()));
    }
    catch (...) {
      finish("Unknown warmup diagnostic failure");
    }
  }

 public:
  GpuProgramWarmupDiagnostic(QObject *parent, QString path, GpuProgramWarmupDiagnosticHooks hooks)
      : QObject(parent), hooks_(std::move(hooks)), path_(std::move(path))
  {
    require(qEnvironmentVariableIntValue("BTS_HOSTED_SHADER_WORKERS") == 1 &&
                !qEnvironmentVariableIsSet("BTS_VERIFY_GPU_STACK"),
            "Warmup diagnostic requires actual worker path, not self_test");
    require(performance::enabled() && !performance::session_logging() &&
                qEnvironmentVariableIntValue("BTS_HEADLESS_WORKSPACE") == 1,
            "Warmup diagnostic requires profiling and actual offscreen Workspace");
    if (qEnvironmentVariableIsSet("BTS_GPU_EARLY_PROJECT_TEST_MESH")) {
      const QFileInfo mesh(qEnvironmentVariable("BTS_GPU_EARLY_PROJECT_TEST_MESH"));
      require(mesh.isAbsolute() && mesh.isFile() &&
                  mesh.suffix().compare("obj", Qt::CaseInsensitive) == 0,
              "Early New requires an absolute own OBJ fixture");
      early_mesh_ = mesh.absoluteFilePath();
      require(bool(hooks_.early_project_state), "Early New state hook missing");
      viewport_ = qobject_cast<QWidget *>(parent);
      require(viewport_, "Early New viewport missing");
      root_ = viewport_->window();
      proof_["early_project_fixture"] = early_mesh_;
      proof_["early_new_mode"] = true;
    }
    clock_.start();
    last_ping_monotonic_ms_ = monotonic_ms();
    ping_.setInterval(10);
    poll_.setInterval(10);
    QObject::connect(&ping_, &QTimer::timeout, this, [this] {
      try {
        const auto now = clock_.elapsed(), gap = now - last_ping_;
        all_ping_max_ = std::max(all_ping_max_, gap);
        close_initial_pending();
        if (stage_ == 0 && saw_pending_ && (early_mesh_.isEmpty() || !initial_pending_closed_)) {
          pending_ping_max_ = std::max(pending_ping_max_, gap);
          ++pending_pings_;
        }
        last_ping_ = now;
        last_ping_monotonic_ms_ = monotonic_ms();
        ++pings_;
      }
      catch (const std::exception &e) {
        finish(QString::fromUtf8(e.what()));
      }
      catch (...) {
        finish("Unknown warmup ping failure");
      }
    });
    QObject::connect(&poll_, &QTimer::timeout, this, [this] { advance(); });
    QObject::connect(
        QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { retire(); });
    ping_.start();
    poll_.start();
  }
  void retire() noexcept
  {
    if (closed_)
      return;
    closed_ = true;
    ping_.stop();
    poll_.stop();
    if (preview_ || rgba_ || mask_ || derived_numeric_)
      try {
        hooks_.host.with_gpu([&] {
          derived_numeric_.reset();
          preview_.reset();
          rgba_.reset();
          mask_.reset();
          GpuLayerPreview::collect_retired(true);
        });
      }
      catch (...) {
        qFatal("Warmup diagnostic GPU retirement failed");
      }
  }
  ~GpuProgramWarmupDiagnostic()
  {
    retire();
  }
};
inline std::function<void()> start_gpu_program_warmup_diagnostic(
    QObject *parent, const QString &path, GpuProgramWarmupDiagnosticHooks hooks)
{
  auto *test = new GpuProgramWarmupDiagnostic(parent, path, std::move(hooks));
  return [test = QPointer<GpuProgramWarmupDiagnostic>(test)]() noexcept {
    if (test)
      test->retire();
  };
}
}  // namespace bts
