#include "Theme.hh"
#pragma once
#include "LayerBlend.hh"
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <QString>
#include <array>
namespace bts {
inline QString compact_layer_blend(int value)
{
  if (value == int(LayerBlend::Multiply))
    return "Mult";
  if (value == int(LayerBlend::LinearDodge))
    return "Add";
  if (value == int(LayerBlend::Passthrough))
    return "PThr";
  return QString::fromLatin1(
      layer_blend_labels.at(std::clamp(value, 0, int(layer_blend_labels.size()) - 1)));
}
inline QString compact_layer_opacity(double percent)
{
  return QString::number(std::clamp(qRound(percent), 0, 100));
}
enum class PainterGlyph {
  Eye,
  Hidden,
  Generator,
  Filter,
  Fill,
  Levels,
  Paint,
  Mask,
  Folder,
  Wand,
  Delete,
  Group,
  Anchor,
  Geometry,
  Count
};
inline void paint_workflow_glyph(QPainter &painter,
                                 const QRectF &rect,
                                 PainterGlyph glyph,
                                 const QColor &color = QColor(bts::theme::Heading))
{
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.translate(rect.topLeft());
  painter.scale(rect.width() / 16., rect.height() / 16.);
  painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  switch (glyph) {
    case PainterGlyph::Eye:
    case PainterGlyph::Hidden: {
      QPainterPath eye;
      eye.moveTo(1, 8);
      eye.cubicTo(4, 2, 12, 2, 15, 8);
      eye.cubicTo(12, 14, 4, 14, 1, 8);
      painter.drawPath(eye);
      painter.drawEllipse(QPointF(8, 8), 2, 2);
      if (glyph == PainterGlyph::Hidden)
        painter.drawLine(QPointF(2, 14), QPointF(14, 2));
      break;
    }
    case PainterGlyph::Generator:
      painter.drawRoundedRect(QRectF(2, 2, 12, 12), 1, 1);
      painter.drawEllipse(QPointF(8, 8), 3, 3);
      painter.drawLine(QPointF(8, 1), QPointF(8, 4));
      painter.drawLine(QPointF(12, 8), QPointF(15, 8));
      break;
    case PainterGlyph::Filter:
      painter.drawPolygon(QPolygonF{{4, 2}, {12, 2}, {15, 8}, {12, 14}, {4, 14}, {1, 8}});
      painter.drawPolyline(QPolygonF{{11, 5}, {6, 5}, {5, 8}, {10, 8}, {9, 11}, {4, 11}});
      break;
    case PainterGlyph::Fill:
      painter.drawPolygon(QPolygonF{{3, 7}, {8, 2}, {13, 7}, {8, 12}});
      painter.drawLine(QPointF(2, 7), QPointF(14, 7));
      painter.drawLine(QPointF(3, 3), QPointF(6, 6));
      painter.setBrush(color);
      painter.drawEllipse(QPointF(13, 12), 1.3, 2);
      break;
    case PainterGlyph::Levels:
      painter.drawLine(QPointF(2, 14), QPointF(14, 14));
      painter.drawLine(QPointF(3, 12), QPointF(3, 9));
      painter.drawLine(QPointF(6, 12), QPointF(6, 4));
      painter.drawLine(QPointF(9, 12), QPointF(9, 2));
      painter.drawLine(QPointF(12, 12), QPointF(12, 7));
      break;
    case PainterGlyph::Paint:
      painter.drawPolygon(QPolygonF{{5, 10}, {11, 2}, {14, 5}, {8, 12}});
      painter.drawArc(QRectF(1, 9, 6, 6), 180 * 16, 180 * 16);
      break;
    case PainterGlyph::Mask:
      painter.drawRect(QRectF(2, 2, 12, 12));
      painter.setBrush(color);
      painter.drawEllipse(QPointF(8, 8), 3, 3);
      break;
    case PainterGlyph::Folder:
    case PainterGlyph::Group:
      painter.drawPolyline(
          QPolygonF{{1, 5}, {1, 3}, {6, 3}, {8, 5}, {15, 5}, {15, 13}, {1, 13}, {1, 5}, {15, 5}});
      if (glyph == PainterGlyph::Group)
        painter.drawLine(QPointF(4, 9), QPointF(12, 9));
      break;
    case PainterGlyph::Wand:
      painter.drawLine(QPointF(3, 14), QPointF(12, 5));
      painter.drawLine(QPointF(11, 1), QPointF(11, 3));
      painter.drawLine(QPointF(14, 4), QPointF(16, 4));
      painter.drawLine(QPointF(5, 2), QPointF(6, 4));
      painter.drawLine(QPointF(13, 9), QPointF(15, 10));
      break;
    case PainterGlyph::Delete:
      painter.drawRect(QRectF(4, 4, 8, 10));
      painter.drawLine(QPointF(2, 4), QPointF(14, 4));
      painter.drawPolyline(QPolygonF{{6, 3}, {6, 1}, {10, 1}, {10, 3}});
      painter.drawLine(QPointF(7, 7), QPointF(7, 12));
      painter.drawLine(QPointF(10, 7), QPointF(10, 12));
      break;
    case PainterGlyph::Anchor:
      painter.drawEllipse(QPointF(8, 3), 1.5, 1.5);
      painter.drawLine(QPointF(8, 5), QPointF(8, 14));
      painter.drawArc(QRectF(2, 3, 12, 12), 180 * 16, 180 * 16);
      painter.drawLine(QPointF(4, 8), QPointF(12, 8));
      break;
    case PainterGlyph::Geometry:
      painter.drawPolygon(QPolygonF{{8, 1}, {14, 5}, {14, 12}, {8, 15}, {2, 12}, {2, 5}});
      painter.drawPolyline(QPolygonF{{2, 5}, {8, 8}, {14, 5}});
      painter.drawLine(QPointF(8, 8), QPointF(8, 15));
      break;
    case PainterGlyph::Count:
      break;
  }
  painter.restore();
}
inline QIcon painter_workflow_icon(PainterGlyph glyph)
{
  static const auto icons = [] {
    std::array<QIcon, int(PainterGlyph::Count)> result;
    for (int i = 0; i < int(result.size()); ++i)
      for (int size : {16, 24, 32, 48}) {
        QPixmap pixmap(size * 2, size * 2);
        pixmap.setDevicePixelRatio(2);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint_workflow_glyph(painter, QRectF(0, 0, size, size), PainterGlyph(i));
        painter.end();
        result[i].addPixmap(pixmap);
      }
    return result;
  }();
  return icons.at(int(glyph));
}
}  // namespace bts
