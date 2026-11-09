#pragma once
#include <QString>
#include <QUuid>
#include <vector>
class QWidget;
namespace bts {
struct RecoveryEntry {
  QString path, source, time;
  QUuid project;
};
class RecoveryStore {
 public:
  explicit RecoveryStore(QString root);
  const QString &directory() const
  {
    return root_;
  }
  QString next_path(const QUuid &) const;
  void record(const QUuid &, const QString &snapshot, const QString &source) const;
  std::vector<RecoveryEntry> entries() const;

 private:
  QString root_;
};
QString choose_recovery(QWidget *, const RecoveryStore &);
}  // namespace bts
