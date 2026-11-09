// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include <QAction>
#include <QAbstractSpinBox>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace bts {
// Explicit production-UI acceptance only. All edits use actual actions/widgets;
// bounded GPU diagnostics are requested between gestures, never in input events.
class NativeWorldNormalDemo final : public QObject {
  QWidget *w_, *v_;
  QString out_, mesh_, fixture_, project_;
  int resolution_;
  std::function<QJsonObject()> state_;
  QTimer timer_;
  QElapsedTimer elapsed_;
  struct Step {
    QString name;
    std::function<bool()> run;
    bool modal = false;
  };
  std::vector<Step> steps_;
  int step_ = 0, frame_ = 0;
  bool busy_ = false, failed_ = false, file_wait_ = false;
  QElapsedTimer new_admission_, input_admission_, probe_clock_;
  QString new_control_, input_control_;
  QJsonObject new_controls_, input_, baseline_, before_, edited_, saved_, picker_, cold_,
      fixture_metadata_, flat_;
  QJsonArray checks_, pickers_;
  QString upper_, parameter_;
  bool probe_wait_ = false;
  qulonglong probe_serial_ = 0;
  qulonglong cycle_generation_ = 0;
  QString cycle_epoch_, cycle_sample_;
  int cycle_done_ = -1;
  QJsonObject cycle_expected_;
  QImage cycle_previous_;
  bool cycle_first_ = true;
  static void require(bool ok, const QString &why)
  {
    if (!ok)
      throw std::runtime_error(why.toStdString());
  }
  void write(const QString &name, const QJsonObject &value)
  {
    QFile file(out_ + "/" + name);
    require(file.open(QIODevice::WriteOnly), "Cannot write own proof");
    require(file.write(QJsonDocument(value).toJson()) > 0, "Cannot write proof bytes");
  }
  static QJsonObject world(const QJsonObject &s)
  {
    return s["native_world_normal"].toObject();
  }
  static QJsonObject native(const QJsonObject &s)
  {
    return s["layer"].toObject()["fill_projection"].toObject()["native_material"].toObject();
  }
  static qulonglong counter(const QJsonObject &o, const char *key)
  {
    bool valid = false;
    const auto value = o[key].toString().toULongLong(&valid);
    require(valid, "Missing decimal counter " + QString(key));
    return value;
  }
  void fail(const QString &why)
  {
    if (failed_)
      return;
    failed_ = true;
    timer_.stop();
    w_->setProperty("nativeWorldNormalProbe", false);
    w_->setProperty("nativeWorldNormalEdgeProbe", false);
    QJsonObject s;
    try {
      s = state_();
    }
    catch (...) {
    }
    try {
      write("failure.json",
            {{"error", why},
             {"step", step_},
             {"stage", step_ < int(steps_.size()) ? steps_[step_].name : QString()},
             {"state", s},
             {"before", before_},
             {"edited", edited_},
             {"picker", picker_},
             {"pickers", pickers_},
             {"input", input_},
             {"new_controls", new_controls_},
             {"checks", checks_}});
      capture_demo_window(w_, v_).save(out_ + "/failure.png");
    }
    catch (...) {
    }
    std::fprintf(stderr, "BTS_NATIVE_WORLD_NORMAL_UI_FAIL step=%d %s\n", step_, qPrintable(why));
    QCoreApplication::exit(1);
  }
  template<class F> void protect(F f)
  {
    try {
      f();
    }
    catch (const std::exception &e) {
      fail(QString::fromUtf8(e.what()));
    }
    catch (...) {
      fail("Nonstandard actor exception");
    }
  }
  template<class F> void later(int ms, F f)
  {
    QTimer::singleShot(ms, this, [this, f] { protect(f); });
  }
  template<class T> T *get(const QString &name)
  {
    auto *p = w_->findChild<T *>(name);
    require(p, "Missing actual control " + name);
    return p;
  }
  static void key(QWidget *widget, int value, Qt::KeyboardModifiers mods = Qt::NoModifier)
  {
    for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
      QKeyEvent e(type, value, mods);
      QApplication::sendEvent(widget, &e);
    }
  }
  static void pointer(QWidget *widget, QEvent::Type type, QPoint point)
  {
    QMouseEvent e(type,
                  point,
                  widget->mapToGlobal(point),
                  type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                  (type == QEvent::MouseMove || type == QEvent::MouseButtonRelease) ?
                      Qt::NoButton :
                      Qt::LeftButton,
                  Qt::NoModifier);
    QApplication::sendEvent(widget, &e);
  }
  static void click(QWidget *widget, QPoint point)
  {
    pointer(widget, QEvent::MouseButtonPress, point);
    pointer(widget, QEvent::MouseButtonRelease, point);
  }
  void reveal(QWidget *widget)
  {
    for (auto *p = widget->parentWidget(); p; p = p->parentWidget())
      if (auto *scroll = qobject_cast<QScrollArea *>(p))
        scroll->ensureWidgetVisible(widget);
    require(widget->isVisible() && widget->isEnabled() && !widget->visibleRegion().isEmpty(),
            "Actual control not visible/enabled: " + widget->objectName());
  }
  void text(QLineEdit *edit, const QString &value)
  {
    reveal(edit);
    require(!edit->isReadOnly(), "Actual text control is read-only: " + edit->objectName());
    edit->setFocus(Qt::OtherFocusReason);
    require(edit->hasFocus(), "Actual text control lacks keyboard focus: " + edit->objectName());
    QApplication::clipboard()->setText(value);
    key(edit, Qt::Key_A, Qt::ControlModifier);
    key(edit, Qt::Key_V, Qt::ControlModifier);
    require(edit->text() == value, "Natural full-path/value paste failed");
  }
  bool new_ready(const QString &control)
  {
    auto *dialog = get<QDialog>("newProjectDialog");
    auto *target = get<QWidget>(control);
    if (new_control_ != control) {
      new_control_ = control;
      new_admission_.start();
    }
    const auto describe = [](QWidget *widget) {
      return widget ? QJsonObject{{"class", widget->metaObject()->className()},
                                  {"name", widget->objectName()},
                                  {"title", widget->windowTitle()}} :
                      QJsonObject{};
    };
    new_controls_ = QJsonObject{{"control", control},
                                {"elapsed_ms", new_admission_.elapsed()},
                                {"visible", dialog->isVisible()},
                                {"active", dialog->isActiveWindow()},
                                {"active_window", describe(QApplication::activeWindow())},
                                {"active_modal", describe(QApplication::activeModalWidget())},
                                {"active_popup", describe(QApplication::activePopupWidget())},
                                {"focus_widget", describe(QApplication::focusWidget())},
                                {"mesh_read_only", get<QLineEdit>("meshPath")->isReadOnly()}};
    require(new_admission_.elapsed() < 5000,
            "Actual New dialog did not activate/focus within5s; see new_controls");
    if (!dialog->isVisible() || QApplication::activeModalWidget() != dialog ||
        QApplication::activePopupWidget())
      return false;
    dialog->activateWindow();
    if (!dialog->isActiveWindow() || QApplication::activeWindow() != dialog)
      return false;
    reveal(target);
    target->setFocus(Qt::OtherFocusReason);
    new_controls_["control_focus"] = target->hasFocus();
    new_controls_["focus_widget"] = describe(QApplication::focusWidget());
    if (!target->hasFocus())
      return false;
    checks_.append(QJsonObject{{"stage", "new-control-admission"}, {"control", new_controls_}});
    return true;
  }
  void action(const QString &name)
  {
    auto *a = get<QAction>(name);
    require(a->isEnabled(), "Disabled action " + name);
    a->trigger();
  }
  void combo(QComboBox *box, int index)
  {
    reveal(box);
    require(index >= 0 && index < box->count(), "Combo target absent");
    box->setFocus();
    key(box, Qt::Key_F4);
    auto *view = box->view();
    const auto model = box->model()->index(index, box->modelColumn(), box->rootModelIndex());
    view->scrollTo(model);
    const auto point = view->visualRect(model).center();
    pointer(view->viewport(), QEvent::MouseMove, point);
    click(view->viewport(), point);
    require(box->currentIndex() == index, "Actual combo pointer selection failed");
  }
  static QString hash(const QString &path)
  {
    QFile f(path);
    require(f.open(QIODevice::ReadOnly), "Cannot hash actual checkpoint");
    QCryptographicHash h(QCryptographicHash::Sha256);
    require(h.addData(&f), "Cannot stream checkpoint hash");
    return QString::fromLatin1(h.result().toHex());
  }
  // Proven BM picker route: actual WindowActivate, focus, whole-path paste and
  // focused Space. No accept(), selectFile(), model mutation or direct slot.
  void picker_input(QPointer<QFileDialog> owner,
                    bool save,
                    const QString &path,
                    QElapsedTimer clock,
                    int phase = 0,
                    std::shared_ptr<QJsonObject> record = {})
  {
    require(owner && owner->isVisible() && QApplication::activeModalWidget() == owner.data(),
            "Actual picker lost modal ownership");
    require(clock.elapsed() < 5000, "Actual picker activation/input exceeded5s");
    if (!owner->isActiveWindow()) {
      owner->activateWindow();
      later(10, [=, this] { picker_input(owner, save, path, clock, phase, record); });
      return;
    }
    auto *field = owner->findChild<QLineEdit *>("fileNameEdit");
    require(field && field->isVisible(), "Actual filename editor absent");
    if (phase == 0) {
      text(field, path);
      later(0, [=, this] { picker_input(owner, save, path, clock, 1); });
      return;
    }
    require(field->text() == path, "Actual picker filename changed");
    if (auto *c = field->completer(); c && c->popup()->isVisible())
      key(c->popup(), Qt::Key_Escape);
    require(!field->completer() || !field->completer()->popup()->isVisible(),
            "Actual completer did not close");
    require(owner->selectedFiles() == QStringList{path},
            "Actual selectedFiles is not the exact expected file");
    require(owner->fileMode() == (save ? QFileDialog::AnyFile : QFileDialog::ExistingFile) &&
                owner->acceptMode() == (save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen),
            "Actual picker mode differs");
    auto *box = owner->findChild<QDialogButtonBox *>();
    require(box, "Actual picker button box absent");
    QPointer<QPushButton> button = box->button(save ? QDialogButtonBox::Save :
                                                      QDialogButtonBox::Open);
    require(button && button->isVisible() && button->isEnabled(),
            "Actual picker button unavailable");
    record = std::make_shared<QJsonObject>(
        QJsonObject{{"save", save},
                    {"path", path},
                    {"selected_files", QJsonArray::fromStringList(owner->selectedFiles())},
                    {"active", owner->isActiveWindow()},
                    {"field_focus", field->hasFocus()},
                    {"pressed", 0},
                    {"released", 0},
                    {"clicked", 0},
                    {"accepted", 0}});
    for (const auto *signal : {"pressed", "released", "clicked", "accepted"})
      (*record)[signal] = 0;
    auto count = [record](const char *name) { (*record)[name] = (*record)[name].toInt() + 1; };
    connect(button.data(), &QPushButton::pressed, this, [count] { count("pressed"); });
    connect(button.data(), &QPushButton::released, this, [count] { count("released"); });
    connect(button.data(), &QPushButton::clicked, this, [count] { count("clicked"); });
    connect(owner.data(), &QDialog::accepted, this, [count] { count("accepted"); });
    button->setFocus(Qt::OtherFocusReason);
    require(button->hasFocus() && !button->isDown(), "Actual picker action lacks idle focus");
    key(button, Qt::Key_Space);
    picker_ = *record;
    auto *poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, [=, this] {
      protect([&] {
        picker_ = *record;
        if (owner && owner->isVisible()) {
          require(clock.elapsed() < 5000, "Actual picker did not close after Space");
          return;
        }
        for (const auto *signal : {"pressed", "released", "clicked", "accepted"})
          require((*record)[signal].toInt() == 1,
                  "Actual picker must emit exactly one " + QString(signal));
        pickers_.append(*record);
        file_wait_ = false;
        poll->stop();
        poll->deleteLater();
      });
    });
    poll->start(10);
  }
  void file_dialog(bool save, const QString &path, const std::function<void()> &open)
  {
    require(!file_wait_, "A normal picker is already pending");
    file_wait_ = true;
    later(30, [=, this] {
      auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
      require(dialog, "Actual file chooser absent");
      QElapsedTimer clock;
      clock.start();
      dialog->activateWindow();
      picker_input(dialog, save, path, clock);
    });
    open();
  }
  void project_file(bool save)
  {
    file_dialog(save, project_, [=, this] {
      if (!save) {
        action("openProject");
        return;
      }
      QAction *selected = nullptr;
      for (auto *a : get<QMenu>("projectFileMenu")->actions()) {
        auto label = a->text();
        label.remove('&');
        if (label == "Save As…")
          selected = a;
      }
      require(selected && selected->isEnabled(), "Actual Save As unavailable");
      selected->trigger();
    });
  }
  void press(int code)
  {
    v_->setFocus(Qt::OtherFocusReason);
    require(v_->hasFocus(), "Viewport lacks actual focus");
    key(v_, code);
  }
  bool input_ready(QWidget *target)
  {
    reveal(target);
    if (input_control_ != target->objectName()) {
      input_control_ = target->objectName();
      input_admission_.start();
    }
    auto *owner = target->window();
    auto *popup = QApplication::activePopupWidget();
    const auto describe = [](QWidget *p) {
      return p ? QJsonObject{{"class", p->metaObject()->className()},
                             {"name", p->objectName()},
                             {"title", p->windowTitle()}} :
                 QJsonObject{};
    };
    input_ = {{"control", target->objectName()},
              {"owner", describe(owner)},
              {"owner_active", owner->isActiveWindow()},
              {"active", describe(QApplication::activeWindow())},
              {"modal", describe(QApplication::activeModalWidget())},
              {"popup", describe(popup)},
              {"focus", describe(QApplication::focusWidget())},
              {"elapsed_ms", input_admission_.elapsed()}};
    require(input_admission_.elapsed() < 5000, "Actual input admission exceeded5s");
    if (QApplication::activeModalWidget() && QApplication::activeModalWidget() != owner)
      return false;
    if (popup && popup != owner)
      return false;
    if (!popup) {
      owner->activateWindow();
      if (!owner->isActiveWindow() || QApplication::activeWindow() != owner)
        return false;
    }
    target->setFocus(Qt::OtherFocusReason);
    input_["focus_after"] = describe(QApplication::focusWidget());
    if (auto *spin = qobject_cast<QAbstractSpinBox *>(target)) {
      auto *edit = spin->findChild<QLineEdit *>();
      require(edit && !edit->isReadOnly(), "Actual numeric editor unavailable/read-only");
      input_["editor_focus"] = edit->hasFocus();
      input_["spin_focus"] = spin->hasFocus();
      input_["editor_proxy"] = describe(edit->focusProxy());
      input_["text"] = edit->text();
      return spin->hasFocus() && edit->hasFocus();
    }
    return target->hasFocus();
  }
  void number(const QString &name, double value)
  {
    QPointer<QAbstractSpinBox> spin = get<QAbstractSpinBox>(name);
    QPointer<QLineEdit> edit = spin->findChild<QLineEdit *>();
    require(edit && spin->hasFocus() && edit->hasFocus(), "Numeric owner lost admitted focus");
    const auto *decimal = qobject_cast<QDoubleSpinBox *>(spin.data());
    const auto numeric = QString::number(value, 'f', decimal ? decimal->decimals() : 0);
    QApplication::clipboard()->setText(numeric);
    for (const auto &g :
         std::array<std::pair<int, Qt::KeyboardModifiers>, 3>{{{Qt::Key_A, Qt::ControlModifier},
                                                               {Qt::Key_V, Qt::ControlModifier},
                                                               {Qt::Key_Return, Qt::NoModifier}}})
    {
      for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        require(spin && edit, "Numeric owner rebuilt during input");
        QKeyEvent e(type, g.first, g.second);
        QApplication::sendEvent(spin, &e);
      }
      if (g.first == Qt::Key_V) {
        require(spin && edit, "Numeric owner disappeared after paste");
        const QString clean = qobject_cast<QDoubleSpinBox *>(spin.data()) ?
                                  qobject_cast<QDoubleSpinBox *>(spin.data())->cleanText() :
                                  qobject_cast<QSpinBox *>(spin.data())->cleanText();
        require(clean == numeric, "Natural paste did not retain requested numeric text");
      }
    }
    if (auto *after = w_->findChild<QDoubleSpinBox *>(name))
      require(after->value() == value, "Actual decimal edit not committed");
    else {
      auto *integer = get<QSpinBox>(name);
      require(integer->value() == value, "Actual integer edit not committed");
    }
    input_control_.clear();
  }
  void toggle(const QString &name, bool enabled)
  {
    auto *box = get<QCheckBox>(name);
    require(box->hasFocus(), "Actual checkbox lacks admitted focus");
    require(box->isChecked() != enabled, "Checkbox gesture would be a no-op");
    key(box, Qt::Key_Space);
    require(get<QCheckBox>(name)->isChecked() == enabled, "Actual checkbox did not toggle");
    input_control_.clear();
  }
  QJsonObject parameter(const QJsonObject &s, const QString &name)
  {
    for (const auto &value : native(s)["parameters"].toArray())
      if (value.toObject()["name"].toString() == name)
        return value.toObject();
    throw std::runtime_error(("Missing retained parameter " + name).toStdString());
  }
  void select_layer()
  {
    auto *list = get<QListWidget>("layerStack");
    QListWidgetItem *found = nullptr;
    for (int i = 0; i < list->count(); ++i)
      if (QUuid(list->item(i)->data(Qt::UserRole).toString()) == QUuid(upper_) &&
          list->item(i)->data(Qt::UserRole + 10).toString().isEmpty() &&
          !list->item(i)->data(Qt::UserRole + 12).toBool())
        found = list->item(i);
    require(found, "Actual upper material row missing");
    list->scrollToItem(found);
    const auto rect = list->visualItemRect(found);
    // Right of the thumbnail/mask hit zones, within the actual text row.
    click(list->viewport(),
          QPoint(std::min(rect.right() - 8, rect.left() + 200), rect.center().y()));
    require(QUuid(state_()["layer_id"].toString()) == QUuid(upper_),
            "Actual row click selected another layer");
  }
  void shelf(const QString &name)
  {
    auto *list = get<QListWidget>("shelfAssets");
    QListWidgetItem *found = nullptr;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->text() == name)
        found = list->item(i);
    require(found, "Imported material missing from actual Shelf");
    list->scrollToItem(found);
    const auto point = list->visualItemRect(found).center();
    require(list->viewport()->rect().contains(point), "Shelf material clipped");
    click(list->viewport(), point);
    pointer(list->viewport(), QEvent::MouseButtonDblClick, point);
    pointer(list->viewport(), QEvent::MouseButtonRelease, point);
  }
  void import_steps(const QString &name)
  {
    add("import-" + name, [this] {
      file_dialog(false, fixture_, [this] { action("importNativeBlendMaterialAction"); });
    });
    steps_.push_back({"choose-" + name,
                      [this, name] {
                        auto *list = w_->findChild<QListWidget *>("blendMaterialList");
                        if (!list || !list->isEnabled())
                          return false;
                        QListWidgetItem *found = nullptr;
                        for (int i = 0; i < list->count(); ++i)
                          if (list->item(i)->data(Qt::UserRole).toString() == name)
                            found = list->item(i);
                        require(found, "Exact owned material absent from real import list");
                        list->scrollToItem(found);
                        click(list->viewport(), list->visualItemRect(found).center());
                        auto *button = get<QPushButton>("importBlendMaterial");
                        require(button->isEnabled(), "Actual native import disabled");
                        click(button, button->rect().center());
                        return true;
                      },
                      true});
    add("shelf-" + name, [this, name] { shelf(name); });
  }
  void bind_steps()
  {
    add("prepare-roughness", [this] {
      press(Qt::Key_M);
      for (int i = 0; i < 8 && state_()["inspection"].toInt(-1) != 2; ++i)
        press(Qt::Key_C);
      require(state_()["inspection"].toInt(-1) == 2, "Actual Roughness inspection failed");
    });
    add("return-material", [this] {
      press(Qt::Key_M);
      require(state_()["inspection"].toInt(-1) == -1, "Actual material inspection return failed");
    });
  }
  bool probe(QJsonObject &result)
  {
    if (!probe_wait_) {
      probe_serial_ = counter(world(state_()), "probe_serial");
      probe_clock_.start();
      probe_wait_ = true;
      w_->setProperty("nativeWorldNormalProbe", true);
      w_->setProperty("nativeWorldNormalEdgeProbe", true);
      try {
        result = state_();
      }
      catch (...) {
        w_->setProperty("nativeWorldNormalProbe", false);
        w_->setProperty("nativeWorldNormalEdgeProbe", false);
        throw;
      }
      w_->setProperty("nativeWorldNormalProbe", false);
      w_->setProperty("nativeWorldNormalEdgeProbe", false);
    }
    else
      result = state_();
    require(probe_clock_.elapsed() < 5000, "Explicit world-normal GPU packet exceeded5s");
    if (counter(world(result), "probe_serial") <= probe_serial_)
      return false;
    probe_wait_ = false;
    const auto data = world(result);
    require(!data.isEmpty() && !data["pending"].toBool() &&
                !data["artist_normal_enabled"].toBool(),
            "World sample packet not settled or artist Normal enabled");
    const auto samples = data["samples"].toArray();
    require(samples.size() == 4, "Expected quarter/center samples on both UDIMs");
    std::set<std::pair<int, int>> coordinates;
    std::set<int> tiles;
    for (const auto &entry : samples) {
      const auto p = entry.toObject();
      const int tile = p["tile"].toInt(), x = p["x"].toInt(-1), y = p["y"].toInt(-1);
      require((tile == 1001 || tile == 1002) && (x == resolution_ / 4 || x == resolution_ / 2) &&
                  y == resolution_ / 2,
              "Unexpected explicit GPU sample coordinate");
      require(coordinates.emplace(tile, x).second, "Duplicate world GPU sample");
      tiles.insert(tile);
      const auto bump = p["bump"].toArray();
      require(bump.size() == 4, "Legacy Bump4 sample missing");
      for (const auto &c : bump)
        require(std::isfinite(c.toDouble(NAN)), "Nonfinite Bump4 sample");
      const auto front = p["front"].toArray(), back = p["back"].toArray();
      if (data["full_targets"].toBool()) {
        require(front.size() == 4 && back.size() == 4, "Full signed world pair sample missing");
        const double a = front[3].toDouble(NAN);
        require(std::isfinite(a) && a >= 0 && a <= 1 && back[3].toDouble(NAN) == a,
                "Front/back coverage differs or invalid");
        for (const auto &rgba : {front, back}) {
          double length = 0;
          for (int c = 0; c < 3; ++c) {
            const double v = rgba[c].toDouble(NAN);
            require(std::isfinite(v), "Nonfinite signed world vector");
            length += v * v;
          }
          require(std::abs(std::sqrt(length) - a) < 3e-5,
                  "Associated world vector length differs from authored coverage");
        }
      }
      else
        require(front.isEmpty() && back.isEmpty(),
                "Inactive tiny pair must not be sampled at full-size coordinates");
    }
    require(tiles == std::set<int>{1001, 1002}, "World packet omitted a UDIM");
    if (data["enabled"].toBool())
      require(data["full_targets"].toBool() && data["completed_tiles"].toInt() == 2 &&
                  data["target_tiles"].toInt() == 2,
              "Published pair did not complete both actual UDIMs");
    const auto edges = data["edge_samples"].toArray();
    require(edges.size() == 4, "Actual island-margin packet missing");
    std::set<std::pair<int, int>> edge_coordinates;
    for (const auto &entry : edges) {
      const auto p = entry.toObject();
      const int tile = p["tile"].toInt(), x = p["x"].toInt(-1);
      require((tile == 1001 || tile == 1002) &&
                  (x == resolution_ / 8 - 1 || x == resolution_ / 8) &&
                  p["y"].toInt(-1) == resolution_ / 2 && edge_coordinates.emplace(tile, x).second,
              "Invalid/duplicate actual island-edge coordinate");
    }
    if (data["full_targets"].toBool())
      for (int tile : {1001, 1002}) {
        QJsonObject outside, inside;
        for (const auto &entry : edges) {
          const auto p = entry.toObject();
          if (p["tile"].toInt() == tile) {
            if (p["x"].toInt() == resolution_ / 8 - 1)
              outside = p;
            else
              inside = p;
          }
        }
        for (const auto *side : {"front", "back"}) {
          const auto a = outside[side].toArray(), b = inside[side].toArray();
          require(a.size() == 4 && b.size() == 4 && a == b,
                  "Outside island margin did not preserve exact associated vector+coverage donor");
          for (const auto &v : a)
            require(std::isfinite(v.toDouble(NAN)), "Nonfinite island-margin vector/coverage");
        }
      }
    else
      for (const auto &entry : edges)
        require(entry.toObject()["front"].toArray().isEmpty() &&
                    entry.toObject()["back"].toArray().isEmpty(),
                "Tiny pair sampled at island-edge coordinates");
    return true;
  }
  static QJsonArray samples(const QJsonObject &s)
  {
    return world(s)["samples"].toArray();
  }
  static double difference(const QJsonArray &a, const QJsonArray &b, const char *channel)
  {
    require(a.size() == 4 && b.size() == 4, "World comparison packet missing");
    double maximum = 0;
    for (const auto &left : a) {
      const auto x = left.toObject();
      QJsonObject y;
      for (const auto &right : b) {
        const auto row = right.toObject();
        if (row["tile"] == x["tile"] && row["x"] == x["x"] && row["y"] == x["y"])
          y = row;
      }
      require(!y.isEmpty(), "World comparison sample identity changed");
      const auto l = x[channel].toArray(), r = y[channel].toArray();
      require(l.size() == r.size(), "World sample representation changed");
      for (int c = 0; c < l.size(); ++c) {
        require(std::isfinite(l[c].toDouble(NAN)) && std::isfinite(r[c].toDouble(NAN)),
                "Nonfinite world comparison");
        maximum = std::max(maximum, std::abs(l[c].toDouble() - r[c].toDouble()));
      }
    }
    return maximum;
  }
  void invariant(const QJsonObject &s, bool cache = true)
  {
    for (const auto *name : {"set",
                             "layer_id",
                             "source_id",
                             "source_sha256",
                             "source_description",
                             "lower",
                             "cpu_materializations",
                             "cpu_layer_compositions"})
      require(s[name] == baseline_[name],
              "Gesture changed immutable source/lower/CPU state: " + QString(name));
    require(s["cpu_materializations"].toInteger() == 0 &&
                s["cpu_layer_compositions"].toInteger() == 0,
            "Interactive CPU composition/materialization");
    if (cache) {
      require(s["compiled_shaders"] == before_["compiled_shaders"],
              "Parameter/uniform edit compiled another shader");
      for (const auto *name :
           {"programs", "geometry_revision", "vertex_uploads", "index_uploads", "object_uploads"})
        require(world(s)[name] == world(before_)[name],
                "Edit rebuilt world programs/geometry: " + QString(name));
    }
  }
  void capture(const QString &name, const QJsonObject &s)
  {
    checks_.append(QJsonObject{{"stage", name}, {"state", s}});
    require(capture_demo_window(w_, v_).save(out_ + "/" + name + ".png"),
            "Cannot capture real UI");
  }
  void restored(const QJsonObject &s,
                const QJsonObject &expected,
                const QString &stage,
                bool cache = true)
  {
    invariant(s, cache);
    require(s["layer"] == expected["layer"] && s["lower"] == expected["lower"] &&
                s["undo_count"] == expected["undo_count"] && samples(s) == samples(expected) &&
                world(s)["edge_samples"] == world(expected)["edge_samples"],
            "Undo/Redo did not restore exact descriptor/history/GPU: " + stage);
    require(world(s)["enabled"] == world(expected)["enabled"] &&
                world(s)["full_targets"] == world(expected)["full_targets"],
            "Undo/Redo changed world target state");
    capture(stage, s);
  }
  void add(const QString &name, std::function<void()> f, bool modal = false)
  {
    steps_.push_back({name,
                      [f] {
                        f();
                        return true;
                      },
                      modal});
  }
  void snapshot_step(const QString &name, std::function<void(const QJsonObject &)> check)
  {
    steps_.push_back({name, [this, check] {
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        check(s);
                        return true;
                      }});
  }
  void gesture_check(const QString &name, const char *changed, bool cache = true, bool undo = true)
  {
    snapshot_step(name + "-edited", [=, this](const QJsonObject &s) {
      edited_ = s;
      invariant(s, cache);
      require(s["undo_count"].toInt() == before_["undo_count"].toInt() + 1 &&
                  s["layer"] != before_["layer"],
              "Gesture must commit exactly one changed Undo");
      if (changed)
        require(difference(samples(s), samples(before_), changed) > 1e-6,
                "Gesture did not causally change its GPU samples");
      capture(name + "-edited", s);
      if (undo)
        action("undoDocument");
    });
    if (undo) {
      snapshot_step(name + "-undo", [=, this](const QJsonObject &s) {
        restored(s, before_, name + "-undo", cache);
        action("redoDocument");
      });
      snapshot_step(name + "-redo", [=, this](const QJsonObject &s) {
        restored(s, edited_, name + "-redo", cache);
      });
    }
  }
  void parameter_steps(const QString &name, double value, bool world_change = true)
  {
    steps_.push_back({name + "-input", [=, this] {
                        const auto p = parameter(state_(), name);
                        require(p["editable"].toBool() && p["type"].toInt(-1) == 0,
                                "Expected editable scalar input");
                        parameter_ = p["id"].toString();
                        const auto control = "nativeValue_" + parameter_ + "_0";
                        if (!input_ready(get<QDoubleSpinBox>(control)))
                          return false;
                        if (!probe(before_))
                          return false;
                        number(control, value);
                        return true;
                      }});
    snapshot_step(name + "-value", [=, this](const QJsonObject &s) {
      const auto before = parameter(before_, name), actual = parameter(s, name);
      auto expected = before;
      auto values = before["value"].toArray();
      require(!values.isEmpty(), "Scalar parameter storage missing");
      values[0] = double(float(value));
      expected["value"] = values;
      require(actual == expected, "Actual scalar input changed another field/value");
      if (!world_change)
        require(samples(s) == samples(before_) &&
                    world(s)["dispatches"] == world(before_)["dispatches"] &&
                    world(s)["published_source_signature"] ==
                        world(before_)["published_source_signature"],
                "Color-only input rerendered/changed native world Normal");
      if (name == "Strength" && value == 0)
        for (const auto &entry : samples(s)) {
          const auto p = entry.toObject();
          const auto rgb = p["front"].toArray();
          const std::array<double, 3> n = p["tile"].toInt() == 1001 ?
                                              std::array<double, 3>{0, -1, 0} :
                                              std::array<double, 3>{.6, -.8, 0};
          const double a = rgb[3].toDouble();
          for (int c = 0; c < 3; ++c)
            require(
                std::abs(rgb[c].toDouble() - n[c] * a) < 3e-5,
                "Strength zero differs from independently axis-transformed fixture geometric normal");
        }
    });
    gesture_check(name, world_change ? "front" : nullptr);
  }
  void cycle_arm(qulonglong previous_generation,
                 const QString &entry_epoch = {},
                 bool first = false)
  {
    cycle_generation_ = previous_generation;
    cycle_epoch_ = entry_epoch;
    cycle_first_ = first;
    cycle_sample_.clear();
    cycle_done_ = -1;
  }
  bool cycle_ready()
  {
    const auto s = state_();
    const auto data = world(s);
    if (!s["render_ui"].toBool())
      return false;
    const auto render = s["render"].toObject();
    const auto status = render["status"].toString();
    require(!render["image_mirror_paused"].toBool() && !render["image_mirror_error"].toBool() &&
                !status.contains("error", Qt::CaseInsensitive) &&
                !status.contains("failed", Qt::CaseInsensitive),
            "Actual Cycles renderer/world pair failed");
    if (!data["cycles_pair_ready"].toBool() || !data["enabled"].toBool() ||
        data["pending"].toBool() || counter(data, "publication_generation") <= cycle_generation_)
      return false;
    const auto epoch = data["cycles_entry_epoch"].toString();
    require(!epoch.isEmpty(), "Cycles entry epoch missing");
    if (cycle_first_) {
      if (epoch == cycle_epoch_)
        return false;
    }
    else
      require(epoch == cycle_epoch_, "Cycles entry changed during parameter Undo/Redo");
    if (QRegularExpression("\\bSample [0-9]+/[0-9]+").match(status).hasMatch())
      cycle_sample_ = status;
    if (cycle_sample_.isEmpty() || !status.contains("Rendering Done"))
      return false;
    if (cycle_done_ < 0) {
      cycle_done_ = s["render_frames"].toInt();
      v_->update();
      return false;
    }
    return s["render_frames"].toInt() > cycle_done_;
  }
  void cycle_capture(const QString &name, const QJsonObject &s)
  {
    require(!cycle_sample_.isEmpty() && cycle_done_ >= 0 &&
                s["render_frames"].toInt() > cycle_done_,
            "No actual Sample -> Done -> later frame");
    const auto data = world(s);
    require(data["cycles_pair_ready"].toBool() &&
                counter(data, "publication_generation") > cycle_generation_,
            "Cycles pair is not the new completed generation");
    if (!cycle_expected_.isEmpty())
      require(s["layer"] == cycle_expected_["layer"] && samples(s) == samples(cycle_expected_),
              "Completed Cycles state differs from canonical Undo/Redo GPU state");
    const auto image = capture_viewport_framebuffer(v_).convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull() && image.save(out_ + "/" + name + ".png"),
            "Completed Cycles framebuffer unavailable");
    QJsonObject record{{"stage", name},
                       {"state", s},
                       {"new_pair_generation", data["publication_generation"]},
                       {"entry_epoch", data["cycles_entry_epoch"]},
                       {"observed_sample", cycle_sample_},
                       {"done_frame", cycle_done_},
                       {"captured_frame", s["render_frames"]}};
    if (!cycle_previous_.isNull()) {
      require(image.size() == cycle_previous_.size(), "Cycles frame dimensions changed");
      double total = 0;
      int changed = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
          const auto a = image.pixelColor(x, y), b = cycle_previous_.pixelColor(x, y);
          const int d = std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                        std::abs(a.blue() - b.blue());
          total += d;
          changed += d > 3;
        }
      const double mean = total / (255.0 * 3 * image.width() * image.height());
      require(changed > 64 && mean > .002, "Completed Cycles viewport did not visibly change");
      record["changed_pixels"] = changed;
      record["mean_absolute_rgb"] = mean;
    }
    record["scope"] =
        "Actual current pair generation -> Cycles Sample -> Done -> later visible frame. Pixel difference is an observation, not a Monte Carlo material oracle; causal signed world-vector changes and exact Undo/Redo packets are checked separately.";
    checks_.append(record);
    cycle_previous_ = image;
    cycle_epoch_ = data["cycles_entry_epoch"].toString();
    cycle_first_ = false;
  }
  void opened(const QJsonObject &s)
  {
    const auto expected = cold_.isEmpty() ? saved_ : cold_["state"].toObject();
    for (const auto *name : {"set",
                             "layer_id",
                             "layer",
                             "lower",
                             "source_id",
                             "source_sha256",
                             "source_description",
                             "tiles",
                             "width",
                             "height"})
      require(s[name] == expected[name], "Packed Open changed persistent field " + QString(name));
    require(world(s)["enabled"].toBool() && world(s)["full_targets"].toBool() &&
                !world(s)["artist_normal_enabled"].toBool(),
            "Packed Open did not restore native world shading");
    for (const auto *channel : {"front", "back", "bump"})
      require(difference(samples(s), samples(expected), channel) < 3e-5,
              "Packed Open changed canonical GPU samples");
    require(s["cpu_materializations"].toInteger() == 0 &&
                s["cpu_layer_compositions"].toInteger() == 0,
            "Packed Open used CPU layer composition");
    capture(cold_.isEmpty() ? "reopened" : "cold-reopened", s);
    if (cold_.isEmpty())
      write(
          "native-world-normal-witness.json",
          {{"project", project_},
           {"project_sha256", hash(project_)},
           {"fixture", fixture_metadata_},
           {"state", s},
           {"scope",
            "Canonical witness after actual GUI Save As/Open. Independent source-hidden cold must execute this actor in a fresh process; this warm witness alone makes no cold claim."}});
  }
  void warm_steps()
  {
    add("new-project", [this] {
      new_control_.clear();
      action("newProject");
    });
    steps_.push_back({"new-template",
                      [this] {
                        if (!new_ready("projectTemplate"))
                          return false;
                        combo(get<QComboBox>("projectTemplate"),
                              get<QComboBox>("projectTemplate")->findData(":pbr"));
                        return true;
                      },
                      true});
    steps_.push_back({"new-mesh",
                      [this] {
                        if (!new_ready("selectMesh"))
                          return false;
                        require(get<QLineEdit>("meshPath")->isReadOnly(),
                                "New mesh field must remain read-only");
                        file_dialog(false, mesh_, [this] {
                          auto *button = get<QPushButton>("selectMesh");
                          click(button, button->rect().center());
                        });
                        return true;
                      },
                      true});
    steps_.push_back({"new-size",
                      [this] {
                        if (!new_ready("documentResolution"))
                          return false;
                        require(get<QLineEdit>("meshPath")->text() == mesh_,
                                "Actual mesh picker returned another path");
                        combo(get<QComboBox>("documentResolution"),
                              get<QComboBox>("documentResolution")->findData(resolution_));
                        return true;
                      },
                      true});
    steps_.push_back({"preserve-udim",
                      [this] {
                        auto *box = get<QCheckBox>("preserveUvTiles");
                        if (!new_ready("preserveUvTiles"))
                          return false;
                        if (!box->isChecked())
                          key(box, Qt::Key_Space);
                        require(box->isChecked(), "Actual UDIM preservation not enabled");
                        return true;
                      },
                      true});
    steps_.push_back({"new-create",
                      [this] {
                        if (!new_ready("createProject"))
                          return false;
                        require(get<QComboBox>("projectTemplate")->currentData().toString() ==
                                        ":pbr" &&
                                    get<QComboBox>("documentResolution")->currentData().toInt() ==
                                        resolution_ &&
                                    get<QLineEdit>("meshPath")->text() == mesh_,
                                "Actual New settings changed");
                        auto *button = get<QPushButton>("createProject");
                        click(button, button->rect().center());
                        return true;
                      },
                      true});
    import_steps("AW World Flat");
    snapshot_step("flat-inactive", [this](const QJsonObject &s) {
      require(s["tiles"].toArray() == QJsonArray{1001, 1002} &&
                  s["width"].toInt() == resolution_ && s["height"].toInt() == resolution_,
              "Actual two-UDIM dimensions differ");
      require(!world(s)["enabled"].toBool() && !world(s)["full_targets"].toBool(),
              "Flat native material allocated/activated full world pair");
      require(native(s)["material"] == "AW World Flat", "Wrong flat source imported");
      require(world(s)["private_cpu_buffers"].toInt(-1) == 0,
              "Inactive private image samplers created CPU buffers");
      for (const auto *key : {"gpu_handles", "gpu_tile_maps"}) {
        const auto handles = world(s)[key].toArray();
        require(handles.size() == 2, "Inactive typed GPU placeholders/maps missing");
        for (const auto &handle : handles) {
          const auto id = handle.toString();
          require(!id.isEmpty() && id != "0" && id != "0x0",
                  "Inactive private sampler has no GPU-owned placeholder/map");
        }
      }
      flat_ = s;
      capture("flat-inactive", s);
    });
    add("flat-placeholder-stable", [this] {
      const auto s = state_();
      require(s["render_frames"].toInt() >= flat_["render_frames"].toInt() + 3,
              "Flat placeholders were not observed across actual later frames");
      require(!world(s)["enabled"].toBool() && !world(s)["full_targets"].toBool() &&
                  world(s)["private_cpu_buffers"].toInt(-1) == 0,
              "Later flat draw materialized private CPU/full-size image storage");
      for (const auto *key : {"gpu_handles", "gpu_tile_maps"})
        require(world(s)[key] == world(flat_)[key],
                "Inactive draw replaced its GPU-only placeholder/map");
      capture("flat-placeholders-stable", s);
    });
    import_steps("AW World Bump");
    steps_.push_back({"disable-lower-bump", [this] {
                        auto *box = get<QCheckBox>("fillChannelEnabled4");
                        if (!input_ready(box))
                          return false;
                        toggle("fillChannelEnabled4", false);
                        return true;
                      }});
    add("apply-upper", [this] { shelf("AW World Bump"); });
    bind_steps();
    snapshot_step("native-world-enabled", [this](const QJsonObject &s) {
      baseline_ = s;
      upper_ = s["layer_id"].toString();
      require(native(s)["material"] == "AW World Bump" &&
                  native(s)["group"] == "AW World Bump Group",
              "Wrong Bump group imported");
      require(world(s)["enabled"].toBool() && world(s)["full_targets"].toBool() &&
                  world(s)["error"].toString().isEmpty(),
              "Native Bump did not publish a full world pair");
      require(!s["source_id"].toString().isEmpty() && !s["source_sha256"].toString().isEmpty(),
              "Retained source identity absent");
      int siblings = 0;
      for (const auto &entry : s["lower"].toArray())
        if (entry.toObject()["fill_projection"].toObject()["material_resource"] == s["source_id"])
          ++siblings;
      require(siblings == 1, "Independent lower instance did not share original source");
      capture("native-world-enabled", s);
    });
    steps_.push_back({"bump-off-input", [this] {
                        auto *box = get<QCheckBox>("fillChannelEnabled4");
                        if (!input_ready(box) || !probe(before_))
                          return false;
                        toggle("fillChannelEnabled4", false);
                        return true;
                      }});
    gesture_check("bump-off", nullptr);
    snapshot_step("bump-off-tiny", [this](const QJsonObject &s) {
      require(!world(s)["enabled"].toBool() && !world(s)["full_targets"].toBool(),
              "Bump-off did not return full pair to tiny storage");
      action("undoDocument");
    });
    snapshot_step("bump-on-restored",
                  [this](const QJsonObject &s) { restored(s, before_, "bump-on-restored"); });
    parameter_steps("Distance", .24);
    parameter_steps("Strength", 0);
    add("restore-strength", [this] { action("undoDocument"); });
    snapshot_step("strength-restored",
                  [this](const QJsonObject &s) { restored(s, before_, "strength-restored"); });
    parameter_steps("Color Level", .625, false);
    add("bump-opacity-context", [this] {
      auto *box = get<QComboBox>("layerChannelContext");
      combo(box, box->findData(4));
    });
    add("open-opacity", [this] { action("editLayerOpacity"); });
    steps_.push_back({"opacity-input",
                      [this] {
                        auto *spin = get<QDoubleSpinBox>("layerChannelOpacity");
                        if (!input_ready(spin) || !probe(before_))
                          return false;
                        number("layerChannelOpacity", 50);
                        auto *menu = QApplication::activePopupWidget();
                        require(menu, "Actual opacity popup missing");
                        key(menu, Qt::Key_Escape);
                        return true;
                      },
                      true});
    gesture_check("opacity-half", "front");
    snapshot_step("opacity-coverage", [this](const QJsonObject &s) {
      for (const auto &entry : samples(s))
        require(std::abs(entry.toObject()["front"].toArray()[3].toDouble() - .5) < 3e-5,
                "Half opacity was not applied exactly once");
      action("undoDocument");
    });
    snapshot_step("opacity-restored",
                  [this](const QJsonObject &s) { restored(s, before_, "opacity-restored"); });
    add("add-black-mask", [this] { action("addBlackMask"); });
    add("add-mask-fill", [this] { action("addMaskFillEffect"); });
    steps_.push_back({"mask-quarter-input", [this] {
                        auto *spin = get<QDoubleSpinBox>("maskEffectFillValue");
                        if (!input_ready(spin))
                          return false;
                        number("maskEffectFillValue", .25);
                        return true;
                      }});
    snapshot_step("mask-quarter", [this](const QJsonObject &s) {
      invariant(s, false);
      for (const auto &entry : samples(s))
        require(std::abs(entry.toObject()["front"].toArray()[3].toDouble() - .25) < 3e-5,
                "Mask quarter coverage was not applied exactly once");
      capture("mask-quarter", s);
    });
    steps_.push_back({"mask-three-quarter-input", [this] {
                        auto *spin = get<QDoubleSpinBox>("maskEffectFillValue");
                        if (!input_ready(spin) || !probe(before_))
                          return false;
                        number("maskEffectFillValue", .75);
                        return true;
                      }});
    gesture_check("mask-three-quarter", "front");
    add("select-material-row", [this] { select_layer(); });
    add("scene-view", [this] { press(Qt::Key_F2); });
    add("frame-scene", [this] {
      require(state_()["view"].toObject()["layout"] == "3D only",
              "Actual Cycles fixture must use3D viewport");
      press(Qt::Key_F);
    });
    add("enter-cycles", [this] {
      const auto data = world(state_());
      cycle_arm(0, data["cycles_entry_epoch"].toString(), true);
      cycle_expected_ = {};
      action("renderingMode");
    });
    steps_.push_back({"cycles-samples", [this] {
                        auto *spin = get<QSpinBox>("renderSamples");
                        if (!input_ready(spin))
                          return false;
                        number("renderSamples", 1024);
                        return true;
                      }});
    steps_.push_back({"cycles-denoise", [this] {
                        auto *box = get<QCheckBox>("renderDenoise");
                        if (!input_ready(box))
                          return false;
                        if (box->isChecked())
                          key(box, Qt::Key_Space);
                        require(!box->isChecked(), "Actual Cycles Denoise did not switch off");
                        input_control_.clear();
                        return true;
                      }});
    steps_.push_back({"cycles-baseline", [this] {
                        if (!cycle_ready())
                          return false;
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        cycle_capture("cycles-baseline", s);
                        return true;
                      }});
    // Rendering hides the source inspector. Return through the actual Painting
    // action for its visible numeric control; Undo/Redo below run live in Cycles.
    add("painting-for-visible-source-input", [this] { action("paintingMode"); });
    steps_.push_back({"cycles-distance-input", [this] {
                        const auto p = parameter(state_(), "Distance");
                        const auto control = "nativeValue_" + p["id"].toString() + "_0";
                        if (!input_ready(get<QDoubleSpinBox>(control)) || !probe(before_))
                          return false;
                        cycle_arm(counter(world(before_), "publication_generation"),
                                  world(before_)["cycles_entry_epoch"].toString(),
                                  true);
                        number(control, .4);
                        cycle_expected_ = {};
                        return true;
                      }});
    snapshot_step("cycles-source-edited-in-painting", [this](const QJsonObject &s) {
      edited_ = s;
      invariant(s);
      const auto actual = parameter(s, "Distance");
      auto expected = parameter(before_, "Distance");
      auto values = expected["value"].toArray();
      values[0] = double(float(.4));
      expected["value"] = values;
      require(actual == expected && s["undo_count"].toInt() == before_["undo_count"].toInt() + 1 &&
                  difference(samples(s), samples(before_), "front") > 1e-6,
              "Visible Distance input missed its independent value/one Undo/causal world change");
      cycle_expected_ = s;
      action("renderingMode");
    });
    steps_.push_back({"cycles-distance-edited", [this] {
                        if (!cycle_ready())
                          return false;
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        edited_ = s;
                        invariant(s);
                        require(s["undo_count"].toInt() == before_["undo_count"].toInt() + 1 &&
                                    difference(samples(s), samples(before_), "front") > 1e-6,
                                "Cycles edit lacked one Undo/causal signed-world change");
                        cycle_capture("cycles-edited", s);
                        cycle_expected_ = before_;
                        cycle_arm(counter(world(s), "publication_generation"),
                                  world(s)["cycles_entry_epoch"].toString());
                        action("undoDocument");
                        return true;
                      }});
    steps_.push_back({"cycles-distance-undo", [this] {
                        if (!cycle_ready())
                          return false;
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        restored(s, before_, "cycles-undo-exact");
                        cycle_capture("cycles-undo", s);
                        cycle_expected_ = edited_;
                        cycle_arm(counter(world(s), "publication_generation"),
                                  world(s)["cycles_entry_epoch"].toString());
                        action("redoDocument");
                        return true;
                      }});
    steps_.push_back({"cycles-distance-redo", [this] {
                        if (!cycle_ready())
                          return false;
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        restored(s, edited_, "cycles-redo-exact");
                        cycle_capture("cycles-redo", s);
                        return true;
                      }});
    add("return-painting", [this] { action("paintingMode"); });
    steps_.push_back({"disable-main-for-raw-source", [this] {
                        auto *box = get<QCheckBox>("fillChannelEnabled4");
                        if (!input_ready(box))
                          return false;
                        toggle("fillChannelEnabled4", false);
                        return true;
                      }});
    import_steps("AW World Raw Eligible");
    snapshot_step("raw-eligible-source-baseline", [this](const QJsonObject &s) {
      require(native(s)["material"] == "AW World Raw Eligible" &&
                  native(s)["group"] == "AW World Raw Eligible Group" &&
                  !native(s).contains("height_policy"),
              "RawH fixture must be separately imported literal-control source");
      require(world(s)["enabled"].toBool() && world(s)["full_targets"].toBool(),
              "Literal Bump source did not activate world pair");
      baseline_ = s;
      upper_ = s["layer_id"].toString();
      capture("raw-eligible-source-baseline", s);
    });
    steps_.push_back({"raw-height-input", [this] {
                        auto *box = get<QComboBox>("nativeHeightPolicy");
                        if (!input_ready(box) || !probe(before_))
                          return false;
                        combo(box, box->findData(1));
                        input_control_.clear();
                        return true;
                      }});
    steps_.push_back({"raw-height-admitted", [this] {
                        if (native(state_())["height_policy"] != "authored-height-raw-v1")
                          return false;
                        QJsonObject s;
                        if (!probe(s))
                          return false;
                        require(!world(s)["enabled"].toBool() &&
                                    !world(s)["full_targets"].toBool() &&
                                    world(s)["layer_bump_enabled"].toBool(),
                                "Raw H must keep Bump4 on but disable/retire source world Normal");
                        return true;
                      }});
    gesture_check("raw-height", "bump", false);
    add("restore-native-shading", [this] { action("undoDocument"); });
    snapshot_step("native-after-raw-height", [this](const QJsonObject &s) {
      restored(s, before_, "native-after-raw-height", false);
    });
    snapshot_step("save-checkpoint", [this](const QJsonObject &s) {
      saved_ = s;
      capture("native-world-final", s);
      project_file(true);
    });
    add("open-packed", [this] {
      require(QFileInfo::exists(project_) && state_()["document_path"].toString() == project_,
              "Normal Save As did not complete");
      project_file(false);
    });
    bind_steps();
    snapshot_step("packed-reopened", [this](const QJsonObject &s) { opened(s); });
  }
  void advance()
  {
    if (busy_ || failed_ || file_wait_)
      return;
    busy_ = true;
    protect([&] {
      require(elapsed_.elapsed() < 360000, "Native world-normal UI deadline360s exceeded");
      const auto s = state_();
      for (const auto *name :
           {"shader_failures", "open_error", "save_error", "material_error", "workspace_error"})
        require(s[name].toString().isEmpty(),
                "Production error " + QString(name) + ": " + s[name].toString());
      if (step_ >= int(steps_.size())) {
        timer_.stop();
        if (cold_.isEmpty())
          require(hash(fixture_) == fixture_metadata_["source_sha256"].toString() &&
                      hash(mesh_) == fixture_metadata_["mesh_sha256"].toString(),
                  "Fixture source/mesh changed during UI acceptance");
        write(
            "native-world-normal-ui.json",
            {{"complete", true},
             {"cold", !cold_.isEmpty()},
             {"pid", QCoreApplication::applicationPid()},
             {"checks", checks_},
             {"pickers", pickers_},
             {"elapsed_ms", elapsed_.elapsed()},
             {"resolution", resolution_},
             {"tiles", QJsonArray{1001, 1002}},
             {"fixture", fixture_metadata_},
             {"scope",
              "Actual native import/Shelf/Bump4/scalars/mask-opacity/RawH/Undo/SaveOpen, current-pair completed Cycles observations. CPU/GPU source math and resource performance are separate gates; no cross-engine beauty or full AW claim."}});
        std::puts("BTS_NATIVE_WORLD_NORMAL_UI_PASS");
        QCoreApplication::quit();
        return;
      }
      const auto &step = steps_[step_];
      if (!step.modal && (QApplication::activeModalWidget() || QApplication::activePopupWidget()))
        return;
      if (!step.modal && step.name != "new-project" && step.name != "cold-open" &&
          !s["has_document"].toBool())
        return;
      if (!step.modal &&
          (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
           s["native_pending"].toBool() || s["open_pending"].toBool() ||
           s["save_pending"].toBool() || s["import_pending"].toBool() ||
           s["texture_loading"].toBool() || s["layer_thumbnails_pending"].toBool() ||
           world(s)["pending"].toBool() || s["render_frames"].toInt() < frame_))
        return;
      if (step.run()) {
        std::printf("BTS_NATIVE_WORLD_NORMAL_UI step=%d %s\n", step_, qPrintable(step.name));
        std::fflush(stdout);
        ++step_;
        frame_ = state_()["render_frames"].toInt() + 3;
      }
    });
    busy_ = false;
  }

 public:
  NativeWorldNormalDemo(QWidget *w,
                        QWidget *v,
                        QString out,
                        QString mesh,
                        int resolution,
                        std::function<QJsonObject()> state)
      : QObject(w),
        w_(w),
        v_(v),
        out_(std::move(out)),
        mesh_(std::move(mesh)),
        resolution_(resolution),
        state_(std::move(state))
  {
    require(resolution_ == 128 || resolution_ == 2048, "World actor resolution must be128 or2048");
    QDir().mkpath(out_);
    project_ = out_ + "/native-world-normal-ui.blend";
    const auto cold = qEnvironmentVariable("BTS_NATIVE_WORLD_NORMAL_COLD");
    if (!cold.isEmpty()) {
      QFile file(cold);
      require(file.open(QIODevice::ReadOnly), "Cold witness unavailable");
      cold_ = QJsonDocument::fromJson(file.readAll()).object();
      project_ = cold_["project"].toString();
      require(QFileInfo(project_).isAbsolute() &&
                  hash(project_) == cold_["project_sha256"].toString(),
              "Cold packed project identity changed");
      fixture_metadata_ = cold_["fixture"].toObject();
      baseline_ = cold_["state"].toObject();
      add("cold-open", [this] { project_file(false); });
      bind_steps();
      snapshot_step("cold-packed-reopened", [this](const QJsonObject &s) { opened(s); });
    }
    else {
      fixture_ = qEnvironmentVariable("BTS_NATIVE_WORLD_NORMAL_FIXTURE");
      require(QFileInfo(fixture_).isAbsolute() && QFileInfo(mesh_).isAbsolute(),
              "Explicit owned fixture/mesh paths required");
      QFile metadata(
          QFileInfo(fixture_).dir().filePath(QFileInfo(fixture_).completeBaseName() + ".json"));
      require(metadata.open(QIODevice::ReadOnly), "World fixture metadata unavailable");
      fixture_metadata_ = QJsonDocument::fromJson(metadata.readAll()).object();
      require(fixture_metadata_["kind"] == "owned-native-world-normal-v1" &&
                  fixture_metadata_["images"].toInt(-1) == 0 &&
                  QFileInfo(fixture_metadata_["source"].toString()).canonicalFilePath() ==
                      QFileInfo(fixture_).canonicalFilePath() &&
                  QFileInfo(fixture_metadata_["mesh"].toString()).canonicalFilePath() ==
                      QFileInfo(mesh_).canonicalFilePath() &&
                  hash(fixture_) == fixture_metadata_["source_sha256"].toString() &&
                  hash(mesh_) == fixture_metadata_["mesh_sha256"].toString(),
              "Owned fixture source metadata/hash changed");
      warm_steps();
    }
    w_->resize(1450, 950);
    w_->activateWindow();
    w_->raise();
    elapsed_.start();
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start(40);
  }
};
inline void start_native_world_normal_demo(QWidget *workspace,
                                           QWidget *viewport,
                                           QString directory,
                                           QString mesh,
                                           int resolution,
                                           std::function<QJsonObject()> state)
{
  new NativeWorldNormalDemo(
      workspace, viewport, std::move(directory), std::move(mesh), resolution, std::move(state));
}
}  // namespace bts
