#include "OverlayScroll.hh"
#include <algorithm>
#include <QAbstractScrollArea>
#include <QEvent>
#include <QPoint>
#include <QScrollBar>
namespace bts {
namespace {
class OverlayScroll final : public QObject {
  QAbstractScrollArea *area_;
  QScrollBar *bar_;
  void place()
  {
    auto *viewport = area_->viewport();
    const int width = std::max(8, bar_->sizeHint().width());
    const auto corner = viewport->mapTo(area_, QPoint(viewport->width(), 0));
    bar_->setGeometry(corner.x() - width, corner.y(), width, viewport->height());
    bar_->raise();
  }
  void follow()
  {
    auto *source = area_->verticalScrollBar();
    bar_->setRange(source->minimum(), source->maximum());
    bar_->setPageStep(source->pageStep());
    bar_->setSingleStep(source->singleStep());
    bar_->setValue(source->value());
    // Nothing to scroll means nothing to draw: an always-present handle over
    // the content would read as a border down the side of the panel.
    bar_->setVisible(source->maximum() > source->minimum());
    place();
  }
  bool eventFilter(QObject *object, QEvent *event) override
  {
    if (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
        event->type() == QEvent::LayoutRequest)
      follow();
    return QObject::eventFilter(object, event);
  }

 public:
  explicit OverlayScroll(QAbstractScrollArea *area)
      : QObject(area), area_(area), bar_(new QScrollBar(Qt::Vertical, area))
  {
    bar_->setObjectName("overlayScrollBar");
    // The real bar keeps its range and still answers the wheel; it just never
    // claims any of the viewport.
    area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area_->installEventFilter(this);
    area_->viewport()->installEventFilter(this);
    connect(bar_, &QScrollBar::valueChanged, area_->verticalScrollBar(), &QScrollBar::setValue);
    connect(area_->verticalScrollBar(), &QScrollBar::valueChanged, bar_, &QScrollBar::setValue);
    connect(area_->verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
      follow();
    });
    follow();
  }
};
}  // namespace
void overlay_scrollbar(QAbstractScrollArea *area)
{
  if (!area || area->findChild<QScrollBar *>("overlayScrollBar"))
    return;
  new OverlayScroll(area);
}
}  // namespace bts
