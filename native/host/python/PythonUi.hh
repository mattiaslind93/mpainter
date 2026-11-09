// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>
class QMainWindow;
namespace bts {
class PythonUi {
 public:
  using Notify = std::function<void(const QString &, const QJsonObject &)>;
  PythonUi(QMainWindow *, Notify);
  ~PythonUi();
  QJsonValue call(const QString &, const QJsonObject &);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
