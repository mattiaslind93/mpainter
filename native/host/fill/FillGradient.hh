#include "Theme.hh"
#include "SliderHitArea.hh"
#pragma once
#include <QWidget>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPainter>
#include <QFontMetrics>
#include <QLocale>
#include <algorithm>
#include <cmath>
namespace bts {
class FillGradient final : public QWidget {
  QSlider *slider_;
  QDoubleSpinBox *value_;
  QString text() const
  {
    QString printed = QLocale::c().toString(value_->value(), 'f', value_->decimals());
    return printed;
  }
  // Where the ramp starts inside the widget. Small, so the handle is not clipped.
  static constexpr int bar_top = 2;
  void place(qreal x)
  {
    const int radius = 7;
    const double span = std::max(1, width() - 2 * radius);
    const double t = std::clamp((x - radius) / span, 0.0, 1.0);
    slider_->setValue(slider_->minimum() +
                      int(std::lround(t * (slider_->maximum() - slider_->minimum()))));
  }

 public:
  FillGradient(QSlider *slider, QDoubleSpinBox *value, QWidget *parent)
      : QWidget(parent), slider_(slider), value_(value)
  {
    new SliderHandleGeometry(this, [this] {
      if (!slider_->isEnabled())
        return QList<QRectF>{};
      const double t = double(slider_->value() - slider_->minimum()) /
                       std::max(1, slider_->maximum() - slider_->minimum());
      const QPointF center(7 + t * std::max(1, width() - 14), bar_top + 7);
      return QList<QRectF>{QRectF(center - QPointF(7.75, 7.75), QSizeF(15.5, 15.5))};
    });
    setProperty("fillOnly", true);
    // The value used to be printed above the ramp, which cost a line of height
    // and repeated the number already in the field beside the caption.
    // docs/design/ui.md §7.4. It stays in the accessible name.
    setMinimumHeight(bar_top + 16);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    hide();
    connect(slider_, &QSlider::valueChanged, this, [this] {
      setAccessibleName(text());
      update();
    });
    connect(value_, &QDoubleSpinBox::valueChanged, this, [this] {
      setAccessibleName(text());
      update();
    });
    setAccessibleName(text());
  }

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!slider_->isEnabled())
      painter.setOpacity(.45);
    setAccessibleName(text());
    const int radius = 7;
    const int bar_h = 8;
    const QRect bar(
        radius, bar_top + radius - bar_h / 2, std::max(1, width() - 2 * radius), bar_h);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    gradient.setColorAt(0, Qt::black);
    gradient.setColorAt(1, Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(bar);
    const double span = std::max(1, slider_->maximum() - slider_->minimum());
    const double t = std::clamp((slider_->value() - slider_->minimum()) / span, 0.0, 1.0);
    const QPointF center(radius + t * std::max(1, width() - 2 * radius), bar.center().y());
    painter.setPen(QPen(QColor(bts::theme::Surface), 1.5));
    painter.setBrush(QColor(bts::theme::Heading));
    painter.drawEllipse(center, radius, radius);
    painter.setBrush(QColor(bts::theme::Border));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, 3, 3);
  }
  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() != Qt::LeftButton || !slider_->isEnabled())
      return;
    slider_->setSliderDown(true);
    place(event->position().x());
  }
  void mouseMoveEvent(QMouseEvent *event) override
  {
    if (!slider_->isSliderDown())
      return;
    place(event->position().x());
  }
  void mouseReleaseEvent(QMouseEvent *event) override
  {
    if (slider_->isSliderDown())
      slider_->setSliderDown(false);
    QWidget::mouseReleaseEvent(event);
  }
};

}  // namespace bts
