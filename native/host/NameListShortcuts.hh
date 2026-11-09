#pragma once
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
namespace bts {
// Editable names own F2 while focused; viewport layout shortcuts keep it elsewhere.
class NameListShortcuts final : public QObject {
 public:
  explicit NameListShortcuts(QListWidget *list) : QObject(list), list_(list)
  {
    list->installEventFilter(this);
    list->viewport()->installEventFilter(this);
  }

 protected:
  bool eventFilter(QObject *, QEvent *event) override
  {
    if (event->type() != QEvent::ShortcutOverride)
      return false;
    const auto *key = static_cast<QKeyEvent *>(event);
    const auto *item = list_->currentItem();
    if (key->key() != Qt::Key_F2 || key->modifiers() != Qt::NoModifier || !item ||
        !(item->flags() & Qt::ItemIsEditable) || !(item->flags() & Qt::ItemIsEnabled))
      return false;
    const auto *focus = QApplication::focusWidget();
    const bool name = qobject_cast<const QLineEdit *>(focus) && list_->isAncestorOf(focus) &&
                      (focus->objectName() == "inlineNameEditor" ||
                       focus->objectName() == "textureSetName");
    if (focus != list_ && focus != list_->viewport() && !name)
      return false;
    event->accept();
    return true;
  }

 private:
  QListWidget *list_;
};
}  // namespace bts
