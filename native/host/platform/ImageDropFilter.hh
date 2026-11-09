// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <functional>
namespace bts {
class ImageDropFilter final : public QObject {
  std::function<void(const QStringList &)> files_;
  std::function<void(const QString &)> asset_;
  std::function<void(QWidget *, QPoint)> before_drop_;
  bool supported(const QMimeData *mime) const
  {
    if (mime->hasFormat("application/x-bts-asset"))
      return bool(asset_);
    if (!mime->hasUrls())
      return false;
    for (const auto &url : mime->urls())
      if (url.isLocalFile() &&
          QStringList{"png", "exr", "tif", "tiff", "jpg", "jpeg", "hdr", "tga", "bmp"}.contains(
              QFileInfo(url.toLocalFile()).suffix().toLower()))
        return true;
    return false;
  }

 protected:
  bool eventFilter(QObject *source, QEvent *event) override
  {
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
      auto *drag = static_cast<QDragMoveEvent *>(event);
      if (supported(drag->mimeData())) {
        drag->setDropAction(Qt::CopyAction);
        drag->accept();
        return true;
      }
    }
    if (event->type() == QEvent::Drop) {
      auto *drop = static_cast<QDropEvent *>(event);
      if (!supported(drop->mimeData()))
        return false;
      if (before_drop_)
        before_drop_(qobject_cast<QWidget *>(source), drop->position().toPoint());
      if (drop->mimeData()->hasFormat("application/x-bts-asset")) {
        asset_(QString::fromUtf8(drop->mimeData()->data("application/x-bts-asset")));
      }
      else {
        QStringList paths;
        for (const auto &url : drop->mimeData()->urls())
          if (url.isLocalFile())
            paths.append(url.toLocalFile());
        if (!paths.empty())
          files_(paths);
      }
      drop->setDropAction(Qt::CopyAction);
      drop->accept();
      return true;
    }
    return false;
  }

 public:
  ImageDropFilter(QWidget *target,
                  std::function<void(const QStringList &)> files,
                  std::function<void(const QString &)> asset = {},
                  std::function<void(QWidget *, QPoint)> before_drop = {})
      : QObject(target),
        files_(std::move(files)),
        asset_(std::move(asset)),
        before_drop_(std::move(before_drop))
  {
    target->setAcceptDrops(true);
    target->installEventFilter(this);
    for (auto *child : target->findChildren<QWidget *>()) {
      child->setAcceptDrops(true);
      child->installEventFilter(this);
    }
  }
};
}  // namespace bts
