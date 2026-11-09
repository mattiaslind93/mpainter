#pragma once
#include "NativeMaterialParameters.hh"
#include <QWidget>
#include <QTimer>
#include <functional>
#include <map>
class QVBoxLayout;
namespace bts {
// Artist-facing top-group inputs only. The source graph stays hidden.
class NativeMaterialPanel final : public QWidget {
 public:
  explicit NativeMaterialPanel(QWidget *parent = nullptr);
  void synchronize(const NativeMaterialDescription &);
  void set_resource_inputs_enabled(bool);
  std::function<std::array<float, 4>(const std::array<float, 4> &)> to_canonical, from_canonical;
  const NativeMaterialDescription &state() const
  {
    return state_;
  }
  std::function<void()> edit_started, edit_finished;
  std::function<void(const NativeMaterialDescription &)> changed;
  std::function<void(const QString &)> texture_requested;
  std::function<void(NativeHeightPolicy)> height_requested;
  std::function<void()> height_cancelled;
  void set_height_busy(bool);

 private:
  std::array<float, 4> picker_color(const std::array<float, 4> &) const;
  std::array<float, 4> material_color(const std::array<float, 4> &) const;
  void begin();
  void publish();
  void finish();
  void schedule();
  void rebuild();
  void rebuild_topology();
  NativeMaterialDescription state_;
  QVBoxLayout *layout_;
  std::map<QString, int> ramp_selection_;
  QTimer preview_, idle_;
  bool gesture_ = false, updating_ = false, dragging_ = false, resources_enabled_ = true,
       height_busy_ = false;
};
}  // namespace bts
