#pragma once
#include <QWidget>
#include <QJsonObject>
#include <functional>
#include <map>
class QCheckBox;
class QDoubleSpinBox;
class QSlider;
class QComboBox;
class QVBoxLayout;
namespace bts {
class DisplayPostPanel final : public QWidget {
 public:
  explicit DisplayPostPanel(QWidget *);
  void synchronize(const QJsonObject &);
  std::function<void(const QString &, double)> changed;

 private:
  QCheckBox *enabled_;
  QWidget *groups_;
  QComboBox *tone_, *glare_;
  std::map<QString, QCheckBox *> toggles_;
  std::map<QString, QDoubleSpinBox *> values_;
  std::map<QString, QSlider *> sliders_;
  QVBoxLayout *group(QVBoxLayout *, const char *, const char *, const char *, const char *);
  void publish(const QString &, double);
};
}  // namespace bts
