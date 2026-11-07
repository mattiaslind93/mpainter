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
class TextureSetNameDelegate final:public InlineNameDelegate {
 QListWidget *list_;
 public:explicit TextureSetNameDelegate(QListWidget *list):InlineNameDelegate(list),list_(list){}
 QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &index)const override{
  auto size=InlineNameDelegate::sizeHint(option,index);
  if(auto *row=list_->indexWidget(index)){row->ensurePolished();size.setHeight(std::max(size.height(),row->sizeHint().expandedTo(row->minimumSizeHint()).height()+2));}
  return size;
 }
 protected:void initStyleOption(QStyleOptionViewItem *option,const QModelIndex &index)const override{
  InlineNameDelegate::initStyleOption(option,index);
  // Custom rows own a real checkbox; orphan rows use the native indicator.
  if(list_->indexWidget(index))option->features&=~QStyleOptionViewItem::HasCheckIndicator;
 }
};
constexpr int adjustment_editable_role=Qt::UserRole+25;
constexpr int adjustment_checked_role=Qt::UserRole+26;
}

namespace {

constexpr int rail_width = 38;
constexpr int header_height = 34;
constexpr int handle_width = 4;
constexpr int inspector_width = 374;
constexpr int shelf_minimum_height = 180;
constexpr int properties_minimum_height = 130;

// The published documentation. One constant so the menu, and anything else that
// links to it later, cannot drift apart.
constexpr auto documentation_url="https://bluedino.se/mpainter/documentation";
enum class Glyph {
  Brush, Eraser, Projection, Smudge, Fill, Clone, Path, Picker, Folder, Mask, Trash, Settings, History, Material,
  Console, Shelf, Reset, Close, Plus, Undo, Redo, Filter, View,
  // Drawn marks for the toolbar buttons that used to carry a bare text
  // character. A glyph at icon size reads at a glance; "⋈" and "◩" did not.
  Symmetry, SymmetryManipulator, HideExcluded, LazyMouse, More,
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
    p.setPen(QPen(QColor(mode == QIcon::Disabled ? bts::theme::Muted : bts::theme::Text), 1.2,
                 Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (glyph) {
      case Glyph::Symmetry:{const QColor tint=p.pen().color();p.drawLine(QPointF(8,1.5),QPointF(8,14.5));p.setBrush(tint);p.drawPolygon(QPolygonF({{6,4},{6,12},{2,8}}));p.setBrush(Qt::NoBrush);p.drawPolygon(QPolygonF({{10,4},{10,12},{14,8}}));break;}
      case Glyph::SymmetryManipulator:
        p.drawLine(QPointF(8,1.5),QPointF(8,14.5));
        p.drawLine(QPointF(2,8),QPointF(14,8));
        p.drawLine(QPointF(2,8),QPointF(5,5));p.drawLine(QPointF(2,8),QPointF(5,11));
        p.drawLine(QPointF(14,8),QPointF(11,5));p.drawLine(QPointF(14,8),QPointF(11,11));
        break;
      case Glyph::HideExcluded:{const QColor tint=p.pen().color();p.drawRect(QRectF(2,2,12,12));p.fillRect(QRectF(2.7,2.7,5.3,10.6),tint);break;}
      case Glyph::LazyMouse:{const QColor tint=p.pen().color();p.drawEllipse(QRectF(2.5,2.5,11,11));p.drawLine(QPointF(5.2,10.8),QPointF(9,7));p.setBrush(tint);p.drawEllipse(QPointF(10.4,5.6),2,2);p.setBrush(Qt::NoBrush);break;}
      case Glyph::More:
        p.drawLine(QPointF(4.5,6.5),QPointF(8,10));
        p.drawLine(QPointF(8,10),QPointF(11.5,6.5));
        break;
      case Glyph::Material:
        p.drawEllipse(QRectF(2,2,12,12));p.drawArc(QRectF(2,5,12,6),0,180*16);p.drawArc(QRectF(5,2,6,12),90*16,180*16);break;
      case Glyph::Path:{QPainterPath curve;curve.moveTo(3,13);curve.cubicTo(5,1,10,15,13,3);p.drawPath(curve);p.drawEllipse(QPointF(3,13),1.5,1.5);p.drawEllipse(QPointF(13,3),1.5,1.5);break;}
      case Glyph::Smudge:{QPainterPath hand;hand.moveTo(3,14);hand.lineTo(2,9);hand.cubicTo(2,7,4,7,5,9);hand.lineTo(5,4);hand.cubicTo(5,2,7,2,7,4);hand.lineTo(7,8);hand.lineTo(8,5);hand.cubicTo(9,4,10,5,10,7);hand.lineTo(10,8);hand.cubicTo(13,7,14,9,13,12);hand.lineTo(11,15);p.drawPath(hand);break;}
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
  explicit TangentBumpBar(QWidget *parent) : QWidget(parent) {
    setObjectName("fillTangentBump");
    setAccessibleName("Normal");
    setToolTip("Normal. Tangent-space normal map, not grayscale Bump.");
    setMinimumHeight(22);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  void set_normal(const std::array<float, 3> &normal) { normal_ = normal; update(); }
  std::function<void(int, double)> edited;
 protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const auto channel = [](float value) { return std::clamp(int(std::lround((value * .5f + .5f) * 255.f)), 0, 255); };
    painter.fillRect(rect(), QColor(channel(normal_[0]), channel(normal_[1]), channel(normal_[2])));
    painter.setPen(QColor(bts::theme::Hover));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
  }
  void mouseReleaseEvent(QMouseEvent *event) override {
    const QColor current = QColor::fromRgbF(std::clamp(normal_[0] * .5f + .5f, 0.f, 1.f), std::clamp(normal_[1] * .5f + .5f, 0.f, 1.f), std::clamp(normal_[2] * .5f + .5f, 0.f, 1.f));
    const QColor picked = QColorDialog::getColor(current, this, "Normal");
    if (!picked.isValid() || !edited) { QWidget::mouseReleaseEvent(event); return; }
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
void align_property_rows(const std::vector<QWidget *> &groups,int trailing_width)
{
  std::vector<QLabel *> labels;int widest=0;
  for(auto *group:groups){
    if(!group)continue;
    for(auto *line:group->findChildren<QWidget *>(QString(),Qt::FindDirectChildrenOnly)){
      auto *layout=qobject_cast<QHBoxLayout *>(line->layout());
      if(!layout||layout->count()<2)continue;
      auto *label=qobject_cast<QLabel *>(layout->itemAt(0)->widget());
      if(!label||label->text().isEmpty())continue;
      labels.push_back(label);
      widest=std::max(widest,label->sizeHint().width());
      if(layout->count()==2&&trailing_width>0)layout->addSpacing(trailing_width);
      // Without this the column is stretched to the scroll viewport and the
      // spare height is shared out among the rows, so unfolding a section
      // changed the height and the spacing of every row on the panel.
      line->setSizePolicy(line->sizePolicy().horizontalPolicy(),QSizePolicy::Maximum);
    }
  }
  for(auto *label:labels)label->setFixedWidth(widest);
}
void align_property_rows(QWidget *group,int trailing_width){align_property_rows(std::vector<QWidget *>{group},trailing_width);}

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
  explicit ShelfCategoryList(QWidget *parent = nullptr) : QListWidget(parent) {
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  void update_grid() {
    const int avail = std::max(272, (viewport() ? viewport()->width() : width()) - 4);
    const int cols = 4;
    const int cell_w = std::max(66, avail / cols);
    const int cell_h = 24;
    if (gridSize() != QSize(cell_w, cell_h)) setGridSize(QSize(cell_w, cell_h));
    const int rows = (std::max(1, count()) + cols - 1) / cols;
    const int target_h = rows * cell_h + 6;
    if (height() != target_h) setFixedHeight(target_h);
  }
 protected:
  void resizeEvent(QResizeEvent *event) override {
    QListWidget::resizeEvent(event);
    update_grid();
  }
  void showEvent(QShowEvent *event) override {
    QListWidget::showEvent(event);
    update_grid();
  }
};

class ShelfCategoryDelegate final : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override {
    if (const auto *list = qobject_cast<const QListWidget *>(option.widget)) {
      const QSize g = list->gridSize();
      if (g.isValid()) return QSize(std::max(64, g.width() - 2), std::max(22, g.height() - 2));
    }
    return QSize(86, 22);
  }
  void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const QRectF cell = QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5);
    const QColor bg = selected ? QColor(bts::theme::Selection) : (hovered ? QColor(bts::theme::Raised) : QColor(bts::theme::Surface));
    const QColor border = selected ? QColor(bts::theme::Accent) : (hovered ? QColor(bts::theme::Border) : QColor(bts::theme::Separator));
    painter->setPen(QPen(border, 1.0));
    painter->setBrush(bg);
    painter->drawRoundedRect(cell, bts::theme::RadiusPx, bts::theme::RadiusPx);

    const QString text = index.data(Qt::DisplayRole).toString();
    const QColor fg = selected ? QColor(bts::theme::Heading) : (hovered ? QColor(bts::theme::Heading) : QColor(bts::theme::Text));
    const QColor ic = selected ? QColor(bts::theme::Heading) : (hovered ? QColor(bts::theme::Heading) : QColor(bts::theme::Muted));

    const qreal ix = cell.left() + 6.0;
    const qreal iy = cell.center().y() - 5.0;
    const QRectF ir(ix, iy, 10.0, 10.0);
    painter->setPen(QPen(ic, 1.15, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    if (text == "All") {
      for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
          painter->drawRect(QRectF(ix + c * 5.5, iy + r * 5.5, 4.0, 4.0));
    } else if (text == "Project") {
      painter->drawPolygon(QPolygonF({{ix, iy + 2.5}, {ix + 3.5, iy + 2.5}, {ix + 4.8, iy + 4.0}, {ix + 10.0, iy + 4.0}, {ix + 10.0, iy + 9.0}, {ix, iy + 9.0}}));
    } else if (text == "Materials" || text == "Material presets") {
      painter->drawEllipse(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      if (text == "Material presets") {
        painter->setBrush(ic);
        painter->drawEllipse(QPointF(ix + 8.2, iy + 2.0), 1.6, 1.6);
        painter->setBrush(Qt::NoBrush);
      } else {
        painter->drawArc(ir.adjusted(2.2, 2.2, -2.2, -2.2), 40 * 16, 110 * 16);
      }
    } else if (text == "Mask generators") {
      painter->drawRect(ir.adjusted(0.5, 1.0, -0.5, -1.0));
      painter->setBrush(ic);
      painter->drawEllipse(ir.center(), 2.2, 2.2);
      painter->setBrush(Qt::NoBrush);
    } else if (text == "Alphas" || text == "Shape") {
      painter->setBrush(ic);
      painter->drawEllipse(ir.adjusted(1.5, 1.5, -1.5, -1.5));
      painter->setBrush(Qt::NoBrush);
    } else if (text == "Grunges" || text == "Particles") {
      painter->setBrush(ic);
      painter->drawEllipse(QPointF(ix + 2.5, iy + 3.0), 1.1, 1.1);
      painter->drawEllipse(QPointF(ix + 7.2, iy + 2.5), 0.9, 0.9);
      painter->drawEllipse(QPointF(ix + 5.0, iy + 6.5), 1.3, 1.3);
      painter->drawEllipse(QPointF(ix + 8.0, iy + 7.5), 1.0, 1.0);
      painter->setBrush(Qt::NoBrush);
    } else if (text == "Procedurals") {
      QPainterPath wave;
      wave.moveTo(ix, iy + 5.0);
      wave.cubicTo(ix + 3.0, iy, ix + 7.0, iy + 10.0, ix + 10.0, iy + 5.0);
      painter->drawPath(wave);
    } else if (text == "Textures" || text == "Flattened layers") {
      painter->drawRect(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      painter->drawLine(QPointF(ix + 0.5, iy + 5.0), QPointF(ix + 9.5, iy + 5.0));
      painter->drawLine(QPointF(ix + 5.0, iy + 0.5), QPointF(ix + 5.0, iy + 9.5));
    } else if (text == "Filters" || text == "Color profiles") {
      painter->drawLine(QPointF(ix + 1.0, iy + 2.5), QPointF(ix + 9.0, iy + 2.5));
      painter->drawLine(QPointF(ix + 2.5, iy + 5.0), QPointF(ix + 7.5, iy + 5.0));
      painter->drawLine(QPointF(ix + 4.0, iy + 7.5), QPointF(ix + 6.0, iy + 7.5));
    } else if (text == "Brushes" || text == "Tools") {
      painter->drawLine(QPointF(ix + 2.0, iy + 8.5), QPointF(ix + 8.5, iy + 2.0));
      painter->drawEllipse(QPointF(ix + 2.2, iy + 8.0), 1.2, 1.2);
    } else if (text == "Environments") {
      painter->drawEllipse(ir.adjusted(0.5, 0.5, -0.5, -0.5));
      painter->drawLine(QPointF(ix + 0.5, iy + 5.0), QPointF(ix + 9.5, iy + 5.0));
    } else {
      painter->drawPolygon(QPolygonF({{ix + 5.0, iy + 0.5}, {ix + 9.5, iy + 3.0}, {ix + 9.5, iy + 7.5}, {ix + 5.0, iy + 9.5}, {ix + 0.5, iy + 7.5}, {ix + 0.5, iy + 3.0}}));
    }

    QFont f = option.font;
    f.setPixelSize(10);
    f.setWeight(selected ? QFont::DemiBold : QFont::Normal);
    painter->setFont(f);
    painter->setPen(fg);
    const QRectF text_rect(ix + 13.0, cell.top(), std::max(10.0, cell.right() - (ix + 15.0)), cell.height());
    const QFontMetrics fm(f);
    painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(text, Qt::ElideRight, int(text_rect.width())));
    painter->restore();
  }
};

class EmptyList final : public QListWidget {
 public:
  EmptyList(const QString &empty_text, QWidget *parent) : QListWidget(parent), empty_text_(empty_text)
  {
    setFrameShape(QFrame::NoFrame);
  }

  void set_empty_text(const QString &text)
  {
    empty_text_ = text;
    viewport()->update();
  }

 protected:
  QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override {auto *data=new QMimeData;if(objectName()=="shelfAssets"&&!items.isEmpty())data->setData("application/x-bts-asset",items.front()->data(Qt::UserRole).toString().toUtf8());return data;}
  Qt::DropActions supportedDropActions() const override {return Qt::CopyAction;}
  void mouseDoubleClickEvent(QMouseEvent *event)override{const auto index=indexAt(event->position().toPoint());if(objectName()=="shelfAssets"&&index.isValid()&&(index.flags()&Qt::ItemIsEditable)&&event->position().y()>visualRect(index).top()+iconSize().height()){edit(index);event->accept();return;}QListWidget::mouseDoubleClickEvent(event);}
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
  install_panel_scrolling();
  if (!viewport) {
    qFatal("WorkspaceShell requires a viewport widget");
  }
  setObjectName("workspaceShell");
  setWindowTitle("mPainter");
  setMinimumSize(1000, 680);
  resize(1440, 900);
  QFont body_font = font();
  body_font.setPixelSize(11);
  setFont(body_font);

  auto *root = new QWidget(this);
  root->setObjectName("workspaceRoot");
  setCentralWidget(root);
  auto *root_layout = row(root, 0);
  root_layout->setSpacing(0);

  shelf_action_ = new QAction("Shelf", this);
  shelf_action_->setIcon(icon(Glyph::Shelf));
  shelf_action_->setObjectName("toggleShelf");
  shelf_action_->setCheckable(true);
  shelf_action_->setChecked(true);
  console_action_ = new QAction("Console", this);
  console_action_->setIcon(icon(Glyph::Console));
  console_action_->setObjectName("toggleConsole");
  console_action_->setCheckable(true);
  console_action_->setChecked(true);
  auto *reset_action = new QAction("Reset UI", this);
  reset_action->setIcon(icon(Glyph::Reset));
  reset_action->setObjectName("resetReferenceLayout");
  addActions({shelf_action_, console_action_, reset_action});

  auto *viewport_menu=menuBar()->addMenu("&Viewport");viewport_menu->setObjectName("viewportMenu");
  quick_mask_enable_=viewport_menu->addAction("Enable quick mask");quick_mask_enable_->setObjectName("enableQuickMask");quick_mask_enable_->setCheckable(true);
  quick_mask_edit_action_=viewport_menu->addAction("Edit quick mask");quick_mask_edit_action_->setObjectName("editQuickMask");quick_mask_edit_action_->setCheckable(true);
  quick_mask_invert_=viewport_menu->addAction("Invert quick mask");quick_mask_invert_->setObjectName("invertQuickMask");quick_mask_invert_->setShortcut(QKeySequence(Qt::Key_I));
  quick_mask_clear_=viewport_menu->addAction("Clear quick mask");quick_mask_clear_->setObjectName("clearQuickMask");quick_mask_clear_->setShortcut(QKeySequence(Qt::Key_Y));
  for(auto *action:{quick_mask_enable_,quick_mask_edit_action_,quick_mask_invert_,quick_mask_clear_})addAction(action);
  connect(quick_mask_enable_,&QAction::triggered,this,[this]{if(callbacks_.quick_mask_action)callbacks_.quick_mask_action(0);});
  connect(quick_mask_edit_action_,&QAction::triggered,this,[this]{if(callbacks_.quick_mask_action)callbacks_.quick_mask_action(1);});
  connect(quick_mask_invert_,&QAction::triggered,this,[this]{if(callbacks_.quick_mask_action)callbacks_.quick_mask_action(2);});
  connect(quick_mask_clear_,&QAction::triggered,this,[this]{if(callbacks_.quick_mask_action)callbacks_.quick_mask_action(3);});
  set_quick_mask_state(false,false);

  auto *window_menu = menuBar()->addMenu("&Window");
  auto *views_menu = window_menu->addMenu("Views");
  views_menu->addActions({shelf_action_, console_action_});
  properties_action_=new QAction("Properties", this);
  properties_action_->setObjectName("toggleProperties");
  properties_action_->setCheckable(true);
  properties_action_->setChecked(true);
  views_menu->addAction(properties_action_);
  window_menu->addSeparator();
  window_menu->addAction(reset_action);

  auto *help_menu = menuBar()->addMenu("&Help");
  help_menu->setObjectName("helpMenu");
  auto *documentation = help_menu->addAction("Documentation");
  documentation->setObjectName("openDocumentation");
  connect(documentation,&QAction::triggered,this,[]{QDesktopServices::openUrl(QUrl(documentation_url));});
  auto *report_bug = help_menu->addAction("Report Bug…");
  report_bug->setObjectName("reportBug");
  connect(report_bug,&QAction::triggered,this,[this]{BugReportDialog(this).exec();});

  auto *workspace_menu = new QMenu(this);
  workspace_menu->addActions({shelf_action_, console_action_, properties_action_});
  workspace_menu->addSeparator();
  workspace_menu->addAction(reset_action);

  auto *left = new QWidget(root);
  left->setObjectName("toolRail");
  left->setFixedWidth(rail_width);
  auto *left_layout = column(left);
  left_layout->setContentsMargins(bts::theme::Space,bts::theme::Space,bts::theme::Space,bts::theme::Space);
  left_layout->setSpacing(bts::theme::Space);
  for (const auto &[glyph, name] : {
           std::pair{Glyph::Brush, "Paint"}, {Glyph::Eraser, "Eraser"},
           {Glyph::Projection, "Projection"}, {Glyph::Fill, "Polygon fill"}, {Glyph::Smudge,"Smudge"},
           {Glyph::Clone, "Clone"}, {Glyph::Path,"Surface paths"}, {Glyph::Picker, "Material picker"}}) {
    auto *button=tool(left,glyph,QString::fromLatin1(name));left_layout->addWidget(button);
    if(glyph==Glyph::Path){path_tool_=button;button->setObjectName("surfacePathTool");button->setCheckable(true);path_action_=new QAction("Surface paths",this);path_action_->setObjectName("selectSurfacePath");addAction(path_action_);connect(path_action_,&QAction::triggered,this,[this]{if(callbacks_.path_selected)callbacks_.path_selected();});connect(button,&QToolButton::clicked,path_action_,&QAction::trigger);}
    if(glyph==Glyph::Clone){
      clone_tool_=button;button->setObjectName("cloneTool");button->setCheckable(true);button->setPopupMode(QToolButton::MenuButtonPopup);
      clone_action_=new QAction("Clone",this);clone_action_->setObjectName("selectCloneTool");clone_action_->setShortcut(QKeySequence(Qt::Key_6));addAction(clone_action_);
      connect(clone_action_,&QAction::triggered,this,[this]{if(callbacks_.clone_selected)callbacks_.clone_selected();});connect(button,&QToolButton::clicked,clone_action_,&QAction::trigger);
      auto *menu=new QMenu(button);button->setMenu(menu);
      clone_relative_=menu->addAction("Relative source");clone_relative_->setObjectName("cloneRelative");clone_relative_->setCheckable(true);
      clone_absolute_=menu->addAction("Absolute source");clone_absolute_->setObjectName("cloneAbsolute");clone_absolute_->setCheckable(true);
      for(auto *action:{clone_relative_,clone_absolute_})connect(action,&QAction::triggered,this,[this,action]{if(callbacks_.clone_selected)callbacks_.clone_selected();if(callbacks_.clone_mode_changed)callbacks_.clone_mode_changed(action==clone_absolute_);});
    }
    if(glyph==Glyph::Smudge){smudge_tool_=button;button->setObjectName("smudgeTool");button->setCheckable(true);smudge_action_=new QAction("Smudge",this);smudge_action_->setObjectName("selectSmudgeTool");smudge_action_->setShortcut(QKeySequence(Qt::Key_5));addAction(smudge_action_);connect(smudge_action_,&QAction::triggered,this,[this]{if(callbacks_.smudge_selected)callbacks_.smudge_selected();});connect(button,&QToolButton::clicked,smudge_action_,&QAction::trigger);}
    if(glyph==Glyph::Projection){projection_tool_=button;button->setObjectName("projectionTool");button->setCheckable(true);projection_action_=new QAction("Projection",this);projection_action_->setObjectName("selectProjectionTool");projection_action_->setShortcut(QKeySequence(Qt::Key_3));addAction(projection_action_);connect(projection_action_,&QAction::triggered,this,[this]{if(callbacks_.projection_selected)callbacks_.projection_selected();});connect(button,&QToolButton::clicked,projection_action_,&QAction::trigger);}
    if(glyph==Glyph::Fill){polygon_tool_=button;button->setObjectName("polygonFillTool");button->setCheckable(true);button->setToolTip("Polygon fill (4) — Auto: 3D object/element, 2D UV island; Ctrl uses secondary ink");polygon_action_=new QAction("Polygon fill",this);polygon_action_->setObjectName("selectPolygonFill");polygon_action_->setShortcut(QKeySequence(Qt::Key_4));addAction(polygon_action_);connect(polygon_action_,&QAction::triggered,this,[this]{if(callbacks_.polygon_tool_changed)callbacks_.polygon_tool_changed(true);});connect(button,&QToolButton::clicked,polygon_action_,&QAction::trigger);}
    if(glyph==Glyph::Picker){picker_tool_=button;button->setObjectName("materialPickerTool");button->setCheckable(true);button->setToolTip("Material picker (P)");picker_action_=new QAction("Material picker",this);picker_action_->setObjectName("selectMaterialPicker");picker_action_->setShortcut(QKeySequence(Qt::Key_P));addAction(picker_action_);connect(picker_action_,&QAction::triggered,button,&QToolButton::click);connect(button,&QToolButton::toggled,this,[this](bool value){if(callbacks_.picker_changed)callbacks_.picker_changed(value);});}
    if (glyph==Glyph::Brush||glyph==Glyph::Eraser) {
      const bool erase=glyph==Glyph::Eraser;button->setCheckable(true);button->setObjectName(erase?"eraserTool":"paintTool");
      auto *action=new QAction(QString::fromLatin1(name),this);action->setObjectName(erase?"selectEraserTool":"selectPaintTool");action->setShortcut(QKeySequence(erase?Qt::Key_2:Qt::Key_1));addAction(action);
      if(erase){eraser_tool_=button;eraser_tool_action_=action;}else{paint_tool_=button;paint_tool_action_=action;}
      connect(action,&QAction::triggered,this,[this,erase]{if(callbacks_.tool_selected)callbacks_.tool_selected(erase);});
      connect(button,&QToolButton::clicked,action,&QAction::trigger);
    }
  }
  left_layout->addStretch();
  auto *workspace_button = tool(left, Glyph::Settings, "Workspace layout", true);
  workspace_button->setObjectName("workspaceMenu");
  workspace_button->setMenu(workspace_menu);
  workspace_button->setPopupMode(QToolButton::InstantPopup);
  left_layout->addWidget(workspace_button);
  root_layout->addWidget(left);

  workspace_splitter_ = splitter(Qt::Horizontal, root, "workspaceSplitter");
  root_layout->addWidget(workspace_splitter_, 1);
  auto *center = new QWidget(workspace_splitter_);
  workspace_splitter_->addWidget(center);
  center->setMinimumWidth(640);
  auto *center_layout = column(center);
  auto *toolbar = new QWidget(center);
  toolbar->setObjectName("contextToolbar");
  toolbar->setFixedHeight(36);
  auto *toolbar_layout = row(toolbar);
  fill_toolbar_=new QWidget(toolbar);auto *fill_toolbar_layout=row(fill_toolbar_,0);fill_toolbar_->hide();toolbar_layout->addWidget(fill_toolbar_);
  fill_manipulator_=new QToolButton(fill_toolbar_);fill_manipulator_->setObjectName("fillToolbarManipulator");fill_manipulator_->setText("UV frame");fill_manipulator_->setCheckable(true);fill_toolbar_layout->addWidget(fill_manipulator_);connect(fill_manipulator_,&QToolButton::toggled,this,[this](bool v){auto p=displayed_fill_;p.manipulator=v;if(callbacks_.fill_projection_changed)callbacks_.fill_projection_changed(p);});
  fill_spatial_toolbar_=new QWidget(fill_toolbar_);auto *gizmo_layout=row(fill_spatial_toolbar_,0);fill_toolbar_layout->addWidget(fill_spatial_toolbar_);fill_gizmo_size_=new bts::NumericSpinBox(fill_spatial_toolbar_);fill_gizmo_size_->setObjectName("fillManipulatorSize");fill_gizmo_size_->setRange(.01,10);fill_gizmo_size_->setDecimals(2);fill_gizmo_size_->setValue(.4);fill_gizmo_size_->setKeyboardTracking(false);fill_gizmo_size_->setToolTip("Projection manipulator size");gizmo_layout->addWidget(fill_gizmo_size_);connect(fill_gizmo_size_,&QDoubleSpinBox::valueChanged,this,[this](double v){auto p=displayed_fill_;p.gizmo_size=v;if(callbacks_.fill_projection_changed)callbacks_.fill_projection_changed(p);});
  for(int mode=0;mode<3;++mode){auto *button=new QToolButton(fill_spatial_toolbar_);button->setText(QStringList{"Translate","Rotate","Scale"}[mode]);button->setCheckable(true);button->setObjectName(QString("fillGizmoMode%1").arg(mode));fill_gizmos_[mode]=button;gizmo_layout->addWidget(button);connect(button,&QToolButton::clicked,this,[this,mode]{auto p=displayed_fill_;p.gizmo=mode;if(callbacks_.fill_projection_changed)callbacks_.fill_projection_changed(p);});auto *action=new QAction(button->text(),this);action->setShortcut(QKeySequence(mode==0?Qt::Key_W:mode==1?Qt::Key_E:Qt::Key_R));action->setEnabled(false);fill_gizmo_actions_[mode]=action;addAction(action);connect(action,&QAction::triggered,button,&QToolButton::click);}
  gizmo_layout->addWidget(new QLabel("Mapping",fill_spatial_toolbar_));
  warp_geometry_toggle_=new QToolButton(fill_toolbar_);warp_geometry_toggle_->setObjectName("warpToGeometry");warp_geometry_toggle_->setText("Warp to geometry");warp_geometry_toggle_->setCheckable(true);warp_geometry_toggle_->setToolTip("Automatically fit the Warp grid to the active Texture Set surface; turning off freezes the current setup");fill_toolbar_layout->addWidget(warp_geometry_toggle_);warp_geometry_toggle_->hide();connect(warp_geometry_toggle_,&QToolButton::toggled,this,[this](bool enabled){if(displayed_fill_.mode!=5)return;edit_fill_projection(toggle_warp_geometry(displayed_fill_,enabled));});
  const auto make_polygon_controls=[&](int which,QWidget *parent){
    auto *panel=new QWidget(parent);QBoxLayout *layout=which==0?static_cast<QBoxLayout *>(row(panel)):column(panel);auto *buttons=new QWidget(panel);auto *buttons_layout=row(buttons,0);
    for(int mode=0;mode<5;++mode){auto *button=new QToolButton(buttons);button->setText(QStringList{"△","□","◇","▦","A"}[mode]);button->setToolTip(QStringList{"Triangle fill","Polygon fill","Mesh / connected element fill","UV island fill","Auto: object/connected element in 3D; UV island in 2D"}[mode]);button->setObjectName(QString("polygonMode%1_%2").arg(which).arg(mode));button->setCheckable(true);button->setChecked(mode==4);polygon_modes_[which][mode]=button;buttons_layout->addWidget(button);connect(button,&QToolButton::clicked,this,[this,which,mode]{if(callbacks_.polygon_fill_changed)callbacks_.polygon_fill_changed(mode,polygon_values_[which]->value());});}
    layout->addWidget(buttons);auto *value_line=new QWidget(panel);auto *value_layout=row(value_line,0);value_layout->addWidget(new QLabel("Grayscale",value_line));auto *number=new bts::NumericSpinBox(value_line);number->setRange(0,1);number->setDecimals(3);number->setSingleStep(.1);number->setValue(1);number->setKeyboardTracking(false);number->setObjectName(QString("polygonGrayscale%1").arg(which));polygon_values_[which]=number;value_layout->addWidget(number);auto *invert=new QToolButton(value_line);invert->setText("⇄");invert->setToolTip("Invert grayscale (X)");value_layout->addWidget(invert);connect(invert,&QToolButton::clicked,this,[number]{number->setValue(1-number->value());});layout->addWidget(value_line);
    auto *slider=new QSlider(Qt::Horizontal,panel);slider->setRange(0,1000);slider->setValue(1000);polygon_sliders_[which]=slider;layout->addWidget(slider);connect(slider,&QSlider::valueChanged,this,[number](int v){number->setValue(v/1000.);});connect(number,&QDoubleSpinBox::valueChanged,this,[this,which](double v){int mode=4;for(int i=0;i<5;++i)if(polygon_modes_[which][i]->isChecked())mode=i;if(callbacks_.polygon_fill_changed)callbacks_.polygon_fill_changed(mode,v);});panel->hide();return panel;
  };
  polygon_toolbar_=make_polygon_controls(0,toolbar);toolbar_layout->addWidget(polygon_toolbar_);
  brush_toolbar_=new QWidget(toolbar);brush_toolbar_->hide();auto *brush_toolbar_layout=row(brush_toolbar_,0);toolbar_layout->addWidget(brush_toolbar_);
  for (const char *name : {"Size", "Flow", "Stroke opacity", "Spacing"}) {
    if (QString::fromLatin1(name)=="Size") {
      auto *group=new QWidget(toolbar);auto *layout=column(group);group->setFixedWidth(98);
      auto *line=new QWidget(group);auto *line_layout=row(line,0);line_layout->addWidget(new QLabel("Size",line));
      toolbar_size_=new bts::NumericSpinBox(line);toolbar_size_->setRange(.1,100);toolbar_size_->setDecimals(1);
      toolbar_size_->setValue(10);toolbar_size_->setKeyboardTracking(false);toolbar_size_->setObjectName("toolbarBrushSize");
      toolbar_size_->setFixedHeight(19);line_layout->addWidget(toolbar_size_);layout->addWidget(line);
      group->setFixedWidth(108);
      toolbar_pressure_[0]=tool(line,Glyph::Brush,"Use pen pressure for Size",true);
      toolbar_pressure_[0]->setFixedSize(20,19);toolbar_pressure_[0]->setCheckable(true);
      toolbar_pressure_[0]->setObjectName("toolbarPressureSize");line_layout->addWidget(toolbar_pressure_[0]);
      connect(toolbar_pressure_[0],&QToolButton::toggled,this,[this](bool value){if(callbacks_.brush_pressure_changed)callbacks_.brush_pressure_changed(0,value);});
      toolbar_size_slider_=new QSlider(Qt::Horizontal,group);toolbar_size_slider_->setRange(1,1000);toolbar_size_slider_->setValue(100);
      layout->addWidget(toolbar_size_slider_);brush_toolbar_layout->addWidget(group);
      connect(toolbar_size_slider_,&QSlider::valueChanged,this,[this](int value){toolbar_size_->setValue(value/10.0);});
      connect(toolbar_size_,&QDoubleSpinBox::valueChanged,this,[this](double value){
        const QSignalBlocker blocker(toolbar_size_slider_);toolbar_size_slider_->setValue(qRound(value*10));
        if(callbacks_.brush_size_changed)callbacks_.brush_size_changed(value);
      });
      continue;
    }
    if(QString::fromLatin1(name)=="Flow") {
      auto *group=new QWidget(toolbar);auto *layout=column(group);group->setFixedWidth(98);
      auto *line=new QWidget(group);auto *line_layout=row(line,0);line_layout->addWidget(new QLabel("Flow",line));
      toolbar_flow_=new bts::NumericSpinBox(line);toolbar_flow_->setRange(0,100);toolbar_flow_->setDecimals(1);
      toolbar_flow_->setValue(100);toolbar_flow_->setKeyboardTracking(false);toolbar_flow_->setObjectName("toolbarBrushFlow");
      toolbar_flow_->setFixedHeight(19);line_layout->addWidget(toolbar_flow_);layout->addWidget(line);
      group->setFixedWidth(108);
      toolbar_pressure_[1]=tool(line,Glyph::Brush,"Use pen pressure for Flow",true);
      toolbar_pressure_[1]->setFixedSize(20,19);toolbar_pressure_[1]->setCheckable(true);
      toolbar_pressure_[1]->setObjectName("toolbarPressureFlow");line_layout->addWidget(toolbar_pressure_[1]);
      connect(toolbar_pressure_[1],&QToolButton::toggled,this,[this](bool value){if(callbacks_.brush_pressure_changed)callbacks_.brush_pressure_changed(1,value);});
      toolbar_flow_slider_=new QSlider(Qt::Horizontal,group);toolbar_flow_slider_->setRange(0,1000);toolbar_flow_slider_->setValue(1000);
      layout->addWidget(toolbar_flow_slider_);brush_toolbar_layout->addWidget(group);
      connect(toolbar_flow_slider_,&QSlider::valueChanged,this,[this](int value){toolbar_flow_->setValue(value/10.0);});
      connect(toolbar_flow_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_flow_changed)callbacks_.brush_flow_changed(value);});
      continue;
    }
    if(QString::fromLatin1(name)=="Stroke opacity") {
      auto *group=new QWidget(toolbar);auto *layout=column(group);group->setFixedWidth(148);
      auto *line=new QWidget(group);auto *line_layout=row(line,0);line_layout->addWidget(new QLabel("Stroke opacity",line));
      toolbar_opacity_=new bts::NumericSpinBox(line);toolbar_opacity_->setRange(0,100);toolbar_opacity_->setDecimals(1);
      toolbar_opacity_->setValue(100);toolbar_opacity_->setKeyboardTracking(false);toolbar_opacity_->setObjectName("toolbarStrokeOpacity");
      toolbar_opacity_->setFixedHeight(19);line_layout->addWidget(toolbar_opacity_);layout->addWidget(line);
      toolbar_opacity_slider_=new QSlider(Qt::Horizontal,group);toolbar_opacity_slider_->setRange(0,1000);toolbar_opacity_slider_->setValue(1000);
      layout->addWidget(toolbar_opacity_slider_);brush_toolbar_layout->addWidget(group);
      connect(toolbar_opacity_slider_,&QSlider::valueChanged,this,[this](int value){toolbar_opacity_->setValue(value/10.0);});
      connect(toolbar_opacity_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_opacity_changed)callbacks_.brush_opacity_changed(value);});
      continue;
    }
    if(QString::fromLatin1(name)=="Spacing") {
      auto *group=new QWidget(toolbar);auto *layout=column(group);group->setFixedWidth(110);
      auto *line=new QWidget(group);auto *line_layout=row(line,0);line_layout->addWidget(new QLabel("Spacing",line));
      toolbar_spacing_=new bts::NumericSpinBox(line);toolbar_spacing_->setRange(1,500);toolbar_spacing_->setDecimals(1);
      toolbar_spacing_->setValue(20);toolbar_spacing_->setKeyboardTracking(false);toolbar_spacing_->setObjectName("toolbarBrushSpacing");
      toolbar_spacing_->setFixedHeight(19);line_layout->addWidget(toolbar_spacing_);layout->addWidget(line);
      toolbar_spacing_slider_=new QSlider(Qt::Horizontal,group);toolbar_spacing_slider_->setRange(10,5000);toolbar_spacing_slider_->setValue(200);
      layout->addWidget(toolbar_spacing_slider_);brush_toolbar_layout->addWidget(group);
      connect(toolbar_spacing_slider_,&QSlider::valueChanged,this,[this](int value){toolbar_spacing_->setValue(value/10.0);});
      connect(toolbar_spacing_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_spacing_changed)callbacks_.brush_spacing_changed(value);});
      continue;
    }
  }
  lazy_toggle_=tool(toolbar,Glyph::LazyMouse,"Lazy mouse",true);lazy_toggle_->setCheckable(true);lazy_toggle_->setObjectName("lazyMouseToggle");lazy_toggle_->setToolTip("Lazy mouse (D)");brush_toolbar_layout->addWidget(lazy_toggle_);
  lazy_controls_=new QWidget(toolbar);auto *lazy_layout=column(lazy_controls_);lazy_controls_->setFixedWidth(108);
  auto *lazy_line=new QWidget(lazy_controls_);auto *lazy_row=row(lazy_line,0);lazy_row->addWidget(new QLabel("Distance",lazy_line));
  lazy_distance_=new bts::NumericSpinBox(lazy_line);lazy_distance_->setObjectName("lazyMouseDistance");lazy_distance_->setRange(.1,100);lazy_distance_->setDecimals(1);lazy_distance_->setValue(8);lazy_distance_->setFixedHeight(19);lazy_distance_->setKeyboardTracking(false);lazy_distance_->setToolTip("Radius as a percentage of view height");lazy_row->addWidget(lazy_distance_);lazy_layout->addWidget(lazy_line);
  lazy_slider_=new QSlider(Qt::Horizontal,lazy_controls_);lazy_slider_->setRange(1,1000);lazy_slider_->setValue(80);lazy_layout->addWidget(lazy_slider_);brush_toolbar_layout->addWidget(lazy_controls_);lazy_controls_->setEnabled(false);
  connect(lazy_slider_,&QSlider::valueChanged,this,[this](int v){lazy_distance_->setValue(v/10.);});
  connect(lazy_distance_,&QDoubleSpinBox::valueChanged,this,[this](double v){if(callbacks_.lazy_mouse_changed)callbacks_.lazy_mouse_changed(lazy_toggle_->isChecked(),v);});
  connect(lazy_toggle_,&QToolButton::toggled,this,[this](bool v){if(callbacks_.lazy_mouse_changed)callbacks_.lazy_mouse_changed(v,lazy_distance_->value());});
  lazy_action_=new QAction("Lazy mouse",this);lazy_action_->setObjectName("toggleLazyMouse");lazy_action_->setShortcut(QKeySequence(Qt::Key_D));addAction(lazy_action_);connect(lazy_action_,&QAction::triggered,lazy_toggle_,&QToolButton::click);
  toolbar_layout->addStretch();
  // A quiet line at the end of the toolbar. It carries no space of its own until
  // a newer release has been confirmed, so nothing is shown when nothing is new.
  update_notice_=new QWidget(toolbar);
  update_notice_->setObjectName("updateNotice");
  auto *update_layout=row(update_notice_,0);
  update_label_=new QLabel(update_notice_);
  update_label_->setObjectName("updateNoticeText");
  update_layout->addWidget(update_label_);
  auto *update_get=new QToolButton(update_notice_);
  update_get->setObjectName("updateNoticeGet");
  update_get->setText("Get");
  update_get->setAutoRaise(true);
  update_get->setToolTip("Open the download page for this release");
  update_layout->addWidget(update_get);
  toolbar_layout->addWidget(update_notice_);
  update_notice_->hide();
  connect(update_get,&QToolButton::clicked,this,[this]{if(update_page_.isValid())QDesktopServices::openUrl(update_page_);});
  auto *undo_toolbar=tool(toolbar,Glyph::Undo,"Undo");undo_toolbar->setObjectName("undoToolbar");toolbar_layout->addWidget(undo_toolbar);
  auto *redo_toolbar=tool(toolbar,Glyph::Redo,"Redo");redo_toolbar->setObjectName("redoToolbar");toolbar_layout->addWidget(redo_toolbar);
  auto *hide_excluded=tool(toolbar,Glyph::HideExcluded,"Hide excluded geometry",true);hide_excluded->setObjectName("hideExcludedGeometry");hide_excluded->setCheckable(true);hide_excluded->setAccessibleName("Paint strokes ignore masked UV Tiles");hide_excluded->setToolTip("Hide/Ignore excluded geometry (Alt+H)");toolbar_layout->addWidget(hide_excluded);connect(hide_excluded,&QToolButton::toggled,this,[this](bool enabled){if(callbacks_.hide_excluded_changed)callbacks_.hide_excluded_changed(enabled);});auto *hide_action=new QAction(this);hide_action->setObjectName("toggleHideExcludedGeometry");hide_action->setShortcut(QKeySequence(Qt::ALT|Qt::Key_H));addAction(hide_action);connect(hide_action,&QAction::triggered,hide_excluded,&QToolButton::toggle);
  symmetry_toggle_=tool(toolbar,Glyph::Symmetry,"Symmetry",true);symmetry_toggle_->setCheckable(true);symmetry_toggle_->setObjectName("symmetryToggle");symmetry_toggle_->setToolTip("3D mirror / radial symmetry (L)");toolbar_layout->addWidget(symmetry_toggle_);
  auto *symmetry_menu_button=tool(toolbar,Glyph::More,"Symmetry settings",true);symmetry_menu_button->setObjectName("symmetryMenuButton");symmetry_menu_button->setToolTip("Symmetry settings");symmetry_menu_button->setPopupMode(QToolButton::InstantPopup);toolbar_layout->addWidget(symmetry_menu_button);
  auto *symmetry_menu=new QMenu(symmetry_menu_button);symmetry_menu_button->setMenu(symmetry_menu);auto *symmetry_action=new QWidgetAction(symmetry_menu);auto *symmetry_panel=new QWidget(symmetry_menu);auto *symmetry_layout=column(symmetry_panel);symmetry_layout->setContentsMargins(12,12,12,12);
  symmetry_mode_=new QComboBox(symmetry_panel);symmetry_mode_->setObjectName("symmetryMode");symmetry_mode_->addItems({"Mirror","Radial"});symmetry_layout->addWidget(new QLabel("Symmetry mode",symmetry_panel));symmetry_layout->addWidget(symmetry_mode_);
  symmetry_axis_=new QComboBox(symmetry_panel);symmetry_axis_->setObjectName("symmetryAxis");symmetry_axis_->addItems({"X","Y","Z"});symmetry_layout->addWidget(new QLabel("World axis",symmetry_panel));symmetry_layout->addWidget(symmetry_axis_);
  symmetry_offset_=new bts::NumericSpinBox(symmetry_panel);symmetry_offset_->setObjectName("symmetryOffset");symmetry_offset_->setRange(-1e6,1e6);symmetry_offset_->setDecimals(4);symmetry_offset_->setSingleStep(.1);symmetry_offset_->setKeyboardTracking(false);symmetry_layout->addWidget(new QLabel("Mirror plane offset",symmetry_panel));symmetry_layout->addWidget(symmetry_offset_);
  symmetry_copies_=new QSpinBox(symmetry_panel);symmetry_copies_->setObjectName("symmetryCopies");symmetry_copies_->setRange(2,32);symmetry_layout->addWidget(new QLabel("Radial copies",symmetry_panel));symmetry_layout->addWidget(symmetry_copies_);
  for(int i=0;i<3;++i){symmetry_center_[i]=new bts::NumericSpinBox(symmetry_panel);symmetry_center_[i]->setObjectName(QString("symmetryCenter%1").arg(i));symmetry_center_[i]->setRange(-1e6,1e6);symmetry_center_[i]->setDecimals(4);symmetry_center_[i]->setSingleStep(.1);symmetry_center_[i]->setKeyboardTracking(false);symmetry_layout->addWidget(new QLabel(QString("Radial center %1").arg(QChar::fromLatin1("XYZ"[i])),symmetry_panel));symmetry_layout->addWidget(symmetry_center_[i]);}
  symmetry_intersection_=new QCheckBox("Show intersection",symmetry_panel);symmetry_intersection_->setObjectName("symmetryIntersection");symmetry_layout->addWidget(symmetry_intersection_);symmetry_action->setDefaultWidget(symmetry_panel);symmetry_menu->addAction(symmetry_action);
  symmetry_manipulator_=tool(toolbar,Glyph::SymmetryManipulator,"Symmetry manipulator",true);symmetry_manipulator_->setCheckable(true);symmetry_manipulator_->setObjectName("symmetryManipulator");symmetry_manipulator_->setToolTip("Show/Hide symmetry manipulator");toolbar_layout->addWidget(symmetry_manipulator_);
  const auto change_symmetry=[this]{auto s=symmetry_;s.enabled=symmetry_toggle_->isChecked();s.manipulator=symmetry_manipulator_->isChecked();s.mode=SymmetryMode(symmetry_mode_->currentIndex());s.axis=symmetry_axis_->currentIndex();s.offset=symmetry_offset_->value();s.copies=symmetry_copies_->value();s.intersection=symmetry_intersection_->isChecked();for(int i=0;i<3;++i)s.center[i]=symmetry_center_[i]->value();if(callbacks_.symmetry_changed)callbacks_.symmetry_changed(s);};
  connect(symmetry_toggle_,&QToolButton::toggled,this,change_symmetry);connect(symmetry_manipulator_,&QToolButton::toggled,this,change_symmetry);connect(symmetry_mode_,&QComboBox::currentIndexChanged,this,change_symmetry);connect(symmetry_axis_,&QComboBox::currentIndexChanged,this,change_symmetry);connect(symmetry_offset_,&QDoubleSpinBox::valueChanged,this,change_symmetry);connect(symmetry_copies_,&QSpinBox::valueChanged,this,change_symmetry);connect(symmetry_intersection_,&QCheckBox::toggled,this,change_symmetry);for(auto *control:symmetry_center_)connect(control,&QDoubleSpinBox::valueChanged,this,change_symmetry);
  auto *symmetry_shortcut=new QAction(this);symmetry_shortcut->setObjectName("toggleSymmetry");symmetry_shortcut->setShortcut(QKeySequence(Qt::Key_L));addAction(symmetry_shortcut);connect(symmetry_shortcut,&QAction::triggered,symmetry_toggle_,&QToolButton::click);set_symmetry(SymmetrySettings{});
  trackball_toggle_=new QCheckBox("Trackball",toolbar);trackball_toggle_->setObjectName("trackballNavigation");trackball_toggle_->setToolTip("Free orbit with roll (Alt + left drag). Shift retains turntable snap.");toolbar_layout->addWidget(trackball_toggle_);
  connect(trackball_toggle_,&QCheckBox::toggled,this,[this](bool enabled){if(callbacks_.trackball_changed)callbacks_.trackball_changed(enabled);});
  view_layout_control_ = new QComboBox(toolbar);
  view_layout_control_->setObjectName("viewLayoutControl");
  view_layout_control_->setAccessibleName("Viewport layout");
  view_layout_control_->addItems({"3D only", "2D only", "3D/2D", "2D/3D"});
  view_layout_control_->setMinimumWidth(72);
  view_layout_control_->setMaximumWidth(80);
  view_layout_control_->setEnabled(bool(callbacks_.view_layout_changed));
  view_layout_control_->setToolTip(callbacks_.view_layout_changed ?
      "F1: split  F2: 3D  F3: 2D  F4: swap split views" : "Viewport layouts are unavailable.");
  toolbar_layout->addWidget(view_layout_control_);
  reference_image_toggle_ = new QCheckBox("Reference Image", toolbar);
  reference_image_toggle_->setObjectName("referenceImageToggle");
  reference_image_toggle_->setAccessibleName("Reference Image");
  reference_image_toggle_->setToolTip("Show a 2D reference image in the 2D view instead of the UV layout and pick colors from it");
  toolbar_layout->addWidget(reference_image_toggle_);
  connect(reference_image_toggle_, &QCheckBox::toggled, this, [this](bool enabled) {
    if (callbacks_.reference_image_toggled) callbacks_.reference_image_toggled(enabled);
  });
  open_reference_image_button_ = new QPushButton("Open Image...", toolbar);
  open_reference_image_button_->setObjectName("openReferenceImageButton");
  open_reference_image_button_->setAccessibleName("Open Reference Image");
  open_reference_image_button_->setToolTip("Open a reference image for the 2D view");
  toolbar_layout->addWidget(open_reference_image_button_);
  connect(open_reference_image_button_, &QPushButton::clicked, this, [this] {
    if (callbacks_.open_reference_image_requested) callbacks_.open_reference_image_requested();
  });
  projection_control_ = new QComboBox(toolbar);
  projection_control_->setObjectName("projectionControl");
  projection_control_->setMaximumWidth(110);
  projection_control_->setAccessibleName("Camera projection");
  projection_control_->addItems({"Perspective view", "Orthographic view"});
  projection_control_->setEnabled(bool(callbacks_.projection_changed));
  projection_control_->setToolTip(callbacks_.projection_changed ?
      "F5: Perspective view  F6: Orthographic view" : "Camera projection is unavailable.");
  toolbar_layout->addWidget(projection_control_);
  display_control_ = new QComboBox(toolbar);
  display_control_->setObjectName("displayTransform");
  display_control_->setAccessibleName("Display transform");
  display_control_->setToolTip("Display transform; authored texture values stay in the working space");
  display_control_->setMaximumWidth(88);
  toolbar_layout->addWidget(display_control_);
  auto *toolbar_bake=new QToolButton(toolbar);toolbar_bake->setObjectName("toolbarBakeMeshMaps");toolbar_bake->setText("Bake Mesh Maps");toolbar_bake->setToolTip("Bake mesh maps for this project");toolbar_bake->setEnabled(false);toolbar_layout->addWidget(toolbar_bake);connect(toolbar_bake,&QToolButton::clicked,this,[this]{if(callbacks_.bake_mesh_maps)callbacks_.bake_mesh_maps();});
  connect(display_control_, &QComboBox::currentTextChanged, this, [this](const QString &name) {
    if (callbacks_.display_transform_changed) callbacks_.display_transform_changed(name);
  });
  // Preserve control widths when the viewport is narrower than the context tools.
  toolbar_layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
  auto *toolbar_scroll=new QScrollArea(center);toolbar_scroll->setObjectName("contextToolbarScroll");
  toolbar_scroll->setWidgetResizable(true);toolbar_scroll->setFrameShape(QFrame::NoFrame);
  toolbar_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  toolbar_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  toolbar_scroll->setFixedHeight(56);toolbar_scroll->setWidget(toolbar);
  center_layout->addWidget(toolbar_scroll);
  viewport_shelf_splitter_ = splitter(Qt::Horizontal, center, "viewportShelfSplitter");
  center_layout->addWidget(viewport_shelf_splitter_, 1);
  viewport_shelf_splitter_->addWidget(viewport);
  viewport_shelf_splitter_->setStretchFactor(1, 1);

  shelf_ = new QWidget(viewport_shelf_splitter_);
  viewport_shelf_splitter_->insertWidget(0,shelf_);
  shelf_->setObjectName("assetShelf");
  shelf_->setMinimumHeight(shelf_minimum_height);shelf_->setMinimumWidth(280);shelf_->setMaximumWidth(420);
  auto *shelf_layout = column(shelf_);
  QToolButton *close_shelf;
  shelf_layout->addWidget(header(shelf_, "ASSETS", &close_shelf));
  auto *library_header=new QLabel("All libraries  /  Your assets",shelf_);library_header->setObjectName("assetLibraryHeader");library_header->setContentsMargins(8,4,8,4);library_header->setStyleSheet(bts::theme::qss("color: @Muted@; font-size: 10px; font-weight: 600; background: @Canvas@; border-bottom: 1px solid @Canvas@;"));shelf_layout->addWidget(library_header);
  auto *filter = new QWidget(shelf_);
  filter->setObjectName("shelfFilter");
  filter->setFixedHeight(30);
  auto *filter_layout = row(filter, 6);
  filter_layout->setSpacing(4);
  auto *import_resources=tool(filter,Glyph::Folder,"Import resources",true);import_resources->setObjectName("importResources");import_resources->setText("Import");import_resources->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);import_resources->setFixedSize(90,24);filter_layout->addWidget(import_resources);connect(import_resources,&QToolButton::clicked,this,[this]{if(callbacks_.resource_import_requested)callbacks_.resource_import_requested();});
  auto *shelf_preferences=tool(filter,Glyph::Settings,"Preferences — Shelf and Viewport",true);shelf_preferences->setObjectName("shelfPreferences");shelf_preferences->setFixedSize(24,24);filter_layout->addWidget(shelf_preferences);connect(shelf_preferences,&QToolButton::clicked,this,[this]{if(callbacks_.shelf_preferences_requested)callbacks_.shelf_preferences_requested();});
  auto *shelf_preferences_action=new QAction("&Preferences…",this);shelf_preferences_action->setObjectName("shelfPreferencesAction");shelf_preferences_action->setShortcut(QKeySequence::Preferences);addAction(shelf_preferences_action);connect(shelf_preferences_action,&QAction::triggered,shelf_preferences,&QToolButton::click);
  asset_scope_control_=new QComboBox(filter);asset_scope_control_->setMinimumWidth(104);asset_scope_control_->setMaximumWidth(130);asset_scope_control_->setObjectName("shelfLibraryScope");asset_scope_control_->addItems({"All libraries","Built-in","Session","Project","User"});filter_layout->addWidget(asset_scope_control_);connect(asset_scope_control_,&QComboBox::currentIndexChanged,this,[this](int i){asset_scope_=i-1;if(shelf_categories_&&shelf_categories_->currentRow()==1&&i!=3){const QSignalBlocker block(shelf_categories_);shelf_categories_->setCurrentRow(0);}refresh_alpha_shelf();});
  asset_reset_=tool(filter,Glyph::Reset,"Reset filters",false);asset_reset_->setObjectName("resetShelfFilters");asset_reset_->setFixedSize(24,24);filter_layout->addWidget(asset_reset_);connect(asset_reset_,&QToolButton::clicked,this,[this]{asset_search_->clear();shelf_categories_->setCurrentRow(0);asset_scope_control_->setCurrentIndex(0);refresh_alpha_shelf();});
  auto *filter_glyph_btn=tool(filter, Glyph::Filter, "Filter assets");filter_glyph_btn->setFixedSize(24,24);filter_layout->addWidget(filter_glyph_btn);
  asset_category_chip_=new QToolButton(filter);asset_category_chip_->setObjectName("shelfCategoryChip");asset_category_chip_->setToolButtonStyle(Qt::ToolButtonTextOnly);asset_category_chip_->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Fixed);asset_category_chip_->setFixedHeight(22);asset_category_chip_->setMaximumWidth(160);asset_category_chip_->hide();filter_layout->addWidget(asset_category_chip_);connect(asset_category_chip_,&QToolButton::clicked,this,[this]{if(shelf_categories_)shelf_categories_->setCurrentRow(0);});
  filter_layout->addStretch(1);
  auto *search = new QLineEdit(shelf_);
  asset_search_=search;search->setClearButtonEnabled(true);
  search->setObjectName("shelfSearch");
  search->setPlaceholderText("Search assets…");
  search->setToolTip("Combine n:name s:library u:usage p:path; append = for exact matches");
  search->setStyleSheet(bts::theme::qss("QLineEdit#shelfSearch { margin: 4px 6px 2px 6px; padding: 2px 6px; min-height: 22px; background: @Separator@; border: 1px solid @Hover@; border-radius: @Radius@; } QLineEdit#shelfSearch:focus { border-color: @Accent@; }"));
  connect(search,&QLineEdit::textChanged,this,[this](const QString &text){alpha_search_=text;refresh_alpha_shelf();});
  shelf_layout->addWidget(search);
  shelf_layout->addWidget(filter);
  auto *download_materials=new QPushButton("Download free materials",shelf_);
  download_materials->setObjectName("downloadFreeMaterialsShelf");
  download_materials->setToolTip("Poly Haven materials are CC0. Download, then import or drop the texture files here.");
  connect(download_materials,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://polyhaven.com/textures"));});
  shelf_layout->addWidget(download_materials);

  auto *shelf_body = new QWidget(shelf_);
  auto *shelf_body_layout = column(shelf_body);
  shelf_body_layout->setSpacing(0);
  auto *categories = new ShelfCategoryList(shelf_body);
  shelf_categories_=categories;categories->setObjectName("shelfCategories");
  categories->setItemDelegate(new ShelfCategoryDelegate(categories));
  categories->setViewMode(QListView::IconMode);categories->setFlow(QListView::LeftToRight);categories->setWrapping(true);categories->setResizeMode(QListView::Adjust);categories->setMovement(QListView::Static);categories->setSpacing(0);categories->setGridSize(QSize(88,24));categories->setFixedHeight(126);
  categories->addItems(shelf_category_names());
  categories->setCurrentRow(0);
  categories->update_grid();
  shelf_body_layout->addWidget(categories);
  auto *assets = new EmptyList("No assets", shelf_body);
  alpha_assets_=assets;assets->setObjectName("shelfAssets");auto *asset_names=new ShelfCardDelegate(assets);assets->setItemDelegate(asset_names);assets->setEditTriggers(QAbstractItemView::EditKeyPressed);new NameListShortcuts(assets);asset_names->renamed=[this](const QModelIndex &index,const QString &name){const QUuid id(index.data(Qt::UserRole).toString());QTimer::singleShot(0,this,[this,id,name]{if(callbacks_.asset_renamed)callbacks_.asset_renamed(id,name);});};
  assets->setViewMode(QListView::IconMode);
  assets->setResizeMode(QListView::Adjust);
  assets->setMovement(QListView::Static);
  // QListView::setMovement turns dragging off for Static, so the shelf has to
  // be made draggable after that call and not before it. Setting it first is
  // silently undone, which is what stopped assets being dragged onto a mesh.
  assets->setDragEnabled(true);
  assets->setDragDropMode(QAbstractItemView::DragOnly);
  assets->setDefaultDropAction(Qt::CopyAction);
  assets->setIconSize(QSize(84, 84));
  assets->setGridSize(QSize(90, 114));
  assets->setSpacing(bts::theme::SpaceTight);
  shelf_body_layout->addWidget(assets, 1);
  {auto *empty_materials=new QWidget(shelf_body);empty_materials->setObjectName("emptyMaterials");
  auto *empty_layout=column(empty_materials);
  auto *empty_note=new QLabel("Your material library is empty. Poly Haven materials are CC0. Import or drop downloaded textures here.",empty_materials);empty_note->setObjectName("emptyMaterialsNote");empty_note->setWordWrap(true);empty_layout->addWidget(empty_note);
  auto *empty_download=new QPushButton("Download free materials",empty_materials);empty_download->setObjectName("downloadFreeMaterialsEmpty");
  connect(empty_download,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://polyhaven.com/textures"));});empty_layout->addWidget(empty_download);
  empty_materials->hide();shelf_body_layout->addWidget(empty_materials); }

  shelf_layout->addWidget(shelf_body, 1);
  connect(categories, &QListWidget::currentTextChanged, this, [this,assets](const QString &text) {
    {const QSignalBlocker block(asset_scope_control_);asset_scope_=text=="Project"?int(AssetScope::Project):-1;asset_scope_control_->setCurrentIndex(asset_scope_+1);}{const QSignalBlocker block(asset_search_);asset_search_->clear();alpha_search_.clear();}assets->set_empty_text(text == "All" ? "No assets" : "No assets in " + text);refresh_alpha_shelf();
  });
  auto *smart_mask_click=new QTimer(assets);smart_mask_click->setSingleShot(true);smart_mask_click->setInterval(QApplication::doubleClickInterval());
  connect(assets,&QListWidget::itemClicked,this,[this,smart_mask_click](QListWidgetItem *item){smart_mask_click->stop();if(asset_catalog_){const QUuid id(item->data(Qt::UserRole).toString());for(const auto &asset:asset_catalog_->assets())if(asset.id==id&&asset.usages.contains("brush")){if(callbacks_.asset_selected)callbacks_.asset_selected(asset);return;}}if(QDateTime::currentMSecsSinceEpoch()-smart_mask_click->property("doubleClickedAt").toLongLong()<QApplication::doubleClickInterval()||!asset_catalog_)return;const QUuid id(item->data(Qt::UserRole).toString());for(const auto &asset:asset_catalog_->assets())if(asset.id==id&&(asset.smart_mask||is_spmsk_asset(asset))){smart_mask_click->setProperty("asset",id);smart_mask_click->setProperty("target",layers_&&layers_->currentItem()?layers_->currentItem()->data(Qt::UserRole):QVariant{});smart_mask_click->start();break;}});
  connect(smart_mask_click,&QTimer::timeout,this,[this,smart_mask_click]{if(!asset_catalog_||!layers_||!layers_->currentItem()||layers_->currentItem()->data(Qt::UserRole)!=smart_mask_click->property("target"))return;const auto id=smart_mask_click->property("asset").toUuid();for(const auto &asset:asset_catalog_->assets())if(asset.id==id&&(asset.smart_mask||is_spmsk_asset(asset))){if(callbacks_.asset_selected)callbacks_.asset_selected(asset);break;}});
  connect(assets,&QListWidget::itemDoubleClicked,this,[this,smart_mask_click](QListWidgetItem *item){smart_mask_click->stop();smart_mask_click->setProperty("doubleClickedAt",QDateTime::currentMSecsSinceEpoch());const auto key=item->data(Qt::UserRole).toString();if(asset_catalog_)for(const auto &asset:asset_catalog_->assets())if(asset.id==QUuid(key)&&asset.usages.contains("brush"))return;if(key.startsWith("baked:")){if(callbacks_.baked_asset&&callbacks_.asset_selected)callbacks_.asset_selected(callbacks_.baked_asset(key));return;}if(asset_catalog_){const auto id=QUuid(key);for(const auto &asset:asset_catalog_->assets())if(asset.id==id){if(callbacks_.asset_selected)callbacks_.asset_selected(asset);break;}}else if(callbacks_.alpha_selected)callbacks_.alpha_selected(alpha_resources_.at(item->data(Qt::UserRole).toInt()));});
  assets->setContextMenuPolicy(Qt::CustomContextMenu);
  new ImageDropFilter(assets,[this](const QStringList &paths){if(callbacks_.texture_assets_import_requested)callbacks_.texture_assets_import_requested(paths);});
  connect(assets,&QListWidget::customContextMenuRequested,this,[this,assets](QPoint point){if(asset_catalog_){auto *item=assets->itemAt(point);const StudioAsset *brush=nullptr;if(item)for(const auto &a:asset_catalog_->assets())if(a.id==QUuid(item->data(Qt::UserRole).toString())&&a.usages.contains("brush")){brush=&a;break;}if(brush||!item){auto *menu=new QMenu(assets);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("brushPresetContextMenu");auto *create=menu->addAction("Create brush preset…");create->setObjectName("createBrushPreset");connect(create,&QAction::triggered,menu,[this]{if(callbacks_.save_brush_preset)callbacks_.save_brush_preset();});auto *import_brush=menu->addAction("Import brush preset…");import_brush->setObjectName("importBrushPreset");connect(import_brush,&QAction::triggered,menu,[this]{if(callbacks_.resource_import_requested)callbacks_.resource_import_requested();});if(brush){const auto id=brush->id;for(const auto &entry:std::array<std::pair<const char *,const char *>,3>{{{"update","Update from current brush"},{"rename","Rename…"},{"delete","Delete…"}}}){auto *action=menu->addAction(entry.second);action->setObjectName(QString("brushPreset_")+entry.first);action->setEnabled(QString(entry.first)=="rename"?brush->scope!=AssetScope::Builtin:brush->scope==AssetScope::User);connect(action,&QAction::triggered,menu,[this,id,operation=QString(entry.first)]{if(callbacks_.brush_preset_action)callbacks_.brush_preset_action(operation,id);});}auto *export_action=menu->addAction("Export brush preset…");export_action->setObjectName("exportBrushPreset");connect(export_action,&QAction::triggered,menu,[this,id]{for(const auto &a:asset_catalog_->assets())if(a.id==id&&callbacks_.asset_export_requested){callbacks_.asset_export_requested(a);break;}});}menu->popup(assets->viewport()->mapToGlobal(point));return;}}auto *item=assets->itemAt(point);if(!item||!asset_catalog_)return;const QUuid id(item->data(Qt::UserRole).toString());const auto found=std::find_if(asset_catalog_->assets().begin(),asset_catalog_->assets().end(),[&](const auto &asset){return asset.id==id;});if(found==asset_catalog_->assets().end()||(!found->material_preset&&!found->smart_mask&&!is_spsm_archive_asset(*found)))return;assets->setCurrentItem(item);auto *menu=new QMenu(assets);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("presetResourceContextMenu");if(found->smart_mask||is_spmsk_asset(*found)){auto *apply=menu->addAction("Apply mask generator");apply->setObjectName("applyShelfSmartMask");apply->setEnabled(asset_project_ready_);connect(apply,&QAction::triggered,menu,[this,id]{if(!asset_catalog_)return;for(const auto &asset:asset_catalog_->assets())if(asset.id==id){if(callbacks_.asset_selected)callbacks_.asset_selected(asset);break;}});}if(is_spsm_archive_asset(*found)){auto *inspect=menu->addAction("Inspect SPSM import report…");inspect->setObjectName("inspectSpsmImportReport");connect(inspect,&QAction::triggered,menu,[this,id]{if(!asset_catalog_)return;for(const auto &asset:asset_catalog_->assets())if(asset.id==id){show_spsm_source_report(this,asset);break;}});}auto *save=menu->addAction("Export preset…");save->setObjectName("exportResourcePreset");connect(save,&QAction::triggered,menu,[this,id]{if(!asset_catalog_)return;for(const auto &asset:asset_catalog_->assets())if(asset.id==id){if(callbacks_.asset_export_requested)callbacks_.asset_export_requested(asset);break;}});menu->popup(assets->viewport()->mapToGlobal(point));});
  add_alpha_resource(BrushAlpha{});BrushAlpha plus;plus.kind=AlphaKind::Plus;add_alpha_resource(plus);

  inspector_splitter_ = splitter(Qt::Vertical, workspace_splitter_, "inspectorSplitter");
  workspace_splitter_->addWidget(inspector_splitter_);
  inspector_splitter_->setMinimumWidth(280);
  workspace_splitter_->setStretchFactor(0, 1);
  workspace_splitter_->setStretchFactor(1, 0);
  auto *texture_panel = new QWidget;
  texture_panel->setObjectName("textureSetPanel");
  texture_panel->setMinimumHeight(195);
  auto *texture_layout = column(texture_panel);
  auto *set_header=header(texture_panel, "Texture sets");auto *set_settings=new QToolButton(set_header);set_settings->setObjectName("textureSetListSettings");set_settings->setText("Settings");set_settings->setToolTip("Texture Sets settings");set_header->layout()->addWidget(set_settings);texture_layout->addWidget(set_header);
  texture_sets_ = new EmptyList("No texture sets", texture_panel);
  texture_sets_->setObjectName("textureSets");
  texture_sets_->setMinimumHeight(64);
  texture_sets_->setToolTip("Uncheck a Texture Set to hide and unload it; check to reload");
  connect(texture_sets_,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){
    if(item->data(Qt::CheckStateRole).isValid()&&callbacks_.residency_changed)
      callbacks_.residency_changed(item->data(Qt::UserRole).toUInt(),0,item->checkState()==Qt::Checked);
  });
  auto *rename_set=new QAction(this);rename_set->setObjectName("renameTextureSet");connect(rename_set,&QAction::triggered,this,[this,rename_set]{const auto values=rename_set->data().toMap();if(callbacks_.texture_set_renamed)callbacks_.texture_set_renamed(values["uid"].toUInt(),values["name"].toString());});
  new NameListShortcuts(texture_sets_);auto *set_names=new TextureSetNameDelegate(texture_sets_);texture_sets_->setItemDelegate(set_names);texture_sets_->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
  set_names->renamed=[this](const QModelIndex &index,const QString &name){const auto uid=index.data(Qt::UserRole).toUInt();QTimer::singleShot(0,this,[this,uid,name]{if(callbacks_.texture_set_renamed)callbacks_.texture_set_renamed(uid,name);});};
  texture_sets_->setSelectionMode(QAbstractItemView::SingleSelection);
  texture_sets_->setAccessibleName("Texture set selection");
  texture_sets_->setEnabled(bool(callbacks_.texture_set_selected));
  if (!callbacks_.texture_set_selected) {
    texture_sets_->setToolTip("Texture set selection is unavailable.");
  }
  texture_layout->addWidget(texture_sets_, 1);
  uv_tiles_=new QTreeWidget(texture_panel);uv_tiles_->setObjectName("uvTiles");uv_tiles_->setHeaderHidden(true);
  connect(uv_tiles_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int){
    const int tile=item->data(0,Qt::UserRole).toInt();
    if(tile&&texture_sets_->currentItem()&&callbacks_.residency_changed)
      callbacks_.residency_changed(texture_sets_->currentItem()->data(Qt::UserRole).toUInt(),tile,item->checkState(0)==Qt::Checked);
  });
  uv_tiles_->setMaximumHeight(100);uv_tiles_->hide();texture_layout->addWidget(uv_tiles_);

  layer_settings_tabs_ = new QTabWidget(inspector_splitter_);
  inspector_splitter_->addWidget(layer_settings_tabs_);
  layer_settings_tabs_->setObjectName("layerSettingsTabs");
  layer_settings_tabs_->setMinimumHeight(145);
  layer_settings_tabs_->setDocumentMode(true);
  layer_settings_tabs_->tabBar()->setDrawBase(false);
  auto *layers = new QWidget(layer_settings_tabs_);
  auto *layers_layout = column(layers);
  auto *layer_actions = new QWidget(layers);
  layer_actions->setFixedHeight(31);
  auto *layer_actions_layout = row(layer_actions);
  auto *channel = new QComboBox(layer_actions);
  layer_context_=channel;
  channel->setObjectName("layerChannelContext");
  channel->setAccessibleName("Layer channel context");
  channel->setToolTip("Channel used by layer thumbnails, opacity and blending");
  connect(channel,&QComboBox::currentIndexChanged,this,[this](int index) {
    if(index<0)return;
    layer_channel_=layer_context_->itemData(index).toInt();
    if(callbacks_.layer_context_changed)callbacks_.layer_context_changed();
  });
  layer_actions_layout->addWidget(channel, 1);
  effect_up_=new QAction("Move effect up",this);effect_up_->setObjectName("moveEffectUp");effect_up_->setShortcut(QKeySequence(Qt::ALT|Qt::Key_Up));effect_down_=new QAction("Move effect down",this);effect_down_->setObjectName("moveEffectDown");effect_down_->setShortcut(QKeySequence(Qt::ALT|Qt::Key_Down));addAction(effect_up_);addAction(effect_down_);connect(effect_up_,&QAction::triggered,this,[this]{if(callbacks_.move_effect)callbacks_.move_effect(1);});connect(effect_down_,&QAction::triggered,this,[this]{if(callbacks_.move_effect)callbacks_.move_effect(-1);});
  effects_button_=tool(layer_actions,Glyph::Filter,"Add effect",true);effects_button_->setIcon(painter_workflow_icon(PainterGlyph::Wand));effects_button_->setObjectName("addLayerEffect");effects_button_->setPopupMode(QToolButton::InstantPopup);auto *effects_menu=new QMenu(effects_button_);effects_button_->setMenu(effects_menu);auto *paint_effect=effects_menu->addAction("Add Paint");paint_effect->setObjectName("addPaintEffect");connect(paint_effect,&QAction::triggered,this,[this]{if(callbacks_.add_paint_effect)callbacks_.add_paint_effect();});auto *generator=effects_menu->addAction("Add generator");generator->setObjectName("addMaskGenerator");connect(generator,&QAction::triggered,this,[this]{if(callbacks_.add_mask_generator)callbacks_.add_mask_generator();});auto *fill_effect=effects_menu->addAction("Add Fill");fill_effect->setObjectName("addMaskFillEffect");connect(fill_effect,&QAction::triggered,this,[this]{if(callbacks_.add_mask_fill)callbacks_.add_mask_fill();});auto *levels_action=effects_menu->addAction("Add Levels");levels_action->setObjectName("addLevelsEffect");connect(levels_action,&QAction::triggered,this,[this]{if(callbacks_.add_levels)callbacks_.add_levels(layer_channel_);});auto *filter_action=effects_menu->addAction("Add Filter");filter_action->setObjectName("addFilterEffect");connect(filter_action,&QAction::triggered,this,[this]{if(callbacks_.add_filter_effect)callbacks_.add_filter_effect(layer_channel_);});auto *anchor_action=effects_menu->addAction("Add anchor point");anchor_action->setObjectName("addMaskAnchor");connect(anchor_action,&QAction::triggered,this,[this]{if(callbacks_.add_mask_anchor)callbacks_.add_mask_anchor();});layer_actions_layout->addWidget(effects_button_);

  auto *cache_button=tool(layer_actions,Glyph::Shelf,"Add Pre-compose / Cache Layer");cache_button->setObjectName("addPrecomposeLayer");layer_actions_layout->addWidget(cache_button);
  connect(cache_button,&QToolButton::clicked,this,[this]{if(callbacks_.precompose_layer)callbacks_.precompose_layer(false);});
  smart_material_button_=tool(layer_actions,Glyph::Material,"Add material preset",true);smart_material_button_->setObjectName("addSmartMaterial");smart_material_button_->setPopupMode(QToolButton::InstantPopup);auto *smart_menu=new QMenu(smart_material_button_);smart_material_button_->setMenu(smart_menu);
  for (const auto &[glyph, name] : {
           std::pair{Glyph::Mask, "Add mask"}, {Glyph::Plus, "Add paint layer"},
           {Glyph::Fill, "Add fill layer"}, {Glyph::Folder, "Add folder"}, {Glyph::Trash, "Delete layer"}}) {
    auto *button = tool(layer_actions, glyph, QString::fromLatin1(name));
    layer_actions_layout->addWidget(button);
    if (glyph==Glyph::Plus) {
      paint_button_=button;button->setIcon(painter_workflow_icon(PainterGlyph::Paint));button->setObjectName("addPaintLayer");
      connect(button,&QToolButton::clicked,this,[this]{if(callbacks_.add_paint)callbacks_.add_paint();});
      button->setPopupMode(QToolButton::MenuButtonPopup);
      auto *menu=new QMenu(button);button->setMenu(menu);
      add_adjustment_action_=menu->addAction("Add adjustment paint layer (Pass Through)");
      add_adjustment_action_->setObjectName("addAdjustmentPaintLayer");
      add_adjustment_action_->setEnabled(false);
      connect(add_adjustment_action_,&QAction::triggered,this,[this]{if(callbacks_.add_adjustment)callbacks_.add_adjustment();});
    }
    if(glyph==Glyph::Mask) {
      mask_button_=button;button->setObjectName("addLayerMask");button->setPopupMode(QToolButton::InstantPopup);
      auto *menu=new QMenu("Mask",button);button->setMenu(menu);
      for(auto [label,background]:{std::pair{"Add white mask",1.f},{"Add black mask",0.f}}) {
        auto *action=menu->addAction(label);action->setObjectName(background>0?"addWhiteMask":"addBlackMask");
        connect(action,&QAction::triggered,this,[this,background]{if(callbacks_.add_mask)callbacks_.add_mask(background);});
      }
    }
    if(glyph==Glyph::Mask) {
      auto *selection=button->menu()->addAction("Add mask with color selection");selection->setObjectName("addColorSelectionMask");connect(selection,&QAction::triggered,this,[this]{if(callbacks_.add_color_selection)callbacks_.add_color_selection();});
      auto *action=button->menu()->addAction("Add bitmap mask…");action->setObjectName("addBitmapMask");
      connect(action,&QAction::triggered,this,[this]{if(callbacks_.add_bitmap_mask)callbacks_.add_bitmap_mask();});
      auto *compare=button->menu()->addAction("Add mask with height combination");compare->setObjectName("addCompareMask");connect(compare,&QAction::triggered,this,[this]{if(callbacks_.add_compare_mask)callbacks_.add_compare_mask();});
    }
    if(glyph==Glyph::Folder) {
      folder_button_=button;button->setObjectName("addLayerFolder");
      connect(button,&QToolButton::clicked,this,[this]{if(callbacks_.add_folder)callbacks_.add_folder();});
    }
    if(glyph==Glyph::Trash)button->setObjectName("deleteLayer");
    if (glyph == Glyph::Fill) {
      fill_button_ = button;
      button->setObjectName("addFillLayer");
      connect(button, &QToolButton::clicked, this, [this] { if (callbacks_.add_fill) callbacks_.add_fill(); });
    }
  }
  const auto selected_mask_layer=[this]{const auto *item=layers_?layers_->currentItem():nullptr;return item?QUuid(item->data(Qt::UserRole).toString()):QUuid{};};
  auto *apply_smart_mask=mask_button_->menu()->addAction("Apply mask generator…");apply_smart_mask->setObjectName("applySmartMask");connect(apply_smart_mask,&QAction::triggered,this,[this]{set_shelf_visible(true);asset_scope_control_->setCurrentIndex(0);shelf_categories_->setCurrentRow(0);asset_search_->setText("u:smartmask");asset_search_->setFocus();});
  mask_button_->menu()->addSeparator();
  for(const auto &[label,name]:{std::pair{"Copy mask content","copyMaskContent"},{"Paste into mask","pasteIntoMask"},{"Create mask generator…","createSmartMask"},{"Invert mask background","invertMaskBackground"},{"Remove mask","removeMask"}}){
    auto *action=mask_button_->menu()->addAction(QString::fromUtf8(label));action->setObjectName(name);
    connect(action,&QAction::triggered,this,[this,selected_mask_layer,name]{const auto id=selected_mask_layer();if(id.isNull())return;if(QStringView(u"copyMaskContent")==QLatin1StringView(name)){if(callbacks_.copy_mask)callbacks_.copy_mask(id);}else if(QStringView(u"pasteIntoMask")==QLatin1StringView(name)){if(callbacks_.paste_mask)callbacks_.paste_mask(id);}else if(QStringView(u"createSmartMask")==QLatin1StringView(name)){if(callbacks_.create_smart_mask)callbacks_.create_smart_mask(id);}else if(QStringView(u"invertMaskBackground")==QLatin1StringView(name)){if(callbacks_.mask_background_inverted)callbacks_.mask_background_inverted(id);}else if(callbacks_.remove_mask)callbacks_.remove_mask(id);});
  }
  layer_actions_layout->addWidget(smart_material_button_);
  auto *image_layer=new QToolButton(layer_actions);image_layer->setText("▧");image_layer->setObjectName("importImageAsFillLayer");image_layer->setToolTip("Load image as a new fill layer…");layer_actions_layout->addWidget(image_layer);connect(image_layer,&QToolButton::clicked,this,[this]{if(callbacks_.layer_texture_file_requested)callbacks_.layer_texture_file_requested();});
  layers_layout->addWidget(layer_actions);
  auto *material_count=new QLabel(layers);
  material_count->setObjectName("layerMaterialLibraryCount");
  material_count->hide();
  layers_layout->addWidget(material_count);
  auto *empty=new QFrame(layers);empty->setObjectName("emptyLayersView");auto *empty_layout=new QVBoxLayout(empty);auto *empty_label=new QLabel("No project – File → New (Ctrl+N) / File → Open",empty);empty_label->setObjectName("emptyLayersMessage");empty_label->setWordWrap(true);empty_layout->addWidget(empty_label);
  for(const auto &[text,name]:{std::pair{"New project…","newProject"},std::pair{"Open project…","openProject"}}){auto *button=new QPushButton(text,empty);empty_layout->addWidget(button);connect(button,&QPushButton::clicked,this,[this,name]{if(auto *action=findChild<QAction *>(name))action->trigger();});}layers_layout->addWidget(empty);
  auto *layer_list = new LayerStackList(layers);
  layer_list->layer_dropped=[this](QUuid id,int target_row){if(callbacks_.layer_dropped)callbacks_.layer_dropped(id,target_row);};
  layer_list->delete_requested=[this]{
    if(!callbacks_.delete_selection)return;
    std::vector<std::pair<QUuid,QUuid>> effects;std::vector<QUuid> masks,layers_to_delete;
    for(auto *item:layers_->selectedItems()){
      const QUuid layer(item->data(Qt::UserRole).toString());const QUuid effect(item->data(Qt::UserRole+10).toString());
      if(!effect.isNull())effects.emplace_back(layer,effect);
      else if(item->data(Qt::UserRole+8).toBool())masks.push_back(layer);
      else layers_to_delete.push_back(layer);
    }
    if(!effects.empty()||!masks.empty()||!layers_to_delete.empty())callbacks_.delete_selection(effects,masks,layers_to_delete);
  };
  if(auto *button=findChild<QToolButton *>("deleteLayer"))connect(button,&QToolButton::clicked,layer_list,[layer_list]{if(layer_list->delete_requested)layer_list->delete_requested();});
  layers_ = layer_list;new ImageDropFilter(layer_list,[this](const QStringList &paths){if(callbacks_.layer_texture_files_dropped)callbacks_.layer_texture_files_dropped(paths);},{},[this](QWidget *source,QPoint point){if(!source)return;point=source->mapTo(layers_->viewport(),point);const auto index=layers_->indexAt(point);if(!index.isValid())return;const QUuid id(index.data(Qt::UserRole).toString()),effect(index.data(Qt::UserRole+10).toString());if(!effect.isNull()){if(callbacks_.effect_selected)callbacks_.effect_selected(id,effect);}else if(callbacks_.mask_selected)callbacks_.mask_selected(id,index.data(Qt::UserRole+6).toBool()&&layer_mask_rect(index,layers_->visualRect(index)).contains(point));});
  layer_list->setObjectName("layerStack");layer_list->hierarchy_dropped=[this](const std::vector<QUuid> &ids,QUuid target,bool into,bool above){if(callbacks_.layer_hierarchy_dropped)callbacks_.layer_hierarchy_dropped(ids,target,into,above);};
  layer_list->asset_accepted=[this](const QString &id){if(id.startsWith("baked:"))return asset_project_ready_;if(!asset_catalog_)return false;for(const auto &asset:asset_catalog_->assets())if(asset.id==QUuid(id))return smart_material_target_ready(asset,asset_project_ready_,asset_smart_ready_)||((asset.material||asset.smart_mask||is_spmsk_asset(asset))&&asset_project_ready_)||(asset.usages.contains("texture")&&asset_project_ready_);return false;};
  layer_list->asset_dropped=[this](const QString &id,int gap,bool replace,int row,bool mask){if(!callbacks_.asset_dropped)return;const auto deliver=[&](const StudioAsset &asset){int index=std::min(gap,layers_->count()-1);
      if(row>=0&&(asset.smart_mask||is_spmsk_asset(asset)||(asset.usages.contains("texture")&&(mask||!layers_->item(row)->data(Qt::UserRole+10).toString().isEmpty()))))index=row;
      else {while(index>=0&&!layers_->item(index)->data(Qt::UserRole+10).toString().isEmpty())--index;}
      const auto *target=index>=0?layers_->item(index):nullptr;const QUuid target_id=target?QUuid(target->data(Qt::UserRole).toString()):QUuid{};if(target&&asset.usages.contains("texture")){const QUuid layer(target->data(Qt::UserRole).toString()),effect(target->data(Qt::UserRole+10).toString());if(!effect.isNull()){if(callbacks_.effect_selected)callbacks_.effect_selected(layer,effect);}else if(callbacks_.mask_selected)callbacks_.mask_selected(layer,mask);}callbacks_.asset_dropped(asset,target_id,gap<layers_->count(),replace);};if(id.startsWith("baked:")){if(callbacks_.baked_asset)deliver(callbacks_.baked_asset(id));return;}if(!asset_catalog_)return;for(const auto &asset:asset_catalog_->assets())if(asset.id==QUuid(id)){deliver(asset);break;}};

  layer_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(layer_list,&QListWidget::itemSelectionChanged,this,[this] {
    if(selection_refresh_pending_)return;
    selection_refresh_pending_=true;
    QTimer::singleShot(0,this,[this]{selection_refresh_pending_=false;if(callbacks_.layer_selection_changed)callbacks_.layer_selection_changed();});
  });
  layers_layout->addWidget(layer_list, 1);
  new LayerEffectRowState(this);
  new NameListShortcuts(layer_list);auto *layer_delegate=new PainterLayerDelegate(layer_list);layer_list->setItemDelegate(layer_delegate);layer_list->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
  layer_delegate->renamed=[this](const QModelIndex &index,const QString &name){const QUuid layer(index.data(Qt::UserRole).toString()),effect(index.data(Qt::UserRole+10).toString());QTimer::singleShot(0,this,[this,layer,effect,name]{if(callbacks_.layer_item_renamed)callbacks_.layer_item_renamed(layer,effect,name);});};
  const auto mix_writer=[this](const QModelIndex &index)->std::function<void(double,int)> {
    const QUuid parent(index.data(Qt::UserRole).toString()),effect_id(index.data(Qt::UserRole+10).toString());
    const int channel=layer_channel_;
    if(effect_id.isNull())return [this,parent,channel](double opacity,int blend){
      if(opacity>=0&&callbacks_.layer_opacity_changed)callbacks_.layer_opacity_changed(parent,channel,opacity);
      if(blend>=0&&callbacks_.layer_blend_changed)callbacks_.layer_blend_changed(parent,channel,LayerBlend(blend));
    };
    const auto *state=static_cast<LayerEffectRowState *>(findChild<QObject *>("layerEffectRowState"));
    const auto found=state->values.find(effect_id);if(found==state->values.end()||(index.data(Qt::UserRole+24).isValid()&&!index.data(Qt::UserRole+24).toBool()))return {};
    const auto domain=found->second.index();
    const int kind=std::visit([](const auto &effect){return int(effect.kind);},found->second);
    return [this,parent,effect_id,domain,kind](double opacity,int blend){
      // Selection commits pending Properties edits and refreshes row metadata.
      // Read again for every popup value, preserving edits made since it opened.
      // No model index or old whole-effect snapshot survives that refresh.
      if(callbacks_.effect_selected)callbacks_.effect_selected(parent,effect_id);
      const auto *state=static_cast<LayerEffectRowState *>(findChild<QObject *>("layerEffectRowState"));
      const auto found=state->values.find(effect_id);
      if(found==state->values.end()||found->second.index()!=domain||std::visit([](const auto &effect){return int(effect.kind);},found->second)!=kind)return;
      auto value=found->second;
      std::visit([&](auto &effect){if(opacity>=0)effect.opacity=float(opacity);if(blend>=0)effect.blending=LayerBlend(blend);},value);
      if(const auto *mask=std::get_if<MaskEffect>(&value)){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(*mask);}
      else if(callbacks_.effect_changed)callbacks_.effect_changed(std::get<LevelsEffect>(value));
    };
  };
  const auto edit_opacity=[this,mix_writer](const QModelIndex &index) {
    if(!index.isValid()||index.data(Qt::UserRole+19).toBool())return;
    const auto publish=mix_writer(index);if(!publish)return;
    auto *menu=new QMenu(layers_);menu->setAttribute(Qt::WA_DeleteOnClose);
    auto *action=new QWidgetAction(menu);auto *panel=new QWidget(menu);auto *layout=column(panel);
    layout->setContentsMargins(12,10,12,10);layout->setSpacing(8);
    layout->addWidget(new QLabel(index.data(Qt::UserRole+10).toString().isEmpty()?layer_context_->currentText()+" opacity":"Effect opacity",panel));
    // Compact row percentages are integer controls; Properties retain NumericSpinBox precision.
    auto *spin=new QDoubleSpinBox(panel);spin->setLocale(bts::numeric_locale());spin->setGroupSeparatorShown(false);spin->setObjectName("layerChannelOpacity");spin->setAccessibleName("Opacity");
    spin->setRange(0,100);spin->setDecimals(0);spin->setSuffix(" %");spin->setKeyboardTracking(false);spin->setValue(index.data(Qt::UserRole+1).toDouble());
    auto *slider=new QSlider(Qt::Horizontal,panel);slider->setObjectName("layerChannelOpacitySlider");slider->setRange(0,1000);slider->setValue(qRound(index.data(Qt::UserRole+1).toDouble()*10));
    layout->addWidget(spin);layout->addWidget(slider);action->setDefaultWidget(panel);menu->addAction(action);
    connect(spin,&QDoubleSpinBox::valueChanged,menu,[publish,slider](double value){const QSignalBlocker blocker(slider);slider->setValue(qRound(value*10));publish(value/100,-1);});
    connect(slider,&QSlider::sliderPressed,menu,[this]{if(callbacks_.property_edit_started)callbacks_.property_edit_started("Opacity");});
    connect(slider,&QSlider::valueChanged,menu,[publish,spin](int value){const QSignalBlocker blocker(spin);spin->setValue(value/10.);publish(value/1000.,-1);});
    connect(spin,&QDoubleSpinBox::editingFinished,menu,[this,slider]{if(!slider->isSliderDown()&&callbacks_.property_edit_finished)callbacks_.property_edit_finished();});
    connect(slider,&QSlider::sliderReleased,menu,[this]{if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();});
    menu->popup(layers_->viewport()->mapToGlobal(layers_->visualRect(index).bottomRight()));spin->setFocus();spin->selectAll();
  };
  layer_delegate->effect_removed=[this](QUuid parent,QUuid effect){if(callbacks_.effect_removed)callbacks_.effect_removed(parent,effect);};
  layer_delegate->instance_selected=[this](QUuid id){if(callbacks_.instance_selected)callbacks_.instance_selected(id);};
  layer_delegate->geometry_selected=[this](QUuid id){if(callbacks_.geometry_selected)callbacks_.geometry_selected(id);};
  layer_delegate->target_selected=[this](QUuid id,bool mask){if(callbacks_.mask_selected)callbacks_.mask_selected(id,mask);};
  layer_delegate->mask_isolated=[this](QUuid id){if(callbacks_.mask_isolated)callbacks_.mask_isolated(id);};
  layer_delegate->mask_bypassed=[this](QUuid id){if(callbacks_.mask_bypassed)callbacks_.mask_bypassed(id);};
  layer_delegate->opacity_requested=edit_opacity;
  const auto edit_blend=[this,mix_writer](const QModelIndex &index) {
    if(!index.isValid()||index.data(Qt::UserRole+19).toBool())return;
    const auto publish=mix_writer(index);if(!publish)return;
    auto *menu=new QMenu(layers_);menu->setObjectName("layerBlendMenu");menu->setAttribute(Qt::WA_DeleteOnClose);menu->setToolTipsVisible(true);
    for(int mode=0;mode<int(layer_blend_names.size());++mode) {
      auto *action=menu->addAction(layer_blend_names[mode]);action->setObjectName(QString("layerBlend%1").arg(mode));action->setCheckable(true);action->setChecked(index.data(Qt::UserRole+5).toInt()==mode);if(index.data(Qt::UserRole+12).toBool()&&!index.data(Qt::UserRole+10).toString().isEmpty()&&index.data(Qt::UserRole+13).toInt()==int(MaskEffectKind::Group)&&mode==int(LayerBlend::Passthrough)){action->setEnabled(false);action->setToolTip("Mask Groups isolate their children over zero. Passthrough applies only to layer folders.");}
      connect(action,&QAction::triggered,menu,[publish,mode]{publish(-1,mode);});
    }
    menu->popup(layers_->viewport()->mapToGlobal(layers_->visualRect(index).topRight()));
  };
  layer_delegate->blend_requested=edit_blend;
  auto *blend_action=new QAction("Edit channel blending…",layers_);blend_action->setObjectName("editLayerBlend");
  connect(blend_action,&QAction::triggered,this,[this,edit_blend]{edit_blend(layers_->currentIndex());});
  layer_delegate->folder_toggled=[this](QUuid id){if(collapsed_folders_.contains(id))collapsed_folders_.remove(id);else collapsed_folders_.insert(id);if(callbacks_.layer_context_changed)callbacks_.layer_context_changed();};
  auto *collapse_action=new QAction("Expand / collapse folder",layers_);collapse_action->setObjectName("toggleLayerFolder");
  connect(collapse_action,&QAction::triggered,this,[this,layer_delegate]{if(layers_->currentItem()&&layers_->currentItem()->data(Qt::UserRole+3).toBool())layer_delegate->folder_toggled(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()));});
  auto *group_action=new QAction("Group layers",this);group_action->setObjectName("groupLayers");group_action->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_G));addAction(group_action);
  connect(group_action,&QAction::triggered,this,[this]{if(callbacks_.group_layers)callbacks_.group_layers();});
  auto *flatten_action=new QAction("Flatten layer/group",this);flatten_action->setObjectName("flattenLayer");flatten_action->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_M));addAction(flatten_action);
  connect(flatten_action,&QAction::triggered,this,[this]{if(callbacks_.flatten_layer)callbacks_.flatten_layer();});
  auto *export_flatten_action=new QAction("Export flattened groups to files…",this);export_flatten_action->setObjectName("exportFlattenedGroups");connect(export_flatten_action,&QAction::triggered,this,[this]{if(callbacks_.export_flattened)callbacks_.export_flattened(false);});
  auto *export_flatten_mask=new QAction("Export flattened mask…",this);export_flatten_mask->setObjectName("exportFlattenedMask");connect(export_flatten_mask,&QAction::triggered,this,[this]{if(callbacks_.export_flattened)callbacks_.export_flattened(true);});
  auto *opacity_action=new QAction("Edit channel opacity…",layers_);opacity_action->setObjectName("editLayerOpacity");
  connect(opacity_action,&QAction::triggered,this,[this,edit_opacity]{edit_opacity(layers_->currentIndex());});
  connect(layer_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
    if(!item)return;const QUuid effect(item->data(Qt::UserRole+10).toString());if(!effect.isNull()){if(callbacks_.effect_selected)callbacks_.effect_selected(QUuid(item->data(Qt::UserRole).toString()),effect);}else if(callbacks_.layer_selected)callbacks_.layer_selected(QUuid(item->data(Qt::UserRole).toString()));
  });
  connect(layer_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
    if(!item)return;const QUuid effect(item->data(Qt::UserRole+10).toString());if(!effect.isNull()){if(callbacks_.effect_visibility_changed)callbacks_.effect_visibility_changed(QUuid(item->data(Qt::UserRole).toString()),effect,item->checkState()==Qt::Checked);}else if(callbacks_.layer_visibility_changed)callbacks_.layer_visibility_changed(QUuid(item->data(Qt::UserRole).toString()),item->checkState()==Qt::Checked);
  });
  move_layer_up_ = new QAction("Move layer up", this);
  move_layer_up_->setObjectName("moveLayerUp");
  move_layer_down_ = new QAction("Move layer down", this);
  move_layer_down_->setObjectName("moveLayerDown");
  for (auto [action,offset] : {std::pair{move_layer_up_,1}, {move_layer_down_,-1}}) {
    connect(action, &QAction::triggered, this, [this,offset] {
      if (layers_->currentItem() && callbacks_.layer_moved)
        callbacks_.layer_moved(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()),offset);
    });
  }
  layer_list->copy_reference=[this]{if(copy_instance_->isEnabled()&&callbacks_.copy_layer_reference)callbacks_.copy_layer_reference();};layer_list->paste_instance=[this]{if(paste_instance_->isEnabled()&&callbacks_.paste_layer_instance)callbacks_.paste_layer_instance();};
  copy_instance_=new QAction("Copy layer reference",layers_);copy_instance_->setObjectName("copyLayerReference");copy_instance_->setShortcut(QKeySequence::Copy);copy_instance_->setShortcutContext(Qt::WidgetWithChildrenShortcut);layers_->addAction(copy_instance_);connect(copy_instance_,&QAction::triggered,this,[this]{if(callbacks_.copy_layer_reference)callbacks_.copy_layer_reference();});
  paste_instance_=new QAction("Paste layer instance",layers_);paste_instance_->setObjectName("pasteLayerInstance");paste_instance_->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_V));paste_instance_->setShortcutContext(Qt::WidgetWithChildrenShortcut);layers_->addAction(paste_instance_);connect(paste_instance_,&QAction::triggered,this,[this]{if(callbacks_.paste_layer_instance)callbacks_.paste_layer_instance();});
  auto *material_id_action=new QAction("Material ID color…",layers_);material_id_action->setObjectName("assignMaterialId");
  connect(material_id_action,&QAction::triggered,this,[this]{if(layers_->currentItem()&&callbacks_.material_id_requested)callbacks_.material_id_requested(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()));});
  layer_list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(layer_list, &QListWidget::customContextMenuRequested, this, [this,material_id_action,opacity_action,blend_action,collapse_action,group_action,flatten_action,export_flatten_action,export_flatten_mask](const QPoint &point) {
    const auto geometry_index=layers_->indexAt(point);
    if(geometry_index.isValid()&&geometry_index.data(Qt::UserRole+10).toString().isEmpty()&&!geometry_index.data(Qt::UserRole+19).toBool()&&layer_geometry_rect(geometry_index,layers_->visualRect(geometry_index)).contains(point)){
      auto *menu=new QMenu(layers_);menu->setAttribute(Qt::WA_DeleteOnClose);menu->setObjectName("geometryMaskContextMenu");const QUuid id(geometry_index.data(Qt::UserRole).toString());if(callbacks_.geometry_selected)callbacks_.geometry_selected(id);auto *copy=menu->addAction("Copy geometry mask");copy->setObjectName("copyGeometryMask");connect(copy,&QAction::triggered,menu,[this,id]{if(callbacks_.copy_geometry)callbacks_.copy_geometry(id);});auto *paste=menu->addAction("Paste into geometry mask");paste->setObjectName("pasteGeometryMask");paste->setEnabled(geometry_clipboard_available_);connect(paste,&QAction::triggered,menu,[this,id]{if(callbacks_.paste_geometry)callbacks_.paste_geometry(id);});for(bool include:{true,false}){auto *all=menu->addAction(include?"Include all":"Exclude all");all->setObjectName(include?"includeAllGeometry":"excludeAllGeometry");connect(all,&QAction::triggered,menu,[this,id,include]{if(callbacks_.geometry_all_changed)callbacks_.geometry_all_changed(id,include);});}menu->popup(layers_->viewport()->mapToGlobal(point));return;
    }
    if(auto *item=layers_->itemAt(point);item&&!item->isSelected())layers_->setCurrentItem(item);
    auto *menu = new QMenu(layers_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto index=layers_->indexAt(point);
    if(index.isValid()&&index.data(Qt::UserRole+6).toBool()&&layer_mask_rect(index,layers_->visualRect(index)).contains(point)) {
      menu->setObjectName("maskContextMenu");
      const QUuid id(index.data(Qt::UserRole).toString());if(callbacks_.mask_selected)callbacks_.mask_selected(id,true);
      menu->addActions(mask_button_->menu()->actions());menu->addSeparator();menu->addActions(effects_button_->menu()->actions());menu->addSeparator();menu->addAction(export_flatten_mask);
      menu->popup(layers_->viewport()->mapToGlobal(point));return;
    }
    if(index.isValid()&&!index.data(Qt::UserRole+10).toString().isEmpty()){menu->addAction(effect_up_);menu->addAction(effect_down_);auto *remove=menu->addAction("Remove effect");const QUuid parent(index.data(Qt::UserRole).toString()),effect(index.data(Qt::UserRole+10).toString());connect(remove,&QAction::triggered,menu,[this,parent,effect]{if(callbacks_.effect_removed)callbacks_.effect_removed(parent,effect);});menu->popup(layers_->viewport()->mapToGlobal(point));return;}
    if(index.isValid()&&index.data(Qt::UserRole+3).toBool()&&!index.data(Qt::UserRole+19).toBool()){const QUuid id(index.data(Qt::UserRole).toString());auto *folder_mat=menu->addAction("Save folder as Material…");folder_mat->setObjectName("createFolderMaterial");folder_mat->setEnabled(bool(callbacks_.create_folder_material));connect(folder_mat,&QAction::triggered,menu,[this,id]{if(callbacks_.create_folder_material)callbacks_.create_folder_material(id);});auto *preset=menu->addAction("Save folder as Material preset…");preset->setObjectName("createSmartMaterial");preset->setEnabled(bool(callbacks_.create_smart_material));connect(preset,&QAction::triggered,menu,[this,id]{if(callbacks_.create_smart_material)callbacks_.create_smart_material(id);});menu->addSeparator();}
    auto *adjustment=menu->addAction("Pass Through / Adjustment Layer (sample underlying layers)");
    adjustment->setObjectName("layerAdjustmentMode");adjustment->setCheckable(true);
    const QUuid adjustment_id(index.data(Qt::UserRole).toString());
    adjustment->setChecked(index.data(adjustment_checked_role).toBool());
    adjustment->setEnabled(index.isValid()&&layers_->isEnabled()&&layers_->selectedItems().size()==1&&
        index.data(adjustment_editable_role).toBool()&&!index.data(Qt::UserRole+8).toBool()&&
        !index.data(Qt::UserRole+17).toBool()&&!quick_mask_edit_&&bool(callbacks_.layer_adjustment_changed));
    connect(adjustment,&QAction::triggered,menu,[this,adjustment_id](bool checked){
      if(callbacks_.layer_adjustment_changed)callbacks_.layer_adjustment_changed(adjustment_id,checked);
    });
    menu->addSeparator();
    menu->addMenu(mask_button_->menu());menu->addSeparator();menu->addAction(copy_instance_);menu->addAction(paste_instance_);menu->addSeparator();if(!index.data(Qt::UserRole+19).toBool())menu->addAction(blend_action);menu->addAction(material_id_action);menu->addAction(opacity_action);menu->addAction(group_action);menu->addAction(flatten_action);menu->addAction(export_flatten_action);menu->addAction(export_flatten_mask);menu->addAction(collapse_action);
    menu->addSeparator();
    for(const auto &[label,update]:std::array<std::pair<const char *,bool>,2>{{{"Create Pre-compose Layer Here (Cache Below)",false},{"Update Pre-compose Cache",true}}}){
      auto *action=menu->addAction(label);action->setObjectName(update?"updatePrecomposeCache":"createPrecomposeHere");action->setEnabled(index.isValid()&&!index.data(Qt::UserRole+27).toBool()&&(!update||index.data(Qt::UserRole+28).toBool()));connect(action,&QAction::triggered,menu,[this,update]{if(callbacks_.precompose_layer)callbacks_.precompose_layer(update);});
    }
    auto *release=menu->addAction("Unfreeze / Release Pre-compose");release->setObjectName("releasePrecompose");release->setEnabled(index.data(Qt::UserRole+28).toBool()&&!index.data(Qt::UserRole+27).toBool());connect(release,&QAction::triggered,menu,[this]{if(callbacks_.release_precompose)callbacks_.release_precompose();});
    menu->addAction(move_layer_up_);
    menu->addAction(move_layer_down_);
    menu->popup(layers_->viewport()->mapToGlobal(point));
  });
  layer_settings_tabs_->addTab(layers, "Layers");
  auto *texture_page = new QSplitter(Qt::Vertical, layer_settings_tabs_);
  texture_page->setObjectName("textureSetsPage");
  texture_page->addWidget(texture_panel);
  layer_settings_tabs_->addTab(texture_page, "Texture Sets");
  auto *settings_scroll=new QScrollArea(layer_settings_tabs_);settings_scroll->setObjectName("textureSetSettingsScroll");settings_scroll->setWidgetResizable(true);settings_scroll->setFrameShape(QFrame::NoFrame);
  auto *settings = new QWidget(settings_scroll);settings_general_=settings;
  auto *scope_root=new QWidget(settings_scroll);auto *scope_layout=column(scope_root);scope_layout->setContentsMargins(0,0,0,0);settings_tile_=new QWidget(scope_root);auto *tile_layout=column(settings_tile_);tile_layout->setContentsMargins(10,10,10,10);tile_layout->addWidget(new QLabel("Size",settings_tile_));tile_resolution_=new TextureExtentComboBox(128,settings_tile_);tile_resolution_->setObjectName("uvTileResolution");tile_resolution_->insertItem(0,"1024 (Default)",0);tile_resolution_->setCurrentIndex(0);tile_resolution_->insertSeparator(1);tile_layout->addWidget(tile_resolution_);scope_layout->addWidget(settings_tile_);scope_layout->addWidget(settings);scope_layout->addStretch();settings_tile_->hide();settings_scroll->setWidget(scope_root);connect(tile_resolution_,&QComboBox::currentIndexChanged,this,[this](int index){if(active_tile_&&index>=0&&tile_resolution_->itemData(index).isValid()&&callbacks_.tile_resolution_changed)callbacks_.tile_resolution_changed(active_tile_,tile_resolution_->itemData(index).toInt());});
  auto *settings_layout = column(settings);
  settings_layout->setContentsMargins(10, 10, 10, 10);
  settings_layout->setSpacing(8);
  auto *resolution_label = new QLabel("Size", settings);
  resolution_label->setEnabled(true);
  settings_layout->addWidget(resolution_label);
  auto *resolution = new TextureExtentComboBox(128,settings);
  resolution_ = resolution;
  resolution->setObjectName("textureSetResolution");
  resolution->setEnabled(false);
  resolution->setToolTip("Evaluate this Texture Set at another size while retaining authored paint.");connect(resolution,&QComboBox::currentIndexChanged,this,[this](int index){if(index>=0&&resolution_->isEnabled()&&callbacks_.texture_resolution_changed)callbacks_.texture_resolution_changed(resolution_->itemData(index).toInt());});
  auto *dimensions=new QHBoxLayout;
  resolution_lock_=new QToolButton(settings);resolution_lock_->setObjectName("textureSetSizeLock");resolution_lock_->setCheckable(true);resolution_lock_->setChecked(true);resolution_lock_->setText("Link");resolution_lock_->setToolTip("Link width and height; linking sets height to width");
  resolution_height_=new TextureExtentComboBox(128,settings);resolution_height_->setObjectName("textureSetHeight");resolution_height_->setEnabled(false);
  dimensions->addWidget(resolution,1);dimensions->addWidget(resolution_lock_);dimensions->addWidget(resolution_height_,1);settings_layout->addLayout(dimensions);
  connect(resolution_lock_,&QToolButton::toggled,this,[this](bool linked){if(callbacks_.texture_size_linked)callbacks_.texture_size_linked(linked);});
  connect(resolution_height_,&QComboBox::currentIndexChanged,this,[this](int index){if(index>=0&&resolution_height_->isEnabled()&&callbacks_.texture_height_changed)callbacks_.texture_height_changed(resolution_height_->itemData(index).toInt());});
  auto *channels_header = new QWidget(settings);
  auto *channels_header_layout = row(channels_header);
  channels_header_layout->addWidget(new QLabel("CHANNELS", channels_header));
  channels_header_layout->addStretch();
  auto *configure_channels=new QAction("Add or remove channels…",channels_header);
  configure_channels->setObjectName("configureChannels");
  auto *configure_button=new QToolButton(channels_header);
  configure_button->setObjectName("configureChannelsButton");
  configure_button->setDefaultAction(configure_channels);
  channels_header_layout->addWidget(configure_button);
  auto *add_channel = new QToolButton(channels_header);
  add_channel->setText("+");
  add_channel->setObjectName("addChannel");
  add_channel->setToolTip("Add a supported material channel");
  add_channel->setPopupMode(QToolButton::InstantPopup);
  auto *channel_menu = new QMenu(add_channel);
  add_channel->setMenu(channel_menu);
  channels_header_layout->addWidget(add_channel);
  settings_layout->addWidget(channels_header);
  auto *channel_scroll = new QScrollArea(settings);
  channel_scroll->setWidgetResizable(true);
  channel_scroll->setFrameShape(QFrame::NoFrame);
  auto *channel_container = new QWidget(channel_scroll);
  auto *channel_layout = column(channel_container);
  for (int index = 0; index < 5; ++index) {
    channel_add_actions_[index] = channel_menu->addAction(channel_names[index]);
    channel_add_actions_[index]->setObjectName(QString("addChannel%1").arg(index));
    channel_add_actions_[index]->setEnabled(false);
    connect(channel_add_actions_[index], &QAction::triggered, this, [this, index] {
      if (callbacks_.channel_changed) callbacks_.channel_changed(index, true);
    });
    auto *channel_row = new QWidget(channel_container);
    channel_rows_[index] = channel_row;
    channel_row->setFixedHeight(30);
    auto *line = row(channel_row);
    line->addWidget(new QLabel(channel_names[index], channel_row), 1);
    auto *format = new QComboBox(channel_row);
    channel_formats_[index]=format;format->setObjectName(QString("channelPrecision%1").arg(index));for(auto name:channel_precision_names)format->addItem(name);
    format->setFixedWidth(74);
    format->setEnabled(true);format->setToolTip("Channel evaluation precision; original authoring values are retained. L formats evaluate luminance.");
    connect(format,&QComboBox::currentIndexChanged,this,[this,index](int format){if(callbacks_.channel_precision_changed)callbacks_.channel_precision_changed(index,format);});
    line->addWidget(format);
    auto *remove = new QToolButton(channel_row);
    remove->setText("−");
    remove->setObjectName(QString("removeChannel%1").arg(index));
    remove->setToolTip(QString("Remove %1; retain its authored data").arg(channel_names[index]));
    connect(remove, &QToolButton::clicked, this, [this, index] {
      if (callbacks_.channel_changed) callbacks_.channel_changed(index, false);
    });
    line->addWidget(remove);
    channel_layout->addWidget(channel_row);
    channel_row->hide();
  }
  ao_add_action_=channel_menu->addAction("Ambient occlusion");ao_add_action_->setObjectName("addChannel5");connect(ao_add_action_,&QAction::triggered,this,[this]{if(callbacks_.channel_changed)callbacks_.channel_changed(5,true);});
  ao_channel_row_=new QWidget(channel_container);ao_channel_row_->setFixedHeight(30);auto *ao_line=row(ao_channel_row_);ao_line->addWidget(new QLabel("Ambient occlusion",ao_channel_row_),1);auto *ao_format=new QComboBox(ao_channel_row_);channel_formats_[5]=ao_format;ao_format->setObjectName("channelPrecision5");for(auto name:channel_precision_names)ao_format->addItem(name);connect(ao_format,&QComboBox::currentIndexChanged,this,[this](int format){if(callbacks_.channel_precision_changed)callbacks_.channel_precision_changed(5,format);});ao_format->setFixedWidth(74);ao_line->addWidget(ao_format);auto *remove_ao=new QToolButton(ao_channel_row_);remove_ao->setText("−");remove_ao->setObjectName("removeChannel5");ao_line->addWidget(remove_ao);connect(remove_ao,&QToolButton::clicked,this,[this]{if(callbacks_.channel_changed)callbacks_.channel_changed(5,false);});channel_layout->addWidget(ao_channel_row_);ao_channel_row_->hide();
  scattering_add_=channel_menu->addAction("Scattering");scattering_add_->setObjectName("addScatteringChannel");
  scattering_add_->setToolTip("Intensity mask for Principled subsurface scattering; activate Subsurface Scattering in Display Settings");
  connect(scattering_add_,&QAction::triggered,this,[this]{
    if(callbacks_.scattering_added)callbacks_.scattering_added();
    QTimer::singleShot(0,this,[this]{for(int slot=0;slot<custom_channel_count;++slot)if(user_names_[slot]->isReadOnly()&&user_names_[slot]->text()=="Scattering"){
      findChild<QScrollArea *>("textureChannelsScroll")->ensureWidgetVisible(user_rows_[slot]);
      findChild<QScrollArea *>("textureSetSettingsScroll")->ensureWidgetVisible(user_rows_[slot]);
    }});
  });
  for(int slot=0;slot<custom_channel_count;++slot){
    user_add_[slot]=channel_menu->addAction(QString("User%1").arg(slot));user_add_[slot]->setObjectName(QString("addUserChannel%1").arg(slot));connect(user_add_[slot],&QAction::triggered,this,[this,slot]{if(callbacks_.user_channel_changed)callbacks_.user_channel_changed(slot,true);});
    auto *line=new QWidget(channel_container);user_rows_[slot]=line;line->setFixedHeight(30);auto *layout=row(line);user_names_[slot]=new QLineEdit(line);user_names_[slot]->setObjectName(QString("userChannelName%1").arg(slot));user_names_[slot]->setMaxLength(std::numeric_limits<int>::max());layout->addWidget(user_names_[slot],1);connect(user_names_[slot],&QLineEdit::editingFinished,this,[this,slot]{if(!user_names_[slot]->isReadOnly()&&callbacks_.user_renamed)callbacks_.user_renamed(slot,user_names_[slot]->text());});
    user_formats_[slot]=new QComboBox(line);user_formats_[slot]->setObjectName(QString("userChannelPrecision%1").arg(slot));for(auto name:channel_precision_names)user_formats_[slot]->addItem(name);user_formats_[slot]->setFixedWidth(74);layout->addWidget(user_formats_[slot]);connect(user_formats_[slot],&QComboBox::currentIndexChanged,this,[this,slot](int value){if(callbacks_.user_precision_changed)callbacks_.user_precision_changed(slot,value);});
    auto *remove=new QToolButton(line);remove->setText("−");remove->setObjectName(QString("removeUserChannel%1").arg(slot));layout->addWidget(remove);connect(remove,&QToolButton::clicked,this,[this,slot]{if(callbacks_.user_channel_changed)callbacks_.user_channel_changed(slot,false);});channel_layout->addWidget(line);line->hide();
  }
  channel_layout->addStretch();
  channel_scroll->setWidget(channel_container);
  channel_scroll->setObjectName("textureChannelsScroll");channel_scroll->setFixedHeight(170);settings_layout->addWidget(channel_scroll);
  displacement_panel_=new DisplacementPanel(settings);settings_layout->addWidget(displacement_panel_);
  displacement_panel_->channel_changed=[this](bool enabled){if(callbacks_.displacement_channel_changed)callbacks_.displacement_channel_changed(enabled);};
  displacement_panel_->changed=[this](const auto &state){if(callbacks_.displacement_changed)callbacks_.displacement_changed(state);};
  auto *normal_row=new QWidget(settings);normal_row->setObjectName("normalMixingRow");auto *normal_layout=row(normal_row);normal_layout->addWidget(new QLabel("Normal mixing",normal_row));normal_mixing_=new QComboBox(normal_row);normal_mixing_->setObjectName("normalMixing");normal_mixing_->setToolTip("How the tangent Normal map combines with the normal derived from grayscale Bump.");normal_mixing_->addItem("Replace",1);normal_mixing_->addItem("Combine",0);normal_layout->addWidget(normal_mixing_);settings_layout->addWidget(normal_row);normal_row->hide();connect(normal_mixing_,&QComboBox::currentIndexChanged,this,[this](int){if(callbacks_.normal_mixing_changed)callbacks_.normal_mixing_changed(normal_mixing_->currentData().toInt());});
  auto *ao_mixing_row=new QWidget(settings);auto *ao_mixing_layout=row(ao_mixing_row);ao_mixing_layout->addWidget(new QLabel("Ambient occlusion mixing",ao_mixing_row));ao_mixing_=new QComboBox(ao_mixing_row);ao_mixing_->setObjectName("ambientOcclusionMixing");ao_mixing_->addItem("Replace",1);ao_mixing_->addItem("Multiply",0);ao_mixing_layout->addWidget(ao_mixing_);settings_layout->addWidget(ao_mixing_row);connect(ao_mixing_,&QComboBox::currentIndexChanged,this,[this](int){if(callbacks_.ao_mixing_changed)callbacks_.ao_mixing_changed(ao_mixing_->currentData().toInt());});
  auto *bump_method_row=new QWidget(settings);auto *bump_method_layout=row(bump_method_row);bump_method_layout->addWidget(new QLabel("Bump to normal method",bump_method_row));bump_method_=new QComboBox(bump_method_row);bump_method_->setObjectName("heightNormalMethod");bump_method_->setToolTip("Converts grayscale Bump into a normal for shading. Displacement is separate and moves the surface.");bump_method_->addItem("Smooth (Sobel)",0);bump_method_->addItem("Sharp",1);bump_method_layout->addWidget(bump_method_);settings_layout->addWidget(bump_method_row);connect(bump_method_,&QComboBox::currentIndexChanged,this,[this](int){if(callbacks_.bump_method_changed)callbacks_.bump_method_changed(bump_method_->currentData().toInt());});
  auto *padding_row=new QWidget(settings);auto *padding_layout=row(padding_row);padding_layout->addWidget(new QLabel("UV padding",padding_row));
  padding_control_=new QComboBox(padding_row);padding_control_->setObjectName("uvPaddingMode");padding_control_->addItems({"2D Space Neighbor","3D Space Neighbor"});padding_control_->setCurrentIndex(1);padding_layout->addWidget(padding_control_);settings_layout->addWidget(padding_row);
  connect(padding_control_,&QComboBox::currentIndexChanged,this,[this](int index){if(callbacks_.padding_changed)callbacks_.padding_changed(index==1);});
  mesh_maps_panel_=new MeshMapsPanel(settings);settings_layout->addWidget(mesh_maps_panel_,1);mesh_maps_panel_->bake=[this]{if(callbacks_.bake_mesh_maps)callbacks_.bake_mesh_maps();};mesh_maps_panel_->remove=[this](int role){if(callbacks_.remove_mesh_map)callbacks_.remove_mesh_map(role);};mesh_maps_panel_->inspect=[this](int role){if(callbacks_.inspect_mesh_map)callbacks_.inspect_mesh_map(role);};auto *bake_action=new QAction("Bake Mesh Maps",this);bake_action->setObjectName("bakeMeshMapsAction");bake_action->setShortcut(QKeySequence(Qt::CTRL|Qt::SHIFT|Qt::Key_B));addAction(bake_action);connect(bake_action,&QAction::triggered,this,[this]{if(callbacks_.bake_mesh_maps)callbacks_.bake_mesh_maps();});
  settings_layout->addStretch();
  texture_page->addWidget(settings_scroll);
  texture_page->setStretchFactor(0, 1);
  texture_page->setStretchFactor(1, 1);
  texture_page->setSizes({260, 240});

  // Properties is a tab beside Layers and Texture Sets, and the tab that opens
  // first, instead of a second panel competing for the same column.
  properties_slot_=new QWidget(layer_settings_tabs_);
  properties_slot_->setObjectName("propertiesSlot");
  layer_settings_tabs_->addTab(properties_slot_,"Properties");
  layer_settings_tabs_->setCurrentWidget(properties_slot_);
  auto *properties_slot_layout=new QVBoxLayout(properties_slot_);
  properties_slot_layout->setContentsMargins(0,0,0,0);
  auto *properties = properties_panel_=new QWidget(properties_slot_);
  properties_slot_layout->addWidget(properties);
  properties->setObjectName("properties");
  properties->setMinimumHeight(properties_minimum_height);
  auto *properties_layout = column(properties);
  QToolButton *properties_close=nullptr;
  auto *properties_header=header(properties, "Properties", &properties_close);
  properties_layout->addWidget(properties_header);
  properties_close->setObjectName("propertiesClose");
  properties_close->setToolTip("Close properties");
  properties_close->setAccessibleName("Close properties");
  properties_undock_=new QToolButton(properties_header);
  properties_undock_->setObjectName("propertiesUndock");
  properties_undock_->setText(QString::fromUtf8("⧉"));
  properties_undock_->setToolTip("Undock properties");
  properties_undock_->setAccessibleName("Undock properties");
  properties_undock_->setFixedSize(22,22);
  static_cast<QHBoxLayout *>(properties_header->layout())->insertWidget(static_cast<QHBoxLayout *>(properties_header->layout())->indexOf(properties_close), properties_undock_);
  connect(properties_undock_,&QToolButton::clicked,this,[this]{if(properties_float_)dock_properties();else float_properties();});
  connect(properties_close,&QToolButton::clicked,this,[this]{if(properties_float_)dock_properties();hide_properties_tab();const QSignalBlocker block(properties_action_);properties_action_->setChecked(false);});
  connect(properties_action_,&QAction::toggled,this,[this](bool visible){if(visible){if(!properties_float_)show_properties_tab();}else{if(properties_float_)dock_properties();hide_properties_tab();}});
  properties->findChild<QLabel *>("dockTitle")->setObjectName("propertiesDockTitle");
  auto *properties_scroll = new QScrollArea(properties);
  properties_scroll->setObjectName("propertiesScroll");
  properties_scroll->setWidgetResizable(true);
  properties_scroll->setFrameShape(QFrame::NoFrame);
  auto *material = new QWidget(properties_scroll);
  material->setObjectName("materialProperties");
  auto *material_layout = column(material);material_body_=material;material_layout_=material_layout;
  material_layout->setAlignment(Qt::AlignTop);
  material_layout->setContentsMargins(8, 8, 8, 8);
  material_layout->setSpacing(6);
  auto *material_label = new QLabel("MATERIAL", material);
  material_label->setObjectName("sectionTitle");
  material_label_ = material_label;
  material_layout->addWidget(material_label);
  // The panel used to be one column of panels with no stated grouping, so it
  // read as an undifferentiated list. The controls are unchanged; they now sit
  // in named sections that hide themselves when empty. docs/design/ui.md §7.3.
  material_layout->setSpacing(bts::theme::SpaceSection);
  content_section_=make_section("Content",material);
  blending_section_=make_section("Blending",material,true);
  projection_section_=make_section("Projection",material,true);
  material_layout->addWidget(content_section_);
  material_layout->addWidget(blending_section_);
  material_layout->addWidget(projection_section_);
  levels_panel_=new LevelsPanel(material);levels_panel_->edit_started=[this]{if(callbacks_.property_edit_started)callbacks_.property_edit_started("Levels");};levels_panel_->edit_finished=[this]{if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();};levels_panel_->histogram_requested=[this](const TextureSet &set,const HistogramRequest &request){return callbacks_.histogram_requested?callbacks_.histogram_requested(set,request):0;};levels_panel_->histogram_result=[this](const QUuid &id){return callbacks_.histogram_result?callbacks_.histogram_result(id):std::nullopt;};levels_panel_->hide();content_section_->add(levels_panel_);levels_panel_->changed=[this](const LevelsEffect &effect){if(callbacks_.effect_changed)callbacks_.effect_changed(effect);};levels_panel_->component_selected=[this](QUuid id,int component){if(callbacks_.effect_component_selected)callbacks_.effect_component_selected(id,component);};
  auto *projection_modes=new QWidget(material);projection_modes->setObjectName("propertiesProjectionModes");auto *projection_modes_layout=row(projection_modes,0);
  for(int mode:{0,1}){auto *button=new QToolButton(projection_modes);button->setObjectName(mode==0?"propertiesUVProjection":"propertiesTriplanarProjection");button->setText(mode==0?"UV":"Triplanar");button->setCheckable(true);projection_modes_layout->addWidget(button);connect(button,&QToolButton::clicked,this,[this,mode]{auto projection=displayed_fill_;projection.mode=mode;edit_fill_projection(projection);});}projection_modes_layout->addStretch();projection_section_->add(projection_modes);projection_modes->hide();
  fill_panel_=new FillPanel(material);
  mask_fill_panel_=new MaskFillPanel(material);mask_fill_panel_->hide();content_section_->add(mask_fill_panel_);mask_fill_panel_->changed=[this](const MaskEffect &effect){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(effect);};mask_fill_panel_->anchor_requested=[this](QUuid id){if(callbacks_.anchor_requested)callbacks_.anchor_requested(id);};mask_fill_panel_->edit_started=[this]{if(callbacks_.property_edit_started)callbacks_.property_edit_started("Mask Fill");};mask_fill_panel_->edit_finished=[this]{if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();};mask_fill_panel_->texture_cancelled=[this](int channel){if(callbacks_.fill_texture_cancelled)callbacks_.fill_texture_cancelled(channel);};mask_fill_panel_->color_space_changed=[this](const QString &space){if(callbacks_.fill_color_space_changed)callbacks_.fill_color_space_changed(space);};
  imported_effect_panel_=new ImportedEffectPanel(material);imported_effect_panel_->hide();content_section_->add(imported_effect_panel_);imported_effect_panel_->content_changed=[this](const LevelsEffect &effect){if(callbacks_.effect_changed)callbacks_.effect_changed(effect);};imported_effect_panel_->mask_changed=[this](const MaskEffect &effect){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(effect);};imported_effect_panel_->child_selected=[this](const QUuid &id){if(layers_->currentItem()&&callbacks_.effect_selected)callbacks_.effect_selected(QUuid(layers_->currentItem()->data(Qt::UserRole).toString()),id);};
  mask_generator_panel_=new MaskGeneratorPanel(material);mask_generator_panel_->hide();content_section_->add(mask_generator_panel_);mask_generator_panel_->changed=[this](const MaskEffect &effect){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(effect);};mask_generator_panel_->content_changed=[this](const MaskEffect &value,int channel){LevelsEffect effect;effect.kind=ContentEffectKind::Generator;static_cast<MeshGeneratorSettings &>(effect)=value;effect.id=value.id;effect.enabled=value.enabled;effect.name=value.name;effect.channel=channel;effect.opacity=value.opacity;effect.blending=value.blending;if(callbacks_.effect_changed)callbacks_.effect_changed(effect);};mask_generator_panel_->input_requested=[this](int role,int tile){if(callbacks_.generator_input_requested)callbacks_.generator_input_requested(role,tile);};mask_generator_panel_->anchor_requested=[this](const QUuid &id){if(callbacks_.anchor_requested)callbacks_.anchor_requested(id);};mask_generator_panel_->bake_requested=[this]{if(callbacks_.bake_mesh_maps)callbacks_.bake_mesh_maps();};
  compare_mask_panel_=new CompareMaskPanel(material);compare_mask_panel_->histogram_requested=[this](const TextureSet &set,const HistogramRequest &request){return callbacks_.histogram_requested?callbacks_.histogram_requested(set,request):0;};compare_mask_panel_->histogram_result=[this](const QUuid &id){return callbacks_.histogram_result?callbacks_.histogram_result(id):std::nullopt;};compare_mask_panel_->hide();content_section_->add(compare_mask_panel_);compare_mask_panel_->changed=[this](const MaskEffect &effect){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(effect);};
  color_selection_panel_=new ColorSelectionPanel(material);color_selection_panel_->hide();content_section_->add(color_selection_panel_);color_selection_panel_->changed=[this](const MaskEffect &effect){if(callbacks_.mask_effect_changed)callbacks_.mask_effect_changed(effect);};color_selection_panel_->pick_changed=[this](bool active){if(callbacks_.id_pick_changed)callbacks_.id_pick_changed(active);};
  paint_effect_mix_=new QWidget(material);paint_effect_mix_->setObjectName("paintEffectMix");auto *paint_mix_layout=row(paint_effect_mix_,0);paint_effect_blend_=new QComboBox(paint_effect_mix_);paint_effect_blend_->setObjectName("paintEffectBlend");for(int i:{0,2,3,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24})paint_effect_blend_->addItem(layer_blend_names[i],i);paint_effect_blend_->setMinimumWidth(0);paint_effect_blend_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);paint_effect_opacity_=new bts::NumericSpinBox(paint_effect_mix_);paint_effect_opacity_->setObjectName("paintEffectOpacity");paint_effect_opacity_->setRange(0,100);paint_effect_opacity_->setSuffix(" %");paint_effect_opacity_->setKeyboardTracking(false);paint_mix_layout->addWidget(paint_effect_blend_,1);paint_mix_layout->addWidget(paint_effect_opacity_);blending_section_->add(paint_effect_mix_);paint_effect_mix_->hide();const auto paint_mix=[this]{if(callbacks_.paint_effect_mix_changed)callbacks_.paint_effect_mix_changed(paint_effect_opacity_->value()/100,LayerBlend(paint_effect_blend_->currentData().toInt()));};connect(paint_effect_opacity_,&QDoubleSpinBox::valueChanged,this,[paint_mix](double){paint_mix();});connect(paint_effect_blend_,&QComboBox::currentIndexChanged,this,[paint_mix](int i){if(i>=0)paint_mix();});
  filter_panel_=new FilterPanel(material);filter_panel_->edit_started=[this]{if(callbacks_.property_edit_started)callbacks_.property_edit_started("Filter");};filter_panel_->edit_finished=[this]{if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();};filter_panel_->hide();content_section_->add(filter_panel_);filter_panel_->changed=[this](const FilterSettings &filter,int channel,float opacity,LayerBlend blending){if(callbacks_.filter_changed)callbacks_.filter_changed(filter,channel,opacity,blending);};
  anchor_panel_=new AnchorPanel(material);anchor_panel_->hide();content_section_->add(anchor_panel_);anchor_panel_->renamed=[this](QUuid id,const QString &name){if(callbacks_.anchor_renamed)callbacks_.anchor_renamed(id,name);};anchor_panel_->consumer_requested=[this](QUuid parent,QUuid id){if(callbacks_.effect_selected)callbacks_.effect_selected(parent,id);};
  auto *linear_properties=new QLabel(material);linear_properties->setObjectName("linearColorProperties");linear_properties->setWordWrap(true);linear_properties->hide();content_section_->add(linear_properties);
  auto *folder_properties=new FolderPropertiesPanel(material);folder_properties->hide();content_section_->add(folder_properties);
  folder_properties->visibility_changed=[this](QUuid id,bool value){if(callbacks_.layer_visibility_changed)callbacks_.layer_visibility_changed(id,value);};
  folder_properties->opacity_changed=[this](QUuid id,int channel,double value){if(callbacks_.layer_opacity_changed)callbacks_.layer_opacity_changed(id,channel,value);};
  folder_properties->blend_changed=[this](QUuid id,int channel,LayerBlend value){if(callbacks_.layer_blend_changed)callbacks_.layer_blend_changed(id,channel,value);};
  instance_panel_=new LayerInstancePanel(material);instance_panel_->hide();content_section_->add(instance_panel_);instance_panel_->activated=[this](QUuid set,QUuid layer){if(callbacks_.instance_navigate)callbacks_.instance_navigate(set,layer);};instance_empty_=new QLabel("Material values are inherited from the source layer. Open the instance hierarchy to edit its source.",material);instance_empty_->setObjectName("instanceNoProperties");instance_empty_->setWordWrap(true);instance_empty_->hide();content_section_->add(instance_empty_);
  geometry_panel_=new GeometryMaskPanel(material);geometry_panel_->hide();content_section_->add(geometry_panel_);geometry_panel_->mesh_changed=[this](QUuid id,const QString &name,bool include){if(callbacks_.geometry_mesh_changed)callbacks_.geometry_mesh_changed(id,name,include);};geometry_panel_->type_changed=[this](QUuid id,int type){if(callbacks_.geometry_type_changed)callbacks_.geometry_type_changed(id,type);};geometry_panel_->tile_changed=[this](QUuid id,int tile,bool include){if(callbacks_.geometry_tile_changed)callbacks_.geometry_tile_changed(id,tile,include);};geometry_panel_->all_changed=[this](QUuid id,bool include){if(callbacks_.geometry_all_changed)callbacks_.geometry_all_changed(id,include);};
  property_content_=new QWidget(material);material_layout->addWidget(property_content_);
  // Without this, folding a section hands its height to the rows above it and
  // every gap in the panel grows. The slack belongs at the bottom.
  material_layout->addStretch();
  material_layout=column(property_content_);material_layout->setContentsMargins(0,0,0,0);
  fill_panel_->color_space_changed=[this](const QString &space){if(callbacks_.fill_color_space_changed)callbacks_.fill_color_space_changed(space);};
  fill_panel_->warp_panel()->selection_changed=[this](std::vector<int> ids){if(callbacks_.warp_selection_changed)callbacks_.warp_selection_changed(std::move(ids));};
  fill_panel_->warp_panel()->split_requested=[this](int mode){if(callbacks_.warp_split_requested)callbacks_.warp_split_requested(mode);};
  fill_panel_->warp_panel()->space_changed=[this](bool local){if(callbacks_.warp_space_changed)callbacks_.warp_space_changed(local);};fill_panel_->hide();material_layout->addWidget(fill_panel_);fill_panel_->edit_started=[this]{if(callbacks_.property_edit_started)callbacks_.property_edit_started("Fill mapping");};fill_panel_->edit_finished=[this]{if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();};fill_panel_->anchor_requested=[this](QUuid id){if(callbacks_.anchor_requested)callbacks_.anchor_requested(id);};fill_panel_->changed=[this](const FillProjection &p){if(callbacks_.fill_projection_changed)callbacks_.fill_projection_changed(p);};fill_panel_->texture_requested=[this](bool file,int){if(file){if(callbacks_.fill_texture_file_requested)callbacks_.fill_texture_file_requested();}else{set_shelf_visible(true);asset_scope_=-1;shelf_categories_->setCurrentRow(0);asset_search_->setText("u:texture");asset_search_->setFocus();refresh_alpha_shelf();}};mask_fill_panel_->texture_requested=fill_panel_->texture_requested;mask_fill_panel_->warp_panel()->selection_changed=fill_panel_->warp_panel()->selection_changed;mask_fill_panel_->warp_panel()->split_requested=fill_panel_->warp_panel()->split_requested;mask_fill_panel_->warp_panel()->space_changed=fill_panel_->warp_panel()->space_changed;mask_fill_panel_->files_dropped=[this](int channel,const QStringList &paths){if(callbacks_.fill_texture_files_dropped)callbacks_.fill_texture_files_dropped(channel,paths);};mask_fill_panel_->asset_dropped=[this](int channel,const QString &id){if(callbacks_.fill_texture_asset_dropped)callbacks_.fill_texture_asset_dropped(channel,id);};
  fill_panel_->material_requested=[this]{setProperty("fillMaterialPick",true);set_shelf_visible(true);asset_scope_=-1;if(shelf_categories_)shelf_categories_->setCurrentRow(0);asset_search_->setText("u:material");asset_search_->setFocus();refresh_alpha_shelf();};
  mask_value_row_=new QWidget(material);auto *gray_layout=row(mask_value_row_);
  gray_layout->addWidget(new QLabel("Grayscale",mask_value_row_));mask_value_=new bts::NumericSpinBox(mask_value_row_);
  mask_value_->setObjectName("maskGrayscale");mask_value_->setRange(0,1);mask_value_->setDecimals(3);mask_value_->setSingleStep(.05);mask_value_->setKeyboardTracking(false);gray_layout->addWidget(mask_value_);
  auto *invert_gray=new QToolButton(mask_value_row_);invert_gray->setText("Invert");gray_layout->addWidget(invert_gray);
  connect(invert_gray,&QToolButton::clicked,this,[this]{mask_value_->setValue(1-mask_value_->value());});
  connect(mask_value_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.mask_value_changed)callbacks_.mask_value_changed(value);});
  material_layout->addWidget(mask_value_row_);mask_value_row_->hide();
  polygon_controls_=make_polygon_controls(1,material);material_layout->addWidget(polygon_controls_);
  brush_controls_=new QWidget(material);auto *brush_layout=column(brush_controls_);
  brush_layout->setSpacing(bts::theme::SpaceSection);
  // The brush settings are the longest list in the panel, so they are where
  // folding earns its keep. Order is unchanged. docs/design/ui.md §7.2.
  auto *brush_section=make_section("Brush",brush_controls_);
  auto *jitter_section=make_section("Jitter",brush_controls_,true);
  auto *projection_section_brush=make_section("Projection",brush_controls_,true);
  auto *stencil_section=make_section("Stencil",brush_controls_,true);
  auto *channels_section=make_section("Channels",brush_controls_,true);
  auto *alignment_section=make_section("Alignment",brush_controls_,true);
  auto *alpha_section=make_section("Alpha",brush_controls_,true);
  for(auto *section:{brush_section,jitter_section,projection_section_brush,stencil_section,
                     channels_section,alignment_section,alpha_section})brush_layout->addWidget(section);
  brush_preview_=new BrushPreview(brush_controls_);brush_preview_->set_renderer([this](const BrushSettings &brush,std::function<void(QImage)> ready){if(callbacks_.brush_preview_requested)callbacks_.brush_preview_requested(brush,std::move(ready));else ready({});});brush_section->add(brush_preview_);
  auto *save_brush=new QPushButton("Save brush preset…",brush_controls_);save_brush->setObjectName("saveBrushPresetButton");brush_section->add(save_brush);connect(save_brush,&QPushButton::clicked,this,[this]{if(callbacks_.save_brush_preset)callbacks_.save_brush_preset();});
  auto *size_line=new QWidget(brush_controls_);auto *size_layout=row(size_line);size_layout->addWidget(new QLabel("Size",size_line));
  brush_size_=new bts::NumericSpinBox(size_line);brush_size_->setRange(.1,100);brush_size_->setDecimals(1);brush_size_->setValue(10);
  brush_size_->setKeyboardTracking(false);brush_size_->setObjectName("brushSize");size_layout->addWidget(brush_size_);brush_section->add(size_line);
  connect(brush_size_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_size_changed)callbacks_.brush_size_changed(value);});
  brush_pressure_[0]=tool(size_line,Glyph::Brush,"Use pen pressure for Size",true);
  brush_pressure_[0]->setCheckable(true);brush_pressure_[0]->setObjectName("brushPressureSize");size_layout->addWidget(brush_pressure_[0]);
  connect(brush_pressure_[0],&QToolButton::toggled,this,[this](bool value){if(callbacks_.brush_pressure_changed)callbacks_.brush_pressure_changed(0,value);});
  auto *minimum_size_line=new QWidget(brush_controls_);auto *minimum_size_layout=row(minimum_size_line);
  minimum_size_layout->addWidget(new QLabel("Minimum size (%)",minimum_size_line));
  brush_minimum_[0]=new bts::NumericSpinBox(minimum_size_line);brush_minimum_[0]->setRange(0,100);
  brush_minimum_[0]->setDecimals(1);brush_minimum_[0]->setValue(5);brush_minimum_[0]->setKeyboardTracking(false);
  brush_minimum_[0]->setObjectName("brushMinimumSize");minimum_size_layout->addWidget(brush_minimum_[0]);brush_section->add(minimum_size_line);
  connect(brush_minimum_[0],&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_minimum_changed)callbacks_.brush_minimum_changed(0,value);});
  auto *flow_line=new QWidget(brush_controls_);auto *flow_layout=row(flow_line);flow_layout->addWidget(new QLabel("Flow",flow_line));
  brush_flow_=new bts::NumericSpinBox(flow_line);brush_flow_->setRange(0,100);brush_flow_->setDecimals(1);brush_flow_->setValue(100);
  brush_flow_->setKeyboardTracking(false);brush_flow_->setObjectName("brushFlow");flow_layout->addWidget(brush_flow_);brush_section->add(flow_line);
  brush_flow_slider_=new QSlider(Qt::Horizontal,brush_controls_);brush_flow_slider_->setRange(0,1000);brush_flow_slider_->setValue(1000);
  brush_section->add(brush_flow_slider_);
  connect(brush_flow_slider_,&QSlider::valueChanged,this,[this](int value){brush_flow_->setValue(value/10.0);});
  connect(brush_flow_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_flow_changed)callbacks_.brush_flow_changed(value);});
  brush_pressure_[1]=tool(flow_line,Glyph::Brush,"Use pen pressure for Flow",true);
  brush_pressure_[1]->setCheckable(true);brush_pressure_[1]->setObjectName("brushPressureFlow");flow_layout->addWidget(brush_pressure_[1]);
  connect(brush_pressure_[1],&QToolButton::toggled,this,[this](bool value){if(callbacks_.brush_pressure_changed)callbacks_.brush_pressure_changed(1,value);});
  auto *minimum_flow_line=new QWidget(brush_controls_);auto *minimum_flow_layout=row(minimum_flow_line);
  minimum_flow_layout->addWidget(new QLabel("Minimum flow (%)",minimum_flow_line));
  brush_minimum_[1]=new bts::NumericSpinBox(minimum_flow_line);brush_minimum_[1]->setRange(0,100);
  brush_minimum_[1]->setDecimals(1);brush_minimum_[1]->setValue(5);brush_minimum_[1]->setKeyboardTracking(false);
  brush_minimum_[1]->setObjectName("brushMinimumFlow");minimum_flow_layout->addWidget(brush_minimum_[1]);brush_section->add(minimum_flow_line);
  connect(brush_minimum_[1],&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_minimum_changed)callbacks_.brush_minimum_changed(1,value);});
  auto *opacity_line=new QWidget(brush_controls_);auto *opacity_layout=row(opacity_line);opacity_layout->addWidget(new QLabel("Stroke opacity",opacity_line));
  brush_opacity_=new bts::NumericSpinBox(opacity_line);brush_opacity_->setRange(0,100);brush_opacity_->setDecimals(1);brush_opacity_->setValue(100);
  brush_opacity_->setKeyboardTracking(false);brush_opacity_->setObjectName("brushStrokeOpacity");opacity_layout->addWidget(brush_opacity_);brush_section->add(opacity_line);
  brush_opacity_slider_=new QSlider(Qt::Horizontal,brush_controls_);brush_opacity_slider_->setRange(0,1000);brush_opacity_slider_->setValue(1000);
  brush_section->add(brush_opacity_slider_);
  connect(brush_opacity_slider_,&QSlider::valueChanged,this,[this](int value){brush_opacity_->setValue(value/10.0);});
  connect(brush_opacity_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_opacity_changed)callbacks_.brush_opacity_changed(value);});
  auto *spacing_line=new QWidget(brush_controls_);auto *spacing_layout=row(spacing_line);spacing_layout->addWidget(new QLabel("Spacing",spacing_line));
  brush_spacing_=new bts::NumericSpinBox(spacing_line);brush_spacing_->setRange(1,500);brush_spacing_->setDecimals(1);brush_spacing_->setValue(20);
  brush_spacing_->setKeyboardTracking(false);brush_spacing_->setObjectName("brushSpacing");spacing_layout->addWidget(brush_spacing_);brush_section->add(spacing_line);
  brush_spacing_slider_=new QSlider(Qt::Horizontal,brush_controls_);brush_spacing_slider_->setRange(10,5000);brush_spacing_slider_->setValue(200);
  brush_section->add(brush_spacing_slider_);
  connect(brush_spacing_slider_,&QSlider::valueChanged,this,[this](int value){brush_spacing_->setValue(value/10.0);});
  connect(brush_spacing_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_spacing_changed)callbacks_.brush_spacing_changed(value);});
  auto *angle_line=new QWidget(brush_controls_);auto *angle_layout=row(angle_line);angle_layout->addWidget(new QLabel("Angle",angle_line));brush_angle_=new bts::NumericSpinBox(angle_line);brush_angle_->setObjectName("brushAngle");brush_angle_->setRange(-180,180);brush_angle_->setDecimals(1);brush_angle_->setKeyboardTracking(false);angle_layout->addWidget(brush_angle_);jitter_section->add(angle_line);
  connect(brush_angle_,&QDoubleSpinBox::valueChanged,this,[this](double value){if(callbacks_.brush_angle_changed)callbacks_.brush_angle_changed(value);});
  auto *follow_line=new QWidget(brush_controls_);auto *follow_layout=row(follow_line);follow_layout->addWidget(new QLabel("Follow Path",follow_line));brush_follow_path_=new QToolButton(follow_line);brush_follow_path_->setObjectName("brushFollowPath");brush_follow_path_->setCheckable(true);brush_follow_path_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);follow_layout->addWidget(brush_follow_path_);jitter_section->add(follow_line);
  connect(brush_follow_path_,&QToolButton::toggled,this,[this](bool enabled){if(callbacks_.brush_follow_path_changed)callbacks_.brush_follow_path_changed(enabled);});
  const std::array<const char *,4> jitter_labels{"Size Jitter","Flow Jitter","Angle Jitter","Position Jitter"};
  const std::array<const char *,4> jitter_ids{"brushSizeJitter","brushFlowJitter","brushAngleJitter","brushPositionJitter"};
  for(int i=0;i<4;++i){
    auto *line=new QWidget(brush_controls_);auto *layout=row(line);layout->addWidget(new QLabel(jitter_labels[i],line));
    auto *spin=new bts::NumericSpinBox(line);brush_jitter_[i]=spin;spin->setObjectName(jitter_ids[i]);spin->setAccessibleName(jitter_labels[i]);spin->setRange(0,i==2?180:100);spin->setDecimals(1);spin->setKeyboardTracking(false);layout->addWidget(spin);jitter_section->add(line);
    auto *slider=new QSlider(Qt::Horizontal,brush_controls_);brush_jitter_sliders_[i]=slider;slider->setRange(0,i==2?1800:1000);jitter_section->add(slider);
    connect(slider,&QSlider::valueChanged,this,[spin](int value){spin->setValue(value/10.);});
    connect(spin,&QDoubleSpinBox::valueChanged,this,[this,i,slider](double value){const QSignalBlocker blocker(slider);slider->setValue(qRound(value*10));if(callbacks_.brush_jitter_changed)callbacks_.brush_jitter_changed(i,value);});
  }
  projection_panel_=new ProjectionPanel(brush_controls_);projection_section_brush->add(projection_panel_);projection_panel_->changed=[this](const ProjectionMaterial &source){if(callbacks_.projection_material_changed)callbacks_.projection_material_changed(source);};projection_panel_->resource_selected=[this](bool weave){if(callbacks_.projection_resource_selected)callbacks_.projection_resource_selected(weave);};
  stencil_panel_=new StencilPanel(brush_controls_);stencil_section->add(stencil_panel_);
  stencil_panel_->changed=[this](const BrushStencil &settings){if(callbacks_.stencil_changed)callbacks_.stencil_changed(settings);};
  stencil_panel_->import_requested=[this]{if(callbacks_.stencil_import_requested)callbacks_.stencil_import_requested();};
  stencil_panel_->asset_requested=[this](QUuid id){return callbacks_.stencil_asset_requested&&callbacks_.stencil_asset_requested(id);};
  auto *channel_line=new QWidget(brush_controls_);auto *brush_channel_layout=row(channel_line);
  const char *labels[]={"color","metal","rough","nrm","bump"};
  for(int i=0;i<5;++i) {
    auto *button=new QToolButton(channel_line);button->setProperty("artistChannelToggle",true);brush_channels_[i]=button;button->setText(labels[i]);button->setToolTip(i==4?"Bump. Grayscale shading only; does not move the surface.":labels[i]);button->setCheckable(true);button->setChecked(i!=3);if(i==3)button->hide();
    button->setObjectName(QString("brushChannel%1").arg(i));brush_channel_layout->addWidget(button);
    connect(button,&QToolButton::toggled,this,[this,i](bool checked){if(callbacks_.brush_channel_changed)callbacks_.brush_channel_changed(i,checked);});
  }
  brush_ao_=new QToolButton(channel_line);brush_ao_->setObjectName("brushChannel5");brush_ao_->setProperty("artistChannelToggle",true);brush_ao_->setText("ao");brush_ao_->setCheckable(true);brush_ao_->hide();brush_channel_layout->addWidget(brush_ao_);connect(brush_ao_,&QToolButton::toggled,this,[this](bool enabled){if(callbacks_.brush_channel_changed)callbacks_.brush_channel_changed(5,enabled);});
  channels_section->add(channel_line);
  auto *user_buttons=new QWidget(brush_controls_);auto *user_grid=new QGridLayout(user_buttons);user_grid->setContentsMargins(0,0,0,0);user_grid->setSpacing(4);for(int slot=0;slot<custom_channel_count;++slot){auto *button=new QToolButton(user_buttons);user_brush_[slot]=button;button->setObjectName(QString("brushUserChannel%1").arg(slot));button->setCheckable(true);user_grid->addWidget(button,slot/4,slot%4);button->hide();connect(button,&QToolButton::toggled,this,[this,slot](bool value){if(callbacks_.user_brush_changed)callbacks_.user_brush_changed(slot,value);});}channels_section->add(user_buttons);

  auto *alignment_line=new QWidget(brush_controls_);auto *alignment_layout=row(alignment_line);alignment_layout->addWidget(new QLabel("Alignment",alignment_line));
  brush_alignment_=new QComboBox(alignment_line);brush_alignment_->setObjectName("brushAlignment");brush_alignment_->addItems({"Camera","Tangent | Wrap","Tangent | Planar","UV"});alignment_layout->addWidget(brush_alignment_);alignment_section->add(alignment_line);
  auto *cull_line=new QWidget(brush_controls_);auto *cull_layout=row(cull_line);
  brush_cull_=new QCheckBox("Backface culling",cull_line);brush_cull_->setObjectName("brushBackfaceCulling");cull_layout->addWidget(brush_cull_);
  brush_cull_angle_=new bts::NumericSpinBox(cull_line);brush_cull_angle_->setObjectName("brushCullingAngle");brush_cull_angle_->setRange(0,180);brush_cull_angle_->setDecimals(1);brush_cull_angle_->setKeyboardTracking(false);cull_layout->addWidget(brush_cull_angle_);alignment_section->add(cull_line);
  brush_cull_slider_=new QSlider(Qt::Horizontal,brush_controls_);brush_cull_slider_->setObjectName("brushCullingAngleSlider");brush_cull_slider_->setRange(0,1800);alignment_section->add(brush_cull_slider_);
  alignment_section->add(new QLabel("Size Space: Object",brush_controls_));
  const auto projection_changed=[this]{if(callbacks_.brush_projection_changed)callbacks_.brush_projection_changed(brush_alignment_->currentIndex(),brush_cull_->isChecked(),brush_cull_angle_->value());};
  connect(brush_alignment_,&QComboBox::currentIndexChanged,this,projection_changed);connect(brush_cull_,&QCheckBox::toggled,this,projection_changed);
  connect(brush_cull_angle_,&QDoubleSpinBox::valueChanged,this,projection_changed);connect(brush_cull_slider_,&QSlider::valueChanged,this,[this](int value){brush_cull_angle_->setValue(value/10.);});
  alpha_panel_=new AlphaPanel(brush_controls_);alpha_section->add(alpha_panel_);
  alpha_panel_->dynamic_changed=[this](const DynamicStrokeParameters &value){if(callbacks_.dynamic_stroke_changed)callbacks_.dynamic_stroke_changed(value);};
  alpha_panel_->cleared=[this]{BrushAlpha alpha;alpha.kind=AlphaKind::Uniform;if(callbacks_.alpha_selected)callbacks_.alpha_selected(alpha);};
  alpha_panel_->import_requested=[this]{if(callbacks_.alpha_import_requested)callbacks_.alpha_import_requested();};
  alpha_panel_->parameters_changed=[this](bool invert,double uniform,double hardness){if(callbacks_.alpha_parameters_changed)callbacks_.alpha_parameters_changed(invert,uniform,hardness);};
  // The field is the slider now: drag across it to change the value, click it
  // to type one. The separate track under each field goes away, which is a line
  // of height per property. docs/design/ui.md §7.4.
  brush_layout->addStretch();
  // Some of these are held as QDoubleSpinBox, so ask rather than assume.
  const auto as_track=[](QDoubleSpinBox *field){if(auto *numeric=dynamic_cast<bts::NumericSpinBox *>(field))numeric->set_track(true);};
  as_track(brush_flow_);as_track(brush_opacity_);as_track(brush_spacing_);as_track(brush_cull_angle_);
  for(auto *field:brush_jitter_)as_track(field);
  for(auto *track:{brush_flow_slider_,brush_opacity_slider_,brush_spacing_slider_,brush_cull_slider_})if(track)track->hide();
  for(auto *track:brush_jitter_sliders_)if(track)track->hide();
  // The pen-pressure buttons made their rows' fields narrower than the rest, so
  // the values did not line up down the panel.
  // The brush rows sit inside the sections now, so each section's contents is
  // its own container for the pass. ui.md §7.3.
  std::vector<QWidget *> row_groups{brush_controls_};
  for(auto *section:property_sections_)row_groups.push_back(section->content());
  align_property_rows(row_groups,28);
  material_layout->addWidget(brush_controls_);brush_controls_->hide();
  color_row_ = new QWidget(material);
  auto *color_layout = column(color_row_);
  // Label on the left, a small filled swatch on the right, the way every other
  // property row in the panel is shaped. Krita, Substance Painter and Blender
  // all show a brush colour as a swatch and keep the numbers inside the picker;
  // a panel-wide button with the hex printed on it read as a button and cost a
  // caption line of its own. docs/design/ui.md §7.5.
  auto *color_line=new QWidget(color_row_);auto *color_row_layout=row(color_line);
  auto *color_caption=new QLabel("Base Color", color_line);color_caption->setProperty("fillCaption", true);color_caption->setToolTip("Scene-linear ACEScg");color_row_layout->addWidget(color_caption);color_row_layout->addStretch();
  color_picker_=new ColorPicker(color_line);color_picker_->setObjectName("materialColorPicker");color_picker_->set_compact();
  color_controls_=color_picker_->linear_fields();color_row_layout->addWidget(color_picker_);color_row_layout->addSpacing(bts::theme::RemoveButtonPx+bts::theme::Space);
  color_layout->addWidget(color_line);
  color_picker_->started=[this]{if(callbacks_.color_started)callbacks_.color_started();};
  color_picker_->changed=[this](const auto &value){if(callbacks_.color_changed)callbacks_.color_changed(value);};
  color_picker_->finished=[this]{if(callbacks_.color_finished)callbacks_.color_finished();};
  color_picker_->pick_requested=[this]{if(callbacks_.color_pick_requested)callbacks_.color_pick_requested();};
  secondary_color_row_=new QWidget(color_row_);auto *secondary_layout=row(secondary_color_row_);auto *secondary_caption=new QLabel("Secondary",secondary_color_row_);secondary_caption->setProperty("fillCaption",true);secondary_caption->setToolTip("Painted while Ctrl is held");secondary_layout->addWidget(secondary_caption);secondary_layout->addStretch();secondary_color_picker_=new ColorPicker(secondary_color_row_);secondary_color_picker_->setObjectName("secondaryColorPicker");secondary_color_picker_->set_compact();for(int i=0;i<3;++i)secondary_color_picker_->linear_fields()[i]->setObjectName(QString("secondaryColor%1").arg(i));secondary_layout->addWidget(secondary_color_picker_);
  // Swapping the pair is one click in Krita and in Photoshop, and the two
  // colours are useless to compare while they sit in different panels.
  auto *swap_colors=new QToolButton(secondary_color_row_);swap_colors->setObjectName("swapBrushColors");swap_colors->setText("⇄");swap_colors->setToolTip("Swap base and secondary colour");swap_colors->setFixedSize(bts::theme::RemoveButtonPx,bts::theme::SwatchHeightPx);secondary_layout->addWidget(swap_colors);
  connect(swap_colors,&QToolButton::clicked,this,[this]{const auto base=color_picker_->color(),other=secondary_color_picker_->color();color_picker_->accept_sample(other);secondary_color_picker_->accept_sample(base);});
  color_layout->addWidget(secondary_color_row_);
std::array<float,4> secondary{1,1,1,1};{QSettings settings("mPainter","Preferences");const auto saved=settings.value("Brush/secondaryColor").toList();if(saved.size()==4)for(int i=0;i<4;++i){const auto value=saved[i].toDouble();if(std::isfinite(value)&&value>=0&&value<=65504)secondary[i]=value;}}secondary_color_picker_->set_color(secondary);if(callbacks_.secondary_color_changed)callbacks_.secondary_color_changed(secondary);secondary_color_picker_->changed=[this](const auto &value){if(callbacks_.secondary_color_changed)callbacks_.secondary_color_changed(value);};secondary_color_picker_->finished=[this]{QVariantList values;for(float value:secondary_color_picker_->color())values.append(value);QSettings settings("mPainter","Preferences");settings.setValue("Brush/secondaryColor",values);};secondary_color_picker_->pick_requested=[this]{if(callbacks_.secondary_pick_requested)callbacks_.secondary_pick_requested();};
  material_layout->addWidget(color_row_);
  color_row_->hide();
  metallic_row_ = new QWidget(material);
  auto *metallic_layout = new QVBoxLayout(metallic_row_);
  metallic_layout->setContentsMargins(0,0,0,0);
  metallic_layout->setSpacing(0);
  auto *metallic_caption=new QLabel("Metallic", metallic_row_);
  metallic_caption->setProperty("fillCaption", true);
  auto *metallic_value=new QHBoxLayout;metallic_value->setContentsMargins(0,0,0,0);metallic_value->addWidget(metallic_caption);
  metallic_control_ = new bts::NumericSpinBox(metallic_row_);
  metallic_control_->setObjectName("metallicControl");
  metallic_control_->setLocale(QLocale::c());
  metallic_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  metallic_control_->setAlignment(Qt::AlignRight);
  metallic_control_->setRange(0,1);
  metallic_control_->setDecimals(3);
  metallic_control_->setSingleStep(.01);
  metallic_control_->setKeyboardTracking(false);
  metallic_control_->setMinimumWidth(0);
  metallic_value->addWidget(metallic_control_,1);
  metallic_layout->addLayout(metallic_value);
  auto *metallic_slider=new QSlider(Qt::Horizontal, metallic_row_);
  metallic_slider->setObjectName("metallicSlider");
  metallic_slider->setRange(0,1000);
  metallic_layout->addWidget(metallic_slider);
  auto *metallic_gradient=new FillGradient(metallic_slider, metallic_control_, metallic_row_);
  metallic_gradient->setObjectName("fillGradientMetallic");
  metallic_layout->addWidget(metallic_gradient);
  metallic_control_->setProperty("fillHideInFill", true);
  metallic_slider->setProperty("fillHideInFill", true);
  connect(metallic_slider,&QSlider::valueChanged,this,[this](int value){metallic_control_->setValue(value/1000.);});
  connect(metallic_control_, &QDoubleSpinBox::valueChanged, this, [this,metallic_slider](double value) {
    const QSignalBlocker block(metallic_slider);metallic_slider->setValue(qRound(value*1000));
    if (callbacks_.metallic_changed) callbacks_.metallic_changed(value);
  });
  material_layout->addWidget(metallic_row_);
  metallic_row_->hide();
  auto *roughness_row = new QWidget(material);
  roughness_row_ = roughness_row;
  auto *roughness_layout = row(roughness_row, 0);
  auto *roughness_label = new QLabel("Roughness", roughness_row);
  roughness_label->setProperty("fillCaption", true);
  roughness_layout->addWidget(roughness_label);
  roughness_control_ = new bts::NumericSpinBox(roughness_row);
  roughness_control_->setObjectName("roughnessControl");
  roughness_control_->setLocale(QLocale::c());
  roughness_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);
  roughness_control_->setAlignment(Qt::AlignRight);
  roughness_control_->setRange(0, 1);
  roughness_control_->setDecimals(3);
  roughness_control_->setSingleStep(0.01);
  roughness_control_->setMinimumWidth(0);
  roughness_control_->setKeyboardTracking(false);
  roughness_control_->setAccessibleName("Material roughness");
  roughness_control_->setEnabled(false);
  roughness_control_->setToolTip("No editable material selected.");
  roughness_label->setBuddy(roughness_control_);
  roughness_layout->addWidget(roughness_control_,1);
  material_layout->addWidget(roughness_row);
  roughness_slider_ = new QSlider(Qt::Horizontal, material);
  roughness_slider_->setObjectName("roughnessSlider");
  roughness_slider_->setRange(0, 1000);
  roughness_slider_->setSingleStep(10);
  roughness_slider_->setPageStep(100);
  roughness_slider_->setAccessibleName("Material roughness");
  roughness_slider_->setEnabled(false);
  roughness_slider_->setToolTip("No editable material selected.");
  material_layout->addWidget(roughness_slider_);
  roughness_row_->setProperty("fillHideInFill", true);
  roughness_slider_->setProperty("fillHideInFill", true);
  auto *roughness_gradient=new FillGradient(roughness_slider_, roughness_control_, material);
  roughness_gradient->setObjectName("fillGradientRoughness");
  material_layout->addWidget(roughness_gradient);
  normal_label_ = new QLabel("Normal", material);
  normal_label_->setProperty("fillCaption", true);
  material_layout->addWidget(normal_label_);
  normal_row_=new QWidget(material);auto *normal_box=column(normal_row_);auto *tangent_bar=new TangentBumpBar(normal_row_);normal_box->addWidget(tangent_bar);auto *tangent_values=new QWidget(normal_row_);tangent_values->setObjectName("tangentBumpValues");tangent_values->hide();auto *normal_inputs=row(tangent_values);normal_box->addWidget(tangent_values);for(int component=0;component<3;++component){auto *control=new bts::NumericSpinBox(tangent_values);control->setObjectName(QString("materialNormal%1").arg(component));control->setLocale(QLocale::c());control->setMinimumWidth(0);control->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);control->setRange(-1,1);control->setDecimals(3);control->setSingleStep(.05);control->setPrefix(QString("%1 ").arg(QLatin1Char("XYZ"[component])));normal_values_[component]=control;normal_inputs->addWidget(control);connect(control,&QDoubleSpinBox::valueChanged,this,[this,component,tangent_bar](double value){tangent_bar->set_normal({float(normal_values_[0]->value()),float(normal_values_[1]->value()),float(normal_values_[2]->value())});if(callbacks_.normal_changed)callbacks_.normal_changed(component,value);});}tangent_bar->edited=[this](int component,double value){normal_values_[component]->setValue(value);};material_layout->addWidget(normal_row_);normal_row_->hide();
  auto *bump_editor=new QWidget(material);bump_editor->setObjectName("bumpEditor");auto *bump_box=column(bump_editor);auto *bump_line=new QWidget(bump_editor);auto *bump_value_line=row(bump_line,0);bump_label_=new QLabel("Bump",bump_line);bump_label_->setProperty("fillCaption",true);bump_value_line->addWidget(bump_label_);bump_control_=new bts::NumericSpinBox(bump_line);bump_control_->setObjectName("materialBump");bump_control_->setLocale(QLocale::c());bump_control_->setButtonSymbols(QAbstractSpinBox::NoButtons);bump_control_->setAlignment(Qt::AlignRight);bump_control_->setRange(-1,1);bump_control_->setDecimals(3);bump_control_->setSingleStep(.01);bump_control_->setMinimumWidth(0);bump_control_->setKeyboardTracking(false);bump_value_line->addWidget(bump_control_,1);bump_box->addWidget(bump_line);auto *bump_slider=new QSlider(Qt::Horizontal,bump_editor);bump_slider->setObjectName("bumpSlider");bump_slider->setRange(-1000,1000);bump_box->addWidget(bump_slider);auto *bump_gradient=new FillGradient(bump_slider, bump_control_, bump_editor);bump_gradient->setObjectName("fillGradientBump");bump_gradient->setToolTip("Bump controls surface detail. Native Normal Map and Bump chains remain normals in the viewport and are combined into one grayscale Bump texture at export.");bump_box->addWidget(bump_gradient);bump_control_->setProperty("fillHideInFill", true);bump_slider->setProperty("fillHideInFill", true);material_layout->addWidget(bump_editor);bump_editor->hide();connect(bump_slider,&QSlider::valueChanged,this,[this](int value){bump_control_->setValue(value/1000.);});connect(bump_control_,&QDoubleSpinBox::valueChanged,this,[this,bump_slider](double value){const QSignalBlocker block(bump_slider);bump_slider->setValue(qRound(value*1000));if(callbacks_.bump_changed)callbacks_.bump_changed(value);});
  auto *disp_row=new QWidget(material);disp_row->setObjectName("materialDisplacementRow");auto *disp_layout=column(disp_row);auto *disp_line=new QHBoxLayout;disp_line->setContentsMargins(0,0,0,0);auto *disp_toggle=new QToolButton(channel_line);disp_toggle->setObjectName("brushDisplacement");disp_toggle->setProperty("artistChannelToggle",true);disp_toggle->setText("disp");disp_toggle->setToolTip("Displacement. Moves the surface. Separate from grayscale Bump.");disp_toggle->setCheckable(true);brush_channel_layout->insertWidget(brush_channel_layout->indexOf(brush_ao_),disp_toggle);disp_line->addWidget(new QLabel("Displacement",disp_row));auto *disp_value=new bts::NumericSpinBox(disp_row);disp_value->setObjectName("materialDisplacement");disp_value->setLocale(QLocale::c());disp_value->setButtonSymbols(QAbstractSpinBox::NoButtons);disp_value->setAlignment(Qt::AlignRight);disp_value->setRange(-1,1);disp_value->setDecimals(4);disp_value->setSingleStep(.05);disp_value->setKeyboardTracking(false);disp_value->setMinimumWidth(0);disp_line->addWidget(disp_value,1);disp_layout->addLayout(disp_line);auto *disp_slider=new QSlider(Qt::Horizontal,disp_row);disp_slider->setObjectName("displacementSlider");disp_slider->setRange(-1000,1000);disp_layout->addWidget(disp_slider);auto *disp_gradient=new FillGradient(disp_slider, disp_value, disp_row);disp_gradient->setObjectName("fillGradientDisplacement");disp_gradient->setToolTip("Displacement. Moves the surface. Separate from grayscale Bump.");disp_layout->addWidget(disp_gradient);disp_value->setProperty("fillHideInFill", true);disp_slider->setProperty("fillHideInFill", true);material_layout->addWidget(disp_row);disp_row->hide();connect(disp_toggle,&QToolButton::toggled,this,[this](bool enabled){if(callbacks_.displacement_brush_changed)callbacks_.displacement_brush_changed(enabled);});connect(disp_slider,&QSlider::valueChanged,disp_value,[disp_value](int value){disp_value->setValue(value/1000.);});connect(disp_value,&QDoubleSpinBox::valueChanged,this,[this,disp_slider](double value){const QSignalBlocker block(disp_slider);disp_slider->setValue(qRound(value*1000));if(callbacks_.displacement_value_changed)callbacks_.displacement_value_changed(value);});
  normal_label_->hide();normal_row_->hide();
  bump_label_->hide();
  ao_value_row_=new QWidget(material);auto *ao_box=column(ao_value_row_);auto *ao_caption=new QLabel("Ambient occlusion",ao_value_row_);ao_caption->setProperty("fillCaption", true);auto *ao_value_line=new QHBoxLayout;ao_value_line->setContentsMargins(0,0,0,0);ao_value_line->addWidget(ao_caption);ao_value_=new bts::NumericSpinBox(ao_value_row_);ao_value_->setObjectName("materialAmbientOcclusion");ao_value_->setLocale(QLocale::c());ao_value_->setButtonSymbols(QAbstractSpinBox::NoButtons);ao_value_->setAlignment(Qt::AlignRight);ao_value_->setRange(0,1);ao_value_->setDecimals(3);ao_value_->setSingleStep(.05);ao_value_->setValue(1);ao_value_->setMinimumWidth(0);ao_value_->setKeyboardTracking(false);ao_value_line->addWidget(ao_value_,1);ao_box->addLayout(ao_value_line);auto *ao_slider=new QSlider(Qt::Horizontal,ao_value_row_);ao_slider->setObjectName("ambientOcclusionSlider");ao_slider->setRange(0,1000);ao_slider->setValue(1000);ao_box->addWidget(ao_slider);auto *ao_gradient=new FillGradient(ao_slider, ao_value_, ao_value_row_);ao_gradient->setObjectName("fillGradientAmbientOcclusion");ao_box->addWidget(ao_gradient);ao_value_->setProperty("fillHideInFill", true);ao_slider->setProperty("fillHideInFill", true);material_layout->addWidget(ao_value_row_);ao_value_row_->hide();connect(ao_slider,&QSlider::valueChanged,this,[this](int value){ao_value_->setValue(value/1000.);});connect(ao_value_,&QDoubleSpinBox::valueChanged,this,[this,ao_slider](double value){const QSignalBlocker block(ao_slider);ao_slider->setValue(qRound(value*1000));if(callbacks_.ao_changed)callbacks_.ao_changed(value);});
  for(int slot=0;slot<custom_channel_count;++slot){auto *panel=new QWidget(material);user_value_rows_[slot]=panel;auto *layout=column(panel);auto *label=new QLabel(panel);label->setProperty("fillCaption", true);user_value_labels_[slot]=label;auto *line=new QHBoxLayout;line->addWidget(label);user_fill_enabled_[slot]=new QToolButton(panel);user_fill_enabled_[slot]->setObjectName(QString("fillUserEnabled%1").arg(slot));user_fill_enabled_[slot]->setCheckable(true);user_fill_enabled_[slot]->setText("Enabled");line->addWidget(user_fill_enabled_[slot]);connect(user_fill_enabled_[slot],&QToolButton::toggled,this,[this,slot](bool value){if(callbacks_.user_brush_changed)callbacks_.user_brush_changed(slot,value);});auto *value=new bts::NumericSpinBox(panel);user_values_[slot]=value;value->setObjectName(QString("userChannelValue%1").arg(slot));value->setLocale(QLocale::c());value->setButtonSymbols(QAbstractSpinBox::NoButtons);value->setAlignment(Qt::AlignRight);value->setRange(0,1);value->setDecimals(3);value->setSingleStep(.01);value->setKeyboardTracking(false);line->addWidget(value,1);auto *invert=new QToolButton(panel);invert->setObjectName(QString("invertUserValue%1").arg(slot));invert->setProperty("fillHideInFill", true);invert->setText("↔");invert->setToolTip("Invert channel value");connect(invert,&QToolButton::clicked,this,[value]{value->setValue(1-value->value());});line->addWidget(invert);layout->addLayout(line);auto *slider=new QSlider(Qt::Horizontal,panel);user_sliders_[slot]=slider;slider->setRange(0,1000);layout->addWidget(slider);auto *gradient=new FillGradient(slider, value, panel);gradient->setObjectName(QString("fillGradientUser%1").arg(slot));layout->addWidget(gradient);value->setProperty("fillHideInFill", true);slider->setProperty("fillHideInFill", true);connect(slider,&QSlider::valueChanged,this,[value](int v){value->setValue(v/1000.);});connect(value,&QDoubleSpinBox::valueChanged,this,[this,slot,slider](double v){const QSignalBlocker block(slider);slider->setValue(qRound(v*1000));if(callbacks_.user_value_changed)callbacks_.user_value_changed(slot,v);});
    connect(slider,&QSlider::sliderPressed,this,[this,slot,slider]{
      if(!slider->property("userFillPropertyEdit").toBool())return;
      slider->setProperty("userFillPropertyDragging",true);
      if(callbacks_.property_edit_started)callbacks_.property_edit_started(QString("User channel %1 value").arg(slot+1));
    });
    connect(slider,&QSlider::sliderReleased,this,[this,slider]{
      if(!slider->property("userFillPropertyDragging").toBool())return;
      slider->setProperty("userFillPropertyDragging",false);
      if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();
    });
    connect(value,&QDoubleSpinBox::editingFinished,this,[this,slider]{
      if(slider->isSliderDown()||!slider->property("userFillPropertyEdit").toBool())return;
      if(callbacks_.property_edit_finished)callbacks_.property_edit_finished();
    });
    material_layout->addWidget(panel);panel->hide();}
  fill_panel_->register_uniform_controls(0,{color_row_});fill_panel_->register_uniform_controls(1,{metallic_row_});fill_panel_->register_uniform_controls(2,{roughness_row_,roughness_slider_,roughness_gradient});fill_panel_->register_uniform_controls(3,{normal_label_,normal_row_});fill_panel_->register_uniform_controls(4,{bump_editor});fill_panel_->register_uniform_controls(5,{ao_value_row_});for(int slot=0;slot<8;++slot)fill_panel_->register_uniform_controls(slot+6,{user_value_rows_[slot]});fill_panel_->register_uniform_controls(14,{disp_row});
  // The field is the slider for the material channels too, the way it already
  // is for the brush: drag across it to change the value, click it to type one.
  // The separate track under each caption goes away, which is a line of height
  // per channel. The gesture still runs through the hidden slider, so undo
  // grouping and the live material preview are untouched. docs/design/ui.md §7.4.
  std::vector<std::pair<QDoubleSpinBox *,QSlider *>> channel_rows{
    {metallic_control_,metallic_slider},{roughness_control_,roughness_slider_},
    {bump_control_,bump_slider},{ao_value_,ao_slider},{disp_value,disp_slider}};
  for(int slot=0;slot<custom_channel_count;++slot)channel_rows.push_back({user_values_[slot],user_sliders_[slot]});
  for(auto *row:std::initializer_list<QWidget *>{color_row_,metallic_row_,roughness_row_,ao_value_row_,disp_row,bump_editor,normal_row_})
    row->setSizePolicy(row->sizePolicy().horizontalPolicy(),QSizePolicy::Maximum);
  for(const auto &[field,slider]:channel_rows){
    if(auto *numeric=dynamic_cast<bts::NumericSpinBox *>(field)){
      numeric->set_track(true);
      // Pressing and releasing the hidden slider is what scoped one edit for
      // undo and for the preview, so a drag across the field does exactly that
      // rather than repeating the bookkeeping here.
      numeric->drag_started=[slider]{if(slider)slider->setSliderDown(true);};
      numeric->drag_finished=[slider]{if(slider)slider->setSliderDown(false);};
    }
    // FillPanel::refresh_channel_visibility shows every control marked
    // fillHideInFill again when the panel leaves fill context, which put these
    // tracks straight back under their fields. The slider stays alive for the
    // gesture it still carries, but it is not a visible control any more.
    if(slider){slider->setProperty("fillHideInFill",false);slider->hide();}
  }
  // One label column down the channel list. These rows nest a value line inside
  // a column so the fill ramp can sit under them, which the flat pass above
  // cannot see.
  {
    std::vector<QLabel *> captions;int widest=0;
    for(auto *row:std::initializer_list<QWidget *>{color_row_,metallic_row_,roughness_row_,ao_value_row_,disp_row,bump_editor})
      for(auto *label:row->findChildren<QLabel *>())
        if(label->property("fillCaption").toBool()&&!label->text().isEmpty()){
          captions.push_back(label);widest=std::max(widest,label->sizeHint().width());
        }
    for(auto *label:captions)label->setMinimumWidth(widest);
  }
  // Keep the shared channel controls under one stable parent. Reparenting the
  // whole tree on Paint/Fill selection repolishes hundreds of Qt controls.
  material_layout->insertWidget(material_layout->indexOf(brush_controls_)+1,fill_panel_->channel_panel());
  fill_panel_->texture_cancelled=[this](int c){if(callbacks_.fill_texture_cancelled)callbacks_.fill_texture_cancelled(c);};fill_panel_->channel_enabled=[this](int c,bool enabled){if(callbacks_.fill_channel_enabled)callbacks_.fill_channel_enabled(c,enabled);};fill_panel_->files_dropped=[this](int c,const QStringList &paths){if(callbacks_.fill_texture_files_dropped)callbacks_.fill_texture_files_dropped(c,paths);};fill_panel_->asset_dropped=[this](int c,const QString &id){if(callbacks_.fill_texture_asset_dropped)callbacks_.fill_texture_asset_dropped(c,id);};
  material_layout->addStretch();
  properties_scroll->setWidget(material);
  properties_layout->addWidget(properties_scroll, 1);

  console_ = new QWidget(inspector_splitter_);
  inspector_splitter_->addWidget(console_);
  console_->setObjectName("console");
  console_->setMinimumHeight(90);
  auto *console_layout = column(console_);
  QToolButton *close_console;
  console_layout->addWidget(header(console_, "Console", &close_console));
  console_text_ = new QPlainTextEdit(console_);
  console_text_->setObjectName("consoleLog");
  console_text_->setReadOnly(true);
  console_text_->setPlaceholderText("No messages");
  console_text_->setMaximumBlockCount(1000);
  console_layout->addWidget(console_text_, 1);
  for (int i = 0; i < inspector_splitter_->count(); ++i) {
    inspector_splitter_->setStretchFactor(i, 1);
  }

  auto *right = new QWidget(root);
  right->setObjectName("utilityRail");
  right->setFixedWidth(rail_width);
  auto *right_layout = column(right);
  right_layout->setContentsMargins(bts::theme::Space,bts::theme::Space,bts::theme::Space,bts::theme::Space);
  right_layout->setSpacing(bts::theme::Space);
  environment_panel_=new EnvironmentPanel(this);
  environment_panel_->resource_requested=[this]{if(callbacks_.environment_resource_requested)callbacks_.environment_resource_requested();};
  environment_panel_->changed=[this](const QString &key,double value){if(callbacks_.environment_changed)callbacks_.environment_changed(key,value);};
  auto *display_button=tool(right,Glyph::View,"Display settings",true);display_button->setObjectName("displaySettingsButton");
  right_layout->addWidget(display_button);
  connect(display_button,&QToolButton::clicked,this,[this]{environment_panel_->show();environment_panel_->raise();environment_panel_->activateWindow();});
  auto *shader_settings=tool(right,Glyph::Settings,"Shader settings",true);shader_settings->setObjectName("shaderSettingsButton");right_layout->addWidget(shader_settings);
  auto *shader_dialog=new QDialog(this);shader_dialog->setObjectName("shaderSettings");shader_dialog->setWindowTitle("Shader settings — Displacement & Tessellation");auto *shader_layout=new QVBoxLayout(shader_dialog);shader_dialog->resize(400,700);auto *shader_scroll=new QScrollArea(shader_dialog);shader_scroll->setWidgetResizable(true);auto *shader_content=new QWidget(shader_scroll);shader_content->setObjectName("shaderSettingsContent");auto *shader_parameters_layout=new QVBoxLayout(shader_content);shader_scroll->setWidget(shader_content);shader_layout->addWidget(shader_scroll);
  connect(shader_settings,&QToolButton::clicked,this,[=,this]{shader_parameters_layout->addWidget(displacement_panel_);displacement_panel_->show();for(auto *button:shader_dialog->findChildren<QPushButton *>()){button->setAutoDefault(false);button->setDefault(false);}shader_dialog->show();shader_dialog->raise();});
  connect(shader_dialog,&QDialog::finished,this,[=,this]{settings_layout->addWidget(displacement_panel_);displacement_panel_->show();});
  right_layout->addWidget(tool(right, Glyph::History, "History"));
  auto *console_button = tool(right, Glyph::Console, "Console", true);
  console_button->setDefaultAction(console_action_);
  console_button->setToolTip("Show or hide Console");
  right_layout->addWidget(console_button);
  auto *shelf_button = tool(right, Glyph::Shelf, "Shelf", true);
  shelf_button->setDefaultAction(shelf_action_);
  shelf_button->setToolTip("Show or hide Shelf");
  right_layout->addWidget(shelf_button);
  right_layout->addStretch();
  auto *reset_button = tool(right, Glyph::Reset, "Reset reference layout", true);
  reset_button->setDefaultAction(reset_action);
  reset_button->setToolTip("Reset reference layout");
  right_layout->addWidget(reset_button);
  root_layout->addWidget(right);

  asset_thumbnail_ready(this,[this]{refresh_alpha_shelf();});
  texture_loading_bar_=new QWidget(shelf_);texture_loading_bar_->setObjectName("shelfTextureLoading");auto *texture_loading_layout=new QHBoxLayout(texture_loading_bar_);texture_loading_layout->setContentsMargins(4,2,4,2);texture_loading_notice_=new QLabel(texture_loading_bar_);texture_loading_notice_->setObjectName("shelfTextureLoadingText");texture_loading_notice_->setWordWrap(true);texture_loading_layout->addWidget(texture_loading_notice_,1);texture_loading_progress_=new QProgressBar(texture_loading_bar_);texture_loading_progress_->setMaximumWidth(120);texture_loading_layout->addWidget(texture_loading_progress_);texture_loading_cancel_=new QPushButton("Cancel",texture_loading_bar_);texture_loading_cancel_->setObjectName("cancelTextureLoads");texture_loading_layout->addWidget(texture_loading_cancel_);shelf_->layout()->addWidget(texture_loading_bar_);texture_loading_bar_->hide();connect(texture_loading_cancel_,&QPushButton::clicked,this,[this]{if(callbacks_.cancel_texture_loads)callbacks_.cancel_texture_loads();});
  statusBar()->setObjectName("workspaceStatus");
  statusBar()->setFixedHeight(22);
  statusBar()->setSizeGripEnabled(false);

  connect(shelf_action_, &QAction::toggled, this, &WorkspaceShell::set_shelf_visible);
  connect(console_action_, &QAction::toggled, this, &WorkspaceShell::set_console_visible);
  connect(reset_action, &QAction::triggered, this, &WorkspaceShell::reset_reference_layout);
  connect(close_shelf, &QToolButton::clicked, this, [this] { set_shelf_visible(false); });
  connect(close_console, &QToolButton::clicked, this, [this] { set_console_visible(false); });
  connect(texture_sets_, &QListWidget::currentRowChanged, this, [this](int index) {
    active_tile_=0;
    if (callbacks_.texture_set_selected) {
      const auto *item = texture_sets_->item(index);
      callbacks_.texture_set_selected(index, item ? item->text() : QString{});
    }
  });
  connect(texture_sets_,&QListWidget::itemClicked,this,[this]{if(active_tile_){active_tile_=0;if(callbacks_.tile_selected)callbacks_.tile_selected(0);}});
  connect(uv_tiles_,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item){const int tile=item?item->data(0,Qt::UserRole).toInt():0;if(active_tile_==tile)return;active_tile_=tile;if(callbacks_.tile_selected)callbacks_.tile_selected(tile);});
  // GPU requests already coalesce at frame start. Submit the value before
  // Qt schedules its paint instead of delaying it by a second frame timer.
  connect(roughness_slider_,&QSlider::sliderPressed,this,[this]{setProperty("roughnessGesture",true);if(callbacks_.roughness_started)callbacks_.roughness_started();});
  connect(roughness_slider_,&QSlider::sliderReleased,this,[this]{if(callbacks_.roughness_changed)callbacks_.roughness_changed(roughness_control_->value());if(callbacks_.roughness_finished)callbacks_.roughness_finished();setProperty("roughnessGesture",false);});
  connect(roughness_slider_, &QSlider::valueChanged, this, [this](int value) {
    roughness_control_->setValue(double(value) / 1000.0);
  });
  connect(roughness_control_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
    const QSignalBlocker blocker(roughness_slider_);
    roughness_slider_->setValue(qRound(value * 1000.0));
    if (roughness_control_->isEnabled() && callbacks_.roughness_changed) {
      callbacks_.roughness_changed(value);
    }
  });
  connect(view_layout_control_, &QComboBox::currentIndexChanged, this, [this](int index) {
    view_layout_.mode = index == 0 ? ViewLayout::SceneOnly :
                        index == 1 ? ViewLayout::UvOnly : ViewLayout::Split;
    if (view_layout_.mode == ViewLayout::Split) {
      view_layout_.uv_first = index == 3;
    }
    if (callbacks_.view_layout_changed) {
      callbacks_.view_layout_changed(view_layout_);
    }
  });
  connect(projection_control_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (callbacks_.projection_changed) {
      callbacks_.projection_changed(index == 1 ? OrbitCamera::Projection::Orthographic :
                                               OrbitCamera::Projection::Perspective);
    }
  });
  // These brush quantities are percentages too, even when the older chrome
  // omitted their unit. Keep them identical to layer/effect percent editors.
  for(const auto *name:{"brushSize","toolbarBrushSize","brushFlow","toolbarBrushFlow","brushStrokeOpacity","toolbarStrokeOpacity","brushSpacing","toolbarBrushSpacing","brushMinimumSize","brushMinimumFlow","brushSizeJitter","brushFlowJitter","brushPositionJitter","lazyMouseDistance"})
    if(auto *number=findChild<QDoubleSpinBox *>(name)){number->setDecimals(2);number->setSuffix(" %");if(number->maximumWidth()<80)number->setFixedWidth(80);}
}

void WorkspaceShell::closeEvent(QCloseEvent *event)
{
  if(!close_requested||close_requested())event->accept();else event->ignore();
}

void WorkspaceShell::retire_editor_callbacks() noexcept
{
  // A pending spinbox commits in hideEvent. Its connection may have a nested
  // panel context, so disconnecting only receivers on this shell is not enough.
  // Walk without allocating, and block BEFORE clearing callbacks or hiding UI.
  const auto retire = [this](auto &&self, QObject *object) noexcept -> void {
    object->blockSignals(true);
    QObject::disconnect(object, nullptr, this, nullptr);
    for (QObject *child : object->children()) self(self, child);
  };
  retire(retire, this);
  callbacks_ = {};
  close_requested = {};
}

WorkspaceShell::~WorkspaceShell() { retire_editor_callbacks(); }

void WorkspaceShell::set_environment(const QJsonObject &state)
{
  environment_panel_->set_state(state);
}

void WorkspaceShell::set_trackball(bool enabled){const QSignalBlocker block(trackball_toggle_);trackball_toggle_->setChecked(enabled);}

void WorkspaceShell::set_symmetry(const SymmetrySettings &s) {
  symmetry_=s;const QSignalBlocker a(symmetry_toggle_),b(symmetry_manipulator_),c(symmetry_mode_),d(symmetry_axis_),e(symmetry_offset_),f(symmetry_copies_),g(symmetry_intersection_);
  symmetry_toggle_->setChecked(s.enabled);symmetry_manipulator_->setChecked(s.manipulator);symmetry_mode_->setCurrentIndex(int(s.mode));symmetry_axis_->setCurrentIndex(s.axis);symmetry_offset_->setValue(s.offset);symmetry_copies_->setValue(s.copies);symmetry_intersection_->setChecked(s.intersection);
  const bool radial=s.mode==SymmetryMode::Radial;symmetry_offset_->setEnabled(!radial);symmetry_copies_->setEnabled(radial);for(int i=0;i<3;++i){const QSignalBlocker blocker(symmetry_center_[i]);symmetry_center_[i]->setValue(s.center[i]);symmetry_center_[i]->setEnabled(radial);}
}
void WorkspaceShell::add_alpha_resource(const BrushAlpha &alpha)
{
  if(asset_catalog_){StudioAsset asset;asset.name=QString::fromStdString(alpha.name());asset.filename=asset.name+".alpha";asset.usages={"alpha"};asset.alpha=alpha;asset_catalog_->add(std::move(asset));}else alpha_resources_.push_back(alpha);refresh_alpha_shelf();
}
bool WorkspaceShell::layer_thumbnails_pending() const{return thumbnails_&&thumbnails_->pending();}
void WorkspaceShell::refresh_alpha_shelf()
{
  if(auto *label=findChild<QLabel *>("layerMaterialLibraryCount")){
    int count=0;
    if(asset_catalog_)for(const auto &asset:asset_catalog_->assets())if(asset_in_category(asset,"Materials"))++count;
    label->setText(count==1?QStringLiteral("1 library material"):QString("%1 library materials").arg(count));
    label->setVisible(count>0);
  }
  if(asset_catalog_&&smart_material_button_){
    auto *menu=smart_material_button_->menu();std::vector<const StudioAsset *> presets;QByteArray signature;
    for(const auto &asset:asset_catalog_->assets())if(asset.smart_material&&asset_in_category(asset,"Material presets")){presets.push_back(&asset);signature+=asset.id.toRfc4122();signature+=QByteArray::number(qulonglong(asset.revision));signature+=';';}
    if(menu->property("materialPresetSignature").toByteArray()!=signature){menu->clear();menu->setProperty("materialPresetSignature",signature);for(const auto *asset:presets){auto *action=menu->addAction(asset->name);action->setObjectName((asset->material_preset||is_spsm_asset(*asset))?"applyUserSmartMaterial":"applyWornPaintedMetal");action->setData(asset->id.toString());const auto id=asset->id;connect(action,&QAction::triggered,menu,[this,id]{for(const auto &source:asset_catalog_->assets())if(source.id==id){if(callbacks_.asset_selected)callbacks_.asset_selected(source);break;}});}}
    const auto actions=menu->actions();bool available=false;for(int i=0;i<int(presets.size());++i){const bool enabled=smart_material_target_ready(*presets[i],asset_project_ready_,asset_smart_ready_);actions[i]->setEnabled(enabled);available|=enabled;}smart_material_button_->setEnabled(available);smart_material_button_->setToolTip(available?"Add material preset":asset_project_ready_?"Import a Material Preset into Assets to use it":"Create or open a project to use material presets");
  }

  performance::Scope phase("shelf_ui_refresh");
  if(!alpha_assets_||!shelf_categories_||!shelf_categories_->currentItem())return;
  const auto category_name=shelf_categories_->currentItem()->text();
  const bool baked_category=category_name=="Baked";alpha_assets_->setGridSize(baked_category?QSize(144,116):QSize(90,114));alpha_assets_->setIconSize(QSize(84,84));alpha_assets_->setWordWrap(baked_category);alpha_assets_->setTextElideMode(baked_category?Qt::ElideNone:Qt::ElideRight);
  if(auto *hdr=findChild<QLabel *>("assetLibraryHeader")){const QString scope_text=asset_scope_control_?asset_scope_control_->currentText():QStringLiteral("All libraries");hdr->setText(scope_text+"  /  "+(category_name=="All"?QStringLiteral("Your assets"):category_name));}
  if(asset_category_chip_){asset_category_chip_->setVisible(category_name!="All");const QString chip_label=fontMetrics().elidedText(category_name,Qt::ElideRight,120)+" ×";asset_category_chip_->setText(chip_label);const int min_w=asset_category_chip_->fontMetrics().horizontalAdvance(chip_label)+12;asset_category_chip_->setMinimumWidth(min_w);asset_category_chip_->resize(std::max(asset_category_chip_->width(),min_w),22);asset_category_chip_->setAccessibleName("Filter: "+category_name);asset_category_chip_->setToolTip("Remove "+category_name+" filter");}
  asset_reset_->setEnabled(!alpha_search_.isEmpty()||asset_scope_!=(category_name=="Project"?int(AssetScope::Project):-1));
  if(auto *empty=findChild<QWidget *>("emptyMaterials")){
    const bool material_category=category_name=="Materials"||category_name=="Material presets";
    const bool presets=category_name=="Material presets";
    if(auto *note=empty->findChild<QLabel *>("emptyMaterialsNote"))note->setText(presets?"No dynamic material presets yet. Add a Mask Generator to a layer folder, then save the folder to your library.":"Your material library is empty. Poly Haven materials are CC0. Import or drop downloaded textures here.");
    if(auto *download=empty->findChild<QPushButton *>("downloadFreeMaterialsEmpty"))download->setVisible(!presets);
    bool has_material=false;if(asset_catalog_)for(const auto &asset:asset_catalog_->assets())if(asset_in_category(asset,category_name)){has_material=true;break;}
    empty->setVisible(material_category&&!has_material);
    alpha_assets_->setVisible(!(material_category&&!has_material));
  }
  if(asset_catalog_){const auto selected=alpha_assets_->currentItem()?alpha_assets_->currentItem()->data(Qt::UserRole).toString():alpha_assets_->property("lastSelectedAsset").toString();if(!selected.isEmpty())alpha_assets_->setProperty("lastSelectedAsset",selected);alpha_assets_->clear();const auto category=shelf_categories_->currentItem()->text();const AssetQuery query(alpha_search_);std::vector<const StudioAsset *> sorted;for(const auto &asset:asset_catalog_->assets())sorted.push_back(&asset);std::stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){return QString::compare(a->name,b->name,Qt::CaseInsensitive)<0;});for(const auto *asset:sorted){if(asset->filename=="baked.map"&&asset->search_terms.contains("baked-map"))continue;if(asset_scope_>=0&&int(asset->scope)!=asset_scope_)continue;if(!asset_in_category(*asset,category))continue;if(!query.matches(*asset))continue;const auto thumbnail=asset_thumbnail(*asset);auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail.scaled(84,84,Qt::KeepAspectRatio,Qt::SmoothTransformation))),asset->name,alpha_assets_);const auto id=asset->id.toString();item->setData(Qt::UserRole,id);if(asset->scope!=AssetScope::Builtin)item->setFlags(item->flags()|Qt::ItemIsEditable);const int sequence_count=!asset->texture_files.empty()&&asset->texture_files.front().tile?int(asset->texture_files.size()):image_sequence_count(asset->bytes);item->setData(Qt::UserRole+20,sequence_count);bool compatible=smart_material_target_ready(*asset,asset_project_ready_,asset_smart_ready_)||((asset->smart_mask||is_spmsk_asset(*asset))&&asset_project_ready_)||bool(asset->brush_preset)||(asset->usages.contains("alpha")&&asset_paint_ready_)||(asset->material&&asset_project_ready_&&(!asset->projection_weave||asset_projection_ready_)&&(!asset_projection_ready_||!asset->fill_preset))||(asset->usages.contains("environment"))||(asset->usages.contains("procedural")&&asset_fill_ready_)||(asset->usages.contains("texture")&&asset_project_ready_);if(path_active_&&asset->usages.contains("alpha")&&((asset->alpha&&asset->alpha->kind==AlphaKind::Bitmap)||!asset->bytes.isEmpty()))compatible=false;item->setToolTip(asset->name+"\n"+asset->filename+(asset->path.isEmpty()?QString{}:"\n"+asset->path)+"\n"+asset_scope_name(asset->scope)+(asset->search_terms.contains("preview:color-metallic-roughness-sphere")?QString(" — Generated Color / Metallic / Roughness sphere; normal, bump and displacement not shown"):QString{})+(asset->search_terms.contains("builtin:native-smart-mask")?QString(" — Native GPU mask on a synthetic cavity/ridge sphere. White coverage brightens a neutral gray carrier; project coverage depends on its mesh maps."):QString{})+(sequence_count?QString(" — %1 UDIM images").arg(sequence_count):QString{})+(compatible?QString{}:" — Incompatible with current target"));if(!compatible)item->setFlags(item->flags()&~Qt::ItemIsEnabled);if(id==selected)alpha_assets_->setCurrentItem(item);}if((category=="Baked"||category=="All"||category=="Project")&&(asset_scope_<0||asset_scope_==int(AssetScope::Project))&&callbacks_.baked_maps)for(const auto &baked:callbacks_.baked_maps()){StudioAsset probe;probe.name=baked.name;probe.usages={"texture"};probe.search_terms={"baked-map"};if(!query.matches(probe))continue;auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(baked.thumbnail)),baked.name);int position=0;while(position<alpha_assets_->count()&&QString::compare(alpha_assets_->item(position)->text(),baked.name,Qt::CaseInsensitive)<=0)++position;alpha_assets_->insertItem(position,item);item->setData(Qt::UserRole,baked.id);item->setToolTip(baked.description.isEmpty()?baked.name+" — baked mesh map":baked.description);if(baked.id==selected)alpha_assets_->setCurrentItem(item);}return;}
  alpha_assets_->clear();const auto category=shelf_categories_->currentItem()->text();if(category!="All"&&category!="Alphas")return;
  for(int index=0;index<int(alpha_resources_.size());++index) {
    const auto &alpha=alpha_resources_[index];const auto name=QString::fromStdString(alpha.name());if(!name.contains(alpha_search_,Qt::CaseInsensitive))continue;
    auto *item=new QListWidgetItem(QIcon(QPixmap::fromImage(AlphaPanel::thumbnail(alpha))),name,alpha_assets_);item->setData(Qt::UserRole,index);item->setToolTip(name+(alpha.kind==AlphaKind::Bitmap?" — session alpha":" — built-in alpha"));
  }
}
void WorkspaceShell::show_texture_set(const TextureSet *set, const NewProjectOptions *settings,const BrushSettings *brush)
{
  // Only the view is filtered. UUIDs and callbacks still address the complete
  // authoritative set; no filtered copy is ever published to the document.
  const bool opacity_stack=set&&set->material_layering.enabled&&set->material_layering.active_mask>=0;
  const bool retained_main=set&&set->material_layering.enabled&&set->material_layering.active_mask<0;
  setProperty("materialOpacityChannel",opacity_stack?QVariant(material_layering_channel_context(*set,0)):QVariant{});
  layers_->setEnabled(!retained_main);
  std::optional<TextureSet> material_view;
  if(set&&set->material_layering.allocated()){
    const bool retained_main=set->material_layering.enabled&&set->material_layering.active_mask<0;
    material_view=material_layering_display(*set);
    if(retained_main)material_view->uniform_editable=false;
    set=&*material_view;
  }
  add_adjustment_action_->setEnabled(set&&set->uniform_editable&&!set->disabled&&!opacity_stack&&bool(callbacks_.add_adjustment));
  performance::Scope phase("layer_properties_ui_refresh");auto section=std::make_unique<performance::Scope>("show_texture_set_settings");
  const auto update_rows = [&](bool same_focus,bool multiple,bool effect_target,bool geometry_target,
                                bool hierarchy_target,bool instance_target,bool painting,bool mask_target) {
  section=std::make_unique<performance::Scope>("show_texture_set_layer_rows");
  const QSignalBlocker layer_signals(layers_);
  static_cast<LayerEffectRowState *>(findChild<QObject *>("layerEffectRowState"))->synchronize(set);
  std::vector<const TextureLayer *> visible_layers;
  int collapsed_depth=-1;
  if(set)for(auto it=set->layers.rbegin();it!=set->layers.rend();++it) {
    if(collapsed_depth>=0&&it->depth>collapsed_depth)continue;
    collapsed_depth=-1;visible_layers.push_back(&*it);
    if(it->kind==LayerKind::Folder&&collapsed_folders_.contains(it->id))collapsed_depth=it->depth;
  }
  std::vector<std::pair<QUuid,QUuid>> row_ids;for(const auto *layer:visible_layers){row_ids.emplace_back(layer->id,QUuid{});if(!set->selected_mask){for(auto effect=layer->effects.rbegin();effect!=layer->effects.rend();++effect)if(effect->channel!=3)row_ids.emplace_back(layer->id,effect->id);}else if(layer->mask){for(const auto &entry:mask_effect_entries(layer->mask->effects,true))row_ids.emplace_back(layer->id,entry.effect->id);}}
  bool rebuild=layers_->count()!=int(row_ids.size());
  if(!rebuild)for(int row=0;row<layers_->count();++row)rebuild|=QUuid(layers_->item(row)->data(Qt::UserRole).toString())!=row_ids[row].first||QUuid(layers_->item(row)->data(Qt::UserRole+10).toString())!=row_ids[row].second;
  if (rebuild) layers_->clear();
  int selected_row=-1;
  material_label_->setText("MATERIAL");
  if (set) {
    int row=0;
    for (const auto *it:visible_layers) {
      auto *item=rebuild ? new QListWidgetItem(layers_) : layers_->item(row);
      item->setData(adjustment_editable_role,set->uniform_editable&&!set->disabled&&!opacity_stack&&it->kind==LayerKind::Paint&&!it->instance);
      item->setData(adjustment_checked_role,adjustment_layer(*it));
      const bool cached=layer_is_cached(*set,it->id);item->setData(Qt::UserRole+27,cached);item->setData(Qt::UserRole+28,it->precompose_below);item->setForeground(cached?QBrush(QColor(bts::theme::Muted)):QBrush(QColor(bts::theme::Heading)));
      item->setText(QString::fromStdString(it->name)+(cached?" [Cached]":""));item->setData(Qt::UserRole+19,it->instance.has_value());item->setData(Qt::UserRole+20,set->instance_hierarchies.contains(it->id));item->setData(Qt::UserRole+21,set->instance_problems.contains(it->id)?set->instance_problems.at(it->id):QString{});
      {std::set<std::string> names;for(const auto &[uid,name]:set->geometry_names)names.insert(name);int included=0;if(it->geometry.type==1){for(int tile:set->tiles)included+=it->geometry.includes_tile(tile);}else for(const auto &name:names)included+=it->geometry.includes(name);item->setData(Qt::UserRole+15,included);item->setData(Qt::UserRole+16,it->geometry.type==1?int(set->tiles.size()):int(names.size()));item->setData(Qt::UserRole+17,it->id==set->selected_layer&&geometry_target);item->setData(Qt::UserRole+18,it->geometry.active());}
      item->setData(Qt::UserRole+1,(layer_channel_==14?it->displacement.opacity:layer_channel_>=6?it->users[layer_channel_-6].opacity:layer_channel_==5?it->ao_opacity:it->opacity[layer_channel_])*100);
      item->setData(Qt::UserRole,it->id.toString());item->setData(Qt::UserRole+10,QString{});item->setData(Qt::UserRole+11,!it->effects.empty());item->setData(Qt::UserRole+12,it->mask&&!it->mask->effects.empty());
      unsigned channel_marks=0;for(int c:{0,1,2,4})if(set->channels[c]&&it->channels[c])channel_marks|=1u<<c;if(set->ao_channel&&it->ao_enabled)channel_marks|=1u<<5;if(set->displacement.channel&&it->displacement.enabled)channel_marks|=1u<<14;item->setData(Qt::UserRole+22,channel_marks);
      item->setData(Qt::UserRole+2,it->depth);item->setData(Qt::UserRole+3,it->kind==LayerKind::Folder);
      item->setData(Qt::UserRole+4,collapsed_folders_.contains(it->id));
      item->setData(Qt::UserRole+5,int(layer_channel_==14?it->displacement.blending:layer_channel_>=6?it->users[layer_channel_-6].blending:layer_channel_==5?it->ao_blending:it->blending[layer_channel_]));
      item->setData(Qt::UserRole+6,it->mask.has_value());item->setData(Qt::UserRole+8,it->id==set->selected_layer&&set->selected_mask);
      if(it->mask){const auto image=thumbnails_->image(set->id,it->id,layer_channel_,true);if(!image.isNull())item->setData(Qt::UserRole+7,QIcon(QPixmap::fromImage(image)));item->setData(Qt::UserRole+9,it->mask->enabled);}
      item->setFlags(cached?(Qt::ItemIsEnabled|Qt::ItemIsSelectable):Qt::ItemFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsDragEnabled|Qt::ItemIsDropEnabled|Qt::ItemIsUserCheckable|Qt::ItemIsEditable));item->setData(InlineNameDelegate::name_role,QString::fromStdString(it->name));
      item->setCheckState(it->visible ? Qt::Checked : Qt::Unchecked);
      item->setSizeHint(QSize(0,44));
      item->setToolTip(it->kind==LayerKind::Folder ? "Folder; click its disclosure to expand or collapse" : it->kind==LayerKind::Fill ? "Uniform fill; right-click to change stack order" : "Paint layer; left-drag in 3D or UV to paint");
      {const auto image=thumbnails_->image(set->id,it->id,layer_channel_,false);if(!image.isNull())item->setIcon(QIcon(QPixmap::fromImage(image)));}
      if (it->id==set->selected_layer && set->selected_effect.isNull()) {
        selected_row=row;
        material_label_->setText(quick_mask_edit_?QString("GRAYSCALE — Quick mask"):hierarchy_target?QString("Properties - ")+QString::fromStdString(it->name):instance_target?QString("PROPERTIES — LAYER INSTANCE"):geometry_target?"GEOMETRY MASK":polygon_active_&&painting?"POLYGON FILL":painting&&brush->erase?(mask_target?"ERASE MASK — No material":"ERASER — No material"):painting&&path_active_?"PROPERTIES — SURFACE PATH":painting&&brush->clone?"CLONE — No material":painting&&brush->smudge?"SMUDGE — No material":painting&&brush->projected?"PROPERTIES — PROJECTION":mask_target?"PAINT MASK":it->kind==LayerKind::Folder?"PROPERTIES — FOLDER":it->kind==LayerKind::Fill ? "FILL LAYER" : "PAINT");
      }
      ++row;
      if(!set->selected_mask)for(auto effect_iterator=it->effects.rbegin();effect_iterator!=it->effects.rend();++effect_iterator){const auto &effect=*effect_iterator;if(effect.channel==3)continue;
        auto *child=rebuild?new QListWidgetItem(layers_):layers_->item(row);const QString name=effect.channel==14?QString("Displacement"):effect.channel>=6?QString::fromStdString(set->users.at(effect.channel-6).name):effect.channel==5?QString("Ambient occlusion"):effect.channel==3?QString("Normal"):QString::fromLatin1(channel_names[effect.channel]);
        child->setText(effect.kind==ContentEffectKind::LinearColorTransform?QString("Linear RGB: %1").arg(effect.linear_color?linear_color_transform_label(*effect.linear_color):QString("Invalid transform")):effect.kind==ContentEffectKind::Anchor?QString("%1").arg(effect.anchor_name):effect.kind==ContentEffectKind::Generator?QString("%1 - %2").arg(effect.generator?mask_generator_names[effect.generator]:"Generator (empty)",name):effect.kind==ContentEffectKind::Filter?QString("%1").arg(filter_names[effect.filter.resource]):effect.kind==ContentEffectKind::Paint?QString("Paint"):effect.kind==ContentEffectKind::Fill?QString("Fill - %1").arg(name):QString("Levels - %1").arg(name));if(!effect.name.empty())child->setText(QString::fromStdString(effect.name));child->setData(Qt::UserRole+24,effect.kind!=ContentEffectKind::Anchor&&effect.kind!=ContentEffectKind::LinearColorTransform);child->setData(Qt::UserRole+2,it->depth+1);child->setData(Qt::UserRole+23,int(effect.kind==ContentEffectKind::Generator?PainterGlyph::Generator:effect.kind==ContentEffectKind::Filter||effect.kind==ContentEffectKind::LinearColorTransform?PainterGlyph::Filter:effect.kind==ContentEffectKind::Paint?PainterGlyph::Paint:effect.kind==ContentEffectKind::Fill?PainterGlyph::Fill:effect.kind==ContentEffectKind::Anchor?PainterGlyph::Anchor:PainterGlyph::Levels));child->setData(Qt::UserRole+13,effect.kind==ContentEffectKind::Anchor?int(MaskEffectKind::Anchor):effect.kind==ContentEffectKind::Generator?int(MaskEffectKind::Generator):effect.kind==ContentEffectKind::Filter?int(MaskEffectKind::Filter):effect.kind==ContentEffectKind::Paint?int(MaskEffectKind::Paint):effect.kind==ContentEffectKind::Fill?int(MaskEffectKind::Fill):-1);child->setData(Qt::UserRole+14,effect.kind==ContentEffectKind::Filter&&effect.filter.resource>0);child->setData(Qt::UserRole+1,effect.opacity*100);child->setData(Qt::UserRole+5,int(effect.blending));child->setData(Qt::UserRole,it->id.toString());child->setData(Qt::UserRole+10,effect.id.toString());child->setData(Qt::UserRole+12,false);child->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable|Qt::ItemIsEditable);child->setData(InlineNameDelegate::name_role,effect.kind==decltype(effect.kind)::Anchor?effect.anchor_name:!effect.name.empty()?QString::fromStdString(effect.name):child->text().trimmed());child->setCheckState(effect.enabled?Qt::Checked:Qt::Unchecked);child->setSizeHint(QSize(0,26));child->setToolTip(effect.kind==ContentEffectKind::Anchor?"Anchor marker; blend and opacity do not apply. Click × to remove.":effect.kind==ContentEffectKind::LinearColorTransform?"Explicit linear RGB conversion after source-space Levels; the Fill tint is applied in ACEScg. Disable for identity conversion.":"Editable content effect; click × to remove");
        if(effect.id==set->selected_effect&&it->id==set->selected_layer){selected_row=row;material_label_->setText(effect.kind==ContentEffectKind::LinearColorTransform?"PROPERTIES — LINEAR COLOR":effect.kind==ContentEffectKind::Anchor?"PROPERTIES — ANCHOR POINT":effect.kind==ContentEffectKind::Generator?"PROPERTIES — GENERATOR":effect.kind==ContentEffectKind::Filter?"PROPERTIES — FILTER":effect.kind==ContentEffectKind::Paint?"PROPERTIES — PAINT":effect.kind==ContentEffectKind::Fill?"PROPERTIES — FILL":"PROPERTIES — LEVELS");}++row;
      }
      if(set->selected_mask&&it->mask)for(const auto &entry:mask_effect_entries(it->mask->effects,true)){const auto *effect=entry.effect;
        auto *child=rebuild?new QListWidgetItem(layers_):layers_->item(row);child->setText(effect->kind==MaskEffectKind::Group?"Group":effect->kind==MaskEffectKind::ColorSelection?"Color selection":effect->kind==MaskEffectKind::Fill?"Fill":effect->kind==MaskEffectKind::Anchor?QString("%1").arg(effect->anchor_name):effect->kind==MaskEffectKind::Filter?QString("%1").arg(filter_names[effect->filter.resource]):effect->kind==MaskEffectKind::Paint?"Paint":effect->kind==MaskEffectKind::Compare?"Compare mask":effect->kind==MaskEffectKind::Generator?(effect->generator==0?"Generator (empty)":QString("%1").arg(mask_generator_names[effect->generator])):"Levels");if(!effect->name.empty())child->setText(QString::fromStdString(effect->name));child->setData(Qt::UserRole+24,effect->kind!=MaskEffectKind::Anchor&&effect->kind!=MaskEffectKind::ColorSelection);child->setData(Qt::UserRole+2,it->depth+1+int(entry.depth));child->setData(Qt::UserRole+23,int(effect->kind==MaskEffectKind::Group?PainterGlyph::Group:effect->kind==MaskEffectKind::Generator?PainterGlyph::Generator:effect->kind==MaskEffectKind::Filter?PainterGlyph::Filter:effect->kind==MaskEffectKind::Paint?PainterGlyph::Paint:effect->kind==MaskEffectKind::Fill?PainterGlyph::Fill:effect->kind==MaskEffectKind::Anchor?PainterGlyph::Anchor:effect->kind==MaskEffectKind::Levels?PainterGlyph::Levels:PainterGlyph::Mask));child->setData(Qt::UserRole+13,int(effect->kind));child->setData(Qt::UserRole+14,effect->kind==MaskEffectKind::Filter&&effect->filter.resource>0);child->setData(Qt::UserRole+1,effect->opacity*100);child->setData(Qt::UserRole+5,int(effect->blending));child->setData(Qt::UserRole,it->id.toString());child->setData(Qt::UserRole+10,effect->id.toString());child->setData(Qt::UserRole+12,true);child->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable|Qt::ItemIsUserCheckable|Qt::ItemIsEditable);child->setData(InlineNameDelegate::name_role,effect->kind==decltype(effect->kind)::Anchor?effect->anchor_name:!effect->name.empty()?QString::fromStdString(effect->name):child->text().trimmed());child->setCheckState(effect->enabled?Qt::Checked:Qt::Unchecked);child->setSizeHint(QSize(0,26));child->setToolTip(effect->kind==MaskEffectKind::Anchor?"Anchor marker; blend and opacity do not apply. Click × to remove.":effect->kind==MaskEffectKind::ColorSelection?"Color selection uses its Output, Hardness and Tolerance properties; row blend and opacity do not apply. Click × to remove.":"Mask effect; click × to remove");if(effect->id==set->selected_effect&&it->id==set->selected_layer){selected_row=row;material_label_->setText(effect->kind==MaskEffectKind::Group?"PROPERTIES — GROUP":effect->kind==MaskEffectKind::ColorSelection?"PROPERTIES — COLOR SELECTION":effect->kind==MaskEffectKind::Fill?"PROPERTIES — FILL":effect->kind==MaskEffectKind::Anchor?"PROPERTIES — ANCHOR POINT":effect->kind==MaskEffectKind::Filter?"PROPERTIES — FILTER":effect->kind==MaskEffectKind::Paint?"PROPERTIES — PAINT":effect->kind==MaskEffectKind::Compare?"PROPERTIES — COMPARE MASK":effect->kind==MaskEffectKind::Generator?"PROPERTIES — GENERATOR":"PROPERTIES — LEVELS");}++row;
      }
    }
  }
  if(!same_focus||rebuild||effect_target||(!multiple&&set&&!LayerEffectRowState::current_target_matches(layers_->currentIndex(),*set)))layers_->setCurrentRow(selected_row,QItemSelectionModel::ClearAndSelect);
  const TextureLayer *selected_layer=nullptr;
  if(set)for(const auto &layer:set->layers)if(layer.id==set->selected_layer){selected_layer=&layer;break;}
  const bool editable_selection=set&&set->uniform_editable&&!set->disabled&&selected_layer&&!layer_is_cached(*set,selected_layer->id);
  auto *delete_button=findChild<QToolButton *>("deleteLayer");
  delete_button->setEnabled(editable_selection&&!layers_->selectedItems().isEmpty()&&bool(callbacks_.delete_selection));
  delete_button->setToolTip(delete_button->isEnabled()?"Delete selected layers, masks or effects":"Select an editable layer, mask or effect to delete");
  auto *precompose_button=findChild<QToolButton *>("addPrecomposeLayer");
  precompose_button->setEnabled(editable_selection&&selected_layer->depth==0&&bool(callbacks_.precompose_layer));
  precompose_button->setToolTip(precompose_button->isEnabled()?"Add Pre-compose / Cache Layer":"Select an editable top-level layer for Pre-compose");
  if(multiple&&!rebuild)material_label_->setText(QString("%1 LAYERS SELECTED").arg(layers_->selectedItems().size()));
  move_layer_up_->setEnabled(set && !effect_target && set->uniform_editable && selected_row>0);
  move_layer_down_->setEnabled(set && !effect_target && set->uniform_editable && selected_row>=0 && selected_row+1<layers_->count());
  if (settings) {
    const QSignalBlocker block(resolution_);resolution_->setEnabled(set&&!set->disabled);
    refresh_texture_extent_choices(*resolution_,128,set?set->texture_resolution:int(settings->resolution));
    {const QSignalBlocker height_block(resolution_height_),lock_block(resolution_lock_);resolution_lock_->setEnabled(set&&!set->disabled);resolution_lock_->setChecked(!set||set->size_linked);resolution_lock_->setText(!set||set->size_linked?"Linked":"Unlinked");resolution_height_->setEnabled(set&&!set->disabled&&!set->size_linked);refresh_texture_extent_choices(*resolution_height_,128,set?set->rows():int(settings->resolution));}
    resolution_->setToolTip(QString("%1 normal map convention; %2")
        .arg(settings->normal_map_format == NormalMapFormat::OpenGL ? "OpenGL" : "DirectX")
        .arg(settings->uv_workflow == UvWorkflow::SingleTile ? "Single UV tile" : "Preserve UV tiles"));
  }
  if(auto *title=findChild<QLabel *>("propertiesDockTitle")){QString text=material_label_->text();if(text=="FILL LAYER")text="PROPERTIES - FILL";else if(text=="PAINT"||text=="PAINT MASK")text=set&&set->selected_mask?"PROPERTIES - MASK":"PROPERTIES - PAINT";else if(text=="MATERIAL")text="PROPERTIES";else if(!text.startsWith("PROPERTIES"))text="PROPERTIES - "+text;text.replace(QString::fromUtf8("—"),"-");title->setText(text);if(properties_float_)properties_float_->setWindowTitle(text);}
  material_label_->setVisible(material_label_->text()!="FILL LAYER"&&!material_label_->text().startsWith("PROPERTIES"));
  };
  // Publish structural selection/list changes before synchronizing expensive
  // Properties widgets after the first composed frame.
  if(property("layerRowsOnly").toBool()&&thumbnails_){
    update_rows(false,false,false,false,false,false,false,false);return;
  }
  if(shown_texture_set_!=(set?set->id:QUuid{})||shown_texture_layer_!=(set?set->selected_layer:QUuid{}))fill_panel_->reset_resource_channel();shown_texture_set_=set?set->id:QUuid{};shown_texture_layer_=set?set->selected_layer:QUuid{};
  if(!thumbnails_){thumbnails_=std::make_unique<LayerThumbnailJobs>();thumbnails_->submit_request=callbacks_.thumbnail_requested;thumbnails_->read_result=callbacks_.thumbnail_result;thumbnails_->updated=[this](const QUuid &set_id,int channel,const QUuid &layer,bool mask,const QImage &image){
    if(set_id!=shown_texture_set_||channel!=layer_channel_)return;const QSignalBlocker blocker(layers_);
    for(int row=0;row<layers_->count();++row){auto *item=layers_->item(row);if(QUuid(item->data(Qt::UserRole).toString())!=layer||!item->data(Qt::UserRole+10).toString().isEmpty())continue;const QIcon icon(QPixmap::fromImage(image));if(mask)item->setData(Qt::UserRole+7,icon);else item->setIcon(icon);}
    setProperty("thumbnailWorkerUpdates",property("thumbnailWorkerUpdates").toInt()+1);
  };}
  if(set&&!property("layerSelectionOnly").toBool())thumbnails_->request(*set,layer_channel_);
  findChild<QAction *>("configureChannels")->setEnabled(set&&set->uniform_editable&&!set->disabled&&!opacity_stack);
  mesh_maps_panel_->synchronize(set);
  for(int r=0;set&&r<layers_->count();++r){auto *item=layers_->item(r);const bool cached=layer_is_cached(*set,QUuid(item->data(Qt::UserRole).toString()));item->setData(Qt::UserRole+27,cached);if(cached){item->setForeground(QColor(bts::theme::Muted));item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);}}
  findChild<QWidget *>("materialProperties")->setEnabled(!set||!layer_is_cached(*set,set->selected_layer));
  findChild<QToolButton *>("toolbarBakeMeshMaps")->setEnabled(set&&!set->disabled&&bool(callbacks_.bake_mesh_maps));
  findChild<QWidget *>("emptyLayersView")->setVisible(!set);layers_->setVisible(set);
  const QString unavailable=!set?QStringLiteral("No project open"):!set->uniform_editable?(property("materialEditabilityReason").toString().isEmpty()?QStringLiteral("Requires a Principled BSDF connected directly to Surface with unlinked Base Color/Roughness/Metallic/Normal inputs"):property("materialEditabilityReason").toString()):QString{};
  for(auto *button:{paint_button_,fill_button_,folder_button_,mask_button_,effects_button_})button->setToolTip(unavailable.isEmpty()?button->accessibleName():unavailable);
  {const TextureLayer *selected=nullptr;if(set)for(const auto &layer:set->layers)if(layer.id==set->selected_layer)selected=&layer;
   const bool editable=set&&set->uniform_editable&&selected&&!selected->instance&&selected->kind!=LayerKind::Folder&&!set->selected_mask;
   displacement_panel_->synchronize(set?set->displacement:DisplacementSettings{},editable,selected?std::optional<bool>(set->displacement.channel&&selected->displacement.enabled):std::nullopt);}
  const bool has_scattering=set&&std::any_of(set->users.begin(),set->users.end(),[](const auto &c){return c.scattering&&c.present;});
  const bool can_scatter=set&&std::any_of(set->users.begin(),set->users.end(),[](const auto &c){return c.scattering||!c.allocated;});
  scattering_add_->setEnabled(set&&set->uniform_editable&&!has_scattering&&can_scatter);
  for(int slot=0;slot<custom_channel_count;++slot){const auto name=QString::fromStdString(set?set->users[slot].name:std::string("User")+std::to_string(slot));const bool present=set&&set->users[slot].present;user_rows_[slot]->setVisible(present);user_rows_[slot]->setEnabled(set&&set->uniform_editable);user_add_[slot]->setVisible(!set||!set->users[slot].scattering);user_add_[slot]->setEnabled(set&&set->uniform_editable&&!present);user_names_[slot]->setReadOnly(set&&set->users[slot].scattering);user_add_[slot]->setText(name);const QSignalBlocker a(user_names_[slot]),b(user_formats_[slot]);if(user_names_[slot]->text()!=name)user_names_[slot]->setText(name);user_formats_[slot]->setCurrentIndex(int(set?set->users[slot].precision:ChannelPrecision::L32F));}

  for(int slot=0;slot<custom_channel_count;++slot)user_formats_[slot]->setEnabled(set&&set->uniform_editable&&!set->disabled&&!opacity_stack);
  if(opacity_stack){scattering_add_->setEnabled(false);for(int slot=0;slot<custom_channel_count;++slot){user_add_[slot]->setEnabled(false);user_names_[slot]->setReadOnly(true);}}
  for(int c=0;c<6;++c){const QSignalBlocker block(channel_formats_[c]);channel_formats_[c]->setEnabled(set&&set->uniform_editable&&(c==5?set->ao_channel:set->channels[c]));channel_formats_[c]->setCurrentIndex(int(set?set->precision[c]:default_channel_precision[c]));}
  ao_channel_row_->setVisible(set&&set->ao_channel);ao_add_action_->setEnabled(set&&set->uniform_editable&&!set->ao_channel);{const QSignalBlocker blocker(ao_mixing_);ao_mixing_->setEnabled(set&&set->uniform_editable&&set->ao_channel);ao_mixing_->setCurrentIndex(ao_mixing_->findData(set&&set->ao_channel?int(set->ao_mixing):0));}
  {const QSignalBlocker blocker(bump_method_);bump_method_->setEnabled(set&&set->uniform_editable&&set->channels[4]);bump_method_->setCurrentIndex(set&&set->channels[4]?int(set->bump_method):0);}
  {const QSignalBlocker blocker(normal_mixing_);normal_mixing_->setEnabled(set&&set->uniform_editable&&set->channels[3]);normal_mixing_->setCurrentIndex(normal_mixing_->findData(set&&set->channels[3]?int(set->normal_mixing):0));}
  findChild<QScrollArea *>("textureChannelsScroll")->setFixedHeight(20+30*(set?int(std::count(set->channels.begin(),set->channels.end(),true))-int(set->channels[3])+int(set->ao_channel)+int(std::count_if(set->users.begin(),set->users.end(),[](const auto &user){return user.present;})):0));
  for (int i = 0; i < 5; ++i) {
    channel_rows_[i]->setVisible(i!=3 && set && set->channels[i]);
    channel_rows_[i]->setEnabled(set && set->uniform_editable);
    channel_add_actions_[i]->setVisible(i!=3);
    channel_add_actions_[i]->setEnabled(i!=3 && set && set->uniform_editable && !set->channels[i]);
  }
  {
    const QSignalBlocker blocker(layer_context_);
    layer_context_->clear();
    for(int c=0;c<5;++c)if(c!=3&&set&&set->channels[c])layer_context_->addItem(channel_names[c],c);
    if(set&&set->ao_channel)layer_context_->addItem("Ambient occlusion",5);
    if(set)for(int slot=0;slot<custom_channel_count;++slot)if(set->users[slot].present)layer_context_->addItem(QString::fromStdString(set->users[slot].name),6+slot);
    if(set&&set->displacement.channel)layer_context_->addItem("Displacement",14);
    int index=layer_context_->findData(layer_channel_);
    if(index<0)index=layer_context_->count()?0:-1;
    layer_context_->setCurrentIndex(index);
    if(index>=0)layer_channel_=layer_context_->currentData().toInt();
    layer_context_->setEnabled(index>=0);
  }
  {const QSignalBlocker block(uv_tiles_);if(!set||displayed_set_!=set->id||!set->tiles.contains(active_tile_))active_tile_=0;// A mesh without UDIMs has the single 1001 tile and nothing to choose between,
    // so the list is only shown once there is more than one tile.
    uv_tiles_->setVisible(set!=nullptr&&set->tiles.size()>1);
    bool rebuild=uv_tiles_->topLevelItemCount()!=1;if(set&&!rebuild){const auto *root=uv_tiles_->topLevelItem(0);rebuild=root->childCount()!=int(set->tiles.size());int index=0;for(int number:set->tiles)if(!rebuild&&root->child(index++)->data(0,Qt::UserRole).toInt()!=number)rebuild=true;}if(!set)rebuild=true;
    if(rebuild){uv_tiles_->clear();if(set){auto *root=new QTreeWidgetItem(uv_tiles_,{QString("UV Tiles (%1)").arg(set->tiles.size())});for(int number:set->tiles){auto *item=new QTreeWidgetItem(root,{QString::number(number)});item->setData(0,Qt::UserRole,number);if(number==active_tile_)uv_tiles_->setCurrentItem(item);}root->setExpanded(true);}}
    if(set&&uv_tiles_->topLevelItemCount()){auto *root=uv_tiles_->topLevelItem(0);for(int i=0;i<root->childCount();++i){auto *item=root->child(i);const int tile=item->data(0,Qt::UserRole).toInt();item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(0,set->tile_loaded(tile)?Qt::Checked:Qt::Unchecked);item->setForeground(0,QColor(set->tile_loaded(tile)?theme::Text:theme::Disabled));item->setToolTip(0,set->tile_loaded(tile)?"Loaded — uncheck to hide and unload":"Unloaded — check to load from cache");item->setDisabled(!set->loaded);}}
  }
  settings_tile_->setVisible(active_tile_>0);settings_general_->setVisible(!active_tile_);
  if(set){const QSignalBlocker block(tile_resolution_);tile_resolution_->setItemText(0,set->texture_resolution==set->rows()?QString("%1 (Default)").arg(set->texture_resolution):QString("%1 × %2 (Default)").arg(set->texture_resolution).arg(set->rows()));const int value=set->tile_resolutions.contains(active_tile_)?set->tile_resolutions.at(active_tile_):0;refresh_texture_extent_choices(*tile_resolution_,128,value);tile_resolution_->setEnabled(active_tile_>0&&set->uniform_editable);tile_resolution_->setToolTip(QString("Evaluation size for UV tile %1; original paint is retained").arg(active_tile_));}
  {const QSignalBlocker blocker(padding_control_);padding_control_->setEnabled(set&&set->uniform_editable);padding_control_->setCurrentIndex(!set||set->padding_3d?1:0);}
  section=std::make_unique<performance::Scope>("show_texture_set_properties");
  const bool same_focus=set && displayed_set_==set->id && layers_->currentItem() && QUuid(layers_->currentItem()->data(Qt::UserRole).toString())==set->selected_layer;
  const bool multiple=same_focus&&layers_->selectedItems().size()!=1;
  displayed_set_=set?set->id:QUuid{};
  const TextureLayer *selected_instance_layer=nullptr;if(set)for(const auto &layer:set->layers)if(layer.id==set->selected_layer)selected_instance_layer=&layer;
  const bool instance_target=selected_instance_layer&&selected_instance_layer->instance.has_value();const bool hierarchy_target=set&&set->id==instance_hierarchy_set_&&set->selected_layer==instance_hierarchy_layer_&&!multiple;
  instance_panel_->synchronize(set,selected_instance_layer,hierarchy_target);instance_empty_->setVisible(instance_target&&!hierarchy_target);copy_instance_->setEnabled(set&&!multiple&&set->selected_effect.isNull());paste_instance_->setEnabled(set&&set->uniform_editable&&!multiple&&instance_clipboard_available_);
  const bool geometry_target=set&&set->selected_geometry&&!multiple&&!instance_target&&!hierarchy_target;
  const TextureLayer *geometry_layer=nullptr;if(geometry_target)for(const auto &layer:set->layers)if(layer.id==set->selected_layer)geometry_layer=&layer;geometry_panel_->synchronize(set,geometry_layer,settings&&settings->uv_workflow==UvWorkflow::PreserveTiles);
  bool effect_target=set&&!set->selected_effect.isNull()&&!multiple&&!geometry_target;
  const LevelsEffect *selected_effect=nullptr;const MaskEffect *selected_mask_effect=nullptr;if(effect_target)for(const auto &layer:set->layers)if(layer.id==set->selected_layer){if(set->selected_mask&&layer.mask){selected_mask_effect=find_mask_effect(layer.mask->effects,set->selected_effect);}else for(const auto &effect:layer.effects)if(effect.id==set->selected_effect)selected_effect=&effect;}
  if(selected_effect&&selected_effect->channel==3){effect_target=false;selected_effect=nullptr;}
  const bool folder_target=selected_instance_layer&&selected_instance_layer->kind==LayerKind::Folder&&!set->selected_mask&&!effect_target&&!multiple&&!geometry_target&&!instance_target&&!hierarchy_target;
  static_cast<FolderPropertiesPanel *>(findChild<QWidget *>("folderProperties"))->synchronize(set,folder_target?selected_instance_layer:nullptr,layer_channel_);
  auto *linear_properties=findChild<QLabel *>("linearColorProperties");const bool linear_target=selected_effect&&selected_effect->kind==ContentEffectKind::LinearColorTransform;linear_properties->setVisible(linear_target);if(linear_target)linear_properties->setText(selected_effect->linear_color?QString("Source: %1\nWorking space: ACEScg\n\nConverts the linear source before Fill tint. Use the eye to enable or bypass this conversion.").arg(QString::fromStdString(selected_effect->linear_color->input_space)):QString("The source color transform is unavailable."));
  const bool paint_effect=set&&TextureDocument::paint_effect(*set);
  const bool mask_filling=selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Fill;
  std::optional<MaskEffect> content_anchor;if(selected_effect&&selected_effect->kind==ContentEffectKind::Anchor){content_anchor=MaskEffect{};content_anchor->kind=MaskEffectKind::Anchor;content_anchor->id=selected_effect->id;content_anchor->anchor_name=selected_effect->anchor_name;}anchor_panel_->synchronize(set,content_anchor?&*content_anchor:selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Anchor?selected_mask_effect:nullptr);
  imported_effect_panel_->synchronize(selected_effect&&selected_effect->kind==ContentEffectKind::Fill?selected_effect:nullptr,selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Group?selected_mask_effect:nullptr);
  const auto *mask_filter=selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Filter?selected_mask_effect:nullptr;const auto *content_filter=selected_effect&&selected_effect->kind==ContentEffectKind::Filter?selected_effect:nullptr;filter_panel_->synchronize(set,mask_filter?&mask_filter->filter:content_filter?&content_filter->filter:nullptr,content_filter?content_filter->channel:-1,mask_filter?mask_filter->opacity:content_filter?content_filter->opacity:1,mask_filter?mask_filter->blending:content_filter?content_filter->blending:LayerBlend::Normal);
  paint_effect_mix_->setVisible(paint_effect);if(paint_effect){const QSignalBlocker a(paint_effect_blend_),b(paint_effect_opacity_);const auto mode=selected_mask_effect?selected_mask_effect->blending:selected_effect->blending;const float opacity=selected_mask_effect?selected_mask_effect->opacity:selected_effect->opacity;paint_effect_blend_->setCurrentIndex(paint_effect_blend_->findData(int(mode)));paint_effect_opacity_->setValue(opacity*100);}
  std::optional<LayerMask> mask_input;std::optional<LevelsEffect> mask_levels;
  if(selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Levels){const auto &selected=*std::find_if(set->layers.begin(),set->layers.end(),[&](const auto &layer){return layer.id==set->selected_layer;});mask_input=mask_effect_input(*selected.mask,set->selected_effect);mask_levels=selected_mask_effect->levels;mask_levels->id=selected_mask_effect->id;selected_effect=&*mask_levels;}
  compare_mask_panel_->synchronize(set,selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Compare?selected_mask_effect:nullptr);property_content_->setVisible((!effect_target||paint_effect)&&!geometry_target&&!instance_target&&!hierarchy_target&&!folder_target&&!multiple);levels_panel_->synchronize(set,selected_effect&&selected_effect->kind==ContentEffectKind::Levels?selected_effect:nullptr,mask_input?&*mask_input:nullptr);color_selection_panel_->synchronize(set,selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::ColorSelection?selected_mask_effect:nullptr);mask_fill_panel_->synchronize(selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Fill?selected_mask_effect:nullptr,set);std::optional<MaskEffect> content_generator;if(selected_effect&&selected_effect->kind==ContentEffectKind::Generator){content_generator=MaskEffect{};auto &effect=*content_generator;effect.kind=MaskEffectKind::Generator;static_cast<MeshGeneratorSettings &>(effect)=*selected_effect;effect.id=selected_effect->id;effect.enabled=selected_effect->enabled;effect.name=selected_effect->name;effect.opacity=selected_effect->opacity;effect.blending=selected_effect->blending;}mask_generator_panel_->synchronize(set,content_generator?&*content_generator:selected_mask_effect&&selected_mask_effect->kind==MaskEffectKind::Generator?selected_mask_effect:nullptr,content_generator?selected_effect->channel:-1);effects_button_->setEnabled(set&&set->uniform_editable&&!multiple&&!geometry_target&&!instance_target&&!hierarchy_target);for(auto *action:effects_button_->menu()->actions())action->setEnabled(effects_button_->isEnabled());effects_button_->menu()->findChild<QAction *>("addMaskFillEffect")->setEnabled(effects_button_->isEnabled()&&set->selected_mask);
  effects_button_->menu()->findChild<QAction *>("addMaskGenerator")->setEnabled(set&&set->uniform_editable&&!multiple&&!geometry_target&&!instance_target&&!hierarchy_target);effects_button_->menu()->findChild<QAction *>("addMaskAnchor")->setEnabled(set&&set->uniform_editable&&!multiple&&!geometry_target&&!instance_target&&!hierarchy_target);
  effect_up_->setEnabled(effect_target);effect_down_->setEnabled(effect_target);

  const bool mask_target=set&&(set->selected_mask||quick_mask_edit_)&&!multiple;
  quick_mask_enable_->setEnabled(set&&set->uniform_editable);
  mask_button_->setEnabled(set&&set->uniform_editable&&!multiple&&!instance_target&&!hierarchy_target);
  for(auto *action:mask_button_->menu()->actions())action->setEnabled(mask_button_->isEnabled());
  const bool has_mask=selected_instance_layer&&selected_instance_layer->mask;
  for(const char *name:{"copyMaskContent","pasteIntoMask","createSmartMask","invertMaskBackground","removeMask"})if(auto *action=mask_button_->menu()->findChild<QAction *>(name))action->setEnabled(mask_button_->isEnabled()&&has_mask&&(QLatin1StringView(name)!=QStringView(u"pasteIntoMask")||mask_clipboard_available_));
  mask_value_row_->setVisible(mask_target&&!polygon_active_&&!(brush&&(brush->erase||brush->smudge||brush->clone)));
  if(brush){const QSignalBlocker blocker(mask_value_);mask_value_->setValue(brush->mask_value);}
  const bool folder=set && !mask_target && std::any_of(set->layers.begin(),set->layers.end(),[&](const auto &layer){return layer.id==set->selected_layer&&layer.kind==LayerKind::Folder;});
  folder_button_->setEnabled(set&&set->uniform_editable);
  if(brush) {
    if(selected_instance_layer&&(selected_instance_layer->kind!=LayerKind::Fill||mask_target||paint_effect)){performance::Scope alpha_phase("brush_alpha_preview_ui");alpha_panel_->synchronize(*brush);}const QSignalBlocker a(brush_alignment_),b(brush_cull_),c(brush_cull_angle_),d(brush_cull_slider_);
    brush_alignment_->setCurrentIndex(int(brush->projection.alignment));brush_cull_->setChecked(brush->projection.cull);brush_cull_angle_->setValue(brush->projection.angle);brush_cull_slider_->setValue(qRound(brush->projection.angle*10));
    const bool culling=brush->projection.alignment!=BrushAlignment::UV;brush_cull_->setEnabled(culling);brush_cull_angle_->setEnabled(culling&&brush->projection.cull);brush_cull_slider_->setEnabled(culling&&brush->projection.cull);
  }
  const auto fill=set?std::find_if(set->layers.begin(),set->layers.end(),[&](const auto &layer){return layer.id==set->selected_layer;}):std::vector<TextureLayer>::const_iterator{};
  const bool filling=set&&!geometry_target&&!instance_target&&!hierarchy_target&&!effect_target&&!multiple&&!mask_target&&fill!=set->layers.end()&&fill->kind==LayerKind::Fill;fill_panel_->setVisible(filling);fill_toolbar_->setVisible((filling&&fill->fill.mode!=6)||(mask_filling&&selected_mask_effect->anchor_source.isNull()));for(auto *action:fill_gizmo_actions_)action->setEnabled((filling&&fill->fill.mode!=6)||(mask_filling&&selected_mask_effect->anchor_source.isNull()&&selected_mask_effect->mapping.projection.mode!=6));if(filling){displayed_fill_=fill->fill;std::array<bool,15> active{};QStringList names{"Base Color","Metallic","Roughness","Normal","Bump","Ambient occlusion"};for(int c=0;c<5;++c)active[c]=set->channels[c];active[5]=set->ao_channel;for(int slot=0;slot<8;++slot){active[slot+6]=set->users[slot].present;names.append(QString::fromStdString(set->users[slot].name));}active[14]=set->displacement.channel;names.append("Displacement");fill_panel_->set_displacement_midlevel(set->displacement.midlevel);fill_panel_->set_channels(active,names);fill_panel_->set_anchor_context(set,set->selected_layer);fill_panel_->synchronize(fill->fill);auto *resolution_warning=fill_panel_->findChild<QLabel *>("fillResolutionWarning");if(!resolution_warning){resolution_warning=new QLabel(fill_panel_);resolution_warning->setObjectName("fillResolutionWarning");resolution_warning->setWordWrap(true);resolution_warning->setStyleSheet(bts::theme::qss("color:@Warning@"));fill_panel_->layout()->addWidget(resolution_warning);}QStringList source_warnings;for(int c=0;c<15;++c)if(active[c])if(const auto &source=fill_channel_texture(fill->fill,c)){const auto check=[&](int tile,const auto &image){if(image.width>set->width(tile)||image.height>set->rows(tile))source_warnings.append(names.value(c)+QString(": source %1×%2 > Texture Set %3×%4 (tile %5)").arg(image.width).arg(image.height).arg(set->width(tile)).arg(set->rows(tile)).arg(tile));};if(source->tiles){for(const auto &[tile,image]:*source->tiles)if(set->tiles.contains(tile))check(tile,*image);}else check(1001,*source);}resolution_warning->setText(source_warnings.join('\n'));resolution_warning->setVisible(!source_warnings.empty());const QSignalBlocker a(fill_manipulator_),b(fill_gizmo_size_);fill_manipulator_->setChecked(fill->fill.manipulator);fill_manipulator_->setText(fill->fill.mode==0?"UV frame":"Projection frame");fill_spatial_toolbar_->setVisible(fill->fill.mode!=6);fill_gizmo_size_->setValue(fill->fill.gizmo_size);for(int i=0;i<3;++i){const QSignalBlocker blocker(fill_gizmos_[i]);fill_gizmos_[i]->setChecked(i==fill->fill.gizmo);}}
  if(filling){QString material_name;if(!fill->fill.material_resource.empty()&&asset_catalog_){const QUuid id(QString::fromStdString(fill->fill.material_resource));for(const auto &asset:asset_catalog_->assets())if(asset.id==id){material_name=asset.name;break;}}fill_panel_->set_material_mode_text(material_name);}
  if(mask_filling){displayed_fill_=selected_mask_effect->mapping.projection;const QSignalBlocker block(fill_manipulator_);fill_manipulator_->setChecked(displayed_fill_.manipulator);fill_manipulator_->setText(fill_surface_projection(displayed_fill_)?"Manipulator":"UV frame");fill_spatial_toolbar_->setVisible(displayed_fill_.mode!=6);}
  findChild<QWidget *>("propertiesProjectionModes")->setVisible((filling||mask_filling)&&displayed_fill_.mode!=6);for(int mode:{0,1}){auto *button=findChild<QToolButton *>(mode==0?"propertiesUVProjection":"propertiesTriplanarProjection");const QSignalBlocker block(button);button->setChecked(displayed_fill_.mode==mode);}
  if(filling||mask_filling){displayed_fill_.cm_per_native=set->cm_per_native;displayed_fill_.native_per_uv=set->native_per_uv;}
  {const QSignalBlocker block(warp_geometry_toggle_);warp_geometry_toggle_->setVisible((filling||mask_filling)&&displayed_fill_.mode==5);warp_geometry_toggle_->setChecked(warp_geometry_enabled(displayed_fill_));warp_geometry_toggle_->setEnabled(set&&set->uniform_editable);}
  const bool painting=set && !geometry_target && !instance_target && !hierarchy_target && (!effect_target||paint_effect) && !multiple && brush && std::any_of(set->layers.begin(),set->layers.end(),[&](const auto &layer){return layer.id==set->selected_layer && (layer.kind==LayerKind::Paint||mask_target||paint_effect);});
  const bool smart_ready=set&&worn_metal_ready(*set);const bool user_ready=set&&asset_catalog_&&std::any_of(asset_catalog_->assets().begin(),asset_catalog_->assets().end(),[](const auto &asset){return (bool(asset.material_preset)||is_spsm_asset(asset))&&asset_in_category(asset,"Material presets");});smart_material_button_->setEnabled(smart_ready||user_ready);smart_material_button_->setToolTip(smart_ready||user_ready?"Add material preset":"Import a Material Preset into Assets to use it");
  const bool projection_ready=painting&&brush&&brush->projected.has_value();
  const bool changed_asset_context=asset_projection_ready_!=projection_ready||asset_smart_ready_!=smart_ready||asset_project_ready_!=bool(set)||asset_paint_ready_!=(painting&&!polygon_active_)||asset_fill_ready_!=filling;asset_projection_ready_=projection_ready;asset_smart_ready_=smart_ready;asset_project_ready_=bool(set);asset_paint_ready_=painting&&!polygon_active_;asset_fill_ready_=filling;if(changed_asset_context)QTimer::singleShot(0,this,[this]{refresh_alpha_shelf();});
  brush_controls_->setVisible(painting&&!polygon_active_);brush_toolbar_->setVisible(!geometry_target&&!instance_target&&!hierarchy_target&&!polygon_active_&&!filling&&!mask_filling);polygon_toolbar_->setVisible(painting&&polygon_active_);polygon_controls_->setVisible(painting&&polygon_active_);for(int which=0;which<2;++which){polygon_values_[which]->parentWidget()->setVisible(mask_target);polygon_sliders_[which]->setVisible(mask_target);}polygon_tool_->setEnabled(painting&&!quick_mask_edit_);polygon_action_->setEnabled(painting&&!quick_mask_edit_);paint_tool_->setEnabled(painting);eraser_tool_->setEnabled(painting);
  const bool can_pick=(painting&&!polygon_active_)||(filling&&set&&set->uniform_editable&&!fill->instance&&!fill->fill.native_material);
  picker_tool_->setEnabled(can_pick);picker_action_->setEnabled(can_pick);
  paint_tool_action_->setEnabled(painting);eraser_tool_action_->setEnabled(painting);
  paint_tool_->setChecked(painting&&!polygon_active_&&!path_active_&&!brush->erase&&!brush->projected&&!brush->smudge&&!brush->clone);smudge_tool_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);smudge_action_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);smudge_tool_->setChecked(painting&&brush->smudge);eraser_tool_->setChecked(painting&&brush->erase);projection_tool_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);projection_action_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);projection_tool_->setChecked(painting&&bool(brush->projected));
  clone_tool_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);clone_action_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);clone_tool_->setChecked(painting&&brush->clone);clone_relative_->setChecked(painting&&!brush->clone_absolute);clone_absolute_->setChecked(painting&&brush->clone_absolute);clone_tool_->setAccessibleName(brush&&brush->clone_absolute?"Clone (absolute source)":"Clone (relative source)");clone_tool_->setToolTip(clone_tool_->accessibleName()+" (6) — V+click to sample selected layer");
  path_tool_->setEnabled(painting&&!polygon_active_&&!quick_mask_edit_);path_action_->setEnabled(path_tool_->isEnabled());path_tool_->setChecked(path_active_&&painting);
  lazy_toggle_->setEnabled(painting&&!path_active_);lazy_action_->setEnabled(painting&&!path_active_);lazy_controls_->setEnabled(painting&&!path_active_&&brush->lazy_mouse);
  toolbar_spacing_->setEnabled(painting);toolbar_spacing_slider_->setEnabled(painting);
  toolbar_opacity_->setEnabled(painting);toolbar_opacity_slider_->setEnabled(painting);
  toolbar_flow_->setEnabled(painting);toolbar_flow_slider_->setEnabled(painting);
  toolbar_size_->setEnabled(painting);toolbar_size_slider_->setEnabled(painting);
  paint_button_->setEnabled(set && set->uniform_editable && bool(callbacks_.add_paint));
  if (brush) {
    if(painting&&!polygon_active_)brush_preview_->synchronize(*brush);
    stencil_panel_->synchronize(brush->stencil);stencil_panel_->setVisible(!brush->projected);
    auto projection=brush->projected;if(projection)projection->values=brush->values;projection_panel_->synchronize(projection?&*projection:nullptr);
    {const QSignalBlocker angle(brush_angle_),follow(brush_follow_path_);brush_angle_->setValue(brush->angle);brush_follow_path_->setChecked(brush->follow_path);brush_follow_path_->setText(brush->follow_path?"On":"Off");}
    const auto jitter=brush->jitter.values();for(int i=0;i<4;++i){const QSignalBlocker spin(brush_jitter_[i]),slider(brush_jitter_sliders_[i]);brush_jitter_[i]->setValue(jitter[i]);brush_jitter_sliders_[i]->setValue(qRound(jitter[i]*10));}
    const QSignalBlocker a(brush_size_),b(toolbar_size_),c(toolbar_size_slider_);
    const QSignalBlocker d(brush_flow_),e(toolbar_flow_),f(brush_flow_slider_),g(toolbar_flow_slider_);
    const QSignalBlocker h(brush_opacity_),i(toolbar_opacity_),j(brush_opacity_slider_),k(toolbar_opacity_slider_);
    const QSignalBlocker lazy_a(lazy_toggle_),lazy_b(lazy_distance_),lazy_c(lazy_slider_);
    lazy_toggle_->setChecked(brush->lazy_mouse);lazy_distance_->setValue(brush->lazy_distance);lazy_slider_->setValue(qRound(brush->lazy_distance*10));
    const QSignalBlocker spacing_a(brush_spacing_),spacing_b(toolbar_spacing_),spacing_c(brush_spacing_slider_),spacing_d(toolbar_spacing_slider_);
    brush_spacing_->setValue(brush->spacing);toolbar_spacing_->setValue(brush->spacing);
    brush_spacing_slider_->setValue(qRound(brush->spacing*10));toolbar_spacing_slider_->setValue(qRound(brush->spacing*10));
    brush_opacity_->setValue(brush->stroke_opacity);toolbar_opacity_->setValue(brush->stroke_opacity);
    brush_opacity_slider_->setValue(qRound(brush->stroke_opacity*10));toolbar_opacity_slider_->setValue(qRound(brush->stroke_opacity*10));
    brush_flow_->setValue(brush->flow);toolbar_flow_->setValue(brush->flow);
    brush_flow_slider_->setValue(qRound(brush->flow*10));toolbar_flow_slider_->setValue(qRound(brush->flow*10));
    brush_size_->setValue(brush->diameter);toolbar_size_->setValue(brush->diameter);toolbar_size_slider_->setValue(qRound(brush->diameter*10));
    for(int i=0;i<5;++i){const QSignalBlocker blocker(brush_channels_[i]);brush_channels_[i]->setChecked(set&&fill!=set->layers.end()&&fill->channels[i]&&set->channels[i]);brush_channels_[i]->setEnabled(set&&set->uniform_editable&&fill!=set->layers.end()&&!fill->instance&&!mask_target&&i!=3);brush_channels_[i]->setToolTip(QString("%1 layer channel. Turning off clears its authored data; Undo restores it.").arg(channel_names[i]));brush_channels_[i]->setVisible(i!=3&&!mask_target);}
  }
  for(int index=0;index<2;++index) {
    const QSignalBlocker a(brush_pressure_[index]),b(toolbar_pressure_[index]),c(brush_minimum_[index]);
    const bool enabled=brush&&(index==0?brush->pressure_size:brush->pressure_flow);
    brush_pressure_[index]->setChecked(enabled);toolbar_pressure_[index]->setChecked(enabled);
    brush_pressure_[index]->setEnabled(painting);toolbar_pressure_[index]->setEnabled(painting);
    brush_minimum_[index]->setEnabled(painting&&enabled);
    if(brush)brush_minimum_[index]->setValue(index==0?brush->minimum_size:brush->minimum_flow);
  }
  for(int slot=0;slot<custom_channel_count;++slot){const bool present=set&&set->users[slot].present&&!folder&&!multiple&&!mask_target;const auto name=QString::fromStdString(set?set->users[slot].name:std::string{});const bool enabled=present&&(painting?brush->users[slot]:fill->users[slot].enabled);const float value=present?(painting?brush->user_values[slot]:fill->users[slot].value):1;{const QSignalBlocker a(user_brush_[slot]),b(user_fill_enabled_[slot]),c(user_values_[slot]),d(user_sliders_[slot]);user_brush_[slot]->setVisible(present);user_brush_[slot]->setEnabled(present&&set->uniform_editable);user_brush_[slot]->setChecked(enabled);user_brush_[slot]->setText(name.compare("Scattering",Qt::CaseInsensitive)==0?QString("scatt"):name.toLower().left(6));user_brush_[slot]->setAccessibleName(name);user_brush_[slot]->setToolTip(name);user_value_rows_[slot]->setVisible(present&&(!painting||enabled)&&!(painting&&(brush->erase||brush->smudge||brush->clone)));user_value_labels_[slot]->setText(name);user_values_[slot]->setAccessibleName(name+" value");user_values_[slot]->setEnabled(enabled);user_values_[slot]->setValue(value);user_sliders_[slot]->setProperty("userFillPropertyEdit",filling&&enabled&&set->uniform_editable&&!material_layering_main_readonly(*set));user_sliders_[slot]->setEnabled(enabled);user_sliders_[slot]->setValue(qRound(value*1000));user_fill_enabled_[slot]->setVisible(false);user_fill_enabled_[slot]->setChecked(enabled);}}
  {const bool available=set&&!folder&&!multiple&&!mask_target&&fill!=set->layers.end()&&!fill->instance;const bool active=available&&set->displacement.channel&&fill->displacement.enabled;auto *toggle=findChild<QToolButton *>("brushDisplacement");auto *value=findChild<QDoubleSpinBox *>("materialDisplacement");const QSignalBlocker a(toggle),b(value);findChild<QWidget *>("materialDisplacementRow")->setVisible(active);toggle->setVisible(painting&&!mask_target);toggle->setChecked(active);toggle->setEnabled(available&&set->uniform_editable);toggle->setToolTip("Displacement layer channel. Turning off clears its authored data; Undo restores it.");value->setValue(active?(painting?brush->displacement_value:fill->displacement.value):set?set->displacement.midlevel:.5);value->setEnabled(active&&!(painting&&(brush->erase||brush->smudge||brush->clone)));}
  const auto visible=[&](int channel){return set && !folder && !multiple && !mask_target && !(painting&&(brush->erase||brush->smudge||brush->clone)) && set->channels[channel] && (!painting || brush->channels[channel]);};
  const bool ao_visible=set&&set->ao_channel&&!folder&&!multiple&&!mask_target&&!(painting&&brush->erase);brush_ao_->setVisible(ao_visible);ao_value_row_->setVisible(ao_visible&&(!painting||brush->ao)&&!(painting&&(brush->smudge||brush->clone)));{const QSignalBlocker blocker(brush_ao_);brush_ao_->setChecked(set&&(painting?brush->ao:std::find_if(set->layers.begin(),set->layers.end(),[&](const auto &layer){return layer.id==set->selected_layer;})->ao_enabled));brush_ao_->setEnabled(set&&set->uniform_editable&&!multiple&&!mask_target);}
  color_row_->setVisible(visible(0));secondary_color_row_->setVisible(painting&&!mask_target&&!folder);
  metallic_row_->setVisible(visible(1));
  roughness_row_->setVisible(!set || visible(2));
  // The roughness field is its own slider now, and the fill ramp takes over in
  // fill context, so this track is never a visible control. It stays alive
  // because the gesture a drag across the field starts still runs through it.
  roughness_slider_->hide();
  normal_label_->setVisible(false);normal_row_->setVisible(false);if(auto *bar=findChild<QWidget *>("fillTangentBump"))bar->hide();
  bump_label_->setVisible(visible(4));if(auto *bump_editor=findChild<QWidget *>("bumpEditor"))bump_editor->setVisible(visible(4));
  fill_button_->setEnabled(set && set->uniform_editable && bool(callbacks_.add_fill));
  if (set) {
    const auto &values=painting ? brush->values : TextureDocument::property_values(*set);
    normal_label_->setText("Normal");for(int component=0;component<3;++component){const QSignalBlocker blocker(normal_values_[component]);normal_values_[component]->setValue(values.normal[component]);normal_values_[component]->setEnabled(set->uniform_editable&&visible(3));}
    if(auto *bar=findChild<QWidget *>("fillTangentBump"))static_cast<TangentBumpBar *>(bar)->set_normal(values.normal);
    fill_panel_->set_material_preview(values.base_color,values.metallic,values.roughness,values.normal);
    {const QSignalBlocker blocker(ao_value_);ao_value_->setValue(values.ambient_occlusion);ao_value_->setEnabled(set->uniform_editable);}
    bump_label_->setText("Bump");{const QSignalBlocker blocker(bump_control_);bump_control_->setValue(painting?values.bump:set->bump_display(values.bump));bump_control_->setEnabled(set->uniform_editable&&visible(4));}
    const QSignalBlocker metallic_blocker(metallic_control_);
    metallic_control_->setValue(values.metallic);
    metallic_control_->setEnabled(set->uniform_editable);
    set_roughness(values.roughness);
    color_picker_->set_color(values.base_color);color_picker_->setEnabled(set->uniform_editable);
  }
  auto *channel_panel=fill_panel_->channel_panel();fill_panel_->set_fill_context(filling);if(filling){std::array<bool,15> enabled{};for(int c=0;c<5;++c)enabled[c]=fill->channels[c]&&set->channels[c];enabled[5]=fill->ao_enabled&&set->ao_channel;for(int c=0;c<8;++c)enabled[c+6]=fill->users[c].enabled;enabled[14]=fill->displacement.enabled&&set->displacement.channel;fill_panel_->set_enabled_channels(enabled);}fill_panel_->refresh_channel_visibility();if(!filling&&!painting)channel_panel->hide();for(int c=0;c<15;++c){const auto key=shown_texture_set_.toString()+shown_texture_layer_.toString()+QString::number(c);const auto state=texture_load_states_.value(key);fill_panel_->set_load_status(c,state.value(0).toString(),state.value(1).toInt(),state.value(2).toInt(),state.value(3).toBool(),state.value(4).toBool());}
  update_rows(same_focus,multiple,effect_target,geometry_target,hierarchy_target,instance_target,painting,mask_target);
  // Last, once every control above has decided whether it is shown: a section
  // with nothing visible in it takes its heading away with it.
  for(auto *section:property_sections_)section->sync_visibility();
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
  projection_control_->setCurrentIndex(projection == OrbitCamera::Projection::Orthographic ? 1 : 0);
}

void WorkspaceShell::set_view_layout(ViewLayoutState layout)
{
  view_layout_ = layout;
  const QSignalBlocker blocker(view_layout_control_);
  view_layout_control_->setCurrentIndex(layout.mode == ViewLayout::SceneOnly ? 0 :
                                        layout.mode == ViewLayout::UvOnly ? 1 :
                                        layout.uv_first ? 3 : 2);
}

void WorkspaceShell::set_reference_image_enabled(bool enabled)
{
  if (!reference_image_toggle_) return;
  const QSignalBlocker blocker(reference_image_toggle_);
  reference_image_toggle_->setChecked(enabled);
}

void WorkspaceShell::set_reference_image_path(const QString &path)
{
  if (!open_reference_image_button_) return;
  const QString file = QFileInfo(path).fileName();
  open_reference_image_button_->setToolTip(
      file.isEmpty() ? QStringLiteral("Open a reference image for the 2D view")
                     : QStringLiteral("Reference image: %1 (click to change)").arg(path));
  if (reference_image_toggle_) {
    reference_image_toggle_->setToolTip(
        file.isEmpty()
            ? QStringLiteral("Show a 2D reference image in the 2D view instead of the UV layout and pick colors from it")
            : QStringLiteral("Show %1 in the 2D view instead of the UV layout and pick colors from it").arg(file));
  }
}

void WorkspaceShell::set_texture_sets(const QStringList &names, int selected_index)
{
  const QSignalBlocker blocker(texture_sets_);
  // Layer edits preserve the set list and its item identity. Rebuilding an
  // identical list needlessly invalidates the dock layout and viewport paint.
  if(texture_sets_->count()!=names.size()){
    texture_sets_->clear();texture_sets_->addItems(names);
  }else for(int i=0;i<names.size();++i)if(texture_sets_->item(i)->text()!=names[i])texture_sets_->item(i)->setText(names[i]);
  const int row = names.isEmpty() ? -1 : std::clamp(selected_index, 0, int(names.size()) - 1);
  for(int i=0;i<texture_sets_->count();++i){
    auto *item=texture_sets_->item(i);item->setFlags(item->flags()|Qt::ItemIsEditable);
    // Metadata renames intentionally skip the broad ShaderController refresh.
    // Keep the persistent row editor in step with the document's display name.
    if(auto *row_widget=texture_sets_->itemWidget(item))
      if(auto *name=row_widget->findChild<QLineEdit *>("textureSetName"))
        static_cast<InlineNameField *>(name)->synchronize(item->text());
  }
  texture_sets_->setCurrentRow(row);
  texture_sets_->setEnabled(!names.isEmpty() && bool(callbacks_.texture_set_selected));
  texture_sets_->setToolTip(names.isEmpty() ? QStringLiteral("No visible mesh materials.") :
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
  const QString tooltip = editable ? QStringLiteral("Material roughness") :
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
  auto *area=statusBar()->findChild<QWidget *>("workspaceError");
  if(!area){area=new QWidget(statusBar());area->setObjectName("workspaceError");auto *layout=new QHBoxLayout(area);layout->setContentsMargins(6,0,0,0);auto *label=new QLabel(area);label->setObjectName("workspaceErrorText");label->setStyleSheet(bts::theme::qss("color:@Error@"));label->setMaximumWidth(850);layout->addWidget(label);auto *dismiss=new QToolButton(area);dismiss->setObjectName("dismissWorkspaceError");dismiss->setText("×");dismiss->setToolTip("Dismiss error");connect(dismiss,&QToolButton::clicked,area,&QWidget::hide);layout->addWidget(dismiss);statusBar()->addPermanentWidget(area);}
  auto *label=area->findChild<QLabel *>("workspaceErrorText");label->setText(message);label->setToolTip(message);area->show();
  const int generation=area->property("errorGeneration").toInt()+1;area->setProperty("errorGeneration",generation);
  QTimer::singleShot(15000,area,[area,generation]{if(area->property("errorGeneration").toInt()==generation)area->hide();});
}

void WorkspaceShell::append_console(const QString &message)
{
  console_text_->appendPlainText(message);
}

NativeMaterialImagePanel *WorkspaceShell::native_image_panel()const{return fill_panel_->native_image_panel();}
NativeMaterialPanel *WorkspaceShell::native_material_panel()const{return fill_panel_->native_material_panel();}
void WorkspaceShell::show_texture_shelf(){set_shelf_visible(true);asset_scope_control_->setCurrentIndex(0);shelf_categories_->setCurrentRow(0);asset_search_->setText("u:texture");asset_search_->setFocus();refresh_alpha_shelf();}
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
  if(properties_float_)dock_properties();
  if(properties_slot_)show_properties_tab();
  if(properties_action_){const QSignalBlocker block(properties_action_);properties_action_->setChecked(true);}
  set_shelf_visible(true);
  set_console_visible(false);
  layer_settings_tabs_->setCurrentIndex(0);
  reference_layout_pending_ = !isVisible();
  const int width = std::max(968, workspace_splitter_->width() - handle_width);
  workspace_splitter_->setSizes({width - inspector_width, inspector_width});
  const int center_height = viewport_shelf_splitter_->height() - handle_width;
  const int shelf_height = std::max(shelf_minimum_height,
                                    qRound(centralWidget()->height() * 0.314017));
  viewport_shelf_splitter_->setSizes({292,std::max(350,viewport_shelf_splitter_->width()-292)});
  const int height = std::max(600, inspector_splitter_->height() - 2 * handle_width);
  const int layers = qRound(height * 0.53);
  const int console_height = 0;
  inspector_splitter_->setSizes(
      {layers, height - layers - console_height, console_height});
}

void WorkspaceShell::float_properties()
{
  if(properties_float_||!properties_panel_)return;
  hide_properties_tab();
  properties_float_=new QDialog(this, Qt::Tool);
  properties_float_->setObjectName("propertiesFloat");
  properties_float_->setAttribute(Qt::WA_DeleteOnClose, false);
  auto *title=findChild<QLabel *>("propertiesDockTitle");
  properties_float_->setWindowTitle(title?title->text():QString("PROPERTIES"));
  auto *layout=new QVBoxLayout(properties_float_);
  layout->setContentsMargins(0,0,0,0);
  properties_panel_->setParent(properties_float_);
  layout->addWidget(properties_panel_);
  properties_float_->resize(340,720);
  properties_undock_->setToolTip("Dock properties");
  properties_undock_->setAccessibleName("Dock properties");
  connect(properties_float_,&QDialog::finished,this,[this]{if(!properties_float_)return;auto *dialog=properties_float_;properties_float_=nullptr;properties_panel_->setParent(properties_slot_);static_cast<QVBoxLayout *>(properties_slot_->layout())->addWidget(properties_panel_);hide_properties_tab();properties_undock_->setToolTip("Undock properties");properties_undock_->setAccessibleName("Undock properties");const QSignalBlocker block(properties_action_);properties_action_->setChecked(false);dialog->deleteLater();});
  properties_float_->show();
  const QSignalBlocker block(properties_action_);
  properties_action_->setChecked(true);
}

void WorkspaceShell::show_update_available(const QString &release,const QUrl &page)
{
  if(!update_notice_||!update_label_)return;
  update_page_=page;
  update_label_->setText(QString("Update available — %1").arg(release));
  update_notice_->show();
}
PropertySection *WorkspaceShell::make_section(const QString &title,QWidget *parent,bool folded)
{
  auto *section=new PropertySection(title,parent,folded);
  property_sections_.push_back(section);
  return section;
}
void WorkspaceShell::show_properties_tab()
{
  if(!properties_slot_||!layer_settings_tabs_)return;
  if(layer_settings_tabs_->indexOf(properties_slot_)<0)layer_settings_tabs_->addTab(properties_slot_,"Properties");
  properties_slot_->show();
  layer_settings_tabs_->setCurrentWidget(properties_slot_);
}
void WorkspaceShell::hide_properties_tab()
{
  if(!properties_slot_||!layer_settings_tabs_)return;
  const int index=layer_settings_tabs_->indexOf(properties_slot_);
  if(index>=0)layer_settings_tabs_->removeTab(index);
  properties_slot_->hide();
}
void WorkspaceShell::dock_properties()
{
  if(!properties_float_)return;
  auto *dialog=properties_float_;
  properties_float_=nullptr;
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
  return QJsonDocument(QJsonObject{
      {"version", 2},
      {"workspace", QString::fromLatin1(workspace_splitter_->saveState().toBase64())},
      {"center", QString::fromLatin1(viewport_shelf_splitter_->saveState().toBase64())},
      {"inspector", QString::fromLatin1(inspector_splitter_->saveState().toBase64())},
      {"shelfVisible", !shelf_->isHidden()},
      {"consoleVisible", !console_->isHidden()},
      {"tab", layer_settings_tabs_->currentIndex()},
  }).toJson(QJsonDocument::Compact);
}

bool WorkspaceShell::restore_layout(const QByteArray &state)
{
  const QJsonDocument document = QJsonDocument::fromJson(state);
  if (!document.isObject()) {
    return false;
  }
  const QJsonObject data = document.object();
  if (data.value("version").toInt() != 2 || !data.value("shelfVisible").isBool() ||
      !data.value("consoleVisible").isBool() || !data.value("tab").isDouble()) {
    return false;
  }
  const int tab = data.value("tab").toInt(-1);
  if (tab < 0 || tab >= layer_settings_tabs_->count()) {
    return false;
  }
  const QByteArray workspace = QByteArray::fromBase64(data.value("workspace").toString().toLatin1());
  const QByteArray center = QByteArray::fromBase64(data.value("center").toString().toLatin1());
  const QByteArray inspector = QByteArray::fromBase64(data.value("inspector").toString().toLatin1());
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

void bts::WorkspaceShell::set_picker_active(bool active) {const QSignalBlocker blocker(picker_tool_);picker_tool_->setChecked(active);}

void bts::WorkspaceShell::set_polygon_fill(bool active,int mode,double value) {
  polygon_active_=active;const QSignalBlocker tool_block(polygon_tool_);polygon_tool_->setChecked(active);
  for(int which=0;which<2;++which){for(int i=0;i<5;++i){const QSignalBlocker blocker(polygon_modes_[which][i]);polygon_modes_[which][i]->setChecked(i==mode);}const QSignalBlocker a(polygon_values_[which]),b(polygon_sliders_[which]);polygon_values_[which]->setValue(value);polygon_sliders_[which]->setValue(qRound(value*1000));}
}

void bts::WorkspaceShell::set_asset_catalog(AssetCatalog *catalog){asset_catalog_=catalog;refresh_alpha_shelf();}

void bts::WorkspaceShell::set_id_picker_active(bool active){color_selection_panel_->set_picking(active);}

void bts::WorkspaceShell::edit_fill_projection(const FillProjection &p){fill_panel_->edit_projection(p);}
void bts::WorkspaceShell::set_warp_selection(std::vector<int> ids){(mask_fill_panel_->isVisible()?mask_fill_panel_->warp_panel():fill_panel_->warp_panel())->set_selection(std::move(ids));}
void bts::WorkspaceShell::cancel_warp_split(){(mask_fill_panel_->isVisible()?mask_fill_panel_->warp_panel():fill_panel_->warp_panel())->cancel_split();}
int bts::WorkspaceShell::fill_resource_channel()const{if(mask_fill_panel_&&mask_fill_panel_->isVisible())return mask_fill_panel_->resource_channel();return fill_panel_->resource_channel();}

namespace bts {
void WorkspaceShell::set_baking_tools(bool baking,bool editing,const BrushSettings &brush){
 if(!baking)return;brush_toolbar_->setVisible(editing&&!polygon_active_);polygon_toolbar_->setVisible(editing&&polygon_active_);paint_tool_->setEnabled(editing);eraser_tool_->setEnabled(editing);paint_tool_action_->setEnabled(editing);eraser_tool_action_->setEnabled(editing);paint_tool_->setChecked(editing&&!brush.erase&&!polygon_active_);eraser_tool_->setChecked(editing&&brush.erase&&!polygon_active_);polygon_tool_->setEnabled(editing);polygon_action_->setEnabled(editing);
 for(auto *w:std::initializer_list<QWidget *>{smudge_tool_,clone_tool_,projection_tool_,path_tool_,picker_tool_})w->setEnabled(false);quick_mask_enable_->setEnabled(false);for(auto *a:{smudge_action_,clone_action_,projection_action_,path_action_,picker_action_})a->setEnabled(false);
 for(auto *w:{toolbar_size_,toolbar_flow_,toolbar_opacity_,toolbar_spacing_})w->setEnabled(editing);for(auto *w:{toolbar_size_slider_,toolbar_flow_slider_,toolbar_opacity_slider_,toolbar_spacing_slider_})w->setEnabled(editing);
 const QSignalBlocker a(toolbar_size_),b(toolbar_flow_),c(toolbar_opacity_),d(toolbar_spacing_),e(toolbar_size_slider_),f(toolbar_flow_slider_),g(toolbar_opacity_slider_),h(toolbar_spacing_slider_);toolbar_size_->setValue(brush.diameter);toolbar_flow_->setValue(brush.flow);toolbar_opacity_->setValue(brush.stroke_opacity);toolbar_spacing_->setValue(brush.spacing);toolbar_size_slider_->setValue(qRound(brush.diameter*10));toolbar_flow_slider_->setValue(qRound(brush.flow*10));toolbar_opacity_slider_->setValue(qRound(brush.stroke_opacity*10));toolbar_spacing_slider_->setValue(qRound(brush.spacing*10));for(auto *w:toolbar_pressure_)w->setEnabled(editing);
}
}

void bts::WorkspaceShell::set_fill_color_spaces(const QStringList &spaces){fill_panel_->set_color_spaces(spaces);mask_fill_panel_->set_color_spaces(spaces);}

void bts::WorkspaceShell::set_texture_load_status(QUuid set,QUuid layer,int channel,const QString &text,int completed,int total,bool loading,bool error){const auto key=set.toString()+layer.toString()+QString::number(channel);texture_load_states_[key]={text,completed,total,loading,error};if(texture_load_states_.size()>4096)texture_load_states_.erase(texture_load_states_.begin());if(set==shown_texture_set_&&layer==shown_texture_layer_){fill_panel_->set_load_status(channel,text,completed,total,loading,error);mask_fill_panel_->set_load_status(channel,text,completed,total,loading,error);}texture_loading_bar_->setVisible(loading||error);texture_loading_notice_->setText(text);texture_loading_notice_->setStyleSheet(bts::theme::qss(error?"color:@Error@":""));texture_loading_progress_->setUpdatesEnabled(false);texture_loading_progress_->setVisible(loading);texture_loading_progress_->setRange(0,total);texture_loading_progress_->setValue(completed);texture_loading_progress_->setUpdatesEnabled(true);texture_loading_cancel_->setVisible(loading);setProperty("textureLoadPending",loading);setProperty("textureLoadError",error?text:QString{});}

namespace bts {
namespace {
QImage render_panel(QWidget *widget,int width){
  widget->ensurePolished();
  const int height=std::max(48,std::max(widget->sizeHint().height(),widget->height()));
  const QSize previous=widget->size();
  widget->resize(width,height);
  QImage image(width,height,QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  QPainter painter(&image);
  widget->render(&painter);
  widget->resize(previous);
  return image;
}
void save_properties(WorkspaceShell &shell,const QString &path){
  auto *properties=shell.findChild<QWidget *>("properties");
  auto *header=properties->findChild<QWidget *>("dockHeader");
  auto *body=shell.findChild<QWidget *>("materialProperties");
  const int width=320;
  const auto head=render_panel(header,width);
  body->setMinimumWidth(width);body->setMaximumWidth(width);body->adjustSize();
  const auto content=render_panel(body,width);
  QImage image(width,head.height()+content.height(),QImage::Format_RGB32);
  image.fill(QColor(bts::theme::Surface));
  QPainter painter(&image);
  painter.drawImage(0,0,head);
  painter.drawImage(0,head.height(),content);
  if(!image.save(path))qFatal("Could not write %s",qPrintable(path));
}
void require_fill_chrome(WorkspaceShell &shell){
  auto fail=[](const char *message){qFatal("AC fill chrome: %s",message);};
  auto *title=shell.findChild<QLabel *>("propertiesDockTitle");
  if(!title||title->text()!="PROPERTIES - FILL")fail("dock title");
  auto *lock=shell.findChild<QCheckBox *>("fillScaleLock");
  auto *scale_v=shell.findChild<QDoubleSpinBox *>("fillScaleV");
  auto *slider=shell.findChild<QSlider *>("fillScaleSlider1");
  if(!lock||!lock->isChecked()||!scale_v||scale_v->isEnabled()||!slider||slider->isEnabled())fail("linked scale");
  if(scale_v->text().contains(',')||shell.findChild<QDoubleSpinBox *>("roughnessControl")->text().contains(','))fail("decimal comma");
  if(!shell.findChild<QWidget *>("fillChannelRow6")->isHidden())fail("scattering block visible");
  if(shell.findChild<QWidget *>("fillChannelRow14")->isHidden())fail("displacement block hidden");
  if(shell.findChild<QWidget *>("fillChannelRow0")->isHidden())fail("color block hidden");
  if(shell.findChild<QWidget *>("fillChannelRow4")->isHidden())fail("channel 4 bump block hidden");
  auto *scatt=shell.findChild<QCheckBox *>("fillChannelEnabled6");
  if(!scatt||scatt->isHidden()||scatt->isChecked()||scatt->text()!="scatt")fail("scatt toggle");
  auto *normal_header=shell.findChild<QToolButton *>("fillChannelHeader3");
  auto *normal_toggle=shell.findChild<QCheckBox *>("fillChannelEnabled3");
  auto *normal_row=shell.findChild<QWidget *>("fillChannelRow3");
  if(!normal_header||!normal_header->isHidden()||!normal_toggle||!normal_toggle->isHidden()||normal_toggle->text()!="nrm"||!normal_row||!normal_row->isHidden())fail("normal channel visible");
  auto *bump4=shell.findChild<QToolButton *>("fillChannelHeader4");
  auto *bump4_toggle=shell.findChild<QCheckBox *>("fillChannelEnabled4");
  if(!bump4||bump4->isHidden()||!bump4->text().startsWith("Bump")||!bump4_toggle||bump4_toggle->isHidden()||bump4_toggle->text()!="bump")fail("channel 4 named Bump");
  shell.findChild<QWidget *>("materialProperties")->adjustSize();
  QApplication::processEvents();
  const auto toggle_at=[&](const char *name){auto *box=shell.findChild<QCheckBox *>(name);if(!box||box->isHidden())fail(name);return box->mapTo(shell.findChild<QWidget *>("fillChannelToggles"),QPoint(0,0));};
  const auto color=toggle_at("fillChannelEnabled0"),metal=toggle_at("fillChannelEnabled1"),rough=toggle_at("fillChannelEnabled2"),bump_button=toggle_at("fillChannelEnabled4"),disp=toggle_at("fillChannelEnabled14"),scatt_at=toggle_at("fillChannelEnabled6");
  if(!(color.y()==metal.y()&&metal.y()==rough.y()&&color.x()<metal.x()&&metal.x()<rough.x()))fail("channel row color metal rough");
  if(!(bump_button.y()==disp.y()&&disp.y()==scatt_at.y()&&bump_button.y()>color.y()&&bump_button.x()<disp.x()&&disp.x()<scatt_at.x()))fail("channel row bump disp scatt");
  auto *bar=shell.findChild<QWidget *>("fillTangentBump");
  auto *xyz=shell.findChild<QWidget *>("tangentBumpValues");
  if(!bar||bar->isVisible()||!xyz||xyz->isVisible())fail("normal swatch visible");
  if(!shell.findChild<QSlider *>("metallicSlider")->isHidden()||!shell.findChild<QSlider *>("roughnessSlider")->isHidden()||!shell.findChild<QSlider *>("bumpSlider")->isHidden()||!shell.findChild<QSlider *>("displacementSlider")->isHidden())fail("scalar sliders still visible");
  if(!shell.findChild<QDoubleSpinBox *>("metallicControl")->isHidden()||!shell.findChild<QDoubleSpinBox *>("materialBump")->isHidden()||!shell.findChild<QDoubleSpinBox *>("materialDisplacement")->isHidden())fail("scalar values still visible");
  const auto gradient_ok=[&](const char *name,const char *text){auto *widget=shell.findChild<QWidget *>(name);if(!widget||widget->isHidden())fail(name);QImage image(std::max(widget->width(),8),std::max(widget->height(),8),QImage::Format_RGB32);image.fill(QColor(bts::theme::Surface));widget->render(&image);if(widget->accessibleName()!=text)fail(name);const int y=std::clamp(image.height()*3/4,0,image.height()-1);const auto left=image.pixelColor(std::clamp(image.width()/8,1,image.width()-1),y);const auto right=image.pixelColor(std::clamp(image.width()*5/8,1,image.width()-1),y);if(right.red()+right.green()+right.blue()-left.red()-left.green()-left.blue()<80)fail(name);};
  gradient_ok("fillGradientMetallic","0.000");
  gradient_ok("fillGradientRoughness","0.800");
  gradient_ok("fillGradientBump","0.500");
  gradient_ok("fillGradientDisplacement","0.0000");
  auto *mode=shell.findChild<QToolButton *>("fillMaterialMode");
  if(!mode||!mode->text().contains("No Resource Selected"))fail("material mode");
  auto *wrap=shell.findChild<QComboBox *>("fillWrap");
  if(!wrap||wrap->isHidden()||wrap->currentText()!="Repeat")fail("uv wrap");
  if(!shell.findChild<QLabel *>("sectionTitle")->isHidden())fail("inner fill title");
  if(!shell.findChild<QComboBox *>("fillProjectionMode")||!shell.findChild<QComboBox *>("fillBaseColorResource"))fail("projection controls");
  if(!shell.findChild<QToolButton *>("propertiesUndock")||!shell.findChild<QToolButton *>("propertiesClose")||!shell.findChild<QAction *>("toggleProperties"))fail("dock controls");
  const auto place=[&](const char *name){auto *widget=shell.findChild<QWidget *>(name);if(!widget||widget->isHidden())fail(name);return widget->mapTo(shell.findChild<QWidget *>("materialProperties"),QPoint(0,0)).y();};
  const int offset=place("fillOffset0"),scale_mode=place("fillScaleMode"),physical=place("fillPhysicalSizeX"),projection=place("fill3dDisclosure"),warp=place("fillWarpDisclosure"),symmetry=place("fillSymmetryPanel"),frame=place("fillShowManipulator");
  if(!(offset<scale_mode&&scale_mode<physical&&physical<projection&&projection<warp&&warp<symmetry&&symmetry<frame))fail("controls after offset");
  auto *sphere=shell.findChild<QWidget *>("fillMaterialSphere");
  if(!sphere||!sphere->isHidden())fail("sphere hidden on settings tab");
}
}
#include "ShelfNavigationTest.inc"
void WorkspaceShell::set_mask_clipboard_available(bool ready){
 mask_clipboard_available_=ready;
 auto *copy=findChild<QAction *>("copyMaskContent"),*paste=findChild<QAction *>("pasteIntoMask");
 if(paste)paste->setEnabled(ready&&copy&&copy->isEnabled());
}
int capture_ac_panels(const QString &directory){
  if(directory.isEmpty())return 2;
  QDir().mkpath(directory);
  auto *viewport=new QWidget;viewport->resize(640,480);
  int shelf_applications=0;WorkspaceShell::Callbacks capture_callbacks;capture_callbacks.asset_selected=[&](const StudioAsset &){++shelf_applications;};
  if(qEnvironmentVariableIsSet("BTS_SHELF_NAV_TEST"))capture_callbacks.baked_maps=[]{return std::vector<BakedShelfItem>{{"baked:6a0b35a4-a916-42c3-a496-a3c7efdb1e13:3","A Fixture baked AO"}};};
  WorkspaceShell shell(viewport,std::move(capture_callbacks));
  shell.resize(1280,860);
  shell.show();
  shell.set_console_visible(false);
  shell.set_shelf_visible(true);
  shell.reset_reference_layout();
  QApplication::processEvents();
  NewProjectOptions options;
  BrushSettings brush;
  TextureSet set;
  set.uniform_editable=true;
  set.name="Default";
  set.channels={true,true,true,true,true};
  set.displacement.channel=true;set.displacement.allocated=true;
  set.users[0].present=true;
  set.users[0].scattering=true;
  set.users[0].name="Scattering";
  TextureLayer fill;
  fill.kind=LayerKind::Fill;
  fill.name="Fill layer 1";
  fill.channels={true,true,true,true,true};
  fill.values.base_color={.75f,.75f,.75f,1};
  fill.values.metallic=0;
  fill.values.roughness=.8f;
  fill.values.normal={0,0,1};
  fill.displacement.enabled=true;
  fill.displacement.value=0;
  fill.users[0].enabled=false;
  set.layers={fill};
  set.selected_layer=fill.id;
  const auto show=[&]{shell.show_texture_set(&set,&options,&brush);QApplication::processEvents();};
  show();
  require_fill_chrome(shell);
  if(qEnvironmentVariableIsSet("BTS_SHELF_NAV_TEST"))return verify_shelf_navigation(shell,set,directory,shelf_applications);
  save_properties(shell,directory+"/fill-properties.png");
  if(qEnvironmentVariableIsSet("BTS_MASK_FILL_UI_TEST")){verify_shared_mask_fill_ui(shell,*static_cast<MaskFillPanel *>(shell.findChild<QWidget *>("maskFillProperties")),set,directory,show,[&](QString path){save_properties(shell,path);});return 0;}
  shell.findChild<QToolButton *>("fillMaterialTab")->click();
  QApplication::processEvents();
  auto *sphere=shell.findChild<QWidget *>("fillMaterialSphere");
  if(!sphere||sphere->isHidden()||sphere->width()<64)qFatal("AC material sphere missing");
  QImage sphere_image(sphere->size(),QImage::Format_RGB32);
  sphere->render(&sphere_image);
  int inside=0,lo=999,hi=0;for(int y=0;y<sphere_image.height();++y)for(int x=0;x<sphere_image.width();++x){const auto px=sphere_image.pixelColor(x,y);if(px.red()>45&&px.red()<60&&px.green()>45&&px.green()<60&&px.blue()>45&&px.blue()<60)continue;const int lum=px.red()+px.green()+px.blue();++inside;lo=std::min(lo,lum);hi=std::max(hi,lum);}
  const auto corner=sphere_image.pixelColor(2,2),south=sphere_image.pixelColor(sphere_image.width()/2,sphere_image.height()*3/4);
  if(inside<8000||hi-lo<80||corner.red()>70||south.red()<70)qFatal("AC material sphere is not shaded");
  if(!sphere_image.save(directory+"/fill-material-sphere.png"))qFatal("sphere grab failed");
  shell.findChild<QToolButton *>("fillSettingsTab")->click();
  QApplication::processEvents();
  shell.findChild<QToolButton *>("propertiesUndock")->click();
  QApplication::processEvents();
  auto *floating=shell.findChild<QDialog *>("propertiesFloat");
  if(!floating||floating->isHidden()||floating->windowTitle()!="PROPERTIES - FILL")qFatal("AC properties did not undock");
  shell.findChild<QToolButton *>("propertiesUndock")->click();
  QApplication::processEvents();
  if(auto *still=shell.findChild<QDialog *>("propertiesFloat");still&&still->isVisible())qFatal("AC properties did not dock");
  if(shell.findChild<QWidget *>("propertiesSlot")->isHidden())qFatal("AC properties did not dock");
  shell.findChild<QToolButton *>("propertiesClose")->click();
  QApplication::processEvents();
  if(!shell.findChild<QWidget *>("propertiesSlot")->isHidden())qFatal("AC properties did not close");
  shell.findChild<QAction *>("toggleProperties")->trigger();
  QApplication::processEvents();
  if(shell.findChild<QWidget *>("propertiesSlot")->isHidden())qFatal("AC properties did not return");
  show();
  fill.kind=LayerKind::Paint;
  set.layers={fill};set.selected_layer=fill.id;set.selected_mask=false;set.selected_effect=QUuid();
  show();
  if(auto *normal=shell.findChild<QToolButton *>("brushChannel3");!normal||!normal->isHidden())qFatal("AC paint still shows nrm");
  if(auto *bump=shell.findChild<QToolButton *>("brushChannel4");!bump||bump->isHidden()||bump->text()!="bump")qFatal("AC paint bump missing");
  if(auto *context=shell.findChild<QComboBox *>("layerChannelContext");!context||context->findData(3)>=0||context->findText("Normal")>=0)qFatal("AC layer list still shows Normal");
  save_properties(shell,directory+"/paint-properties.png");
  fill.kind=LayerKind::Fill;
  LayerMask mask;
  MaskEffect mask_fill;mask_fill.kind=MaskEffectKind::Fill;mask_fill.fill=.5f;
  mask.effects={mask_fill};
  fill.mask=mask;fill.effects.clear();
  set.layers={fill};set.selected_layer=fill.id;set.selected_mask=true;set.selected_effect=mask_fill.id;
  show();
  save_properties(shell,directory+"/mask-fill-properties.png");
  MaskEffect generator;generator.kind=MaskEffectKind::Generator;generator.generator=2;
  fill.mask->effects={generator};
  set.layers={fill};set.selected_effect=generator.id;
  show();
  save_properties(shell,directory+"/generator-properties.png");
  MaskEffect filter_mask;filter_mask.kind=MaskEffectKind::Filter;filter_mask.filter.resource=5;
  fill.mask->effects={filter_mask};
  set.layers={fill};set.selected_effect=filter_mask.id;
  show();
  save_properties(shell,directory+"/filter-properties.png");
  MaskEffect anchor;anchor.kind=MaskEffectKind::Anchor;anchor.anchor_name="Fill layer 1 mask";
  fill.mask->effects={anchor};
  set.layers={fill};set.selected_effect=anchor.id;
  show();
  save_properties(shell,directory+"/anchor-properties.png");
  fill.mask.reset();
  LevelsEffect levels;levels.kind=ContentEffectKind::Levels;levels.channel=0;
  fill.effects={levels};
  set.layers={fill};set.selected_mask=false;set.selected_effect=levels.id;
  show();
  save_properties(shell,directory+"/levels-properties.png");
  shell.layer_settings_tabs()->setCurrentIndex(0);
  QApplication::processEvents();
  if(!shell.findChild<QWidget *>("layerSettingsTabs")->grab().save(directory+"/layers.png"))qFatal("layers grab failed");
  shell.layer_settings_tabs()->setCurrentIndex(1);
  QApplication::processEvents();
  if(!shell.findChild<QWidget *>("layerSettingsTabs")->grab().save(directory+"/texture-set-settings.png"))qFatal("settings grab failed");
  if(!shell.findChild<QWidget *>("assetShelf")->grab().save(directory+"/shelf.png"))qFatal("shelf grab failed");
  if(!shell.findChild<QWidget *>("toolRail")->grab().save(directory+"/tool-rail.png"))qFatal("tool rail grab failed");
  if(!shell.findChild<QWidget *>("contextToolbar")->grab().save(directory+"/context-toolbar.png"))qFatal("toolbar grab failed");
  auto *display=shell.findChild<QDialog *>("displaySettings");
  display->show();
  QApplication::processEvents();
  auto *opacity=display->findChild<QDoubleSpinBox *>("environmentRotation");
  auto *exposure=display->findChild<QDoubleSpinBox *>("environmentExposure");
  opacity->setValue(40.5);
  exposure->setValue(1.5);
  QApplication::processEvents();
  if(opacity->text().contains(',')||exposure->text().contains(',')||!opacity->text().contains('.')||!exposure->text().contains('.'))qFatal("AC display decimals: %s %s",qPrintable(opacity->text()),qPrintable(exposure->text()));
  if(!display->grab().save(directory+"/display-settings.png"))qFatal("display grab failed");
  display->hide();
  shell.resize(1400,900);
  shell.reset_reference_layout();
  show();
  QApplication::processEvents();
  if(!shell.grab().save(directory+"/workspace.png"))qFatal("workspace grab failed");
  std::printf("BTS_AC_CAPTURE directory=%s\n",qPrintable(directory));
  std::fflush(stdout);
  return 0;
}
}
