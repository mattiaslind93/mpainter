#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
namespace bts {
class BakedMapsDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      qFatal("Missing baked control: %s", name);
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    w->setProperty("bakedSample", true);
    auto data = state();
    w->setProperty("bakedSample", false);
    data["stage"] = name;
    QJsonArray shelf;
    auto *list = get<QListWidget>("shelfAssets");
    for (int i = 0; i < list->count(); ++i) {
      const auto *item = list->item(i);
      if (item->data(Qt::UserRole).toString().startsWith("baked:"))
        shelf.append(QJsonObject{{"id", item->data(Qt::UserRole).toString()},
                                 {"name", item->text()},
                                 {"tooltip", item->toolTip()},
                                 {"icon", !item->icon().isNull()}});
    }
    data["shelf"] = shelf;
    captures.append(data);
    QFile progress(output + "/baked-maps-progress.json");
    if (progress.open(QIODevice::WriteOnly))
      progress.write(QJsonDocument(QJsonObject{{"captures", captures}}).toJson());
    capture_demo_window(w, v).save(output + "/" + name + ".png");
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2200)
      qFatal("Baked demo timeout stage=%d", stage);
    const auto data = state();
    if (stage > 1 && (data["composite_pending"].toBool() || data["texture_loading"].toBool() ||
                      data["property_pending"].toBool() || data["render_frames"].toInt() < frame))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QCheckBox>("preserveUvTiles")->setChecked(true);
        auto *res = get<QComboBox>("documentResolution");
        res->setCurrentIndex(res->findData(128));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 2:
        busy = true;
        QTimer::singleShot(0, this, [this] {
          action("bakeMeshMapsAction");
          busy = false;
        });
        break;
      case 3: {
        auto *categories = get<QListWidget>("shelfCategories");
        const auto found = categories->findItems("Baked", Qt::MatchExactly);
        if (found.empty())
          qFatal("Baked category missing");
        categories->setCurrentItem(found.front());
        break;
      }
      case 4:
        capture("baked-shelf");
        get<QToolButton>("addFillLayer")->click();
        action("addBitmapMask");
        break;
      case 5: {
        capture("mask-before");
        w->setProperty("bakedSourceRole", 3);
        auto *list = get<QListWidget>("shelfAssets");
        QListWidgetItem *ao = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString().endsWith(":3")) {
            ao = list->item(i);
            break;
          }
        if (!ao)
          qFatal("Baked AO missing");
        list->setCurrentItem(ao);
        list->itemDoubleClicked(ao);
        break;
      }
      case 6: {
        capture("mask-applied");
        w->setProperty("bakedSourceRole", 5);
        auto *list = get<QListWidget>("shelfAssets");
        QListWidgetItem *position = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole).toString().endsWith(":5")) {
            position = list->item(i);
            break;
          }
        if (!position)
          qFatal("Baked Position missing");
        list->setCurrentItem(position);
        list->itemDoubleClicked(position);
        break;
      }
      case 7:
        capture("position-mask");
        busy = true;
        action("guiSkewSave");
        busy = false;
        break;
      case 8:
        busy = true;
        action("guiSkewOpen");
        busy = false;
        break;
      case 9: {
        capture("reopened");
        timer.stop();
        QFile file(output + "/baked-maps.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Cannot write baked proof");
        file.write(QJsonDocument(QJsonObject{{"captures", captures},
                                             {"pid", QCoreApplication::applicationPid()}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 5;
  }

 public:
  BakedMapsDemo(QWidget *window,
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
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *dialog = qobject_cast<QDialog *>(widget);
            dialog && dialog->isVisible() && dialog->objectName() == "meshBakeDialog" &&
            !dialog->property("aiPrepared").toBool())
        {
          dialog->setProperty("aiPrepared", true);
          auto *maps = get<QListWidget>("bakeMapTypes");
          for (int i = 0; i < maps->count(); ++i)
            maps->item(i)->setCheckState(i >= 3 ? Qt::Checked : Qt::Unchecked);
          get<QCheckBox>("bakeUseProjectMesh")->setChecked(true);
          get<QComboBox>("bakeResolution")
              ->setCurrentIndex(get<QComboBox>("bakeResolution")->findData(128));
          get<QSpinBox>("bakeSecondaryRays")->setValue(8);
          get<QPushButton>("startMeshBake")->click();
        }
        if (auto *dialog = qobject_cast<QDialog *>(widget);
            dialog && dialog->isVisible() && dialog->objectName() == "meshBakeProgress")
        {
          auto *finish = dialog->findChild<QPushButton *>("finishMeshBake");
          if (finish && finish->text() == "OK")
            finish->click();
          else if (finish && finish->text() == "Close")
            qFatal("Bake failed: %s",
                   qPrintable(dialog->findChild<QLabel *>("meshBakeProgressStatus")->text()));
        }
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            qFatal("Baked GUI error: %s", qPrintable(box->text()));
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
      }
    });
    dialogs.start(40);
  }
};
}  // namespace bts
