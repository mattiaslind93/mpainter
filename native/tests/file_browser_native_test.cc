// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileBrowser.hh"
#include "FileDialogs.hh"
#include <OpenImageIO/imageio.h>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QToolButton>
#include <QTreeView>
#include <QListView>
#include <QHelpEvent>
#include <QLabel>
#include <QFileSystemModel>
#include <cassert>
#include <iostream>
#include "CLG_log.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace {
QByteArray hash(const QString &path)
{
  QFile file(path);
  assert(file.open(QIODevice::ReadOnly));
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}
}  // namespace
int main(int argc, char **argv)
{
  // The caller supplies private XDG paths before process startup. Never mutate
  // environment from a worker or borrow the artist's native thumbnail cache.
  assert(!qEnvironmentVariable("XDG_CACHE_HOME").isEmpty());
  assert(!qEnvironmentVariable("XDG_CONFIG_HOME").isEmpty());
  QApplication app(argc, argv);
  assert(argc == 3);
  const auto root = QString::fromLocal8Bit(argv[1]);
  QDir().mkpath(root);
  QDir().mkpath(root + "/decoded");
  CLG_init();
  blender::IMB_init();
  const auto png = root + "/pattern å.png", exr = root + "/pattern.exr",
             blend = root + "/embedded.blend";
  QImage pattern(64, 32, QImage::Format_RGBA8888);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 64; ++x)
      pattern.setPixelColor(x, y, y < 16 ? QColor(255, 20 + x, 10) : QColor(10, 20 + x, 255));
  assert(pattern.save(png));
  auto writer = OIIO::ImageOutput::create(exr.toStdString());
  assert(writer);
  OIIO::ImageSpec spec(64, 32, 4, OIIO::TypeDesc::FLOAT);
  assert(writer->open(exr.toStdString(), spec));
  std::vector<float> pixels(64 * 32 * 4);
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 64; ++x) {
      auto *pixel = &pixels[(y * 64 + x) * 4];
      pixel[0] = y < 16 ? 1.f : .01f;
      pixel[1] = .08f;
      pixel[2] = y < 16 ? .01f : 1.f;
      pixel[3] = 1.f;
    }
  assert(writer->write_image(OIIO::TypeDesc::FLOAT, pixels.data()));
  assert(writer->close());
  assert(QFile::copy(QString::fromLocal8Bit(argv[2]), blend));
  const auto loader = bts::browser_thumbnail_loader();
  assert(loader);
  for (const auto &path : {png, exr, blend}) {
    const auto before = hash(path);
    blender::ThumbCancellationToken token;
    const auto image = loader(path, path == blend, token);
    assert(!image.isNull());
    assert(image.width() <= 256 && image.height() <= 256);
    assert(image.width() > 8 && image.height() > 8);
    if (path != blend) {
      const auto top = image.pixelColor(image.width() / 2, image.height() / 4),
                 bottom = image.pixelColor(image.width() / 2, 3 * image.height() / 4);
      assert(top.red() > top.blue());
      assert(bottom.blue() > bottom.red());
    }
    const auto encoded = path.toUtf8();
    auto *cached = blender::IMB_thumb_read(encoded.constData(), blender::THB_LARGE);
    assert(cached && cached->byte_buffer.data);
    assert(cached->x == image.width() && cached->y == image.height());
    blender::IMB_freeImBuf(cached);
    assert(hash(path) == before);
    assert(image.save(root + "/decoded/" + QFileInfo(path).fileName() + ".thumbnail.png"));
    token.cancel();
    assert(loader(path, path == blend, token).isNull());
    std::cout << "Native thumbnail/cache/cancel passed: " << path.toStdString() << '\n';
  }
  assert(QDir(qEnvironmentVariable("XDG_CACHE_HOME") + "/thumbnails/large")
             .entryList({"*.png"}, QDir::Files)
             .size() == 3);
  bts::FileDialog::prepare_directory_thread();
  auto *dialog = new bts::FileDialog(nullptr, "Native PNG, EXR and blend previews", root);
  dialog->findChild<QToolButton *>("fileBrowserMode2")->click();
  dialog->show();
  QElapsedTimer wait;
  wait.start();
  while (dialog->property("fileBrowserPreviewsCompleted").toInt() < 3 && wait.elapsed() < 5000) {
    app.processEvents();
    QThread::msleep(2);
  }
  assert(dialog->property("fileBrowserPreviewsCompleted").toInt() == 3);
  assert(dialog->grab().save(root + "/native-browser.png"));
  for (int mode : {0, 1}) {
    dialog->findChild<QToolButton *>(QString("fileBrowserMode%1").arg(mode))->click();
    app.processEvents();
    auto *view = mode == 0 ?
                     static_cast<QAbstractItemView *>(dialog->findChild<QTreeView *>("treeView")) :
                     static_cast<QAbstractItemView *>(dialog->findChild<QListView *>("listView"));
    QModelIndex image_index;
    for (int row = 0; row < view->model()->rowCount(view->rootIndex()); ++row) {
      const auto index = view->model()->index(row, 0, view->rootIndex());
      if (index.data(QFileSystemModel::FilePathRole).toString() == png) {
        image_index = index;
        break;
      }
    }
    assert(image_index.isValid());
    const auto before_icon = image_index.data(Qt::DecorationRole).value<QIcon>().cacheKey();
    const auto point = view->visualRect(image_index).center();
    QHelpEvent hover(QEvent::ToolTip, point, view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(view->viewport(), &hover);
    wait.restart();
    auto *tooltip = dialog->findChild<QWidget *>("fileBrowserPreviewTooltip");
    assert(tooltip);
    bool image_visible = false;
    while (!image_visible && wait.elapsed() < 5000) {
      app.processEvents();
      for (auto *label : tooltip->findChildren<QLabel *>())
        image_visible |= !label->pixmap().isNull() && label->isVisible();
      QThread::msleep(2);
    }
    assert(image_visible);
    assert(image_index.data(Qt::DecorationRole).value<QIcon>().cacheKey() == before_icon);
    assert(dialog->grab().save(root + QString("/hover-mode-%1.png").arg(mode)));
    assert(tooltip->grab().save(root + QString("/tooltip-mode-%1.png").arg(mode)));
  }
  QElapsedTimer close;
  close.start();
  dialog->hide();
  delete dialog;
  const auto close_ms = close.nsecsElapsed() / 1e6;
  assert(close_ms < 50);
  bts::shutdown_browser_previews();
  std::cout << "Native async visible previews=3 and dialog close " << close_ms << " ms passed\n";
  blender::IMB_exit();
  CLG_exit();
  return 0;
}
