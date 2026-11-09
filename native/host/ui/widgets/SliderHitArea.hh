#pragma once
#include <QWidget>
#include <QSlider>
#include <QStyleOptionSlider>
#include <functional>

namespace bts {
// Geometry is in logical pixels. Painting and layout keep their original sizes.
inline QRectF doubled_handle(const QRectF &visual)
{
  return QRectF(visual.center() - QPointF(visual.width(), visual.height()),
                QSizeF(visual.width() * 2, visual.height() * 2));
}
class SliderHandleGeometry final : public QObject {
 public:
  std::function<QList<QRectF>()> rectangles;
  SliderHandleGeometry(QWidget *owner, std::function<QList<QRectF>()> geometry)
      : QObject(owner), rectangles(std::move(geometry))
  {
    setObjectName("sliderHandleGeometry");
  }
};
inline QList<QRectF> slider_handles(QWidget *widget)
{
  if (auto *slider = qobject_cast<QSlider *>(widget)) {
    QStyleOptionSlider option;
    option.initFrom(slider);
    option.orientation = slider->orientation();
    option.minimum = slider->minimum();
    option.maximum = slider->maximum();
    option.sliderPosition = slider->sliderPosition();
    option.sliderValue = slider->value();
    option.singleStep = slider->singleStep();
    option.pageStep = slider->pageStep();
    option.tickPosition = slider->tickPosition();
    option.tickInterval = slider->tickInterval();
    option.upsideDown = option.orientation == Qt::Horizontal ?
                            slider->invertedAppearance() !=
                                (slider->layoutDirection() == Qt::RightToLeft) :
                            !slider->invertedAppearance();
    if (option.orientation == Qt::Horizontal)
      option.state |= QStyle::State_Horizontal;
    return {slider->style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider)};
  }
  if (auto *geometry = widget->findChild<QObject *>("sliderHandleGeometry",
                                                    Qt::FindDirectChildrenOnly))
    return static_cast<SliderHandleGeometry *>(geometry)->rectangles();
  return {};
}
}  // namespace bts
