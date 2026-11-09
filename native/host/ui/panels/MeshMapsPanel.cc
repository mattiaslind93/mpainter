// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshMapsPanel.hh"
#include "Theme.hh"
#include <QHeaderView>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace bts {
// One source for the thumbnail: the icon size the view reserves and the image
// that is built for it must agree, or the view rescales and blurs the result.
namespace {
constexpr int thumbnail_side = 36;
}
MeshMapsPanel::MeshMapsPanel(QWidget *parent) : QWidget(parent)
{
  setObjectName("meshMapsPanel");
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 4, 0, 0);
  layout->addWidget(new QLabel("MESH MAPS", this));
  bake_ = new QPushButton("Bake Mesh Maps…", this);
  bake_->setObjectName("bakeMeshMaps");
  layout->addWidget(bake_);
  connect(bake_, &QPushButton::clicked, this, [this] {
    if (bake)
      bake();
  });
  maps_ = new QTreeWidget(this);
  maps_->setObjectName("meshMapResources");
  maps_->setColumnCount(3);
  maps_->setHeaderHidden(true);
  maps_->setRootIsDecorated(false);
  maps_->setIconSize({thumbnail_side, thumbnail_side});
  maps_->setMinimumHeight(160);
  maps_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  maps_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
  maps_->header()->setSectionResizeMode(2, QHeaderView::Fixed);
  maps_->setColumnWidth(2, bts::theme::RemoveColumnPx);
  layout->addWidget(maps_);
  connect(maps_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
    if (item->data(0, Qt::UserRole + 1).toBool() && inspect)
      inspect(item->data(0, Qt::UserRole).toInt());
  });
}
void MeshMapsPanel::synchronize(const TextureSet *set)
{
  bake_->setEnabled(set && set->uniform_editable);
  QString key = set ? set->id.toString() : QString("none");
  if (set)
    for (const auto &map : set->mesh_maps) {
      if (map)
        for (const auto &[tile, plane] : map->tiles)
          key += QString::number(tile) + QString::fromStdString(plane.hash);
    }
  if (key == signature_)
    return;
  signature_ = key;
  maps_->clear();
  for (int role = 0; role < 7; ++role) {
    const auto *map = set && set->mesh_maps[role] ? &*set->mesh_maps[role] : nullptr;
    auto *item = new QTreeWidgetItem(maps_,
                                     {QString::fromLatin1(mesh_map_names[role]),
                                      map ? QString::fromStdString(map->name) : "Not assigned"});
    item->setData(0, Qt::UserRole, role);
    item->setData(0, Qt::UserRole + 1, bool(map));
    item->setToolTip(1,
                     map ? QString("%1 — %2/%3 UV tiles")
                               .arg(item->text(1))
                               .arg(map->tiles.size())
                               .arg(set->tiles.size()) :
                           item->text(1));
    item->setSizeHint(0, {0, 42});
    if (map && !map->tiles.empty() && map->tiles.begin()->second.pixels) {
      const auto &data = *map->tiles.begin()->second.pixels;
      // Sample into the map's own proportions and letterbox the rest, so a map that
      // is not square is no longer stretched to fill the icon. The image is built at
      // the screen's pixel ratio so the thumbnail stays sharp on a Retina display.
      const qreal ratio = std::max(qreal(1), devicePixelRatioF());
      const int side = int(std::lround(thumbnail_side * ratio));
      const int source_w = std::max(1, int(data.resolution)),
                source_h = std::max(1, int(data.height));
      int w = side, h = side;
      if (source_w > source_h)
        h = std::max(1, int(std::lround(double(side) * source_h / source_w)));
      else if (source_h > source_w)
        w = std::max(1, int(std::lround(double(side) * source_w / source_h)));
      const int ox = (side - w) / 2, oy = (side - h) / 2;
      QImage image(side, side, QImage::Format_RGB32);
      image.fill(QColor(bts::theme::Surface));
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const auto sx = std::size_t(std::int64_t(x) * source_w / w),
                     sy = std::size_t(std::int64_t(h - 1 - y) * source_h / h);
          const auto i = (sy * std::size_t(source_w) + sx) * 4;
          image.setPixel(ox + x,
                         oy + y,
                         qRgb(qRound(255 * std::clamp(data.rgba[i], 0.f, 1.f)),
                              qRound(255 * std::clamp(data.rgba[i + 1], 0.f, 1.f)),
                              qRound(255 * std::clamp(data.rgba[i + 2], 0.f, 1.f))));
        }
      image.setDevicePixelRatio(ratio);
      item->setIcon(0, QPixmap::fromImage(image));
    }
    auto *button = new QToolButton(maps_);
    button->setObjectName("meshMapRemove");
    button->setText("×");
    button->setEnabled(map);
    button->setToolTip("Remove mesh map");
    // Without a fixed size the themed padding made this button as wide as the name
    // beside it. Keep it a small square that matches the row.
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(bts::theme::RemoveButtonPx, bts::theme::RemoveButtonPx);
    maps_->setItemWidget(item, 2, button);
    connect(button, &QToolButton::clicked, this, [this, role] {
      if (remove)
        remove(role);
    });
  }
}
}  // namespace bts
