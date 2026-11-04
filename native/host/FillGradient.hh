#include "Theme.hh"
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
  QString text() const {
    QString printed = QLocale::c().toString(value_->value(), 'f', value_->decimals());
    return printed;
  }
  void place(qreal x) {
    const int radius = 7;
    const double span = std::max(1, width() - 2 * radius);
    const double t = std::clamp((x - radius) / span, 0.0, 1.0);
    slider_->setValue(slider_->minimum() + int(std::lround(t * (slider_->maximum() - slider_->minimum()))));
  }
 public:
  FillGradient(QSlider *slider, QDoubleSpinBox *value, QWidget *parent) : QWidget(parent), slider_(slider), value_(value) {
    setProperty("fillOnly", true);
    setMinimumHeight(36);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    hide();
    connect(slider_, &QSlider::valueChanged, this, [this] { setAccessibleName(text()); update(); });
    connect(value_, &QDoubleSpinBox::valueChanged, this, [this] { setAccessibleName(text()); update(); });
    setAccessibleName(text());
  }
 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!slider_->isEnabled()) painter.setOpacity(.45);
    const QString printed = text();
    setAccessibleName(printed);
    QFont font = painter.font();
    font.setPixelSize(11);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int radius = 7;
    const int bar_h = 8;
    const int bar_y = metrics.height() + 4;
    const QRect bar(radius, bar_y + radius - bar_h / 2, std::max(1, width() - 2 * radius), bar_h);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    gradient.setColorAt(0, Qt::black);
    gradient.setColorAt(1, Qt::white);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(bar);
    const double span = std::max(1, slider_->maximum() - slider_->minimum());
    const double t = std::clamp((slider_->value() - slider_->minimum()) / span, 0.0, 1.0);
    const QPointF center(radius + t * std::max(1, width() - 2 * radius), bar.center().y());
    painter.setPen(QColor(bts::theme::Heading));
    const int text_w = metrics.horizontalAdvance(printed);
    painter.drawText(QPointF(std::clamp(center.x() - text_w / 2.0, 0.0, double(std::max(0, width() - text_w))), metrics.ascent()), printed);
    painter.setPen(QPen(QColor(bts::theme::Surface), 1.5));
    painter.setBrush(QColor(bts::theme::Heading));
    painter.drawEllipse(center, radius, radius);
    painter.setBrush(QColor(bts::theme::Border));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(center, 3, 3);
  }
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() != Qt::LeftButton || !slider_->isEnabled()) return;
    slider_->setSliderDown(true);
    place(event->position().x());
  }
  void mouseMoveEvent(QMouseEvent *event) override {
    if (!slider_->isSliderDown()) return;
    place(event->position().x());
  }
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (slider_->isSliderDown()) slider_->setSliderDown(false);
    QWidget::mouseReleaseEvent(event);
  }
};

}
