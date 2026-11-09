// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlendLibraryPanel.hh"
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPromise>
#include <QPushButton>
#include <QThreadPool>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>

namespace bts {
namespace {
struct ListingResult {
  std::optional<BlendLibraryCatalog> catalog;
  QString error;
};
}  // namespace
BlendLibraryPanel::BlendLibraryPanel(QLineEdit *path,
                                     const BlendMeshSelection &selection,
                                     QWidget *parent,
                                     std::string fallback)
    : QWidget(parent), path_(path), fallback_(std::move(fallback)), selection_(selection)
{
  setObjectName("blendLibraryPanel");
  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  summary_ = new QLabel(this);
  summary_->setObjectName("blendLibrarySelectionSummary");
  summary_->setWordWrap(true);
  layout->addWidget(summary_, 1);
  auto *choose_button = new QPushButton("Choose objects / collections…", this);
  choose_button->setObjectName("chooseBlendLibrary");
  choose_button->setAutoDefault(false);
  layout->addWidget(choose_button);
  source_ = path_->text().isEmpty() ? fallback_ : path_->text().toStdString();
  connect(path_, &QLineEdit::textChanged, this, [this] { refresh_path(); });
  connect(choose_button, &QPushButton::clicked, this, [this] { choose(); });
  refresh_path();
}
void BlendLibraryPanel::refresh_summary()
{
  summary_->setText(selection_.empty() ? "No Blender objects or collections selected" :
                                         QString("%1 object(s), %2 collection(s)")
                                             .arg(selection_.objects.size())
                                             .arg(selection_.collections.size()));
}
void BlendLibraryPanel::refresh_path()
{
  const auto next = path_->text().isEmpty() ? fallback_ : path_->text().toStdString();
  if (source_ != next) {
    source_ = next;
    selection_ = {};
  }
  setVisible(is_blend_library(source_));
  refresh_summary();
  if (changed)
    changed();
}
BlendMeshSelection BlendLibraryPanel::options() const
{
  if (!is_blend_library(source_))
    return {};
  selection_.validate(true);
  return selection_;
}
void BlendLibraryPanel::choose()
{
  const auto source = source_;
  if (!is_blend_library(source))
    return;
  auto *dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName("blendAppendDialog");
  dialog->setWindowTitle("Append mesh from Blender library");
  dialog->resize(620, 560);
  auto *layout = new QVBoxLayout(dialog);
  auto *notice = new QLabel(QString::fromStdString(source), dialog);
  notice->setWordWrap(true);
  layout->addWidget(notice);
  auto *search = new QLineEdit(dialog);
  search->setObjectName("blendLibrarySearch");
  search->setPlaceholderText("Filter names…");
  layout->addWidget(search);
  auto *status = new QLabel("Reading library…", dialog);
  status->setObjectName("blendLibraryStatus");
  status->setWordWrap(true);
  layout->addWidget(status);
  auto *tree = new QTreeWidget(dialog);
  tree->setObjectName("blendLibraryTree");
  tree->setHeaderHidden(true);
  tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree->setEnabled(false);
  layout->addWidget(tree, 1);
  auto *hint = new QLabel(
      "Ctrl/Shift selects multiple entries. Collections include nested objects; overlapping choices are imported once.\nStatic original mesh cages are imported; source modifiers, shape keys and animation are not retained. Select source collections rather than collection-instance objects.",
      dialog);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  auto *accept = buttons->button(QDialogButtonBox::Ok);
  accept->setText("Append selection");
  accept->setObjectName("appendBlendSelection");
  accept->setEnabled(false);
  buttons->button(QDialogButtonBox::Cancel)->setObjectName("cancelBlendSelection");
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  connect(tree, &QTreeWidget::itemSelectionChanged, dialog, [tree, accept] {
    accept->setEnabled(!tree->selectedItems().empty());
  });
  connect(search, &QLineEdit::textChanged, dialog, [tree](const QString &text) {
    for (int row = 0; row < tree->topLevelItemCount(); ++row) {
      auto *group = tree->topLevelItem(row);
      bool any = false;
      for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        const bool visible = item->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!visible);
        any |= visible;
      }
      group->setHidden(!any);
    }
  });
  connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, tree, status, source] {
    try {
      if (source != source_)
        throw std::runtime_error("The selected Blender file changed; choose entries again");
      BlendMeshSelection selected;
      for (auto *item : tree->selectedItems()) {
        const auto name = item->data(0, Qt::UserRole + 1).toString().toStdString();
        if (item->data(0, Qt::UserRole).toString() == "object")
          selected.objects.push_back(name);
        else if (item->data(0, Qt::UserRole).toString() == "collection")
          selected.collections.push_back(name);
      }
      selected.validate(true);
      selection_ = std::move(selected);
      refresh_summary();
      if (changed)
        changed();
      dialog->accept();
    }
    catch (const std::exception &error) {
      status->setText(QString::fromUtf8(error.what()));
    }
  });
  auto cancelled = std::make_shared<std::stop_source>();
  connect(dialog, &QObject::destroyed, [cancelled] { cancelled->request_stop(); });
  auto promise = std::make_shared<QPromise<ListingResult>>();
  auto *watcher = new QFutureWatcher<ListingResult>(dialog);
  connect(
      watcher,
      &QFutureWatcher<ListingResult>::finished,
      dialog,
      [this, watcher, tree, status, accept] {
        const auto result = watcher->result();
        if (!result.catalog) {
          status->setText(result.error);
          return;
        }
        tree->setUpdatesEnabled(false);
        for (const auto &[label, names, kind, selected] :
             {std::tuple{"Objects", &result.catalog->objects, "object", &selection_.objects},
              std::tuple{"Collections",
                         &result.catalog->collections,
                         "collection",
                         &selection_.collections}})
        {
          auto *group = new QTreeWidgetItem(tree, {label});
          group->setFlags(Qt::ItemIsEnabled);
          group->setExpanded(true);
          for (const auto &name : *names) {
            auto *item = new QTreeWidgetItem(group, {QString::fromStdString(name)});
            item->setData(0, Qt::UserRole, kind);
            item->setData(0, Qt::UserRole + 1, QString::fromStdString(name));
            item->setSelected(std::find(selected->begin(), selected->end(), name) !=
                              selected->end());
          }
        }
        tree->setEnabled(true);
        tree->setUpdatesEnabled(true);
        accept->setEnabled(!tree->selectedItems().empty());
        status->setText(
            "Choose mesh objects or collections. Cameras follow the project's Import Cameras option.");
      });
  promise->start();
  watcher->setFuture(promise->future());
  QThreadPool::globalInstance()->start([promise, source, cancelled] {
    ListingResult result;
    try {
      result.catalog = inspect_blend_library(source, cancelled->get_token());
    }
    catch (const std::exception &error) {
      result.error = QString::fromUtf8(error.what());
    }
    promise->addResult(std::move(result));
    promise->finish();
  });
  dialog->open();
}
}  // namespace bts
