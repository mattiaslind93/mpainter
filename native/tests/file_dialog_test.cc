// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileDialogs.hh"
#include "FileBrowser.hh"
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QCryptographicHash>
#include <QImage>
#include <QListView>
#include <QSlider>
#include <QToolButton>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QListWidget>
#include <QLineEdit>
#include <QHeaderView>
#include <QTreeView>
#include <QMimeData>
#include <QPushButton>
#include <QPointer>
#include <QSettings>
#include <QLockFile>
#include <QEventLoop>
#include <QThread>
#include <QThreadPool>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>
#include <QtCore/private/qobject_p.h>
#include <cassert>
#include <iostream>
namespace {
void verify_filters_detached(QApplication &app, bts::FileDialog &dialog)
{
  const QPointer<QObject> controller = dialog.findChild<QObject *>("fileBrowserController");
  assert(controller);
  for (const char *name : {"listView", "treeView"}) {
    auto *view = dialog.findChild<QAbstractItemView *>(name);
    assert(view);
    for (QObject *object :
         {static_cast<QObject *>(view), static_cast<QObject *>(view->viewport())})
    {
      QObject::connect(object, &QObject::destroyed, &app, [controller, object] {
        const auto *data = QObjectPrivate::get(object);
        assert(!controller || !data->extraData ||
               !data->extraData->eventFilters.contains(controller));
      });
    }
  }
}
void browser_lifetime(QApplication &app, const QString &root)
{
  const auto path = root + "/lifetime.abc";
  {
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    file.write("file-pick fixture");
  }
  for (int mode = 0; mode < 3; ++mode) {
    for (int iteration = 0; iteration < 20; ++iteration) {
      bts::FileDialog::prepare_directory_thread();
      QPointer<bts::FileDialog> dialog = new bts::FileDialog(
          nullptr, "Lifetime", root, "Alembic (*.abc)");
      verify_filters_detached(app, *dialog);
      dialog->setFileMode(QFileDialog::ExistingFile);
      dialog->findChild<QToolButton *>(QString("fileBrowserMode%1").arg(mode))->click();
      dialog->setAttribute(Qt::WA_DeleteOnClose, iteration % 2 == 0);
      dialog->selectFile(path);
      dialog->show();
      app.processEvents();
      assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
      assert(dialog);
      assert(dialog->result() == QDialog::Accepted);
      assert(dialog->selectedFiles() == QStringList{path});
      if (iteration % 2 == 0)
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
      else
        delete dialog.data();
      assert(!dialog);
    }
  }
  // Parent teardown and retained-dialog reuse exercise the other close paths.
  {
    auto *owner = new QWidget;
    QPointer<bts::FileDialog> dialog = bts::FileDialog::borrow(owner, "Retained", root);
    verify_filters_detached(app, *dialog);
    dialog->show();
    app.processEvents();
    dialog->hide();
    assert(bts::FileDialog::borrow(owner, "Reused", root) == dialog);
    dialog->show();
    app.processEvents();
    owner->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    assert(!dialog);
  }
  // The shared controller also supports plain QFileDialog callers. Destroy a
  // viewport/view first, or the controller before the delegate, then the dialog.
  for (int teardown = 0; teardown < 4; ++teardown) {
    auto *dialog = new QFileDialog(nullptr, "Shared controller", root);
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    bts::setup_browser_controls(dialog, 2, 96);
    bts::resume_browser_previews(dialog);
    auto *tree = dialog->findChild<QTreeView *>("treeView");
    assert(tree);
    if (teardown == 0)
      tree->setViewport(new QWidget);
    if (teardown == 1) {
      auto *controller = dialog->findChild<QObject *>("fileBrowserController");
      assert(controller);
      QEvent deletion(QEvent::DeferredDelete);
      assert(!controller->eventFilter(tree, &deletion));
      // Dispatch only to the filter. QFileDialog itself requires its tree for
      // saveSettings(), so independently deleting that tree is invalid Qt usage.
      QEvent tooltip(QEvent::ToolTip);
      assert(!controller->eventFilter(tree->viewport(), &tooltip));
    }
    if (teardown == 2)
      delete dialog->findChild<QObject *>("fileBrowserController");
    delete dialog;
  }
  bts::shutdown_browser_previews();
  QThreadPool::globalInstance()->waitForDone();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  std::cout
      << "Browser lifetime passed: 60 accepts across three modes, direct/deferred deletion, retained parent teardown, shared controller and child teardown\n";
}
void browser_modes(QApplication &app, const QString &root)
{
  const auto directory = root + "/browser-types";
  QDir().mkpath(directory);
  const QStringList names = {"scene.blend",
                             "scene.blend1",
                             "image.png",
                             "clip.mp4",
                             "script.py",
                             "sound.wav",
                             "font.ttf",
                             "mesh.obj",
                             "readme.txt",
                             "archive.zip",
                             "volume.vdb",
                             "material.spsm",
                             "unknown.zzz"};
  for (const auto &name : names) {
    QFile file(directory + "/" + name);
    assert(file.open(QIODevice::WriteOnly));
    file.write("fixture");
  }
  for (int n = 0; n < 40; ++n) {
    QFile file(directory + QString("/z_extra_%1.txt").arg(n, 2, 10, QChar('0')));
    assert(file.open(QIODevice::WriteOnly));
    file.write("columns");
  }
  QDir().mkpath(directory + "/folder");
  auto *dialog = new bts::FileDialog(nullptr, "Blender icons and three browser modes", directory);
  QSet<QByteArray> hashes;
  for (const auto &name : names) {
    const auto icon = dialog->iconProvider()->icon(QFileInfo(directory + "/" + name));
    assert(!icon.isNull());
    const auto image = icon.pixmap(32, 32).toImage().convertToFormat(QImage::Format_RGBA8888);
    assert(!image.isNull());
    bool nonempty = false;
    for (int y = 0; y < image.height(); ++y)
      for (int x = 0; x < image.width(); ++x)
        nonempty |= qAlpha(image.pixel(x, y)) != 0;
    assert(nonempty);
    const auto bytes = QByteArray(reinterpret_cast<const char *>(image.constBits()),
                                  image.sizeInBytes());
    assert(!hashes.contains(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)));
    hashes.insert(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
  }
  assert(!dialog->iconProvider()->icon(QFileInfo(directory + "/folder")).isNull());
  assert(bts::browser_file_type("material.sbsar") == bts::BrowserFileType::MaterialArchive);
  assert(bts::browser_file_type("UPPER.EXR") == bts::BrowserFileType::Image);
  assert(bts::browser_file_type("s.blend.gz") == bts::BrowserFileType::Blend);
  assert(bts::browser_file_type("mesh.usdz") == bts::BrowserFileType::Object);
  struct DecoderGate {
    std::mutex mutex;
    std::condition_variable ready;
    bool released = false;
    std::atomic<int> blocked = 0;
    std::atomic<bool> timed_out = false;
  };
  auto gate = std::make_shared<DecoderGate>();
  auto started = std::make_shared<std::atomic<int>>(0);
  auto finished = std::make_shared<std::atomic<int>>(0);
  bts::set_browser_thumbnail_loader([started, finished, gate](
                                        const QString &,
                                        bool,
                                        const blender::ThumbCancellationToken &cancel) {
    if (started->fetch_add(1) == 0) {
      ++*finished;
      throw std::runtime_error("intentional decoder failure");
    }
    {
      std::unique_lock lock(gate->mutex);
      ++gate->blocked;
      if (!gate->ready.wait_for(lock, std::chrono::seconds(10), [&] { return gate->released; }))
        gate->timed_out = true;
      --gate->blocked;
    }
    ++*finished;
    if (cancel.is_cancelled())
      return QImage{};
    QImage image(64, 32, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    return image;
  });
  dialog->show();
  app.processEvents();
  auto *list = dialog->findChild<QListView *>("listView");
  assert(list);
  auto *zoom = dialog->findChild<QSlider *>("fileBrowserZoom");
  assert(zoom);
  for (int mode = 0; mode < 3; ++mode) {
    auto *button = dialog->findChild<QToolButton *>(QString("fileBrowserMode%1").arg(mode));
    assert(button);
    button->click();
    app.processEvents();
    assert(dialog->property("fileBrowserPresentation").toInt() == mode);
    assert(dialog->viewMode() == (mode == 0 ? QFileDialog::Detail : QFileDialog::List));
    if (mode == 1) {
      assert(list->viewMode() == QListView::ListMode && list->flow() == QListView::TopToBottom &&
             list->isWrapping());
      assert(list->visualRect(list->model()->index(40, 0, list->rootIndex())).left() >
             list->visualRect(list->model()->index(0, 0, list->rootIndex())).left());
    }
    if (mode == 2)
      assert(list->viewMode() == QListView::IconMode);
    dialog->grab().save(root + QString("/browser-mode-%1.png").arg(mode));
  }
  zoom->setValue(144);
  assert(list->iconSize() == QSize(144, 144));
  QElapsedTimer wait;
  wait.start();
  while ((started->load() < 2 || gate->blocked.load() == 0) && wait.elapsed() < 3000) {
    app.processEvents();
    QThread::msleep(2);
  }
  assert(started->load() >= 2 && gate->blocked.load() > 0 && !gate->timed_out.load());
  // Simulate a delayed UI turn beyond the former 180ms decoder sleep.
  QThread::msleep(220);
  // Navigation invalidates the in-flight generation. The old decoder deliberately
  // ignores cancellation until explicitly released, so close must not wait for
  // its completion. A wall-clock sleep races slow CI scheduling/screenshots.
  dialog->setDirectory(root + "/folders/0");
  app.processEvents();
  assert(dialog->property("fileBrowserPreviewsCompleted").toInt() == 0);
  QElapsedTimer close;
  close.start();
  dialog->hide();
  delete dialog;
  const auto elapsed = close.nsecsElapsed() / 1e6;
  assert(elapsed < 50);
  assert(finished->load() < started->load());
  assert(gate->blocked.load() > 0 && !gate->timed_out.load());
  {
    const std::lock_guard lock(gate->mutex);
    gate->released = true;
  }
  gate->ready.notify_all();
  bts::FileDialog restored;
  assert(restored.property("fileBrowserPresentation").toInt() == 2);
  assert(restored.findChild<QSlider *>("fileBrowserZoom")->value() == 144);
  restored.findChild<QToolButton *>("fileBrowserMode0")->click();
  restored.hide();
  bts::shutdown_browser_previews();
  assert(!gate->timed_out.load());
  assert(finished->load() == started->load());
  bts::set_browser_thumbnail_loader({});
  std::cout
      << "Distinct Blender icons, three views, zoom persistence, stale cancellation and in-flight close "
      << elapsed << " ms passed\n";
}
void state_roundtrip(QApplication &app, const QString &root, bool read)
{
  const auto first = root + "/folders/0", second = root + "/folders/1";
  if (read) {
    bts::FileDialog dialog;
    assert(dialog.property("_btsFileDialogOwnSettingsSupported").toBool());
    assert(dialog.directory().absolutePath() == second);
    assert(dialog.viewMode() == QFileDialog::List);
    assert(dialog.history().contains(first));
    assert(dialog.findChild<QTreeView *>("treeView")->header()->sectionSize(0) == 321);
    std::cout << "Cross-process dialog state and shutdown drain passed\n";
    return;
  }
  bts::FileDialog older(nullptr, "Older retained chooser", first);
  older.setViewMode(QFileDialog::Detail);
  older.show();
  app.processEvents();
  older.hide();
  auto *pool = QThreadPool::globalInstance();
  pool->waitForDone();
  bts::FileDialog::prepare_directory_thread();
  const int maximum = pool->maxThreadCount();
  pool->setMaxThreadCount(1);
  QSettings state_settings("mPainter", "FileDialogState");
  QLockFile held(state_settings.fileName() + ".lock");
  assert(held.tryLock());
  {
    auto *latest = new bts::FileDialog(nullptr, "Latest chooser", second);
    latest->setViewMode(QFileDialog::List);
    latest->setHistory({first, second});
    latest->findChild<QTreeView *>("treeView")->header()->resizeSection(0, 321);
    delete latest;
    // The first/sole worker is blocked in QSettings by this held file lock.
    // New chooser restoration must use the cache and never this file's mutex.
    QThread::msleep(50);
    assert(pool->activeThreadCount() == 1);
    QElapsedTimer open;
    open.start();
    bts::FileDialog immediate;
    const double open_ms = open.nsecsElapsed() / 1e6;
    assert(open_ms < 50);
    assert(immediate.directory().absolutePath() == second);
    assert(immediate.viewMode() == QFileDialog::List);
    assert(immediate.history().contains(first));
    assert(immediate.findChild<QTreeView *>("treeView")->header()->sectionSize(0) == 321);
    QElapsedTimer recent;
    recent.start();
    bts::FileDialog::recent_folders();
    bts::FileDialog::pinned_folders();
    const double recent_ms = recent.nsecsElapsed() / 1e6;
    assert(recent_ms < 50);
    QEventLoop loop;
    QElapsedTimer ping;
    ping.start();
    double ping_ms = 0;
    QTimer::singleShot(10, &loop, [&] {
      ping_ms = ping.nsecsElapsed() / 1e6;
      assert(ping_ms < 50);
      loop.quit();
    });
    loop.exec();
    assert(pool->activeThreadCount() == 1);
    std::cout << "Held state write: cached chooser " << open_ms << " ms, Recent/Pinned "
              << recent_ms << " ms, 10ms ping " << ping_ms << " ms\n";
  }
  held.unlock();
  pool->setMaxThreadCount(maximum);
  // Deliberately no waitForDone: QApplication teardown must drain persistence.
  std::cout << "Immediate cached dialog state passed; returning with queued persistence\n";
}
void helper_semantics(QApplication &app, const QString &root)
{
  const auto directory = root + "/folders/12", one = directory + "/input.obj",
             two = directory + "/second.obj";
  {
    QFile file(two);
    assert(file.open(QIODevice::WriteOnly));
    file.write("fixture");
  }
  const QString filters = "Meshes (*.obj);;All files (*)";
  auto active = [] {
    auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
    assert(dialog && dialog->property("btsSharedFileDialog").toBool());
    return dialog;
  };
  QString selected = "All files (*)";
  QTimer::singleShot(0, &app, [&] {
    auto *dialog = active();
    assert(dialog->fileMode() == QFileDialog::ExistingFile);
    dialog->selectFile(one);
    dialog->selectNameFilter("Meshes (*.obj)");
    assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
  });
  assert(bts::FileDialog::getOpenFileName(nullptr, "Open one", directory, filters, &selected) ==
         one);
  assert(selected == "Meshes (*.obj)");
  QTimer::singleShot(0, &app, [&] {
    auto *dialog = active();
    assert(dialog->fileMode() == QFileDialog::ExistingFiles);
    dialog->findChild<QLineEdit *>("fileNameEdit")->setText("\"input.obj\" \"second.obj\"");
    assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
  });
  assert(bts::FileDialog::getOpenFileNames(nullptr, "Open several", directory, filters) ==
         QStringList({one, two}));
  const auto output = directory + "/new-output.obj";
  QTimer::singleShot(0, &app, [&] {
    auto *dialog = active();
    assert(dialog->acceptMode() == QFileDialog::AcceptSave &&
           dialog->fileMode() == QFileDialog::AnyFile);
    assert(dialog->directory().absolutePath() == directory);
    assert(dialog->findChild<QLineEdit *>("fileNameEdit")->text() == "new-output.obj");
    assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
  });
  assert(bts::FileDialog::getSaveFileName(nullptr, "Save one", output, filters) == output);
  QTimer::singleShot(0, &app, [&] {
    auto *dialog = active();
    assert(dialog->fileMode() == QFileDialog::Directory &&
           dialog->testOption(QFileDialog::ShowDirsOnly));
    assert(QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection));
  });
  assert(bts::FileDialog::getExistingDirectory(nullptr, "Directory", directory) == directory);
  selected = "All files (*)";
  QTimer::singleShot(0, &app, [&] {
    auto *dialog = active();
    dialog->selectNameFilter("Meshes (*.obj)");
    dialog->reject();
  });
  assert(bts::FileDialog::getOpenFileName(nullptr, "Cancel", directory, filters, &selected)
             .isEmpty());
  assert(selected == "All files (*)");
  std::cout
      << "Static helper modes, explicit path/filename, selected filter, multiple files and cancel passed\n";
}
}  // namespace
int main(int argc, char **argv)
{
  QTemporaryDir temp;
  const QString root = argc > 2 ? QString::fromLocal8Bit(argv[2]) : temp.path();
  assert(!root.isEmpty());
  qputenv("XDG_CONFIG_HOME", (root + "/config").toUtf8());
  qputenv("XDG_CACHE_HOME", (root + "/cache").toUtf8());
  qputenv("XDG_DATA_HOME", (root + "/data").toUtf8());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/config");
  QApplication app(argc, argv);
  bts::FileDialog::prepare_directory_thread();
  const bool read = argc > 1 && QString(argv[1]) == "--read";
  if (argc > 1 && QString(argv[1]) == "--lifetime") {
    QDir().mkpath(root);
    browser_lifetime(app, root);
    return 0;
  }
  QDir().mkpath(root + "/folders");
  for (int i = 0; i < 13; ++i)
    QDir().mkpath(root + QString("/folders/%1").arg(i));
  const QString first = root + "/folders/0", second = root + "/folders/1",
                dropped = root + "/folders/2";
  if (argc > 1 && QString(argv[1]) == "--stock-qt") {
    bts::FileDialog dialog(nullptr, "Stock Qt fallback", first);
    assert(!dialog.property("_btsFileDialogOwnSettingsSupported").toBool());
    assert(!dialog.property("_btsOwnsFileDialogSettings").toBool());
    assert(dialog.directory().absolutePath() == first);
    std::cout << "Stock Qt fallback preserves synchronous persistence and warns\n";
    return 0;
  }
  if (argc > 1 && QString(argv[1]) == "--plain-qt") {
    {
      QFileDialog dialog(nullptr, "Unmodified Qt behavior", first);
      assert(dialog.property("_btsFileDialogOwnSettingsSupported").toBool());
      assert(!dialog.property("_btsOwnsFileDialogSettings").toBool());
    }
    QSettings settings(QSettings::UserScope, "QtProject");
    assert(settings.value("FileDialog/qtVersion").toString() == "6.8.3");
    std::cout << "Plain QFileDialog keeps upstream settings persistence\n";
    return 0;
  }
  if (argc > 1 && (QString(argv[1]) == "--state-write" || QString(argv[1]) == "--state-read")) {
    state_roundtrip(app, root, QString(argv[1]) == "--state-read");
    return 0;
  }
  if (read) {
    bts::FileDialog dialog(nullptr, "Persistence after app restart");
    assert(dialog.testOption(QFileDialog::DontUseNativeDialog));
    assert(bts::FileDialog::pinned_folders() == QStringList({second, dropped, first}));
    assert(bts::FileDialog::recent_folders().size() == 10);
    dialog.show();
    app.processEvents();
    dialog.grab().save(root + "/file-browser-restarted.png");
    std::cout << "Cross-process Recent/Pinned persistence passed\n";
    return 0;
  }
  for (int i = 0; i < 13; ++i)
    bts::FileDialog::remember_folder(root + QString("/folders/%1").arg(i));
  assert(bts::FileDialog::recent_folders().size() == 10);
  bts::FileDialog dialog(nullptr, "Recent and Pinned folders", first);
  dialog.show();
  app.processEvents();
  assert(dialog.testOption(QFileDialog::DontUseNativeDialog));
  assert(dialog.property("_btsFileDialogOwnSettingsSupported").toBool());
  auto *pins = dialog.findChild<QListWidget *>("fileBrowserPinned");
  assert(pins);
  auto click = [&](const char *name) {
    auto *b = dialog.findChild<QPushButton *>(name);
    assert(b);
    b->click();
  };
  click("fileBrowserPin");
  dialog.setDirectory(second);
  click("fileBrowserPin");
  assert(pins->count() == 2);
  pins->setCurrentRow(1);
  click("fileBrowserPinUp");
  assert(bts::FileDialog::pinned_folders() == QStringList({second, first}));
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(dropped)});
  QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(pins->viewport(), &enter);
  QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(pins->viewport(), &drop);
  assert(bts::FileDialog::pinned_folders().contains(dropped));
  pins->setCurrentRow(2);
  click("fileBrowserPinUp");
  assert(bts::FileDialog::pinned_folders() == QStringList({second, dropped, first}));
  pins->setCurrentRow(2);
  click("fileBrowserUnpin");
  assert(pins->count() == 2);
  dialog.setDirectory(first);
  click("fileBrowserPin");
  assert(pins->count() == 3);
  // File acceptance, not just a direct history helper, records the selected parent.
  const QString file = root + "/folders/12/input.obj";
  {
    QFile output(file);
    assert(output.open(QIODevice::WriteOnly));
    output.write("fixture");
  }
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.selectFile(file);
  assert(QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection));
  QThreadPool::globalInstance()->waitForDone();
  assert(bts::FileDialog::recent_folders().front() == root + "/folders/12");
  bts::FileDialog reopened(nullptr, "All file choosers share folders");
  assert(reopened.findChild<QListWidget *>("fileBrowserPinned")->count() == 3);
  reopened.show();
  app.processEvents();
  reopened.grab().save(root + "/file-browser.png");
  {
    QSettings settings("mPainter", "Preferences");
    const auto original = bts::FileDialog::pinned_folders();
    auto missing = original;
    missing << root + "/unavailable-volume";
    settings.setValue("fileBrowser/pinnedFolders", missing);
    settings.sync();
    bts::FileDialog absent;
    auto *list = absent.findChild<QListWidget *>("fileBrowserPinned");
    assert(list->count() == 4 && !list->item(3)->data(Qt::UserRole + 1).toBool());
    assert(list->item(3)->toolTip().contains("unavailable"));
    settings.setValue("fileBrowser/pinnedFolders", original);
    settings.sync();
  }
  bts::FileDialog::remember_folder_async(first);
  QThreadPool::globalInstance()->waitForDone();
  assert(bts::FileDialog::recent_folders().front() == first);
  QTemporaryDir listing(root + "/listing-XXXXXX");
  assert(listing.isValid());
  if (argc > 2)
    listing.setAutoRemove(false);
  constexpr int listed = 2000;
  for (int i = 0; i < listed; ++i) {
    QFile file(listing.filePath(QString("map_%1.png").arg(i, 4, 10, QChar('0'))));
    assert(file.open(QIODevice::WriteOnly));
    file.write("x");
  }
  bts::FileDialog::prepare_directory_thread();
  auto *chooser = new bts::FileDialog(nullptr, "Listing", listing.path());
  chooser->setFileMode(QFileDialog::ExistingFile);
  chooser->selectFile(listing.filePath("map_0000.png"));
  std::cout << "BTS_FILE_DIALOG_TIMED_CLOSE_BEGIN" << std::endl;
  QElapsedTimer close_timer;
  close_timer.start();
  assert(QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection));
  assert(chooser->result() == QDialog::Accepted);
  delete chooser;
  const double close_ms = close_timer.nsecsElapsed() / 1e6;
  std::cout << "BTS_FILE_DIALOG_TIMED_CLOSE_END" << std::endl;
  QThreadPool::globalInstance()->waitForDone();
  std::cout << "File dialog accept+destroy " << close_ms << " ms over " << listed << " files\n";
  assert(close_ms < 50);
  QWidget owner;
  auto *kept = bts::FileDialog::borrow(&owner, "Fill texture", listing.path(), "Images (*.png)");
  kept->setObjectName("fillTextureFileDialog");
  kept->setFileMode(QFileDialog::ExistingFile);
  kept->selectFile(listing.filePath("map_0000.png"));
  QTimer::singleShot(0, &owner, [kept] {
    assert(QMetaObject::invokeMethod(kept, "accept", Qt::DirectConnection));
  });
  assert(kept->exec() == QDialog::Accepted);
  assert(!kept->isVisible());
  assert(kept->property("btsRetainedFileDialog").toBool());
  auto *again = bts::FileDialog::borrow(&owner, "Fill texture", listing.path(), "Images (*.png)");
  assert(again == kept);
  assert(!again->testAttribute(Qt::WA_DeleteOnClose));
  helper_semantics(app, root);
  browser_modes(app, root);
  std::cout
      << "Shared Qt file browser: max10 recent, pin/unpin/reorder, directory drop, accepted-file history passed\n";
}
