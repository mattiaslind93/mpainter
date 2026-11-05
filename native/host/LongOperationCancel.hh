#pragma once
#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <functional>

namespace bts {
// Scoped application filter: works even when a spin box or another child owns
// focus. With no pending operation Escape keeps its ordinary editing meaning.
class LongOperationCancel final : public QObject {
 public:
  LongOperationCancel(std::function<bool()> pending,std::function<void()> cancel,QObject *owner=nullptr)
      :QObject(owner),pending_(std::move(pending)),cancel_(std::move(cancel)) {
    qApp->installEventFilter(this);
  }
 protected:
  bool eventFilter(QObject *,QEvent *event) override {
    if(event->type()!=QEvent::ShortcutOverride&&event->type()!=QEvent::KeyPress)return false;
    auto *key=static_cast<QKeyEvent *>(event);
    if(key->key()!=Qt::Key_Escape||key->modifiers()!=Qt::NoModifier||!pending_())return false;
    event->accept();
    if(event->type()==QEvent::KeyPress&&!key->isAutoRepeat())cancel_();
    return true;
  }
 private:
  std::function<bool()> pending_;
  std::function<void()> cancel_;
};

// Reject (Escape, Cancel or window close) requests cancellation while work is
// running. The owner timer closes the dialog only after the worker has retired.
class CancellableOperationDialog : public QDialog {
 public:
  using QDialog::QDialog;
  std::function<bool()> pending;
  std::function<void()> cancel;
  void reject() override {
    if(pending&&pending()){if(cancel)cancel();return;}
    QDialog::reject();
  }
};
}
