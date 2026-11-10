// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialHeightPolicy.hh"
#pragma once
#include "ViewportDemoCapture.hh"
#include "LayerRowLayout.hh"
#include "TextureDocument.hh"
#include <QAbstractItemView>
#include <QFileInfo>
#include <QLineF>
#include <QPushButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSpinBox>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>
namespace bts {
// Explicit diagnostic actor. All authored edits go through real controls or
// pointer events. Actions named guiBj* read metadata or perform normal Save/Open.
class NativeMaterialProjectionDemo final : public QObject {
  QWidget *w, *v;
  QString out, mesh, kind, phase, failure, await_project;
  std::function<QJsonObject()> state;
  QTimer timer, dialogs;
  std::vector<std::function<void()>> steps;
  int step = 0, ticks = 0, frame = 0;
  bool busy = false, import_selection = false, height_file_picker = false;
  QUuid set_id, layer_id;
  QJsonArray checks, captures, checkpoints;
  QJsonObject before, edited, source_parameters;
  QJsonArray before_gpu, edited_gpu;
  QString edit_name;
  qint64 undo_before = 0, policy_compilations = -1;
  QJsonObject cpu_baseline, policy_cache, idle_cache;
  QJsonArray idle_gpu;
  bool cold = false;
  bool cycles_wait = false, cycles_publication = false;
  quint64 cycles_revision = 0;
  int cycles_done = -1;
  QString cycles_sample;
  QJsonArray cycles_expected;
  QImage cycles_previous;
  QJsonObject warp_camera_mapping;
  bool warp_camera_requested = false;
  double warp_camera_distance = 0;
  qint64 warp_camera_undo = 0;
  static void require(bool okay, const char *why)
  {
    if (!okay)
      throw std::runtime_error(why);
  }
  template<class T> T *get(const QString &name) const
  {
    auto *value = w->findChild<T *>(name);
    if (!value)
      throw std::runtime_error("Missing BJ actual control: " + name.toStdString());
    return value;
  }
  void action(const char *name)
  {
    auto *a = get<QAction>(name);
    require(a->isEnabled(), name);
    a->trigger();
  }
  QJsonObject snapshot(bool gpu = false)
  {
    w->setProperty("bjReadGpu", gpu);
    action("guiBjSnapshot");
    w->setProperty("bjReadGpu", false);
    return w->property("bjState").toJsonObject();
  }
  static QJsonObject projection(const QJsonObject &s)
  {
    return s["authored_layer"].toObject()["fill_projection"].toObject();
  }
  static void key(QWidget *widget,
                  int code,
                  const QString &text = {},
                  Qt::KeyboardModifiers mods = Qt::NoModifier)
  {
    for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
      QKeyEvent event(type, code, mods, text);
      QCoreApplication::sendEvent(widget, &event);
    }
  }
  static void mouse(QWidget *widget,
                    QEvent::Type type,
                    QPointF point,
                    Qt::MouseButton button,
                    Qt::MouseButtons held)
  {
    QMouseEvent event(
        type, point, QPointF(widget->mapToGlobal(point.toPoint())), button, held, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
  }
  void number(const char *name, double value)
  {
    auto *spin = get<QDoubleSpinBox>(name);
    require(spin->isVisible() && spin->isEnabled(), name);
    spin->setFocus(Qt::OtherFocusReason);
    auto *edit = spin->findChild<QLineEdit *>();
    require(edit, "BJ numeric line editor absent");
    key(edit, Qt::Key_A, "a", Qt::ControlModifier);
    const auto text = QString::number(value, 'g', 9);
    for (auto c : text)
      key(edit, Qt::Key_unknown, QString(c));
    key(edit, Qt::Key_Return);
  }
  void combo(const char *name, int index, bool sticky = true)
  {
    auto *box = get<QComboBox>(name);
    require(box->isVisible() && box->isEnabled() && index >= 0 && index < box->count(), name);
    if (box->currentIndex() == index)
      return;
    box->setFocus(Qt::OtherFocusReason);
    box->showPopup();
    auto *view = box->view();
    const auto target = box->model()->index(index, box->modelColumn(), box->rootModelIndex());
    view->scrollTo(target);
    const auto point = view->visualRect(target).center();
    mouse(view->viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(view->viewport(), QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    if (sticky)
      require(box->currentIndex() == index, "BJ real combo pointer selection failed");
  }
  void ordinary_height_file()
  {
    auto *header = get<QToolButton>("fillChannelHeader4");
    get<QScrollArea>("propertiesScroll")->ensureWidgetVisible(header);
    require(header->isVisible() && header->isEnabled() && !header->visibleRegion().isEmpty(),
            "BJ actual Bump resource header unavailable");
    checks.append(
        QJsonObject{{"stage", "ordinary-height-resource-navigation"},
                    {"legacy_combo_hidden", !get<QComboBox>("fillChannelResource4")->isVisible()},
                    {"header_visible", header->isVisible()},
                    {"header_enabled", header->isEnabled()},
                    {"header_visible_region", !header->visibleRegion().isEmpty()}});
    height_file_picker = true;
    QTimer::singleShot(0, this, [this] {
      try {
        auto *menu = get<QToolButton>("fillChannelHeader4")->menu();
        require(menu && menu->isVisible(), "BJ Bump resource popup absent");
        QAction *load = nullptr;
        for (auto *item : menu->actions())
          if (item->text() == QString::fromUtf8("Load image…"))
            load = item;
        require(load && load->isEnabled(), "BJ Bump Load image menu entry absent");
        const auto point = menu->actionGeometry(load).center();
        mouse(menu, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        mouse(menu, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
      }
      catch (const std::exception &e) {
        fail(e.what());
      }
    });
    const auto point = header->rect().center();
    mouse(header, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(header, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    height_file_picker = false;
  }
  void layer_tab(int index)
  {
    auto *tabs = get<QTabWidget>("layerSettingsTabs");
    require(index >= 0 && index < tabs->count(), "BJ layer/settings tab index invalid");
    if (tabs->currentIndex() == index)
      return;
    auto *bar = tabs->tabBar();
    require(bar->isVisible() && bar->isEnabled(), "BJ actual layer/settings tabs unavailable");
    const auto point = bar->tabRect(index).center();
    mouse(bar, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(bar, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    require(get<QTabWidget>("layerSettingsTabs")->currentIndex() == index,
            "BJ real settings tab click failed");
  }
  void channel_precision(int channel, int precision)
  {
    const auto name = QString("channelPrecision%1").arg(channel);
    const int previous = get<QTabWidget>("layerSettingsTabs")->currentIndex();
    auto *initial = get<QComboBox>(name);
    QJsonObject witness{{"stage", "channel-precision-navigation"},
                        {"channel", channel},
                        {"previous_tab", previous},
                        {"visible_before", initial->isVisible()},
                        {"enabled_before", initial->isEnabled()},
                        {"requested_precision", precision}};
    layer_tab(1);
    auto *box = get<QComboBox>(name);
    get<QScrollArea>("textureChannelsScroll")->ensureWidgetVisible(box);
    get<QScrollArea>("textureSetSettingsScroll")->ensureWidgetVisible(box);
    witness["visible_for_pointer_edit"] = box->isVisible();
    witness["enabled_for_pointer_edit"] = box->isEnabled();
    witness["visible_region_nonempty"] = !box->visibleRegion().isEmpty();
    checks.append(witness);
    combo(qPrintable(name), precision);
    layer_tab(previous);
  }
  void toggle(const char *name, bool checked)
  {
    auto *button = get<QAbstractButton>(name);
    require(button->isVisible() && button->isEnabled(), name);
    if (button->isChecked() != checked)
      button->click();
  }
  void write()
  {
    QFile file(out + "/native-projection-ui.json");
    require(file.open(QIODevice::WriteOnly), "BJ proof write failed");
    file.write(
        QJsonDocument(
            QJsonObject{
                {"version", 1},
                {"pid", QCoreApplication::applicationPid()},
                {"phase", phase},
                {"kind", kind},
                {"completed_steps", step},
                {"failure", failure},
                {"checks", checks},
                {"captures", captures},
                {"checkpoints", checkpoints},
                {"cold_scope",
                 "Independent retained data first, then explicit GPU diagnostic read. Normal Open may prepare/draw before the actor timer; no zero-GPU-before-witness claim."}})
            .toJson());
  }
  void capture(const QString &name)
  {
    auto row = state();
    row["stage"] = name;
    const auto diagnostic = snapshot(true);
    if (name == "initial-imported" || name.startsWith("policy"))
      invariant(diagnostic, false);
    row["diagnostic"] = diagnostic;
    captures.append(row);
    require(capture_demo_window(w, v).save(out + "/" + name + ".png"), "BJ capture write failed");
    write();
  }
  void fail(const char *why)
  {
    failure = QString::fromUtf8(why);
    timer.stop();
    dialogs.stop();
    try {
      write();
    }
    catch (...) {
    }
    try {
      QFile details(out + "/failure-state.json");
      if (details.open(QIODevice::WriteOnly))
        details.write(QJsonDocument(QJsonObject{{"original_failure", failure},
                                                {"step", step},
                                                {"state", state()},
                                                {"diagnostic", snapshot(false)}})
                          .toJson());
      capture_demo_window(w, v).save(out + "/failure.png");
    }
    catch (...) {
    }
    std::fprintf(stderr, "BTS_BJ_PROJECTION_FAIL step=%d %s\n", step, why);
    QCoreApplication::exit(1);
  }
  static double difference(const QJsonArray &a, const QJsonArray &b)
  {
    require(!a.isEmpty() && a.size() == b.size(), "BJ nonempty GPU packet shape differs");
    double maximum = 0;
    for (int i = 0; i < a.size(); ++i) {
      const auto x = a[i].toObject(), y = b[i].toObject();
      for (const char *field : {"set", "tile", "channel"})
        require(x[field] == y[field], "BJ GPU packet identity differs");
      const auto p = x["points"].toArray(), q = y["points"].toArray();
      require(p.size() == 5 && q.size() == 5, "BJ GPU point count differs");
      for (int j = 0; j < 5; ++j) {
        require(p[j].toObject()["xy"] == q[j].toObject()["xy"], "BJ GPU coordinate differs");
        const auto left = p[j].toObject()["rgba"].toArray(),
                   right = q[j].toObject()["rgba"].toArray();
        require(left.size() == 4 && right.size() == 4, "BJ GPU RGBA shape differs");
        for (int c = 0; c < 4; ++c) {
          const double u = left[c].toDouble(std::numeric_limits<double>::quiet_NaN()),
                       v = right[c].toDouble(std::numeric_limits<double>::quiet_NaN());
          require(std::isfinite(u) && std::isfinite(v), "BJ nonfinite GPU sample");
          maximum = std::max(maximum, std::abs(u - v));
        }
      }
    }
    return maximum;
  }
  void invariant(const QJsonObject &s, bool shader = true)
  {
    for (const char *key : {"cpu_materializations", "cpu_layer_compositions"})
      require(s[key].isDouble() && s[key] == cpu_baseline[key],
              "BJ interactive CPU composition/materialization delta");
    if (shader)
      require(s["compiled_shaders"] == before["compiled_shaders"],
              "BJ mapping edit recompiled shaders");
    if (policy_compilations >= 0)
      require(s["compiled_shaders"].toInteger() == policy_compilations,
              "BJ projection mode/control change recompiled shaders");
    if (!policy_cache.isEmpty()) {
      const auto cache = s["queues"].toObject()["bj_native_gpu"].toObject();
      require(cache["sources"] == policy_cache["sources"] &&
                  cache["programs"] == policy_cache["programs"],
              "BJ projection changed shared native source/program identity");
    }
    if (!source_parameters.isEmpty()) {
      const auto native = projection(s)["native_material"].toObject();
      require(native["parameters"] == source_parameters["parameters"],
              "BJ Fill edit changed authored native source parameters/links");
    }
  }
  void selected_ids()
  {
    const auto s = snapshot();
    const QUuid owner(s["set"].toString()), layer(s["layer"].toString());
    require(!owner.isNull() && !layer.isNull(), "BJ persistent IDs absent");
    if (set_id.isNull()) {
      set_id = owner;
      layer_id = layer;
    }
    require(owner == set_id && layer == layer_id, "BJ selected persistent set/layer changed");
    require(s["tiles"].toArray() == QJsonArray{1001, 1002, 1011},
            "BJ actual document does not contain the three fixture tiles");
    require(s["mesh_objects"].toInt() == 2 && s["owners"].toArray().size() == 2,
            "BJ actual geometry must contain two mesh owners");
    const auto dims = s["dimensions"].toArray();
    require(dims.size() == 3, "BJ actual tile dimensions absent");
    for (const auto &row : dims) {
      const auto d = row.toArray();
      require(d.size() == 3 && d[1].toInt() == 128 && d[2].toInt() == 128,
              "BJ actual fixture dimensions differ");
    }
  }
  void begin_edit(QString name, std::function<void()> mutate)
  {
    selected_ids();
    before = snapshot(true);
    before_gpu = before["gpu"].toArray();
    undo_before = state()["undo_count"].toInteger();
    edit_name = std::move(name);
    mutate();
  }
  void edited_check(bool changed = true)
  {
    edited = snapshot(true);
    edited_gpu = edited["gpu"].toArray();
    require(state()["undo_count"].toInteger() == undo_before + 1,
            "BJ edit must publish exactly one Undo");
    require(projection(edited) != projection(before),
            "BJ gesture did not change authored mapping");
    invariant(edited);
    const double delta = difference(before_gpu, edited_gpu);
    if (changed)
      require(delta > 2e-6, "BJ mapping gesture has no nonzero GPU witness");
    checks.append(QJsonObject{{"stage", edit_name},
                              {"projection", projection(edited)},
                              {"gpu_max_delta", delta},
                              {"changed_required", changed},
                              {"native_source_immutable", true},
                              {"compiled_before", before["compiled_shaders"]},
                              {"compiled_after", edited["compiled_shaders"]},
                              {"cache_before", before["queues"].toObject()["bj_native_gpu"]},
                              {"cache_after", edited["queues"].toObject()["bj_native_gpu"]},
                              {"zero_shader_recompiles", true},
                              {"zero_cpu_composition", true}});
    action("undoDocument");
  }
  void undo_check()
  {
    const auto s = snapshot(true);
    require(s["edit_state"] == before["edit_state"] &&
                state()["undo_count"].toInteger() == undo_before,
            "BJ Undo did not restore exact document/history");
    require(difference(before_gpu, s["gpu"].toArray()) < 2e-6,
            "BJ Undo restored stale GPU output");
    invariant(s);
    action("redoDocument");
  }
  void redo_check()
  {
    const auto s = snapshot(true);
    require(s["edit_state"] == edited["edit_state"] &&
                state()["undo_count"].toInteger() == undo_before + 1,
            "BJ Redo did not restore exact document/history");
    require(difference(edited_gpu, s["gpu"].toArray()) < 2e-6,
            "BJ Redo restored stale GPU output");
    invariant(s);
    idle_cache = s["queues"].toObject()["bj_native_gpu"].toObject();
    idle_gpu = s["gpu"].toArray();
  }
  void edit_steps(const QString &name, std::function<void()> edit, bool changed = true)
  {
    steps.push_back([this, name, edit] { begin_edit(name, edit); });
    steps.push_back([this, changed] { edited_check(changed); });
    steps.push_back([this] { undo_check(); });
    steps.push_back([this] { redo_check(); });
    steps.push_back([this] {
      const auto s = snapshot(true);
      invariant(s);
      require(difference(idle_gpu, s["gpu"].toArray()) < 2e-6,
              "BJ unchanged redraw changed cached GPU output");
      if (kind != "uniform") {
        const auto cache = s["queues"].toObject()["bj_native_gpu"].toObject();
        require(cache["dispatches"].isDouble() && cache["dispatches"] == idle_cache["dispatches"],
                "BJ unchanged redraw redispatched native material");
        checks.append(QJsonObject{{"stage", "unchanged-redraw-cached"},
                                  {"gesture", edit_name},
                                  {"native_cache", cache},
                                  {"max_error", difference(idle_gpu, s["gpu"].toArray())}});
      }
    });
  }
  void drag_handle(const char *domain)
  {
    const auto diagnostic = snapshot();
    const auto handle = diagnostic["gizmo_drag"].toObject();
    checks.append(
        QJsonObject{{"stage", "actual-gizmo-hit-test"},
                    {"requested_domain", domain},
                    {"handle", handle},
                    {"projection", projection(diagnostic)},
                    {"warp_edition_control", get<QComboBox>("warpEdition")->currentIndex()},
                    {"warp_surface_control", get<QCheckBox>("warpSurfaceSnap")->isChecked()}});
    require(handle["domain"].toString() == domain,
            "BJ actual production gizmo hit-test domain absent");
    const auto from = handle["from"].toArray(), to = handle["to"].toArray();
    require(from.size() == 2 && to.size() == 2, "BJ actual gizmo endpoints absent");
    const QPointF a(from[0].toDouble(), from[1].toDouble()), b(to[0].toDouble(), to[1].toDouble());
    require(v->rect().contains(a.toPoint()) && v->rect().contains(b.toPoint()) &&
                QLineF(a, b).length() > 3,
            "BJ gizmo endpoints outside viewport/degenerate");
    v->setFocus(Qt::OtherFocusReason);
    mouse(v, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 8; ++i)
      mouse(v, QEvent::MouseMove, a + (b - a) * (i / 8.), Qt::NoButton, Qt::LeftButton);
    mouse(v, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
  }
  void shelf_apply()
  {
    auto *list = get<QListWidget>("shelfAssets");
    QPoint point;
    bool found = false;
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      if (item->text() == material_name()) {
        list->scrollToItem(item);
        const auto rect = list->visualItemRect(item);
        point = {rect.center().x(), rect.top() + 24};
        found = true;
        break;
      }
    }
    require(found, "BJ imported asset absent from actual Shelf");
    for (auto type : {QEvent::MouseButtonPress,
                      QEvent::MouseButtonRelease,
                      QEvent::MouseButtonDblClick,
                      QEvent::MouseButtonRelease})
      mouse(list->viewport(),
            type,
            point,
            Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton);
  }
  QString material_name() const
  {
    return kind == "uniform" ? "BJ Uniform bitmap" : "BJ Native " + kind;
  }
  void select_persistent_layer()
  {
    auto *list = get<QListWidget>("layerStack");
    QPoint point;
    bool found = false;
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      if (QUuid(item->data(Qt::UserRole).toString()) == layer_id &&
          item->data(Qt::UserRole + 10).toString().isEmpty())
      {
        list->setCurrentItem(item);
        break;
      }
    }
    // setCurrentItem can synchronously rebuild every item. Reacquire by UUID.
    for (int i = 0; i < list->count(); ++i) {
      auto *item = list->item(i);
      if (QUuid(item->data(Qt::UserRole).toString()) == layer_id &&
          item->data(Qt::UserRole + 10).toString().isEmpty())
      {
        list->scrollToItem(item);
        const auto index = list->indexFromItem(item);
        const auto rect = list->visualItemRect(item);
        point = {layer_thumbnail_x(index, rect) + 12, rect.center().y()};
        found = true;
        break;
      }
    }
    require(found, "BJ packed persistent layer row missing");
    mouse(list->viewport(), QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(list->viewport(), QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
  }
  void uv_oracle(bool height)
  {
    if (kind != "UV")
      return;
    const auto s = snapshot(true);
    int checked = 0;
    double error = 0;
    for (const auto &value : s["gpu"].toArray()) {
      const auto packet = value.toObject();
      const int channel = packet["channel"].toInt(), tile = packet["tile"].toInt();
      if (channel != (height ? 4 : 2))
        continue;
      for (const auto &p : packet["points"].toArray()) {
        const auto row = p.toObject();
        const auto xy = row["xy"].toArray(), rgba = row["rgba"].toArray();
        require(xy.size() == 2 && rgba.size() == 4 && rgba[3].toDouble() > .99999,
                "BJ analytic UV coverage missing");
        const double u = (tile - 1001) % 10 + (xy[0].toDouble() + .5) / 128.,
                     v = (tile - 1001) / 10 + (xy[1].toDouble() + .5) / 128.;
        const double expected = height ? .5 + .2 * u - .3 * v : .15 + .2 * u + .1 * v;
        error = std::max(error, std::abs(rgba[0].toDouble() - expected));
        ++checked;
      }
    }
    require(checked == 15 && error < 3e-6,
            "BJ independent linked-UV analytic roughness/raw H failed");
    checks.append(
        QJsonObject{{"stage", height ? "initial-raw-H-oracle" : "initial-roughness-oracle"},
                    {"checked", checked},
                    {"max_error", error},
                    {"tolerance", 3e-6},
                    {"reference",
                     height ? ".5+.2*globalU-.3*globalV (raw H; no Bump strength scaling)" :
                              ".15+.2*globalU+.1*globalV"}});
  }
  void ordinary_height_oracle()
  {
    const auto s = snapshot(true);
    int checked = 0;
    double error = 0, low = 1e30, high = -1e30;
    for (const auto &value : s["gpu"].toArray()) {
      const auto packet = value.toObject();
      if (packet["channel"].toInt() != 4)
        continue;
      for (const auto &point : packet["points"].toArray()) {
        const auto p = point.toObject();
        const auto xy = p["xy"].toArray(), rgba = p["rgba"].toArray();
        require(xy.size() == 2 && rgba.size() == 4 && rgba[3].toDouble() > .99999,
                "BJ ordinary bitmap Bump coverage absent");
        const double expected = -.25 + 1.5 * (xy[0].toDouble() + .5) / 128. +
                                .125 * (xy[1].toDouble() + .5) / 128.;
        error = std::max(error, std::abs(rgba[0].toDouble() - expected));
        low = std::min(low, expected);
        high = std::max(high, expected);
        ++checked;
      }
    }
    require(checked == 15 && low < 0 && high > 1 && error < 3e-6,
            "BJ ordinary Float32 bitmap Bump signed/HDR analytic oracle failed");
    checks.append(QJsonObject{
        {"stage", "ordinary-bitmap-height-oracle"},
        {"components", checked},
        {"max_error", error},
        {"minimum", low},
        {"maximum", high},
        {"tolerance", 3e-6},
        {"scope", "Ordinary Fill Bump image; not native RawHeight or native Bump shading"}});
  }
  void checkpoint(int policy, int mode)
  {
    const auto name = QString("policy%1-mode%2").arg(policy).arg(mode),
               path = out + "/" + name + ".blend";
    selected_ids();
    const auto s = snapshot(true);
    require(projection(s)["mode"].toInt() == mode &&
                (s["native"].toBool() ? s["height_policy"].toInt() :
                                        int(s["height_enabled"].toBool())) == policy,
            "BJ actual mapping/policy differs at Save");
    checkpoints.append(QJsonObject{{"name", name},
                                   {"project", path},
                                   {"set", set_id.toString()},
                                   {"layer", layer_id.toString()},
                                   {"policy", policy},
                                   {"height_kind", s["height_kind"]},
                                   {"mode", mode},
                                   {"gpu", s["gpu"]},
                                   {"projection", projection(s)},
                                   {"controls", control_values()}});
    w->setProperty("bjProject", path);
    action("guiBjSave");
  }
  QJsonObject control_values()
  {
    QJsonObject result;
    for (const char *name :
         {"fillProjectionMode", "fillScaleMode", "fillWrap", "nativeHeightPolicy"})
      if (auto *box = w->findChild<QComboBox *>(name))
        result[name] = QJsonObject{{"index", box->currentIndex()},
                                   {"enabled", box->isEnabled()},
                                   {"visible", box->isVisible()}};
    for (const char *name : {"fillScaleU",
                             "fillScaleV",
                             "fillUvRotation",
                             "fillOffset0",
                             "fillOffset1",
                             "fill3d0_0",
                             "fill3d1_2",
                             "fill3d2_0",
                             "fillTriplanarHardness",
                             "fillPhysicalSizeX",
                             "fillPhysicalSizeY",
                             "fillPhysicalSizeZ"})
      if (auto *number = w->findChild<QDoubleSpinBox *>(name))
        result[name] = QJsonObject{{"value", number->value()},
                                   {"enabled", number->isEnabled()},
                                   {"visible", number->isVisible()}};
    return result;
  }
  void export_files()
  {
    busy = true;
    QTimer::singleShot(100, this, [this] {
      try {
        auto *dialog = get<QDialog>("flattenExportDialog");
        const auto directory = out + "/flatten-export";
        QDir().mkpath(directory);
        get<QLineEdit>("flattenExportDirectory")->setText(directory);
        get<QPushButton>("runFlattenExport")->click();
        const auto probes = dialog->property("flattenExportProbes").toJsonArray();
        require(dialog->property("flattenExportError").toString().isEmpty() &&
                    dialog->property("flattenExportFinished").toBool() && !probes.isEmpty(),
                "BJ GPU Flatten/export actual covered-texel verification unavailable");
        for (const auto &value : probes) {
          const auto p = value.toObject();
          require(p["covered_components"].toDouble() > 0 &&
                      p["maximum_error"].toDouble() <= p["tolerance"].toDouble() &&
                      p["channel"].toInt() != 3,
                  "BJ exported GPU pixels invalid");
        }
        checks.append(QJsonObject{
            {"stage", "gpu-flatten-export"},
            {"probes", probes},
            {"files",
             QJsonArray::fromStringList(dialog->property("flattenExportFiles").toStringList())}});
        dialog->accept();
        busy = false;
      }
      catch (const std::exception &e) {
        busy = false;
        fail(e.what());
      }
    });
    action("exportFlattenedGroups");
  }
  void arm_cycles(QJsonArray expected = {})
  {
    cycles_expected = expected.isEmpty() ? snapshot(true)["gpu"].toArray() : expected;
    require(!cycles_expected.isEmpty(), "BJ Cycles canonical GPU samples unavailable");
    cycles_revision = 0;
    for (const auto &value :
         snapshot()["queues"].toObject()["bj_cycles_mirrors"].toObject()["packets"].toArray())
      cycles_revision = std::max(cycles_revision,
                                 value.toObject()["published"].toString().toULongLong());
    cycles_publication = false;
    cycles_sample.clear();
    cycles_done = -1;
    cycles_wait = true;
    timer.setInterval(10);
  }
  bool cycles_ready()
  {
    const auto s = state();
    if (!s["render_ui"].toBool())
      return true;
    const auto render = s["render"].toObject();
    require(!render["image_mirror_paused"].toBool(), "BJ Cycles publication error");
    const auto status = render["status"].toString();
    require(!status.contains("error", Qt::CaseInsensitive), "BJ Cycles renderer error");
    if (!cycles_wait)
      return status.contains("Rendering Done");
    const auto mirrors = snapshot()["queues"].toObject()["bj_cycles_mirrors"].toObject();
    int matched = 0;
    const int role = kind == "uniform" ? 0 : 2;
    const auto current = snapshot();
    for (const auto &value : mirrors["packets"].toArray()) {
      const auto p = value.toObject();
      if (p["role"].toInt() != role || p["material"] != current["runtime_uid"] ||
          !p["current"].toBool() || p["published"].toString().toULongLong() <= cycles_revision)
        continue;
      const auto points = p["samples"].toArray();
      require(points.size() == 3, "BJ successful Cycles packet sample shape differs");
      for (const auto &packet : cycles_expected) {
        const auto canonical = packet.toObject();
        if (canonical["channel"].toInt() != role || canonical["tile"] != p["tile"])
          continue;
        const auto expected = canonical["points"].toArray();
        require(expected.size() == 5, "BJ Cycles expected packet shape absent");
        for (int i = 0; i < 3; ++i) {
          const auto rgba = points[i].toObject()["rgba"].toArray(),
                     want = expected[i].toObject()["rgba"].toArray();
          require(rgba.size() == 4 && want.size() == 4,
                  "BJ Cycles publication RGBA shape differs");
          for (int c = 0; c < 4; ++c)
            require(std::abs(rgba[c].toDouble() - want[c].toDouble()) < 3e-6,
                    "BJ successful current Cycles publication differs from canonical GPU pixels");
        }
        ++matched;
      }
    }
    cycles_publication = matched == 3;
    if (cycles_publication &&
        QRegularExpression("\\bSample [0-9]+/[0-9]+").match(status).hasMatch())
      cycles_sample = status;
    if (cycles_sample.isEmpty() || !status.contains("Rendering Done"))
      return false;
    if (cycles_done < 0) {
      cycles_done = s["render_frames"].toInt();
      return false;
    }
    return s["render_frames"].toInt() > cycles_done;
  }
  void cycles_capture(const char *name)
  {
    require(cycles_wait && cycles_publication && !cycles_sample.isEmpty() && cycles_done >= 0 &&
                state()["render_frames"].toInt() > cycles_done,
            "BJ publication -> Sample -> Done -> later draw incomplete");
    const auto image = capture_viewport_framebuffer(v).convertToFormat(QImage::Format_RGBA8888);
    require(!image.isNull() && image.save(out + "/" + name + ".png"),
            "BJ Cycles displayed frame absent");
    QJsonObject proof{
        {"stage", name},
        {"mirrors", snapshot()["queues"].toObject()["bj_cycles_mirrors"]},
        {"observed_sample_status", cycles_sample},
        {"done_frame", cycles_done},
        {"captured_frame", state()["render_frames"]},
        {"scope",
         "New successful publication -> actual Cycles Sample -> Rendering Done -> later displayed frame; no cross-engine RGB equality"}};
    if (!cycles_previous.isNull()) {
      require(image.size() == cycles_previous.size(), "BJ Cycles viewport extent changed");
      const QRect roi(image.width() * 35 / 100,
                      image.height() * 35 / 100,
                      image.width() * 30 / 100,
                      image.height() * 30 / 100);
      double delta = 0;
      int changed = 0;
      for (int y = roi.top(); y <= roi.bottom(); ++y)
        for (int x = roi.left(); x <= roi.right(); ++x) {
          int sum = 0;
          for (int c = 0; c < 3; ++c)
            sum += std::abs(int(image.constScanLine(y)[x * 4 + c]) -
                            int(cycles_previous.constScanLine(y)[x * 4 + c]));
          delta += sum / (3. * 255);
          changed += sum > 3;
        }
      delta /= roi.width() * roi.height();
      require(changed > 64 && delta > .002,
              "BJ projection edit did not change a completed Cycles frame");
      proof["mean_absolute_rgb"] = delta;
      proof["changed_pixels"] = changed;
    }
    cycles_previous = image;
    checks.append(proof);
    cycles_wait = false;
    timer.setInterval(120);
  }
  void warm_steps()
  {
    steps.push_back([this] { action("newProject"); });
    steps.push_back([this] {
      {
        auto *preset = get<QComboBox>("projectTemplate");
        const auto index = preset->findData(":pbr");
        require(index >= 0, "BJ real PBR template absent");
        preset->setCurrentIndex(index);
      }
      get<QLineEdit>("meshPath")->setText(mesh);
      toggle("preserveUvTiles", true);
      auto *size = get<QComboBox>("documentResolution");
      size->setCurrentIndex(size->findData(128));
      get<QPushButton>("createProject")->click();
    });
    steps.push_back([this] { channel_precision(0, int(ChannelPrecision::RGB32F)); });
    steps.push_back([this] { channel_precision(2, int(ChannelPrecision::L32F)); });
    steps.push_back([this] {
      combo("viewLayoutControl", 2);
      auto *shelf = get<QAction>("toggleShelf");
      if (!shelf->isChecked())
        shelf->trigger();
      auto *categories = get<QListWidget>("shelfCategories");
      const auto all = categories->findItems("All", Qt::MatchExactly);
      require(all.size() == 1, "BJ All Shelf category absent");
      categories->setCurrentItem(all.front());
      combo("shelfLibraryScope", 0);
      get<QLineEdit>("shelfSearch")->setText(material_name());
    });
    steps.push_back([this] {
      import_selection = true;
      QTimer::singleShot(0, this, [this] {
        try {
          action(kind == "uniform" ? "importBlendMaterialAction" :
                                     "importNativeBlendMaterialAction");
        }
        catch (const std::exception &e) {
          fail(e.what());
        }
      });
    });
    steps.push_back([this] {
      auto *list = get<QListWidget>("blendMaterialList");
      require(list->isEnabled(), "BJ source material enumeration pending");
      QListWidgetItem *chosen = nullptr;
      for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == material_name())
          chosen = list->item(i);
      require(chosen, "BJ owned material absent from import dialog");
      list->setCurrentItem(chosen);
      get<QPushButton>("importBlendMaterial")->click();
      import_selection = false;
    });
    steps.push_back([this] { shelf_apply(); });
    steps.push_back([this] {
      selected_ids();
      const auto s = snapshot(true);
      invariant(s, false);
      source_parameters = projection(s)["native_material"].toObject();
      require(s["native"].toBool() == (kind != "uniform"), "BJ actual asset path kind differs");
      uv_oracle(false);
      capture("initial-imported");
    });
    if (kind != "uniform") {
      steps.push_back([this] { combo("nativeHeightPolicy", 1, false); });
      steps.push_back([this] { channel_precision(4, int(ChannelPrecision::L32F)); });
      steps.push_back([this] {
        uv_oracle(true);
        combo("nativeHeightPolicy", 0, false);
      });
    }
    for (int policy = 0; policy < 2; ++policy) {
      steps.push_back([this] {
        policy_compilations = -1;
        policy_cache = {};
      });
      if (policy && kind != "uniform")
        steps.push_back([this] { combo("nativeHeightPolicy", 1, false); });
      if (policy && kind == "uniform") {
        steps.push_back([this] {
          combo("fillProjectionMode", 0);
          get<QPushButton>("fillResetUvTransforms")->click();
        });
        steps.push_back([this] { toggle("fillChannelEnabled4", true); });
        steps.push_back([this] { channel_precision(4, int(ChannelPrecision::L32F)); });
        steps.push_back([this] { ordinary_height_file(); });
        steps.push_back([this] { ordinary_height_oracle(); });
      }
      steps.push_back([this] {
        const auto s = snapshot();
        policy_compilations = s["compiled_shaders"].toInteger();
        if (kind != "uniform") {
          policy_cache = s["queues"].toObject()["bj_native_gpu"].toObject();
          require(policy_cache["sources"].isDouble() && policy_cache["programs"].isDouble() &&
                      policy_cache["dispatches"].isDouble(),
                  "BJ actual native cache counters missing");
        }
      });
      for (int mode = 0; mode < 7; ++mode) {
        steps.push_back([this, mode] { combo("fillProjectionMode", mode); });
        steps.push_back([this, mode] {
          require(projection(snapshot())["mode"].toInt() == mode,
                  "BJ actual projection mode mismatch");
          if (mode >= 1 && mode <= 5)
            toggle("fill3dDisclosure", true);
        });
        if (mode == 0) {
          edit_steps("uv-offset", [this] {
            number("fillOffset0", get<QDoubleSpinBox>("fillOffset0")->value() + .137);
          });
          edit_steps("uv-rotation", [this] {
            number("fillUvRotation", get<QDoubleSpinBox>("fillUvRotation")->value() + 23.);
          });
          edit_steps("uv-scale", [this] {
            number("fillScaleU", get<QDoubleSpinBox>("fillScaleU")->value() * 1.37);
          });
          steps.push_back([this] { toggle("fillShowManipulator", true); });
          edit_steps("uv-gizmo-drag", [this] { drag_handle("uv"); });
        }
        if (mode >= 1 && mode <= 5) {
          edit_steps(QString("mode%1-offset3").arg(mode),
                     [this, mode] { number("fill3d0_0", .071 + mode * .031); });
          edit_steps(QString("mode%1-rotation3").arg(mode),
                     [this, mode] { number("fill3d1_2", 11. + mode * 7.); });
          edit_steps(QString("mode%1-scale3").arg(mode),
                     [this, mode] { number("fill3d2_0", .71 + mode * .11); });
        }
        if (mode == 0) {
          for (int wrap = 1; wrap <= 3; ++wrap)
            edit_steps(
                QString("uv-wrap%1").arg(wrap), [this, wrap] { combo("fillWrap", wrap); }, false);
          steps.push_back([this] { combo("fillWrap", 0); });
        }
        if (mode == 1) {
          edit_steps("triplanar-hardness", [this] {
            number("fillTriplanarHardness",
                   get<QDoubleSpinBox>("fillTriplanarHardness")->value() > .5 ? .17 : .93);
          });
          steps.push_back([this] { toggle("fillShowManipulator", true); });
          edit_steps("spatial-gizmo-drag", [this] { drag_handle("spatial"); });
        }
        if (mode == 5) {
          steps.push_back([this] { combo("warpEdition", 1); });
          steps.push_back([this] {
            const auto p = projection(snapshot());
            require(p["mode"].toInt() == 5 && p["warp"].toObject()["edit"].toBool(),
                    "BJ real Warp edition did not publish Edit vertices");
            toggle("warpSurfaceSnap", false);
          });
          steps.push_back([this] {
            const auto s = snapshot(), p = projection(s);
            const auto warp = p["warp"].toObject();
            require(warp["edit"].toBool() && !warp["surface"].toBool() &&
                        p["manipulator"].toBool(),
                    "BJ actual Warp edit/surface/manipulator setup differs");
            checks.append(
                QJsonObject{{"stage", "warp-setup"},
                            {"projection", p},
                            {"gizmo_drag", s["gizmo_drag"]},
                            {"edition_control", get<QComboBox>("warpEdition")->currentIndex()},
                            {"surface_control", get<QCheckBox>("warpSurfaceSnap")->isChecked()}});
          });
          steps.push_back([this] {
            const auto s = state();
            const auto view = s["view"].toObject();
            QPoint center;
            bool found = false;
            for (const auto &value : view["panes"].toArray()) {
              const auto pane = value.toObject();
              if (pane["view"] != "3D")
                continue;
              const auto r = pane["rect"].toArray();
              require(r.size() == 4, "BJ real scene pane rectangle missing");
              center = {r[0].toInt() + r[2].toInt() / 2, r[1].toInt() + r[3].toInt() / 2};
              found = true;
              break;
            }
            require(found, "BJ real scene pane missing before Warp framing");
            const auto diagnostic = snapshot();
            warp_camera_mapping = projection(diagnostic);
            warp_camera_requested = diagnostic["gizmo_drag"].toObject()["domain"] != "warp";
            warp_camera_undo = s["undo_count"].toInteger();
            warp_camera_distance = view["scene_distance"].toDouble();
            if (!warp_camera_requested)
              return;
            v->setFocus(Qt::OtherFocusReason);
            mouse(v, QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
            key(v, Qt::Key_F);
          });
          steps.push_back([this] {
            const auto s = snapshot(), ui = state();
            const auto handle = s["gizmo_drag"].toObject();
            require(projection(s) == warp_camera_mapping &&
                        ui["undo_count"].toInteger() == warp_camera_undo,
                    "BJ camera framing changed authored mapping/history");
            const auto distance = ui["view"].toObject()["scene_distance"].toDouble();
            require(warp_camera_requested ? std::abs(distance - warp_camera_distance) > 1e-6 :
                                            std::abs(distance - warp_camera_distance) < 1e-6,
                    "BJ conditional Warp framing changed/no-op camera unexpectedly");
            require(handle["domain"] == "warp",
                    "BJ framed Warp still has no production hit-test handle");
            checks.append(QJsonObject{{"stage", "warp-real-camera-frame"},
                                      {"framing_requested", warp_camera_requested},
                                      {"before_distance", warp_camera_distance},
                                      {"after_distance", distance},
                                      {"mapping_and_undo_unchanged", true},
                                      {"handle", handle}});
          });
          edit_steps("warp-control-drag", [this] { drag_handle("warp"); });
        }
        if (mode < 6) {
          steps.push_back([this] { combo("fillScaleMode", 1); });
          edit_steps(QString("mode%1-physical-size").arg(mode),
                     [this, mode] { number("fillPhysicalSizeX", 37. + mode * 13.); });
          steps.push_back([this] { combo("fillScaleMode", 0); });
        }
        if (mode == 6) {
          steps.push_back([this] {
            before_gpu = snapshot(true)["gpu"].toArray();
            combo("fillProjectionMode", 0);
          });
          steps.push_back([this] {
            number("fillOffset1", get<QDoubleSpinBox>("fillOffset1")->value() + .213);
          });
          steps.push_back([this] { combo("fillProjectionMode", 6); });
          steps.push_back([this] {
            const auto s = snapshot(true);
            const auto error = difference(before_gpu, s["gpu"].toArray());
            require(error < 2e-6, "BJ mode6 reapplied nonidentity Fill 2D transform");
            checks.append(QJsonObject{{"stage", "mode6-ignores-fill-2d"},
                                      {"projection", projection(s)},
                                      {"max_error", error},
                                      {"tolerance", 2e-6}});
          });
        }
        steps.push_back([this, policy, mode] { checkpoint(policy, mode); });
        steps.push_back(
            [this, policy, mode] { capture(QString("policy%1-mode%2").arg(policy).arg(mode)); });
      }
    }
    // Representative actual spatial native/uniform renderer and capture path.
    steps.push_back([this] { combo("fillProjectionMode", 1); });
    edit_steps("cycles-spatial-edit", [this] { number("fill3d0_0", .47); });
    steps.push_back([this] { export_files(); });
    steps.push_back([this] {
      policy_compilations = -1;
      policy_cache = {};
      arm_cycles();
      action("renderingMode");
      get<QSpinBox>("renderSamples")->setValue(1024);
      toggle("renderDenoise", false);
    });
    steps.push_back([this] {
      cycles_capture("cycles-edited");
      arm_cycles(before_gpu);
      action("undoDocument");
    });
    steps.push_back([this] {
      cycles_capture("cycles-undo");
      arm_cycles(edited_gpu);
      action("redoDocument");
    });
    steps.push_back([this] {
      cycles_capture("cycles-redo");
      action("paintingMode");
    });
    steps.push_back([this] {
      before = snapshot(true);
      before_gpu = before["gpu"].toArray();
      undo_before = state()["undo_count"].toInteger();
      action("flattenLayer");
    });
    steps.push_back([this] {
      const auto s = snapshot(true);
      const auto undo = state()["undo_count"].toInteger();
      const auto console = get<QPlainTextEdit>("consoleLog")->toPlainText().right(1500);
      const bool installed = s["layer"] != before["layer"] && !s["native"].toBool() &&
                             s["layer_count"].toInteger() ==
                                 before["layer_count"].toInteger() + 1 &&
                             undo == undo_before + 1;
      checks.append(QJsonObject{{"stage", "flatten-installation"},
                                {"before_layer", before["layer"]},
                                {"after_layer", s["layer"]},
                                {"before_layer_count", before["layer_count"]},
                                {"after_layer_count", s["layer_count"]},
                                {"before_undo", undo_before},
                                {"after_undo", undo},
                                {"installed", installed},
                                {"reported_console_tail", console}});
      if (!installed)
        throw std::runtime_error(
            ("BJ Flatten did not install a bitmap layer with exactly one Undo: " + console)
                .toStdString());
      require(difference(before_gpu, s["gpu"].toArray()) < 3e-5,
              "BJ GPU Flatten changed final samples");
      checks.append(QJsonObject{{"stage", "gpu-flatten"},
                                {"max_error", difference(before_gpu, s["gpu"].toArray())}});
      action("undoDocument");
    });
    steps.push_back([this] {
      const auto s = snapshot();
      require(s["layer"] == before["layer"] && s["layer_count"] == before["layer_count"] &&
                  s["native"] == before["native"] && projection(s) == projection(before) &&
                  s["edit_state"] == before["edit_state"] &&
                  state()["undo_count"].toInteger() == undo_before,
              "BJ Flatten Undo did not restore exact authored source/mapping/history");
      if (s["height_policy"].toInt() == int(NativeHeightPolicy::RawAuthoredFloatV1)) {
        auto *panel = get<QWidget>("nativeImageReplacements");
        require(!panel->property("nativeImageInspecting").toBool() &&
                    !w->property("nativeImageLoading").toBool() &&
                    state()["workspace_error"].toString().isEmpty(),
                "BJ passive RawHeight restoration started image work or raised an error");
        require(
            !get<QPushButton>("nativeImageFile")->isEnabled() &&
                !get<QPushButton>("nativeImageShelf")->isEnabled() &&
                !get<QPushButton>("nativeImageResetAll")->isEnabled() &&
                get<QLabel>("nativeImageStatus")->text().contains("Disable raw authored Height"),
            "BJ restored RawHeight image controls are not locally restricted");
        checks.append(QJsonObject{{"stage", "passive-raw-height-image-refresh"},
                                  {"workspace_error", false},
                                  {"image_job", false},
                                  {"replacement_controls_disabled", true}});
      }
      capture("flatten-undone");
    });
  }
  void cold_steps()
  {
    QFile file(qEnvironmentVariable("BTS_BJ_PROJECTION_EXPECTED"));
    require(file.open(QIODevice::ReadOnly), "BJ warm checkpoint manifest missing");
    const auto expected = QJsonDocument::fromJson(file.readAll()).object();
    require(expected["failure"].toString().isEmpty() && expected["kind"].toString() == kind,
            "BJ warm predecessor failed/wrong kind");
    checkpoints = expected["checkpoints"].toArray();
    if (!checkpoints.isEmpty())
      await_project = checkpoints.first().toObject()["project"].toString();
    require(checkpoints.size() == 14, "BJ all mapping/policy checkpoints required");
    for (int index = 0; index < checkpoints.size(); ++index) {
      const auto checkpoint = checkpoints[index].toObject();
      if (index)
        steps.push_back([this, checkpoint] {
          await_project = checkpoint["project"].toString();
          w->setProperty("bjProject", await_project);
          action("guiBjOpen");
        });
      steps.push_back([this, checkpoint] {
        w->setProperty("bjProject", checkpoint["project"].toString());
        action("guiBjValidateOpen");
        const auto proof = w->property("bjOpenProof").toJsonObject();
        require(proof["source_bytes_parameters_and_resolved_pixels_equal"].toBool(),
                "BJ cold independent packed data failed");
        set_id = QUuid(checkpoint["set"].toString());
        layer_id = QUuid(checkpoint["layer"].toString());
        select_persistent_layer();
        checks.append(QJsonObject{{"stage", "packed-data-before-explicit-gpu-read"},
                                  {"checkpoint", checkpoint["name"]},
                                  {"proof", proof}});
      });
      steps.push_back([this, checkpoint] {
        selected_ids();
        const auto s = snapshot(true);
        invariant(s, false);
        require(projection(s) == checkpoint["projection"].toObject(),
                "BJ packed authored projection differs");
        const double error = difference(checkpoint["gpu"].toArray(), s["gpu"].toArray());
        require(error < 3e-6, "BJ cold fresh-cache GPU output differs from warm checkpoint");
        checks.append(QJsonObject{{"stage", "cold-gpu-checkpoint"},
                                  {"mode", checkpoint["mode"]},
                                  {"policy", checkpoint["policy"]},
                                  {"max_error", error},
                                  {"tolerance", 3e-6}});
        capture(checkpoint["name"].toString() + "-cold");
      });
    }
  }
  void handle_dialogs()
  {
    try {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *file = qobject_cast<QFileDialog *>(widget);
            file && file->isVisible() &&
            (file->objectName() == "blendMaterialFileDialog" ||
             file->objectName() == "fillTextureFileDialog"))
        {
          const bool height = file->objectName() == "fillTextureFileDialog";
          require(!height || height_file_picker, "BJ unexpected Height chooser");
          const auto source = qEnvironmentVariable(height ? "BTS_BJ_PROJECTION_HEIGHT" :
                                                            "BTS_BJ_PROJECTION_SOURCE");
          require(!cold && !source.isEmpty(), "BJ unexpected source chooser in cold process");
          if (!file->property("bjPrepared").toBool()) {
            file->setProperty("bjPrepared", true);
            file->setDirectory(QFileInfo(source).absolutePath());
          }
          auto *name = file->findChild<QLineEdit *>("fileNameEdit");
          require(name, "BJ source filename editor missing");
          name->setText(source);
          QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
        }
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical)
            throw std::runtime_error(box->text().toStdString());
          if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
      }
    }
    catch (const std::exception &e) {
      fail(e.what());
    }
  }
  void advance()
  {
    if (busy)
      return;
    try {
      if (++ticks > 24000)
        throw std::runtime_error("BJ UI diagnostic timeout");
      v->update();
      const auto s = state();
      for (const char *key : {"material_error", "open_error", "save_error", "workspace_error"})
        require(s[key].toString().isEmpty(), qPrintable(s[key].toString()));
      const bool new_dialog = !cold && step == 1, import_dialog = !cold && import_selection;
      if (s["height_pending"].toBool() || s["native_source_pending"].toBool() ||
          s["open_pending"].toBool() || s["save_pending"].toBool() ||
          s["texture_loading"].toBool() || s["composite_pending"].toBool() ||
          s["property_pending"].toBool() || w->property("blendMaterialImporting").toBool() ||
          s["layer_thumbnails_pending"].toBool())
        return;
      // CLI Open is first admitted after three actual viewport frames. Preserve all
      // errors/pending checks, then wait for a real document, never a fixed delay.
      if (cold && (!s["has_document"].toBool() ||
                   QFileInfo(s["document_path"].toString()).absoluteFilePath() !=
                       QFileInfo(await_project).absoluteFilePath()))
        return;
      if (import_dialog) {
        auto *list = w->findChild<QListWidget *>("blendMaterialList");
        if (!list || !list->isEnabled())
          return;
      }
      if (!new_dialog && !import_dialog &&
          (QApplication::activeModalWidget() || QApplication::activePopupWidget()))
        return;
      if (!new_dialog && !import_dialog && !w->isActiveWindow()) {
        if (w->windowHandle())
          w->windowHandle()->requestActivate();
        return;
      }
      if (!cycles_ready() || (!new_dialog && !import_dialog && s["render_frames"].toInt() < frame))
        return;
      if (step == int(steps.size())) {
        timer.stop();
        dialogs.stop();
        write();
        std::printf("BTS_BJ_PROJECTION_PASS phase=%s kind=%s checkpoints=%lld\n",
                    qPrintable(phase),
                    qPrintable(kind),
                    static_cast<long long>(checkpoints.size()));
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        return;
      }
      std::fprintf(stderr, "BTS_BJ_PROJECTION_STEP %d\n", step);
      busy = true;
      steps[step++]();
      busy = false;
      frame = state()["render_frames"].toInt() + 3;
      write();
    }
    catch (const std::exception &e) {
      busy = false;
      fail(e.what());
    }
  }

 public:
  NativeMaterialProjectionDemo(QWidget *workspace,
                               QWidget *viewport,
                               QString directory,
                               QString source,
                               std::function<QJsonObject()> callback)
      : QObject(workspace),
        w(workspace),
        v(viewport),
        out(std::move(directory)),
        mesh(std::move(source)),
        kind(qEnvironmentVariable("BTS_BJ_PROJECTION_KIND")),
        phase(qEnvironmentVariable("BTS_BJ_PROJECTION_PHASE")),
        state(std::move(callback))
  {
    require(kind == "UV" || kind == "Object" || kind == "Generated" || kind == "uniform",
            "BJ material kind invalid");
    cold = phase == "cold";
    require(cold || phase == "warm", "BJ phase invalid");
    QDir().mkpath(out);
    cpu_baseline = state();
    for (const char *key : {"cpu_materializations", "cpu_layer_compositions"})
      require(cpu_baseline[key].isDouble(), "BJ actual process CPU counters missing");
    if (cold)
      cold_steps();
    else
      warm_steps();
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start(120);
    connect(&dialogs, &QTimer::timeout, this, [this] { handle_dialogs(); });
    dialogs.start(30);
  }
};
}  // namespace bts
