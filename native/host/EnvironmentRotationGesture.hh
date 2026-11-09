// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMouseEvent>
#include <cmath>
#include <optional>

namespace bts {
class EnvironmentRotationGesture {
 public:
  bool active() const
  {
    return drag_.has_value();
  }
  void finish()
  {
    drag_.reset();
  }

  bool begin(const QMouseEvent &event, bool scene, double rotation)
  {
    if (active() || !scene || event.button() != Qt::RightButton ||
        event.buttons() != Qt::RightButton || event.modifiers() != Qt::ShiftModifier ||
        !std::isfinite(rotation) || !std::isfinite(event.position().x()))
      return false;
    drag_ = Drag{event.position().x(), rotation};
    return true;
  }

  std::optional<double> move(const QMouseEvent &event)
  {
    if (!event.buttons().testFlag(Qt::RightButton)) {
      finish();
      return {};
    }
    return angle(event.position().x());
  }

  std::optional<double> release(const QMouseEvent &event)
  {
    if (event.button() != Qt::RightButton)
      return {};
    const auto result = angle(event.position().x());
    finish();
    return result;
  }

 private:
  struct Drag {
    double x, rotation;
  };
  std::optional<Drag> drag_;

  std::optional<double> angle(double x) const
  {
    if (!drag_ || !std::isfinite(x))
      return {};
    return std::remainder(drag_->rotation + (x - drag_->x) * .5, 360.);
  }
};
}  // namespace bts
