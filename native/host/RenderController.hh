#pragma once
#include "ViewLayout.hh"
#include <QObject>
#include <QJsonObject>
#include <functional>
#include <memory>
namespace bts {
class WorkspaceShell;
struct RenderHooks {
  std::function<bool()> document;
  std::function<void()> end_input;
  std::function<void(bool, bool)> backend;
  std::function<QJsonObject()> state;
  std::function<void(int, bool)> quality;
  std::function<ViewLayoutState()> layout;
  std::function<void(ViewLayoutState)> set_layout;
  std::function<void(bool)> rendering;
  std::function<void()> repaint, dirty;
  std::function<void(const QString &)> log;
};
class RenderController final : public QObject {
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool eventFilter(QObject *, QEvent *) override;

 public:
  RenderController(WorkspaceShell *, RenderHooks);
  ~RenderController() override;
  bool active() const;
  void set_active(bool);
  void reset_document();
};
}  // namespace bts
