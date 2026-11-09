// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewDemo.hh"
#include "ViewportDemoCapture.hh"
#include "PaintRecording.hh"
#include <algorithm>
#include <QTreeWidget>
#include <cmath>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QProgressBar>
#include <QLabel>
#include <QSaveFile>
#include <QScopeGuard>
#include <stdexcept>
#include <cstdio>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTimer>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QScreen>
#include <QListWidget>
#include <QKeyEvent>
#include <QDir>
#include <QElapsedTimer>
namespace bts {
namespace {
// Separate bounded legacy Configure history sequence; default skew proof stays intact.
class HistoryDemo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  std::function<QJsonObject()> state;
  QTimer timer, dialogs;
  QElapsedTimer lifetime;
  int stage = 0, modal_phase = 0;
  bool busy = false, change_distance = false;
  double distance = .01;
  QString operation;
  QJsonObject modal_before;
  QJsonArray captures, choices;
  template<class T> T *get(const char *id)
  {
    auto *value = w->findChild<T *>(id);
    check(value, "Configure history control missing");
    return value;
  }
  static void check(bool value, const char *message)
  {
    if (!value)
      throw std::runtime_error(message);
  }
  void action(const char *id)
  {
    get<QAction>(id)->trigger();
  }
  QJsonObject recipe(const QJsonObject &s) const
  {
    return s["document"].toObject()["bake_configuration"].toObject();
  }
  QJsonArray layers(const QJsonObject &s) const
  {
    return s["document"].toObject()["texture_sets"].toArray()[0].toObject()["layers"].toArray();
  }
  QJsonArray maps(const QJsonObject &s) const
  {
    return s["document"].toObject()["texture_sets"].toArray()[0].toObject()["mesh_maps"].toArray();
  }
  QJsonObject captured(const char *name) const
  {
    for (const auto &entry : captures)
      if (entry.toObject()["stage"] == name)
        return entry.toObject();
    throw std::runtime_error("Configure history baseline missing");
  }
  QJsonObject admission(const QJsonObject &s) const
  {
    QJsonObject result;
    for (const char *key : {"document", "document_revision", "undo_count", "redo_count"}) {
      check(!s[key].isUndefined(), "Configure history snapshot field missing");
      result[key] = s[key];
    }
    return result;
  }
  void retain()
  {
    QSaveFile file(dir + "/configure-history-observed.json");
    check(file.open(QIODevice::WriteOnly) &&
              file.write(QJsonDocument(QJsonObject{{"classification", "observed/incomplete"},
                                                   {"captures", captures},
                                                   {"dialog_choices", choices}})
                             .toJson()) >= 0 &&
              file.commit(),
          "Cannot retain Configure history observations");
  }
  void capture(const char *name, const QJsonObject &s)
  {
    auto value = s;
    value["stage"] = name;
    captures.append(value);
    retain();
  }
  void unchanged(const QJsonObject &s)
  {
    check(admission(s) == modal_before,
          "Configure Cancel/unchanged Save changed document/revision/history");
  }
  void mouse(QEvent::Type type, QPointF point)
  {
    QMouseEvent event(type,
                      point,
                      v->mapToGlobal(point.toPoint()),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(v, &event);
  }
  void open(const char *op, double wanted, bool edit = false, bool legacy = false)
  {
    modal_before = admission(state());
    operation = op;
    distance = wanted;
    change_distance = edit;
    modal_phase = 0;
    if (legacy)
      action("bakeMeshMapsAction");
    else
      get<QPushButton>("configureSkewBaking")->click();
    check(modal_phase == 2, "Configure modal did not finish through its actual controls");
    operation.clear();
  }
  void modal()
  {
    if (operation.isEmpty())
      return;
    check(lifetime.elapsed() < 180000, "Configure history modal timed out");
    if (auto *dialog = w->findChild<QDialog *>("meshBakeDialog"); dialog && dialog->isVisible()) {
      if (modal_phase == 2)
        return;
      auto *field = dialog->findChild<QDoubleSpinBox *>("bakeFrontalDistance");
      check(field, "Configure frontal control missing");
      if (modal_phase == 0) {
        for (auto *parent = field->parentWidget(); parent; parent = parent->parentWidget())
          if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
            scroll->ensureWidgetVisible(field);
            break;
          }
        modal_phase = 1;
        return;
      }
      check(field->isVisible() && field->isEnabled() && !field->visibleRegion().isEmpty(),
            "Configure frontal control is clipped or disabled");
      const double before = field->value();
      if (change_distance) {
        field->setFocus();
        auto *text = field->findChild<QLineEdit *>();
        check(text, "Configure distance input missing");
        QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a"),
            input(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier, QString::number(distance, 'g', 8));
        QApplication::sendEvent(text, &select);
        QApplication::sendEvent(text, &input);
        field->clearFocus();
      }
      check(std::abs(field->value() - distance) < 1e-9,
            "Reopened Configure cache or actual parameter input differs");
      if (operation == "bake") {
        auto *size = dialog->findChild<QComboBox *>("bakeResolution");
        check(size, "Configure resolution missing");
        size->setCurrentIndex(size->findData(32));
        dialog->findChild<QCheckBox *>("bakeUseProjectMesh")->setChecked(true);
        auto *roles = dialog->findChild<QListWidget *>("bakeMapTypes");
        check(roles && roles->count() == 7, "Configure map roles missing");
        for (int i = 0; i < 7; ++i)
          roles->item(i)->setCheckState(i == 0 ? Qt::Checked : Qt::Unchecked);
      }
      choices.append(QJsonObject{{"operation", operation},
                                 {"before", before},
                                 {"displayed", field->value()},
                                 {"changed", change_distance},
                                 {"actual_visible_control", true}});
      retain();
      if (operation == "save" || operation == "bake")
        check(dialog->grab().save(dir + "/configure-history-" + QString::number(choices.size()) +
                                  ".png"),
              "Cannot retain actual Configure controls");
      modal_phase = 2;
      if (operation == "cancel" || operation == "inspect") {
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        check(buttons && buttons->button(QDialogButtonBox::Cancel), "Configure Cancel missing");
        buttons->button(QDialogButtonBox::Cancel)->click();
      }
      else {
        auto *button = dialog->findChild<QPushButton *>(operation == "bake" ? "startMeshBake" :
                                                                              "saveBakeSettings");
        check(button && button->isEnabled(), "Configure acceptance control disabled");
        button->click();
      }
      return;
    }
    if (auto *progress = w->findChild<QDialog *>("meshBakeProgress");
        progress && progress->isVisible())
    {
      auto *finish = progress->findChild<QPushButton *>("finishMeshBake");
      check(finish, "Bake progress control missing");
      if (!finish->isEnabled())
        return;
      if (finish->text() == "Close") {
        auto *status = progress->findChild<QLabel *>("meshBakeProgressStatus");
        throw std::runtime_error(
            (status ? status->text() : QString("Mesh Bake failed")).toStdString());
      }
      if (finish->text() != "OK")
        return;
      auto *bar = progress->findChild<QProgressBar *>("bakeProgressBar");
      check(bar && bar->maximum() > 0 && bar->value() == bar->maximum(),
            "Bake progress did not complete every task");
      finish->click();
    }
  }
  void advance()
  {
    if (busy)
      return;
    check(lifetime.elapsed() < 180000, "Configure history actor timed out");
    auto current = state();
    if (stage >= 2 &&
        (current["composite_pending"].toBool() || current["property_pending"].toBool() ||
         current["skew"].toObject()["pending"].toBool()))
      return;
    if (stage == 3 && layers(current) == layers(captured("unpainted")))
      return;
    busy = true;
    const auto release = qScopeGuard([&] { busy = false; });
    std::fprintf(stderr, "PD150_HISTORY_STAGE %d\n", stage);
    if (stage > 3) {
      check(layers(current) == layers(captured("painted")),
            "Bake/mode history changed retained Paint data");
      check(current["source_obj_hash"] == captured("painted")["source_obj_hash"],
            "Configure history changed original geometry/UV source");
    }
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(128));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        check(recipe(current).isEmpty(), "Configure history needs an initially empty recipe");
        capture("unpainted", current);
        action("guiSkewPrepare");
        action("selectPaintTool");
        get<QDoubleSpinBox>("brushSize")->setValue(18);
        const auto xy = state()["skew_uv_point"].toArray();
        check(xy.size() == 2, "Paint point missing");
        const QPointF at(xy[0].toDouble(), xy[1].toDouble());
        mouse(QEvent::MouseButtonPress, at);
        mouse(QEvent::MouseMove, at + QPointF(8, 0));
        mouse(QEvent::MouseButtonRelease, at + QPointF(8, 0));
        break;
      }
      case 3:
        check(layers(current) != layers(captured("unpainted")) &&
                  current["undo_count"].toInt() == captured("unpainted")["undo_count"].toInt() + 1,
              "Actual Paint gesture did not add one history entry");
        capture("painted", current);
        action("enterBakingMode");
        break;
      case 4:
        check(current["skew"].toObject()["mode"] == 1 &&
                  current["skew"].toObject()["auto_role"] == -1,
              "Baking entry did not preserve Auto Off");
        capture("baking-empty", current);
        open("noop", .01);
        break;
      case 5:
        unchanged(current);
        capture("unchanged-save", current);
        open("save", .125, true, true);
        break;
      case 6:
        check(recipe(current)["frontal"] == .125 &&
                  current["undo_count"].toInt() ==
                      captured("baking-empty")["undo_count"].toInt() + 1,
              "Configure Save did not commit exactly one parameter history entry");
        check(get<QDoubleSpinBox>("skewFrontalDistance")->value() == .125,
              "Direct Configure Save left the Baking panel stale");
        check(maps(current) == maps(captured("painted")), "Save settings installed mesh maps");
        capture("settings-saved", current);
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 7:
        capture("painting", current);
        action("undoDocument");
        break;
      case 8:
        check(current["skew"].toObject()["mode"] == 1 && recipe(current)["frontal"] == .125,
              "Undo must restore Baking before its parameter edit");
        capture("undo-mode", current);
        action("undoDocument");
        break;
      case 9:
        check(recipe(current) == recipe(captured("baking-empty")),
              "Undo Save did not restore exact old recipe");
        capture("undo-settings", current);
        open("inspect", .01);
        break;
      case 10:
        unchanged(current);
        capture("cache-old", current);
        action("undoDocument");
        break;
      case 11:
        check(recipe(current).isEmpty() && current["skew"].toObject()["mode"] == 0,
              "Undo Baking did not restore initially empty recipe");
        capture("undo-entry", current);
        open("noop", .01, false, true);
        break;
      case 12:
        unchanged(current);
        capture("empty-noop-save", current);
        action("redoDocument");
        break;
      case 13:
        check(recipe(current) == recipe(captured("baking-empty")),
              "Redo Baking entry changed recipe");
        capture("redo-entry", current);
        action("redoDocument");
        break;
      case 14:
        check(recipe(current) == recipe(captured("settings-saved")),
              "Redo Save did not restore exact recipe");
        capture("redo-settings", current);
        open("inspect", .125);
        break;
      case 15:
        unchanged(current);
        capture("cache-new", current);
        action("redoDocument");
        break;
      case 16:
        check(recipe(current) == recipe(captured("painting")),
              "Redo Painting changed accepted recipe/mode");
        capture("redo-painting", current);
        action("undoDocument");
        break;
      case 17:
        capture("branch-baking", current);
        action("undoDocument");
        break;
      case 18:
        check(recipe(current) == recipe(captured("baking-empty")),
              "Branch baseline recipe differs");
        capture("branch-old", current);
        open("save", .25, true);
        break;
      case 19:
        check(recipe(current)["frontal"] == .25 && current["redo_count"] == 0,
              "New Configure edit did not clear Redo");
        capture("branch-saved", current);
        open("cancel", .5, true);
        break;
      case 20:
        unchanged(current);
        capture("cancelled", current);
        open("inspect", .25);
        break;
      case 21:
        unchanged(current);
        capture("cancel-cache", current);
        action("undoDocument");
        break;
      case 22:
        check(recipe(current) == recipe(captured("branch-old")),
              "Branched Save Undo changed recipe");
        capture("branch-undone", current);
        action("redoDocument");
        break;
      case 23:
        check(recipe(current) == recipe(captured("branch-saved")),
              "Branched Save Redo changed recipe");
        capture("branch-redone", current);
        open("bake", .375, true);
        break;
      case 24:
        check(recipe(current)["frontal"] == .375 && recipe(current)["resolution"] == 32 &&
                  maps(current) != maps(captured("branch-redone")) &&
                  current["undo_count"].toInt() ==
                      captured("branch-redone")["undo_count"].toInt() + 1,
              "Completed Bake did not publish one recipe+maps entry");
        capture("baked", current);
        action("undoDocument");
        break;
      case 25:
        check(recipe(current) == recipe(captured("branch-redone")) &&
                  maps(current) == maps(captured("branch-redone")),
              "Undo completed Bake lost old recipe/maps");
        capture("bake-undone", current);
        open("inspect", .25);
        break;
      case 26:
        unchanged(current);
        capture("bake-old-cache", current);
        action("redoDocument");
        break;
      case 27:
        check(recipe(current) == recipe(captured("baked")) &&
                  maps(current) == maps(captured("baked")),
              "Redo completed Bake changed accepted recipe/maps");
        capture("bake-redone", current);
        open("inspect", .375);
        break;
      case 28:
        unchanged(current);
        capture("bake-new-cache", current);
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 29: {
        check(current["skew"].toObject()["mode"] == 0, "Final Painting mode missing");
        capture("finished", current);
        timer.stop();
        dialogs.stop();
        QSaveFile file(dir + "/configure-history.json");
        check(file.open(QIODevice::WriteOnly) &&
                  file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                                       {"captures", captures},
                                                       {"dialog_choices", choices},
                                                       {"actual_controls", true},
                                                       {"normal_bake_resolution", 32},
                                                       {"performance_claim", false}})
                                 .toJson()) >= 0 &&
                  file.commit(),
              "Cannot retain Configure history proof");
        std::puts("PD150_CONFIGURE_HISTORY PASS");
        QCoreApplication::quit();
        return;
      }
    }
  }
  void fail(const std::exception &error)
  {
    timer.stop();
    dialogs.stop();
    QSaveFile file(dir + "/configure-history-failure.json");
    if (file.open(QIODevice::WriteOnly)) {
      file.write(QJsonDocument(QJsonObject{{"error", QString::fromUtf8(error.what())},
                                           {"stage", stage},
                                           {"captures", captures},
                                           {"dialog_choices", choices}})
                     .toJson());
      file.commit();
    }
    std::fprintf(stderr, "PD150_CONFIGURE_HISTORY_FAILED %s\n", error.what());
    QCoreApplication::exit(1);
  }

 public:
  HistoryDemo(QWidget *window,
              QWidget *viewport,
              QString directory,
              QString path,
              std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        dir(std::move(directory)),
        mesh(std::move(path)),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    lifetime.start();
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &error) {
        fail(error);
      }
    });
    dialogs.setInterval(20);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      try {
        modal();
      }
      catch (const std::exception &error) {
        fail(error);
      }
    });
    dialogs.start();
    timer.start();
  }
};
// Independent two-set map-publication/history acceptance. Fixture setup is not
// authoring proof; only the actual Configure Bake and Undo/Redo are under test.
class MultiSetHistoryDemo final : public QObject {
  QWidget *w;
  QString dir, mesh;
  std::function<QJsonObject()> state;
  QTimer timer, dialogs;
  QElapsedTimer lifetime;
  int stage = 0;
  bool busy = false, choosing = false, accepted = false;
  QJsonArray captures;
  QJsonObject latest, sync_checks;
  static void check(bool value, const char *message)
  {
    if (!value)
      throw std::runtime_error(message);
  }
  template<class T> T *get(const char *name)
  {
    auto *value = w->findChild<T *>(name);
    check(value, "Multiset history control missing");
    return value;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  QJsonObject captured(const char *name) const
  {
    for (const auto &entry : captures)
      if (entry.toObject()["stage"] == name)
        return entry.toObject();
    throw std::runtime_error("Multiset history capture missing");
  }
  void write(const QString &name, const QJsonObject &value)
  {
    QSaveFile file(dir + "/" + name);
    check(file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(value).toJson()) >= 0 &&
              file.commit(),
          "Cannot retain multiset history observations");
  }
  void retain()
  {
    write("multiset-history-observed.json",
          {{"classification", "observed/incomplete"},
           {"stage", stage},
           {"captures", captures},
           {"latest", latest},
           {"synchronous_history_checks", sync_checks}});
  }
  QJsonObject observe()
  {
    w->setProperty("mapHistoryCapture", true);
    const auto clear = qScopeGuard([&] { w->setProperty("mapHistoryCapture", false); });
    latest = state();
    retain();
    return latest;
  }
  static QJsonArray rows(const QJsonObject &s)
  {
    return s["map_history_sets"].toArray();
  }
  // Independent nearest-map scalar/white-premultiplied oracle, not a CPU layer
  // composite. The snapshot retains actual source RGBA and exact integer coords.
  bool pixels_match(const QJsonObject &s, bool old)
  {
    const auto sets = rows(s);
    check(sets.size() == 2, "Multiset probe needs exactly two native UIDs");
    bool match = true;
    for (int i = 0; i < 2; ++i) {
      const auto set = sets[i].toObject();
      check(set["precision"] == "RGB32F" && set["map_size"].toArray() == QJsonArray{32, 32},
            "Multiset target/map dimensions or precision changed");
      const auto probes = set["probes"].toArray();
      check(probes.size() == 3, "Multiset GPU packet shape differs");
      for (const auto &entry : probes) {
        const auto probe = entry.toObject();
        const auto raw = probe["raw"].toArray(), gpu = probe["gpu"].toArray(),
                   xy = probe["xy"].toArray(), map_xy = probe["map_xy"].toArray();
        check(raw.size() == 4 && gpu.size() == 4 && xy.size() == 2 && map_xy.size() == 2,
              "Multiset scalar packet incomplete");
        for (int c = 0; c < 4; ++c)
          check(raw[c].isDouble() && gpu[c].isDouble() && std::isfinite(raw[c].toDouble()) &&
                    std::isfinite(gpu[c].toDouble()),
                "Multiset scalar packet nonfinite");
        check(raw[3].toDouble() == 1, "Multiset nearest-map probe is not fully covered");
        for (int c = 0; c < 2; ++c)
          check(map_xy[c].toInt() == int((float(xy[c].toInt()) + .5f) / 128.f * 32.f),
                "Multiset nearest-map coordinate differs");
        const float input = float(raw[0].toDouble()) / float(raw[3].toDouble());
        const float expected = std::clamp((input - .5f) * 1.f + .5f + 0.f, 0.f, 1.f);
        if (old)
          check(input == (i == 0 ? .25f : .75f),
                "Multiset old AO fixture differs from independent scalar");
        for (int c = 0; c < 4; ++c)
          match = std::abs(gpu[c].toDouble() - double(expected)) <= 3e-5 && match;
      }
    }
    return match;
  }
  void capture(const char *name, const QJsonObject &s)
  {
    auto row = s;
    row["stage"] = name;
    captures.append(row);
    retain();
  }
  void history(const char *name)
  {
    const auto before = state();
    const auto materializations = bts::local_paint_materializations;
    {
      bts::GpuPersistenceScope guard;
      bts::PaintRecordingScope recording;
      action(name);
    }
    const auto after = state();
    check(after["padding_calls"] == before["padding_calls"] &&
              bts::local_paint_materializations == materializations,
          "Synchronous map history invoked padding or authored CPU pixel evaluation");
    sync_checks[name] = QJsonObject{
        {"padding_calls_before", before["padding_calls"]},
        {"padding_calls_after", after["padding_calls"]},
        {"materializations_before", qint64(materializations)},
        {"materializations_after", qint64(bts::local_paint_materializations)}};
  }
  void modal()
  {
    if (!choosing)
      return;
    check(lifetime.elapsed() < 180000, "Multiset Bake dialog timed out");
    if (auto *dialog = w->findChild<QDialog *>("meshBakeDialog");
        dialog && dialog->isVisible() && !accepted)
    {
      auto *sets = dialog->findChild<QTreeWidget *>("bakeTextureSets");
      auto *roles = dialog->findChild<QListWidget *>("bakeMapTypes");
      check(sets && sets->topLevelItemCount() == 2 && roles && roles->count() == 7,
            "Actual Bake set/role controls differ");
      for (int i = 0; i < 2; ++i) {
        check(sets->topLevelItem(i)->flags() & Qt::ItemIsEnabled, "A tested set is disabled");
        sets->topLevelItem(i)->setCheckState(0, Qt::Checked);
      }
      for (int i = 0; i < 7; ++i)
        roles->item(i)->setCheckState(i == 3 ? Qt::Checked : Qt::Unchecked);
      auto *size = dialog->findChild<QComboBox *>("bakeResolution");
      auto *lock = dialog->findChild<QCheckBox *>("bakeDimensionsLocked");
      auto *source = dialog->findChild<QCheckBox *>("bakeUseProjectMesh");
      auto *rays = dialog->findChild<QSpinBox *>("bakeSecondaryRays");
      check(size && lock && source && rays, "Actual Bake options missing");
      lock->setChecked(true);
      size->setCurrentIndex(size->findData(32));
      source->setChecked(true);
      rays->setValue(8);
      check(size->currentData().toInt() == 32 && rays->value() == 8,
            "Actual Bake options rejected");
      check(dialog->grab().save(dir + "/multiset-configure.png"),
            "Cannot retain actual two-set Bake controls");
      auto *start = dialog->findChild<QPushButton *>("startMeshBake");
      check(start && start->isEnabled(), "Actual AO Bake is unavailable");
      accepted = true;
      start->click();
      return;
    }
    if (auto *progress = w->findChild<QDialog *>("meshBakeProgress");
        progress && progress->isVisible())
    {
      auto *finish = progress->findChild<QPushButton *>("finishMeshBake");
      check(finish, "Actual Bake progress button missing");
      if (!finish->isEnabled())
        return;
      if (finish->text() == "Close") {
        auto *status = progress->findChild<QLabel *>("meshBakeProgressStatus");
        throw std::runtime_error(
            (status ? status->text() : QString("Multiset AO Bake failed")).toStdString());
      }
      if (finish->text() != "OK")
        return;
      auto *bar = progress->findChild<QProgressBar *>("bakeProgressBar");
      check(bar && bar->maximum() > 0 && bar->value() == bar->maximum(),
            "Actual multiset Bake did not complete");
      finish->click();
    }
  }
  void advance()
  {
    if (busy)
      return;
    check(lifetime.elapsed() < 180000,
          "Multiset history timed out waiting for both GPU consumers");
    auto current = state();
    if (stage >= 3 &&
        (current["composite_pending"].toBool() || current["property_pending"].toBool() ||
         current["skew"].toObject()["pending"].toBool()))
      return;
    busy = true;
    const auto release = qScopeGuard([&] { busy = false; });
    if (stage >= 3) {
      current = observe();
      if (!pixels_match(current, stage == 3 || stage == 4 || stage == 6))
        return;
    }
    if (stage >= 4) {
      check(current["selected_uid"] == captured("seeded")["selected_uid"],
            "Map history switched the selected set");
      check(current["source_obj_hash"] == captured("seeded")["source_obj_hash"],
            "Map history changed original mesh/UV source");
      check(current["cpu_materializations"] == captured("seeded")["cpu_materializations"],
            "Map history materialized an authored CPU image");
      check(current["shader_failures"].toString().isEmpty(), "Map history GPU shader failure");
    }
    std::fprintf(stderr, "PD150_MULTISET_STAGE %d\n", stage);
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(128));
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        action("guiSeedMultiSetMapHistory");
        break;
      case 3:
        check(current["map_history_seeded"].toBool(), "Map history fixture setup missing");
        check(current["selected_uid"] == rows(current)[0].toObject()["uid"],
              "Fixture must keep A selected");
        capture("seeded", current);
        action("enterBakingMode");
        break;
      case 4:
        check(current["skew"].toObject()["mode"] == 1 &&
                  current["skew"].toObject()["auto_role"] == -1,
              "Multiset Bake requires Baking with Auto Off");
        capture("before-bake", current);
        choosing = true;
        accepted = false;
        action("bakeMeshMapsAction");
        choosing = false;
        check(accepted, "Actual Configure Bake was not accepted");
        break;
      case 5: {
        const auto before = captured("before-bake");
        check(current["undo_count"].toInt() == before["undo_count"].toInt() + 1,
              "Multiset Bake did not make exactly one Undo entry");
        for (int i = 0; i < 2; ++i)
          check(rows(current)[i].toObject()["pixel_hash"] !=
                    rows(before)[i].toObject()["pixel_hash"],
                "Actual Bake did not replace both old AO maps");
        const auto recipe = current["document"].toObject()["bake_configuration"].toObject();
        check(recipe["resolution"] == 32 && recipe["height"] == 32 && recipe["rays"] == 8 &&
                  recipe["project_mesh"].toBool() &&
                  recipe["enabled"].toArray() ==
                      QJsonArray{false, false, false, true, false, false, false},
              "Actual AO Bake recipe differs");
        capture("baked", current);
        history("undoDocument");
        break;
      }
      case 6:
        check(current["document"] == captured("before-bake")["document"] &&
                  current["undo_count"] == captured("before-bake")["undo_count"] &&
                  current["redo_count"] == 1,
              "Undo did not restore both sets/recipe in one entry");
        capture("undone", current);
        history("redoDocument");
        break;
      case 7:
        check(current["document"] == captured("baked")["document"] &&
                  current["undo_count"] == captured("baked")["undo_count"] &&
                  current["redo_count"] == 0,
              "Redo did not restore both baked sets/recipe");
        capture("redone", current);
        timer.stop();
        dialogs.stop();
        write("multiset-history.json",
              {{"pid", QCoreApplication::applicationPid()},
               {"captures", captures},
               {"synchronous_history_checks", sync_checks},
               {"ao_bake_resolution", 32},
               {"both_sets_without_selection", true},
               {"performance_claim", false}});
        std::puts("PD150_MULTISET_HISTORY PASS");
        QCoreApplication::quit();
        break;
    }
  }
  void fail(const std::exception &error)
  {
    timer.stop();
    dialogs.stop();
    write("multiset-history-failure.json",
          {{"classification", "failed/incomplete"},
           {"error", QString::fromUtf8(error.what())},
           {"stage", stage},
           {"latest", latest},
           {"captures", captures}});
    std::fprintf(stderr, "PD150_MULTISET_HISTORY_FAILED %s\n", error.what());
    QCoreApplication::exit(1);
  }

 public:
  MultiSetHistoryDemo(QWidget *window,
                      QString directory,
                      QString path,
                      std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        dir(std::move(directory)),
        mesh(std::move(path)),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    lifetime.start();
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &error) {
        fail(error);
      }
    });
    dialogs.setInterval(20);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      try {
        modal();
      }
      catch (const std::exception &error) {
        fail(error);
      }
    });
    dialogs.start();
    timer.start();
  }
};
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int resolution, stage = 0, moves = 0;
  bool active = false;
  QPointF origin;
  QTimer timer, pulse, ping;
  QElapsedTimer heartbeat;
  double max_ping = 0;
  QJsonArray captures;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *id)
  {
    auto *value = w->findChild<T *>(id);
    if (!value)
      qFatal("Skew control missing: %s", id);
    return value;
  }
  void action(const char *id)
  {
    get<QAction>(id)->trigger();
  }
  void mouse(QEvent::Type type, QPointF point)
  {
    QMouseEvent event(type,
                      point,
                      v->mapToGlobal(point.toPoint()),
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(v, &event);
  }
  void capture(const char *label)
  {
    auto s = state();
    s["stage"] = label;
    s["paint_button_enabled"] = get<QPushButton>("paintSkewCorrection")->isEnabled();
    s["paint_button_tooltip"] = get<QPushButton>("paintSkewCorrection")->toolTip();
    if (QString(label) == "painted-3d" || QString(label) == "edges-on" ||
        QString(label) == "vectors" || QString(label) == "opened")
    {
      const auto image = capture_demo_window(w, v);
      image.save(dir + "/" + label + ".png");
      int colored = 0;
      const QPoint at = v->mapTo(w, QPoint(0, 0));
      const double scale = image.devicePixelRatio();
      for (int y = 0; y < v->height(); ++y)
        for (int x = 0; x < v->width(); ++x) {
          const auto c = image.pixelColor(qRound((at.x() + x) * scale),
                                          qRound((at.y() + y) * scale));
          if (c.red() > 100 && c.green() < c.red() / 2 && c.blue() < c.red() / 2)
            ++colored;
          else if (c.green() > 100 && c.red() < c.green() / 2 && c.blue() < c.green() / 2)
            ++colored;
        }
      s["vector_colored_pixels"] = colored;
    }
    captures.append(s);
  }
  void stroke(const char *key)
  {
    const auto p = state()[key].toArray();
    origin = {p[0].toDouble(), p[1].toDouble()};
    moves = 0;
    active = true;
    heartbeat.start();
    ping.start();
    mouse(QEvent::MouseButtonPress, origin);
    pulse.start();
  }
  void advance()
  {
    if (active)
      return;
    auto current = state();
    if (stage > 5 &&
        (current["composite_pending"].toBool() || current["skew"].toObject()["pending"].toBool()))
      return;
    switch (stage++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2:
        if (current["composite_pending"].toBool()) {
          --stage;
          return;
        }
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        if (current["composite_pending"].toBool()) {
          --stage;
          return;
        }
        action("guiSkewPrepare");
        action("enterBakingMode");
        capture("project-source-disabled");
        break;
      case 4:
        get<QLineEdit>("skewHighFiles")->setText(qEnvironmentVariable("BTS_SKEW_HIGH_SOURCE"));
        QMetaObject::invokeMethod(
            get<QLineEdit>("skewHighFiles"), "editingFinished", Qt::DirectConnection);
        get<QCheckBox>("skewUseProjectMesh")->setChecked(false);
        get<QCheckBox>("skewRelativeDistances")->setChecked(false);
        get<QDoubleSpinBox>("skewFrontalDistance")->setValue(.2);
        get<QDoubleSpinBox>("skewRearDistance")->setValue(.2);
        get<QCheckBox>("skewAverageNormals")->setChecked(false);
        capture("average-disabled");
        get<QCheckBox>("skewAverageNormals")->setChecked(true);
        get<QCheckBox>("skewUseCage")->setChecked(true);
        capture("cage-disabled");
        get<QCheckBox>("skewUseCage")->setChecked(false);
        break;
      case 5:
        get<QPushButton>("paintSkewCorrection")->click();
        break;
      case 6:
        capture("initial");
        get<QDoubleSpinBox>("brushSize")->setValue(20);
        stroke("skew_uv_point");
        break;
      case 7:
        capture("painted-uv");
        action("selectPaintTool");
        get<QDoubleSpinBox>("brushSize")->setValue(18);
        stroke("skew_scene_point");
        break;
      case 8:
        capture("painted-3d");
        action("selectEraserTool");
        get<QDoubleSpinBox>("brushSize")->setValue(22);
        stroke("skew_uv_point");
        break;
      case 9:
        capture("erased");
        action("undoDocument");
        break;
      case 10:
        capture("undo-erase");
        action("redoDocument");
        break;
      case 11:
        capture("redo-erase");
        action("selectPolygonFill");
        break;
      case 12: {
        const auto p = state()["skew_polygon_point"].toArray();
        QPointF at(p[0].toDouble(), p[1].toDouble());
        mouse(QEvent::MouseButtonPress, at);
        mouse(QEvent::MouseButtonRelease, at);
        break;
      }
      case 13:
        capture("polygon");
        get<QCheckBox>("skewEdgeProtection")->setChecked(false);
        break;
      case 14:
        capture("edges-off");
        get<QCheckBox>("skewEdgeProtection")->setChecked(true);
        break;
      case 15:
        capture("edges-on");
        get<QSpinBox>("skewVectorDensity")->setValue(32);
        get<QDoubleSpinBox>("skewVectorLength")->setValue(.15);
        get<QDoubleSpinBox>("skewVectorOpacity")->setValue(.6);
        break;
      case 16: {
        QKeyEvent invert(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
        action("selectPaintTool");
        QCoreApplication::sendEvent(v, &invert);
        if (get<QDoubleSpinBox>("skewPaintValue")->value() != 0)
          qFatal("Skew X did not invert grayscale");
        QCoreApplication::sendEvent(v, &invert);
      }
        capture("vectors");
        get<QCheckBox>("skewEnabled")->setChecked(false);
        break;
      case 17:
        capture("disabled");
        get<QCheckBox>("skewEnabled")->setChecked(true);
        break;
      case 18:
        capture("enabled");
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 19:
        capture("painting-mode");
        action("undoDocument");
        break;
      case 20:
        capture("undo-mode");
        action("checkGuiComposite");
        action("guiSkewReference");
        break;
      case 21:
        capture("reference");
        get<QCheckBox>("skewAutoRebake")->setChecked(false);
        get<QCheckBox>("skewEdgeProtection")->setChecked(false);
        break;
      case 22:
        capture("manual-before");
        get<QPushButton>("rebakeSkewNormal")->click();
        break;
      case 23:
        capture("manual-baked");
        get<QPushButton>("leaveBakingMode")->click();
        break;
      case 24:
        action("undoDocument");
        break;
      case 25:
        action("undoDocument");
        break;
      case 26:
        capture("undo-manual");
        action("redoDocument");
        break;
      case 27:
        capture("redo-manual");
        get<QCheckBox>("skewEdgeProtection")->setChecked(true);
        get<QCheckBox>("skewAutoRebake")->setChecked(true);
        break;
      case 28:
        action("guiSkewSave");
        break;
      case 29:
        capture("saved");
        action("guiSkewOpen");
        break;
      case 30:
        capture("opened");
        action("newProject");
        break;
      case 31: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(qEnvironmentVariable("BTS_SKEW_SETS_LOW"));
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(resolution));
        d->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 32:
        action("guiSkewPrepare");
        action("enterBakingMode");
        get<QLineEdit>("skewHighFiles")->setText(qEnvironmentVariable("BTS_SKEW_SETS_HIGH"));
        QMetaObject::invokeMethod(
            get<QLineEdit>("skewHighFiles"), "editingFinished", Qt::DirectConnection);
        get<QCheckBox>("skewUseProjectMesh")->setChecked(false);
        get<QCheckBox>("skewRelativeDistances")->setChecked(false);
        get<QDoubleSpinBox>("skewFrontalDistance")->setValue(.2);
        get<QDoubleSpinBox>("skewRearDistance")->setValue(.2);
        get<QPushButton>("paintSkewCorrection")->click();
        break;
      case 33:
        capture("sets-initial");
        get<QDoubleSpinBox>("brushSize")->setValue(20);
        stroke("skew_uv_point");
        break;
      case 34:
        break;  // stroke release switches the selection before the bake debounce fires
      case 35:
        capture("sets-second");
        get<QDoubleSpinBox>("brushSize")->setValue(20);
        stroke("skew_uv_point");
        break;
      case 36:
        break;
      case 37:
        capture("sets-finished");
        action("guiSkewSave");
        break;
      case 38:
        capture("sets-saved");
        action("guiSkewOpen");
        break;
      case 39: {
        capture("sets-opened");
        timer.stop();
        QFile f(dir + "/skew.json");
        if (!f.open(QIODevice::WriteOnly))
          qFatal("Skew proof unavailable");
        f.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                          {"captures", captures},
                                          {"stroke_max_ping_ms", max_ping}})
                    .toJson());
        f.close();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
  }

 public:
  Demo(QWidget *window,
       QWidget *viewport,
       QString d,
       QString path,
       int size,
       std::function<QJsonObject()> get_state)
      : QObject(window),
        w(window),
        v(viewport),
        dir(d),
        mesh(path),
        resolution(size),
        state(std::move(get_state))
  {
    QDir().mkpath(dir);
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    pulse.setInterval(25);
    connect(&pulse, &QTimer::timeout, this, [this] {
      if (++moves > 20) {
        mouse(QEvent::MouseButtonRelease, origin + QPointF(10, 0));
        if (stage == 34 || stage == 36) {
          auto *sets = get<QListWidget>("textureSets");
          sets->setCurrentRow(stage == 34 ? 1 : 0);
        }
        pulse.stop();
        ping.stop();
        active = false;
        return;
      }
      mouse(QEvent::MouseMove, origin + QPointF(moves * .5, std::sin(moves * .2) * 3));
    });
    ping.setInterval(10);
    ping.setTimerType(Qt::PreciseTimer);
    connect(&ping, &QTimer::timeout, this, [this] {
      max_ping = std::max(max_ping, double(heartbeat.restart()));
    });
    timer.start();
  }
};
}  // namespace
void start_skew_demo(QWidget *w,
                     QWidget *v,
                     const QString &dir,
                     const QString &mesh,
                     int size,
                     std::function<QJsonObject()> state)
{
  if (qEnvironmentVariableIsSet("BTS_BAKE_MULTISET_HISTORY_DEMO"))
    new MultiSetHistoryDemo(w, dir, mesh, std::move(state));
  else if (qEnvironmentVariableIsSet("BTS_BAKE_SETTINGS_HISTORY_DEMO"))
    new HistoryDemo(w, v, dir, mesh, std::move(state));
  else
    new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
