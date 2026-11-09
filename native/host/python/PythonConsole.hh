// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QObject>
#include <memory>
#include <functional>
class QMainWindow;
namespace bts {
struct PythonHooks {
  std::function<QJsonValue(const QString &, const QJsonObject &)> call;
  std::function<bool()> busy;
  QString plugin_directory;
  std::function<QJsonObject()> snapshot;
};
class PythonConsole final : public QObject {
 public:
  PythonConsole(QMainWindow *, PythonHooks);
  ~PythonConsole();
  bool execute(const QString &);
  void notify(const QString &, const QJsonObject &data = {});
  void output(const QString &);
  QString plugin_directory() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
