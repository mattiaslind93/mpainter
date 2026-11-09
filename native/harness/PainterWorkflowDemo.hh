// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerRowLayout.hh"
#include "PainterWorkflow.hh"
#include "WorkspaceShell.hh"
#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabWidget>
#include <QTimer>
#include <QWindow>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <functional>
#include <cmath>
#include <map>
#include <vector>
namespace bts {
// Explicit native offscreen proof actor. No CPU material rasterization or
// replacement UI: every capture contains the production workspace and GL FBO.
class PainterWorkflowDemo final : public QObject {
  QWidget *window_, *viewport_;
  QString output_, mesh_, fill_, paint_layer_, folder_, generator_, levels_, filter_, mask_fill_,
      paint_, content_levels_;
  std::map<QString, QString> nested_;
  QString nested_anchor_;
  QJsonArray nested_before_, nested_after_, nested_gpu_checks_, rename_before_, rename_after_,
      rename_checks_;
  bool nested_undo_verified_ = false, nested_persistence_verified_ = false,
       nested_paint_verified_ = false;
  QJsonArray opacity_before_;
  int opacity_undo_ = 0;
  bool opacity_popup_verified_ = false;
  std::function<QJsonObject()> state_;
  QTimer timer_, dialogs_;
  std::vector<std::function<void()>> steps_;
  QJsonArray captures_;
  int step_ = 0, ticks_ = 0, min_frame_ = 0;
  bool busy_ = false;
  template<class T> T *get(const char *name) const
  {
    auto *value = window_->findChild<T *>(name);
    if (!value)
      qFatal("Painter workflow missing %s at step %d", name, step_);
    return value;
  }
  void action(const char *name)
  {
    auto *value = get<QAction>(name);
    if (!value->isEnabled())
      qFatal("Painter action disabled: %s", name);
    value->trigger();
  }
  QListWidget *list() const
  {
    return get<QListWidget>("layerStack");
  }
  static void click(QWidget *widget, QPoint point)
  {
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent event(type,
                        QPointF(point),
                        QPointF(widget->mapToGlobal(point)),
                        Qt::LeftButton,
                        type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(widget, &event);
    }
  }
  QListWidgetItem *row(const QString &layer, const QString &effect = {}) const
  {
    for (int i = 0; i < list()->count(); ++i) {
      auto *item = list()->item(i);
      if (item->data(Qt::UserRole).toString() == layer &&
          item->data(Qt::UserRole + 10).toString() == effect)
        return item;
    }
    qFatal("Painter row missing: %s / %s", qPrintable(layer), qPrintable(effect));
    return nullptr;
  }
  QString selected_effect() const
  {
    auto *item = list()->currentItem();
    if (!item || item->data(Qt::UserRole + 10).toString().isEmpty())
      qFatal("Painter effect not selected");
    return item->data(Qt::UserRole + 10).toString();
  }
  void select_effect(const QString &id)
  {
    auto *item = row(fill_, id);
    list()->scrollToItem(item);
    const auto box = list()->visualItemRect(item);
    const int indent = item->data(Qt::UserRole + 2).toInt() * 20;
    click(list()->viewport(), QPoint(box.left() + indent + 62, box.center().y()));
    if (list()->currentItem() != item)
      qFatal("Painter effect row did not select on click");
  }
  void select_target(const QString &id, bool mask)
  {
    auto *item = row(id);
    list()->scrollToItem(item);
    const auto index = list()->indexFromItem(item);
    const auto box = list()->visualItemRect(item);
    click(list()->viewport(),
          mask ? layer_mask_rect(index, box).center() :
                 QPoint(layer_thumbnail_x(index, box) + 12, box.center().y()));
  }
  void rename_selected(const QString &name)
  {
    auto *view = list();
    view->setFocus();
    QKeyEvent f2(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &f2);
    auto *editor = view->findChild<QLineEdit *>("inlineNameEditor");
    if (!editor || !editor->isVisible() || !editor->hasFocus())
      qFatal(
          "Painter Enter rename editor exists=%d visible=%d focus=%d active=%d current=%s flags=%d",
          bool(editor),
          editor && editor->isVisible(),
          editor && editor->hasFocus(),
          window_->isActiveWindow(),
          qPrintable(view->currentItem() ? view->currentItem()->text() : "null"),
          view->currentItem() ? int(view->currentItem()->flags()) : 0);
    editor->setText(name);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(editor, &enter);
  }
  void rename_focus_out(const QString &name)
  {
    auto *view = list();
    view->setFocus();
    QKeyEvent f2(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
    QCoreApplication::sendEvent(view, &f2);
    QPointer<QLineEdit> editor = view->findChild<QLineEdit *>("inlineNameEditor");
    if (!editor || !editor->isVisible() || !editor->hasFocus()) {
      auto diagnostic = state_();
      diagnostic["editor_exists"] = bool(editor);
      diagnostic["editor_visible"] = editor && editor->isVisible();
      diagnostic["editor_focus"] = editor && editor->hasFocus();
      diagnostic["window_active"] = window_->isActiveWindow();
      diagnostic["active_window"] = QApplication::activeWindow() ?
                                        QApplication::activeWindow()->objectName() :
                                        "null";
      diagnostic["active_popup"] = QApplication::activePopupWidget() ?
                                       QApplication::activePopupWidget()->objectName() :
                                       "null";
      QFile file(output_ + "/focus-failure.json");
      if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(diagnostic).toJson());
      capture_demo_window(window_, viewport_).save(output_ + "/focus-failure.png");
      qFatal(
          "Painter focus-out rename editor exists=%d visible=%d focus=%d active=%d window=%s popup=%s effect=%s",
          bool(editor),
          editor && editor->isVisible(),
          editor && editor->hasFocus(),
          window_->isActiveWindow(),
          qPrintable(diagnostic["active_window"].toString()),
          qPrintable(diagnostic["active_popup"].toString()),
          qPrintable(diagnostic["selected_effect"].toString()));
    }
    editor->setText(name);
    viewport_->setFocus(Qt::OtherFocusReason);
    if (editor && editor->hasFocus())
      qFatal("Painter rename did not transfer actual focus");
  }
  void require_panel(const char *name) const
  {
    if (!get<QWidget>(name)->isVisible())
      qFatal("Painter properties not routed to %s", name);
    if (get<QLabel>("propertiesDockTitle")->text().contains("No properties", Qt::CaseInsensitive))
      qFatal("Painter valid target has no properties");
  }
  void mix_row(const QString &effect, int percent, int mode)
  {
    auto *item = row(fill_, effect);
    list()->scrollToItem(item);
    auto box = list()->visualItemRect(item);
    click(list()->viewport(), layer_effect_opacity_rect(box).center());
    auto *spin = get<QDoubleSpinBox>("layerChannelOpacity");
    if (spin->decimals() != 0 || spin->minimum() != 0 || spin->maximum() != 100)
      qFatal("Painter row opacity is not integer 0..100");
    spin->setValue(percent);
    if (auto *popup = QApplication::activePopupWidget())
      popup->close();
    item = row(fill_, effect);
    box = list()->visualItemRect(item);
    click(list()->viewport(), layer_effect_blend_rect(box).center());
    auto *menu = get<QMenu>("layerBlendMenu");
    auto *value = menu->findChild<QAction *>(QString("layerBlend%1").arg(mode));
    if (!value)
      qFatal("Painter blend action missing");
    value->trigger();
    menu->close();
  }
  void layer_height(int height)
  {
    auto *split = get<QSplitter>("inspectorSplitter");
    auto *handle = split->handle(2);
    const int delta = height - split->widget(1)->height();
    const QPoint start = handle->mapToGlobal(handle->rect().center());
    const auto mouse = [&](QEvent::Type type, QPoint global) {
      QMouseEvent event(type,
                        QPointF(handle->mapFromGlobal(global)),
                        QPointF(global),
                        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(handle, &event);
    };
    mouse(QEvent::MouseButtonPress, start);
    for (int i = 1; i <= 6; ++i)
      mouse(QEvent::MouseMove, start + QPoint(0, delta * i / 6));
    mouse(QEvent::MouseButtonRelease, start + QPoint(0, delta));
  }
  void category(const char *name)
  {
    auto *categories = get<QListWidget>("shelfCategories");
    const auto items = categories->findItems(QString::fromLatin1(name), Qt::MatchExactly);
    if (items.size() != 1)
      qFatal("Painter Shelf category missing %s", name);
    categories->scrollToItem(items.front());
    click(categories->viewport(), categories->visualItemRect(items.front()).center());
    if (categories->currentItem() != items.front())
      qFatal("Painter Shelf category did not select in one click");
  }
  static QString button_background(QAbstractButton *button)
  {
    // Sample only the rendered UI control, never a material or viewport image.
    const auto image = button->grab().toImage().convertToFormat(QImage::Format_RGB32);
    std::map<QRgb, int> colors;
    for (int y = 1; y + 1 < image.height(); ++y)
      for (int x = 1; x + 1 < image.width(); ++x)
        ++colors[image.pixel(x, y)];
    if (colors.empty())
      return {};
    const auto dominant = std::max_element(
        colors.begin(), colors.end(), [](const auto &a, const auto &b) {
          return a.second < b.second;
        });
    return QColor::fromRgb(dominant->first).name();
  }
  static QString icon_hash(const QIcon &icon)
  {
    const auto image = icon.pixmap(24, 24).toImage().convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
      return {};
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()),
            QCryptographicHash::Sha256)
            .toHex());
  }
  void capture(const QString &stage, bool require_thumbnails = true)
  {
    auto data = state_();
    if (data["layer_thumbnails_pending"].toBool())
      qFatal("Painter captured pending thumbnails");
    data["stage"] = stage;
    data["window_width"] = window_->width();
    data["window_height"] = window_->height();
    data["properties_title"] = get<QLabel>("propertiesDockTitle")->text();
    const auto viewport_origin = viewport_->mapTo(window_, QPoint{});
    data["viewport_rect"] = QJsonArray{
        viewport_origin.x(), viewport_origin.y(), viewport_->width(), viewport_->height()};
    QJsonArray channels;
    auto *channel = get<QComboBox>("layerChannelContext");
    for (int i = 0; i < channel->count(); ++i) {
      if (channel->itemData(i).toInt() == 3 || channel->itemText(i) == "Normal")
        qFatal("Painter exposes internal Normal");
      channels.append(
          QJsonObject{{"name", channel->itemText(i)}, {"id", channel->itemData(i).toInt()}});
    }
    data["artist_channels"] = channels;
    QJsonArray rows;
    for (int i = 0; i < list()->count(); ++i) {
      auto *item = list()->item(i);
      const bool effect = !item->data(Qt::UserRole + 10).toString().isEmpty();
      rows.append(QJsonObject{
          {"name", item->text()},
          {"layer", item->data(Qt::UserRole).toString()},
          {"effect", item->data(Qt::UserRole + 10).toString()},
          {"depth", item->data(Qt::UserRole + 2).toInt()},
          {"folder", item->data(Qt::UserRole + 3).toBool()},
          {"mask_effect", effect && item->data(Qt::UserRole + 12).toBool()},
          {"blend", compact_layer_blend(item->data(Qt::UserRole + 5).toInt())},
          {"opacity", compact_layer_opacity(item->data(Qt::UserRole + 1).toDouble())},
          {"glyph", item->data(Qt::UserRole + 23).toInt()},
          {"height", list()->visualItemRect(item).height()},
          {"visible", list()->viewport()->rect().intersects(list()->visualItemRect(item))},
          {"has_mask", item->data(Qt::UserRole + 6).toBool()}});
    }
    data["rows"] = rows;
    data["layer_list_height"] = list()->viewport()->height();
    const auto *material = row(fill_);
    const auto mask_icon = qvariant_cast<QIcon>(material->data(Qt::UserRole + 7));
    if (require_thumbnails && (material->icon().isNull() || mask_icon.isNull()))
      qFatal("Painter native material/mask preview missing");
    data["material_thumbnail_sha256"] = icon_hash(material->icon());
    data["mask_thumbnail_sha256"] = icon_hash(mask_icon);
    QJsonArray visible;
    for (const char *name : {"folderProperties",
                             "fillProjectionMode",
                             "maskFillProperties",
                             "maskGeneratorProperties",
                             "levelsProperties",
                             "filterProperties",
                             "paintEffectMix",
                             "orderedImportedEffectProperties",
                             "maskGroupChildren"})
      if (auto *widget = window_->findChild<QWidget *>(name); widget && widget->isVisible())
        visible.append(name);
    data["visible_properties"] = visible;
    auto *shelf = get<QListWidget>("shelfAssets");
    QJsonArray cards;
    for (int i = 0; i < shelf->count(); ++i)
      cards.append(
          QJsonObject{{"name", shelf->item(i)->text()}, {"tooltip", shelf->item(i)->toolTip()}});
    data["shelf_cards"] = cards;
    data["shelf_category"] = get<QListWidget>("shelfCategories")->currentItem()->text();
    data["shelf_grid"] = QJsonArray{shelf->gridSize().width(), shelf->gridSize().height()};
    QJsonArray toggles;
    for (const char *name : {"fillChannelEnabled0",
                             "fillChannelEnabled1",
                             "fillChannelEnabled3",
                             "fillChannelEnabled4",
                             "fillChannelEnabled14",
                             "brushChannel0",
                             "brushChannel1",
                             "brushChannel3",
                             "brushChannel4",
                             "brushDisplacement"})
      if (auto *button = window_->findChild<QAbstractButton *>(name)) {
        QJsonObject value{
            {"name", name},
            {"text", button->text()},
            {"checked", button->isChecked()},
            {"visible", button->isVisible()},
            {"within_view", button->isVisible() && !button->visibleRegion().isEmpty()}};
        if (button->isVisible())
          value["background"] = button_background(button);
        toggles.append(value);
      }
    data["channel_buttons"] = toggles;
    if (auto *bump = window_->findChild<QWidget *>("fillGradientBump"))
      data["bump_display"] = bump->accessibleName();
    data["bump_value"] = get<QDoubleSpinBox>("materialBump")->value();
    auto *low = get<QCheckBox>("skewUseProjectMesh");
    data["low_poly_text"] = low->text();
    data["low_poly_checked"] = low->isChecked();
    data["low_poly_visible"] = low->isVisible();
    data["low_poly_common_parent"] = low->parentWidget()->objectName();
    const auto image = capture_demo_window(window_, viewport_);
    if (image.isNull() || !image.save(output_ + "/" + stage + ".png"))
      qFatal("Painter capture failed");
    if (auto *baking = window_->findChild<QWidget *>("bakingDock"); baking && baking->isVisible())
      if (!baking->grab().save(output_ + "/" + stage + "-panel.png"))
        qFatal("Painter baking capture failed");
    captures_.append(data);
  }
  QJsonArray mask_tree() const
  {
    return state_()["painter_mask_effects"].toArray();
  }
  static QJsonValue persistent_shape(const QJsonValue &value)
  {
    if (value.isArray()) {
      QJsonArray result;
      for (const auto &entry : value.toArray())
        result.append(persistent_shape(entry));
      return result;
    }
    if (value.isObject()) {
      auto object = value.toObject();
      if (object.contains("immutable_storage"))
        return QJsonObject{{"retained_history", true}};
      object.remove("hashes");
      for (auto it = object.begin(); it != object.end(); ++it)
        it.value() = persistent_shape(it.value());
      return object;
    }
    return value;
  }

  void nested_target(const char *kind, const char * /*panel*/)
  {
    select_target(fill_, true);
    select_effect(nested_.at(QString::fromLatin1(kind)));
    const auto state = state_();
    if (!state["selected_mask"].toBool() ||
        state["selected_effect"].toString() != nested_.at(QString::fromLatin1(kind)))
    {
      auto diagnostic = state;
      diagnostic["expected_effect"] = nested_.at(QString::fromLatin1(kind));
      auto *current = list()->currentItem();
      diagnostic["current_effect"] = current ? current->data(Qt::UserRole + 10).toString() :
                                               "null";
      diagnostic["current_mask"] = current && current->data(Qt::UserRole + 12).toBool();
      diagnostic["current_text"] = current ? current->text() : "null";
      diagnostic["list_size"] = QJsonArray{list()->width(), list()->height()};
      QFile file(output_ + "/nested-target-failure.json");
      if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(diagnostic).toJson());
      capture_demo_window(window_, viewport_).save(output_ + "/nested-target-failure.png");
      qFatal("Painter nested target %s expected=%s actual=%s mask=%d current=%s currentmask=%d",
             kind,
             qPrintable(diagnostic["expected_effect"].toString()),
             qPrintable(state["selected_effect"].toString()),
             state["selected_mask"].toBool(),
             qPrintable(diagnostic["current_effect"].toString()),
             diagnostic["current_mask"].toBool());
    }
  }
  void verify_nested_gpu(const char *stage, bool painted)
  {
    const auto sample = state_()["painter_gpu_mask_probe"].toArray();
    if (sample.size() != 4)
      qFatal("Painter nested native GPU probe unavailable");
    const double levels = .46 * (.57 + .43 * std::pow(.46, 1 / 1.7));
    const double inner = painted ? .48 * levels + .52 * .71 : levels;
    const double expected = .35 + .65 * inner;
    const double actual = sample[0].toDouble();
    if (std::abs(actual - expected) > .002 || sample[3].toDouble() < .999)
      qFatal("Painter nested GPU %s actual %.9g expected %.9g alpha %.9g",
             stage,
             actual,
             expected,
             sample[3].toDouble());
    nested_gpu_checks_.append(
        QJsonObject{{"stage", stage},
                    {"xy", QJsonArray{64, 64}},
                    {"actual", actual},
                    {"expected", expected},
                    {"coverage", sample[3]},
                    {"stroke_operations", state_()["selected_paint_stroke_operations"]},
                    {"tolerance", .002},
                    {"cpu_composite", false}});
  }
  void add_nested_steps()
  {
    steps_.push_back([this] {
      select_target(fill_, true);
      for (int i = 0; i < list()->count(); ++i) {
        auto *item = list()->item(i);
        if (item->data(Qt::UserRole + 13).toInt() == int(MaskEffectKind::Anchor) &&
            !item->data(Qt::UserRole + 10).toString().isEmpty())
          nested_anchor_ = item->data(Qt::UserRole + 10).toString();
        else if (item->text().startsWith("Nested ") && item->text() != "Nested mask group") {
          const auto kind = item->text().mid(7);
          nested_[kind] = item->data(Qt::UserRole + 10).toString();
          if (item->data(Qt::UserRole + 2).toInt() !=
              row(fill_)->data(Qt::UserRole + 2).toInt() + 3)
            qFatal("Painter nested indentation lost");
        }
      }
      if (nested_.size() != 5 || nested_anchor_.isEmpty())
        qFatal("Painter nested effect/Anchor rows missing");
      nested_target("Generator", "maskGeneratorProperties");
    });
    steps_.push_back([this] {
      require_panel("maskGeneratorProperties");
      opacity_before_ = mask_tree();
      opacity_undo_ = state_()["undo_count"].toInt();
      auto *item = row(fill_, nested_.at("Generator"));
      if (std::abs(item->data(Qt::UserRole + 1).toDouble() - 86.4) > .001)
        qFatal("Painter fractional opacity fixture lost");
      list()->scrollToItem(item);
      click(list()->viewport(), layer_effect_opacity_rect(list()->visualItemRect(item)).center());
      auto *spin = get<QDoubleSpinBox>("layerChannelOpacity");
      if (spin->decimals() != 0 || spin->minimum() != 0 || spin->maximum() != 100 ||
          spin->value() != 86 || get<QSlider>("layerChannelOpacitySlider")->maximum() != 1000 ||
          get<QSlider>("layerChannelOpacitySlider")->value() != 864)
        qFatal("Painter integer opacity popup incorrect");
      auto *popup = QApplication::activePopupWidget();
      if (!popup)
        qFatal("Painter opacity popup missing");
      popup->close();
    });
    steps_.push_back([this] {
      if (mask_tree() != opacity_before_ || state_()["undo_count"].toInt() != opacity_undo_)
        qFatal("Painter opening integer opacity popup changed authored fraction");
      opacity_popup_verified_ = true;
    });
    steps_.push_back([this] {
      require_panel("maskGeneratorProperties");
      get<QDoubleSpinBox>("generatorGrungeAmount")->setValue(.42);
      list()->setFocus();
    });
    steps_.push_back([this] { rename_selected("Nested MaskBuilder"); });
    steps_.push_back([this] { mix_row(nested_.at("Generator"), 61, int(LayerBlend::Multiply)); });
    steps_.push_back([this] {
      require_panel("maskGeneratorProperties");
      if (std::abs(get<QDoubleSpinBox>("generatorGrungeAmount")->value() - .42) > .001)
        qFatal("Painter nested Generator edit lost");
      auto *item = row(fill_, nested_.at("Generator"));
      const auto box = list()->visualItemRect(item);
      click(list()->viewport(),
            layer_eye_rect(box, item->data(Qt::UserRole + 2).toInt() * 20).center());
    });
    steps_.push_back([this] {
      if (row(fill_, nested_.at("Generator"))->checkState() != Qt::Unchecked)
        qFatal("Painter nested eye failed");
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      if (row(fill_, nested_.at("Generator"))->checkState() != Qt::Checked)
        qFatal("Painter nested eye Undo failed");
      nested_target("Filter", "filterProperties");
    });
    steps_.push_back([this] {
      require_panel("filterProperties");
      action("filterInvert");
    });
    steps_.push_back([this] {
      require_panel("filterProperties");
      nested_target("Fill", "maskFillProperties");
    });
    steps_.push_back([this] {
      require_panel("maskFillProperties");
      get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.46);
    });
    steps_.push_back([this] {
      require_panel("maskFillProperties");
      if (std::abs(get<QDoubleSpinBox>("maskEffectFillValue")->value() - .46) > .001)
        qFatal("Painter nested Fill edit lost");
      nested_target("Levels", "levelsProperties");
    });
    steps_.push_back([this] {
      require_panel("levelsProperties");
      get<QDoubleSpinBox>("levelsGamma")->setValue(1.7);
      mix_row(nested_.at("Levels"), 43, int(LayerBlend::Multiply));
    });
    steps_.push_back([this] {
      require_panel("levelsProperties");
      if (std::abs(get<QDoubleSpinBox>("levelsGamma")->value() - 1.7) > .001)
        qFatal("Painter nested Levels edit lost");
      if (!get<QWidget>("levelsHistogram")->property("gpuHistogramReady").toBool())
        qFatal("Painter nested Levels GPU histogram unavailable");
      nested_target("Paint", "paintEffectMix");
    });
    steps_.push_back([this] {
      require_panel("paintEffectMix");
      if (!state_()["paint_enabled"].toBool())
        qFatal("Painter nested Paint target disabled");
      nested_before_ = mask_tree();
      get<QDoubleSpinBox>("paintEffectOpacity")->setValue(52);
    });
    steps_.push_back([this] {
      nested_after_ = mask_tree();
      if (nested_before_ == nested_after_)
        qFatal("Painter nested Paint edit did not publish");
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != nested_before_)
        qFatal("Painter nested exact tree Undo failed");
      get<QToolButton>("redoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != nested_after_)
        qFatal("Painter nested exact tree Redo failed");
      nested_undo_verified_ = true;
      rename_before_ = mask_tree();
      list()->setFocus();
    });
    steps_.push_back([this] { rename_focus_out("Nested paint focus out"); });
    steps_.push_back([this] {
      if (row(fill_, nested_.at("Paint"))->text() != "Nested paint focus out")
        qFatal("Painter effect focus-out rename did not commit");
      rename_after_ = mask_tree();
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != rename_before_)
        qFatal("Painter effect focus-out rename Undo failed");
      get<QToolButton>("redoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != rename_after_)
        qFatal("Painter effect focus-out rename Redo failed");
      rename_checks_.append(
          QJsonObject{{"stage", "effect-focus-out-undo-redo"}, {"effect", nested_.at("Paint")}});
      select_effect(nested_anchor_);
    });
    steps_.push_back([this] {
      require_panel("anchorProperties");
      rename_selected("Nested anchor Enter");
    });
    steps_.push_back([this] {
      require_panel("anchorProperties");
      if (row(fill_, nested_anchor_)->text() != "Nested anchor Enter" ||
          get<QLineEdit>("anchorName")->text() != "Nested anchor Enter")
        qFatal("Painter Anchor Enter rename did not route/commit");
      rename_checks_.append(QJsonObject{{"stage", "anchor-enter"}, {"effect", nested_anchor_}});
      rename_before_ = mask_tree();
      list()->setFocus();
    });
    steps_.push_back([this] { rename_focus_out("Nested anchor focus out"); });
    steps_.push_back([this] {
      if (row(fill_, nested_anchor_)->text() != "Nested anchor focus out" ||
          get<QLineEdit>("anchorName")->text() != "Nested anchor focus out")
        qFatal("Painter Anchor focus-out rename did not commit");
      rename_after_ = mask_tree();
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != rename_before_)
        qFatal("Painter Anchor focus-out rename Undo failed");
      get<QToolButton>("redoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != rename_after_)
        qFatal("Painter Anchor focus-out rename Redo failed");
      rename_checks_.append(
          QJsonObject{{"stage", "anchor-focus-out-undo-redo"}, {"effect", nested_anchor_}});
      nested_target("Paint", "paintEffectMix");
    });
    steps_.push_back([this] {
      require_panel("paintEffectMix");
      window_->setProperty("painterGpuReadback", true);
      action("painterWorkflowPaintSetup");
    });
    steps_.push_back([this] {
      require_panel("paintEffectMix");
      verify_nested_gpu("before-dab", false);
      get<QDoubleSpinBox>("alphaHardness")->setValue(1);
      get<QDoubleSpinBox>("brushSize")->setValue(40);
      get<QDoubleSpinBox>("brushFlow")->setValue(100);
      get<QDoubleSpinBox>("brushStrokeOpacity")->setValue(100);
      get<QDoubleSpinBox>("maskGrayscale")->setValue(.71);
      nested_before_ = mask_tree();
      const auto data = state_();
      const auto point = data["painter_paint_point"].toArray();
      const QPoint rounded(qRound(point[0].toDouble()), qRound(point[1].toDouble()));
      if (!viewport_->rect().contains(rounded))
        qFatal("Painter UV paint point outside viewport");
      qWarning(
          "BTS_PAINTER_PAINT_POINT logical=%.9g,%.9g rounded=%d,%d viewport=%dx%d follow_path=%d diameter=%.9g",
          point[0].toDouble(),
          point[1].toDouble(),
          rounded.x(),
          rounded.y(),
          viewport_->width(),
          viewport_->height(),
          get<QToolButton>("brushFollowPath")->isChecked(),
          get<QDoubleSpinBox>("brushSize")->value());
      QMouseEvent hover(QEvent::MouseMove,
                        QPointF(rounded),
                        QPointF(viewport_->mapToGlobal(rounded)),
                        Qt::NoButton,
                        Qt::NoButton,
                        Qt::NoModifier);
      QCoreApplication::sendEvent(viewport_, &hover);
    });
    steps_.push_back([this] {
      const auto data = state_();
      QFile diagnostic(output_ + "/paint-before.json");
      if (diagnostic.open(QIODevice::WriteOnly))
        diagnostic.write(QJsonDocument(data).toJson());
      capture_demo_window(window_, viewport_).save(output_ + "/paint-before.png");
      const auto point = data["painter_paint_point"].toArray();
      click(viewport_, QPoint(qRound(point[0].toDouble()), qRound(point[1].toDouble())));
    });
    steps_.push_back([this] {
      verify_nested_gpu("after-dab", true);
      if (!state_()["selected_paint_has_strokes"].toBool())
        qFatal("Painter nested Paint dab did not retain strokes");
      nested_after_ = mask_tree();
      if (nested_before_ == nested_after_)
        qFatal("Painter nested Paint dab did not publish");
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      verify_nested_gpu("undo-dab", false);
      if (mask_tree() != nested_before_)
        qFatal("Painter nested Paint dab Undo failed");
      get<QToolButton>("redoToolbar")->click();
    });
    steps_.push_back([this] {
      verify_nested_gpu("redo-dab", true);
      if (mask_tree() != nested_after_ || !state_()["selected_paint_has_strokes"].toBool())
        qFatal("Painter nested Paint dab Redo failed");
      nested_paint_verified_ = true;
      action("moveEffectDown");
    });
    steps_.push_back([this] {
      if (mask_tree() == nested_after_)
        qFatal("Painter nested sibling move failed");
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      if (mask_tree() != nested_after_)
        qFatal("Painter nested sibling move Undo failed");
      auto *item = row(fill_, nested_.at("Paint"));
      click(list()->viewport(), layer_effect_delete_rect(list()->visualItemRect(item)).center());
    });
    steps_.push_back([this] {
      for (int i = 0; i < list()->count(); ++i)
        if (list()->item(i)->data(Qt::UserRole + 10).toString() == nested_.at("Paint"))
          qFatal("Painter nested delete failed");
      get<QToolButton>("undoToolbar")->click();
    });
    steps_.push_back([this] {
      select_target(fill_, true);
      if (mask_tree() != nested_after_)
        qFatal("Painter nested delete Undo failed");
      action("painterWorkflowSave");
    });
    steps_.push_back([this] {
      if (!QFileInfo::exists(output_ + "/painter-workflow.blend"))
        qFatal("Painter nested save missing");
      window_->setProperty("painterGpuReadback", false);
      action("painterWorkflowOpen");
    });
    steps_.push_back([this] {
      select_target(fill_, true);
      if (persistent_shape(mask_tree()) != persistent_shape(nested_after_))
        qFatal("Painter nested persistence changed parameters/pages/history presence");
      nested_target("Paint", "paintEffectMix");
      if (!state_()["selected_paint_has_strokes"].toBool())
        qFatal("Painter persisted nested Paint history missing");
      window_->setProperty("painterGpuReadback", true);
      action("painterWorkflowPaintSetup");
    });
    steps_.push_back([this] {
      verify_nested_gpu("reopen-dab", true);
      if (row(fill_, nested_.at("Paint"))->text() != "Nested paint focus out" ||
          row(fill_, nested_anchor_)->text() != "Nested anchor focus out")
        qFatal("Painter effect/Anchor names changed on packed Open");
      rename_checks_.append(QJsonObject{
          {"stage", "packed-open"}, {"effect", nested_.at("Paint")}, {"anchor", nested_anchor_}});
      nested_persistence_verified_ = true;
      window_->setProperty("painterGpuReadback", false);
      action("painterWorkflowPaintFinish");
    });
  }
  void add_capture_steps(int width, int height)
  {
    const auto suffix = QString::number(width);
    steps_.push_back([=, this] {
      window_->resize(width, height);
      if (get<QAction>("toggleConsole")->isChecked())
        action("toggleConsole");
      select_target(fill_, true);
      layer_height(width == 1024 ? 420 : 540);
      list()->scrollToItem(row(folder_), QAbstractItemView::PositionAtTop);
    });
    steps_.push_back([=, this] {
      capture("layer-stack-" + suffix);
      layer_height(width == 1024 ? 210 : 330);
      select_effect(generator_);
      get<QScrollArea>("propertiesScroll")->verticalScrollBar()->setValue(0);
    });
    steps_.push_back([=, this] {
      require_panel("maskGeneratorProperties");
      if (!get<QSlider>("generatorGrungeAmountSlider")->isVisible() ||
          !get<QSlider>("generatorAoInfluenceSlider")->isVisible())
        qFatal("Painter generator sections missing");
      capture("generator-" + suffix);
      select_effect(levels_);
    });
    steps_.push_back([=, this] {
      require_panel("levelsProperties");
      const auto *item = row(fill_, levels_);
      if (qRound(item->data(Qt::UserRole + 1).toDouble()) != 40 ||
          item->data(Qt::UserRole + 5).toInt() != int(LayerBlend::Multiply))
        qFatal("Painter mask Levels mix lost");
      capture("mask-levels-" + suffix);
      select_effect(filter_);
    });
    steps_.push_back([=, this] {
      require_panel("filterProperties");
      capture("filter-" + suffix);
      select_effect(mask_fill_);
    });
    steps_.push_back([=, this] {
      require_panel("maskFillProperties");
      capture("mask-fill-" + suffix);
      select_effect(paint_);
    });
    steps_.push_back([=, this] {
      require_panel("paintEffectMix");
      capture("paint-effect-" + suffix);
      for (int i = 0; i < list()->count(); ++i)
        if (list()->item(i)->text() == "Imported mask group") {
          list()->setCurrentRow(i);
          return;
        }
      qFatal("Painter imported mask Group missing");
    });
    steps_.push_back([=, this] {
      require_panel("orderedImportedEffectProperties");
      require_panel("maskGroupChildren");
      if (get<QTreeWidget>("maskGroupChildren")->topLevelItemCount() != 2)
        qFatal("Painter Group child missing");
      capture("mask-group-" + suffix);
      select_target(fill_, false);
    });
    for (const auto &[kind, panel] :
         std::array<std::pair<const char *, const char *>, 5>{
             {{"Generator", "maskGeneratorProperties"},
              {"Filter", "filterProperties"},
              {"Fill", "maskFillProperties"},
              {"Levels", "levelsProperties"},
              {"Paint", "paintEffectMix"}}})
    {
      steps_.push_back([=, this] { nested_target(kind, panel); });
      steps_.push_back([=, this] {
        require_panel(panel);
        capture("nested-" + QString::fromLatin1(kind).toLower() + "-" + suffix);
      });
    }
    steps_.push_back([this] { select_target(fill_, false); });
    steps_.push_back([this] {
      get<QScrollArea>("propertiesScroll")
          ->ensureWidgetVisible(get<QWidget>("fillChannelToggles"), 0, 12);
    });
    steps_.push_back([=, this] { capture("fill-channels-" + suffix); });
    steps_.push_back([this] {
      const double value = get<QDoubleSpinBox>("materialBump")->value();
      auto *gradient = get<QWidget>("fillGradientBump");
      qWarning("BTS_PAINTER_BUMP value=%.9g cached=%s visible=%d",
               value,
               qPrintable(gradient->accessibleName()),
               gradient->isVisible());
      if (value != .5)
        qFatal("Painter actual Bump neutral %.9g is not 0.5", value);
      get<QScrollArea>("propertiesScroll")->ensureWidgetVisible(gradient, 0, 12);
    });
    steps_.push_back([=, this] {
      require_panel("fillProjectionMode");
      if (get<QCheckBox>("fillChannelEnabled3")->isVisible() ||
          !get<QCheckBox>("fillChannelEnabled4")->isVisible() ||
          !get<QCheckBox>("fillChannelEnabled14")->isVisible())
        qFatal("Painter Bump/Normal/Displacement controls incorrect");
      if (get<QWidget>("fillGradientBump")->accessibleName() != "0.500")
        qFatal("Painter Bump display=%s actual=%.9g",
               qPrintable(get<QWidget>("fillGradientBump")->accessibleName()),
               get<QDoubleSpinBox>("materialBump")->value());
      capture("fill-" + suffix);
      select_effect(content_levels_);
    });
    steps_.push_back([=, this] {
      require_panel("levelsProperties");
      const auto *item = row(fill_, content_levels_);
      if (qRound(item->data(Qt::UserRole + 1).toDouble()) != 37 ||
          item->data(Qt::UserRole + 5).toInt() != int(LayerBlend::Multiply))
        qFatal("Painter content Levels mix lost");
      capture("content-levels-" + suffix);
      for (int i = 0; i < list()->count(); ++i)
        if (list()->item(i)->text() == "Imported color fill") {
          list()->setCurrentRow(i);
          return;
        }
      qFatal("Painter imported content Fill missing");
    });
    steps_.push_back([=, this] {
      require_panel("orderedImportedEffectProperties");
      if (get<QTreeWidget>("maskGroupChildren")->isVisible())
        qFatal("Painter content Fill routed to mask Group");
      capture("content-fill-" + suffix);
      select_target(folder_, false);
    });
    steps_.push_back([=, this] {
      require_panel("folderProperties");
      capture("folder-" + suffix);
      select_target(paint_layer_, false);
      get<QToolButton>("brushChannel1")->setChecked(false);
    });
    steps_.push_back([=, this] {
      if (get<QToolButton>("brushChannel3")->isVisible() ||
          !get<QToolButton>("brushChannel4")->isVisible() ||
          !get<QToolButton>("brushDisplacement")->isVisible())
        qFatal("Painter paint artist channels incorrect");
      capture("paint-layer-" + suffix);
      category("Material presets");
    });
    steps_.push_back([=, this] {
      if (get<QListWidget>("shelfAssets")->count() == 0)
        qFatal("Painter material preset Shelf empty");
      capture("smart-materials-" + suffix);
      category("Mask generators");
    });
    steps_.push_back([=, this] {
      if (get<QListWidget>("shelfAssets")->count() == 0)
        qFatal("Painter mask generator Shelf empty");
      capture("smart-masks-" + suffix);
      action("enterBakingMode");
    });
    steps_.push_back([=, this] {
      auto *low = get<QCheckBox>("skewUseProjectMesh");
      if (!low->isVisible() || !low->isChecked() ||
          low->text() != "Use Low Poly Mesh as High Poly Mesh" ||
          low->parentWidget()->objectName() != "bakingCommonParameters")
        qFatal("Painter Common Parameters source checkbox incorrect");
      capture("baking-" + suffix);
      get<QPushButton>("leaveBakingMode")->click();
    });
  }
  void finish()
  {
    timer_.stop();
    dialogs_.stop();
    QFile file(output_ + "/painter-workflow.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Painter proof unavailable");
    file.write(
        QJsonDocument(
            QJsonObject{
                {"pid", QCoreApplication::applicationPid()},
                {"captures", captures_},
                {"native_framebuffer", true},
                {"typed_import_only_fixture", true},
                {"nested_paint_history_seed",
                 "One empty opt-in StrokeHistory in the typed fixture; ordinary Add Paint defaults unchanged"},
                {"nested_undo_verified", nested_undo_verified_},
                {"nested_persistence_verified", nested_persistence_verified_},
                {"nested_paint_verified", nested_paint_verified_},
                {"fractional_opacity_popup_unchanged", opacity_popup_verified_},
                {"nested_gpu_checks", nested_gpu_checks_},
                {"rename_checks", rename_checks_},
                {"scope",
                 "Production UI and native GPU packets; generated metadata only for import-only Fill/Group kinds"}})
            .toJson());
    file.close();
    QTimer::singleShot(100, qApp, &QCoreApplication::quit);
  }
  void advance()
  {
    if (busy_)
      return;
    if (++ticks_ > 2400)
      qFatal("Painter workflow timeout at step %d", step_);
    if (!window_->isActiveWindow() && !QApplication::activeModalWidget() &&
        !QApplication::activePopupWidget())
    {
      window_->raise();
      window_->activateWindow();
      window_->windowHandle()->requestActivate();
      return;
    }
    viewport_->update();
    const auto state = state_();
    if (!state["shader_failures"].toString().isEmpty())
      qFatal("Painter native shader failure");
    if (step_ > 1 &&
        (state["composite_pending"].toBool() || state["property_pending"].toBool() ||
         state["layer_thumbnails_pending"].toBool() || state["open_pending"].toBool() ||
         state["save_pending"].toBool() || state["render_frames"].toInt() < min_frame_))
      return;
    if (!state["open_error"].toString().isEmpty() || !state["save_error"].toString().isEmpty())
      qFatal("Painter persistence failed");
    if (window_->property("painterGpuReadback").toBool() &&
        state["painter_gpu_mask_probe"].toArray().size() != 4)
      return;
    if (auto *histogram = window_->findChild<QWidget *>("levelsHistogram");
        histogram && histogram->isVisible() && !histogram->property("gpuHistogramReady").toBool())
      return;
    if (step_ == int(steps_.size())) {
      finish();
      return;
    }
    busy_ = true;
    steps_[step_++]();
    busy_ = false;
    min_frame_ = state_()["render_frames"].toInt() + 2;
  }

 public:
  PainterWorkflowDemo(QWidget *window,
                      QWidget *viewport,
                      QString output,
                      QString mesh,
                      std::function<QJsonObject()> state)
      : QObject(window),
        window_(window),
        viewport_(viewport),
        output_(std::move(output)),
        mesh_(std::move(mesh)),
        state_(std::move(state))
  {
    const bool headless = QGuiApplication::platformName() == "offscreen" &&
                          qEnvironmentVariable("BTS_HEADLESS_WORKSPACE") == "1";
    bool owned_xcb = false;
    const auto display = qEnvironmentVariable("DISPLAY");
    bool valid_display = false;
    const int number = display.startsWith(':') ? display.mid(1).toInt(&valid_display) : 0;
    const auto owned_pid = qEnvironmentVariable("BTS_PRIVATE_XVFB_PID").toLongLong();
    if (QGuiApplication::platformName() == "xcb" && valid_display && number >= 90 && owned_pid > 0)
    {
      QFile lock(QString("/tmp/.X%1-lock").arg(number));
      owned_xcb = lock.open(QIODevice::ReadOnly) &&
                  lock.readAll().trimmed().toLongLong() == owned_pid;
    }
    if (!headless && !owned_xcb)
      qFatal("Painter actor requires Qt offscreen or its verified owned Xvfb");
    QDir().mkpath(output_);
    auto *shell = static_cast<WorkspaceShell *>(window_);
    shell->reset_reference_layout();
    if (get<QAction>("toggleConsole")->isChecked())
      action("toggleConsole");
    shell->set_shelf_visible(true);
    steps_.push_back([this] {
      window_->raise();
      window_->activateWindow();
      window_->windowHandle()->requestActivate();
    });
    steps_.push_back([this] {
      window_->resize(1024, 768);
      action("newProject");
    });
    steps_.push_back([this] {
      get<QComboBox>("projectTemplate")->setCurrentIndex(0);
      get<QLineEdit>("meshPath")->setText(mesh_);
      auto *size = get<QComboBox>("documentResolution");
      size->setCurrentIndex(size->findData(128));
      get<QPushButton>("createProject")->click();
    });
    steps_.push_back([this] {
      paint_layer_ = list()->currentItem()->data(Qt::UserRole).toString();
      get<QCheckBox>("displacementChannel")->setChecked(true);
      get<QToolButton>("addFillLayer")->click();
    });
    steps_.push_back([this] {
      get<QCheckBox>("fillChannelEnabled1")->setChecked(false);
      fill_ = list()->currentItem()->data(Qt::UserRole).toString();
      list()->setFocus();
    });
    steps_.push_back([this] { rename_selected("Weathered painted steel"); });
    steps_.push_back([this] {
      list()->clearSelection();
      for (int i = 0; i < list()->count(); ++i)
        list()->item(i)->setSelected(true);
      action("groupLayers");
      if (!list()->currentItem()->data(Qt::UserRole + 3).toBool())
        qFatal("Painter folder not selected");
      folder_ = list()->currentItem()->data(Qt::UserRole).toString();
      list()->setFocus();
    });
    steps_.push_back([this] { rename_selected("Surface treatment"); });
    steps_.push_back([this] {
      select_target(fill_, false);
      action("addWhiteMask");
      action("addMaskGenerator");
      generator_ = selected_effect();
      action("generatorNativeGrunge");
    });
    steps_.push_back([this] {
      get<QDoubleSpinBox>("generatorGrungeAmount")->setValue(.63);
      get<QDoubleSpinBox>("generatorGrungeScale")->setValue(5);
      get<QDoubleSpinBox>("generatorContrast")->setValue(1.6);
      action("addLevelsEffect");
      levels_ = selected_effect();
    });
    steps_.push_back([this] {
      get<QDoubleSpinBox>("levelsGamma")->setValue(1.4);
      mix_row(levels_, 40, int(LayerBlend::Multiply));
      action("addFilterEffect");
      filter_ = selected_effect();
      action("filterInvert");
    });
    steps_.push_back([this] {
      action("addMaskFillEffect");
      mask_fill_ = selected_effect();
      get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.75);
      get<QDoubleSpinBox>("maskEffectFillOpacity")->setValue(28);
      action("addPaintEffect");
      paint_ = selected_effect();
    });
    steps_.push_back([this] {
      select_target(fill_, false);
      action("addLevelsEffect");
      content_levels_ = selected_effect();
    });
    steps_.push_back([this] {
      get<QDoubleSpinBox>("levelsGamma")->setValue(1.2);
      mix_row(content_levels_, 37, int(LayerBlend::Multiply));
      action("painterWorkflowImportedKinds");
    });
    add_nested_steps();
    add_capture_steps(1024, 768);
    add_capture_steps(1920, 1080);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start(100);
    connect(&dialogs_, &QTimer::timeout, this, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
          qFatal("Painter actor dialog: %s", qPrintable(box->text()));
    });
    dialogs_.start(40);
  }
};
}  // namespace bts
