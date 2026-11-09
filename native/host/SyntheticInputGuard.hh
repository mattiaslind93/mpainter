#pragma once
#include <QInputEvent>
#include <QApplication>
#include <QWidget>
#include <unordered_set>
namespace bts {
// Deterministic GUI demos can run while Mattias uses another input device.
// Only events explicitly supplied by the demo enter its viewport; the separate
// XI2/uinput verifier disables this guard and tests actual platform delivery.
class SyntheticInputGuard final : public QObject {
  QWidget *viewport_;
  std::unordered_set<const QInputEvent *> owned_;
  bool enabled_ = false;
  bool eventFilter(QObject *, QEvent *event) override
  {
    const auto type = event->type();
    if (enabled_ && type == QEvent::FocusOut) {
      ++ignored;
      return true;
    }
    if (!enabled_ || !(type == QEvent::MouseMove || type == QEvent::MouseButtonPress ||
                       type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick ||
                       type == QEvent::TabletMove || type == QEvent::TabletPress ||
                       type == QEvent::TabletRelease || type == QEvent::Wheel))
      return false;
    auto *input = static_cast<QInputEvent *>(event);
    if (owned_.erase(input))
      return false;
    ++ignored;
    event->accept();
    return true;
  }

 public:
  std::size_t ignored = 0;
  explicit SyntheticInputGuard(QWidget *viewport) : viewport_(viewport)
  {
    viewport->installEventFilter(this);
  }
  void enabled(bool value)
  {
    enabled_ = value;
    if (value)
      QObject::disconnect(qApp, &QGuiApplication::applicationStateChanged, viewport_, nullptr);
  }
  void own(const QInputEvent *event)
  {
    if (enabled_)
      owned_.insert(event);
  }
};
}  // namespace bts
