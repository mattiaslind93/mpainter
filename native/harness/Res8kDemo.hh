// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTimer>
#include <functional>
#include <stdexcept>

namespace bts {
// This diagnostic only runs under the private Xvfb launched by its host script.
// Capture the real X server pixels so separate Qt popup windows are included.
class Res8kDemo final : public QObject {
  QWidget *window_;
  QString directory_, mesh_;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  int stage_ = 0, ticks_ = 0;
  QJsonArray evidence_;
  QStringList plain_items_, dynamic_items_;

  static void require(bool okay, const char *message)
  {
    if (!okay)
      throw std::runtime_error(message);
  }
  template<class T> T *get(const char *name)
  {
    auto *result = window_->findChild<T *>(name);
    require(result, name);
    return result;
  }
  void action(const char *name)
  {
    auto *a = get<QAction>(name);
    require(a->isEnabled(), name);
    a->trigger();
  }
  void capture(const QString &name)
  {
    require(qApp->platformName() == "xcb" && qEnvironmentVariableIsSet("BTS_PRIVATE_XVFB_PID"),
            "Screenshot requires the private Xvfb server");
    require(window_->screen()->grabWindow(0).save(directory_ + "/" + name + ".png"),
            "Screenshot write failed");
  }
  void popup(const char *name)
  {
    auto *combo = get<QComboBox>(name);
    for (auto *parent = combo->parentWidget(); parent; parent = parent->parentWidget())
      if (auto *scroll = qobject_cast<QScrollArea *>(parent))
        scroll->ensureWidgetVisible(combo);
    require(combo->isVisible() && combo->isEnabled(), "Dropdown is not visible and enabled");
    combo->showPopup();
  }
  void capture_popup(const char *name, const char *path, bool resolution = false)
  {
    auto *combo = get<QComboBox>(name);
    require(combo->view()->isVisible(), "Dropdown did not open");
    QJsonArray items;
    bool has_8k = false;
    for (int row = 0; row < combo->count(); ++row) {
      const auto index = combo->model()->index(row, combo->modelColumn());
      require(!index.data(Qt::DisplayRole).toString().isEmpty(), "Dropdown has empty text");
      require(!index.data(Qt::CheckStateRole).isValid(),
              "Ordinary dropdown item has an unexpected checkbox");
      if (resolution) {
        const int value = combo->itemData(row).toInt();
        require(value > 0 && value <= 8192, "Resolution dropdown exceeds 8192");
        has_8k |= value == 8192;
      }
      items.append(combo->itemText(row));
    }
    if (resolution)
      require(has_8k, "Resolution dropdown omits 8192");
    capture(path);
    evidence_.append(QJsonObject{{"control", name},
                                 {"items", items},
                                 {"popup_visible", true},
                                 {"check_state_absent", true}});
    combo->hidePopup();
  }
  void category(const char *name)
  {
    auto *categories = get<QListWidget>("shelfCategories");
    auto items = categories->findItems(name, Qt::MatchExactly);
    require(!items.empty(), "Shelf category missing");
    categories->setCurrentItem(items.front());
    get<QLineEdit>("shelfSearch")->clear();
  }
  QStringList shelf_names()
  {
    QStringList names;
    auto *list = get<QListWidget>("shelfAssets");
    for (int row = 0; row < list->count(); ++row)
      names.append(list->item(row)->text());
    return names;
  }
  void advance()
  {
    try {
      require(++ticks_ < 1200, "Resolution/shelf proof timed out");
      const auto state = state_();
      if (stage_ > 3 &&
          (state["texture_loading"].toBool() || state["composite_pending"].toBool() ||
           state["property_pending"].toBool()))
        return;
      qInfo("RES8K_STAGE %d", stage_);
      switch (stage_++) {
        case 0:
          action("newProject");
          break;
        case 1:
          get<QComboBox>("projectTemplate")->setCurrentIndex(0);
          get<QLineEdit>("meshPath")->setText(mesh_);
          get<QComboBox>("documentResolution")
              ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
          popup("documentResolution");
          break;
        case 2:
          capture("res8k");
          capture_popup("documentResolution", "dropdowns/new-project", true);
          break;
        case 3:
          get<QPushButton>("createProject")->click();
          break;
        case 4:
          action("guiRes8kPrepareShelf");
          break;
        case 5:
          get<QTabWidget>("layerSettingsTabs")->setCurrentIndex(1);
          break;
        case 6:
          popup("textureSetResolution");
          break;
        case 7:
          capture_popup("textureSetResolution", "dropdowns/texture-set", true);
          break;
        case 8:
          QTimer::singleShot(0, this, [this] { action("bakeMeshMapsAction"); });
          break;
        case 9:
          popup("bakeResolution");
          break;
        case 10:
          capture_popup("bakeResolution", "dropdowns/bake-settings", true);
          get<QDialog>("meshBakeDialog")->reject();
          break;
        case 11:
          get<QTabWidget>("layerSettingsTabs")->setCurrentIndex(0);
          action("editLayerBlend");
          break;
        case 12: {
          auto *menu = get<QMenu>("layerBlendMenu");
          require(menu->isVisible(), "Blend menu did not open");
          capture("dropdowns/blend-mode");
          menu->close();
          break;
        }
        case 13:
          action("guiRes8kGenerator");
          break;
        case 14:
          popup("generatorCurve");
          break;
        case 15:
          capture_popup("generatorCurve", "dropdowns/mask-generator");
          break;
        case 16:
          category("Materials");
          break;
        case 17:
          plain_items_ = shelf_names();
          require(plain_items_.contains("Plain finish — saved folder"),
                  "Plain folder absent from Materials");
          require(!plain_items_.contains("World height — saved folder"),
                  "Dynamic preset leaked into Materials");
          capture("shelf-materials");
          break;
        case 18:
          category("Material presets");
          break;
        case 19:
          dynamic_items_ = shelf_names();
          require(dynamic_items_.contains("World height — saved folder"),
                  "Dynamic folder absent from Material presets");
          for (const auto &name : dynamic_items_)
            require(!plain_items_.contains(name), "Material shelves overlap");
          capture("shelf-material-presets");
          break;
        case 20:
          popup("shelfLibraryScope");
          break;
        case 21:
          capture_popup("shelfLibraryScope", "dropdowns/assets-filters");
          break;
        case 22: {
          QFile file(directory_ + "/res8k-proof.json");
          require(file.open(QIODevice::WriteOnly), "Proof write failed");
          file.write(
              QJsonDocument(
                  QJsonObject{{"passed", true},
                              {"dropdowns", evidence_},
                              {"materials", QJsonArray::fromStringList(plain_items_)},
                              {"material_presets", QJsonArray::fromStringList(dynamic_items_)},
                              {"fixture_resolution", 128}})
                  .toJson());
          timer_.stop();
          qInfo("RES8K_PROOF_PASS");
          QTimer::singleShot(0, qApp, &QCoreApplication::quit);
          break;
        }
      }
    }
    catch (const std::exception &error) {
      qCritical("RES8K_PROOF_FAILED stage=%d: %s", stage_, error.what());
      timer_.stop();
      QCoreApplication::exit(1);
    }
  }

 public:
  Res8kDemo(QWidget *window, QString directory, QString mesh, std::function<QJsonObject()> state)
      : QObject(window),
        window_(window),
        directory_(std::move(directory)),
        mesh_(std::move(mesh)),
        state_(std::move(state))
  {
    QDir().mkpath(directory_ + "/dropdowns");
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start(500);
  }
};
}  // namespace bts
