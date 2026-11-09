// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuiltinPolyHaven.hh"
#include "NativeMaterialBundle.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
namespace bts {
// One-material detail proof uses the real 1K New default. The separate mapping
// stress sequence stays at 128px so four retained materials do not inflate RAM.
class PolyHavenDemo final : public QObject {
  QWidget *w;
  QString out;
  std::function<QJsonObject()> state;
  QTimer timer, dialogs;
  QJsonArray checks;
  const QString detail = qEnvironmentVariable("BTS_POLYHAVEN_DETAIL");
  int stage = 0, ticks = 0, frame = 0;
  bool busy = false;
  template<class T> T *get(const char *name)
  {
    auto *x = w->findChild<T *>(name);
    if (!x)
      qFatal("Poly Haven missing control: %s", name);
    return x;
  }
  static void require(bool ok, const char *why)
  {
    if (!ok)
      qFatal("Poly Haven: %s", why);
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void apply(const QString &name)
  {
    auto *list = get<QListWidget>("shelfAssets");
    const auto found = list->findItems(name, Qt::MatchExactly);
    require(!found.empty(), "Preset absent from Materials");
    list->setCurrentItem(found.front());
    const auto current = list->findItems(name, Qt::MatchExactly);
    require(!current.empty(), "Preset disappeared after selection");
    Q_EMIT list->itemDoubleClicked(current.front());
  }
  QJsonObject snapshot(const char *label)
  {
    w->setProperty("bjReadGpu", true);
    action("guiBjSnapshot");
    w->setProperty("bjReadGpu", false);
    auto proof = w->property("bjState").toJsonObject();
    require(proof["native"].toBool(), "Preset did not create native Fill");
    require(!proof["gpu"].toArray().empty(), "No GPU samples");
    proof["stage"] = label;
    checks.append(proof);
    QFile receipt(out + "/mapping-checks.json");
    require(receipt.open(QIODevice::WriteOnly), "Cannot save mapping checks");
    receipt.write(QJsonDocument(checks).toJson());
    return proof;
  }
  void advance()
  {
    if (busy)
      return;
    busy = true;
    require(++ticks < 2400, "Timed out");
    auto s = state();
    if (stage >= 12 && ticks % 20 == 0) {
      action("guiBjSnapshot");
      QFile diagnostic(out + "/pending-open.json");
      if (diagnostic.open(QIODevice::WriteOnly))
        diagnostic.write(
            QJsonDocument(
                QJsonObject{{"state", s}, {"snapshot", w->property("bjState").toJsonObject()}})
                .toJson());
      w->screen()->grabWindow(w->winId()).save(out + "/pending-open.png");
    }
    if (stage > 1 && (s["composite_pending"].toBool() || s["texture_loading"].toBool() ||
                      s["native_source_pending"].toBool() || s["open_pending"].toBool() ||
                      s["save_pending"].toBool() || s["render_frames"].toInt() < frame))
    {
      busy = false;
      return;
    }
    require(s["shader_failures"].toString().isEmpty(), "Shader compilation failure");
    require(s["material_error"].toString().isEmpty(), "Material load failed");
    require(s["open_error"].toString().isEmpty() && s["save_error"].toString().isEmpty(),
            "Project I/O failed");
    qInfo("POLYHAVEN_STAGE %d", stage);
    if (!detail.isEmpty()) {
      switch (stage++) {
        case 0:
          action("newProject");
          break;
        case 1:
          get<QPushButton>("useShaderballMesh")->click();
          require(get<QComboBox>("documentResolution")->currentData().toInt() == 1024,
                  "New project must default to bundled 1K detail");
          get<QPushButton>("createProject")->click();
          break;
        case 2: {
          auto *shelf = get<QAction>("toggleShelf");
          if (!shelf->isChecked())
            shelf->trigger();
          auto *categories = get<QListWidget>("shelfCategories");
          auto found = categories->findItems("Materials", Qt::MatchExactly);
          require(found.size() == 1, "Materials category missing");
          categories->setCurrentItem(found.front());
          get<QLineEdit>("shelfSearch")->setText("polyhaven");
          require(get<QListWidget>("shelfAssets")->count() == 47, "Materials shelf count differs");
          apply(detail);
          break;
        }
        case 3: {
          auto proof = snapshot("detail-uv");
          require(proof["dimensions"].toArray() == QJsonArray{QJsonArray{1001, 1024, 1024}},
                  "Viewport composite is not 1K");
          require(w->screen()->grabWindow(w->winId()).save(out + "/uv.png"),
                  "UV screenshot failed");
          get<QComboBox>("fillProjectionMode")->setCurrentIndex(1);
          break;
        }
        case 4:
          snapshot("detail-triplanar");
          require(w->screen()->grabWindow(w->winId()).save(out + "/triplanar.png"),
                  "Triplanar screenshot failed");
          get<QComboBox>("fillProjectionMode")->setCurrentIndex(0);
          break;
        case 5:
          w->setProperty("bjProject", out + "/starter.blend");
          action("guiBjSave");
          break;
        case 6:
          action("guiBjOpen");
          break;
        case 7:
          action("guiBjValidateOpen");
          get<QComboBox>("shelfLibraryScope")->setCurrentIndex(1);
          require(get<QListWidget>("shelfAssets")->count() == 47,
                  "Built-in shelf changed after reopen");
          break;
        case 8: {
          auto proof = snapshot("detail-reopened");
          require(proof["dimensions"].toArray() == QJsonArray{QJsonArray{1001, 1024, 1024}},
                  "Reopen lost 1K resolution");
          require(w->screen()->grabWindow(w->winId()).save(out + "/workspace.png"),
                  "Screenshot failed");
          QFile file(out + "/validation.json");
          require(file.open(QIODevice::WriteOnly), "Cannot save receipt");
          file.write(QJsonDocument(
                         QJsonObject{{"material_count", 47},
                                     {"material", detail},
                                     {"resolution", 1024},
                                     {"default_resolution", true},
                                     {"checks", checks},
                                     {"packed_open", w->property("bjOpenProof").toJsonObject()}})
                         .toJson());
          timer.stop();
          dialogs.stop();
          QTimer::singleShot(100, qApp, &QCoreApplication::quit);
          break;
        }
      }
      frame = state()["render_frames"].toInt() + 16;
      busy = false;
      return;
    }
    switch (stage++) {
      case 0: {
        const auto &materials = builtin_polyhaven_materials();
        require(materials.size() == 47, "Expected 47 built-in presets");
        for (const auto &m : materials)
          require(!native_material_bundle_preview(m.bytes).isNull(), "Missing saved preview");
        action("newProject");
        break;
      }
      case 1:
        get<QPushButton>("useShaderballMesh")->click();
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        get<QPushButton>("createProject")->click();
        break;
      case 2: {
        require(s["has_document"].toBool(), "Missing JL01 project");
        auto *shelf = get<QAction>("toggleShelf");
        if (!shelf->isChecked())
          shelf->trigger();
        auto *categories = get<QListWidget>("shelfCategories");
        auto found = categories->findItems("Materials", Qt::MatchExactly);
        require(found.size() == 1, "Materials category missing");
        categories->setCurrentItem(found.front());
        get<QLineEdit>("shelfSearch")->setText("polyhaven");
        require(get<QListWidget>("shelfAssets")->count() == 47, "Materials shelf count differs");
        apply("Gold");
        break;
      }
      case 3:
        snapshot("gold-uv");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 4:
        snapshot("gold-triplanar");
        apply("Denim");
        break;
      case 5:
        snapshot("denim-uv");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 6:
        snapshot("denim-triplanar");
        apply("Carbon Fibre");
        break;
      case 7:
        snapshot("carbon-uv");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 8:
        snapshot("carbon-triplanar");
        apply("Cherry Wood");
        break;
      case 9:
        snapshot("cherry-uv");
        get<QComboBox>("fillProjectionMode")->setCurrentIndex(1);
        break;
      case 10:
        snapshot("cherry-triplanar");
        w->setProperty("bjProject", out + "/starter.blend");
        action("guiBjSave");
        break;
      case 11:
        require(QFileInfo(out + "/starter.blend").size() > 0, "Project not saved");
        action("guiBjOpen");
        break;
      case 12:
        action("guiBjValidateOpen");
        get<QComboBox>("shelfLibraryScope")->setCurrentIndex(1);
        require(get<QListWidget>("shelfAssets")->count() == 47,
                "Built-in shelf changed after reopen");
        break;
      case 13: {
        require(w->screen()->grabWindow(w->winId()).save(out + "/workspace.png"),
                "Screenshot failed");
        QFile file(out + "/validation.json");
        require(file.open(QIODevice::WriteOnly), "Cannot save receipt");
        file.write(
            QJsonDocument(QJsonObject{{"material_count", 47},
                                      {"resolution", 128},
                                      {"checks", checks},
                                      {"packed_open", w->property("bjOpenProof").toJsonObject()}})
                .toJson());
        timer.stop();
        dialogs.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame = state()["render_frames"].toInt() + 16;
    busy = false;
  }

 public:
  PolyHavenDemo(QWidget *window, QString directory, std::function<QJsonObject()> get_state)
      : QObject(window), w(window), out(directory), state(std::move(get_state))
  {
    QDir().mkpath(out);
    timer.setInterval(150);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
    dialogs.setInterval(50);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *box : w->findChildren<QMessageBox *>())
        if (box->isVisible()) {
          if (box->standardButtons() & QMessageBox::Discard)
            box->button(QMessageBox::Discard)->click();
          else
            qFatal("Poly Haven dialog: %s", qPrintable(box->text()));
        }
    });
    dialogs.start();
  }
};
}  // namespace bts
