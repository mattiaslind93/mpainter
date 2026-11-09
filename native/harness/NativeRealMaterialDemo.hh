// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ViewportDemoCapture.hh"
#include "IMB_colormanagement.hh"
#include <QAction>
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
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QLabel>
#include <QTreeWidget>
#include <QScrollArea>
#include <QTabWidget>
#include <QTabBar>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <array>
#include <utility>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace bts {
// Diagnostic actor only. Every mutation uses actual visible production controls.
class NativeRealMaterialDemo final : public QObject {
  QWidget *w_, *v_;
  QString out_, mesh_, project_;
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
  QElapsedTimer new_admission_;
  QString new_control_;
  QJsonObject new_controls_;
  QElapsedTimer number_admission_;
  QString number_control_;
  QJsonObject number_controls_;
  QJsonArray checks_, pickers_, checkpoints_;
  QJsonObject baseline_, before_, edited_, saved_, picker_;
  QString parameter_, number_prefix_;
  QJsonObject case_, gesture_, input_;
  QElapsedTimer input_clock_;
  QTimer bake_timer_;
  QElapsedTimer bake_clock_;
  int bake_phase_ = 0;
  QJsonObject bake_;
  static void require(bool ok, const QString &why)
  {
    if (!ok)
      throw std::runtime_error(why.toStdString());
  }
  void write(const QString &name, const QJsonObject &value)
  {
    QFile f(out_ + "/" + name);
    require(f.open(QIODevice::WriteOnly), "Cannot write owned proof");
    f.write(QJsonDocument(value).toJson());
  }
  void fail(const QString &why)
  {
    if (failed_)
      return;
    failed_ = true;
    timer_.stop();
    bake_timer_.stop();
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
             {"case", case_},
             {"gesture", gesture_},
             {"input", input_},
             {"new_controls", new_controls_},
             {"number_controls", number_controls_},
             {"bake", bake_},
             {"pickers", pickers_},
             {"checks", checks_}});
      capture_demo_window(w_, v_).save(out_ + "/failure.png");
    }
    catch (...) {
    }
    std::fprintf(stderr, "BTS_AW_REAL_UI_FAIL step=%d %s\n", step_, qPrintable(why));
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
                  type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
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
  // New is a real WindowModal dialog; combo closure/file-picker return can
  // precede its WindowActivate. Poll actual ownership/focus, never force Qt's
  // active-window model or make its deliberately read-only mesh field editable.
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
  void tab(int index)
  {
    auto *tabs = get<QTabWidget>("layerSettingsTabs");
    auto *bar = tabs->tabBar();
    reveal(bar);
    click(bar, bar->tabRect(index).center());
    require(tabs->currentIndex() == index, "Actual settings tab click failed");
  }
  void precision(int channel, int index)
  {
    const int previous = get<QTabWidget>("layerSettingsTabs")->currentIndex();
    tab(1);
    combo(get<QComboBox>(QString("channelPrecision%1").arg(channel)), index);
    tab(previous);
  }
  // QAbstractSpinBox owns keyboard focus through its editor's focus proxy.
  // Admit the actual active owner before any before-state capture or typing;
  // the displayed editor retains its XYZ prefix while cleanText is numeric.
  bool number_ready(const QString &suffix)
  {
    const auto name = number_prefix_ + parameter_ + "_" + suffix;
    QPointer<QDoubleSpinBox> spin = get<QDoubleSpinBox>(name);
    QPointer<QLineEdit> edit = spin->findChild<QLineEdit *>();
    require(edit, "Actual numeric line editor absent");
    reveal(spin);
    reveal(edit);
    if (number_control_ != name) {
      number_control_ = name;
      number_admission_.start();
    }
    const auto describe = [](QWidget *p) {
      return p ? QJsonObject{{"class", p->metaObject()->className()},
                             {"name", p->objectName()},
                             {"title", p->windowTitle()}} :
                 QJsonObject{};
    };
    auto record = [&] {
      number_controls_ = QJsonObject{
          {"control", name},
          {"elapsed_ms", number_admission_.elapsed()},
          {"owner", describe(spin ? spin->window() : nullptr)},
          {"owner_active", spin && spin->window()->isActiveWindow()},
          {"active_window", describe(QApplication::activeWindow())},
          {"active_modal", describe(QApplication::activeModalWidget())},
          {"active_popup", describe(QApplication::activePopupWidget())},
          {"focus_widget", describe(QApplication::focusWidget())},
          {"spin_alive", bool(spin)},
          {"editor_alive", bool(edit)},
          {"spin_focus", spin && spin->hasFocus()},
          {"editor_focus", edit && edit->hasFocus()},
          {"editor_focus_proxy", describe(edit ? edit->focusProxy() : nullptr)},
          {"spin_focus_proxy", describe(spin ? spin->focusProxy() : nullptr)},
          {"read_only", !edit || edit->isReadOnly()},
          {"value", spin ? spin->value() : 0},
          {"text", edit ? edit->text() : QString()}};
    };
    record();
    require(number_admission_.elapsed() < 5000,
            "Actual numeric owner/focus admission exceeded5s; see number_controls");
    require(!edit->isReadOnly(), "Actual numeric editor is read-only");
    if (QApplication::activeModalWidget() || QApplication::activePopupWidget())
      return false;
    auto *owner = spin->window();
    owner->activateWindow();
    if (!owner->isActiveWindow() || QApplication::activeWindow() != owner)
      return false;
    spin->setFocus(Qt::OtherFocusReason);
    record();
    require(spin && edit, "Numeric controls were rebuilt during focus admission");
    return spin->hasFocus() && edit->hasFocus();
  }
  void number(const QString &suffix, double value)
  {
    const auto name = number_prefix_ + parameter_ + "_" + suffix;
    QPointer<QDoubleSpinBox> spin = get<QDoubleSpinBox>(name);
    QPointer<QLineEdit> edit = spin->findChild<QLineEdit *>();
    require(edit && spin->hasFocus() && edit->hasFocus(),
            "Actual numeric keyboard owner lost focus before typing");
    const auto numeric = QString::number(value, 'f', spin->decimals());
    QApplication::clipboard()->setText(numeric);
    for (const auto &gesture :
         std::array<std::pair<int, Qt::KeyboardModifiers>, 3>{{{Qt::Key_A, Qt::ControlModifier},
                                                               {Qt::Key_V, Qt::ControlModifier},
                                                               {Qt::Key_Return, Qt::NoModifier}}})
    {
      for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
        require(spin && edit, "Numeric controls rebuilt during keyboard input");
        QKeyEvent event(type, gesture.first, gesture.second);
        QApplication::sendEvent(spin, &event);
      }
      if (gesture.first == Qt::Key_V) {
        require(spin && edit && spin->cleanText() == numeric &&
                    edit->text() == spin->prefix() + numeric + spin->suffix(),
                "Natural numeric paste changed prefix/value text");
      }
    }
    require(get<QDoubleSpinBox>(name)->value() == value, "Actual numeric edit was not committed");
    number_control_.clear();
  }
  void press(int value)
  {
    v_->setFocus(Qt::OtherFocusReason);
    key(v_, value);
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
  static QJsonObject native(const QJsonObject &s)
  {
    return s["layer"].toObject()["fill_projection"].toObject()["native_material"].toObject();
  }
  QJsonObject parameter(const QJsonObject &s, const QString &name)
  {
    for (const auto &v : native(s)["parameters"].toArray())
      if (v.toObject()["name"].toString() == name)
        return v.toObject();
    throw std::runtime_error(("Actual retained parameter absent: " + name).toStdString());
  }
  QJsonObject probe()
  {
    w_->setProperty("awVectorProbe", true);
    QJsonObject s;
    try {
      s = state_();
    }
    catch (...) {
      w_->setProperty("awVectorProbe", false);
      throw;
    }
    w_->setProperty("awVectorProbe", false);
    require(s["probe_ready"].toBool() && s["gpu"].toArray().size() == 2,
            "Actual Color/Roughness GPU bindings not ready");
    const auto grid = s["probe_grid"].toObject();
    require(grid["origin"].toArray() == QJsonArray{4, 4} &&
                grid["step"].toArray() == QJsonArray{8, 8} &&
                grid["shape"].toArray() == QJsonArray{16, 16} &&
                grid["samples_per_channel"].toInt() == 256,
            "Actual Real-only diagnostic grid differs");
    for (const auto &packet : s["gpu"].toArray())
      require(packet.toObject()["values"].toArray().size() == 256,
              "Actual Real GPU packet lacks256points");
    return s;
  }
  static double difference(const QJsonArray &a, const QJsonArray &b)
  {
    require(a.size() == 2 && b.size() == 2, "Missing causal GPU packets");
    double delta = 0;
    for (int p = 0; p < 2; ++p) {
      const auto x = a[p].toObject(), y = b[p].toObject();
      require(x["tile"] == y["tile"] && x["channel"] == y["channel"],
              "GPU packet identity changed");
      const auto av = x["values"].toArray(), bv = y["values"].toArray();
      require(av.size() == 256 && bv.size() == 256, "Real GPU grid sample count changed");
      for (int i = 0; i < 256; ++i) {
        const auto ar = av[i].toArray(), br = bv[i].toArray();
        require(ar.size() == 4 && br.size() == 4, "GPU RGBA missing");
        for (int c = 0; c < 4; ++c) {
          const double q = ar[c].toDouble(NAN), r = br[c].toDouble(NAN);
          require(std::isfinite(q) && std::isfinite(r), "Nonfinite GPU sample");
          delta = std::max(delta, std::abs(q - r));
        }
      }
    }
    return delta;
  }
  void invariant(const QJsonObject &s)
  {
    for (const auto *name : {"set",
                             "layer_id",
                             "source_id",
                             "source_sha256",
                             "source_description",
                             "lower",
                             "cpu_materializations",
                             "cpu_layer_compositions",
                             "compiled_shaders",
                             "real_color_context",
                             "real_mesh_maps",
                             "real_bake_configuration"})
      require(s[name] == baseline_[name],
              "Edit changed immutable source/lower/other-instance/CPU/shader state: " +
                  QString(name));
    require(s["cpu_materializations"].toInteger() == 0 &&
                s["cpu_layer_compositions"].toInteger() == 0,
            "Interactive CPU composition/materialization");
    const auto a = s["queues"].toObject()["bj_native_gpu"].toObject(),
               b = baseline_["queues"].toObject()["bj_native_gpu"].toObject();
    require(!a.isEmpty() && a["sources"] == b["sources"] && a["programs"] == b["programs"],
            "Edit changed shared source/program cache");
  }
  void capture(const QString &name, const QJsonObject &s)
  {
    checks_.append(QJsonObject{{"stage", name}, {"gesture", gesture_}, {"state", s}});
    require(capture_demo_window(w_, v_).save(out_ + "/" + name + ".png"),
            "Cannot save owned UI capture");
  }
  bool owner_ready(QWidget *widget, bool keyboard = true)
  {
    reveal(widget);
    auto *owner = widget->window();
    if (number_control_ != widget->objectName()) {
      number_control_ = widget->objectName();
      number_admission_.start();
    }
    input_ = QJsonObject{
        {"control", widget->objectName()},
        {"owner", owner->objectName()},
        {"owner_title", owner->windowTitle()},
        {"owner_active", owner->isActiveWindow()},
        {"active_window",
         QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : QString()},
        {"focus",
         QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QString()},
        {"elapsed_ms", number_admission_.elapsed()}};
    require(number_admission_.elapsed() < 5000, "Actual color/ramp owner admission exceeded5s");
    owner->activateWindow();
    if (!owner->isActiveWindow() || QApplication::activeWindow() != owner)
      return false;
    if (!keyboard)
      return true;
    widget->setFocus(Qt::OtherFocusReason);
    return widget->hasFocus();
  }
  void input_done()
  {
    checks_.append(QJsonObject{
        {"stage", "natural-input"},
        {"gesture", gesture_},
        {"input_handler_ms", input_clock_.nsecsElapsed() / 1e6},
        {"scope",
         "Qt input handler wall time only, excludes explicit pre/post GPU diagnostics; no GPU elapsed or general latency claim"}});
    number_control_.clear();
  }
  bool edit_number(const QJsonObject &g)
  {
    const auto p = parameter(state_(), g["parameter"].toString());
    parameter_ = p["id"].toString();
    const bool mapping = g["kind"] == "mapping";
    require(
        p["editable"].toBool() &&
            (mapping ? p["mapping"].toBool() : !p["mapping"].toBool() && p["type"].toInt(-1) == 0),
        "Actual numeric source capability differs");
    number_prefix_ = mapping ? "nativeMapping_" : "nativeValue_";
    const QString component = mapping ? "Offset_0" : "0";
    if (!number_ready(component))
      return false;
    before_ = probe();
    gesture_ = g;
    input_clock_.start();
    number(component, g["value"].toDouble());
    input_done();
    return true;
  }
  QWidget *color_picker()
  {
    if (gesture_["kind"] != "ramp-color")
      return get<QWidget>("nativeColor_" + parameter_);
    auto *position = get<QDoubleSpinBox>(
        QString("nativeRampPosition_%1_%2").arg(parameter_).arg(gesture_["stop"].toInt()));
    const auto buttons = position->parentWidget()->findChildren<QPushButton *>("colorSwatch");
    require(buttons.size() == 1, "Actual ramp stop row must own exactly one color swatch");
    auto *picker = buttons.front()->parentWidget();
    require(picker && picker->findChild<QDialog *>("colorPickerDialog"),
            "Actual ramp stop ColorPicker dialog absent");
    return picker;
  }
  bool open_color(const QJsonObject &g)
  {
    const auto p = parameter(state_(), g["parameter"].toString());
    const bool ramp_color = g["kind"] == "ramp-color";
    require(p["editable"].toBool() && p["type"].toInt(-1) == 2,
            "Actual color input capability differs");
    if (ramp_color) {
      const auto stops = p["ramp"].toObject()["stops"].toArray();
      require(p["source_node"] == "ShaderNodeValToRGB" && p["source_socket"] == "Color" &&
                  g["stop"].toInt(-1) == 0 && stops.size() >= 3 &&
                  stops[0].toObject()["position"].toDouble(-1) == 0,
              "Actual retained first-stop ramp capability differs");
    }
    else
      require(p["ramp"].toObject().isEmpty(), "Ordinary color target unexpectedly has a ramp");
    parameter_ = p["id"].toString();
    gesture_ = g;
    auto *picker = color_picker();
    auto *button = picker->findChild<QPushButton *>("colorSwatch");
    require(button, "Actual color swatch absent");
    if (!owner_ready(button))
      return false;
    before_ = probe();
    gesture_ = g;
    click(button, button->rect().center());
    number_control_.clear();
    return true;
  }
  bool edit_color()
  {
    auto *picker = color_picker();
    QPointer<QDialog> dialog = picker->findChild<QDialog *>("colorPickerDialog");
    require(dialog && dialog->isVisible(), "Actual ColorPicker popup absent");
    QPointer<QLineEdit> hex = dialog->findChild<QLineEdit *>("colorHex");
    require(hex && !hex->isReadOnly(), "Actual color hex editor absent/read-only");
    if (!owner_ready(hex))
      return false;
    input_clock_.start();
    text(hex, gesture_["hex"].toString());
    for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
      require(dialog && hex, "Color controls rebuilt during real Return");
      QKeyEvent event(type, Qt::Key_Return, Qt::NoModifier);
      QApplication::sendEvent(hex, &event);
    }
    input_done();
    require(dialog && hex, "Color controls disappeared after real commit");
    require(hex->text().compare(gesture_["hex"].toString(), Qt::CaseInsensitive) == 0,
            "Committed ColorPicker hex differs from requested input");
    checks_.append(QJsonObject{
        {"stage", "actual-color-hex"},
        {"entered_hex", gesture_["hex"]},
        {"displayed_hex", hex->text()},
        {"dialog_active", dialog->isActiveWindow()},
        {"input_method",
         "actual colorSwatch, focused clipboard hex, Return; no accept_sample/set_color"}});
    require(dialog->grab().save(out_ + "/color-picker-hex.png"),
            "Cannot save own actual color popup");
    return true;
  }
  void close_color()
  {
    auto *picker = color_picker();
    auto *dialog = picker->findChild<QDialog *>("colorPickerDialog");
    require(dialog && dialog->isVisible(), "Color popup disappeared before natural close");
    QPushButton *button = nullptr;
    for (auto *b : dialog->findChildren<QPushButton *>())
      if (b->text() == "New")
        button = b;
    require(button && button->isVisible() && button->isEnabled(),
            "Actual New color close button absent");
    click(button, button->rect().center());
    require(!dialog->isVisible(), "Actual color popup did not close");
  }
  bool edit_ramp(const QJsonObject &g)
  {
    const auto p = parameter(state_(), g["parameter"].toString());
    const auto stops = p["ramp"].toObject()["stops"].toArray();
    require(p["editable"].toBool() && stops.size() >= 3, "Actual native ramp capability differs");
    parameter_ = p["id"].toString();
    auto *row = get<QWidget>("nativeParameter_" + parameter_);
    auto *ramp = row->findChild<QWidget *>("nativeColorRamp");
    require(ramp, "Actual native ramp strip absent");
    if (!owner_ready(ramp, false))
      return false;
    const int stop = g["stop"].toInt();
    require(stop > 0 && stop + 1 < stops.size(), "Ramp fixture needs an interior stop");
    const double old = stops[stop].toObject()["position"].toDouble(),
                 target = g["position"].toDouble();
    require(target > stops[stop - 1].toObject()["position"].toDouble() &&
                target < stops[stop + 1].toObject()["position"].toDouble() &&
                std::abs(target - old) > .05,
            "Ramp target must make a genuine bounded change");
    before_ = probe();
    gesture_ = g;
    const QPoint from(qRound(8 + old * (ramp->width() - 16)), 38),
        to(qRound(8 + target * (ramp->width() - 16)), 38);
    gesture_["pointer_target_position"] = double(to.x() - 8) / std::max(1, ramp->width() - 16);
    gesture_["ramp_width"] = ramp->width();
    require(ramp->visibleRegion().contains(from) && ramp->visibleRegion().contains(to),
            "Actual ramp handles clipped");
    input_clock_.start();
    pointer(ramp, QEvent::MouseButtonPress, from);
    for (int i = 1; i <= 12; ++i)
      pointer(ramp, QEvent::MouseMove, from + (to - from) * i / 12);
    pointer(ramp, QEvent::MouseButtonRelease, to);
    input_done();
    return true;
  }
  void color_reference(const QJsonArray &values)
  {
    // Independent double reference: IEC sRGB EOTF, published XYZ/Rec.709
    // primaries and the actual recorded owner context, then retained source primaries.
    // No ColorPicker/Panel conversion helper and no actual-value-derived expectation.
    const auto context = before_["real_color_context"].toObject();
    require(context["working_space"] == "ACEScg" && context["active_working_space"] == "ACEScg" &&
                context["native_acescg"].toBool(),
            "Color reference requires the actual recorded/active ACEScg owner context");
    const auto hex = gesture_["hex"].toString();
    require(hex.size() == 7 && hex.startsWith('#'), "Malformed requested sRGB hex");
    double linear[3]{};
    float named_rgba[4]{0, 0, 0, 1};
    QJsonArray encoded, encoded_f32, linear_rgb;
    for (int c = 0; c < 3; ++c) {
      bool ok = false;
      const auto code = hex.mid(1 + 2 * c, 2).toUInt(&ok, 16);
      require(ok && code <= 255, "Malformed sRGB hex component");
      const double s = double(code) / 255.;
      linear[c] = s <= .04045 ? s / 12.92 : std::pow((s + .055) / 1.055, 2.4);
      named_rgba[c] = float(code) / 255.f;
      encoded.append(s);
      encoded_f32.append(named_rgba[c]);
      linear_rgb.append(linear[c]);
    }
    // Published decimal coefficients from pinned OCIO_matrix.hh, expressed as rows.
    // IMB's actual inline path derives inverse(XYZ_TO_REC709 * owner_scene_to_XYZ).
    constexpr double xyz_to_rec709[3][3]{{3.2409699, -1.5373832, -.4986108},
                                         {-.9692436, 1.8759675, .0415551},
                                         {.0556301, -.2039770, 1.0569715}};
    const auto owner_rows = context["scene_linear_to_xyz"].toArray();
    require(owner_rows.size() == 3, "Recorded owner primary matrix must have three rows");
    double owner_xyz[3][3]{}, forward[3][3]{};
    QJsonArray standard_rows;
    for (int r = 0; r < 3; ++r) {
      const auto row = owner_rows[r].toArray();
      require(row.size() == 3, "Recorded owner primary matrix row must have three entries");
      QJsonArray standard_row;
      for (int c = 0; c < 3; ++c) {
        require(row[c].isDouble() && std::isfinite(row[c].toDouble()),
                "Recorded owner primary matrix is not finite");
        owner_xyz[r][c] = row[c].toDouble();
        standard_row.append(xyz_to_rec709[r][c]);
      }
      standard_rows.append(standard_row);
    }
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k)
          forward[r][c] += xyz_to_rec709[r][k] * owner_xyz[k][c];
    const double a = forward[0][0], b = forward[0][1], c = forward[0][2], d = forward[1][0],
                 e = forward[1][1], f = forward[1][2], g = forward[2][0], h = forward[2][1],
                 i = forward[2][2];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    require(std::isfinite(det) && std::abs(det) > 1e-12,
            "Recorded owner primary transform is singular");
    const double rec709_to_owner[3][3]{
        {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det},
        {(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det},
        {(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det}};
    double canonical_value[3]{};
    QJsonArray canonical, inverse_rows;
    for (int r = 0; r < 3; ++r) {
      QJsonArray row;
      for (int c = 0; c < 3; ++c) {
        canonical_value[r] += rec709_to_owner[r][c] * linear[c];
        row.append(rec709_to_owner[r][c]);
      }
      require(std::isfinite(canonical_value[r]),
              "Independent analytic canonical color is not finite");
      canonical.append(canonical_value[r]);
      inverse_rows.append(row);
    }
    const auto matrix = native(before_)["color_from_aces"].toArray();
    require(matrix.size() == 9 &&
                matrix == baseline_["source_description"].toObject()["color_from_aces"].toArray(),
            "Retained source color_from_aces matrix absent/changed");
    require(values.size() == 4 && values[3].isDouble() && values[3].toDouble() == 1,
            "sRGB hex edit must retain opaque alpha1");
    QJsonArray expected, errors;
    double maximum = 0;
    constexpr double bound = 2e-6;
    for (int row = 0; row < 3; ++row) {
      double source_value = 0;
      for (int column = 0; column < 3; ++column) {
        const auto entry = matrix[column * 3 + row];
        require(entry.isDouble() && std::isfinite(entry.toDouble()),
                "Nonfinite recorded source primary matrix");
        source_value += entry.toDouble() * canonical_value[column];
      }
      require(values[row].isDouble() && std::isfinite(values[row].toDouble()),
              "Nonfinite actual native color");
      const double error = std::abs(values[row].toDouble() - source_value);
      expected.append(source_value);
      errors.append(error);
      maximum = std::max(maximum, error);
    }
    // OCIO's named nonlinear processor remains a separately reported diagnostic.
    // Its approximation/processor result is not the gate for the inline picker path.
    const char *input_name = blender::IMB_colormanagement_srgb_colorspace_name_get();
    const auto canonical_name = context["working_space"].toString().toUtf8();
    QJsonObject named_diagnostic{{"gating", false}, {"input_float32_RGB", encoded_f32}};
    if (input_name && *input_name) {
      try {
        blender::IMB_colormanagement_transform_v4(
            named_rgba, input_name, canonical_name.constData());
        QJsonArray output, delta;
        bool finite = true;
        for (int r = 0; r < 3; ++r) {
          finite &= std::isfinite(named_rgba[r]);
          output.append(named_rgba[r]);
          delta.append(std::abs(double(named_rgba[r]) - canonical_value[r]));
        }
        named_diagnostic["available"] = finite;
        named_diagnostic["canonical_RGB"] = output;
        named_diagnostic["absolute_difference_from_analytic"] = delta;
      }
      catch (const std::exception &error) {
        named_diagnostic["available"] = false;
        named_diagnostic["error"] = QString::fromUtf8(error.what());
      }
    }
    else
      named_diagnostic["available"] = false;
    checks_.append(QJsonObject{
        {"stage", "independent-color-metadata"},
        {"gesture", gesture_},
        {"requested_hex", hex},
        {"named_input_space", QString::fromUtf8(input_name ? input_name : "")},
        {"named_canonical_space", QString::fromUtf8(canonical_name.constData())},
        {"input_encoded_RGB", encoded},
        {"input_float32_encoded_RGB", encoded_f32},
        {"analytic_linear_Rec709_RGB", linear_rgb},
        {"canonical_RGB", canonical},
        {"published_XYZ_to_Rec709_rows", standard_rows},
        {"recorded_owner_scene_linear_to_XYZ_rows", owner_rows},
        {"derived_Rec709_to_owner_rows", inverse_rows},
        {"recorded_color_from_aces", matrix},
        {"matrix_order",
         "published and owner matrices row-major; source matrix column-major: output[row] = sum(matrix[column*3+row] * canonical[column])"},
        {"target_space",
         "retained source linear primaries defined by recorded matrix; no invented source-space name"},
        {"expected_source_RGB", expected},
        {"actual_source_RGBA", values},
        {"absolute_errors", errors},
        {"maximum_error", maximum},
        {"absolute_bound", bound},
        {"owner_context", context},
        {"named_OCIO_secondary", named_diagnostic},
        {"oracle",
         "independent double IEC sRGB EOTF -> inverse(published XYZ_TO_REC709 * recorded owner scene_linear_to_XYZ) -> recorded source matrix; no ColorPicker/Panel helper, actual-as-expected or tolerance change"}});
    require(maximum <= bound,
            "Actual native RGB differs from independent analytic color reference");
    require(state_()["real_color_context"] == before_["real_color_context"],
            "Independent color reference or UI commit changed owner working context");
  }
  void edited()
  {
    edited_ = probe();
    invariant(edited_);
    require(edited_["undo_count"].toInt() == before_["undo_count"].toInt() + 1 &&
                native(edited_) != native(before_),
            "Natural control change must commit exactly one Undo");
    const auto name = gesture_["parameter"].toString(), kind = gesture_["kind"].toString();
    const auto actual = parameter(edited_, name);
    auto expected = parameter(before_, name);
    if (kind == "scalar") {
      auto values = expected["value"].toArray();
      values[0] = gesture_["value"];
      expected["value"] = values;
    }
    else if (kind == "mapping")
      expected["mapping_transform"] = QJsonObject{
          {"offset", QJsonArray{gesture_["value"].toDouble(), 0, 0}},
          {"rotation_xyz_radians", QJsonArray{0, 0, 0}},
          {"scale", QJsonArray{1, 1, 1}}};
    else if (kind == "color") {
      require(actual["value"] != expected["value"], "Actual RGB hex color did not change");
      color_reference(actual["value"].toArray());
    }
    else if (kind == "ramp-color") {
      auto ramp = expected["ramp"].toObject();
      auto stops = ramp["stops"].toArray();
      const int index = gesture_["stop"].toInt(-1);
      require(index == 0 && index < stops.size(), "Actual ramp-color stop differs");
      auto stop = stops[index].toObject();
      const auto actual_stops = actual["ramp"].toObject()["stops"].toArray();
      require(actual_stops.size() == stops.size(), "Ramp color edit changed stop count");
      const auto actual_color = actual_stops[index].toObject()["color"].toArray();
      require(actual_color != stop["color"].toArray(), "Actual ramp stop color did not change");
      color_reference(actual_color);
      stop["color"] = actual_color;
      stops[index] = stop;
      ramp["stops"] = stops;
      expected["ramp"] = ramp;
    }
    else if (kind == "ramp") {
      auto ramp = expected["ramp"].toObject();
      auto stops = ramp["stops"].toArray();
      const int index = gesture_["stop"].toInt();
      auto stop = stops[index].toObject();
      stop["position"] = gesture_["pointer_target_position"];
      stops[index] = stop;
      ramp["stops"] = stops;
      expected["ramp"] = ramp;
    }
    else
      throw std::runtime_error("Unknown actual gesture result");
    if (kind == "color") {
      auto actual_metadata = actual, expected_metadata = expected;
      actual_metadata.remove("value");
      expected_metadata.remove("value");
      require(actual_metadata == expected_metadata,
              "Hex color edit changed unrelated target metadata");
    }
    else
      require(
          actual == expected,
          "Natural control changed unrelated target metadata or missed exact entered/pointer value");
    auto description = native(before_);
    auto parameters = description["parameters"].toArray();
    bool replaced = false;
    for (int i = 0; i < parameters.size(); ++i)
      if (parameters[i].toObject()["id"] == actual["id"]) {
        parameters[i] = actual;
        replaced = true;
      }
    require(replaced, "Target parameter identity absent");
    description["parameters"] = parameters;
    if (kind == "mapping")
      description["version"] = 3;
    require(native(edited_) == description,
            "One control gesture changed another native parameter or top-level policy");
    const double delta = difference(edited_["gpu"].toArray(), before_["gpu"].toArray());
    require(delta > 1e-6,
            "Real material edit did not causally change sampled Color/Roughness GPU values");
    checks_.append(QJsonObject{{"stage", "causal-GPU-change"},
                               {"gesture", gesture_},
                               {"maximum_delta", delta},
                               {"minimum_delta", 1e-6},
                               {"scope", "Causal change, not a universal material/color oracle"}});
    capture(gesture_["name"].toString() + "-edited", edited_);
    action("undoDocument");
  }
  void undone()
  {
    const auto s = probe();
    invariant(s);
    require(s["layer"] == before_["layer"] && s["gpu"] == before_["gpu"] &&
                s["undo_count"] == before_["undo_count"],
            "One Undo did not restore exact selected descriptor/GPU");
    checks_.append(QJsonObject{{"stage", "exact-undo"}, {"gesture", gesture_}, {"state", s}});
    action("redoDocument");
  }
  void redone()
  {
    const auto s = probe();
    invariant(s);
    require(s["layer"] == edited_["layer"] && s["gpu"] == edited_["gpu"] &&
                s["undo_count"] == edited_["undo_count"],
            "Redo did not restore exact selected descriptor/GPU");
    checks_.append(QJsonObject{{"stage", "exact-redo"}, {"gesture", gesture_}, {"state", s}});
  }
  void opened()
  {
    const auto s = probe();
    for (const auto *name : {"set",
                             "layer_id",
                             "layer",
                             "lower",
                             "source_id",
                             "source_sha256",
                             "source_description",
                             "gpu",
                             "probe_grid",
                             "real_color_context",
                             "real_mesh_maps",
                             "real_bake_configuration",
                             "tiles",
                             "width",
                             "height"})
      require(s[name] == saved_[name], "Actual packed Open changed " + QString(name));
    require(s["cpu_materializations"].toInteger() == 0 &&
                s["cpu_layer_compositions"].toInteger() == 0,
            "Packed Open used CPU composition");
    capture("reopened", s);
    write(
        "native-real-material-witness.json",
        {{"case", case_},
         {"project", project_},
         {"project_sha256", hash(project_)},
         {"state", s},
         {"cold_compare_fields",
          QJsonArray{"set",
                     "layer_id",
                     "layer",
                     "lower",
                     "source_id",
                     "source_sha256",
                     "source_description",
                     "gpu",
                     "probe_grid",
                     "real_color_context",
                     "real_mesh_maps",
                     "real_bake_configuration",
                     "tiles",
                     "width",
                     "height"}},
         {"scope",
          "Canonical witness after actual same-process GUI Open; fresh independent cold remains a separate future gate. No runtime UID/pointer identity comparison across Open."}});
    checkpoints_.append(QJsonObject{
        {"case", case_}, {"project", project_}, {"project_sha256", hash(project_)}, {"state", s}});
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
  void bind_steps()
  {
    add("inspect-roughness", [this] {
      press(Qt::Key_M);
      press(Qt::Key_C);
      press(Qt::Key_C);
      press(Qt::Key_C);
      require(state_()["inspection"].toInt(-1) == 2, "Real Roughness inspection failed");
    });
    add("inspect-color", [this] {
      press(Qt::Key_M);
      press(Qt::Key_C);
      require(state_()["inspection"].toInt(-1) == 0, "Real Color inspection failed");
    });
  }
  void shelf()
  {
    auto *list = get<QListWidget>("shelfAssets");
    QListWidgetItem *found = nullptr;
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->text() == case_["material"].toString())
        found = list->item(i);
    require(found, "Real imported native source absent from actual Shelf");
    list->scrollToItem(found);
    const auto pos = list->visualItemRect(found).center();
    require(list->viewport()->rect().contains(pos), "Real Shelf material clipped");
    click(list->viewport(), pos);
    pointer(list->viewport(), QEvent::MouseButtonDblClick, pos);
    pointer(list->viewport(), QEvent::MouseButtonRelease, pos);
  }
  // The Bake QAction enters nested modal loops. This independent timer drives
  // real controls while advance() is busy; completion is semantic, not a delay.
  void bake_checkbox(const QString &name, bool value)
  {
    auto *box = get<QCheckBox>(name);
    reveal(box);
    require(box->hasFocus(), "Bake checkbox must own actual keyboard focus");
    if (box->isChecked() != value)
      key(box, Qt::Key_Space);
    require(box->isChecked() == value, "Actual Bake checkbox rejected keyboard input");
  }
  void bake_combo(QComboBox *box, int target)
  {
    reveal(box);
    require(box->hasFocus() && box->window()->isActiveWindow() &&
                QApplication::activeWindow() == box->window() &&
                !QApplication::activePopupWidget(),
            "Actual closed Bake combo lacks active keyboard ownership");
    require(box->findData(target) >= 0, "Actual Bake combo target absent");
    const int before = box->currentData().toInt(), before_index = box->currentIndex();
    auto changed = std::make_shared<int>(0), activated = std::make_shared<int>(0);
    const auto change_connection = connect(
        box, &QComboBox::currentIndexChanged, this, [changed](int) { ++*changed; });
    const auto activate_connection = connect(
        box, &QComboBox::activated, this, [activated](int) { ++*activated; });
    key(box, Qt::Key_Home);
    int down = 0;
    while (box->currentData().toInt() != target && down < box->count()) {
      key(box, Qt::Key_Down);
      ++down;
    }
    key(box, Qt::Key_Tab);
    disconnect(change_connection);
    disconnect(activate_connection);
    const bool popup = QApplication::activePopupWidget() != nullptr;
    auto inputs = bake_["combo_inputs"].toArray();
    inputs.append(
        QJsonObject{{"control", box->objectName()},
                    {"input_method", "actual focused closed-combo Home, Down to target, Tab"},
                    {"before_data", before},
                    {"before_index", before_index},
                    {"target_data", target},
                    {"after_data", box->currentData().toInt()},
                    {"after_index", box->currentIndex()},
                    {"down_keys", down},
                    {"currentIndexChanged", *changed},
                    {"activated", *activated},
                    {"popup_after", popup},
                    {"owner_active", box->window()->isActiveWindow()}});
    bake_["combo_inputs"] = inputs;
    require(!popup && box->currentData().toInt() == target &&
                (before == target || (*changed > 0 && *activated > 0)),
            "Actual closed Bake combo keyboard selection failed");
  }
  void bake_modal()
  {
    require(bake_clock_.elapsed() < 120000, "Actual AO Bake exceeded120s");
    if (bake_phase_ < 8) {
      auto *dialog = get<QDialog>("meshBakeDialog");
      require(dialog->isVisible() && QApplication::activeModalWidget() == dialog,
              "Actual Bake settings lost modal ownership");
      const QString target = bake_phase_ == 0 ? "bakeMapTypes" :
                             bake_phase_ == 1 ? "bakeUseProjectMesh" :
                             bake_phase_ == 2 ? "bakeDimensionsLocked" :
                             bake_phase_ == 3 ? "bakeResolution" :
                             bake_phase_ == 4 ? "bakeAntialiasing" :
                             bake_phase_ == 5 ? "bakeSecondaryRays" :
                                                "startMeshBake";
      if (!owner_ready(get<QWidget>(target)))
        return;
      if (bake_phase_ == 0) {
        auto *sets = get<QTreeWidget>("bakeTextureSets");
        require(sets->topLevelItemCount() == 1, "AO prerequisite requires one actual Texture Set");
        auto *set = sets->topLevelItem(0);
        require(set->data(0, Qt::UserRole).toUuid().toString(QUuid::WithoutBraces) ==
                        state_()["set"].toString() &&
                    set->checkState(0) == Qt::Checked && set->childCount() == 0,
                "Actual AO Bake set/tile selection differs");
        connect(dialog, &QDialog::accepted, this, [this] {
          bake_["settings_accepted"] = bake_["settings_accepted"].toInt() + 1;
        });
        auto *maps = get<QListWidget>("bakeMapTypes");
        require(maps->count() == 7, "Actual Bake role list differs");
        for (int role = 0; role < 7; ++role) {
          auto *item = maps->item(role);
          const auto wanted = role == 3 ? Qt::Checked : Qt::Unchecked;
          if (item->checkState() != wanted) {
            maps->scrollToItem(item);
            const auto point = maps->visualItemRect(item).center();
            require(maps->viewport()->rect().contains(point), "Bake map role clipped");
            pointer(maps->viewport(), QEvent::MouseMove, point);
            click(maps->viewport(), point);
            if (item->checkState() != wanted)
              key(maps, Qt::Key_Space);
          }
          require(item->checkState() == wanted, "Actual Bake role keyboard selection differs");
        }
        bake_["role"] = 3;
        bake_["tile"] = 1001;
      }
      else if (bake_phase_ == 1)
        bake_checkbox("bakeUseProjectMesh", true);
      else if (bake_phase_ == 2)
        bake_checkbox("bakeDimensionsLocked", true);
      else if (bake_phase_ == 3)
        bake_combo(get<QComboBox>("bakeResolution"), 128);
      else if (bake_phase_ == 4)
        bake_combo(get<QComboBox>("bakeAntialiasing"), 1);
      else if (bake_phase_ == 5) {
        auto *spin = get<QSpinBox>("bakeSecondaryRays");
        auto *edit = spin->findChild<QLineEdit *>();
        require(edit && edit->hasFocus() && !edit->isReadOnly(),
                "Actual Bake rays editor lacks keyboard focus");
        QApplication::clipboard()->setText("8");
        key(spin, Qt::Key_A, Qt::ControlModifier);
        key(spin, Qt::Key_V, Qt::ControlModifier);
        key(spin, Qt::Key_Tab);
        require(spin->value() == 8, "Actual Bake rays input rejected");
      }
      else if (bake_phase_ == 6) {
        QJsonArray roles;
        auto *maps = get<QListWidget>("bakeMapTypes");
        for (int role = 0; role < 7; ++role)
          roles.append(maps->item(role)->checkState() == Qt::Checked);
        require(roles == QJsonArray{false, false, false, true, false, false, false} &&
                    get<QCheckBox>("bakeUseProjectMesh")->isChecked() &&
                    get<QCheckBox>("bakeDimensionsLocked")->isChecked() &&
                    get<QComboBox>("bakeResolution")->currentData().toInt() == 128 &&
                    get<QComboBox>("bakeHeight")->currentData().toInt() == 128 &&
                    get<QComboBox>("bakeAntialiasing")->currentData().toInt() == 1 &&
                    get<QSpinBox>("bakeSecondaryRays")->value() == 8,
                "Actual AO Bake settings changed before Start");
        bake_["enabled"] = roles;
        bake_["resolution"] = 128;
        bake_["height"] = 128;
        bake_["rays"] = 8;
        bake_["sampling"] = 1;
        bake_["project_mesh"] = true;
        require(dialog->grab().save(out_ + "/ao-bake-settings.png"),
                "Cannot retain actual AO Bake dialog");
        auto *start = get<QPushButton>("startMeshBake");
        connect(start, &QPushButton::pressed, this, [this] {
          bake_["start_pressed"] = bake_["start_pressed"].toInt() + 1;
        });
        connect(start, &QPushButton::released, this, [this] {
          bake_["start_released"] = bake_["start_released"].toInt() + 1;
        });
        connect(start, &QPushButton::clicked, this, [this] {
          bake_["start_clicked"] = bake_["start_clicked"].toInt() + 1;
        });
        bake_phase_ = 8;
        key(start, Qt::Key_Space);
        number_control_.clear();
        return;
      }
      ++bake_phase_;
      number_control_.clear();
      return;
    }
    if (bake_phase_ == 9)
      return;
    auto *progress = w_->findChild<QDialog *>("meshBakeProgress");
    if (!progress || !progress->isVisible())
      return;
    require(QApplication::activeModalWidget() == progress,
            "Actual Bake progress lost modal ownership");
    auto *finish = progress->findChild<QPushButton *>("finishMeshBake");
    auto *status = progress->findChild<QLabel *>("meshBakeProgressStatus");
    require(finish && status, "Actual Bake progress controls missing");
    bake_["progress_status"] = status->text();
    if (finish->text() == "Close")
      throw std::runtime_error(status->text().toStdString());
    if (finish->text() != "OK" || !finish->isEnabled())
      return;
    if (!owner_ready(finish))
      return;
    auto *bar = progress->findChild<QProgressBar *>("bakeProgressBar");
    require(bar && bar->maximum() == 1 && bar->value() == bar->maximum(),
            "Actual AO Bake task incomplete");
    bake_["completed_tasks"] = bar->value();
    bake_["total_tasks"] = bar->maximum();
    connect(progress, &QDialog::accepted, this, [this] {
      bake_["progress_accepted"] = bake_["progress_accepted"].toInt() + 1;
    });
    connect(finish, &QPushButton::clicked, this, [this] {
      bake_["finish_clicked"] = bake_["finish_clicked"].toInt() + 1;
    });
    require(progress->grab().save(out_ + "/ao-bake-completed.png"),
            "Cannot retain completed AO Bake dialog");
    bake_phase_ = 9;
    key(finish, Qt::Key_Space);
    number_control_.clear();
  }
  void begin_bake()
  {
    require(case_["id"] == "mclay" &&
                case_["prerequisite"].toObject() == QJsonObject{{"kind", "project-mesh-ao-bake"},
                                                                {"role", 3},
                                                                {"tile", 1001},
                                                                {"resolution", 128},
                                                                {"rays", 8},
                                                                {"sampling", 1}},
            "Unexpected AO Bake prerequisite");
    const auto before = state_();
    require(before["real_mesh_maps"].toArray().isEmpty(),
            "New mClay project already has mesh maps");
    bake_ = QJsonObject{{"undo_before", before["undo_count"]}};
    bake_phase_ = 0;
    bake_clock_.start();
    bake_timer_.start(20);
    action("bakeMeshMapsAction");
    bake_timer_.stop();
    require(bake_phase_ == 9, "Actual AO Bake did not reach completed progress");
    for (const auto *name : {"start_pressed",
                             "start_released",
                             "start_clicked",
                             "settings_accepted",
                             "progress_accepted",
                             "finish_clicked"})
      require(bake_[name].toInt() == 1, "Actual AO Bake must emit exactly one " + QString(name));
  }
  void baked()
  {
    const auto s = state_();
    require(s["undo_count"].toInt() == bake_["undo_before"].toInt() + 1,
            "Actual AO Bake must commit one document Undo");
    const auto maps = s["real_mesh_maps"].toArray();
    require(maps.size() == 1 && maps[0].toObject()["role"].toInt(-1) == 3,
            "Actual AO Bake did not install only role3");
    const auto planes = maps[0].toObject()["tiles"].toArray();
    require(planes.size() == 1, "AO Bake did not install exactly one tile");
    const auto plane = planes[0].toObject();
    require(plane["tile"].toInt() == 1001 && plane["width"].toInt() == 128 &&
                plane["height"].toInt() == 128 && plane["pixels_present"].toBool() &&
                plane["hash"].toString().size() == 16 && plane["hash"] == plane["storage_hash"],
            "Actual AO tile dimensions/residency/stored hash differ");
    const auto recipe = s["real_bake_configuration"].toObject();
    require(recipe["project_mesh"].toBool() && recipe["resolution"].toInt() == 128 &&
                recipe["height"].toInt() == 128 && recipe["rays"].toInt() == 8 &&
                recipe["sampling"].toInt() == 1 && recipe["enabled"] == bake_["enabled"],
            "Committed AO Bake recipe differs");
    bake_["map"] = maps[0];
    bake_["recipe"] = recipe;
    bake_["elapsed_ms"] = bake_clock_.elapsed();
    checks_.append(
        QJsonObject{{"stage", "actual-project-mesh-ao-bake"}, {"bake", bake_}, {"state", s}});
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
    steps_.push_back({"new-mesh-picker",
                      [this] {
                        if (!new_ready("selectMesh"))
                          return false;
                        require(get<QLineEdit>("meshPath")->isReadOnly(),
                                "Actual New mesh path must remain read-only");
                        file_dialog(false, mesh_, [this] {
                          auto *b = get<QPushButton>("selectMesh");
                          click(b, b->rect().center());
                        });
                        return true;
                      },
                      true});
    steps_.push_back({"new-resolution",
                      [this] {
                        if (!new_ready("documentResolution"))
                          return false;
                        require(get<QLineEdit>("meshPath")->text() == mesh_,
                                "Real mesh picker path differs");
                        combo(get<QComboBox>("documentResolution"),
                              get<QComboBox>("documentResolution")->findData(128));
                        return true;
                      },
                      true});
    steps_.push_back(
        {"new-create",
         [this] {
           if (!new_ready("createProject"))
             return false;
           require(get<QComboBox>("projectTemplate")->currentData().toString() == ":pbr" &&
                       get<QComboBox>("documentResolution")->currentData().toInt() == 128 &&
                       get<QLineEdit>("meshPath")->isReadOnly() &&
                       get<QLineEdit>("meshPath")->text() == mesh_,
                   "Actual New controls changed");
           auto *b = get<QPushButton>("createProject");
           click(b, b->rect().center());
           return true;
         },
         true});
    add("precision-color", [this] { precision(0, 8); });
    add("precision-roughness", [this] { precision(2, 7); });
    add("import-picker", [this] {
      file_dialog(false, case_["source"].toString(), [this] {
        action("importNativeBlendMaterialAction");
      });
    });
    steps_.push_back({"select-import",
                      [this] {
                        auto *list = w_->findChild<QListWidget *>("blendMaterialList");
                        if (!list || !list->isEnabled())
                          return false;
                        QListWidgetItem *item = nullptr;
                        for (int i = 0; i < list->count(); ++i)
                          if (list->item(i)->data(Qt::UserRole).toString() ==
                              case_["material"].toString())
                            item = list->item(i);
                        require(item, "Explicit real material absent from import list");
                        list->scrollToItem(item);
                        click(list->viewport(), list->visualItemRect(item).center());
                        auto *b = get<QPushButton>("importBlendMaterial");
                        require(b->isEnabled(), "Real native import selection disabled");
                        click(b, b->rect().center());
                        return true;
                      },
                      true});
    if (!case_["prerequisite"].toObject().isEmpty()) {
      add("bake-ao", [this] { begin_bake(); });
      add("bake-ao-installed", [this] { baked(); });
    }
    add("apply-lower", [this] { shelf(); });
    add("apply-upper", [this] { shelf(); });
    bind_steps();
    add("identity", [this] {
      baseline_ = probe();
      require(!native(baseline_).contains("height_policy"),
              "This real-material controls unit must not enable Raw Height/native Bump");
      require(baseline_["tiles"].toArray() == QJsonArray{1001} &&
                  baseline_["width"].toInt() == 128 && baseline_["height"].toInt() == 128,
              "Actual real-material fixture dimensions differ");
      require(native(baseline_)["material"] == case_["material"] &&
                  native(baseline_)["group"] == case_["group"],
              "Retained material/group identity differs");
      require(!baseline_["source_id"].toString().isEmpty() &&
                  !baseline_["source_sha256"].toString().isEmpty(),
              "Retained native source absent");
      int siblings = 0;
      for (const auto &x : baseline_["lower"].toArray())
        if (x.toObject()["fill_projection"].toObject()["material_resource"] ==
            baseline_["source_id"])
          ++siblings;
      require(siblings == 1, "Two actual native instances must share retained source");
      for (const auto &x : case_["interface"].toArray()) {
        const auto want = x.toObject(), have = parameter(baseline_, want["name"].toString());
        for (const auto *key :
             {"id", "name", "type", "editable", "mapping", "source_node", "source_socket"})
          require(want[key] == have[key],
                  "Real top interface capability changed: " + QString(key));
      }
      invariant(baseline_);
      capture("identity", baseline_);
    });
    for (const auto &value : case_["gestures"].toArray()) {
      const auto g = value.toObject();
      const auto name = g["name"].toString(), kind = g["kind"].toString();
      if (kind == "scalar" || kind == "mapping")
        steps_.push_back({name + "-input", [=, this] { return edit_number(g); }});
      else if (kind == "ramp")
        steps_.push_back({name + "-input", [=, this] { return edit_ramp(g); }});
      else if (kind == "color" || kind == "ramp-color") {
        steps_.push_back({name + "-open", [=, this] { return open_color(g); }});
        steps_.push_back({name + "-hex", [this] { return edit_color(); }, true});
        add(name + "-close", [this] { close_color(); }, true);
      }
      else
        throw std::runtime_error("Unknown real-material test gesture");
      add(name + "-check", [this] { edited(); });
      add(name + "-undo", [this] { undone(); });
      add(name + "-redo", [this] { redone(); });
    }
    add("save-as", [this] {
      saved_ = probe();
      project_file(true);
    });
    add("saved", [this] {
      require(QFileInfo::exists(project_) && state_()["document_path"].toString() == project_,
              "Actual Save As failed");
      project_file(false);
    });
    bind_steps();
    add("reopened", [this] { opened(); });
  }
  void advance()
  {
    if (busy_ || failed_ || file_wait_)
      return;
    busy_ = true;
    protect([&] {
      require(elapsed_.elapsed() < 240000, "Real material UI deadline240s exceeded");
      const auto s = state_();
      for (const auto *name :
           {"shader_failures", "open_error", "save_error", "material_error", "workspace_error"})
        require(s[name].toString().isEmpty(),
                "Production error " + QString(name) + ": " + s[name].toString());
      if (step_ >= int(steps_.size())) {
        timer_.stop();
        write(
            "native-real-material-ui.json",
            {{"complete", true},
             {"case", case_},
             {"pid", QCoreApplication::applicationPid()},
             {"checks", checks_},
             {"pickers", pickers_},
             {"checkpoints", checkpoints_},
             {"elapsed_ms", elapsed_.elapsed()},
             {"scope",
              "Actual explicit real source native import/Shelf2instances/natural visible controls/oneUndoRedo/SaveOpen at128². Causal GPU deltas and exact restoration, not a universal material oracle, native Bump parity, full AW or latency claim."}});
        std::puts("BTS_AW_REAL_UI_PASS");
        QCoreApplication::quit();
        return;
      }
      const auto &step = steps_[step_];
      if (!step.modal && (QApplication::activeModalWidget() || QApplication::activePopupWidget()))
        return;
      if (!step.modal && step.name != "new-project" && !s["has_document"].toBool())
        return;
      if (!step.modal &&
          (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
           s["native_pending"].toBool() || s["open_pending"].toBool() ||
           s["save_pending"].toBool() || s["import_pending"].toBool() ||
           s["texture_loading"].toBool() || s["layer_thumbnails_pending"].toBool() ||
           s["render_frames"].toInt() < frame_))
        return;
      std::printf("BTS_AW_REAL_UI step=%d %s\n", step_, qPrintable(step.name));
      std::fflush(stdout);
      if (step.run()) {
        ++step_;
        frame_ = state_()["render_frames"].toInt() + 3;
      }
    });
    busy_ = false;
  }

 public:
  NativeRealMaterialDemo(QWidget *w,
                         QWidget *v,
                         const QString &out,
                         const QString &mesh,
                         std::function<QJsonObject()> state)
      : QObject(w), w_(w), v_(v), out_(out), mesh_(mesh), state_(std::move(state))
  {
    QFile f(qEnvironmentVariable("BTS_AW_REAL_CASES"));
    require(f.open(QIODevice::ReadOnly), "Explicit real-material case manifest missing");
    const auto manifest = QJsonDocument::fromJson(f.readAll()).object();
    require(manifest["kind"] == "owned-real-native-material-ui-v1" &&
                manifest["cases"].toArray().size() == 6,
            "Real material case table differs");
    const auto slug = qEnvironmentVariable("BTS_AW_REAL_CASE");
    for (const auto &row : manifest["cases"].toArray())
      if (row.toObject()["id"] == slug) {
        require(case_.isEmpty(), "Duplicate real case");
        case_ = row.toObject();
      }
    require(!case_.isEmpty() && QFileInfo(case_["source"].toString()).isAbsolute(),
            "Explicit real source/case absent");
    project_ = out_ + "/" + slug + ".blend";
    QDir().mkpath(out_);
    w_->resize(1450, 950);
    w_->activateWindow();
    w_->raise();
    warm_steps();
    elapsed_.start();
    connect(&bake_timer_, &QTimer::timeout, this, [this] { protect([this] { bake_modal(); }); });
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start(40);
  }
};
}  // namespace bts
