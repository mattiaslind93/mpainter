#include "Theme.hh"
#pragma once
#include "SceneSnapshot.hh"
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QVector3D>
#include <QWidget>
#include <QMimeData>
#include <QStringList>
#include <functional>
#include <optional>
#include <limits>
#include <cmath>

namespace bts {
struct AssetMeshHit {
  std::size_t material = 0;
  std::uint32_t object = 0;
  std::string name;
  bool operator==(const AssetMeshHit &) const = default;
};
// Query every set, including unselected ones. Nearest positive intersection wins;
// an unsupported foreground mesh must never let a drop leak through to one behind.
inline std::optional<AssetMeshHit> pick_asset_mesh(const SceneSnapshot &scene,
                                                   QVector3D origin,
                                                   QVector3D direction)
{
  float nearest = std::numeric_limits<float>::max();
  std::optional<AssetMeshHit> result;
  for (const auto &triangle : scene.uv_triangles) {
    const auto point = [](const auto &p) { return QVector3D(p[0], p[1], p[2]); };
    const auto a = point(triangle.positions[0]), e1 = point(triangle.positions[1]) - a,
               e2 = point(triangle.positions[2]) - a;
    const auto h = QVector3D::crossProduct(direction, e2);
    const float det = QVector3D::dotProduct(e1, h);
    if (std::abs(det) < 1e-9f)
      continue;
    const auto s = origin - a;
    const float u = QVector3D::dotProduct(s, h) / det;
    if (u < 0 || u > 1)
      continue;
    const auto q = QVector3D::crossProduct(s, e1);
    const float v = QVector3D::dotProduct(direction, q) / det;
    if (v < 0 || u + v > 1)
      continue;
    const float t = QVector3D::dotProduct(e2, q) / det;
    if (t < 0 || t >= nearest)
      continue;
    nearest = t;
    const auto object = std::uint32_t(triangle.triangle_id >> 32);
    const auto name = scene.object_names.find(object);
    result = AssetMeshHit{triangle.material,
                          object,
                          name == scene.object_names.end() ? std::string{} : name->second};
  }
  return result;
}
class AssetDropHighlight final : public QWidget {
 public:
  explicit AssetDropHighlight(QWidget *parent) : QWidget(parent)
  {
    setObjectName("assetDropHighlight");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setStyleSheet("background:transparent;");
    hide();
  }
  QPainterPath silhouette;
  QString label;
  QRectF clip;

 protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.save();
    if (!clip.isEmpty())
      painter.setClipRect(clip);
    // The path is one closed polygon per mesh triangle. Stroking it outlines
    // every single triangle, which is why the mesh appeared as a wireframe
    // instead of a filled shape, and stroking thousands of edges is what made
    // the drag stall. Fill the union and draw no outline at all.
    painter.setPen(Qt::NoPen);
    painter.setBrush(bts::theme::alpha(bts::theme::Accent, 65));
    painter.drawPath(silhouette);
    painter.restore();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(bts::theme::Heading));
    painter.fillRect(QRect(12, 12, std::min(width() - 24, 500), 30),
                     bts::theme::alpha(bts::theme::Accent, 230));
    painter.drawText(QRect(20, 12, width() - 40, 30), Qt::AlignVCenter, "Apply to " + label);
  }
};
class AssetViewportDrop final : public QObject {
 public:
  explicit AssetViewportDrop(QWidget *view)
      : QObject(view), view_(view), highlight_(new AssetDropHighlight(view))
  {
    view_->setAcceptDrops(true);
    view_->installEventFilter(this);
  }
  std::function<bool(const QString &)> accepts;
  std::function<std::optional<AssetMeshHit>(QPointF)> pick;
  std::function<QPainterPath(const AssetMeshHit &)> silhouette;
  std::function<QRectF()> clip;
  std::function<void(const QString &, const AssetMeshHit &)> apply;
  std::optional<AssetMeshHit> hovered() const
  {
    return hovered_;
  }

 protected:
  bool eventFilter(QObject *, QEvent *event) override
  {
    if (event->type() == QEvent::DragLeave) {
      clear();
      event->accept();
      return true;
    }
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove &&
        event->type() != QEvent::Drop)
      return false;
    auto *drop = static_cast<QDropEvent *>(event);
    const auto *mime = drop->mimeData();
    const QString id = QString::fromUtf8(mime->data("application/x-bts-asset"));
    if (!mime->hasFormat("application/x-bts-asset") || !accepts || !accepts(id)) {
      clear();
      drop->ignore();
      return false;
    }
    if (event->type() == QEvent::DragEnter) {
      // The pointer always crosses into this view at an edge, where there is no
      // mesh under it yet. Refusing the entry takes the view out of the drag for
      // the rest of the session, so it could never be offered the mesh the user
      // was heading for. Accept the entry and judge each position on its own.
      hovered_.reset();
      highlight_->hide();
      drop->setDropAction(Qt::CopyAction);
      drop->accept();
      return true;
    }
    const auto previous = hovered_;
    hovered_ = pick ? pick(drop->position()) : std::nullopt;
    if (!hovered_) {
      clear();
      drop->ignore();
      return true;
    }
    drop->setDropAction(Qt::CopyAction);
    drop->accept();
    if (event->type() == QEvent::Drop) {
      const auto hit = *hovered_;
      clear();
      // Applying the asset is slow and spins its own event loops. Doing that
      // inside the drop handler keeps the platform's drag session alive across
      // all of it, and on macOS the session's autorelease pool is then popped
      // over objects the work has already released. Let the drop return first.
      if (apply)
        QTimer::singleShot(0, this, [this, id, hit] {
          if (apply)
            apply(id, hit);
        });
      return true;
    }
    if (previous == hovered_ && highlight_->isVisible())
      return true;
    highlight_->setGeometry(view_->rect());
    highlight_->silhouette = silhouette ? silhouette(*hovered_) : QPainterPath{};
    highlight_->clip = clip ? clip() : QRectF{};
    highlight_->label = QString::fromStdString(hovered_->name);
    highlight_->show();
    highlight_->raise();
    highlight_->update();
    return true;
  }

 private:
  QWidget *view_;
  AssetDropHighlight *highlight_;
  std::optional<AssetMeshHit> hovered_;
  void clear()
  {
    hovered_.reset();
    highlight_->hide();
  }
};
}  // namespace bts
