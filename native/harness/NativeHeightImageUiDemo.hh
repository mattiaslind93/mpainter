// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include "ChannelPrecision.hh"
#include "../tests/NativeHeightImageReference.hh"
#include <QtWidgets>
#include <functional>
#include <memory>
#include <cmath>
#include <cstdio>
namespace bts {
// Real workspace controls. Native data-first/fresh cold is a separate actor.
class NativeHeightImageUiDemo final : public QObject {
  QWidget *w_, *v_;
  QString out_;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  int step_ = 0, frame_ = 0, ticks_ = 0;
  bool busy_ = false, gpu_wait_ = false;
  QUuid set_, layer_;
  QJsonObject input_, last_, before_, enabled_, saved_, picker_activation_, picker_diagnostic_,
      policy_diagnostic_;
  QJsonArray checks_, captures_, picker_events_, policy_events_;
  [[noreturn]] void fail(const QString &message)
  {
    w_->setProperty("awHeightReadGpu", false);
    QString refresh_error;
    try {
      if (state_)
        last_ = state_();
    }
    catch (const std::exception &error) {
      refresh_error = QString::fromUtf8(error.what());
    }
    catch (...) {
      refresh_error = "Unknown metadata refresh error";
    }
    QFile file(out_ + "/failure.json");
    if (file.open(QIODevice::WriteOnly))
      file.write(QJsonDocument(QJsonObject{{"error", message},
                                           {"step", step_},
                                           {"state", last_},
                                           {"state_refresh_error", refresh_error},
                                           {"before", before_},
                                           {"enabled", enabled_},
                                           {"checks", checks_},
                                           {"captures", captures_},
                                           {"picker", picker_diagnostic_},
                                           {"pickers", picker_events_},
                                           {"policy", policy_diagnostic_},
                                           {"policies", policy_events_}})
                     .toJson());
    qFatal("AW Height UI step%d: %s", step_, qPrintable(message));
  }
  void require(bool okay, const QString &message)
  {
    if (!okay)
      fail(message);
  }
  template<class T> T *get(const char *name)
  {
    auto *result = w_->findChild<T *>(name);
    require(result, QString("Missing actual control ") + name);
    return result;
  }
  void action(const char *name)
  {
    auto *a = get<QAction>(name);
    require(a->isEnabled(), QString("Disabled actual action ") + name);
    a->trigger();
  }
  static void key(QWidget *target,
                  int value,
                  bool down = true,
                  Qt::KeyboardModifiers mods = Qt::NoModifier,
                  const QString &text = {})
  {
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease, value, mods, text);
    QApplication::sendEvent(target, &event);
  }
  static void mouse(QWidget *target,
                    QEvent::Type kind,
                    QPointF point,
                    Qt::MouseButton button,
                    Qt::MouseButtons buttons)
  {
    QMouseEvent event(
        kind, point, target->mapToGlobal(point.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
  }
  static void click(QWidget *target, QPoint point)
  {
    mouse(target, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(target, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  }
  void reveal(QWidget *widget)
  {
    for (auto *p = widget->parentWidget(); p; p = p->parentWidget())
      if (auto *s = qobject_cast<QScrollArea *>(p))
        s->ensureWidgetVisible(widget);
    require(widget->isVisible() && widget->isEnabled(),
            "Actual control is not visible/enabled: " + widget->objectName());
  }
  void select()
  {
    auto *list = get<QListWidget>("layerStack");
    QListWidgetItem *item = nullptr;
    for (int i = 0; i < list->count(); ++i) {
      auto *row = list->item(i);
      if (QUuid(row->data(Qt::UserRole).toString()) == layer_ &&
          QUuid(row->data(Qt::UserRole + 10).toString()).isNull())
      {
        item = row;
        break;
      }
    }
    require(item, "Persistent actual Fill row missing");
    list->scrollToItem(item);
    const auto point = list->visualItemRect(item).center();
    click(list->viewport(), point);
  }
  void policy(int value)
  {
    auto *box = get<QComboBox>("nativeHeightPolicy");
    reveal(box);
    QPointer<QComboBox> watched(box);
    int activated_count = 0;
    QJsonArray activated_indices;
    policy_diagnostic_ = QJsonObject{{"requested_policy", value},
                                     {"current_index_before", box->currentIndex()},
                                     {"activated_count", 0},
                                     {"activated_indices", activated_indices}};
    const auto observation = connect(box, &QComboBox::activated, this, [&](int index) {
      ++activated_count;
      activated_indices.append(index);
      policy_diagnostic_["activated_count"] = activated_count;
      policy_diagnostic_["activated_indices"] = activated_indices;
    });
    click(box, box->rect().center());
    auto *view = box->view();
    const auto row = box->findData(value);
    require(row >= 0 && view->isVisible(), "Actual Height policy popup absent");
    const auto index = box->model()->index(row, 0);
    view->scrollTo(index);
    const auto point = view->visualRect(index).center();
    policy_diagnostic_["requested_index"] = row;
    policy_diagnostic_["popup_visible_before_row"] = view->isVisible();
    // Real pointer movement selects the hovered row before its press/release.
    mouse(view->viewport(), QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
    click(view->viewport(), point);
    disconnect(observation);
    policy_diagnostic_["combo_survived"] = bool(watched);
    policy_diagnostic_["current_index_after"] = watched ? watched->currentIndex() : -1;
    policy_diagnostic_["activated_count"] = activated_count;
    policy_diagnostic_["activated_indices"] = activated_indices;
    w_->setProperty("awHeightReadGpu", false);
    last_ = state_();
    policy_diagnostic_["post_state"] = last_;
    policy_diagnostic_["height_error"] = last_["height_error"];
    policy_events_.append(policy_diagnostic_);
    require(activated_count == 1 && activated_indices == QJsonArray{row},
            "Actual Height policy gesture did not activate exactly the requested row");
  }
  void bump_view()
  {
    v_->setFocus(Qt::OtherFocusReason);
    key(v_, Qt::Key_M);
    key(v_, Qt::Key_M, false);
    for (int i = 0; i < 8 && state_()["inspection"].toInt(-1) != 4; ++i) {
      key(v_, Qt::Key_C);
      key(v_, Qt::Key_C, false);
    }
    require(state_()["inspection"].toInt(-1) == 4,
            "Real viewport key gesture did not select Bump inspection");
    gpu_wait_ = true;
  }
  QJsonObject probe()
  {
    w_->setProperty("awHeightReadGpu", true);
    QJsonObject d;
    try {
      d = state_();
    }
    catch (const std::exception &error) {
      w_->setProperty("awHeightReadGpu", false);
      fail("Actual GPU probe failed: " + QString::fromUtf8(error.what()));
    }
    catch (...) {
      w_->setProperty("awHeightReadGpu", false);
      fail("Actual GPU probe failed");
    }
    w_->setProperty("awHeightReadGpu", false);
    last_ = d;
    require(d["aw_probe_ready"].toBool() && d["gpu"].toArray().size() == 2,
            "Actual Bump bindings/two-tile packets missing");
    return d;
  }
  void target(const QJsonObject &d)
  {
    require(QUuid(d["set"].toString()) == set_ && QUuid(d["layer"].toString()) == layer_ &&
                !d["selected_mask"].toBool() && QUuid(d["selected_effect"].toString()).isNull(),
            "Actual persistent target changed");
    require(d["tiles"].toArray() == QJsonArray{1001, 1002} &&
                d["precision"].toInt() == int(ChannelPrecision::L32F) &&
                d["bump_origin"].toDouble() == .5 && !d["artist_normal"].toBool(),
            "Actual fixture tiles/precision/neutral/hidden Normal differ");
    for (auto x : d["dimensions"].toArray())
      require(x.toArray().size() == 3 && x.toArray()[1].toInt() == 128 &&
                  x.toArray()[2].toInt() == 128,
              "Actual UI target is not128");
  }
  void unchanged_source(const QJsonObject &d)
  {
    target(d);
    require(d["source"] == before_["source"],
            "UI operation changed immutable source bytes/interface");
    require(d["cpu_materializations"] == before_["cpu_materializations"] &&
                d["cpu_layer_compositions"] == before_["cpu_layer_compositions"],
            "UI Height path performed CPU composition/materialization");
  }
  double gpu_error(const QJsonArray &a, const QJsonArray &b)
  {
    require(a.size() == 2 && b.size() == 2, "Actual GPU comparison packets absent");
    double error = 0;
    for (int i = 0; i < 2; ++i) {
      auto x = a[i].toObject(), y = b[i].toObject();
      require(x["tile"] == y["tile"] && x["channel"] == y["channel"],
              "GPU packet identity changed");
      auto u = x["points"].toArray(), v = y["points"].toArray();
      require(u.size() == 4 && v.size() == 4, "GPU point count changed");
      for (int j = 0; j < 4; ++j) {
        auto m = u[j].toObject(), n = v[j].toObject();
        require(m["xy"] == n["xy"] && m["rgba"].toArray().size() == 4 &&
                    n["rgba"].toArray().size() == 4,
                "GPU point identity/shape changed");
        for (int c = 0; c < 4; ++c) {
          const double value = m["rgba"].toArray()[c].toDouble();
          require(std::isfinite(value), "Nonfinite GPU data");
          error = std::max(error, std::abs(value - n["rgba"].toArray()[c].toDouble()));
        }
      }
    }
    return error;
  }
  void raw_oracle(const QJsonObject &d)
  {
    target(d);
    double error = 0, low = 1e30, high = -1e30;
    int count = 0;
    const auto pixels = height_image_reference::fixture_pixels();
    for (auto value : d["gpu"].toArray()) {
      const auto row = value.toObject();
      const int tile = row["tile"].toInt();
      for (auto point : row["points"].toArray()) {
        auto p = point.toObject();
        auto xy = p["xy"].toArray(), rgba = p["rgba"].toArray();
        require(xy.size() == 2 && rgba.size() == 4, "Signed/HDR packet shape missing");
        const double h = height_image_reference::sample(
            pixels,
            4,
            3,
            (tile - 1001) % 10 + (xy[0].toInt() + .5) / 128.,
            (tile - 1001) / 10 + (xy[1].toInt() + .5) / 128.,
            true,
            true);
        low = std::min(low, h);
        high = std::max(high, h);
        for (int c = 0; c < 4; ++c) {
          const double got = rgba[c].toDouble();
          require(std::isfinite(got), "Nonfinite raw Height");
          error = std::max(error, std::abs(got - (c == 3 ? 1. : h)));
          ++count;
        }
      }
    }
    require(count == 32 && low < 0 && high > 1 && error < 3e-5,
            "Actual L32F UI Raw Height differs from independent signed/HDR R sampler");
    checks_.append(QJsonObject{{"stage", "independent-raw-H"},
                               {"components", count},
                               {"maximum_error", error},
                               {"bound", 3e-5},
                               {"source_min", low},
                               {"source_max", high},
                               {"graph_precision", "F32"},
                               {"actual_layer_precision", d["precision"]},
                               {"semantics", "H unchanged; no active Bump response"}});
  }
  void capture(const char *name, const QJsonObject &d)
  {
    auto row = d;
    row["stage"] = name;
    captures_.append(row);
    require(capture_demo_window(w_, v_).save(out_ + "/" + name + ".png"),
            "Cannot capture owned UI");
  }
  static QJsonObject widget_description(QWidget *widget)
  {
    if (!widget)
      return {};
    return QJsonObject{{"class", widget->metaObject()->className()},
                       {"object_name", widget->objectName()},
                       {"title", widget->windowTitle()}};
  }
  void project_file_input(bool save, QPointer<QFileDialog> picker, QElapsedTimer admission)
  {
    picker_activation_["active_window_after"] = widget_description(QApplication::activeWindow());
    picker_activation_["active_modal_after"] = widget_description(
        QApplication::activeModalWidget());
    picker_activation_["focus_widget_after"] = widget_description(QApplication::focusWidget());
    picker_activation_["owner_active"] = picker && picker->isActiveWindow();
    picker_activation_["wait_ms"] = admission.elapsed();
    picker_diagnostic_ = QJsonObject{{"operation", save ? "Save As" : "Open"},
                                     {"activation", picker_activation_}};
    require(picker && picker->isVisible() && QApplication::activeModalWidget() == picker.data(),
            "Actual project picker lost visible modal ownership before activation");
    if (!picker->isActiveWindow()) {
      require(admission.elapsed() < 5000, "Actual project picker did not activate within5s");
      picker_activation_["polls"] = picker_activation_["polls"].toInt() + 1;
      QTimer::singleShot(10, this, [this, save, picker, admission] {
        project_file_input(save, picker, admission);
      });
      return;
    }
    auto *field = picker->findChild<QLineEdit *>("fileNameEdit");
    picker_activation_["filename_focus_proxy"] = widget_description(field ? field->focusProxy() :
                                                                            nullptr);
    require(field && field->isVisible() && !field->isReadOnly(),
            "Actual editable fileNameEdit absent");
    const auto path = out_ + "/aw-height-image-ui.blend";
    field->setFocus(Qt::OtherFocusReason);
    const bool focus_before = field->hasFocus();
    picker_diagnostic_ = QJsonObject{{"activation", picker_activation_},
                                     {"operation", save ? "Save As" : "Open"},
                                     {"expected_filename", path},
                                     {"field_focus_before", focus_before},
                                     {"input_method", "private clipboard full-path Ctrl+V"}};
    require(focus_before, "Actual project filename field did not acquire keyboard focus");
    auto *clipboard = QApplication::clipboard();
    require(clipboard, "Private Qt clipboard unavailable");
    clipboard->setText(path);
    key(field, Qt::Key_A, true, Qt::ControlModifier);
    key(field, Qt::Key_A, false, Qt::ControlModifier);
    require(field->hasFocus(), "Actual filename focus lost before paste");
    key(field, Qt::Key_V, true, Qt::ControlModifier);
    key(field, Qt::Key_V, false, Qt::ControlModifier);
    const bool focus_after = field->hasFocus();
    picker_diagnostic_["field_focus_after"] = focus_after;
    picker_diagnostic_["filename"] = field->text();
    require(field->text() == path, "Actual project filename paste differs");
    QPointer<QFileDialog> owner(picker);
    QPointer<QLineEdit> editor(field);
    const auto activation = picker_activation_;
    QTimer::singleShot(
        0, this, [this, save, owner, editor, path, focus_before, focus_after, activation] {
          require(owner && editor && QApplication::activeModalWidget() == owner.data(),
                  "Project picker lost actual modal ownership");
          auto *completion = editor->completer();
          QPointer<QAbstractItemView> popup = completion ? completion->popup() : nullptr;
          const bool visible = popup && popup->isVisible();
          picker_diagnostic_ = QJsonObject{{"activation", activation},
                                           {"operation", save ? "Save As" : "Open"},
                                           {"field_focus_before", focus_before},
                                           {"field_focus_after", focus_after},
                                           {"input_method", "private clipboard full-path Ctrl+V"},
                                           {"title", owner->windowTitle()},
                                           {"filename", editor->text()},
                                           {"expected_filename", path},
                                           {"completion_was_visible", visible},
                                           {"closed", false}};
          if (visible) {
            key(popup, Qt::Key_Escape);
            if (popup)
              key(popup, Qt::Key_Escape, false);
          }
          require(!popup || !popup->isVisible(),
                  "Actual filename completer popup did not close on Escape");
          require(owner && owner->isVisible() && QApplication::activeModalWidget() == owner.data(),
                  "Dismissing filename completion closed the actual picker");
          require(editor && editor->text() == path,
                  "Dismissing completion changed exact project filename");
          auto *buttons = owner->findChild<QDialogButtonBox *>();
          require(buttons, "Actual file chooser buttons missing");
          QPointer<QPushButton> button = buttons->button(save ? QDialogButtonBox::Save :
                                                                QDialogButtonBox::Open);
          require(button && button->isVisible() && button->isEnabled(),
                  "Actual project file action unavailable");
          // Observe the normal button/dialog signals. Never call click(), accept() or a slot.
          auto record = std::make_shared<QJsonObject>(picker_diagnostic_);
          auto finished = std::make_shared<bool>(false);
          const auto publish = [this, record] { picker_diagnostic_ = *record; };
          for (const auto *name : {"pressed", "released", "clicked", "accepted"})
            (*record)[name] = 0;
          const auto observed = [record, publish](const char *name) {
            (*record)[name] = (*record)[name].toInt() + 1;
            publish();
          };
          connect(button.data(), &QPushButton::pressed, this, [observed] { observed("pressed"); });
          connect(
              button.data(), &QPushButton::released, this, [observed] { observed("released"); });
          connect(button.data(), &QPushButton::clicked, this, [observed] { observed("clicked"); });
          connect(owner.data(), &QDialog::accepted, this, [observed] { observed("accepted"); });
          button->setFocus(Qt::OtherFocusReason);
          const auto selected = owner->selectedFiles();
          const auto rect = button->geometry();
          const auto parent = QFileInfo(path).absoluteDir();
          (*record)["selected_files"] = QJsonArray::fromStringList(selected);
          (*record)["file_mode"] = int(owner->fileMode());
          (*record)["accept_mode"] = int(owner->acceptMode());
          (*record)["current_directory"] = owner->directory().absolutePath();
          (*record)["parent_directory"] = parent.absolutePath();
          (*record)["parent_exists"] = parent.exists();
          (*record)["expected_file_exists"] = QFileInfo::exists(path);
          (*record)["button_text"] = button->text();
          (*record)["button_geometry"] = QJsonArray{
              rect.x(), rect.y(), rect.width(), rect.height()};
          (*record)["button_enabled"] = button->isEnabled();
          (*record)["button_visible"] = button->isVisible();
          (*record)["button_focus"] = button->hasFocus();
          (*record)["button_down_before"] = button->isDown();
          (*record)["input"] = "focused Space press/release";
          publish();
          require(selected == QStringList{path},
                  "Actual project picker selectedFiles differs from exact expected path");
          require(parent.exists(), "Actual project picker destination parent directory absent");
          require(owner->fileMode() == (save ? QFileDialog::AnyFile : QFileDialog::ExistingFile) &&
                      owner->acceptMode() ==
                          (save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen),
                  "Actual project picker mode differs");
          require(button->hasFocus() && !button->isDown() && !rect.isEmpty(),
                  "Actual project action did not acquire keyboard focus with a valid idle button");
          const auto closed = [this, record, finished, publish] {
            if (*finished)
              return;
            (*record)["closed"] = true;
            publish();
            for (const auto *name : {"pressed", "released", "clicked", "accepted"})
              require((*record)[name].toInt() == 1,
                      QString("Project picker did not emit exactly one normal signal: ") + name);
            *finished = true;
            picker_events_.append(*record);
          };
          auto *poll = new QTimer(this);
          QElapsedTimer elapsed;
          elapsed.start();
          connect(
              poll,
              &QTimer::timeout,
              this,
              [this, poll, owner, editor, record, publish, closed, elapsed] {
                if (!owner || !owner->isVisible()) {
                  closed();
                  poll->stop();
                  poll->deleteLater();
                  return;
                }
                (*record)["filename"] = editor ? editor->text() : QString();
                (*record)["selected_files_after"] = QJsonArray::fromStringList(
                    owner->selectedFiles());
                (*record)["active_modal"] = QApplication::activeModalWidget() ?
                                                QApplication::activeModalWidget()->windowTitle() :
                                                QString();
                (*record)["completion_visible"] = editor && editor->completer() &&
                                                  editor->completer()->popup()->isVisible();
                publish();
                require(
                    elapsed.elapsed() < 5000,
                    "Actual project picker did not close within5s after button input; see project_picker metadata");
              });
          poll->start(50);
          key(button.data(), Qt::Key_Space);
          (*record)["button_down_after_press"] = button && button->isDown();
          publish();
          require(button && button->isDown() && (*record)["pressed"].toInt() == 1,
                  "Actual focused project button did not receive Space press");
          key(button.data(), Qt::Key_Space, false);
          (*record)["button_down_after_release"] = button && button->isDown();
          publish();
          if (!owner || !owner->isVisible())
            closed();
        });
  }
  void project_file(bool save)
  {
    QTimer::singleShot(30, this, [this, save] {
      auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
      require(picker, "Actual project file chooser absent");
      require(picker->windowTitle() == (save ? "Save project" : "Open project"),
              "Wrong actual project chooser");
      picker_activation_ = QJsonObject{
          {"active_window_before", widget_description(QApplication::activeWindow())},
          {"active_modal_before", widget_description(QApplication::activeModalWidget())},
          {"focus_widget_before", widget_description(QApplication::focusWidget())},
          {"owner_active_before", picker->isActiveWindow()},
          {"polls", 0}};
      picker->activateWindow();
      QElapsedTimer admission;
      admission.start();
      project_file_input(save, picker, admission);
    });
    if (!save) {
      action("openProject");
      return;
    }
    QAction *chosen = nullptr;
    for (auto *action : get<QMenu>("projectFileMenu")->actions()) {
      auto text = action->text();
      text.remove('&');
      if (text == "Save As…")
        chosen = action;
    }
    require(chosen && chosen->isEnabled(), "Actual Save As action missing");
    chosen->trigger();
  }
  void advance()
  {
    if (busy_)
      return;
    if (++ticks_ > 6000)
      fail("Workspace Height lifecycle timeout");
    last_ = state_();
    for (const char *name : {"material_error",
                             "open_error",
                             "save_error",
                             "height_error",
                             "workspace_error",
                             "shader_failures"})
      require(last_[name].toString().isEmpty(),
              QString("Actual workspace error ") + name + ": " + last_[name].toString());
    for (const char *name : {"open_pending",
                             "save_pending",
                             "native_source_pending",
                             "height_pending",
                             "texture_loading",
                             "composite_pending",
                             "property_pending",
                             "layer_thumbnails_pending"})
      if (last_[name].toBool())
        return;
    // Initial CLI Open is admitted after ordinary viewport frames. An absent
    // document is a wait only here, after all real error/pending checks.
    if (step_ == 0 && !last_["has_document"].toBool())
      return;
    if (frame_ && last_["render_frames"].toInt() < frame_)
      return;
    frame_ = 0;
    if (gpu_wait_ && !last_["aw_probe_ready"].toBool()) {
      v_->update();
      return;
    }
    gpu_wait_ = false;
    busy_ = true;
    switch (step_++) {
      case 0:
        require(QUuid(last_["set"].toString()) == set_,
                "Packed NONE fixture persistent set missing");
        select();
        break;
      case 1:
        bump_view();
        break;
      case 2: {
        before_ = probe();
        target(before_);
        require(before_["height_policy"].toInt() == 0 && !before_["height_enabled"].toBool(),
                "Initial packed fixture was not NONE");
        require(QUuid(before_["source"].toObject()["id"].toString()) ==
                        QUuid(input_["source"].toString()) &&
                    before_["source"].toObject()["sha256"] == input_["source_sha256"],
                "Packed NONE source identity/hash differs");
        policy(1);
        const auto pending = state_();
        require(pending["height_pending"].toBool(),
                "Cancel did not reach a real pending Height request");
        auto *cancel = get<QPushButton>("cancelNativeHeight");
        reveal(cancel);
        checks_.append(QJsonObject{{"stage", "cancel-before-click"},
                                   {"pending", pending["height_pending"]},
                                   {"policy", pending["height_policy"]},
                                   {"undo_count", pending["undo_count"]},
                                   {"button_visible", cancel->isVisible()},
                                   {"button_enabled", cancel->isEnabled()}});
        click(cancel, cancel->rect().center());
        break;
      }
      case 3: {
        const auto d = probe();
        unchanged_source(d);
        require(d["height_policy"].toInt() == 0 && d["edit_state"] == before_["edit_state"] &&
                    d["undo_count"] == before_["undo_count"] &&
                    gpu_error(d["gpu"].toArray(), before_["gpu"].toArray()) < 1e-6,
                "Actual pending Cancel changed policy/document/history/GPU");
        checks_.append(QJsonObject{{"stage", "cancel-terminal-unchanged"}, {"state", d}});
        policy(1);
        require(state_()["height_pending"].toBool(),
                "Actual Enable did not enter Height preparation");
        break;
      }
      case 4: {
        enabled_ = probe();
        unchanged_source(enabled_);
        require(enabled_["height_policy"].toInt() == 1 && enabled_["height_enabled"].toBool() &&
                    enabled_["undo_count"].toInt() == before_["undo_count"].toInt() + 1,
                "Enable was not one committed Height edit");
        raw_oracle(enabled_);
        require(gpu_error(enabled_["gpu"].toArray(), before_["gpu"].toArray()) > .01,
                "Enabled image Height did not change actual Bump pixels");
        capture("height-enabled", enabled_);
        action("undoDocument");
        break;
      }
      case 5: {
        const auto d = probe();
        unchanged_source(d);
        require(d["height_policy"].toInt() == 0 && d["edit_state"] == before_["edit_state"] &&
                    d["undo_count"] == before_["undo_count"] &&
                    gpu_error(d["gpu"].toArray(), before_["gpu"].toArray()) < 1e-6,
                "Undo did not restore exact NONE metadata/history/GPU");
        require(d["compiled_shaders"] == enabled_["compiled_shaders"],
                "Height Undo recompiled shaders");
        checks_.append(QJsonObject{{"stage", "undo"}, {"state", d}});
        action("redoDocument");
        break;
      }
      case 6: {
        const auto d = probe();
        unchanged_source(d);
        require(d["height_policy"].toInt() == 1 && d["edit_state"] == enabled_["edit_state"] &&
                    d["undo_count"] == enabled_["undo_count"] &&
                    gpu_error(d["gpu"].toArray(), enabled_["gpu"].toArray()) < 1e-6,
                "Redo did not restore exact Raw metadata/history/GPU");
        require(d["compiled_shaders"] == enabled_["compiled_shaders"],
                "Height Redo recompiled shaders");
        checks_.append(QJsonObject{{"stage", "redo"}, {"state", d}});
        project_file(true);
        break;
      }
      case 7: {
        require(QFileInfo(out_ + "/aw-height-image-ui.blend").size() > 0 &&
                    last_["document_path"].toString() == out_ + "/aw-height-image-ui.blend",
                "Actual Save As did not complete");
        saved_ = probe();
        unchanged_source(saved_);
        require(saved_["instance"] == enabled_["instance"] &&
                    gpu_error(saved_["gpu"].toArray(), enabled_["gpu"].toArray()) < 1e-6,
                "Save changed actual raw instance/GPU");
        checks_.append(QJsonObject{{"stage", "saved"}, {"state", saved_}});
        project_file(false);
        break;
      }
      case 8:
        require(last_["has_document"].toBool() && QUuid(last_["set"].toString()) == set_,
                "Actual Open did not restore persistent set");
        select();
        break;
      case 9:
        bump_view();
        break;
      case 10: {
        const auto d = probe();
        unchanged_source(d);
        require(d["instance"] == saved_["instance"] && d["height_policy"].toInt() == 1 &&
                    d["height_enabled"].toBool() &&
                    gpu_error(d["gpu"].toArray(), saved_["gpu"].toArray()) < 1e-6,
                "Actual GUI Open changed persistent Raw instance/source/GPU");
        raw_oracle(d);
        capture("height-reopened", d);
        require(picker_events_.size() == 2, "Real Save As/Open picker observations missing");
        QFile file(out_ + "/aw-height-image-ui.json");
        require(file.open(QIODevice::WriteOnly), "Cannot write UI proof");
        file.write(
            QJsonDocument(
                QJsonObject{
                    {"passed", true},
                    {"set", set_.toString()},
                    {"layer", layer_.toString()},
                    {"checks", checks_},
                    {"captures", captures_},
                    {"pickers", picker_events_},
                    {"policies", policy_events_},
                    {"cpu_composition_delta",
                     d["cpu_layer_compositions"].toInteger() -
                         before_["cpu_layer_compositions"].toInteger()},
                    {"cpu_materialization_delta",
                     d["cpu_materializations"].toInteger() -
                         before_["cpu_materializations"].toInteger()},
                    {"scope",
                     "Actual workspace packed-NONE Enable/Cancel/Undo/Redo/Save/Open; source-free fresh-process native cold is separate evidence; no UI-cold claim"}})
                .toJson());
        file.close();
        timer_.stop();
        std::puts("AW_HEIGHT_IMAGE_UI_VERIFIED");
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        busy_ = false;
        return;
      }
      default:
        fail("Unexpected workspace Height step");
    }
    frame_ = state_()["render_frames"].toInt() + 5;
    busy_ = false;
  }

 public:
  NativeHeightImageUiDemo(QWidget *window,
                          QWidget *viewport,
                          QString directory,
                          const QString &,
                          std::function<QJsonObject()> state)
      : QObject(window),
        w_(window),
        v_(viewport),
        out_(std::move(directory)),
        state_(std::move(state))
  {
    QDir().mkpath(out_);
    QFile file(qEnvironmentVariable("BTS_AW_HEIGHT_IMAGE_UI_INPUT"));
    require(file.open(QIODevice::ReadOnly), "Packed NONE UI input witness missing");
    QJsonParseError error;
    input_ = QJsonDocument::fromJson(file.readAll(), &error).object();
    require(error.error == QJsonParseError::NoError && !input_.isEmpty(),
            "Malformed UI fixture witness");
    set_ = QUuid(input_["set"].toString());
    layer_ = QUuid(input_["layer"].toString());
    require(!set_.isNull() && !layer_.isNull() && input_["width"].toInt() == 128 &&
                input_["tiles"].toArray() == QJsonArray{1001, 1002} && input_["linear"].toBool() &&
                input_["repeat"].toBool() && input_["layer_precision"] == "L32F",
            "Actual fixture contract differs");
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace bts
