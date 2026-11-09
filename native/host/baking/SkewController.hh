// SPDX-License-Identifier: GPL-3.0-or-later
#include "BakeSource.hh"
#pragma once
#include "SkewBakeEngine.hh"
#include "AutoBakeEngine.hh"
#include "BrushSettings.hh"
#include <QObject>
#include <QTimer>
#include <future>
#include <functional>
class QMainWindow;
class QDialog;
namespace bts {
class SkewPanel;
struct SkewHooks {
  std::function<TextureDocument *()> document;
  std::function<const SceneSnapshot &()> scene;
  std::function<SceneSnapshot()> bake_scene;
  std::function<std::uint32_t()> material;
  BakeSourceLoader load_source;
  std::function<void(std::unique_ptr<TextureDocument>, const QString &, std::uint32_t)> history;
  std::function<void(std::uint32_t, int, MeshMap)> install;
  std::function<void(int)> preview;
  std::function<int()> preview_role;
  std::function<void(bool,
                     std::optional<SkewField>,
                     const std::vector<SkewSurfaceVector> &,
                     std::shared_ptr<const SkewGeometry>)>
      view;
  std::function<void()> end_input, refresh, configure;
  std::function<void(const std::function<void()> &)> with_gpu;
  std::function<void(const QString &)> log;
};
class SkewController final : public QObject {
 public:
  SkewController(QMainWindow *, SkewHooks);
  ~SkewController();
  void finish_edit()
  {
    hooks_.end_input();
    commit_settings();
  }
  void reset_document();
  void sync();
  void sync_panel();
  void mode(int);
  int mode() const;
  bool painting() const;
  bool stroke() const
  {
    return bool(before_);
  }
  bool begin(const BrushSettings &);
  void stamps(const std::vector<PaintPixels::Stamp> &, std::size_t stable);
  void end(bool cancel);
  void polygon(const PaintPixels::Stamp &);
  bool cancel_transient();
  void invert();
  void request(bool full = true);
  bool flush();
  bool pending() const;
  QJsonObject state() const;
  QJsonObject reference();

 private:
  std::mutex gpu_mutex_;
  std::packaged_task<void()> gpu_task_;
  void process_gpu();
  void request_gpu(const std::function<void()> &, const std::shared_ptr<std::atomic<bool>> &);
  SkewHooks hooks_;
  SkewPanel *panel_;
  QDialog *dock_;
  QTimer idle_, poll_, debounce_;
  std::unique_ptr<TextureDocument> before_, settings_before_;
  TilePaintPixels ink_, prefix_;
  PaintPixels::Bounds dirty_{}, tail_{};
  std::uint32_t stroke_uid_ = 0;
  float opacity_ = 1, value_ = 1;
  bool erase_ = false, settings_bake_ = false;
  std::vector<SkewSurfaceVector> vectors_;
  QString vector_key_, seen_token_, source_key_;
  int prior_mode_ = 0;
  std::shared_ptr<AutoBakeEngine> general_;
  std::shared_ptr<const SceneSnapshot> cage_;
  std::shared_ptr<SkewBakeEngine> engine_;
  std::shared_ptr<const SceneSnapshot> low_, high_;
  std::shared_ptr<const SkewGeometry> geometry_;
  struct Request {
    bool full = false, manual = false;
  };
  std::map<std::uint32_t, std::map<int, PaintPixels::Bounds>> regions_;
  using Key = std::pair<std::uint32_t, int>;
  std::map<Key, Request> requests_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  struct Result {
    std::shared_ptr<SkewBakeEngine> engine;
    std::shared_ptr<AutoBakeEngine> general;
    MeshMap map;
    QString source, token;
    QUuid document;
    std::uint32_t material = 0;
    int role = 0;
    bool manual = false;
  };
  std::future<Result> job_;
  QString error_, failed_token_;
  std::array<int, 7> role_completed_{};
  int completed_ = 0, dropped_ = 0;
  QString token(std::uint32_t uid = 0, int role = -1) const;
  QString source_key() const;
  bool eligible() const;
  bool bake_eligible() const;
  int auto_role() const;
  QJsonObject options(std::uint32_t, int) const;
  std::set<int> tiles(std::uint32_t) const;
  void request_map(int, bool, bool, std::uint32_t uid = 0);
  void choose_auto(int);
  void quality(int, const QJsonObject &);
  void synchronize_quality(int);
  void settings(const SkewSettings &);
  void commit_settings();
  void start_job();
  void poll();
  void bounds(PaintPixels::Bounds);
};
}  // namespace bts
