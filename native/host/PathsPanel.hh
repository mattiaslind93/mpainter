#include "Theme.hh"
#pragma once
#include "SurfacePath.hh"
#include <QFrame>
#include <QKeyEvent>
#include <QTimer>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <functional>
namespace bts {
class PathList final : public QListWidget {
 public:
  using QListWidget::QListWidget;
  std::function<void()> remove_requested, duplicate_requested, finish_requested;

 protected:
  bool event(QEvent *event) override
  {
    if (event->type() == QEvent::ShortcutOverride) {
      const auto *key = static_cast<QKeyEvent *>(event);
      if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Return ||
          key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape ||
          (key->key() == Qt::Key_D && key->modifiers() == Qt::ControlModifier))
      {
        event->accept();
        return true;
      }
    }
    return QListWidget::event(event);
  }
  void keyPressEvent(QKeyEvent *event) override
  {
    if (event->key() == Qt::Key_Delete) {
      if (remove_requested)
        remove_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_D && event->modifiers() == Qt::ControlModifier) {
      if (duplicate_requested)
        duplicate_requested();
      event->accept();
      return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Escape)
    {
      if (finish_requested)
        finish_requested();
      event->accept();
      return;
    }
    QListWidget::keyPressEvent(event);
  }
};
class PathsPanel final : public QFrame {
 public:
  std::function<void(const std::string &)> selected, duplicated, removed;
  std::function<void(const std::string &, const std::string &)> renamed;
  std::function<void(const std::string &, bool)> visibility;
  std::function<void()> finished;
  explicit PathsPanel(QWidget *parent) : QFrame(parent)
  {
    setObjectName("pathsPanel");
    setStyleSheet(bts::theme::qss(
        "#pathsPanel{background:@Surface@;border:1px solid @Border@;} QListWidget{background:@Surface@;color:@Text@;border:0;}"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        bts::theme::SpaceWide, bts::theme::Space, bts::theme::SpaceWide, bts::theme::Space);
    layout->setSpacing(4);
    layout->addWidget(new QLabel("PATHS", this));
    list_ = new PathList(this);
    list_->setObjectName("pathsList");
    list_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    layout->addWidget(list_, 1);
    auto *buttons = new QHBoxLayout;
    auto *duplicate = new QPushButton("Duplicate", this);
    duplicate->setObjectName("duplicateSurfacePath");
    buttons->addWidget(duplicate);
    auto *remove = new QPushButton("Delete", this);
    remove->setObjectName("deleteSurfacePath");
    buttons->addWidget(remove);
    auto *finish = new QPushButton("Finish", this);
    finish->setObjectName("finishSurfacePath");
    buttons->addWidget(finish);
    layout->addLayout(buttons);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
      if (item && selected)
        selected(item->data(Qt::UserRole).toString().toStdString());
    });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
      const auto id = item->data(Qt::UserRole).toString().toStdString();
      for (const auto &path : paths_)
        if (path->id == id) {
          if (item->text().toStdString() != path->name && renamed) {
            const auto name = item->text().toStdString();
            QTimer::singleShot(0, this, [this, id, name] {
              if (renamed)
                renamed(id, name);
            });
          }
          else if ((item->checkState() == Qt::Checked) != path->visible && visibility) {
            const bool visible = item->checkState() == Qt::Checked;
            QTimer::singleShot(0, this, [this, id, visible] {
              if (visibility)
                visibility(id, visible);
            });
          }
          break;
        }
    });
    connect(duplicate, &QPushButton::clicked, this, [this] {
      if (list_->currentItem() && duplicated)
        duplicated(list_->currentItem()->data(Qt::UserRole).toString().toStdString());
    });
    connect(remove, &QPushButton::clicked, this, [this] {
      if (list_->currentItem() && removed)
        removed(list_->currentItem()->data(Qt::UserRole).toString().toStdString());
    });
    list_->remove_requested = [this] {
      if (list_->currentItem() && removed)
        removed(list_->currentItem()->data(Qt::UserRole).toString().toStdString());
    };
    list_->duplicate_requested = [this] {
      if (list_->currentItem() && duplicated)
        duplicated(list_->currentItem()->data(Qt::UserRole).toString().toStdString());
    };
    list_->finish_requested = [this] {
      if (finished)
        finished();
    };
    connect(finish, &QPushButton::clicked, this, [this] {
      if (finished)
        finished();
    });
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](QPoint point) {
      auto *item = list_->itemAt(point);
      if (!item)
        return;
      list_->setCurrentItem(item);
      const auto id = item->data(Qt::UserRole).toString().toStdString();
      QMenu menu(this);
      auto *rename = menu.addAction("Rename");
      auto *duplicate = menu.addAction("Duplicate");
      auto *remove = menu.addAction("Delete");
      const auto action = menu.exec(list_->viewport()->mapToGlobal(point));
      if (action == rename) {
        for (int row = 0; row < list_->count(); ++row)
          if (list_->item(row)->data(Qt::UserRole).toString().toStdString() == id) {
            list_->editItem(list_->item(row));
            break;
          }
      }
      else if (action == duplicate && duplicated)
        duplicated(id);
      else if (action == remove && removed)
        removed(id);
    });
    hide();
  }
  void synchronize(const std::vector<std::shared_ptr<const SurfacePath>> &paths,
                   const std::string &selected)
  {
    const QSignalBlocker block(list_);
    paths_ = paths;
    list_->clear();
    for (const auto &path : paths_) {
      auto *item = new QListWidgetItem(QString::fromStdString(path->name), list_);
      item->setData(Qt::UserRole, QString::fromStdString(path->id));
      item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
      item->setCheckState(path->visible ? Qt::Checked : Qt::Unchecked);
      if (path->id == selected)
        list_->setCurrentItem(item);
    }
  }

 private:
  PathList *list_;
  std::vector<std::shared_ptr<const SurfacePath>> paths_;
};
}  // namespace bts
