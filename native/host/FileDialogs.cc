#include "Theme.hh"
#include "FileDialogs.hh"
#include "FileBrowser.hh"
#include "Performance.hh"
#include <QtGui/private/qfilesystemmodel_p.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <cstdio>
#include <QDir>
#include <memory>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QShowEvent>
#include <QHideEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QDropEvent>
#include <QPushButton>
#include <QSettings>
#include <QThreadPool>
#include <QVBoxLayout>
#include <functional>
#include <mutex>
namespace bts {
namespace {
constexpr auto recent_key = "fileBrowser/recentFolders";
constexpr auto pinned_key = "fileBrowser/pinnedFolders";
constexpr auto state_key = "fileBrowser/dialogState";
constexpr auto directory_key = "fileBrowser/dialogDirectory";
constexpr auto presentation_key = "fileBrowser/presentation";
constexpr auto thumbnail_size_key = "fileBrowser/thumbnailSize";
struct DialogState {
  QByteArray bytes;
  QString directory;
  int presentation = 0, thumbnail_size = 96;
};
// The cache is GUI-thread-only. A newly opened chooser sees the most recently
// closed chooser immediately, even while durable persistence is still queued.
DialogState &cached_state()
{
  // Keep this separate from Preferences.conf: QSettings holds its per-file
  // mutex across fsync, and GUI Recent/Pinned readers must never wait for it.
  static DialogState state = [] {
    QSettings settings("mPainter", "FileDialogState");
    return DialogState{settings.value(state_key).toByteArray(),
                       settings.value(directory_key).toString(),
                       settings.value(presentation_key, 0).toInt(),
                       settings.value(thumbnail_size_key, 96).toInt()};
  }();
  return state;
}
struct StateWriter {
  std::mutex mutex;
  DialogState pending;
  bool running = false;
};
StateWriter state_writer;
void queue_state(DialogState state)
{
  cached_state() = state;
  {
    const std::lock_guard lock(state_writer.mutex);
    state_writer.pending = std::move(state);
    if (state_writer.running)
      return;
    state_writer.running = true;
  }
  // Only value types cross to the worker. One loop owns durable writes; newer
  // snapshots coalesce while it is busy, so an older write can never finish last.
  // QCoreApplication::~QCoreApplication drains this pool before Qt teardown.
  QThreadPool::globalInstance()->start([] {
    for (;;) {
      DialogState next;
      {
        const std::lock_guard lock(state_writer.mutex);
        if (state_writer.pending.bytes.isEmpty()) {
          state_writer.running = false;
          return;
        }
        next = std::move(state_writer.pending);
        state_writer.pending = {};
      }
      QSettings settings("mPainter", "FileDialogState");
      settings.setValue(state_key, next.bytes);
      settings.setValue(directory_key, next.directory);
      settings.setValue(presentation_key, next.presentation);
      settings.setValue(thumbnail_size_key, next.thumbnail_size);
      settings.sync();
      if (settings.status() != QSettings::NoError)
        std::fprintf(
            stderr, "BTS_FILE_DIALOG_STATE_WRITE_FAILED status=%d\n", int(settings.status()));
    }
  });
}
// ~QFileSystemModelPrivate waits for its directory thread on the destroying
// thread. A live LowPriority thread under nice -n 19 is not scheduled for
// tens of milliseconds, which is the import ping tail after the dialog hides.
// Stock holds gatherers whose threads have already exited, so that wait
// returns immediately. Threads are started while the chooser is visible, or
// at workspace creation, never on the close path.
constexpr int finished_listing_stock_size = 2;
QFileInfoGatherer *finished_listing_stock[finished_listing_stock_size] = {};
int finished_listing_count = 0, finishing_listing_count = 0;
void publish_finished_listing(QFileInfoGatherer *gatherer)
{
  if (finished_listing_count < finished_listing_stock_size)
    finished_listing_stock[finished_listing_count++] = gatherer;
  else
    delete gatherer;
  if (finishing_listing_count > 0)
    --finishing_listing_count;
}
void arm_finished_listing()
{
#if QT_CONFIG(filesystemwatcher)
  if (finished_listing_count + finishing_listing_count >= finished_listing_stock_size)
    return;
  auto *gatherer = new QFileInfoGatherer;
  gatherer->requestAbort();
  ++finishing_listing_count;
  auto *app = QCoreApplication::instance();
  QThreadPool::globalInstance()->start([gatherer, app] {
    gatherer->wait();
    if (!app) {
      delete gatherer;
      return;
    }
    QMetaObject::invokeMethod(
        app, [gatherer] { publish_finished_listing(gatherer); }, Qt::QueuedConnection);
  });
#endif
}
QFileInfoGatherer *take_finished_listing()
{
#if QT_CONFIG(filesystemwatcher)
  if (finished_listing_count > 0)
    return finished_listing_stock[--finished_listing_count];
#endif
  return nullptr;
}
QString normalized(const QString &path)
{
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
QStringList stored(const char *key)
{
  QSettings settings("mPainter", "Preferences");
  return settings.value(key).toStringList();
}
void store(const char *key, const QStringList &paths)
{
  QSettings settings("mPainter", "Preferences");
  settings.setValue(key, paths);
  settings.sync();
}
class FolderList : public QListWidget {
 public:
  using QListWidget::QListWidget;
  std::function<void(const QString &)> pin;
  std::function<void()> reordered;

 protected:
  void dragEnterEvent(QDragEnterEvent *event) override
  {
    if (event->mimeData()->hasUrls())
      event->acceptProposedAction();
    else
      QListWidget::dragEnterEvent(event);
  }
  void dragMoveEvent(QDragMoveEvent *event) override
  {
    if (event->mimeData()->hasUrls())
      event->acceptProposedAction();
    else
      QListWidget::dragMoveEvent(event);
  }
  void dropEvent(QDropEvent *event) override
  {
    if (event->mimeData()->hasUrls()) {
      for (const auto &url : event->mimeData()->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isDir())
          pin(url.toLocalFile());
      event->acceptProposedAction();
    }
    else {
      QListWidget::dropEvent(event);
      if (reordered)
        reordered();
    }
  }
};
void populate(QListWidget *list, const QStringList &paths, bool keep_missing)
{
  list->clear();
  for (const auto &path : paths) {
    const bool exists = QFileInfo(path).isDir();
    if (!exists && !keep_missing)
      continue;
    auto *item = new QListWidgetItem(
        QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName(), list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path + (exists ? QString() : "\nFolder unavailable; pin retained"));
    if (!exists) {
      item->setForeground(QColor(bts::theme::Disabled));
      item->setData(Qt::UserRole + 1, false);
    }
    else
      item->setData(Qt::UserRole + 1, true);
  }
}
}  // namespace
QStringList FileDialog::recent_folders()
{
  QStringList result;
  for (const auto &path : stored(recent_key))
    if (QFileInfo(path).isDir() && !result.contains(normalized(path))) {
      result << normalized(path);
      if (result.size() == 10)
        break;
    }
  return result;
}
QStringList FileDialog::pinned_folders()
{
  return stored(pinned_key);
}
void FileDialog::remember_folder(const QString &path)
{
  if (path.isEmpty() || !QFileInfo(path).isDir())
    return;
  auto paths = recent_folders();
  const auto clean = normalized(path);
  if (!paths.isEmpty() && paths.front() == clean)
    return;
  paths.removeAll(clean);
  paths.prepend(clean);
  while (paths.size() > 10)
    paths.removeLast();
  store(recent_key, paths);
}
void FileDialog::remember_folder_async(const QString &path)
{
  QThreadPool::globalInstance()->start([path] { remember_folder(path); });
}
void FileDialog::prepare_directory_thread()
{
  // The palette is installed before workspace startup. Build every icon size
  // here, so the first chooser does not rasterize both atlases inside a click.
  {
    performance::Scope timing("file_browser_icons_startup");
    browser_file_icon(BrowserFileType::Blank);
    browser_file_large_icon(BrowserFileType::Blank);
  }
  arm_finished_listing();
  arm_finished_listing();
  // Workspace creation is outside the import ping. Publish the finished
  // threads before the first chooser can close on this same stack.
  QThreadPool::globalInstance()->waitForDone();
  if (auto *app = QCoreApplication::instance())
    app->processEvents(QEventLoop::ExcludeUserInputEvents);
}
FileDialog *FileDialog::borrow(QWidget *parent,
                               const QString &caption,
                               const QString &directory,
                               const QString &filter)
{
  FileDialog *idle = nullptr;
  if (parent)
    for (auto *dialog : parent->findChildren<QFileDialog *>(QString(), Qt::FindDirectChildrenOnly))
    {
      if (!dialog->property("btsRetainedFileDialog").toBool() || dialog->isVisible())
        continue;
      dialog->setObjectName({});
      if (!idle)
        idle = static_cast<FileDialog *>(dialog);
    }
  if (!idle) {
    {
      performance::Scope timing("file_browser_first_construct");
      idle = new FileDialog(parent, caption, directory, filter);
    }
    idle->setAttribute(Qt::WA_DeleteOnClose, false);
    idle->setProperty("btsRetainedFileDialog", true);
  }
  else {
    idle->setWindowTitle(caption);
    if (!filter.isEmpty())
      idle->setNameFilter(filter);
    if (!directory.isEmpty())
      idle->setDirectory(directory);
    idle->selectFile(QString());
  }
  // exec() deletes the widget if this was set on entry; retained choosers reuse
  // their initialized model and snapshot state when hidden.
  idle->setAttribute(Qt::WA_DeleteOnClose, false);
  return idle;
}
FileDialog::FileDialog(QWidget *parent,
                       const QString &caption,
                       const QString &directory,
                       const QString &filter)
    : QFileDialog(parent, caption, directory, filter)
{
  if (property("_btsFileDialogOwnSettingsSupported").toBool())
    setProperty("_btsOwnsFileDialogSettings", true);
  else {
    static bool warned = false;
    if (!warned) {
      warned = true;
      std::fprintf(
          stderr,
          "BTS_FILE_DIALOG_QT_PATCH_MISSING: using stock synchronous Qt settings; install the pinned file-dialog patch for bounded close latency\n");
    }
  }
  setOption(DontUseNativeDialog);
  setOption(DontUseCustomDirectoryIcons);
  setProperty("btsSharedFileDialog", true);
  relax_model();
  const auto initial_directory = this->directory().absolutePath();
  const QString initial_selection = !directory.isEmpty() && !QFileInfo(directory).isDir() ?
                                        QFileInfo(directory).fileName() :
                                        QString();
  const auto state = cached_state();
  if (!state.bytes.isEmpty() && restoreState(state.bytes)) {
    if (!directory.isEmpty()) {
      setDirectory(initial_directory);
      if (!initial_selection.isEmpty())
        selectFile(initial_selection);
    }
    else if (QFileInfo(state.directory).isDir())
      setDirectory(state.directory);
  }
  auto *pane = new QWidget(this);
  pane->setObjectName("fileBrowserFolders");
  pane->setMinimumWidth(180);
  pane->setMaximumWidth(240);
  auto *body = new QVBoxLayout(pane);
  body->setContentsMargins(0, 0, 6, 0);
  body->addWidget(new QLabel("Pinned folders", pane));
  auto *pins = new FolderList(pane);
  pinned_ = pins;
  pins->setObjectName("fileBrowserPinned");
  pins->setDragDropMode(QAbstractItemView::DragDrop);
  pins->setDefaultDropAction(Qt::MoveAction);
  pins->setContextMenuPolicy(Qt::CustomContextMenu);
  pins->pin = [this](const QString &path) { pin_folder(path); };
  pins->reordered = [this] { save_pins(); };
  body->addWidget(pins, 1);
  auto *buttons = new QHBoxLayout;
  body->addLayout(buttons);
  const auto button =
      [&](const QString &label, const char *name, const QString &tip, auto action) {
        auto *b = new QPushButton(label, pane);
        b->setObjectName(name);
        b->setToolTip(tip);
        b->setMaximumWidth(44);
        buttons->addWidget(b);
        connect(b, &QPushButton::clicked, this, action);
      };
  button("+", "fileBrowserPin", "Pin current folder", [this] {
    pin_folder(this->directory().absolutePath());
  });
  button("−", "fileBrowserUnpin", "Remove selected pin", [this] { unpin_folder(); });
  button("↑", "fileBrowserPinUp", "Move pin up", [this] { move_pin(-1); });
  button("↓", "fileBrowserPinDown", "Move pin down", [this] { move_pin(1); });
  body->addWidget(new QLabel("Recent folders", pane));
  recent_ = new QListWidget(pane);
  recent_->setObjectName("fileBrowserRecent");
  body->addWidget(recent_, 1);
  const auto navigate = [this](QListWidgetItem *item) {
    if (item && item->data(Qt::UserRole + 1).toBool())
      setDirectory(item->data(Qt::UserRole).toString());
  };
  connect(pinned_, &QListWidget::itemClicked, this, navigate);
  connect(recent_, &QListWidget::itemClicked, this, navigate);
  connect(pinned_, &QListWidget::customContextMenuRequested, this, [this](QPoint point) {
    QMenu menu(this);
    menu.addAction(
        "Pin current folder", this, [this] { pin_folder(this->directory().absolutePath()); });
    if (pinned_->itemAt(point)) {
      pinned_->setCurrentItem(pinned_->itemAt(point));
      menu.addAction("Remove pin", this, [this] { unpin_folder(); });
      menu.addAction("Move up", this, [this] { move_pin(-1); });
      menu.addAction("Move down", this, [this] { move_pin(1); });
    }
    menu.exec(pinned_->viewport()->mapToGlobal(point));
  });
  if (auto *grid = qobject_cast<QGridLayout *>(layout())) {
    struct Cell {
      QLayoutItem *item;
      int row, column, rows, columns;
    };
    QList<Cell> cells;
    const int rows = grid->rowCount();
    while (grid->count()) {
      Cell cell;
      grid->getItemPosition(0, &cell.row, &cell.column, &cell.rows, &cell.columns);
      cell.item = grid->takeAt(0);
      cells.append(cell);
    }
    for (auto cell : cells)
      grid->addItem(cell.item, cell.row, cell.column + 1, cell.rows, cell.columns);
    grid->addWidget(pane, 0, 0, rows, 1);
    if (auto *sidebar = findChild<QWidget *>("sidebar"))
      sidebar->hide();
  }
  setup_browser_controls(this, state.presentation, state.thumbnail_size);
  refresh_folders();
  resize(1000, 650);
  // Folder history is bookkeeping. QSettings::sync must not sit on the UI thread
  // between dialog hide and the caller's next paint.
  connect(this, &QDialog::accepted, this, [this] {
    for (const auto &file : selectedFiles()) {
      const QFileInfo info(file);
      remember_folder_async(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
    }
  });
}
FileDialog::~FileDialog()
{
  cancel_browser_previews(this);
  if (!state_published_)
    publish_state();
  release_directory_listing();
}
void FileDialog::publish_state()
{
  state_published_ = true;
  queue_state(
      {saveState(),
       directory().absolutePath(),
       (viewMode() == QFileDialog::Detail ? 0 :
                                            qMax(1, property("fileBrowserPresentation").toInt())),
       property("fileBrowserThumbnailSize").toInt()});
}
void FileDialog::hideEvent(QHideEvent *event)
{
  cancel_browser_previews(this);
  publish_state();
  QFileDialog::hideEvent(event);
}
void FileDialog::showEvent(QShowEvent *event)
{
  state_published_ = false;
  relax_model();
  // start() blocks until the new thread is running. That wait is inside the
  // modal Show window, so the import ping subtracts it. Closing must not do it.
  arm_finished_listing();
  arm_finished_listing();
  QFileDialog::showEvent(event);
  resume_browser_previews(this);
}
void FileDialog::release_directory_listing()
{
#if QT_CONFIG(filesystemwatcher)
  auto *model = findChild<QFileSystemModel *>("qt_filesystem_model");
  if (!model)
    return;
  auto *d = static_cast<QFileSystemModelPrivate *>(QObjectPrivate::get(model));
  if (!d->fileInfoGatherer)
    return;
  auto *busy = d->fileInfoGatherer.release();
  QObject::disconnect(busy, nullptr, nullptr, nullptr);
  busy->requestAbort();
  auto *finished = take_finished_listing();
  const bool prepared = finished != nullptr;
  if (!finished) {
    // No prefinished thread. Joining a freshly started one here is the stall.
    std::fprintf(stderr, "BTS_FILE_DIALOG_LISTING_UNPREPARED\n");
    finished = new QFileInfoGatherer;
    finished->requestAbort();
    finished->wait();
  }
  d->fileInfoGatherer.reset(finished);
  auto *app = QCoreApplication::instance();
  QThreadPool::globalInstance()->start([busy, app] {
    busy->wait();
    if (app)
      QMetaObject::invokeMethod(app, [busy] { delete busy; }, Qt::QueuedConnection);
    else
      delete busy;
  });
  std::fprintf(stderr,
               "BTS_FILE_DIALOG_CLOSE listing=%s title=%s\n",
               prepared ? "finished" : "unprepared",
               qPrintable(windowTitle()));
#endif
}
void FileDialog::relax_model()
{
  struct FastIcons : QFileIconProvider {
    QIcon icon(IconType type) const override
    {
      return browser_file_icon(type == Folder                    ? BrowserFileType::Folder :
                               type == Drive || type == Computer ? BrowserFileType::Drive :
                                                                   BrowserFileType::Blank);
    }
    QIcon icon(const QFileInfo &info) const override
    {
      return browser_file_icon(
          info.fileName() == ".." ? BrowserFileType::Parent :
          info.isDir() ? (info.isSymLink() ? BrowserFileType::Redirect : BrowserFileType::Folder) :
                         browser_file_type(info.fileName()));
    }
    QString type(const QFileInfo &info) const override
    {
      return info.suffix();
    }
  };
  {
    performance::Scope timing("file_browser_icons_lookup");
    browser_file_icon(BrowserFileType::Blank);
    browser_file_large_icon(BrowserFileType::Blank);
  }
  static FastIcons icons;
  setIconProvider(&icons);
  if (auto *model = findChild<QFileSystemModel *>("qt_filesystem_model")) {
    model->setOption(QFileSystemModel::DontWatchForChanges);
    model->setOption(QFileSystemModel::DontUseCustomDirectoryIcons);
  }
}
void FileDialog::refresh_folders()
{
  populate(pinned_, pinned_folders(), true);
  populate(recent_, recent_folders(), false);
  QList<QUrl> urls{QUrl::fromLocalFile(QDir::homePath())};
  for (const auto &path : pinned_folders())
    if (QFileInfo(path).isDir())
      urls << QUrl::fromLocalFile(path);
  for (const auto &path : recent_folders())
    if (!urls.contains(QUrl::fromLocalFile(path)))
      urls << QUrl::fromLocalFile(path);
  setSidebarUrls(urls);
}
void FileDialog::pin_folder(const QString &path)
{
  if (!QFileInfo(path).isDir())
    return;
  auto paths = pinned_folders();
  const auto clean = normalized(path);
  if (!paths.contains(clean)) {
    paths << clean;
    store(pinned_key, paths);
  }
  refresh_folders();
}
void FileDialog::save_pins()
{
  QStringList paths;
  for (int row = 0; row < pinned_->count(); ++row)
    paths << pinned_->item(row)->data(Qt::UserRole).toString();
  store(pinned_key, paths);
}
void FileDialog::unpin_folder()
{
  const int row = pinned_->currentRow();
  if (row < 0)
    return;
  delete pinned_->takeItem(row);
  save_pins();
  refresh_folders();
}
void FileDialog::move_pin(int offset)
{
  const int row = pinned_->currentRow(), next = row + offset;
  if (row < 0 || next < 0 || next >= pinned_->count())
    return;
  auto *item = pinned_->takeItem(row);
  pinned_->insertItem(next, item);
  pinned_->setCurrentRow(next);
  save_pins();
}
QStringList FileDialog::getOpenFileNames(QWidget *parent,
                                         const QString &caption,
                                         const QString &directory,
                                         const QString &filter,
                                         QString *selected_filter,
                                         Options options)
{
  FileDialog dialog(parent, caption, directory, filter);
  dialog.setOptions(options | DontUseNativeDialog);
  dialog.setFileMode(ExistingFiles);
  if (selected_filter && !selected_filter->isEmpty())
    dialog.selectNameFilter(*selected_filter);
  if (dialog.exec() != Accepted)
    return {};
  if (selected_filter)
    *selected_filter = dialog.selectedNameFilter();
  return dialog.selectedFiles();
}
QString FileDialog::getOpenFileName(QWidget *parent,
                                    const QString &caption,
                                    const QString &directory,
                                    const QString &filter,
                                    QString *selected_filter,
                                    Options options)
{
  FileDialog dialog(parent, caption, directory, filter);
  dialog.setOptions(options | DontUseNativeDialog);
  dialog.setFileMode(ExistingFile);
  if (selected_filter && !selected_filter->isEmpty())
    dialog.selectNameFilter(*selected_filter);
  if (dialog.exec() != Accepted || dialog.selectedFiles().isEmpty())
    return {};
  if (selected_filter)
    *selected_filter = dialog.selectedNameFilter();
  return dialog.selectedFiles().front();
}
QString FileDialog::getSaveFileName(QWidget *parent,
                                    const QString &caption,
                                    const QString &directory,
                                    const QString &filter,
                                    QString *selected_filter,
                                    Options options)
{
  FileDialog dialog(parent, caption, directory, filter);
  dialog.setOptions(options | DontUseNativeDialog);
  dialog.setAcceptMode(AcceptSave);
  dialog.setFileMode(AnyFile);
  if (selected_filter && !selected_filter->isEmpty())
    dialog.selectNameFilter(*selected_filter);
  if (dialog.exec() != Accepted || dialog.selectedFiles().isEmpty())
    return {};
  if (selected_filter)
    *selected_filter = dialog.selectedNameFilter();
  return dialog.selectedFiles().front();
}
QString FileDialog::getExistingDirectory(QWidget *parent,
                                         const QString &caption,
                                         const QString &directory,
                                         Options options)
{
  FileDialog dialog(parent, caption, directory);
  dialog.setOptions(options | DontUseNativeDialog);
  dialog.setFileMode(Directory);
  if (dialog.exec() != Accepted || dialog.selectedFiles().isEmpty())
    return {};
  return dialog.selectedFiles().front();
}
}  // namespace bts
