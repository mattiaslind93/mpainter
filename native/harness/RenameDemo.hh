#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QFocusEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
namespace bts {
class RenameDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *x = w->findChild<T *>(name);
    if (!x)
      qFatal("Missing rename control %s", name);
    return x;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    auto d = state();
    d["stage"] = name;
    QJsonArray rows, shaders;
    auto *list = get<QListWidget>("textureSets");
    for (int i = 0; i < list->count(); ++i)
      if (auto *row = list->itemWidget(list->item(i))) {
        rows.append(row->findChild<QLineEdit *>("textureSetName")->text());
        shaders.append(row->findChild<QPushButton *>("textureSetShader")->text());
      }
    d["texture_set_names"] = rows;
    d["texture_set_shader_names"] = shaders;
    d["shader_name"] = get<QLineEdit>("shaderInstanceName")->text();
    captures.append(d);
    capture_demo_window(w, v).save(output + "/" + name + ".png");
  }
  void rename(QListWidget *list,
              int row,
              const QString &name,
              int key = Qt::Key_Return,
              bool keyboard = false)
  {
    auto *item = list->item(row);
    if (!item)
      qFatal("Missing rename row");
    if (list->objectName() == "textureSets") {
      auto *field = list->itemWidget(item)->findChild<QLineEdit *>("textureSetName");
      if (keyboard) {
        list->setCurrentItem(item);
        list->setFocus();
        QKeyEvent f2(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
        QCoreApplication::sendEvent(list, &f2);
      }
      else {
        const QPointF p(field->rect().center());
        QMouseEvent click(QEvent::MouseButtonDblClick,
                          p,
                          field->mapToGlobal(p.toPoint()),
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(field, &click);
      }
      if (field->isReadOnly())
        qFatal("Texture Set inline field stayed read-only");
      field->setText(name);
      if (key == 0) {
        list->setFocus();
        QFocusEvent focus(QEvent::FocusOut, Qt::MouseFocusReason);
        QCoreApplication::sendEvent(field, &focus);
      }
      else {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(field, &event);
      }
      return;
    }
    list->setCurrentItem(item);
    list->scrollToItem(item);
    const auto rect = list->visualItemRect(item);
    const QPointF p(rect.left() + std::min(175, rect.width() - 85), rect.center().y());
    for (const auto type :
         {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick})
    {
      QMouseEvent event(type,
                        p,
                        list->viewport()->mapToGlobal(p.toPoint()),
                        Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(list->viewport(), &event);
    }
    auto *editor = list->findChild<QLineEdit *>("inlineNameEditor");
    if (!editor || !editor->isVisible())
      qFatal("Double-click did not open inline name editor");
    editor->setText(name);
    if (key == 0) {
      list->setFocus();
      QFocusEvent focus(QEvent::FocusOut, Qt::MouseFocusReason);
      QCoreApplication::sendEvent(editor, &focus);
      return;
    }
    QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &event);
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 1200)
      qFatal("Rename demo timeout stage %d", stage);
    const auto d = state();
    if (stage > 1 && (d["composite_pending"].toBool() || d["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      case 2:
        capture("initial");
        rename(get<QListWidget>("layerStack"), 0, "Ink – åäö");
        break;
      case 3:
        capture("layer-renamed");
        rename(get<QListWidget>("layerStack"), 0, "Cancelled", Qt::Key_Escape);
        break;
      case 4:
        capture("cancelled");
        rename(get<QListWidget>("layerStack"), 0, "  ");
        break;
      case 5:
        capture("empty-rejected");
        get<QToolButton>("addLayerFolder")->click();
        break;
      case 6:
        rename(get<QListWidget>("layerStack"), 0, "Surface group", 0);
        break;
      case 7:
        capture("folder-renamed");
        rename(get<QListWidget>("textureSets"), 0, "Hero Surface");
        break;
      case 8:
        capture("material-renamed");
        rename(get<QListWidget>("textureSets"), 0, "Keyboard Surface", Qt::Key_Enter, true);
        break;
      case 9:
        capture("material-f2");
        rename(get<QListWidget>("textureSets"), 0, "Hero Surface", 0);
        break;
      case 10:
        capture("material-focus-out");
        rename(get<QListWidget>("textureSets"), 0, "Cancelled", Qt::Key_Escape);
        break;
      case 11:
        capture("material-cancelled");
        rename(get<QListWidget>("textureSets"), 0, "   ");
        break;
      case 12:
        capture("material-empty-rejected");
        action("undoDocument");
        action("undoDocument");
        action("undoDocument");
        break;
      case 13:
        capture("undo-material");
        action("redoDocument");
        action("redoDocument");
        action("redoDocument");
        break;
      case 14:
        capture("redo-material");
        {
          get<QDialog>("shaderSettings")->show();
          auto *name = get<QLineEdit>("shaderInstanceName");
          name->setFocus();
          name->setText("Hero Shader");
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QCoreApplication::sendEvent(name, &enter);
        }
        break;
      case 15:
        capture("shader-renamed");
        get<QPushButton>("shaderUndo")->click();
        break;
      case 16:
        capture("undo-shader");
        get<QPushButton>("shaderRedo")->click();
        break;
      case 17:
        capture("redo-shader");
        get<QDialog>("shaderSettings")->hide();
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 18:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 19:
        capture("reopened");
        rename(get<QListWidget>("textureSets"), 0, "Reopened Surface", Qt::Key_Return, true);
        break;
      case 20:
        capture("reopened-renamed");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 21:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 22:
        capture("reopened-again");
        timer.stop();
        {
          QFile f(output + "/rename.json");
          if (!f.open(QIODevice::WriteOnly))
            qFatal("Rename proof write failed");
          f.write(QJsonDocument(QJsonObject{{"captures", captures}}).toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
    frame = state()["render_frames"].toInt() + 3;
  }

 public:
  RenameDemo(QWidget *window,
             QWidget *viewport,
             QString directory,
             QString source,
             std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        output(directory),
        mesh(source),
        state(std::move(snapshot))
  {
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(100);
    connect(&dialogs, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Rename GUI error: %s", qPrintable(box->text()));
          if (auto *button = box->button(QMessageBox::Discard))
            button->click();
        }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
