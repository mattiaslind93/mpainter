#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTimer>
#include <QToolButton>
#include <cmath>
namespace bts {
class TextureSetSelectionDemo final : public QObject {
  QWidget *window_, *viewport_;
  QString directory_, mesh_;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  int stage_ = 0, ticks_ = 0;
  bool busy_ = false;
  QJsonArray captures_;
  template<class T> T *get(const char *name)
  {
    auto *result = window_->findChild<T *>(name);
    if (!result)
      qFatal("Selection control missing: %s", name);
    return result;
  }
  void click(int row)
  {
    auto *list = get<QListWidget>("textureSets");
    list->scrollToItem(list->item(row));
    auto *field = list->itemWidget(list->item(row))->findChild<QLineEdit *>("textureSetName");
    const QPointF point = field->rect().center();
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent event(type,
                        point,
                        field->mapToGlobal(point.toPoint()),
                        Qt::LeftButton,
                        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(field, &event);
    }
  }
  void check(int row)
  {
    const auto state = state_();
    auto *list = get<QListWidget>("textureSets");
    const auto uid = list->item(row)->data(Qt::UserRole).toUInt();
    const auto expected = state["sets"].toArray().at(row).toObject();
    auto *layers = get<QListWidget>("layerStack");
    if (list->currentRow() != row || state["selected_index"].toInt() != row ||
        state["native_selected_uid"].toDouble() != uid ||
        state["view"].toObject()["selected_material_session_id"].toDouble() != uid)
      qFatal("Texture Set selection did not reach native/view row %d", row);
    if (layers->count() != row + 1 ||
        layers->currentItem()->data(Qt::UserRole).toString() != expected["layer_id"].toString())
      qFatal("Layers panel stayed on previous Texture Set %d", row);
    if (get<QComboBox>("textureSetResolution")->currentText().toInt() != (128 << row))
      qFatal("Texture Set Settings stayed on previous set %d", row);
    if (!get<QWidget>("fillMaterialSphere")->isVisible() ||
        std::abs(get<QDoubleSpinBox>("roughnessControl")->value() - .2 * (row + 1)) > .001)
      qFatal("Properties Material stayed on previous set %d", row);
    if (!get<QDialog>("shaderSettings")->isVisible() ||
        get<QLineEdit>("shaderInstanceName")->text() != QString("Selection shader %1").arg(row) ||
        std::abs(get<QDoubleSpinBox>("shaderNormalIntensity")->value() - .5 * (row + 1)) > .001)
      qFatal("Shader settings stayed on previous set %d", row);
    if (!state["paint_target"].toString().startsWith(QString::number(uid)))
      qFatal("Viewport painting target stayed on previous set %d", row);
    QJsonObject proof = state;
    proof["row"] = row;
    proof["layers"] = layers->count();
    proof["resolution"] = get<QComboBox>("textureSetResolution")->currentText();
    proof["roughness"] = get<QDoubleSpinBox>("roughnessControl")->value();
    proof["shader"] = get<QLineEdit>("shaderInstanceName")->text();
    captures_.append(proof);
  }
  void advance()
  {
    if (busy_)
      return;
    const QScopedValueRollback busy(busy_, true);
    if (++ticks_ > 1200)
      qFatal("Texture Set selection demo timeout at %d", stage_);
    if (stage_ > 1) {
      const auto state = state_();
      if (state["composite_pending"].toBool() || state["texture_loading"].toBool())
        return;
    }
    static constexpr int sequence[]{2, 0, 1, 2, 1, 0};
    switch (stage_++) {
      case 0:
        get<QAction>("newProject")->trigger();
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh_);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        get<QPushButton>("createProject")->click();
        break;
      case 2:
        get<QAction>("guiTextureSetSelectionPrepare")->trigger();
        get<QToolButton>("fillMaterialTab")->click();
        get<QToolButton>("shaderSettingsButton")->click();
        break;
      default: {
        const int step = stage_ - 4;
        if (step > 0)
          check(sequence[step - 1]);
        if (step < 6)
          click(sequence[step]);
        else {
          timer_.stop();
          QFile file(directory_ + "/selection.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("Texture Set selection proof unavailable");
          file.write(QJsonDocument(QJsonObject{{"captures", captures_}}).toJson());
          capture_demo_window(window_, viewport_).save(directory_ + "/selection.png");
          get<QDialog>("shaderSettings")->grab().save(directory_ + "/shader.png");
          QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        }
        break;
      }
    }
  }

 public:
  TextureSetSelectionDemo(QWidget *window,
                          QWidget *viewport,
                          QString directory,
                          QString mesh,
                          std::function<QJsonObject()> state)
      : QObject(window),
        window_(window),
        viewport_(viewport),
        directory_(std::move(directory)),
        mesh_(std::move(mesh)),
        state_(std::move(state))
  {
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start(120);
  }
};
}  // namespace bts
