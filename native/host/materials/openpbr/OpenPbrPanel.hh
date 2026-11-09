#pragma once
#include "OpenPbrParameters.hh"
#include <QWidget>
#include <functional>
#include <map>
class QLabel;
namespace bts {
class OpenPbrPanel final : public QWidget {
 public:
  explicit OpenPbrPanel(QWidget *parent = nullptr);
  void synchronize(const OpenPbrParameters &, bool editable);
  std::function<void(const OpenPbrParameters &, bool)> changed;
  std::function<void()> finished;
  std::function<void(const QString &)> error;

 private:
  QLabel *error_;
  OpenPbrParameters state_;
  bool syncing_ = false;
  std::map<std::string, QWidget *> controls_;
  void publish(std::string_view, OpenPbrParameters::Value, bool);
};
}  // namespace bts
