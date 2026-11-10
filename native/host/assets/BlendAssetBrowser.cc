// SPDX-License-Identifier: GPL-3.0-or-later
#include "Theme.hh"
#include "BlendLibraryPreferences.hh"
#include <QInputDialog>
#include <QMessageBox>
#include "BlendAssetBrowser.hh"
#include "FileDialogs.hh"
#include <QCheckBox>
#include <QColor>
#include <QPixmap>
#include <QVariant>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <deque>
#include <mutex>
#include <set>
namespace bts {
namespace {
class Browser final : public QObject {
  QDialog *dialog_;
  QLabel *status_;
  QLineEdit *search_;
  QTreeWidget *catalogs_;
  QListWidget *items_;
  QCheckBox *assets_;
  QPushButton *import_, *preview_ = nullptr;
  QProgressBar *progress_;
  BlendAssetBrowserHooks hooks_;
  QTimer poll_, populate_;
  BlendAssetLibrary library_;
  std::shared_ptr<std::stop_source> scan_cancel_ = std::make_shared<std::stop_source>();
  std::shared_future<BlendAssetLibrary> scan_;
  struct Progress {
    std::mutex mutex;
    QString text;
  };
  std::shared_ptr<Progress> scan_progress_ = std::make_shared<Progress>();
  std::shared_ptr<std::atomic_bool> job_cancel_;
  std::shared_future<PreparedBlendMaterial> job_;
  int job_row_ = -1;
  bool job_preview_ = false;
  std::optional<int> wanted_import_;
  std::shared_future<NativeMaterialPreviewResult> native_preview_;
  std::shared_ptr<std::atomic_bool> native_preview_cancel_;
  int native_preview_row_ = -1;
  std::map<int, QImage> previews_;
  std::map<int, QString> errors_;
  std::set<int> attempted_, native_previews_;
  std::vector<int> matching_;
  std::size_t insertion_ = 0;
  int selected() const
  {
    const auto *item = items_->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : -1;
  }
  void cancel_preview()
  {
    if (native_preview_cancel_)
      native_preview_cancel_->store(true);
    native_preview_ = {};
    native_preview_cancel_.reset();
    dialog_->setProperty("previewPending", false);
  }
  void cancel()
  {
    cancel_preview();
    scan_cancel_->request_stop();
    if (job_cancel_)
      job_cancel_->store(true);
    poll_.stop();
    populate_.stop();
  }
  QImage thumbnail(int row) const
  {
    if (auto i = previews_.find(row); i != previews_.end())
      return i->second;
    QImage placeholder(64, 64, QImage::Format_RGB32);
    placeholder.fill(errors_.contains(row) ? QColor(bts::theme::ErrorSurface) :
                                             QColor(bts::theme::Raised));
    return placeholder;
  }
  QString description(int row) const
  {
    const auto &entry = library_.materials[row];
    return entry.material.name + "\n" +
           (entry.material.marked_asset ? "Marked asset" : "Material datablock") + " — " +
           (entry.catalog_path.isEmpty() ? "Unassigned" : entry.catalog_path) + "\n" + entry.file +
           "\n" + entry.material.description + "\nTags: " + entry.material.tags.join(", ") +
           (errors_.contains(row) ? "\n" + errors_.at(row) : QString{}) +
           (native_previews_.contains(row) ?
                "\nGenerated native Base Color preview — UV tile1001; not a shaded material render" :
            entry.material.preview.isNull() ?
                (hooks_.native_shader ?
                     (previews_.contains(row) ?
                          "\nGenerated native Base Color preview — UV tile1001; not a shaded material render" :
                          "\nNo saved preview — request a native Base Color preview or Import") :
                     "\nGenerated first-tile channel-preset preview") :
                "\nSaved Blender PreviewImage");
  }
  void update_row(int row)
  {
    for (int i = 0; i < items_->count(); ++i) {
      auto *item = items_->item(i);
      if (item->data(Qt::UserRole).toInt() != row)
        continue;
      item->setIcon(QPixmap::fromImage(thumbnail(row)));
      item->setToolTip(description(row));
      item->setData(Qt::UserRole + 5,
                    previews_.contains(row) && library_.materials[row].material.preview.isNull());
      item->setData(Qt::UserRole + 6, errors_.contains(row) ? errors_.at(row) : QString{});
    }
  }
  void selection()
  {
    const int row = selected();
    if (native_preview_.valid() && row != native_preview_row_) {
      cancel_preview();
      progress_->hide();
    }
    if (preview_)
      preview_->setEnabled(row >= 0 && !native_preview_.valid());
    import_->setEnabled(row >= 0 && !wanted_import_);
    if (row >= 0 && errors_.contains(row))
      status_->setText(errors_.at(row));
    if (job_.valid() && job_preview_ && row != job_row_)
      job_cancel_->store(true);
    poll_.start();
  }
  void filter()
  {
    dialog_->setProperty("rowsReady", false);
    const auto catalog = catalogs_->currentItem() ?
                             catalogs_->currentItem()->data(0, Qt::UserRole).toString() :
                             QString{};
    matching_.clear();
    for (int i = 0; i < int(library_.materials.size()); ++i)
      if (blend_library_matches(
              library_.materials[i], search_->text(), catalog, assets_->isChecked()))
        matching_.push_back(i);
    items_->clear();
    insertion_ = 0;
    dialog_->setProperty("matchedMaterials", int(matching_.size()));
    populate_.start();
    poll_.start();
  }
  void populate()
  {
    QElapsedTimer elapsed;
    elapsed.start();
    items_->setUpdatesEnabled(false);
    int count = 0;
    while (insertion_ < matching_.size() && count++ < 64 && elapsed.elapsed() < 4) {
      const int row = matching_[insertion_++];
      const auto &entry = library_.materials[row];
      auto *item = new QListWidgetItem(QPixmap::fromImage(thumbnail(row)),
                                       entry.material.name + "\n" + entry.relative_file,
                                       items_);
      item->setData(Qt::UserRole, row);
      item->setData(Qt::UserRole + 1, entry.file);
      item->setData(Qt::UserRole + 2, entry.catalog_path);
      item->setData(Qt::UserRole + 3, entry.material.marked_asset);
      item->setData(Qt::UserRole + 4, !entry.material.preview.isNull());
      item->setData(Qt::UserRole + 5, previews_.contains(row) && entry.material.preview.isNull());
      item->setToolTip(description(row));
    }
    items_->setUpdatesEnabled(true);
    if (insertion_ == matching_.size()) {
      populate_.stop();
      status_->setText(
          QString("%1 matching materials · %2 source files%3")
              .arg(matching_.size())
              .arg(library_.files)
              .arg(library_.errors.isEmpty() ?
                       QString{} :
                       QString(" · %1 file errors (see tooltip)").arg(library_.errors.size())));
      status_->setToolTip(library_.errors.join('\n'));
      if (items_->count())
        items_->setCurrentRow(0);
      dialog_->setProperty("rowsReady", true);
      poll_.start();
    }
  }
  void start(int row, bool preview)
  {
    const auto &entry = library_.materials.at(row);
    status_->setStyleSheet({});
    job_cancel_ = std::make_shared<std::atomic_bool>(false);
    job_row_ = row;
    job_preview_ = preview;
    job_ = hooks_.prepare(entry.file, entry.material.name, job_cancel_, preview);
    dialog_->setProperty("previewPending", preview);
    progress_->setRange(0, 0);
    progress_->show();
    status_->setText((preview ? "Generating preview — " : "Importing — ") + entry.material.name);
    poll_.start();
  }
  void poll()
  {
    if (native_preview_.valid()) {
      if (native_preview_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const int row = native_preview_row_;
        try {
          auto result = native_preview_.get();
          if (!native_preview_cancel_->load() && selected() == row) {
            previews_[row] = std::move(result.image);
            native_previews_.insert(row);
            errors_.erase(row);
            dialog_->setProperty("generatedNativeBaseColorPreviews",
                                 dialog_->property("generatedNativeBaseColorPreviews").toInt() +
                                     1);
            status_->setText(
                "Native Base Color preview — UV tile1001; not a shaded material render");
          }
        }
        catch (const std::exception &error) {
          errors_[row] = QString::fromUtf8(error.what());
          status_->setText(errors_.at(row));
        }
        native_preview_ = {};
        native_preview_cancel_.reset();
        dialog_->setProperty("previewPending", false);
        progress_->hide();
        if (preview_)
          preview_->setEnabled(selected() >= 0);
        update_row(row);
      }
      else
        return;
    }
    if (scan_.valid()) {
      {
        std::lock_guard lock(scan_progress_->mutex);
        if (!scan_progress_->text.isEmpty())
          status_->setText(scan_progress_->text);
      }
      if (scan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      try {
        library_ = scan_.get();
        scan_ = {};
        catalogs_->clear();
        auto *all = new QTreeWidgetItem(catalogs_, QStringList{"All catalogs"});
        all->setData(0, Qt::UserRole, QString{});
        auto *none = new QTreeWidgetItem(catalogs_, QStringList{"Unassigned"});
        none->setData(0, Qt::UserRole, "@unassigned");
        QMap<QString, QTreeWidgetItem *> paths;
        for (const auto &path : library_.catalogs.values()) {
          QString prefix;
          QTreeWidgetItem *parent = nullptr;
          for (const auto &part : path.split('/', Qt::SkipEmptyParts)) {
            prefix += prefix.isEmpty() ? part : "/" + part;
            auto *item = paths.value(prefix);
            if (!item) {
              item = parent ? new QTreeWidgetItem(parent, QStringList{part}) :
                              new QTreeWidgetItem(catalogs_, QStringList{part});
              item->setData(0, Qt::UserRole, prefix);
              paths.insert(prefix, item);
            }
            parent = item;
          }
        }
        for (int row = 0; row < int(library_.materials.size()); ++row)
          if (!library_.materials[row].material.preview.isNull())
            previews_[row] = library_.materials[row].material.preview;
        catalogs_->expandAll();
        catalogs_->setCurrentItem(all);
        dialog_->setProperty("libraryLoaded", true);
        dialog_->setProperty("sourceFiles", library_.files);
        dialog_->setProperty("allMaterials", int(library_.materials.size()));
        progress_->hide();
        filter();
      }
      catch (const std::exception &error) {
        scan_ = {};
        status_->setText(QString::fromUtf8(error.what()));
        status_->setStyleSheet(bts::theme::qss("color:@Error@"));
        progress_->hide();
        poll_.stop();
      }
      return;
    }
    if (job_.valid()) {
      if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      const int row = job_row_;
      try {
        if (!job_cancel_->load()) {
          const auto result = job_.get();
          auto image = blend_material_preview(result.material);
          if (!image.isNull())
            previews_[row] = std::move(image);
          if (!job_preview_) {
            if (result.preview_only)
              throw std::logic_error("Preview cannot be installed");
            hooks_.install(result);
            wanted_import_.reset();
            const char *counter = hooks_.native_shader ? "nativeQueuedCount" : "importedCount";
            dialog_->setProperty(counter, dialog_->property(counter).toInt() + 1);
            status_->setText(
                (hooks_.native_shader ?
                     "Native shader queued — appears in Shelf when preparation finishes: " :
                     "Imported to Shelf — ") +
                library_.materials[row].material.name);
          }
        }
      }
      catch (const std::exception &error) {
        errors_[row] = QString::fromUtf8(error.what());
        status_->setText(errors_.at(row));
        status_->setStyleSheet(bts::theme::qss("color:@Error@"));
        wanted_import_.reset();
      }
      if (!job_cancel_->load()) {
        attempted_.insert(row);
        if (job_preview_ && !errors_.contains(row))
          status_->setText(QString("%1 matching materials · %2 source files")
                               .arg(matching_.size())
                               .arg(library_.files));
      }
      job_ = {};
      job_cancel_.reset();
      dialog_->setProperty("previewPending", false);
      progress_->hide();
      update_row(row);
      import_->setEnabled(selected() >= 0 && !wanted_import_);
      int generated = 0;
      for (const auto &[index, image] : previews_)
        generated += library_.materials[index].material.preview.isNull();
      dialog_->setProperty("generatedPreviews", generated);
    }
    if (wanted_import_) {
      start(*wanted_import_, false);
      return;
    }
    if (populate_.isActive())
      return;
    if (hooks_.native_shader) {
      poll_.stop();
      return;
    }
    const int selected_row = selected();
    if (selected_row >= 0 && !previews_.contains(selected_row) &&
        !attempted_.contains(selected_row))
    {
      start(selected_row, true);
      return;
    }
    // Generate just the visible cards. Scrolling schedules the next batch; browsing
    // does not decode or snapshot an entire library's image payloads at once.
    const QRect visible = items_->viewport()->rect();
    for (int i = 0; i < items_->count(); ++i) {
      auto *item = items_->item(i);
      const int row = item->data(Qt::UserRole).toInt();
      if (items_->visualItemRect(item).intersects(visible) && !previews_.contains(row) &&
          !attempted_.contains(row))
      {
        start(row, true);
        return;
      }
    }
    poll_.stop();
  }

 public:
  Browser(QDialog *dialog, QString root, BlendAssetBrowserHooks hooks)
      : QObject(dialog), dialog_(dialog), hooks_(std::move(hooks))
  {
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel("Read-only library: " + root, dialog);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    auto *search_row = new QHBoxLayout;
    search_ = new QLineEdit(dialog);
    search_->setObjectName("blendLibrarySearch");
    search_->setPlaceholderText("Search names, catalogs, tags or source files");
    search_row->addWidget(search_, 1);
    assets_ = new QCheckBox("Marked assets only", dialog);
    assets_->setObjectName("blendLibraryAssetsOnly");
    assets_->setChecked(true);
    search_row->addWidget(assets_);
    layout->addLayout(search_row);
    auto *splitter = new QSplitter(dialog);
    catalogs_ = new QTreeWidget(splitter);
    catalogs_->setObjectName("blendLibraryCatalogs");
    catalogs_->setHeaderHidden(true);
    catalogs_->setMinimumWidth(180);
    items_ = new QListWidget(splitter);
    items_->setObjectName("blendLibraryMaterials");
    items_->setViewMode(QListView::IconMode);
    items_->setResizeMode(QListView::Adjust);
    items_->setMovement(QListView::Static);
    items_->setIconSize({64, 64});
    items_->setGridSize({140, 110});
    items_->setWordWrap(true);
    items_->setUniformItemSizes(true);
    splitter->setSizes({220, 660});
    layout->addWidget(splitter, 1);
    status_ = new QLabel("Reading library…", dialog);
    status_->setObjectName("blendLibraryStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    progress_ = new QProgressBar(dialog);
    progress_->setRange(0, 0);
    progress_->setMaximumHeight(12);
    layout->addWidget(progress_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    import_ = buttons->addButton("Import to Shelf", QDialogButtonBox::ActionRole);
    import_->setObjectName("blendLibraryImport");
    import_->setEnabled(false);
    if (hooks_.native_shader && hooks_.base_color_preview) {
      preview_ = buttons->addButton("Generate Base Color preview", QDialogButtonBox::ActionRole);
      preview_->setObjectName("blendLibraryBaseColorPreview");
      preview_->setToolTip(
          "Native GPU Base Color on UV tile1001; object/view/baked AO sources require a real preview context");
      preview_->setEnabled(false);
      connect(preview_, &QPushButton::clicked, this, [this] {
        const int row = selected();
        if (row < 0)
          return;
        cancel_preview();
        native_preview_cancel_ = std::make_shared<std::atomic_bool>(false);
        native_preview_row_ = row;
        try {
          const auto &entry = library_.materials.at(row);
          native_preview_ = hooks_.base_color_preview(
              entry.file, entry.material.name, native_preview_cancel_);
          dialog_->setProperty("previewPending", true);
          preview_->setEnabled(false);
          progress_->show();
          status_->setText("Preparing native Base Color preview — " + entry.material.name);
          poll_.start();
        }
        catch (const std::exception &error) {
          errors_[row] = QString::fromUtf8(error.what());
          status_->setText(errors_.at(row));
          update_row(row);
        }
      });
    }
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this] { cancel(); });
    connect(import_, &QPushButton::clicked, this, [this] {
      const int row = selected();
      if (row < 0)
        return;
      cancel_preview();
      wanted_import_ = row;
      import_->setEnabled(false);
      if (job_.valid() && job_preview_)
        job_cancel_->store(true);
      poll_.start();
    });
    connect(items_, &QListWidget::itemDoubleClicked, this, [this](auto *) { import_->click(); });
    connect(items_, &QListWidget::currentRowChanged, this, [this] { selection(); });
    connect(
        items_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { poll_.start(); });
    connect(search_, &QLineEdit::textChanged, this, [this] {
      dialog_->setProperty("rowsReady", false);
      filter();
    });
    connect(assets_, &QCheckBox::toggled, this, [this] { filter(); });
    connect(catalogs_, &QTreeWidget::currentItemChanged, this, [this] { filter(); });
    poll_.setInterval(20);
    connect(&poll_, &QTimer::timeout, this, [this] { poll(); });
    populate_.setInterval(0);
    connect(&populate_, &QTimer::timeout, this, [this] { populate(); });
    auto progress = scan_progress_;
    scan_ = hooks_.scan(root, scan_cancel_, [progress](const QString &text) {
      std::lock_guard lock(progress->mutex);
      progress->text = text;
    });
    poll_.start();
  }
  ~Browser()
  {
    cancel();
  }
};
}  // namespace
void browse_blend_asset_library(QWidget *owner, BlendAssetBrowserHooks hooks)
{
  QString selected;
  const auto configured = material_library_paths();
  if (!configured.isEmpty()) {
    QStringList choices = configured;
    choices.append("Choose another folder…");
    choices.append("Choose a .blend file…");
    bool accepted = false;
    const auto choice = QInputDialog::getItem(owner,
                                              "Material Libraries",
                                              "Choose an explicitly configured library",
                                              choices,
                                              0,
                                              false,
                                              &accepted);
    if (!accepted)
      return;
    if (configured.contains(choice))
      selected = choice;
    else if (choice == "Choose a .blend file…") {
      selected = FileDialog::getOpenFileName(
          owner, "Choose Blender material library", {}, "Blender libraries (*.blend)");
      if (selected.isEmpty())
        return;
    }
  }
  if (selected.isEmpty()) {
    auto *folder = FileDialog::borrow(owner, "Choose Blender Asset Library folder");
    folder->setObjectName("blendLibraryFolderDialog");
    folder->setAcceptMode(QFileDialog::AcceptOpen);
    folder->setFileMode(QFileDialog::Directory);
    folder->setNameFilter("Folders (*)");
    folder->setOption(QFileDialog::ShowDirsOnly);
    if (folder->exec() != QDialog::Accepted || folder->selectedFiles().isEmpty())
      return;
    selected = folder->selectedFiles().first();
  }
  try {
    selected = validated_material_library_path(selected);
  }
  catch (const std::exception &error) {
    QMessageBox::warning(owner, "Library unavailable", QString::fromUtf8(error.what()));
    return;
  }
  auto *dialog = new QDialog(owner);
  dialog->setObjectName("blendAssetLibraryBrowser");
  dialog->setWindowTitle(hooks.native_shader ? "Blender Asset Library — native shaders" :
                                               "Blender Asset Library — material channel presets");
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->resize(960, 680);
  dialog->setMinimumSize(760, 520);
  new Browser(dialog, selected, std::move(hooks));
  dialog->open();
}
}  // namespace bts
