// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QApplication>
#include <QProgressBar>
#include <QTimer>
#include <functional>

namespace bts {
// One visible operation owns the busy cursor. Native/GPU work stays with its
// existing owner; callers advance only at real preparation/publication boundaries.
class AssetApplyProgress final : public QProgressBar {
 public:
  explicit AssetApplyProgress(QWidget *parent) : QProgressBar(parent)
  {
    setObjectName("assetApplyProgress");
    setMinimumWidth(280);
    setMaximumWidth(520);
    setTextVisible(true);
    setFixedHeight(16);
    hide();
    poll_.setInterval(16);
    connect(&poll_, &QTimer::timeout, this, [this] {
      if (!active_ || !dispatched_)
        return;
      if (pending && pending())
        return;
      finish();
    });
  }
  ~AssetApplyProgress() override
  {
    release_cursor();
  }
  std::function<bool()> pending;
  std::function<void(const QString &, bool)> completed;
  bool active() const
  {
    return active_;
  }
  bool begin(const QString &name)
  {
    if (active_)
      return false;
    ++generation_;
    active_ = true;
    dispatched_ = false;
    name_ = name;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    cursor_ = true;
    stage("Load source", 0);
    show();
    poll_.start();
    return true;
  }
  void stage(const QString &text, int step = -1, int total = 4)
  {
    if (!active_)
      return;
    setRange(0, step < 0 ? 0 : total);
    if (step >= 0)
      setValue(step);
    setFormat(name_ + " — " + text);
    setToolTip(format());
    setAccessibleName("Material application progress");
    setAccessibleDescription(format());
  }
  void dispatched()
  {
    dispatched_ = true;
  }
  void fail(const QString &error)
  {
    if (active_)
      finish(error);
  }

 private:
  QTimer poll_;
  QString name_;
  bool active_ = false, dispatched_ = false, cursor_ = false;
  int generation_ = 0;
  void release_cursor()
  {
    if (cursor_) {
      QApplication::restoreOverrideCursor();
      cursor_ = false;
    }
  }
  void finish(const QString &error = {})
  {
    poll_.stop();
    active_ = false;
    release_cursor();
    setRange(0, 4);
    setValue(error.isEmpty() ? 4 : 0);
    setFormat(error.isEmpty() ? name_ + " — Applied" : "Material failed: " + error);
    setToolTip(format());
    if (completed)
      completed(error, error.isEmpty());
    const int generation = generation_;
    QTimer::singleShot(error.isEmpty() ? 1500 : 8000, this, [this, generation] {
      if (generation_ == generation && !active_)
        hide();
    });
  }
};
}  // namespace bts
