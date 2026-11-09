#include "FileBrowser.hh"
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCache>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHelpEvent>
#include <QPointer>
#include <QScreen>
#include <QVBoxLayout>
#include <QListView>
#include <QScrollBar>
#include <QSet>
#include <QScopedValueRollback>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <deque>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
namespace bts {
namespace {
struct PreviewSession {
  blender::ThumbCancellationToken cancel;
  std::mutex mutex;
  struct Request {
    QString path;
    bool blend;
  };
  struct Result {
    QString path;
    QImage image;
  };
  std::deque<Request> requests;
  std::deque<Result> results;
  bool running = false;
};
class PreviewService : public QObject {
 public:
  QThreadPool pool;
  QList<std::weak_ptr<PreviewSession>> sessions;
  explicit PreviewService(QObject *parent) : QObject(parent)
  {
    pool.setMaxThreadCount(2);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      shutdown();
    });
  }
  void shutdown()
  {
    for (auto &weak : sessions)
      if (auto state = weak.lock())
        state->cancel.cancel();
    pool.clear();
    pool.waitForDone();
  }
  ~PreviewService() override
  {
    shutdown();
  }
  std::shared_ptr<PreviewSession> session()
  {
    sessions.removeIf([](const auto &weak) { return weak.expired(); });
    auto result = std::make_shared<PreviewSession>();
    sessions.append(result);
    return result;
  }
  void request(const std::shared_ptr<PreviewSession> &state, const QString &path, bool blend)
  {
    if (state->cancel.is_cancelled())
      return;
    const auto loader = browser_thumbnail_loader();
    if (!loader)
      return;
    {
      const std::lock_guard lock(state->mutex);
      if (state->requests.size() >= 96)
        return;
      state->requests.push_back({path, blend});
      if (state->running)
        return;
      state->running = true;
    }
    pool.start([state, loader] {
      for (;;) {
        PreviewSession::Request next;
        {
          const std::lock_guard lock(state->mutex);
          if (state->cancel.is_cancelled() || state->requests.empty()) {
            state->requests.clear();
            state->running = false;
            return;
          }
          next = std::move(state->requests.front());
          state->requests.pop_front();
        }
        QImage image;
        try {
          image = loader(next.path, next.blend, state->cancel);
        }
        catch (const std::exception &error) {
          std::fprintf(stderr,
                       "BTS_FILE_BROWSER_THUMBNAIL_FAILED path=%s error=%s\n",
                       qPrintable(next.path),
                       error.what());
        }
        catch (...) {
          std::fprintf(stderr,
                       "BTS_FILE_BROWSER_THUMBNAIL_FAILED path=%s error=unknown\n",
                       qPrintable(next.path));
        }
        {
          const std::lock_guard lock(state->mutex);
          if (!state->cancel.is_cancelled())
            state->results.push_back({std::move(next.path), std::move(image)});
        }
      }
    });
  }
};
QPointer<PreviewService> service_instance;
PreviewService &preview_service()
{
  if (!service_instance)
    service_instance = new PreviewService(QCoreApplication::instance());
  return *service_instance;
}
class PreviewDelegate : public QStyledItemDelegate {
 public:
  std::function<QIcon(const QModelIndex &, bool)> preview;
  mutable bool painting = false;
  explicit PreviewDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
  void paint(QPainter *painter,
             const QStyleOptionViewItem &option,
             const QModelIndex &index) const override
  {
    QScopedValueRollback<bool> scope(painting, true);
    QStyledItemDelegate::paint(painter, option, index);
  }
  void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
  {
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.column() == 0 && preview)
      if (auto icon = preview(index, painting); !icon.isNull())
        option->icon = icon;
  }
};
class BrowserController : public QObject {
  QFileDialog *dialog_;
  QListView *list_;
  QTreeView *tree_;
  QSlider *zoom_;
  QButtonGroup *buttons_;
  QTimer timer_;
  std::shared_ptr<PreviewSession> session_;
  QCache<QString, QIcon> images_{32 * 1024};  // KiB, including raster icon storage.
  QSet<QString> attempted_, failed_;
  int mode_ = -1;
  bool live_ = false;
  QString hover_path_;
  QPoint hover_global_;
  QWidget *tooltip_ = nullptr;
  QLabel *tooltip_image_ = nullptr, *tooltip_text_ = nullptr;

 public:
  BrowserController(QFileDialog *dialog, int mode, int zoom)
      : QObject(dialog),
        dialog_(dialog),
        list_(dialog->findChild<QListView *>("listView")),
        tree_(dialog->findChild<QTreeView *>("treeView"))
  {
    setObjectName("fileBrowserController");
    auto *bar = new QWidget(dialog);
    bar->setObjectName("fileBrowserViews");
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 2, 0, 2);
    buttons_ = new QButtonGroup(this);
    buttons_->setExclusive(true);
    int id = 0;
    for (const auto &name :
         {QString("Vertical List"), QString("Horizontal List"), QString("Thumbnails")})
    {
      auto *button = new QToolButton(bar);
      button->setText(name);
      button->setObjectName(QString("fileBrowserMode%1").arg(id));
      button->setCheckable(true);
      button->setToolTip(name);
      buttons_->addButton(button, id++);
      layout->addWidget(button);
    }
    layout->addStretch();
    layout->addWidget(new QLabel("Thumbnail size", bar));
    zoom_ = new QSlider(Qt::Horizontal, bar);
    zoom_->setObjectName("fileBrowserZoom");
    zoom_->setAccessibleName("Thumbnail size");
    zoom_->setRange(48, 192);
    zoom_->setValue(qBound(48, zoom, 192));
    zoom_->setMaximumWidth(160);
    layout->addWidget(zoom_);
    if (auto *grid = qobject_cast<QGridLayout *>(dialog->layout()))
      grid->addWidget(bar, grid->rowCount(), 1, 1, qMax(1, grid->columnCount() - 1));
    // Qt's two internal buttons are redundant and would bypass the 3-mode state.
    for (const char *name : {"listModeButton", "detailModeButton"})
      if (auto *button = dialog->findChild<QWidget *>(name))
        button->hide();
    connect(buttons_, &QButtonGroup::idClicked, this, [this](int value) { set_mode(value); });
    connect(zoom_, &QSlider::valueChanged, this, [this](int value) {
      dialog_->setProperty("fileBrowserThumbnailSize", value);
      if (mode_ == 2)
        configure_list();
    });
    connect(dialog, &QFileDialog::directoryEntered, this, [this] { invalidate(); });
    if (auto *model = dialog->findChild<QFileSystemModel *>("qt_filesystem_model"))
      connect(model, &QFileSystemModel::rootPathChanged, this, [this] { invalidate(); });
    auto *delegate = new PreviewDelegate(dialog);
    delegate->preview = [this](const QModelIndex &index, bool paint) {
      return preview(index, paint);
    };
    dialog->setItemDelegate(delegate);
    for (auto *view :
         {static_cast<QAbstractItemView *>(list_), static_cast<QAbstractItemView *>(tree_)})
      if (view) {
        view->viewport()->installEventFilter(this);
        view->viewport()->setMouseTracking(true);
      }
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, [this] { complete(); });
    set_mode(qBound(0, mode, 2));
    dialog_->setProperty("fileBrowserThumbnailSize", zoom_->value());
  }
  ~BrowserController() override
  {
    cancel();
  }
  void cancel()
  {
    if (session_) {
      session_->cancel.cancel();
      const std::lock_guard lock(session_->mutex);
      session_->requests.clear();
      session_->results.clear();
    }
    session_.reset();
    timer_.stop();
    attempted_.clear();
    failed_.clear();
    hover_path_.clear();
    if (tooltip_)
      tooltip_->hide();
    live_ = false;
  }
  void resume()
  {
    live_ = true;
    session_ = preview_service().session();
    timer_.start();
  }
  void invalidate()
  {
    const bool resume_after = live_;
    cancel();
    images_.clear();
    attempted_.clear();
    if (resume_after)
      resume();
  }
  void set_mode(int value)
  {
    if (mode_ == value)
      return;
    const bool was_live = live_;
    cancel();
    images_.clear();
    mode_ = value;
    dialog_->setProperty("fileBrowserPresentation", mode_);
    buttons_->button(mode_)->setChecked(true);
    zoom_->setEnabled(mode_ == 2);
    dialog_->setViewMode(mode_ == 0 ? QFileDialog::Detail : QFileDialog::List);
    configure_list();
    if (was_live)
      resume();
  }
  void configure_list()
  {
    if (!list_)
      return;
    if (mode_ == 2) {
      const int size = zoom_->value();
      list_->setViewMode(QListView::IconMode);
      list_->setFlow(QListView::LeftToRight);
      list_->setWrapping(true);
      list_->setResizeMode(QListView::Adjust);
      list_->setMovement(QListView::Static);
      list_->setIconSize({size, size});
      list_->setGridSize({size + 42, size + 46});
      list_->setWordWrap(true);
      list_->setTextElideMode(Qt::ElideMiddle);
    }
    else {
      list_->setViewMode(QListView::ListMode);
      list_->setFlow(QListView::TopToBottom);
      list_->setWrapping(true);
      list_->setResizeMode(QListView::Adjust);
      list_->setMovement(QListView::Static);
      list_->setIconSize({20, 20});
      list_->setGridSize({240, 28});
      list_->setWordWrap(false);
      list_->setTextElideMode(Qt::ElideMiddle);
    }
    list_->viewport()->update();
  }
  bool request_image(const QString &path, BrowserFileType type)
  {
    if (!live_ || !session_ || attempted_.contains(path))
      return false;
    if (type != BrowserFileType::Image && type != BrowserFileType::Blend &&
        type != BrowserFileType::Backup)
      return false;
    {
      const std::lock_guard lock(session_->mutex);
      if (session_->requests.size() + session_->results.size() >= 96)
        return false;
    }
    attempted_.insert(path);
    preview_service().request(session_, path, type != BrowserFileType::Image);
    return true;
  }
  QIcon preview(const QModelIndex &index, bool paint)
  {
    if (mode_ != 2)
      return {};  // Vertical/Horizontal rows retain their small type icons.
    const auto path = index.data(QFileSystemModel::FilePathRole).toString();
    if (auto *icon = images_.object(path))
      return *icon;
    const auto fallback = index.data(Qt::DecorationRole).value<QIcon>();
    const bool folder = index.model()->hasChildren(index);
    auto type = folder ? BrowserFileType::Folder : browser_file_type(path);
    // QFileSystemModel includes directory/link type in its already cached icon.
    if (folder && fallback.cacheKey() == browser_file_icon(BrowserFileType::Redirect).cacheKey())
      type = BrowserFileType::Redirect;
    if (fallback.cacheKey() == browser_file_icon(BrowserFileType::Parent).cacheKey())
      type = BrowserFileType::Parent;
    if (paint)
      request_image(path, type);
    return browser_file_large_icon(type);
  }
  bool eventFilter(QObject *object, QEvent *event) override
  {
    auto *view = object == (list_ ? list_->viewport() : nullptr) ?
                     static_cast<QAbstractItemView *>(list_) :
                 object == (tree_ ? tree_->viewport() : nullptr) ?
                     static_cast<QAbstractItemView *>(tree_) :
                     nullptr;
    if (!view)
      return QObject::eventFilter(object, event);
    if (event->type() == QEvent::ToolTip) {
      const auto *help = static_cast<QHelpEvent *>(event);
      const auto index = view->indexAt(help->pos());
      if (!index.isValid())
        return false;
      const auto path = index.data(QFileSystemModel::FilePathRole).toString();
      const auto type = browser_file_type(path);
      if (type != BrowserFileType::Image && type != BrowserFileType::Blend &&
          type != BrowserFileType::Backup)
        return false;
      hover_path_ = path;
      hover_global_ = help->globalPos();
      request_image(path, type);
      show_hover();
      return true;
    }
    if (event->type() == QEvent::Leave || event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonPress)
    {
      hover_path_.clear();
      if (tooltip_)
        tooltip_->hide();
    }
    return QObject::eventFilter(object, event);
  }
  void show_hover()
  {
    if (hover_path_.isEmpty() || !live_)
      return;
    if (!tooltip_) {
      tooltip_ = new QWidget(dialog_, Qt::ToolTip);
      tooltip_->setObjectName("fileBrowserPreviewTooltip");
      tooltip_->setAttribute(Qt::WA_ShowWithoutActivating);
      auto *layout = new QVBoxLayout(tooltip_);
      tooltip_image_ = new QLabel(tooltip_);
      tooltip_image_->setAlignment(Qt::AlignCenter);
      tooltip_text_ = new QLabel(tooltip_);
      layout->addWidget(tooltip_image_);
      layout->addWidget(tooltip_text_);
    }
    if (auto *icon = images_.object(hover_path_)) {
      tooltip_image_->setPixmap(icon->pixmap(256, 256));
      tooltip_image_->show();
      tooltip_text_->setText(hover_path_.mid(hover_path_.lastIndexOf('/') + 1));
    }
    else {
      tooltip_image_->hide();
      tooltip_text_->setText(failed_.contains(hover_path_) ? "Preview unavailable" :
                                                             "Loading preview…");
    }
    tooltip_->adjustSize();
    auto position = hover_global_ + QPoint(12, 20);
    if (auto *screen = dialog_->screen()) {
      const auto area = screen->availableGeometry();
      position.setX(
          qBound(area.left(), position.x(), qMax(area.left(), area.right() - tooltip_->width())));
      position.setY(
          qBound(area.top(), position.y(), qMax(area.top(), area.bottom() - tooltip_->height())));
    }
    tooltip_->move(position);
    tooltip_->show();
  }
  void complete()
  {
    if (!session_)
      return;
    std::deque<PreviewSession::Result> results;
    {
      const std::lock_guard lock(session_->mutex);
      results.swap(session_->results);
    }
    if (results.empty())
      return;
    int completed = dialog_->property("fileBrowserPreviewsCompleted").toInt();
    for (auto &result : results) {
      if (result.image.isNull()) {
        failed_.insert(result.path);
        continue;
      }
      failed_.remove(result.path);
      attempted_.remove(result.path);
      auto *icon = new QIcon(QPixmap::fromImage(result.image));
      images_.insert(result.path, icon, qMax(1, int(result.image.sizeInBytes() / 1024)));
      ++completed;
    }
    dialog_->setProperty("fileBrowserPreviewsCompleted", completed);
    if (list_)
      list_->viewport()->update();
    if (!hover_path_.isEmpty())
      show_hover();
  }
};
BrowserController *controller(QFileDialog *dialog)
{
  return static_cast<BrowserController *>(dialog->findChild<QObject *>("fileBrowserController"));
}
}  // namespace
void shutdown_browser_previews()
{
  if (service_instance)
    service_instance->shutdown();
}
void setup_browser_controls(QFileDialog *dialog, int mode, int zoom)
{
  new BrowserController(dialog, mode, zoom);
}
void resume_browser_previews(QFileDialog *dialog)
{
  if (auto *value = controller(dialog))
    value->resume();
}
void cancel_browser_previews(QFileDialog *dialog)
{
  if (auto *value = controller(dialog))
    value->cancel();
}
}  // namespace bts
