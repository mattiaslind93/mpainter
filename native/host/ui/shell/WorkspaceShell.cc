// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include <QDateTime>
#include "PainterLayerDelegate.hh"
#include "NameListShortcuts.hh"
#include "ShelfCardDelegate.hh"
#include "FolderPropertiesPanel.hh"
#include "LayerEffectRowState.hh"
#include "AdjustmentPaintPlan.hh"
#include "MaskEffectContext.hh"
#include "ImportedEffectPanel.hh"
#include "SpsmImportUi.hh"
#include "TextureExtentChoices.hh"
#include "NumericLocale.hh"
#include "MaskFillUiTest.hh"
#include "FillGradient.hh"
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include "InlineNameDelegate.hh"
#include "WarpProjection.hh"
#include <QBuffer>
#include "SmartMaterialPreset.hh"
#include "MaterialLayeringAuthoring.hh"
#include "Preferences.hh"
#include "OverlayScroll.hh"
#include "PanelScrolling.hh"
#include "DisplacementPanel.hh"
#include <QTimer>
#include <QGridLayout>
#include "EnvironmentPanel.hh"
#include "ColorPicker.hh"
#include "LayerThumbnail.hh"
#include "LayerThumbnailJobs.hh"
#include "LayerStackList.hh"
#include "BugReportDialog.hh"
#include "ImageDropFilter.hh"
#include "PropertySection.hh"
#include <QProgressBar>
#include <QTreeWidget>
#include <QColorDialog>
#include <QDialog>
#include <QCloseEvent>
#include "WorkspaceShell.hh"
#include "SpsmAssetDispatch.hh"
#include "Performance.hh"
#include "BrushPreview.hh"
#include "StencilPanel.hh"
#include "ProjectionPanel.hh"
#include "FillPanel.hh"
#include "WarpPanel.hh"
#include "LevelsPanel.hh"
#include "ColorSelectionPanel.hh"
#include "MaskFillPanel.hh"
#include "MaskGeneratorPanel.hh"
#include "CompareMaskPanel.hh"
#include "FilterPanel.hh"
#include "AnchorPanel.hh"
#include "GeometryMaskPanel.hh"
#include "LayerInstancePanel.hh"
#include "AssetQuery.hh"
#include "AssetCategories.hh"
#include "AssetThumbnail.hh"
#include "ImageSequence.hh"
#include "MeshMapsPanel.hh"
#include "SmartMaterial.hh"
#include <QMap>
#include <QTimer>
#include "AlphaPanel.hh"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QStyledItemDelegate>
#include <QWidgetAction>
#include <QMenuBar>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace bts {
namespace {
class TextureSetNameDelegate final : public InlineNameDelegate {
  QListWidget *list_;

 public:
  explicit TextureSetNameDelegate(QListWidget *list) : InlineNameDelegate(list), list_(list) {}
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
  {
    auto size = InlineNameDelegate::sizeHint(option, index);
    if (auto *row = list_->indexWidget(index)) {
      row->ensurePolished();
      size.setHeight(std::max(size.height(),
                              row->sizeHint().expandedTo(row->minimumSizeHint()).height() + 2));
    }
    return size;
  }

 protected:
  void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
  {
    InlineNameDelegate::initStyleOption(option, index);
    // Custom rows own a real checkbox; orphan rows use the native indicator.
    if (list_->indexWidget(index))
      option->features &= ~QStyleOptionViewItem::HasCheckIndicator;
  }
};
constexpr int adjustment_editable_role = Qt::UserRole + 25;
constexpr int adjustment_checked_role = Qt::UserRole + 26;
}  // namespace

namespace {

constexpr int rail_width = 38;
constexpr int header_height = 34;
constexpr int handle_width = 4;
constexpr int inspector_width = 374;
constexpr int shelf_minimum_height = 180;
constexpr int properties_minimum_height = 130;

// The published documentation. One constant so the menu, and anything else that
// links to it later, cannot drift apart.
constexpr auto documentation_url = "https://bluedino.se/mpainter/documentation";
enum class Glyph {
  Brush,
  Eraser,
  Projection,
  Smudge,
  Fill,
  Clone,
  Path,
  Picker,
  Folder,
  Mask,
  Trash,
  Settings,
  History,
  Material,
  Console,
  Shelf,
  Reset,
  Close,
  Plus,
  Undo,
  Redo,
  Filter,
  View,
  // Drawn marks for the toolbar buttons that used to carry a bare text
  // character. A glyph at icon size reads at a glance; "⋈" and "◩" did not.
  Symmetry,
  SymmetryManipulator,
  HideExcluded,
  LazyMouse,
  More,
};

QIcon icon(Glyph glyph)
{
  QIcon result;
  for (QIcon::Mode mode : {QIcon::Normal, QIcon::Disabled}) {
    QPixmap pixmap(32, 32);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(mode == QIcon::Disabled ? bts::theme::Muted : bts::theme::Text),
                  1.2,
                  Qt::SolidLine,
                  Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (glyph) {
      case Glyph::Symmetry: {
        const QColor tint = p.pen().color();
        p.drawLine(QPointF(8, 1.5), QPointF(8, 14.5));
        p.setBrush(tint);
        p.drawPolygon(QPolygonF({{6, 4}, {6, 12}, {2, 8}}));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(QPolygonF({{10, 4}, {10, 12}, {14, 8}}));
        break;
      }
      case Glyph::SymmetryManipulator:
        p.drawLine(QPointF(8, 1.5), QPointF(8, 14.5));
        p.drawLine(QPointF(2, 8), QPointF(14, 8));
        p.drawLine(QPointF(2, 8), QPointF(5, 5));
        p.drawLine(QPointF(2, 8), QPointF(5, 11));
        p.drawLine(QPointF(14, 8), QPointF(11, 5));
        p.drawLine(QPointF(14, 8), QPointF(11, 11));
        break;
      case Glyph::HideExcluded: {
        const QColor tint = p.pen().color();
        p.drawRect(QRectF(2, 2, 12, 12));
        p.fillRect(QRectF(2.7, 2.7, 5.3, 10.6), tint);
        break;
      }
      case Glyph::LazyMouse: {
        const QColor tint = p.pen().color();
        p.drawEllipse(QRectF(2.5, 2.5, 11, 11));
        p.drawLine(QPointF(5.2, 10.8), QPointF(9, 7));
        p.setBrush(tint);
        p.drawEllipse(QPointF(10.4, 5.6), 2, 2);
        p.setBrush(Qt::NoBrush);
        break;
      }
      case Glyph::More:
        p.drawLine(QPointF(4.5, 6.5), QPointF(8, 10));
        p.drawLine(QPointF(8, 10), QPointF(11.5, 6.5));
        break;
      case Glyph::Material:
        p.drawEllipse(QRectF(2, 2, 12, 12));
        p.drawArc(QRectF(2, 5, 12, 6), 0, 180 * 16);
        p.drawArc(QRectF(5, 2, 6, 12), 90 * 16, 180 * 16);
        break;
      case Glyph::Path: {
        QPainterPath curve;
        curve.moveTo(3, 13);
        curve.cubicTo(5, 1, 10, 15, 13, 3);
        p.drawPath(curve);
        p.drawEllipse(QPointF(3, 13), 1.5, 1.5);
        p.drawEllipse(QPointF(13, 3), 1.5, 1.5);
        break;
      }
      case Glyph::Smudge: {
        QPainterPath hand;
        hand.moveTo(3, 14);
        hand.lineTo(2, 9);
        hand.cubicTo(2, 7, 4, 7, 5, 9);
        hand.lineTo(5, 4);
        hand.cubicTo(5, 2, 7, 2, 7, 4);
        hand.lineTo(7, 8);
        hand.lineTo(8, 5);
        hand.cubicTo(9, 4, 10, 5, 10, 7);
        hand.lineTo(10, 8);
        hand.cubicTo(13, 7, 14, 9, 13, 12);
        hand.lineTo(11, 15);
        p.drawPath(hand);
        break;
      }
      case Glyph::Brush:
      case Glyph::Picker:
        p.drawLine(QPointF(6, 10), QPointF(12, 3));
        p.drawLine(QPointF(8, 12), QPointF(14, 5));
        p.drawLine(QPointF(12, 3), QPointF(14, 5));
        p.drawEllipse(QRectF(2, 10, 5, 4));
        break;
      case Glyph::Eraser:
        p.drawPolygon(QPolygonF({{2, 10}, {8, 3}, {14, 8}, {9, 14}, {6, 14}}));
        p.drawLine(QPointF(5, 7), QPointF(11, 12));
        break;
      case Glyph::Projection:
        p.drawRect(QRectF(2, 2, 12, 12));
        p.drawLine(QPointF(2, 14), QPointF(8, 7));
        p.drawLine(QPointF(8, 7), QPointF(14, 14));
        p.drawEllipse(QPointF(11, 5), 1, 1);
        break;
      case Glyph::Fill:
        p.drawPolygon(QPolygonF({{2, 8}, {8, 2}, {13, 7}, {7, 13}}));
        p.drawLine(QPointF(3, 8), QPointF(12, 8));
        p.drawLine(QPointF(13, 11), QPointF(14, 14));
        break;
      case Glyph::Clone:
        p.drawEllipse(QRectF(6, 2, 4, 5));
        p.drawRect(QRectF(4, 10, 8, 3));
        p.drawLine(QPointF(8, 7), QPointF(8, 10));
        p.drawLine(QPointF(2, 15), QPointF(14, 15));
        break;
      case Glyph::Folder:
        p.drawPolygon(QPolygonF({{2, 4}, {7, 4}, {9, 6}, {14, 6}, {14, 13}, {2, 13}}));
        break;
      case Glyph::Mask:
        p.drawEllipse(QRectF(2, 2, 12, 12));
        p.drawLine(QPointF(8, 2), QPointF(8, 14));
        p.drawLine(QPointF(4, 5), QPointF(8, 5));
        p.drawLine(QPointF(3, 8), QPointF(8, 8));
        p.drawLine(QPointF(4, 11), QPointF(8, 11));
        break;
      case Glyph::Trash:
        p.drawRect(QRectF(4, 5, 8, 9));
        p.drawLine(QPointF(2, 3), QPointF(14, 3));
        p.drawLine(QPointF(6, 1), QPointF(10, 1));
        p.drawLine(QPointF(7, 7), QPointF(7, 12));
        p.drawLine(QPointF(10, 7), QPointF(10, 12));
        break;
      case Glyph::Settings:
        for (int x : {4, 8, 12}) {
          p.drawLine(QPointF(x, 2), QPointF(x, 14));
        }
        p.drawEllipse(QPointF(4, 5), 1.5, 1.5);
        p.drawEllipse(QPointF(8, 11), 1.5, 1.5);
        p.drawEllipse(QPointF(12, 7), 1.5, 1.5);
        break;
      case Glyph::History:
      case Glyph::Reset:
        p.drawArc(QRectF(2, 2, 12, 12), 45 * 16, 285 * 16);
        p.drawPolyline(QPolygonF({{2, 2}, {2, 6}, {6, 6}}));
        if (glyph == Glyph::History) {
          p.drawPolyline(QPolygonF({{8, 4}, {8, 8}, {11, 10}}));
        }
        break;
      case Glyph::Console:
        p.drawRect(QRectF(1, 2, 14, 12));
        p.drawPolyline(QPolygonF({{4, 6}, {7, 8}, {4, 10}}));
        p.drawLine(QPointF(9, 11), QPointF(12, 11));
        break;
      case Glyph::Shelf:
        p.drawRect(QRectF(1, 2, 14, 12));
        p.drawLine(QPointF(1, 8), QPointF(15, 8));
        for (int x : {4, 8, 12}) {
          p.drawPoint(QPointF(x, 11));
        }
        break;
      case Glyph::Close:
        p.drawLine(QPointF(4, 4), QPointF(12, 12));
        p.drawLine(QPointF(12, 4), QPointF(4, 12));
        break;
      case Glyph::Plus:
        p.drawLine(QPointF(8, 3), QPointF(8, 13));
        p.drawLine(QPointF(3, 8), QPointF(13, 8));
        break;
      case Glyph::Redo:
        p.translate(16, 0);
        p.scale(-1, 1);
        [[fallthrough]];
      case Glyph::Undo: {
        QPainterPath path;
        path.moveTo(3, 6);
        path.cubicTo(13, 3, 15, 7, 13, 12);
        p.drawPath(path);
        p.drawPolyline(QPolygonF({{6, 2}, {2, 6}, {6, 10}}));
        break;
      }
      case Glyph::Filter:
        p.drawPolyline(QPolygonF({{2, 3}, {14, 3}, {9, 8}, {9, 13}, {7, 14}, {7, 8}, {2, 3}}));
        break;
      case Glyph::View:
        p.drawPolygon(QPolygonF({{8, 1}, {14, 5}, {14, 11}, {8, 15}, {2, 11}, {2, 5}}));
        p.drawPolyline(QPolygonF({{2, 5}, {8, 9}, {14, 5}}));
        p.drawLine(QPointF(8, 9), QPointF(8, 15));
        break;
    }
    p.end();
    result.addPixmap(pixmap, mode);
  }
  return result;
}

class TangentBumpBar final : public QWidget {
 public:
  explicit TangentBumpBar(QWidget *parent) : QWidget(parent)
  {
    setObjectName("fillTangentBump");
    setAccessibleName("Normal");
    setToolTip("Normal. Tangent-space normal map, not grayscale Bump.");
    setMinimumHeight(22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  void set_normal(const std::array<float, 3> &normal)
  {
    normal_ = normal;
    update();
  }
  std::function<void(int, double)> edited;

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    const auto channel = [](float value) {
      return std::clamp(int(std::lround((value * .5f + .5f) * 255.f)), 0, 255);
    };
    painter.fillRect(rect(),
                     QColor(channel(normal_[0]), channel(normal_[1]), channel(normal_[2])));
    painter.setPen(QColor(bts::theme::Hover));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }
  void mouseReleaseEvent(QMouseEvent *event) override
  {
    const QColor current = QColor::fromRgbF(std::clamp(normal_[0] * .5f + .5f, 0.f, 1.f),
                                            std::clamp(normal_[1] * .5f + .5f, 0.f, 1.f),
                                            std::clamp(normal_[2] * .5f + .5f, 0.f, 1.f));
    const QColor picked = QColorDialog::getColor(current, this, "Normal");
    if (!picked.isValid() || !edited) {
      QWidget::mouseReleaseEvent(event);
      return;
    }
    edited(0, picked.redF() * 2. - 1.);
    edited(1, picked.greenF() * 2. - 1.);
    edited(2, picked.blueF() * 2. - 1.);
  }

 private:
  std::array<float, 3> normal_{0, 0, 1};
};

// One label column and one field column, for every row in a group. A row that
// carries a trailing button keeps the button outside the field column, and a
// row without one reserves the same width, so no field is left shorter than the
// field above it. docs/design/ui.md §5.3.
// One label column across the whole panel, and a row that is exactly as tall as
// its contents. Rows moved into folding sections, so a single container is no
// longer enough: each section's contents is a container of its own, and the
// column is measured across all of them together.
void align_property_rows(const std::vector<QWidget *> &groups, int trailing_width)
{
  std::vector<QLabel *> labels;
  int widest = 0;
  for (auto *group : groups) {
    if (!group)
      continue;
    for (auto *line : group->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
      auto *layout = qobject_cast<QHBoxLayout *>(line->layout());
      if (!layout || layout->count() < 2)
        continue;
      auto *label = qobject_cast<QLabel *>(layout->itemAt(0)->widget());
      if (!label || label->text().isEmpty())
        continue;
      labels.push_back(label);
      widest = std::max(widest, label->sizeHint().width());
      if (layout->count() == 2 && trailing_width > 0)
        layout->addSpacing(trailing_width);
      // Without this the column is stretched to the scroll viewport and the
      // spare height is shared out among the rows, so unfolding a section
      // changed the height and the spacing of every row on the panel.
      line->setSizePolicy(line->sizePolicy().horizontalPolicy(), QSizePolicy::Maximum);
    }
  }
  for (auto *label : labels)
    label->setFixedWidth(widest);
}

QToolButton *tool(QWidget *parent, Glyph glyph, const QString &name, bool enabled = false)
{
  auto *button = new QToolButton(parent);
  button->setIcon(icon(glyph));
  // 16 px marks in a 26 px button read as specks on a Retina panel; one step up
  // keeps the row the same shape while making each symbol identifiable.
  button->setIconSize(QSize(18, 18));
  button->setFixedSize(28, 28);
  button->setAutoRaise(true);
  button->setAccessibleName(name);
  button->setToolTip(enabled ? name : name + QStringLiteral(" is unavailable."));
  button->setEnabled(enabled);
  return button;
}

QVBoxLayout *column(QWidget *widget)
{
  auto *layout = new QVBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  return layout;
}

QHBoxLayout *row(QWidget *widget, int margin = 4)
{
  auto *layout = new QHBoxLayout(widget);
  layout->setContentsMargins(margin, 0, margin, 0);
  layout->setSpacing(4);
  return layout;
}

QWidget *header(QWidget *parent, const QString &title, QToolButton **close = nullptr)
{
  auto *widget = new QWidget(parent);
  widget->setObjectName("dockHeader");
  widget->setFixedHeight(header_height);
  auto *layout = row(widget, 8);
  auto *label = new QLabel(title.toUpper(), widget);
  label->setObjectName("dockTitle");
  layout->addWidget(label);
  layout->addStretch();
  if (close) {
    *close = tool(widget, Glyph::Close, QStringLiteral("Hide ") + title, true);
    layout->addWidget(*close);
  }
  return widget;
}

QSplitter *splitter(Qt::Orientation orientation, QWidget *parent, const char *name)
{
  auto *widget = new QSplitter(orientation, parent);
  widget->setObjectName(name);
  widget->setHandleWidth(handle_width);
  widget->setChildrenCollapsible(false);
  widget->setOpaqueResize(true);
  return widget;
}

class ShelfCategoryList final : public QListWidget {
 public:
  explicit ShelfCategoryList(QWidget *parent = nullptr) : QListWidget(parent)
  {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  void update_grid()
  {
    const int avail = std::max(272, (viewport() ? viewport()->width() : width()) - 4);
    const int cols = 4;
    const int cell_w = std::max(66, avail / cols);
    const int cell_h = 24;
    if (gridSize() != QSize(cell_w, cell_h))
      setGridSize(QSize(cell_w, cell_h));
    const int rows = (std::max(1, count()) + cols - 1) / cols;
    const int target_h = rows * cell_h + 6;
    if (height() != target_h)
      setFixedHeight(target_h);
  }

 protected:
  void resizeEvent(QResizeEvent *event) override
  {
    QListWidget::resizeEvent(event);
    update_grid();
  }
  void showEvent(QShowEvent *event) override
  {
    QListWidget::showEvent(event);
    update_grid();
  }
};

class ShelfCategoryDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
  {
    if (const auto *list = qobject_cast<const QListWidget *>(option.widget)) {
      const QSize g = list->gridSize();
      if (g.isValid())
        return QSize(std::max(64, g.width() - 2), std::max(22, g.height() - 2));
    }
    return QSize(86, 22);
  }
  void paint(QPainter *painter,
             const QStyleOptionViewItem &option,
             const QModelIndex &index) const override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // The row paints itself, so a disabled category has to be dimmed here:
    // nothing else in the view would show it.
    const bool enabled = option.state & QStyle::State_Enabled;
    const bool selected = enabled && (option.state & QStyle::State_Selected);
    const bool hovered = enabled && (option.state & QStyle::State_MouseOver);
    const QRectF cell = QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5);
    const QColor bg = selected ? QColor(bts::theme::Selection) :
                                 (hovered ? QColor(bts::theme::Raised) :
                                  enabled ? QColor(bts::theme::Surface) :
                                            QColor(bts::theme::Canvas));
    const QColor border = selected ? QColor(bts::theme::Accent) :
                                     (hovered ? QColor(bts::theme::Border) :
                                                QColor(bts::theme::Separator));
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(bg);
    painter->drawRoundedRect(cell, bts::theme::RadiusPx, bts::theme::RadiusPx);

    const QString text = index.data(Qt::DisplayRole).toString();
    const QColor fg = selected ? QColor(bts::theme::Heading) :
                                 (hovered ? QColor(bts::theme::Heading) :
                                  enabled ? QColor(bts::theme::Text) :
                                            QColor(bts::theme::Disabled));
    const QColor ic = selected ? QColor(bts::theme::Heading) :
                                 (hovered ? QColor(bts::theme::Heading) :
                                  enabled ? QColor(bts::theme::Muted) :
                                            QColor(bts::theme::Disabled));

    const qreal ix = cell.left() + 6.0;
    const qreal iy = cell.center().y() - 5.0;
    const QRectF ir(ix, iy, 10.0, 10.0);
    painter->setPen(QPen(ic, 1.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    if (text == "All") {
      for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
          painter->drawRect(QRectF(ix + c * 5.5, iy + r * 5.5, 4.0, 4.0));
    }
    else if (text == "Project") {
      painter->drawPolygon(QPolygonF({{ix, iy + 2.5},
                                      {ix + 3.5, iy + 2.5},
                                      {ix + 4.8, iy + 4.0},
                                      {ix + 10.0, iy + 4.0},
                                      {ix + 10.0, iy + 9.0},
                                      {ix, iy + 9.0}}));
    }
    else if (text == "Materials" || text == "Material presets") {
      painter->drawEllipse(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      if (text == "Material presets") {
        painter->setBrush(ic);
        painter->drawEllipse(QPointF(ix + 8.2, iy + 2.0), 1.6, 1.6);
        painter->setBrush(Qt::NoBrush);
      }
      else {
        painter->drawArc(ir.adjusted(2.2, 2.2, -2.2, -2.2), 40 * 16, 110 * 16);
      }
    }
    else if (text == "Mask generators") {
      painter->drawRect(ir.adjusted(0.5, 1.0, -0.5, -1.0));
      painter->setBrush(ic);
      painter->drawEllipse(ir.center(), 2.2, 2.2);
      painter->setBrush(Qt::NoBrush);
    }
    else if (text == "Alphas" || text == "Shape") {
      painter->setBrush(ic);
      painter->drawEllipse(ir.adjusted(1.5, 1.5, -1.5, -1.5));
      painter->setBrush(Qt::NoBrush);
    }
    else if (text == "Grunges" || text == "Particles") {
      painter->setBrush(ic);
      painter->drawEllipse(QPointF(ix + 2.5, iy + 3.0), 1.1, 1.1);
      painter->drawEllipse(QPointF(ix + 7.2, iy + 2.5), 0.9, 0.9);
      painter->drawEllipse(QPointF(ix + 5.0, iy + 6.5), 1.3, 1.3);
      painter->drawEllipse(QPointF(ix + 8.0, iy + 7.5), 1.0, 1.0);
      painter->setBrush(Qt::NoBrush);
    }
    else if (text == "Procedurals") {
      QPainterPath wave;
      wave.moveTo(ix, iy + 5.0);
      wave.cubicTo(ix + 3.0, iy, ix + 7.0, iy + 10.0, ix + 10.0, iy + 5.0);
      painter->drawPath(wave);
    }
    else if (text == "Textures" || text == "Flattened layers") {
      painter->drawRect(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      painter->drawLine(QPointF(ix + 0.5, iy + 5.0), QPointF(ix + 9.5, iy + 5.0));
      painter->drawLine(QPointF(ix + 5.0, iy + 0.5), QPointF(ix + 5.0, iy + 9.5));
    }
    else if (text == "Filters" || text == "Color profiles") {
      painter->drawLine(QPointF(ix + 1.0, iy + 2.5), QPointF(ix + 9.0, iy + 2.5));
      painter->drawLine(QPointF(ix + 2.5, iy + 5.0), QPointF(ix + 7.5, iy + 5.0));
      painter->drawLine(QPointF(ix + 4.0, iy + 7.5), QPointF(ix + 6.0, iy + 7.5));
    }
    else if (text == "Brushes" || text == "Tools") {
      painter->drawLine(QPointF(ix + 2.0, iy + 8.5), QPointF(ix + 8.5, iy + 2.0));
      painter->drawEllipse(QPointF(ix + 2.2, iy + 8.0), 1.2, 1.2);
    }
    else if (text == "Environments") {
      painter->drawEllipse(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      painter->drawLine(QPointF(ix + 0.5, iy + 5.0), QPointF(ix + 9.5, iy + 5.0));
    }
    else {
      painter->drawPolygon(QPolygonF({{ix + 5.0, iy + 0.5},
                                      {ix + 9.5, iy + 3.0},
                                      {ix + 9.5, iy + 7.5},
                                      {ix + 5.0, iy + 9.5},
                                      {ix + 0.5, iy + 7.5},
                                      {ix + 0.5, iy + 3.0}}));
    }

    QFont f = option.font;
    f.setPixelSize(10);
    f.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter->setFont(f);
    painter->setPen(fg);
    const QRectF text_rect(
        ix + 13.0, cell.top(), std::max(10.0, cell.right() - (ix + 15.0)), cell.height());
    const QFontMetrics fm(f);
    painter->drawText(text_rect,
                      Qt::AlignLeft | Qt::AlignVCenter,
                      fm.elidedText(text, Qt::ElideRight, int(text_rect.width())));
    painter->restore();
  }
};

class EmptyList final : public QListWidget {
 public:
  EmptyList(const QString &empty_text, QWidget *parent)
      : QListWidget(parent), empty_text_(empty_text)
  {
    setFrameShape(QFrame::NoFrame);
  }

  void set_empty_text(const QString &text)
  {
    empty_text_ = text;
    viewport()->update();
  }

 protected:
  QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
  {
    auto *data = new QMimeData;
    if (objectName() == "shelfAssets" && !items.isEmpty())
      data->setData("application/x-bts-asset",
                    items.front()->data(Qt::UserRole).toString().toUtf8());
    return data;
  }
  Qt::DropActions supportedDropActions() const override
  {
    return Qt::CopyAction;
  }
  void mouseDoubleClickEvent(QMouseEvent *event) override
  {
    const auto index = indexAt(event->position().toPoint());
    if (objectName() == "shelfAssets" && index.isValid() && (index.flags() & Qt::ItemIsEditable) &&
        event->position().y() > visualRect(index).top() + iconSize().height())
    {
      edit(index);
      event->accept();
      return;
    }
    QListWidget::mouseDoubleClickEvent(event);
  }
  void paintEvent(QPaintEvent *event) override
  {
    QListWidget::paintEvent(event);
    if (count() == 0) {
      QPainter painter(viewport());
      painter.setPen(QColor(bts::theme::Muted));
      painter.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignCenter, empty_text_);
    }
  }

 private:
  QString empty_text_;
};

}  // namespace

WorkspaceShell::WorkspaceShell(QWidget *viewport, Callbacks callbacks, QWidget *parent)
    : QMainWindow(parent), callbacks_(std::move(callbacks))
{
#include "ui/shell/WorkspaceShellMenus.inc"
#include "ui/shell/WorkspaceShellToolRail.inc"
#include "ui/shell/WorkspaceShellToolbar.inc"
#include "ui/shell/WorkspaceShellShelf.inc"
#include "ui/shell/WorkspaceShellTextureSets.inc"
#include "ui/shell/WorkspaceShellLayers.inc"
#include "ui/shell/WorkspaceShellChannels.inc"
#include "ui/shell/WorkspaceShellProperties.inc"
#include "ui/shell/WorkspaceShellBrush.inc"
#include "ui/shell/WorkspaceShellMaterial.inc"
#include "ui/shell/WorkspaceShellConsoleAndRails.inc"
}

void WorkspaceShell::closeEvent(QCloseEvent *event)
{
  if (!close_requested || close_requested())
    event->accept();
  else
    event->ignore();
}

void WorkspaceShell::retire_editor_callbacks() noexcept
{
  // A pending spinbox commits in hideEvent. Its connection may have a nested
  // panel context, so disconnecting only receivers on this shell is not enough.
  // Walk without allocating, and block BEFORE clearing callbacks or hiding UI.
  const auto retire = [this](auto &&self, QObject *object) noexcept -> void {
    object->blockSignals(true);
    QObject::disconnect(object, nullptr, this, nullptr);
    for (QObject *child : object->children())
      self(self, child);
  };
  retire(retire, this);
  callbacks_ = {};
  close_requested = {};
}

WorkspaceShell::~WorkspaceShell()
{
  retire_editor_callbacks();
}

void WorkspaceShell::set_environment(const QJsonObject &state)
{
  environment_panel_->set_state(state);
}

void WorkspaceShell::set_trackball(bool enabled)
{
  const QSignalBlocker block(trackball_toggle_);
  trackball_toggle_->setChecked(enabled);
}

void WorkspaceShell::set_symmetry(const SymmetrySettings &s)
{
  symmetry_ = s;
  const QSignalBlocker a(symmetry_toggle_), b(symmetry_manipulator_), c(symmetry_mode_),
      d(symmetry_axis_), e(symmetry_offset_), f(symmetry_copies_), g(symmetry_intersection_);
  symmetry_toggle_->setChecked(s.enabled);
  symmetry_manipulator_->setChecked(s.manipulator);
  symmetry_mode_->setCurrentIndex(int(s.mode));
  symmetry_axis_->setCurrentIndex(s.axis);
  symmetry_offset_->setValue(s.offset);
  symmetry_copies_->setValue(s.copies);
  symmetry_intersection_->setChecked(s.intersection);
  const bool radial = s.mode == SymmetryMode::Radial;
  symmetry_offset_->setEnabled(!radial);
  symmetry_copies_->setEnabled(radial);
  for (int i = 0; i < 3; ++i) {
    const QSignalBlocker blocker(symmetry_center_[i]);
    symmetry_center_[i]->setValue(s.center[i]);
    symmetry_center_[i]->setEnabled(radial);
  }
}
void WorkspaceShell::add_alpha_resource(const BrushAlpha &alpha)
{
  if (asset_catalog_) {
    StudioAsset asset;
    asset.name = QString::fromStdString(alpha.name());
    asset.filename = asset.name + ".alpha";
    asset.usages = {"alpha"};
    asset.alpha = alpha;
    asset_catalog_->add(std::move(asset));
  }
  else
    alpha_resources_.push_back(alpha);
  refresh_alpha_shelf();
}
bool WorkspaceShell::layer_thumbnails_pending() const
{
  return thumbnails_ && thumbnails_->pending();
}
void WorkspaceShell::refresh_alpha_shelf()
{
  if (auto *label = findChild<QLabel *>("layerMaterialLibraryCount")) {
    int count = 0;
    if (asset_catalog_)
      for (const auto &asset : asset_catalog_->assets())
        if (asset_in_category(asset, "Materials"))
          ++count;
    label->setText(count == 1 ? QStringLiteral("1 library material") :
                                QString("%1 library materials").arg(count));
    label->setVisible(count > 0);
  }
  if (asset_catalog_ && smart_material_button_) {
    auto *menu = smart_material_button_->menu();
    std::vector<const StudioAsset *> presets;
    QByteArray signature;
    for (const auto &asset : asset_catalog_->assets())
      if (asset.smart_material && asset_in_category(asset, "Material presets")) {
        presets.push_back(&asset);
        signature += asset.id.toRfc4122();
        signature += QByteArray::number(qulonglong(asset.revision));
        signature += ';';
      }
    if (menu->property("materialPresetSignature").toByteArray() != signature) {
      menu->clear();
      menu->setProperty("materialPresetSignature", signature);
      for (const auto *asset : presets) {
        auto *action = menu->addAction(asset->name);
        action->setObjectName((asset->material_preset || is_spsm_asset(*asset)) ?
                                  "applyUserSmartMaterial" :
                                  "applyWornPaintedMetal");
        action->setData(asset->id.toString());
        const auto id = asset->id;
        connect(action, &QAction::triggered, menu, [this, id] {
          for (const auto &source : asset_catalog_->assets())
            if (source.id == id) {
              if (callbacks_.asset_selected)
                callbacks_.asset_selected(source);
              break;
            }
        });
      }
    }
    const auto actions = menu->actions();
    bool available = false;
    for (int i = 0; i < int(presets.size()); ++i) {
      const bool enabled = smart_material_target_ready(
          *presets[i], asset_project_ready_, asset_smart_ready_);
      actions[i]->setEnabled(enabled);
      available |= enabled;
    }
    smart_material_button_->setEnabled(available);
    smart_material_button_->setToolTip(
        available            ? "Add material preset" :
        asset_project_ready_ ? "Import a Material Preset into Assets to use it" :
                               "Create or open a project to use material presets");
  }

  performance::Scope phase("shelf_ui_refresh");
  if (!alpha_assets_ || !shelf_categories_ || !shelf_categories_->currentItem())
    return;
  const auto category_name = shelf_categories_->currentItem()->text();
  const bool baked_category = category_name == "Baked";
  alpha_assets_->setGridSize(baked_category ? QSize(144, 116) : QSize(90, 114));
  alpha_assets_->setIconSize(QSize(84, 84));
  alpha_assets_->setWordWrap(baked_category);
  alpha_assets_->setTextElideMode(baked_category ? Qt::ElideNone : Qt::ElideRight);
  if (auto *hdr = findChild<QLabel *>("assetLibraryHeader")) {
    const QString scope_text = asset_scope_control_ ? asset_scope_control_->currentText() :
                                                      QStringLiteral("All libraries");
    hdr->setText(scope_text + "  /  " +
                 (category_name == "All" ? QStringLiteral("Your assets") : category_name));
  }
  if (asset_category_chip_) {
    asset_category_chip_->setVisible(category_name != "All");
    const QString chip_label = fontMetrics().elidedText(category_name, Qt::ElideRight, 120) + " ×";
    asset_category_chip_->setText(chip_label);
    const int min_w = asset_category_chip_->fontMetrics().horizontalAdvance(chip_label) + 12;
    asset_category_chip_->setMinimumWidth(min_w);
    asset_category_chip_->resize(std::max(asset_category_chip_->width(), min_w), 22);
    asset_category_chip_->setAccessibleName("Filter: " + category_name);
    asset_category_chip_->setToolTip("Remove " + category_name + " filter");
  }
  asset_reset_->setEnabled(!alpha_search_.isEmpty() ||
                           asset_scope_ !=
                               (category_name == "Project" ? int(AssetScope::Project) : -1));
  if (auto *empty = findChild<QWidget *>("emptyMaterials")) {
    const bool material_category = category_name == "Materials" ||
                                   category_name == "Material presets";
    const bool presets = category_name == "Material presets";
    if (auto *note = empty->findChild<QLabel *>("emptyMaterialsNote"))
      note->setText(
          presets ?
              "No dynamic material presets yet. Add a Mask Generator to a layer folder, then save the folder to your library." :
              "Your material library is empty. Poly Haven materials are CC0. Import or drop downloaded textures here.");
    if (auto *download = empty->findChild<QPushButton *>("downloadFreeMaterialsEmpty"))
      download->setVisible(!presets);
    bool has_material = false;
    if (asset_catalog_)
      for (const auto &asset : asset_catalog_->assets())
        if (shelf_shows_asset(asset, category_name)) {
          has_material = true;
          break;
        }
    empty->setVisible(material_category && !has_material);
    alpha_assets_->setVisible(!(material_category && !has_material));
  }
  if (asset_catalog_) {
    const auto selected = alpha_assets_->currentItem() ?
                              alpha_assets_->currentItem()->data(Qt::UserRole).toString() :
                              alpha_assets_->property("lastSelectedAsset").toString();
    if (!selected.isEmpty())
      alpha_assets_->setProperty("lastSelectedAsset", selected);
    alpha_assets_->clear();
    const auto category = shelf_categories_->currentItem()->text();
    const AssetQuery query(alpha_search_);
    std::vector<const StudioAsset *> sorted;
    for (const auto &asset : asset_catalog_->assets())
      sorted.push_back(&asset);
    std::stable_sort(sorted.begin(), sorted.end(), [](auto a, auto b) {
      return QString::compare(a->name, b->name, Qt::CaseInsensitive) < 0;
    });
    for (const auto *asset : sorted) {
      if (asset->filename == "baked.map" && asset->search_terms.contains("baked-map"))
        continue;
      if (asset_scope_ >= 0 && int(asset->scope) != asset_scope_)
        continue;
      if (!shelf_shows_asset(*asset, category))
        continue;
      if (!query.matches(*asset))
        continue;
      const auto thumbnail = asset_thumbnail(*asset);
      auto *item = new QListWidgetItem(
          QIcon(QPixmap::fromImage(
              thumbnail.scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation))),
          asset->name,
          alpha_assets_);
      const auto id = asset->id.toString();
      item->setData(Qt::UserRole, id);
      if (asset->scope != AssetScope::Builtin)
        item->setFlags(item->flags() | Qt::ItemIsEditable);
      const int sequence_count = !asset->texture_files.empty() &&
                                         asset->texture_files.front().tile ?
                                     int(asset->texture_files.size()) :
                                     image_sequence_count(asset->bytes);
      item->setData(Qt::UserRole + 20, sequence_count);
      bool compatible = smart_material_target_ready(
                            *asset, asset_project_ready_, asset_smart_ready_) ||
                        ((asset->smart_mask || is_spmsk_asset(*asset)) && asset_project_ready_) ||
                        bool(asset->brush_preset) ||
                        (asset->usages.contains("alpha") && asset_paint_ready_) ||
                        (asset->material && asset_project_ready_ &&
                         (!asset->projection_weave || asset_projection_ready_) &&
                         (!asset_projection_ready_ || !asset->fill_preset)) ||
                        (asset->usages.contains("environment")) ||
                        (asset->usages.contains("procedural") && asset_fill_ready_) ||
                        (asset->usages.contains("texture") && asset_project_ready_);
      if (path_active_ && asset->usages.contains("alpha") &&
          ((asset->alpha && asset->alpha->kind == AlphaKind::Bitmap) || !asset->bytes.isEmpty()))
        compatible = false;
      item->setToolTip(
          asset->name + "\n" + asset->filename +
          (asset->path.isEmpty() ? QString{} : "\n" + asset->path) + "\n" +
          asset_scope_name(asset->scope) +
          (asset->search_terms.contains("preview:color-metallic-roughness-sphere") ?
               QString(
                   " — Generated Color / Metallic / Roughness sphere; normal, bump and displacement not shown") :
               QString{}) +
          (asset->search_terms.contains("builtin:native-smart-mask") ?
               QString(
                   " — Native GPU mask on a synthetic cavity/ridge sphere. White coverage brightens a neutral gray carrier; project coverage depends on its mesh maps.") :
               QString{}) +
          (sequence_count ? QString(" — %1 UDIM images").arg(sequence_count) : QString{}) +
          (compatible ? QString{} : " — Incompatible with current target"));
      if (!compatible)
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
      if (id == selected)
        alpha_assets_->setCurrentItem(item);
    }
    if ((category == "Baked" || category == "All" || category == "Project") &&
        (asset_scope_ < 0 || asset_scope_ == int(AssetScope::Project)) && callbacks_.baked_maps)
      for (const auto &baked : callbacks_.baked_maps()) {
        StudioAsset probe;
        probe.name = baked.name;
        probe.usages = {"texture"};
        probe.search_terms = {"baked-map"};
        if (!query.matches(probe))
          continue;
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(baked.thumbnail)), baked.name);
        int position = 0;
        while (position < alpha_assets_->count() &&
               QString::compare(
                   alpha_assets_->item(position)->text(), baked.name, Qt::CaseInsensitive) <= 0)
          ++position;
        alpha_assets_->insertItem(position, item);
        item->setData(Qt::UserRole, baked.id);
        item->setToolTip(baked.description.isEmpty() ? baked.name + " — baked mesh map" :
                                                       baked.description);
        if (baked.id == selected)
          alpha_assets_->setCurrentItem(item);
      }
    return;
  }
  alpha_assets_->clear();
  const auto category = shelf_categories_->currentItem()->text();
  if (category != "All" && category != "Alphas")
    return;
  for (int index = 0; index < int(alpha_resources_.size()); ++index) {
    const auto &alpha = alpha_resources_[index];
    const auto name = QString::fromStdString(alpha.name());
    if (!name.contains(alpha_search_, Qt::CaseInsensitive))
      continue;
    auto *item = new QListWidgetItem(
        QIcon(QPixmap::fromImage(AlphaPanel::thumbnail(alpha))), name, alpha_assets_);
    item->setData(Qt::UserRole, index);
    item->setToolTip(name +
                     (alpha.kind == AlphaKind::Bitmap ? " — session alpha" : " — built-in alpha"));
  }
}
void WorkspaceShell::show_texture_set(const TextureSet *set,
                                      const NewProjectOptions *settings,
                                      const BrushSettings *brush)
{
  // Only the view is filtered. UUIDs and callbacks still address the complete
  // authoritative set; no filtered copy is ever published to the document.
  const bool opacity_stack = set && set->material_layering.enabled &&
                             set->material_layering.active_mask >= 0;
  const bool retained_main = set && set->material_layering.enabled &&
                             set->material_layering.active_mask < 0;
  setProperty("materialOpacityChannel",
              opacity_stack ? QVariant(material_layering_channel_context(*set, 0)) : QVariant{});
  layers_->setEnabled(!retained_main);
  std::optional<TextureSet> material_view;
  if (set && set->material_layering.allocated()) {
    const bool retained_main = set->material_layering.enabled &&
                               set->material_layering.active_mask < 0;
    material_view = material_layering_display(*set);
    if (retained_main)
      material_view->uniform_editable = false;
    set = &*material_view;
  }
  add_adjustment_action_->setEnabled(set && set->uniform_editable && !set->disabled &&
                                     !opacity_stack && bool(callbacks_.add_adjustment));
  performance::Scope phase("layer_properties_ui_refresh");
  auto section = std::make_unique<performance::Scope>("show_texture_set_settings");
  const auto update_rows = [&](bool same_focus,
                               bool multiple,
                               bool effect_target,
                               bool geometry_target,
                               bool hierarchy_target,
                               bool instance_target,
                               bool painting,
                               bool mask_target) {
    section = std::make_unique<performance::Scope>("show_texture_set_layer_rows");
    const QSignalBlocker layer_signals(layers_);
    static_cast<LayerEffectRowState *>(findChild<QObject *>("layerEffectRowState"))
        ->synchronize(set);
    std::vector<const TextureLayer *> visible_layers;
    int collapsed_depth = -1;
    if (set)
      for (auto it = set->layers.rbegin(); it != set->layers.rend(); ++it) {
        if (collapsed_depth >= 0 && it->depth > collapsed_depth)
          continue;
        collapsed_depth = -1;
        visible_layers.push_back(&*it);
        if (it->kind == LayerKind::Folder && collapsed_folders_.contains(it->id))
          collapsed_depth = it->depth;
      }
    std::vector<std::pair<QUuid, QUuid>> row_ids;
    for (const auto *layer : visible_layers) {
      row_ids.emplace_back(layer->id, QUuid{});
      if (!set->selected_mask) {
        for (auto effect = layer->effects.rbegin(); effect != layer->effects.rend(); ++effect)
          if (effect->channel != 3)
            row_ids.emplace_back(layer->id, effect->id);
      }
      else if (layer->mask) {
        for (const auto &entry : mask_effect_entries(layer->mask->effects, true))
          row_ids.emplace_back(layer->id, entry.effect->id);
      }
    }
    bool rebuild = layers_->count() != int(row_ids.size());
    if (!rebuild)
      for (int row = 0; row < layers_->count(); ++row)
        rebuild |= QUuid(layers_->item(row)->data(Qt::UserRole).toString()) !=
                       row_ids[row].first ||
                   QUuid(layers_->item(row)->data(Qt::UserRole + 10).toString()) !=
                       row_ids[row].second;
    if (rebuild)
      layers_->clear();
    int selected_row = -1;
    material_label_->setText("MATERIAL");
    if (set) {
      int row = 0;
      for (const auto *it : visible_layers) {
        auto *item = rebuild ? new QListWidgetItem(layers_) : layers_->item(row);
        item->setData(adjustment_editable_role,
                      set->uniform_editable && !set->disabled && !opacity_stack &&
                          it->kind == LayerKind::Paint && !it->instance);
        item->setData(adjustment_checked_role, adjustment_layer(*it));
        const bool cached = layer_is_cached(*set, it->id);
        item->setData(Qt::UserRole + 27, cached);
        item->setData(Qt::UserRole + 28, it->precompose_below);
        item->setForeground(cached ? QBrush(QColor(bts::theme::Muted)) :
                                     QBrush(QColor(bts::theme::Heading)));
        item->setText(QString::fromStdString(it->name) + (cached ? " [Cached]" : ""));
        item->setData(Qt::UserRole + 19, it->instance.has_value());
        item->setData(Qt::UserRole + 20, set->instance_hierarchies.contains(it->id));
        item->setData(Qt::UserRole + 21,
                      set->instance_problems.contains(it->id) ? set->instance_problems.at(it->id) :
                                                                QString{});
        {
          std::set<std::string> names;
          for (const auto &[uid, name] : set->geometry_names)
            names.insert(name);
          int included = 0;
          if (it->geometry.type == 1) {
            for (int tile : set->tiles)
              included += it->geometry.includes_tile(tile);
          }
          else
            for (const auto &name : names)
              included += it->geometry.includes(name);
          item->setData(Qt::UserRole + 15, included);
          item->setData(Qt::UserRole + 16,
                        it->geometry.type == 1 ? int(set->tiles.size()) : int(names.size()));
          item->setData(Qt::UserRole + 17, it->id == set->selected_layer && geometry_target);
          item->setData(Qt::UserRole + 18, it->geometry.active());
        }
        item->setData(Qt::UserRole + 1,
                      (layer_channel_ == 14 ? it->displacement.opacity :
                       layer_channel_ >= 6  ? it->users[layer_channel_ - 6].opacity :
                       layer_channel_ == 5  ? it->ao_opacity :
                                              it->opacity[layer_channel_]) *
                          100);
        item->setData(Qt::UserRole, it->id.toString());
        item->setData(Qt::UserRole + 10, QString{});
        item->setData(Qt::UserRole + 11, !it->effects.empty());
        item->setData(Qt::UserRole + 12, it->mask && !it->mask->effects.empty());
        unsigned channel_marks = 0;
        for (int c : {0, 1, 2, 4})
          if (set->channels[c] && it->channels[c])
            channel_marks |= 1u << c;
        if (set->ao_channel && it->ao_enabled)
          channel_marks |= 1u << 5;
        if (set->displacement.channel && it->displacement.enabled)
          channel_marks |= 1u << 14;
        item->setData(Qt::UserRole + 22, channel_marks);
        item->setData(Qt::UserRole + 2, it->depth);
        item->setData(Qt::UserRole + 3, it->kind == LayerKind::Folder);
        item->setData(Qt::UserRole + 4, collapsed_folders_.contains(it->id));
        item->setData(Qt::UserRole + 5,
                      int(layer_channel_ == 14 ? it->displacement.blending :
                          layer_channel_ >= 6  ? it->users[layer_channel_ - 6].blending :
                          layer_channel_ == 5  ? it->ao_blending :
                                                 it->blending[layer_channel_]));
        item->setData(Qt::UserRole + 6, it->mask.has_value());
        item->setData(Qt::UserRole + 8, it->id == set->selected_layer && set->selected_mask);
        if (it->mask) {
          const auto image = thumbnails_->image(set->id, it->id, layer_channel_, true);
          if (!image.isNull())
            item->setData(Qt::UserRole + 7, QIcon(QPixmap::fromImage(image)));
          item->setData(Qt::UserRole + 9, it->mask->enabled);
        }
        item->setFlags(cached ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) :
                                Qt::ItemFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                              Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
                                              Qt::ItemIsUserCheckable | Qt::ItemIsEditable));
        item->setData(InlineNameDelegate::name_role, QString::fromStdString(it->name));
        item->setCheckState(it->visible ? Qt::Checked : Qt::Unchecked);
        item->setSizeHint(QSize(0, 44));
        item->setToolTip(
            it->kind == LayerKind::Folder ? "Folder; click its disclosure to expand or collapse" :
            it->kind == LayerKind::Fill   ? "Uniform fill; right-click to change stack order" :
                                            "Paint layer; left-drag in 3D or UV to paint");
        {
          const auto image = thumbnails_->image(set->id, it->id, layer_channel_, false);
          if (!image.isNull())
            item->setIcon(QIcon(QPixmap::fromImage(image)));
        }
        if (it->id == set->selected_layer && set->selected_effect.isNull()) {
          selected_row = row;
          material_label_->setText(
              quick_mask_edit_ ? QString("GRAYSCALE — Quick mask") :
              hierarchy_target ? QString("Properties - ") + QString::fromStdString(it->name) :
              instance_target  ? QString("PROPERTIES — LAYER INSTANCE") :
              geometry_target  ? "GEOMETRY MASK" :
              polygon_active_ && painting   ? "POLYGON FILL" :
              painting && brush->erase      ? (mask_target ? "ERASE MASK — No material" :
                                                             "ERASER — No material") :
              painting && path_active_      ? "PROPERTIES — SURFACE PATH" :
              painting && brush->clone      ? "CLONE — No material" :
              painting && brush->smudge     ? "SMUDGE — No material" :
              painting && brush->projected  ? "PROPERTIES — PROJECTION" :
              mask_target                   ? "PAINT MASK" :
              it->kind == LayerKind::Folder ? "PROPERTIES — FOLDER" :
              it->kind == LayerKind::Fill   ? "FILL LAYER" :
                                              "PAINT");
        }
        ++row;
        if (!set->selected_mask)
          for (auto effect_iterator = it->effects.rbegin(); effect_iterator != it->effects.rend();
               ++effect_iterator)
          {
            const auto &effect = *effect_iterator;
            if (effect.channel == 3)
              continue;
            auto *child = rebuild ? new QListWidgetItem(layers_) : layers_->item(row);
            const QString name = effect.channel == 14 ?
                                     QString("Displacement") :
                                 effect.channel >= 6 ?
                                     QString::fromStdString(
                                         set->users.at(effect.channel - 6).name) :
                                 effect.channel == 5 ?
                                     QString("Ambient occlusion") :
                                 effect.channel == 3 ?
                                     QString("Normal") :
                                     QString::fromLatin1(channel_names[effect.channel]);
            child->setText(
                effect.kind == ContentEffectKind::LinearColorTransform ?
                    QString("Linear RGB: %1")
                        .arg(effect.linear_color ?
                                 linear_color_transform_label(*effect.linear_color) :
                                 QString("Invalid transform")) :
                effect.kind == ContentEffectKind::Anchor ?
                    QString("%1").arg(effect.anchor_name) :
                effect.kind == ContentEffectKind::Generator ?
                    QString("%1 - %2").arg(effect.generator ?
                                               mask_generator_names[effect.generator] :
                                               "Generator (empty)",
                                           name) :
                effect.kind == ContentEffectKind::Filter ?
                    QString("%1").arg(filter_names[effect.filter.resource]) :
                effect.kind == ContentEffectKind::Paint ? QString("Paint") :
                effect.kind == ContentEffectKind::Fill  ? QString("Fill - %1").arg(name) :
                                                          QString("Levels - %1").arg(name));
            if (!effect.name.empty())
              child->setText(QString::fromStdString(effect.name));
            child->setData(Qt::UserRole + 24,
                           effect.kind != ContentEffectKind::Anchor &&
                               effect.kind != ContentEffectKind::LinearColorTransform);
            child->setData(Qt::UserRole + 2, it->depth + 1);
            child->setData(Qt::UserRole + 23,
                           int(effect.kind == ContentEffectKind::Generator ?
                                   PainterGlyph::Generator :
                               effect.kind == ContentEffectKind::Filter ||
                                       effect.kind == ContentEffectKind::LinearColorTransform ?
                                   PainterGlyph::Filter :
                               effect.kind == ContentEffectKind::Paint  ? PainterGlyph::Paint :
                               effect.kind == ContentEffectKind::Fill   ? PainterGlyph::Fill :
                               effect.kind == ContentEffectKind::Anchor ? PainterGlyph::Anchor :
                                                                          PainterGlyph::Levels));
            child->setData(
                Qt::UserRole + 13,
                effect.kind == ContentEffectKind::Anchor    ? int(MaskEffectKind::Anchor) :
                effect.kind == ContentEffectKind::Generator ? int(MaskEffectKind::Generator) :
                effect.kind == ContentEffectKind::Filter    ? int(MaskEffectKind::Filter) :
                effect.kind == ContentEffectKind::Paint     ? int(MaskEffectKind::Paint) :
                effect.kind == ContentEffectKind::Fill      ? int(MaskEffectKind::Fill) :
                                                              -1);
            child->setData(Qt::UserRole + 14,
                           effect.kind == ContentEffectKind::Filter && effect.filter.resource > 0);
            child->setData(Qt::UserRole + 1, effect.opacity * 100);
            child->setData(Qt::UserRole + 5, int(effect.blending));
            child->setData(Qt::UserRole, it->id.toString());
            child->setData(Qt::UserRole + 10, effect.id.toString());
            child->setData(Qt::UserRole + 12, false);
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                            Qt::ItemIsEditable);
            child->setData(InlineNameDelegate::name_role,
                           effect.kind == decltype(effect.kind)::Anchor ? effect.anchor_name :
                           !effect.name.empty() ? QString::fromStdString(effect.name) :
                                                  child->text().trimmed());
            child->setCheckState(effect.enabled ? Qt::Checked : Qt::Unchecked);
            child->setSizeHint(QSize(0, 26));
            child->setToolTip(
                effect.kind == ContentEffectKind::Anchor ?
                    "Anchor marker; blend and opacity do not apply. Click × to remove." :
                effect.kind == ContentEffectKind::LinearColorTransform ?
                    "Explicit linear RGB conversion after source-space Levels; the Fill tint is applied in ACEScg. Disable for identity conversion." :
                    "Editable content effect; click × to remove");
            if (effect.id == set->selected_effect && it->id == set->selected_layer) {
              selected_row = row;
              material_label_->setText(
                  effect.kind == ContentEffectKind::LinearColorTransform ?
                      "PROPERTIES — LINEAR COLOR" :
                  effect.kind == ContentEffectKind::Anchor    ? "PROPERTIES — ANCHOR POINT" :
                  effect.kind == ContentEffectKind::Generator ? "PROPERTIES — GENERATOR" :
                  effect.kind == ContentEffectKind::Filter    ? "PROPERTIES — FILTER" :
                  effect.kind == ContentEffectKind::Paint     ? "PROPERTIES — PAINT" :
                  effect.kind == ContentEffectKind::Fill      ? "PROPERTIES — FILL" :
                                                                "PROPERTIES — LEVELS");
            }
            ++row;
          }
        if (set->selected_mask && it->mask)
          for (const auto &entry : mask_effect_entries(it->mask->effects, true)) {
            const auto *effect = entry.effect;
            auto *child = rebuild ? new QListWidgetItem(layers_) : layers_->item(row);
            child->setText(effect->kind == MaskEffectKind::Group ?
                               "Group" :
                           effect->kind == MaskEffectKind::ColorSelection ?
                               "Color selection" :
                           effect->kind == MaskEffectKind::Fill ?
                               "Fill" :
                           effect->kind == MaskEffectKind::Anchor ?
                               QString("%1").arg(effect->anchor_name) :
                           effect->kind == MaskEffectKind::Filter ?
                               QString("%1").arg(filter_names[effect->filter.resource]) :
                           effect->kind == MaskEffectKind::Paint ?
                               "Paint" :
                           effect->kind == MaskEffectKind::Compare ?
                               "Compare mask" :
                           effect->kind == MaskEffectKind::Generator ?
                               (effect->generator == 0 ?
                                    "Generator (empty)" :
                                    QString("%1").arg(mask_generator_names[effect->generator])) :
                               "Levels");
            if (!effect->name.empty())
              child->setText(QString::fromStdString(effect->name));
            child->setData(Qt::UserRole + 24,
                           effect->kind != MaskEffectKind::Anchor &&
                               effect->kind != MaskEffectKind::ColorSelection);
            child->setData(Qt::UserRole + 2, it->depth + 1 + int(entry.depth));
            child->setData(Qt::UserRole + 23,
                           int(effect->kind == MaskEffectKind::Group ? PainterGlyph::Group :
                               effect->kind == MaskEffectKind::Generator ?
                                                                       PainterGlyph::Generator :
                               effect->kind == MaskEffectKind::Filter ? PainterGlyph::Filter :
                               effect->kind == MaskEffectKind::Paint  ? PainterGlyph::Paint :
                               effect->kind == MaskEffectKind::Fill   ? PainterGlyph::Fill :
                               effect->kind == MaskEffectKind::Anchor ? PainterGlyph::Anchor :
                               effect->kind == MaskEffectKind::Levels ? PainterGlyph::Levels :
                                                                        PainterGlyph::Mask));
            child->setData(Qt::UserRole + 13, int(effect->kind));
            child->setData(Qt::UserRole + 14,
                           effect->kind == MaskEffectKind::Filter && effect->filter.resource > 0);
            child->setData(Qt::UserRole + 1, effect->opacity * 100);
            child->setData(Qt::UserRole + 5, int(effect->blending));
            child->setData(Qt::UserRole, it->id.toString());
            child->setData(Qt::UserRole + 10, effect->id.toString());
            child->setData(Qt::UserRole + 12, true);
            child->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                            Qt::ItemIsEditable);
            child->setData(InlineNameDelegate::name_role,
                           effect->kind == decltype(effect->kind)::Anchor ? effect->anchor_name :
                           !effect->name.empty() ? QString::fromStdString(effect->name) :
                                                   child->text().trimmed());
            child->setCheckState(effect->enabled ? Qt::Checked : Qt::Unchecked);
            child->setSizeHint(QSize(0, 26));
            child->setToolTip(
                effect->kind == MaskEffectKind::Anchor ?
                    "Anchor marker; blend and opacity do not apply. Click × to remove." :
                effect->kind == MaskEffectKind::ColorSelection ?
                    "Color selection uses its Output, Hardness and Tolerance properties; row blend and opacity do not apply. Click × to remove." :
                    "Mask effect; click × to remove");
            if (effect->id == set->selected_effect && it->id == set->selected_layer) {
              selected_row = row;
              material_label_->setText(
                  effect->kind == MaskEffectKind::Group          ? "PROPERTIES — GROUP" :
                  effect->kind == MaskEffectKind::ColorSelection ? "PROPERTIES — COLOR SELECTION" :
                  effect->kind == MaskEffectKind::Fill           ? "PROPERTIES — FILL" :
                  effect->kind == MaskEffectKind::Anchor         ? "PROPERTIES — ANCHOR POINT" :
                  effect->kind == MaskEffectKind::Filter         ? "PROPERTIES — FILTER" :
                  effect->kind == MaskEffectKind::Paint          ? "PROPERTIES — PAINT" :
                  effect->kind == MaskEffectKind::Compare        ? "PROPERTIES — COMPARE MASK" :
                  effect->kind == MaskEffectKind::Generator      ? "PROPERTIES — GENERATOR" :
                                                                   "PROPERTIES — LEVELS");
            }
            ++row;
          }
      }
    }
    if (!same_focus || rebuild || effect_target ||
        (!multiple && set &&
         !LayerEffectRowState::current_target_matches(layers_->currentIndex(), *set)))
      layers_->setCurrentRow(selected_row, QItemSelectionModel::ClearAndSelect);
    const TextureLayer *selected_layer = nullptr;
    if (set)
      for (const auto &layer : set->layers)
        if (layer.id == set->selected_layer) {
          selected_layer = &layer;
          break;
        }
    const bool editable_selection = set && set->uniform_editable && !set->disabled &&
                                    selected_layer && !layer_is_cached(*set, selected_layer->id);
    auto *delete_button = findChild<QToolButton *>("deleteLayer");
    delete_button->setEnabled(editable_selection && !layers_->selectedItems().isEmpty() &&
                              bool(callbacks_.delete_selection));
    delete_button->setToolTip(delete_button->isEnabled() ?
                                  "Delete selected layers, masks or effects" :
                                  "Select an editable layer, mask or effect to delete");
    auto *precompose_button = findChild<QToolButton *>("addPrecomposeLayer");
    precompose_button->setEnabled(editable_selection && selected_layer->depth == 0 &&
                                  bool(callbacks_.precompose_layer));
    precompose_button->setToolTip(precompose_button->isEnabled() ?
                                      "Add Pre-compose / Cache Layer" :
                                      "Select an editable top-level layer for Pre-compose");
    if (multiple && !rebuild)
      material_label_->setText(QString("%1 LAYERS SELECTED").arg(layers_->selectedItems().size()));
    move_layer_up_->setEnabled(set && !effect_target && set->uniform_editable && selected_row > 0);
    move_layer_down_->setEnabled(set && !effect_target && set->uniform_editable &&
                                 selected_row >= 0 && selected_row + 1 < layers_->count());
    if (settings) {
      const QSignalBlocker block(resolution_);
      resolution_->setEnabled(set && !set->disabled);
      refresh_texture_extent_choices(
          *resolution_, 128, set ? set->texture_resolution : int(settings->resolution));
      {
        const QSignalBlocker height_block(resolution_height_), lock_block(resolution_lock_);
        resolution_lock_->setEnabled(set && !set->disabled);
        resolution_lock_->setChecked(!set || set->size_linked);
        resolution_lock_->setText(!set || set->size_linked ? "Linked" : "Unlinked");
        resolution_height_->setEnabled(set && !set->disabled && !set->size_linked);
        refresh_texture_extent_choices(
            *resolution_height_, 128, set ? set->rows() : int(settings->resolution));
      }
      resolution_->setToolTip(
          QString("%1 normal map convention; %2")
              .arg(settings->normal_map_format == NormalMapFormat::OpenGL ? "OpenGL" : "DirectX")
              .arg(settings->uv_workflow == UvWorkflow::SingleTile ? "Single UV tile" :
                                                                     "Preserve UV tiles"));
    }
    if (auto *title = findChild<QLabel *>("propertiesDockTitle")) {
      QString text = material_label_->text();
      if (text == "FILL LAYER")
        text = "PROPERTIES - FILL";
      else if (text == "PAINT" || text == "PAINT MASK")
        text = set && set->selected_mask ? "PROPERTIES - MASK" : "PROPERTIES - PAINT";
      else if (text == "MATERIAL")
        text = "PROPERTIES";
      else if (!text.startsWith("PROPERTIES"))
        text = "PROPERTIES - " + text;
      text.replace(QString::fromUtf8("—"), "-");
      title->setText(text);
      if (properties_float_)
        properties_float_->setWindowTitle(text);
    }
    material_label_->setVisible(material_label_->text() != "FILL LAYER" &&
                                !material_label_->text().startsWith("PROPERTIES"));
  };
  // Publish structural selection/list changes before synchronizing expensive
  // Properties widgets after the first composed frame.
  if (property("layerRowsOnly").toBool() && thumbnails_) {
    update_rows(false, false, false, false, false, false, false, false);
    return;
  }
  if (shown_texture_set_ != (set ? set->id : QUuid{}) ||
      shown_texture_layer_ != (set ? set->selected_layer : QUuid{}))
    fill_panel_->reset_resource_channel();
  shown_texture_set_ = set ? set->id : QUuid{};
  shown_texture_layer_ = set ? set->selected_layer : QUuid{};
  if (!thumbnails_) {
    thumbnails_ = std::make_unique<LayerThumbnailJobs>();
    thumbnails_->submit_request = callbacks_.thumbnail_requested;
    thumbnails_->read_result = callbacks_.thumbnail_result;
    thumbnails_->updated =
        [this](
            const QUuid &set_id, int channel, const QUuid &layer, bool mask, const QImage &image) {
          if (set_id != shown_texture_set_ || channel != layer_channel_)
            return;
          const QSignalBlocker blocker(layers_);
          for (int row = 0; row < layers_->count(); ++row) {
            auto *item = layers_->item(row);
            if (QUuid(item->data(Qt::UserRole).toString()) != layer ||
                !item->data(Qt::UserRole + 10).toString().isEmpty())
              continue;
            const QIcon icon(QPixmap::fromImage(image));
            if (mask)
              item->setData(Qt::UserRole + 7, icon);
            else
              item->setIcon(icon);
          }
          setProperty("thumbnailWorkerUpdates", property("thumbnailWorkerUpdates").toInt() + 1);
        };
  }
  if (set && !property("layerSelectionOnly").toBool())
    thumbnails_->request(*set, layer_channel_);
  findChild<QAction *>("configureChannels")
      ->setEnabled(set && set->uniform_editable && !set->disabled && !opacity_stack);
  mesh_maps_panel_->synchronize(set);
  for (int r = 0; set && r < layers_->count(); ++r) {
    auto *item = layers_->item(r);
    const bool cached = layer_is_cached(*set, QUuid(item->data(Qt::UserRole).toString()));
    item->setData(Qt::UserRole + 27, cached);
    if (cached) {
      item->setForeground(QColor(bts::theme::Muted));
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
  }
  findChild<QWidget *>("materialProperties")
      ->setEnabled(!set || !layer_is_cached(*set, set->selected_layer));
  findChild<QToolButton *>("toolbarBakeMeshMaps")
      ->setEnabled(set && !set->disabled && bool(callbacks_.bake_mesh_maps));
  findChild<QWidget *>("emptyLayersView")->setVisible(!set);
  layers_->setVisible(set);
  const QString unavailable =
      !set ?
          QStringLiteral("No project open") :
      !set->uniform_editable ?
          (property("materialEditabilityReason").toString().isEmpty() ?
               QStringLiteral(
                   "Requires a Principled BSDF connected directly to Surface with unlinked Base Color/Roughness/Metallic/Normal inputs") :
               property("materialEditabilityReason").toString()) :
          QString{};
  for (auto *button : {paint_button_, fill_button_, folder_button_, mask_button_, effects_button_})
    button->setToolTip(unavailable.isEmpty() ? button->accessibleName() : unavailable);
  {
    const TextureLayer *selected = nullptr;
    if (set)
      for (const auto &layer : set->layers)
        if (layer.id == set->selected_layer)
          selected = &layer;
    const bool editable = set && set->uniform_editable && selected && !selected->instance &&
                          selected->kind != LayerKind::Folder && !set->selected_mask;
    displacement_panel_->synchronize(
        set ? set->displacement : DisplacementSettings{},
        editable,
        selected ?
            std::optional<bool>(set->displacement.channel && selected->displacement.enabled) :
            std::nullopt);
  }
  const bool has_scattering = set &&
                              std::any_of(set->users.begin(), set->users.end(), [](const auto &c) {
                                return c.scattering && c.present;
                              });
  const bool can_scatter = set &&
                           std::any_of(set->users.begin(), set->users.end(), [](const auto &c) {
                             return c.scattering || !c.allocated;
                           });
  scattering_add_->setEnabled(set && set->uniform_editable && !has_scattering && can_scatter);
  for (int slot = 0; slot < custom_channel_count; ++slot) {
    const auto name = QString::fromStdString(set ? set->users[slot].name :
                                                   std::string("User") + std::to_string(slot));
    const bool present = set && set->users[slot].present;
    user_rows_[slot]->setVisible(present);
    user_rows_[slot]->setEnabled(set && set->uniform_editable);
    user_add_[slot]->setVisible(!set || !set->users[slot].scattering);
    user_add_[slot]->setEnabled(set && set->uniform_editable && !present);
    user_names_[slot]->setReadOnly(set && set->users[slot].scattering);
    user_add_[slot]->setText(name);
    const QSignalBlocker a(user_names_[slot]), b(user_formats_[slot]);
    if (user_names_[slot]->text() != name)
      user_names_[slot]->setText(name);
    user_formats_[slot]->setCurrentIndex(
        int(set ? set->users[slot].precision : ChannelPrecision::L32F));
  }

  for (int slot = 0; slot < custom_channel_count; ++slot)
    user_formats_[slot]->setEnabled(set && set->uniform_editable && !set->disabled &&
                                    !opacity_stack);
  if (opacity_stack) {
    scattering_add_->setEnabled(false);
    for (int slot = 0; slot < custom_channel_count; ++slot) {
      user_add_[slot]->setEnabled(false);
      user_names_[slot]->setReadOnly(true);
    }
  }
  for (int c = 0; c < 6; ++c) {
    const QSignalBlocker block(channel_formats_[c]);
    channel_formats_[c]->setEnabled(set && set->uniform_editable &&
                                    (c == 5 ? set->ao_channel : set->channels[c]));
    channel_formats_[c]->setCurrentIndex(
        int(set ? set->precision[c] : default_channel_precision[c]));
  }
  ao_channel_row_->setVisible(set && set->ao_channel);
  ao_add_action_->setEnabled(set && set->uniform_editable && !set->ao_channel);
  {
    const QSignalBlocker blocker(ao_mixing_);
    ao_mixing_->setEnabled(set && set->uniform_editable && set->ao_channel);
    ao_mixing_->setCurrentIndex(
        ao_mixing_->findData(set && set->ao_channel ? int(set->ao_mixing) : 0));
  }
  {
    const QSignalBlocker blocker(bump_method_);
    bump_method_->setEnabled(set && set->uniform_editable && set->channels[4]);
    bump_method_->setCurrentIndex(set && set->channels[4] ? int(set->bump_method) : 0);
  }
  {
    const QSignalBlocker blocker(normal_mixing_);
    normal_mixing_->setEnabled(set && set->uniform_editable && set->channels[3]);
    normal_mixing_->setCurrentIndex(
        normal_mixing_->findData(set && set->channels[3] ? int(set->normal_mixing) : 0));
  }
  findChild<QScrollArea *>("textureChannelsScroll")
      ->setFixedHeight(
          20 + 30 * (set ? int(std::count(set->channels.begin(), set->channels.end(), true)) -
                               int(set->channels[3]) + int(set->ao_channel) +
                               int(std::count_if(set->users.begin(),
                                                 set->users.end(),
                                                 [](const auto &user) { return user.present; })) :
                           0));
  for (int i = 0; i < 5; ++i) {
    channel_rows_[i]->setVisible(i != 3 && set && set->channels[i]);
    channel_rows_[i]->setEnabled(set && set->uniform_editable);
    channel_add_actions_[i]->setVisible(i != 3);
    channel_add_actions_[i]->setEnabled(i != 3 && set && set->uniform_editable &&
                                        !set->channels[i]);
  }
  {
    const QSignalBlocker blocker(layer_context_);
    layer_context_->clear();
    for (int c = 0; c < 5; ++c)
      if (c != 3 && set && set->channels[c])
        layer_context_->addItem(channel_names[c], c);
    if (set && set->ao_channel)
      layer_context_->addItem("Ambient occlusion", 5);
    if (set)
      for (int slot = 0; slot < custom_channel_count; ++slot)
        if (set->users[slot].present)
          layer_context_->addItem(QString::fromStdString(set->users[slot].name), 6 + slot);
    if (set && set->displacement.channel)
      layer_context_->addItem("Displacement", 14);
    int index = layer_context_->findData(layer_channel_);
    if (index < 0)
      index = layer_context_->count() ? 0 : -1;
    layer_context_->setCurrentIndex(index);
    if (index >= 0)
      layer_channel_ = layer_context_->currentData().toInt();
    layer_context_->setEnabled(index >= 0);
  }
  {
    const QSignalBlocker block(uv_tiles_);
    if (!set || displayed_set_ != set->id || !set->tiles.contains(active_tile_))
      active_tile_ =
          0;  // A mesh without UDIMs has the single 1001 tile and nothing to choose between,
    // so the list is only shown once there is more than one tile.
    uv_tiles_->setVisible(set != nullptr && set->tiles.size() > 1);
    bool rebuild = uv_tiles_->topLevelItemCount() != 1;
    if (set && !rebuild) {
      const auto *root = uv_tiles_->topLevelItem(0);
      rebuild = root->childCount() != int(set->tiles.size());
      int index = 0;
      for (int number : set->tiles)
        if (!rebuild && root->child(index++)->data(0, Qt::UserRole).toInt() != number)
          rebuild = true;
    }
    if (!set)
      rebuild = true;
    if (rebuild) {
      uv_tiles_->clear();
      if (set) {
        auto *root = new QTreeWidgetItem(uv_tiles_,
                                         {QString("UV Tiles (%1)").arg(set->tiles.size())});
        for (int number : set->tiles) {
          auto *item = new QTreeWidgetItem(root, {QString::number(number)});
          item->setData(0, Qt::UserRole, number);
          if (number == active_tile_)
            uv_tiles_->setCurrentItem(item);
        }
        root->setExpanded(true);
      }
    }
    if (set && uv_tiles_->topLevelItemCount()) {
      auto *root = uv_tiles_->topLevelItem(0);
      for (int i = 0; i < root->childCount(); ++i) {
        auto *item = root->child(i);
        const int tile = item->data(0, Qt::UserRole).toInt();
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, set->tile_loaded(tile) ? Qt::Checked : Qt::Unchecked);
        item->setForeground(0, QColor(set->tile_loaded(tile) ? theme::Text : theme::Disabled));
        item->setToolTip(0,
                         set->tile_loaded(tile) ? "Loaded — uncheck to hide and unload" :
                                                  "Unloaded — check to load from cache");
        item->setDisabled(!set->loaded);
      }
    }
  }
  settings_tile_->setVisible(active_tile_ > 0);
  settings_general_->setVisible(!active_tile_);
  if (set) {
    const QSignalBlocker block(tile_resolution_);
    tile_resolution_->setItemText(
        0,
        set->texture_resolution == set->rows() ?
            QString("%1 (Default)").arg(set->texture_resolution) :
            QString("%1 × %2 (Default)").arg(set->texture_resolution).arg(set->rows()));
    const int value = set->tile_resolutions.contains(active_tile_) ?
                          set->tile_resolutions.at(active_tile_) :
                          0;
    refresh_texture_extent_choices(*tile_resolution_, 128, value);
    tile_resolution_->setEnabled(active_tile_ > 0 && set->uniform_editable);
    tile_resolution_->setToolTip(
        QString("Evaluation size for UV tile %1; original paint is retained").arg(active_tile_));
  }
  {
    const QSignalBlocker blocker(padding_control_);
    padding_control_->setEnabled(set && set->uniform_editable);
    padding_control_->setCurrentIndex(!set || set->padding_3d ? 1 : 0);
  }
  section = std::make_unique<performance::Scope>("show_texture_set_properties");
  const bool same_focus = set && displayed_set_ == set->id && layers_->currentItem() &&
                          QUuid(layers_->currentItem()->data(Qt::UserRole).toString()) ==
                              set->selected_layer;
  const bool multiple = same_focus && layers_->selectedItems().size() != 1;
  displayed_set_ = set ? set->id : QUuid{};
  const TextureLayer *selected_instance_layer = nullptr;
  if (set)
    for (const auto &layer : set->layers)
      if (layer.id == set->selected_layer)
        selected_instance_layer = &layer;
  const bool instance_target = selected_instance_layer &&
                               selected_instance_layer->instance.has_value();
  const bool hierarchy_target = set && set->id == instance_hierarchy_set_ &&
                                set->selected_layer == instance_hierarchy_layer_ && !multiple;
  instance_panel_->synchronize(set, selected_instance_layer, hierarchy_target);
  instance_empty_->setVisible(instance_target && !hierarchy_target);
  copy_instance_->setEnabled(set && !multiple && set->selected_effect.isNull());
  paste_instance_->setEnabled(set && set->uniform_editable && !multiple &&
                              instance_clipboard_available_);
  const bool geometry_target = set && set->selected_geometry && !multiple && !instance_target &&
                               !hierarchy_target;
  const TextureLayer *geometry_layer = nullptr;
  if (geometry_target)
    for (const auto &layer : set->layers)
      if (layer.id == set->selected_layer)
        geometry_layer = &layer;
  geometry_panel_->synchronize(
      set, geometry_layer, settings && settings->uv_workflow == UvWorkflow::PreserveTiles);
  bool effect_target = set && !set->selected_effect.isNull() && !multiple && !geometry_target;
  const LevelsEffect *selected_effect = nullptr;
  const MaskEffect *selected_mask_effect = nullptr;
  if (effect_target)
    for (const auto &layer : set->layers)
      if (layer.id == set->selected_layer) {
        if (set->selected_mask && layer.mask) {
          selected_mask_effect = find_mask_effect(layer.mask->effects, set->selected_effect);
        }
        else
          for (const auto &effect : layer.effects)
            if (effect.id == set->selected_effect)
              selected_effect = &effect;
      }
  if (selected_effect && selected_effect->channel == 3) {
    effect_target = false;
    selected_effect = nullptr;
  }
  const bool folder_target = selected_instance_layer &&
                             selected_instance_layer->kind == LayerKind::Folder &&
                             !set->selected_mask && !effect_target && !multiple &&
                             !geometry_target && !instance_target && !hierarchy_target;
  static_cast<FolderPropertiesPanel *>(findChild<QWidget *>("folderProperties"))
      ->synchronize(set, folder_target ? selected_instance_layer : nullptr, layer_channel_);
  auto *linear_properties = findChild<QLabel *>("linearColorProperties");
  const bool linear_target = selected_effect &&
                             selected_effect->kind == ContentEffectKind::LinearColorTransform;
  linear_properties->setVisible(linear_target);
  if (linear_target)
    linear_properties->setText(
        selected_effect->linear_color ?
            QString(
                "Source: %1\nWorking space: ACEScg\n\nConverts the linear source before Fill tint. Use the eye to enable or bypass this conversion.")
                .arg(QString::fromStdString(selected_effect->linear_color->input_space)) :
            QString("The source color transform is unavailable."));
  const bool paint_effect = set && TextureDocument::paint_effect(*set);
  const bool mask_filling = selected_mask_effect &&
                            selected_mask_effect->kind == MaskEffectKind::Fill;
  std::optional<MaskEffect> content_anchor;
  if (selected_effect && selected_effect->kind == ContentEffectKind::Anchor) {
    content_anchor = MaskEffect{};
    content_anchor->kind = MaskEffectKind::Anchor;
    content_anchor->id = selected_effect->id;
    content_anchor->anchor_name = selected_effect->anchor_name;
  }
  anchor_panel_->synchronize(set,
                             content_anchor ? &*content_anchor :
                             selected_mask_effect &&
                                     selected_mask_effect->kind == MaskEffectKind::Anchor ?
                                              selected_mask_effect :
                                              nullptr);
  imported_effect_panel_->synchronize(
      selected_effect && selected_effect->kind == ContentEffectKind::Fill ? selected_effect :
                                                                            nullptr,
      selected_mask_effect && selected_mask_effect->kind == MaskEffectKind::Group ?
          selected_mask_effect :
          nullptr);
  const auto *mask_filter = selected_mask_effect &&
                                    selected_mask_effect->kind == MaskEffectKind::Filter ?
                                selected_mask_effect :
                                nullptr;
  const auto *content_filter = selected_effect &&
                                       selected_effect->kind == ContentEffectKind::Filter ?
                                   selected_effect :
                                   nullptr;
  filter_panel_->synchronize(set,
                             mask_filter    ? &mask_filter->filter :
                             content_filter ? &content_filter->filter :
                                              nullptr,
                             content_filter ? content_filter->channel : -1,
                             mask_filter    ? mask_filter->opacity :
                             content_filter ? content_filter->opacity :
                                              1,
                             mask_filter    ? mask_filter->blending :
                             content_filter ? content_filter->blending :
                                              LayerBlend::Normal);
  paint_effect_mix_->setVisible(paint_effect);
  if (paint_effect) {
    const QSignalBlocker a(paint_effect_blend_), b(paint_effect_opacity_);
    const auto mode = selected_mask_effect ? selected_mask_effect->blending :
                                             selected_effect->blending;
    const float opacity = selected_mask_effect ? selected_mask_effect->opacity :
                                                 selected_effect->opacity;
    paint_effect_blend_->setCurrentIndex(paint_effect_blend_->findData(int(mode)));
    paint_effect_opacity_->setValue(opacity * 100);
  }
  std::optional<LayerMask> mask_input;
  std::optional<LevelsEffect> mask_levels;
  if (selected_mask_effect && selected_mask_effect->kind == MaskEffectKind::Levels) {
    const auto &selected = *std::find_if(
        set->layers.begin(), set->layers.end(), [&](const auto &layer) {
          return layer.id == set->selected_layer;
        });
    mask_input = mask_effect_input(*selected.mask, set->selected_effect);
    mask_levels = selected_mask_effect->levels;
    mask_levels->id = selected_mask_effect->id;
    selected_effect = &*mask_levels;
  }
  compare_mask_panel_->synchronize(set,
                                   selected_mask_effect &&
                                           selected_mask_effect->kind == MaskEffectKind::Compare ?
                                       selected_mask_effect :
                                       nullptr);
  property_content_->setVisible((!effect_target || paint_effect) && !geometry_target &&
                                !instance_target && !hierarchy_target && !folder_target &&
                                !multiple);
  levels_panel_->synchronize(
      set,
      selected_effect && selected_effect->kind == ContentEffectKind::Levels ? selected_effect :
                                                                              nullptr,
      mask_input ? &*mask_input : nullptr);
  color_selection_panel_->synchronize(set,
                                      selected_mask_effect && selected_mask_effect->kind ==
                                                                  MaskEffectKind::ColorSelection ?
                                          selected_mask_effect :
                                          nullptr);
  mask_fill_panel_->synchronize(selected_mask_effect &&
                                        selected_mask_effect->kind == MaskEffectKind::Fill ?
                                    selected_mask_effect :
                                    nullptr,
                                set);
  std::optional<MaskEffect> content_generator;
  if (selected_effect && selected_effect->kind == ContentEffectKind::Generator) {
    content_generator = MaskEffect{};
    auto &effect = *content_generator;
    effect.kind = MaskEffectKind::Generator;
    static_cast<MeshGeneratorSettings &>(effect) = *selected_effect;
    effect.id = selected_effect->id;
    effect.enabled = selected_effect->enabled;
    effect.name = selected_effect->name;
    effect.opacity = selected_effect->opacity;
    effect.blending = selected_effect->blending;
  }
  mask_generator_panel_->synchronize(set,
                                     content_generator ? &*content_generator :
                                     selected_mask_effect && selected_mask_effect->kind ==
                                                                 MaskEffectKind::Generator ?
                                                         selected_mask_effect :
                                                         nullptr,
                                     content_generator ? selected_effect->channel : -1);
  effects_button_->setEnabled(set && set->uniform_editable && !multiple && !geometry_target &&
                              !instance_target && !hierarchy_target);
  for (auto *action : effects_button_->menu()->actions())
    action->setEnabled(effects_button_->isEnabled());
  effects_button_->menu()
      ->findChild<QAction *>("addMaskFillEffect")
      ->setEnabled(effects_button_->isEnabled() && set->selected_mask);
  effects_button_->menu()
      ->findChild<QAction *>("addMaskGenerator")
      ->setEnabled(set && set->uniform_editable && !multiple && !geometry_target &&
                   !instance_target && !hierarchy_target);
  effects_button_->menu()
      ->findChild<QAction *>("addMaskAnchor")
      ->setEnabled(set && set->uniform_editable && !multiple && !geometry_target &&
                   !instance_target && !hierarchy_target);
  effect_up_->setEnabled(effect_target);
  effect_down_->setEnabled(effect_target);

  const bool mask_target = set && (set->selected_mask || quick_mask_edit_) && !multiple;
  quick_mask_enable_->setEnabled(set && set->uniform_editable);
  mask_button_->setEnabled(set && set->uniform_editable && !multiple && !instance_target &&
                           !hierarchy_target);
  for (auto *action : mask_button_->menu()->actions())
    action->setEnabled(mask_button_->isEnabled());
  const bool has_mask = selected_instance_layer && selected_instance_layer->mask;
  for (const char *name : {"copyMaskContent",
                           "pasteIntoMask",
                           "createSmartMask",
                           "invertMaskBackground",
                           "removeMask"})
    if (auto *action = mask_button_->menu()->findChild<QAction *>(name))
      action->setEnabled(
          mask_button_->isEnabled() && has_mask &&
          (QLatin1StringView(name) != QStringView(u"pasteIntoMask") || mask_clipboard_available_));
  mask_value_row_->setVisible(mask_target && !polygon_active_ &&
                              !(brush && (brush->erase || brush->smudge || brush->clone)));
  if (brush) {
    const QSignalBlocker blocker(mask_value_);
    mask_value_->setValue(brush->mask_value);
  }
  const bool folder = set && !mask_target &&
                      std::any_of(set->layers.begin(), set->layers.end(), [&](const auto &layer) {
                        return layer.id == set->selected_layer && layer.kind == LayerKind::Folder;
                      });
  folder_button_->setEnabled(set && set->uniform_editable);
  if (brush) {
    if (selected_instance_layer &&
        (selected_instance_layer->kind != LayerKind::Fill || mask_target || paint_effect))
    {
      performance::Scope alpha_phase("brush_alpha_preview_ui");
      alpha_panel_->synchronize(*brush);
    }
    const QSignalBlocker a(brush_alignment_), b(brush_cull_), c(brush_cull_angle_),
        d(brush_cull_slider_);
    brush_alignment_->setCurrentIndex(int(brush->projection.alignment));
    brush_cull_->setChecked(brush->projection.cull);
    brush_cull_angle_->setValue(brush->projection.angle);
    brush_cull_slider_->setValue(qRound(brush->projection.angle * 10));
    const bool culling = brush->projection.alignment != BrushAlignment::UV;
    brush_cull_->setEnabled(culling);
    brush_cull_angle_->setEnabled(culling && brush->projection.cull);
    brush_cull_slider_->setEnabled(culling && brush->projection.cull);
  }
  const auto fill = set ? std::find_if(
                              set->layers.begin(),
                              set->layers.end(),
                              [&](const auto &layer) { return layer.id == set->selected_layer; }) :
                          std::vector<TextureLayer>::const_iterator{};
  const bool filling = set && !geometry_target && !instance_target && !hierarchy_target &&
                       !effect_target && !multiple && !mask_target && fill != set->layers.end() &&
                       fill->kind == LayerKind::Fill;
  fill_panel_->setVisible(filling);
  fill_toolbar_->setVisible((filling && fill->fill.mode != 6) ||
                            (mask_filling && selected_mask_effect->anchor_source.isNull()));
  for (auto *action : fill_gizmo_actions_)
    action->setEnabled((filling && fill->fill.mode != 6) ||
                       (mask_filling && selected_mask_effect->anchor_source.isNull() &&
                        selected_mask_effect->mapping.projection.mode != 6));
  if (filling) {
    displayed_fill_ = fill->fill;
    std::array<bool, 15> active{};
    QStringList names{
        "Base Color", "Metallic", "Roughness", "Normal", "Bump", "Ambient occlusion"};
    for (int c = 0; c < 5; ++c)
      active[c] = set->channels[c];
    active[5] = set->ao_channel;
    for (int slot = 0; slot < 8; ++slot) {
      active[slot + 6] = set->users[slot].present;
      names.append(QString::fromStdString(set->users[slot].name));
    }
    active[14] = set->displacement.channel;
    names.append("Displacement");
    fill_panel_->set_displacement_midlevel(set->displacement.midlevel);
    fill_panel_->set_channels(active, names);
    fill_panel_->set_anchor_context(set, set->selected_layer);
    fill_panel_->synchronize(fill->fill);
    auto *resolution_warning = fill_panel_->findChild<QLabel *>("fillResolutionWarning");
    if (!resolution_warning) {
      resolution_warning = new QLabel(fill_panel_);
      resolution_warning->setObjectName("fillResolutionWarning");
      resolution_warning->setWordWrap(true);
      resolution_warning->setStyleSheet(bts::theme::qss("color:@Warning@"));
      fill_panel_->layout()->addWidget(resolution_warning);
    }
    QStringList source_warnings;
    for (int c = 0; c < 15; ++c)
      if (active[c])
        if (const auto &source = fill_channel_texture(fill->fill, c)) {
          const auto check = [&](int tile, const auto &image) {
            if (image.width > set->width(tile) || image.height > set->rows(tile))
              source_warnings.append(names.value(c) +
                                     QString(": source %1×%2 > Texture Set %3×%4 (tile %5)")
                                         .arg(image.width)
                                         .arg(image.height)
                                         .arg(set->width(tile))
                                         .arg(set->rows(tile))
                                         .arg(tile));
          };
          if (source->tiles) {
            for (const auto &[tile, image] : *source->tiles)
              if (set->tiles.contains(tile))
                check(tile, *image);
          }
          else
            check(1001, *source);
        }
    resolution_warning->setText(source_warnings.join('\n'));
    resolution_warning->setVisible(!source_warnings.empty());
    const QSignalBlocker a(fill_manipulator_), b(fill_gizmo_size_);
    fill_manipulator_->setChecked(fill->fill.manipulator);
    fill_manipulator_->setText(fill->fill.mode == 0 ? "UV frame" : "Projection frame");
    fill_spatial_toolbar_->setVisible(fill->fill.mode != 6);
    fill_gizmo_size_->setValue(fill->fill.gizmo_size);
    for (int i = 0; i < 3; ++i) {
      const QSignalBlocker blocker(fill_gizmos_[i]);
      fill_gizmos_[i]->setChecked(i == fill->fill.gizmo);
    }
  }
  if (filling) {
    QString material_name;
    if (!fill->fill.material_resource.empty() && asset_catalog_) {
      const QUuid id(QString::fromStdString(fill->fill.material_resource));
      for (const auto &asset : asset_catalog_->assets())
        if (asset.id == id) {
          material_name = asset.name;
          break;
        }
    }
    fill_panel_->set_material_mode_text(material_name);
  }
  if (mask_filling) {
    displayed_fill_ = selected_mask_effect->mapping.projection;
    const QSignalBlocker block(fill_manipulator_);
    fill_manipulator_->setChecked(displayed_fill_.manipulator);
    fill_manipulator_->setText(fill_surface_projection(displayed_fill_) ? "Manipulator" :
                                                                          "UV frame");
    fill_spatial_toolbar_->setVisible(displayed_fill_.mode != 6);
  }
  findChild<QWidget *>("propertiesProjectionModes")
      ->setVisible((filling || mask_filling) && displayed_fill_.mode != 6);
  for (int mode : {0, 1}) {
    auto *button = findChild<QToolButton *>(mode == 0 ? "propertiesUVProjection" :
                                                        "propertiesTriplanarProjection");
    const QSignalBlocker block(button);
    button->setChecked(displayed_fill_.mode == mode);
  }
  if (filling || mask_filling) {
    displayed_fill_.cm_per_native = set->cm_per_native;
    displayed_fill_.native_per_uv = set->native_per_uv;
  }
  {
    const QSignalBlocker block(warp_geometry_toggle_);
    warp_geometry_toggle_->setVisible((filling || mask_filling) && displayed_fill_.mode == 5);
    warp_geometry_toggle_->setChecked(warp_geometry_enabled(displayed_fill_));
    warp_geometry_toggle_->setEnabled(set && set->uniform_editable);
  }
  const bool painting = set && !geometry_target && !instance_target && !hierarchy_target &&
                        (!effect_target || paint_effect) && !multiple && brush &&
                        std::any_of(
                            set->layers.begin(), set->layers.end(), [&](const auto &layer) {
                              return layer.id == set->selected_layer &&
                                     (layer.kind == LayerKind::Paint || mask_target ||
                                      paint_effect);
                            });
  const bool smart_ready = set && worn_metal_ready(*set);
  const bool user_ready = set && asset_catalog_ &&
                          std::any_of(asset_catalog_->assets().begin(),
                                      asset_catalog_->assets().end(),
                                      [](const auto &asset) {
                                        return (bool(asset.material_preset) ||
                                                is_spsm_asset(asset)) &&
                                               asset_in_category(asset, "Material presets");
                                      });
  smart_material_button_->setEnabled(smart_ready || user_ready);
  smart_material_button_->setToolTip(smart_ready || user_ready ?
                                         "Add material preset" :
                                         "Import a Material Preset into Assets to use it");
  const bool projection_ready = painting && brush && brush->projected.has_value();
  const bool changed_asset_context = asset_projection_ready_ != projection_ready ||
                                     asset_smart_ready_ != smart_ready ||
                                     asset_project_ready_ != bool(set) ||
                                     asset_paint_ready_ != (painting && !polygon_active_) ||
                                     asset_fill_ready_ != filling;
  asset_projection_ready_ = projection_ready;
  asset_smart_ready_ = smart_ready;
  asset_project_ready_ = bool(set);
  asset_paint_ready_ = painting && !polygon_active_;
  asset_fill_ready_ = filling;
  if (changed_asset_context)
    QTimer::singleShot(0, this, [this] { refresh_alpha_shelf(); });
  brush_controls_->setVisible(painting && !polygon_active_);
  brush_toolbar_->setVisible(!geometry_target && !instance_target && !hierarchy_target &&
                             !polygon_active_ && !filling && !mask_filling);
  polygon_toolbar_->setVisible(painting && polygon_active_);
  polygon_controls_->setVisible(painting && polygon_active_);
  for (int which = 0; which < 2; ++which) {
    polygon_values_[which]->parentWidget()->setVisible(mask_target);
    polygon_sliders_[which]->setVisible(mask_target);
  }
  polygon_tool_->setEnabled(painting && !quick_mask_edit_);
  polygon_action_->setEnabled(painting && !quick_mask_edit_);
  paint_tool_->setEnabled(painting);
  eraser_tool_->setEnabled(painting);
  const bool can_pick = (painting && !polygon_active_) ||
                        (filling && set && set->uniform_editable && !fill->instance &&
                         !fill->fill.native_material);
  picker_tool_->setEnabled(can_pick);
  picker_action_->setEnabled(can_pick);
  paint_tool_action_->setEnabled(painting);
  eraser_tool_action_->setEnabled(painting);
  paint_tool_->setChecked(painting && !polygon_active_ && !path_active_ && !brush->erase &&
                          !brush->projected && !brush->smudge && !brush->clone);
  smudge_tool_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  smudge_action_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  smudge_tool_->setChecked(painting && brush->smudge);
  eraser_tool_->setChecked(painting && brush->erase);
  projection_tool_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  projection_action_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  projection_tool_->setChecked(painting && bool(brush->projected));
  clone_tool_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  clone_action_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  clone_tool_->setChecked(painting && brush->clone);
  clone_relative_->setChecked(painting && !brush->clone_absolute);
  clone_absolute_->setChecked(painting && brush->clone_absolute);
  clone_tool_->setAccessibleName(brush && brush->clone_absolute ? "Clone (absolute source)" :
                                                                  "Clone (relative source)");
  clone_tool_->setToolTip(clone_tool_->accessibleName() +
                          " (6) — V+click to sample selected layer");
  path_tool_->setEnabled(painting && !polygon_active_ && !quick_mask_edit_);
  path_action_->setEnabled(path_tool_->isEnabled());
  path_tool_->setChecked(path_active_ && painting);
  lazy_toggle_->setEnabled(painting && !path_active_);
  lazy_action_->setEnabled(painting && !path_active_);
  lazy_controls_->setEnabled(painting && !path_active_ && brush->lazy_mouse);
  toolbar_spacing_->setEnabled(painting);
  toolbar_spacing_slider_->setEnabled(painting);
  toolbar_opacity_->setEnabled(painting);
  toolbar_opacity_slider_->setEnabled(painting);
  toolbar_flow_->setEnabled(painting);
  toolbar_flow_slider_->setEnabled(painting);
  toolbar_size_->setEnabled(painting);
  toolbar_size_slider_->setEnabled(painting);
  paint_button_->setEnabled(set && set->uniform_editable && bool(callbacks_.add_paint));
  if (brush) {
    if (painting && !polygon_active_)
      brush_preview_->synchronize(*brush);
    stencil_panel_->synchronize(brush->stencil);
    stencil_panel_->setVisible(!brush->projected);
    auto projection = brush->projected;
    if (projection)
      projection->values = brush->values;
    projection_panel_->synchronize(projection ? &*projection : nullptr);
    {
      const QSignalBlocker angle(brush_angle_), follow(brush_follow_path_);
      brush_angle_->setValue(brush->angle);
      brush_follow_path_->setChecked(brush->follow_path);
      brush_follow_path_->setText(brush->follow_path ? "On" : "Off");
    }
    const auto jitter = brush->jitter.values();
    for (int i = 0; i < 4; ++i) {
      const QSignalBlocker spin(brush_jitter_[i]), slider(brush_jitter_sliders_[i]);
      brush_jitter_[i]->setValue(jitter[i]);
      brush_jitter_sliders_[i]->setValue(qRound(jitter[i] * 10));
    }
    const QSignalBlocker a(brush_size_), b(toolbar_size_), c(toolbar_size_slider_);
    const QSignalBlocker d(brush_flow_), e(toolbar_flow_), f(brush_flow_slider_),
        g(toolbar_flow_slider_);
    const QSignalBlocker h(brush_opacity_), i(toolbar_opacity_), j(brush_opacity_slider_),
        k(toolbar_opacity_slider_);
    const QSignalBlocker lazy_a(lazy_toggle_), lazy_b(lazy_distance_), lazy_c(lazy_slider_);
    lazy_toggle_->setChecked(brush->lazy_mouse);
    lazy_distance_->setValue(brush->lazy_distance);
    lazy_slider_->setValue(qRound(brush->lazy_distance * 10));
    const QSignalBlocker spacing_a(brush_spacing_), spacing_b(toolbar_spacing_),
        spacing_c(brush_spacing_slider_), spacing_d(toolbar_spacing_slider_);
    brush_spacing_->setValue(brush->spacing);
    toolbar_spacing_->setValue(brush->spacing);
    brush_spacing_slider_->setValue(qRound(brush->spacing * 10));
    toolbar_spacing_slider_->setValue(qRound(brush->spacing * 10));
    brush_opacity_->setValue(brush->stroke_opacity);
    toolbar_opacity_->setValue(brush->stroke_opacity);
    brush_opacity_slider_->setValue(qRound(brush->stroke_opacity * 10));
    toolbar_opacity_slider_->setValue(qRound(brush->stroke_opacity * 10));
    brush_flow_->setValue(brush->flow);
    toolbar_flow_->setValue(brush->flow);
    brush_flow_slider_->setValue(qRound(brush->flow * 10));
    toolbar_flow_slider_->setValue(qRound(brush->flow * 10));
    brush_size_->setValue(brush->diameter);
    toolbar_size_->setValue(brush->diameter);
    toolbar_size_slider_->setValue(qRound(brush->diameter * 10));
    for (int i = 0; i < 5; ++i) {
      const QSignalBlocker blocker(brush_channels_[i]);
      brush_channels_[i]->setChecked(set && fill != set->layers.end() && fill->channels[i] &&
                                     set->channels[i]);
      brush_channels_[i]->setEnabled(set && set->uniform_editable && fill != set->layers.end() &&
                                     !fill->instance && !mask_target && i != 3);
      brush_channels_[i]->setToolTip(
          QString("%1 layer channel. Turning off clears its authored data; Undo restores it.")
              .arg(channel_names[i]));
      brush_channels_[i]->setVisible(i != 3 && !mask_target);
    }
  }
  for (int index = 0; index < 2; ++index) {
    const QSignalBlocker a(brush_pressure_[index]), b(toolbar_pressure_[index]),
        c(brush_minimum_[index]);
    const bool enabled = brush && (index == 0 ? brush->pressure_size : brush->pressure_flow);
    brush_pressure_[index]->setChecked(enabled);
    toolbar_pressure_[index]->setChecked(enabled);
    brush_pressure_[index]->setEnabled(painting);
    toolbar_pressure_[index]->setEnabled(painting);
    brush_minimum_[index]->setEnabled(painting && enabled);
    if (brush)
      brush_minimum_[index]->setValue(index == 0 ? brush->minimum_size : brush->minimum_flow);
  }
  for (int slot = 0; slot < custom_channel_count; ++slot) {
    const bool present = set && set->users[slot].present && !folder && !multiple && !mask_target;
    const auto name = QString::fromStdString(set ? set->users[slot].name : std::string{});
    const bool enabled = present && (painting ? brush->users[slot] : fill->users[slot].enabled);
    const float value = present ? (painting ? brush->user_values[slot] : fill->users[slot].value) :
                                  1;
    {
      const QSignalBlocker a(user_brush_[slot]), b(user_fill_enabled_[slot]),
          c(user_values_[slot]), d(user_sliders_[slot]);
      user_brush_[slot]->setVisible(present);
      user_brush_[slot]->setEnabled(present && set->uniform_editable);
      user_brush_[slot]->setChecked(enabled);
      user_brush_[slot]->setText(name.compare("Scattering", Qt::CaseInsensitive) == 0 ?
                                     QString("scatt") :
                                     name.toLower().left(6));
      user_brush_[slot]->setAccessibleName(name);
      user_brush_[slot]->setToolTip(name);
      user_value_rows_[slot]->setVisible(
          present && (!painting || enabled) &&
          !(painting && (brush->erase || brush->smudge || brush->clone)));
      user_value_labels_[slot]->setText(name);
      user_values_[slot]->setAccessibleName(name + " value");
      user_values_[slot]->setEnabled(enabled);
      user_values_[slot]->setValue(value);
      user_sliders_[slot]->setProperty("userFillPropertyEdit",
                                       filling && enabled && set->uniform_editable &&
                                           !material_layering_main_readonly(*set));
      user_sliders_[slot]->setEnabled(enabled);
      user_sliders_[slot]->setValue(qRound(value * 1000));
      user_fill_enabled_[slot]->setVisible(false);
      user_fill_enabled_[slot]->setChecked(enabled);
    }
  }
  {
    const bool available = set && !folder && !multiple && !mask_target &&
                           fill != set->layers.end() && !fill->instance;
    const bool active = available && set->displacement.channel && fill->displacement.enabled;
    auto *toggle = findChild<QToolButton *>("brushDisplacement");
    auto *value = findChild<QDoubleSpinBox *>("materialDisplacement");
    const QSignalBlocker a(toggle), b(value);
    findChild<QWidget *>("materialDisplacementRow")->setVisible(active);
    toggle->setVisible(painting && !mask_target);
    toggle->setChecked(active);
    toggle->setEnabled(available && set->uniform_editable);
    toggle->setToolTip(
        "Displacement layer channel. Turning off clears its authored data; Undo restores it.");
    value->setValue(active ? (painting ? brush->displacement_value : fill->displacement.value) :
                    set    ? set->displacement.midlevel :
                             .5);
    value->setEnabled(active && !(painting && (brush->erase || brush->smudge || brush->clone)));
  }
  const auto visible = [&](int channel) {
    return set && !folder && !multiple && !mask_target &&
           !(painting && (brush->erase || brush->smudge || brush->clone)) &&
           set->channels[channel] && (!painting || brush->channels[channel]);
  };
  const bool ao_visible = set && set->ao_channel && !folder && !multiple && !mask_target &&
                          !(painting && brush->erase);
  brush_ao_->setVisible(ao_visible);
  ao_value_row_->setVisible(ao_visible && (!painting || brush->ao) &&
                            !(painting && (brush->smudge || brush->clone)));
  {
    const QSignalBlocker blocker(brush_ao_);
    brush_ao_->setChecked(
        set &&
        (painting ? brush->ao :
                    std::find_if(set->layers.begin(), set->layers.end(), [&](const auto &layer) {
                      return layer.id == set->selected_layer;
                    })->ao_enabled));
    brush_ao_->setEnabled(set && set->uniform_editable && !multiple && !mask_target);
  }
  color_row_->setVisible(visible(0));
  secondary_color_row_->setVisible(painting && !mask_target && !folder);
  metallic_row_->setVisible(visible(1));
  roughness_row_->setVisible(!set || visible(2));
  // The roughness field is its own slider now, and the fill ramp takes over in
  // fill context, so this track is never a visible control. It stays alive
  // because the gesture a drag across the field starts still runs through it.
  roughness_slider_->hide();
  normal_label_->setVisible(false);
  normal_row_->setVisible(false);
  if (auto *bar = findChild<QWidget *>("fillTangentBump"))
    bar->hide();
  bump_label_->setVisible(visible(4));
  if (auto *bump_editor = findChild<QWidget *>("bumpEditor"))
    bump_editor->setVisible(visible(4));
  fill_button_->setEnabled(set && set->uniform_editable && bool(callbacks_.add_fill));
  if (set) {
    const auto &values = painting ? brush->values : TextureDocument::property_values(*set);
    normal_label_->setText("Normal");
    for (int component = 0; component < 3; ++component) {
      const QSignalBlocker blocker(normal_values_[component]);
      normal_values_[component]->setValue(values.normal[component]);
      normal_values_[component]->setEnabled(set->uniform_editable && visible(3));
    }
    if (auto *bar = findChild<QWidget *>("fillTangentBump"))
      static_cast<TangentBumpBar *>(bar)->set_normal(values.normal);
    fill_panel_->set_material_preview(
        values.base_color, values.metallic, values.roughness, values.normal);
    {
      const QSignalBlocker blocker(ao_value_);
      ao_value_->setValue(values.ambient_occlusion);
      ao_value_->setEnabled(set->uniform_editable);
    }
    bump_label_->setText("Bump");
    {
      const QSignalBlocker blocker(bump_control_);
      bump_control_->setValue(painting ? values.bump : set->bump_display(values.bump));
      bump_control_->setEnabled(set->uniform_editable && visible(4));
    }
    const QSignalBlocker metallic_blocker(metallic_control_);
    metallic_control_->setValue(values.metallic);
    metallic_control_->setEnabled(set->uniform_editable);
    set_roughness(values.roughness);
    color_picker_->set_color(values.base_color);
    color_picker_->setEnabled(set->uniform_editable);
  }
  auto *channel_panel = fill_panel_->channel_panel();
  fill_panel_->set_fill_context(filling);
  if (filling) {
    std::array<bool, 15> enabled{};
    for (int c = 0; c < 5; ++c)
      enabled[c] = fill->channels[c] && set->channels[c];
    enabled[5] = fill->ao_enabled && set->ao_channel;
    for (int c = 0; c < 8; ++c)
      enabled[c + 6] = fill->users[c].enabled;
    enabled[14] = fill->displacement.enabled && set->displacement.channel;
    fill_panel_->set_enabled_channels(enabled);
  }
  fill_panel_->refresh_channel_visibility();
  if (!filling && !painting)
    channel_panel->hide();
  for (int c = 0; c < 15; ++c) {
    const auto key = shown_texture_set_.toString() + shown_texture_layer_.toString() +
                     QString::number(c);
    const auto state = texture_load_states_.value(key);
    fill_panel_->set_load_status(c,
                                 state.value(0).toString(),
                                 state.value(1).toInt(),
                                 state.value(2).toInt(),
                                 state.value(3).toBool(),
                                 state.value(4).toBool());
  }
  update_rows(same_focus,
              multiple,
              effect_target,
              geometry_target,
              hierarchy_target,
              instance_target,
              painting,
              mask_target);
  // Last, once every control above has decided whether it is shown: a section
  // with nothing visible in it takes its heading away with it.
  for (auto *section : property_sections_)
    section->sync_visibility();
}

void WorkspaceShell::set_display_transforms(const QStringList &choices, const QString &selected)
{
  const QSignalBlocker blocker(display_control_);
  display_control_->clear();
  display_control_->addItems(choices);
  display_control_->setCurrentText(selected);
}

void WorkspaceShell::set_projection(OrbitCamera::Projection projection)
{
  const QSignalBlocker blocker(projection_control_);
  projection_control_->setCurrentIndex(projection == OrbitCamera::Projection::Orthographic ? 1 :
                                                                                             0);
}

void WorkspaceShell::set_view_layout(ViewLayoutState layout)
{
  view_layout_ = layout;
  const QSignalBlocker blocker(view_layout_control_);
  view_layout_control_->setCurrentIndex(layout.mode == ViewLayout::SceneOnly ? 0 :
                                        layout.mode == ViewLayout::UvOnly    ? 1 :
                                        layout.uv_first                      ? 3 :
                                                                               2);
}

void WorkspaceShell::set_reference_image_enabled(bool enabled)
{
  if (!reference_image_toggle_)
    return;
  const QSignalBlocker blocker(reference_image_toggle_);
  reference_image_toggle_->setChecked(enabled);
}

void WorkspaceShell::set_reference_image_path(const QString &path)
{
  if (!open_reference_image_button_)
    return;
  const QString file = QFileInfo(path).fileName();
  open_reference_image_button_->setToolTip(
      file.isEmpty() ? QStringLiteral("Open a reference image for the 2D view") :
                       QStringLiteral("Reference image: %1 (click to change)").arg(path));
  if (reference_image_toggle_) {
    reference_image_toggle_->setToolTip(
        file.isEmpty() ?
            QStringLiteral(
                "Show a 2D reference image in the 2D view instead of the UV layout and pick colors from it") :
            QStringLiteral(
                "Show %1 in the 2D view instead of the UV layout and pick colors from it")
                .arg(file));
  }
}

void WorkspaceShell::set_texture_sets(const QStringList &names, int selected_index)
{
  const QSignalBlocker blocker(texture_sets_);
  // Layer edits preserve the set list and its item identity. Rebuilding an
  // identical list needlessly invalidates the dock layout and viewport paint.
  if (texture_sets_->count() != names.size()) {
    texture_sets_->clear();
    texture_sets_->addItems(names);
  }
  else
    for (int i = 0; i < names.size(); ++i)
      if (texture_sets_->item(i)->text() != names[i])
        texture_sets_->item(i)->setText(names[i]);
  const int row = names.isEmpty() ? -1 : std::clamp(selected_index, 0, int(names.size()) - 1);
  for (int i = 0; i < texture_sets_->count(); ++i) {
    auto *item = texture_sets_->item(i);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    // Metadata renames intentionally skip the broad ShaderController refresh.
    // Keep the persistent row editor in step with the document's display name.
    if (auto *row_widget = texture_sets_->itemWidget(item))
      if (auto *name = row_widget->findChild<QLineEdit *>("textureSetName"))
        static_cast<InlineNameField *>(name)->synchronize(item->text());
  }
  texture_sets_->setCurrentRow(row);
  texture_sets_->setEnabled(!names.isEmpty() && bool(callbacks_.texture_set_selected));
  texture_sets_->setToolTip(names.isEmpty() ?
                                QStringLiteral("No visible mesh materials.") :
                                QStringLiteral("Select a material from the visible meshes."));
}

void WorkspaceShell::set_roughness(double value)
{
  if (!std::isfinite(value)) {
    return;
  }
  const QSignalBlocker spin_blocker(roughness_control_);
  const QSignalBlocker slider_blocker(roughness_slider_);
  roughness_control_->setValue(std::clamp(value, 0.0, 1.0));
  roughness_slider_->setValue(qRound(roughness_control_->value() * 1000.0));
}

void WorkspaceShell::set_roughness_editable(bool editable, const QString &reason)
{
  editable = editable && bool(callbacks_.roughness_changed);
  roughness_control_->setEnabled(editable);
  roughness_slider_->setEnabled(editable);
  const QString tooltip = editable          ? QStringLiteral("Material roughness") :
                          !reason.isEmpty() ? reason :
                                              QStringLiteral("No editable material selected.");
  roughness_control_->setToolTip(tooltip);
  roughness_slider_->setToolTip(tooltip);
}

void WorkspaceShell::set_status(const QString &message)
{
  statusBar()->showMessage(message);
}

void WorkspaceShell::set_error_status(const QString &message)
{
  auto *area = statusBar()->findChild<QWidget *>("workspaceError");
  if (!area) {
    area = new QWidget(statusBar());
    area->setObjectName("workspaceError");
    auto *layout = new QHBoxLayout(area);
    layout->setContentsMargins(6, 0, 0, 0);
    auto *label = new QLabel(area);
    label->setObjectName("workspaceErrorText");
    label->setStyleSheet(bts::theme::qss("color:@Error@"));
    label->setMaximumWidth(850);
    layout->addWidget(label);
    auto *dismiss = new QToolButton(area);
    dismiss->setObjectName("dismissWorkspaceError");
    dismiss->setText("×");
    dismiss->setToolTip("Dismiss error");
    connect(dismiss, &QToolButton::clicked, area, &QWidget::hide);
    layout->addWidget(dismiss);
    statusBar()->addPermanentWidget(area);
  }
  auto *label = area->findChild<QLabel *>("workspaceErrorText");
  label->setText(message);
  label->setToolTip(message);
  area->show();
  const int generation = area->property("errorGeneration").toInt() + 1;
  area->setProperty("errorGeneration", generation);
  QTimer::singleShot(15000, area, [area, generation] {
    if (area->property("errorGeneration").toInt() == generation)
      area->hide();
  });
}

void WorkspaceShell::append_console(const QString &message)
{
  console_text_->appendPlainText(message);
}

NativeMaterialImagePanel *WorkspaceShell::native_image_panel() const
{
  return fill_panel_->native_image_panel();
}
NativeMaterialPanel *WorkspaceShell::native_material_panel() const
{
  return fill_panel_->native_material_panel();
}
void WorkspaceShell::show_texture_shelf()
{
  set_shelf_visible(true);
  asset_scope_control_->setCurrentIndex(0);
  shelf_categories_->setCurrentRow(0);
  asset_search_->setText("u:texture");
  asset_search_->setFocus();
  refresh_alpha_shelf();
}
void WorkspaceShell::set_shelf_visible(bool visible)
{
  shelf_->setVisible(visible);
  shelf_action_->setChecked(visible);
}

void WorkspaceShell::set_console_visible(bool visible)
{
  console_->setVisible(visible);
  console_action_->setChecked(visible);
}

void WorkspaceShell::reset_reference_layout()
{
  if (properties_float_)
    dock_properties();
  if (properties_slot_)
    show_properties_tab();
  if (properties_action_) {
    const QSignalBlocker block(properties_action_);
    properties_action_->setChecked(true);
  }
  set_shelf_visible(true);
  set_console_visible(false);
  layer_settings_tabs_->setCurrentIndex(0);
  reference_layout_pending_ = !isVisible();
  const int width = std::max(968, workspace_splitter_->width() - handle_width);
  workspace_splitter_->setSizes({width - inspector_width, inspector_width});
  const int center_height = viewport_shelf_splitter_->height() - handle_width;
  const int shelf_height = std::max(shelf_minimum_height,
                                    qRound(centralWidget()->height() * 0.314017));
  viewport_shelf_splitter_->setSizes(
      {292, std::max(350, viewport_shelf_splitter_->width() - 292)});
  const int height = std::max(600, inspector_splitter_->height() - 2 * handle_width);
  const int layers = qRound(height * 0.53);
  const int console_height = 0;
  inspector_splitter_->setSizes({layers, height - layers - console_height, console_height});
}

void WorkspaceShell::float_properties()
{
  if (properties_float_ || !properties_panel_)
    return;
  hide_properties_tab();
  properties_float_ = new QDialog(this, Qt::Tool);
  properties_float_->setObjectName("propertiesFloat");
  properties_float_->setAttribute(Qt::WA_DeleteOnClose, false);
  auto *title = findChild<QLabel *>("propertiesDockTitle");
  properties_float_->setWindowTitle(title ? title->text() : QString("PROPERTIES"));
  auto *layout = new QVBoxLayout(properties_float_);
  layout->setContentsMargins(0, 0, 0, 0);
  properties_panel_->setParent(properties_float_);
  layout->addWidget(properties_panel_);
  properties_float_->resize(340, 720);
  properties_undock_->setToolTip("Dock properties");
  properties_undock_->setAccessibleName("Dock properties");
  connect(properties_float_, &QDialog::finished, this, [this] {
    if (!properties_float_)
      return;
    auto *dialog = properties_float_;
    properties_float_ = nullptr;
    properties_panel_->setParent(properties_slot_);
    static_cast<QVBoxLayout *>(properties_slot_->layout())->addWidget(properties_panel_);
    hide_properties_tab();
    properties_undock_->setToolTip("Undock properties");
    properties_undock_->setAccessibleName("Undock properties");
    const QSignalBlocker block(properties_action_);
    properties_action_->setChecked(false);
    dialog->deleteLater();
  });
  properties_float_->show();
  const QSignalBlocker block(properties_action_);
  properties_action_->setChecked(true);
}

void WorkspaceShell::show_update_available(const QString &release, const QUrl &page)
{
  if (!update_notice_ || !update_label_)
    return;
  update_page_ = page;
  update_label_->setText(QString("Update available — %1").arg(release));
  update_notice_->show();
}
PropertySection *WorkspaceShell::make_section(const QString &title, QWidget *parent, bool folded)
{
  auto *section = new PropertySection(title, parent, folded);
  property_sections_.push_back(section);
  return section;
}
void WorkspaceShell::show_properties_tab()
{
  if (!properties_slot_ || !layer_settings_tabs_)
    return;
  if (layer_settings_tabs_->indexOf(properties_slot_) < 0)
    layer_settings_tabs_->addTab(properties_slot_, "Properties");
  properties_slot_->show();
  layer_settings_tabs_->setCurrentWidget(properties_slot_);
}
void WorkspaceShell::hide_properties_tab()
{
  if (!properties_slot_ || !layer_settings_tabs_)
    return;
  const int index = layer_settings_tabs_->indexOf(properties_slot_);
  if (index >= 0)
    layer_settings_tabs_->removeTab(index);
  properties_slot_->hide();
}
void WorkspaceShell::dock_properties()
{
  if (!properties_float_)
    return;
  auto *dialog = properties_float_;
  properties_float_ = nullptr;
  dialog->disconnect(this);
  properties_panel_->setParent(properties_slot_);
  static_cast<QVBoxLayout *>(properties_slot_->layout())->addWidget(properties_panel_);
  show_properties_tab();
  properties_undock_->setToolTip("Undock properties");
  properties_undock_->setAccessibleName("Undock properties");
  dialog->close();
  dialog->deleteLater();
}

QByteArray WorkspaceShell::save_layout() const
{
  return QJsonDocument(
             QJsonObject{
                 {"version", 2},
                 {"workspace", QString::fromLatin1(workspace_splitter_->saveState().toBase64())},
                 {"center", QString::fromLatin1(viewport_shelf_splitter_->saveState().toBase64())},
                 {"inspector", QString::fromLatin1(inspector_splitter_->saveState().toBase64())},
                 {"shelfVisible", !shelf_->isHidden()},
                 {"consoleVisible", !console_->isHidden()},
                 {"tab", layer_settings_tabs_->currentIndex()},
             })
      .toJson(QJsonDocument::Compact);
}

bool WorkspaceShell::restore_layout(const QByteArray &state)
{
  const QJsonDocument document = QJsonDocument::fromJson(state);
  if (!document.isObject()) {
    return false;
  }
  const QJsonObject data = document.object();
  if (data.value("version").toInt() != 2 || !data.value("shelfVisible").isBool() ||
      !data.value("consoleVisible").isBool() || !data.value("tab").isDouble())
  {
    return false;
  }
  const int tab = data.value("tab").toInt(-1);
  if (tab < 0 || tab >= layer_settings_tabs_->count()) {
    return false;
  }
  const QByteArray workspace = QByteArray::fromBase64(
      data.value("workspace").toString().toLatin1());
  const QByteArray center = QByteArray::fromBase64(data.value("center").toString().toLatin1());
  const QByteArray inspector = QByteArray::fromBase64(
      data.value("inspector").toString().toLatin1());
  if (workspace.isEmpty() || center.isEmpty() || inspector.isEmpty()) {
    return false;
  }
  const QByteArray previous_workspace = workspace_splitter_->saveState();
  const QByteArray previous_center = viewport_shelf_splitter_->saveState();
  const QByteArray previous_inspector = inspector_splitter_->saveState();
  const bool previous_shelf_visible = !shelf_->isHidden();
  const bool previous_console_visible = !console_->isHidden();
  set_shelf_visible(data.value("shelfVisible").toBool());
  set_console_visible(data.value("consoleVisible").toBool());
  const bool valid = workspace_splitter_->restoreState(workspace) &&
                     viewport_shelf_splitter_->restoreState(center) &&
                     inspector_splitter_->restoreState(inspector);
  if (!valid) {
    set_shelf_visible(previous_shelf_visible);
    set_console_visible(previous_console_visible);
    workspace_splitter_->restoreState(previous_workspace);
    viewport_shelf_splitter_->restoreState(previous_center);
    inspector_splitter_->restoreState(previous_inspector);
    return false;
  }
  layer_settings_tabs_->setCurrentIndex(tab);
  reference_layout_pending_ = false;
  return true;
}

void WorkspaceShell::showEvent(QShowEvent *event)
{
  QMainWindow::showEvent(event);
  if (reference_layout_pending_) {
    QTimer::singleShot(0, this, [this] {
      if (reference_layout_pending_) {
        // Lay out the first visible geometry without undoing caller choices made before show().
        const bool shelf_visible = !shelf_->isHidden();
        const bool console_visible = !console_->isHidden();
        const int tab = layer_settings_tabs_->currentIndex();
        reset_reference_layout();
        set_shelf_visible(shelf_visible);
        set_console_visible(console_visible);
        layer_settings_tabs_->setCurrentIndex(tab);
      }
    });
  }
}

}  // namespace bts

void bts::WorkspaceShell::set_picker_active(bool active)
{
  const QSignalBlocker blocker(picker_tool_);
  picker_tool_->setChecked(active);
}

void bts::WorkspaceShell::set_polygon_fill(bool active, int mode, double value)
{
  polygon_active_ = active;
  const QSignalBlocker tool_block(polygon_tool_);
  polygon_tool_->setChecked(active);
  for (int which = 0; which < 2; ++which) {
    for (int i = 0; i < 5; ++i) {
      const QSignalBlocker blocker(polygon_modes_[which][i]);
      polygon_modes_[which][i]->setChecked(i == mode);
    }
    const QSignalBlocker a(polygon_values_[which]), b(polygon_sliders_[which]);
    polygon_values_[which]->setValue(value);
    polygon_sliders_[which]->setValue(qRound(value * 1000));
  }
}

void bts::WorkspaceShell::set_asset_catalog(AssetCatalog *catalog)
{
  asset_catalog_ = catalog;
  refresh_alpha_shelf();
}

void bts::WorkspaceShell::set_id_picker_active(bool active)
{
  color_selection_panel_->set_picking(active);
}

void bts::WorkspaceShell::edit_fill_projection(const FillProjection &p)
{
  fill_panel_->edit_projection(p);
}
void bts::WorkspaceShell::set_warp_selection(std::vector<int> ids)
{
  (mask_fill_panel_->isVisible() ? mask_fill_panel_->warp_panel() : fill_panel_->warp_panel())
      ->set_selection(std::move(ids));
}
void bts::WorkspaceShell::cancel_warp_split()
{
  (mask_fill_panel_->isVisible() ? mask_fill_panel_->warp_panel() : fill_panel_->warp_panel())
      ->cancel_split();
}
int bts::WorkspaceShell::fill_resource_channel() const
{
  if (mask_fill_panel_ && mask_fill_panel_->isVisible())
    return mask_fill_panel_->resource_channel();
  return fill_panel_->resource_channel();
}

namespace bts {
void WorkspaceShell::set_baking_tools(bool baking, bool editing, const BrushSettings &brush)
{
  if (!baking)
    return;
  brush_toolbar_->setVisible(editing && !polygon_active_);
  polygon_toolbar_->setVisible(editing && polygon_active_);
  paint_tool_->setEnabled(editing);
  eraser_tool_->setEnabled(editing);
  paint_tool_action_->setEnabled(editing);
  eraser_tool_action_->setEnabled(editing);
  paint_tool_->setChecked(editing && !brush.erase && !polygon_active_);
  eraser_tool_->setChecked(editing && brush.erase && !polygon_active_);
  polygon_tool_->setEnabled(editing);
  polygon_action_->setEnabled(editing);
  for (auto *w : std::initializer_list<QWidget *>{
           smudge_tool_, clone_tool_, projection_tool_, path_tool_, picker_tool_})
    w->setEnabled(false);
  quick_mask_enable_->setEnabled(false);
  for (auto *a : {smudge_action_, clone_action_, projection_action_, path_action_, picker_action_})
    a->setEnabled(false);
  for (auto *w : {toolbar_size_, toolbar_flow_, toolbar_opacity_, toolbar_spacing_})
    w->setEnabled(editing);
  for (auto *w : {toolbar_size_slider_,
                  toolbar_flow_slider_,
                  toolbar_opacity_slider_,
                  toolbar_spacing_slider_})
    w->setEnabled(editing);
  const QSignalBlocker a(toolbar_size_), b(toolbar_flow_), c(toolbar_opacity_),
      d(toolbar_spacing_), e(toolbar_size_slider_), f(toolbar_flow_slider_),
      g(toolbar_opacity_slider_), h(toolbar_spacing_slider_);
  toolbar_size_->setValue(brush.diameter);
  toolbar_flow_->setValue(brush.flow);
  toolbar_opacity_->setValue(brush.stroke_opacity);
  toolbar_spacing_->setValue(brush.spacing);
  toolbar_size_slider_->setValue(qRound(brush.diameter * 10));
  toolbar_flow_slider_->setValue(qRound(brush.flow * 10));
  toolbar_opacity_slider_->setValue(qRound(brush.stroke_opacity * 10));
  toolbar_spacing_slider_->setValue(qRound(brush.spacing * 10));
  for (auto *w : toolbar_pressure_)
    w->setEnabled(editing);
}
}  // namespace bts

void bts::WorkspaceShell::set_fill_color_spaces(const QStringList &spaces)
{
  fill_panel_->set_color_spaces(spaces);
  mask_fill_panel_->set_color_spaces(spaces);
}

void bts::WorkspaceShell::set_texture_load_status(QUuid set,
                                                  QUuid layer,
                                                  int channel,
                                                  const QString &text,
                                                  int completed,
                                                  int total,
                                                  bool loading,
                                                  bool error)
{
  const auto key = set.toString() + layer.toString() + QString::number(channel);
  texture_load_states_[key] = {text, completed, total, loading, error};
  if (texture_load_states_.size() > 4096)
    texture_load_states_.erase(texture_load_states_.begin());
  if (set == shown_texture_set_ && layer == shown_texture_layer_) {
    fill_panel_->set_load_status(channel, text, completed, total, loading, error);
    mask_fill_panel_->set_load_status(channel, text, completed, total, loading, error);
  }
  texture_loading_bar_->setVisible(loading || error);
  texture_loading_notice_->setText(text);
  texture_loading_notice_->setStyleSheet(bts::theme::qss(error ? "color:@Error@" : ""));
  texture_loading_progress_->setUpdatesEnabled(false);
  texture_loading_progress_->setVisible(loading);
  texture_loading_progress_->setRange(0, total);
  texture_loading_progress_->setValue(completed);
  texture_loading_progress_->setUpdatesEnabled(true);
  texture_loading_cancel_->setVisible(loading);
  setProperty("textureLoadPending", loading);
  setProperty("textureLoadError", error ? text : QString{});
}

namespace bts {
#include "AcPanelCaptureHelpers.inc"
void WorkspaceShell::set_mask_clipboard_available(bool ready)
{
  mask_clipboard_available_ = ready;
  auto *copy = findChild<QAction *>("copyMaskContent"),
       *paste = findChild<QAction *>("pasteIntoMask");
  if (paste)
    paste->setEnabled(ready && copy && copy->isEnabled());
}
#include "AcPanelCapture.inc"
}  // namespace bts
