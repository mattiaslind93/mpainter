#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

namespace bts {
class BlendLibraryDemo final : public QObject {
  QWidget *w, *v;
  QString output, mesh;
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  QTimer timer, dialogs;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Blend import control missing: %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void apply_action(const char *name)
  {
    busy = true;
    action(name);
    busy = false;
  }
  bool listing_ready()
  {
    auto *tree = w->findChild<QTreeWidget *>("blendLibraryTree");
    return tree && tree->isEnabled() && tree->topLevelItemCount() == 2;
  }
  void select(const QString &kind, const QStringList &names)
  {
    auto *tree = get<QTreeWidget>("blendLibraryTree");
    tree->clearSelection();
    int found = 0;
    for (int r = 0; r < tree->topLevelItemCount(); ++r) {
      auto *group = tree->topLevelItem(r);
      for (int i = 0; i < group->childCount(); ++i) {
        auto *item = group->child(i);
        if (item->data(0, Qt::UserRole).toString() == kind && names.contains(item->text(0))) {
          item->setSelected(true);
          ++found;
        }
      }
    }
    if (found != names.size())
      qFatal("Requested source entries were not listed");
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    capture_viewport_framebuffer(v).save(output + "/" + name + ".png");
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 2400)
      qFatal("Blend import demo timeout stage=%d", stage);
    const auto data = state();
    if (ticks % 20 == 0) {
      std::printf("BTS_BLEND_DEMO stage=%d pending=%d frame=%d target=%d listing=%d\n",
                  stage,
                  int(data["composite_pending"].toBool()),
                  data["render_frames"].toInt(),
                  frame,
                  int(listing_ready()));
      std::fflush(stdout);
    }
    if (stage > 3 && (data["texture_loading"].toBool() || data["composite_pending"].toBool() ||
                      data["property_pending"].toBool() || data["render_frames"].toInt() < frame))
      return;
    if ((stage == 3 || stage == 9 || stage == 13) && !listing_ready())
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *resolution = dialog->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(128));
        dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
        get<QPushButton>("chooseBlendLibrary")->click();
        break;
      }
      case 2:
        break;
      case 3: {
        select("collection", {"Character"});
        auto *tree = get<QTreeWidget>("blendLibraryTree");
        for (int r = 0; r < tree->topLevelItemCount(); ++r)
          for (int i = 0; i < tree->topLevelItem(r)->childCount(); ++i) {
            auto *item = tree->topLevelItem(r)->child(i);
            if (item->data(0, Qt::UserRole).toString() == "object" && item->text(0) == "Body")
              item->setSelected(true);
          }
        get<QPushButton>("appendBlendSelection")->click();
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 4:
        capture("collection-and-object");
        get<QToolButton>("addFillLayer")->click();
        break;
      case 5:
        capture("layer-added");
        apply_action("guiSkewSave");
        break;
      case 6:
        if (!QFile::rename(mesh, mesh + ".held"))
          qFatal("Cannot hide own source blend");
        apply_action("guiSkewOpen");
        break;
      case 7:
        if (!QFile::rename(mesh + ".held", mesh))
          qFatal("Cannot restore own source blend");
        capture("opened");
        QTimer::singleShot(0, this, [this] { action("projectConfiguration"); });
        break;
      case 8:
        get<QPushButton>("chooseBlendLibrary")->click();
        break;
      case 9:
        select("object", {"Body"});
        get<QPushButton>("appendBlendSelection")->click();
        busy = true;
        get<QPushButton>("commitReplacementMesh")->click();
        busy = false;
        break;
      case 10:
        capture("reimport-object");
        apply_action("guiSkewSave");
        break;
      case 11:
        apply_action("guiSkewOpen");
        break;
      case 12:
        capture("reimport-opened");
        action("newProject");
        get<QDialog>("newProjectDialog")->findChild<QLineEdit *>("meshPath")->setText(mesh);
        get<QPushButton>("chooseBlendLibrary")->click();
        break;
      case 13:
        select("object", {"Öga"});
        get<QPushButton>("cancelBlendSelection")->click();
        get<QPushButton>("cancelProject")->click();
        break;
      case 14: {
        capture("cancel-preserved");
        timer.stop();
        QFile file(output + "/blend-library.json");
        if (!file.open(QIODevice::WriteOnly))
          qFatal("Cannot write blend import proof");
        file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                             {"captures", captures}})
                       .toJson());
        file.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 20;
  }

 public:
  BlendLibraryDemo(QWidget *window,
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
            qFatal("Blend GUI failure: %s", qPrintable(box->text()));
          if (auto *button = box->button(QMessageBox::Discard))
            button->click();
        }
    });
    dialogs.start(30);
  }
};
}  // namespace bts
