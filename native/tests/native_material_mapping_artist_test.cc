// SPDX-License-Identifier: GPL-3.0-or-later
// Actual artist Mapping widgets, frame queue and Undo/Redo; production main is unchanged.
#define main bts_native_artist_main
#define start_gui_fix_demo start_native_mapping_artist_demo
#ifndef BTS_NATIVE_MAPPING_MAIN
#  define BTS_NATIVE_MAPPING_MAIN "../host/main.cc"
#endif
#include BTS_NATIVE_MAPPING_MAIN
#undef start_gui_fix_demo
#undef main
#include "GPU_pass.hh"
#include "ViewportDemoCapture.hh"
#include <QKeyEvent>
namespace bts {
namespace {
class NativeMappingArtist final : public QObject {
  QWidget *window, *viewport;
  QString output, mesh, library;
  std::function<QJsonObject()> snapshot;
  QTimer timer, dialogs;
  int stage = 0, ticks = 0, after_frame = 0, history = 0, last_logged_stage = -1;
  bool busy = false, unused_rejected = false;
  QJsonObject rejection_before;
  QJsonArray captures;
  std::uint64_t compilations = 0, recipes = 0;
  QJsonObject source_descriptor;
  template<class T> T *control(const char *name)
  {
    auto *widget = window->findChild<T *>(name);
    if (!widget)
      throw std::runtime_error(std::string("Missing artist control: ") + name);
    return widget;
  }
  static void require(bool ok, const char *message)
  {
    if (!ok)
      throw std::runtime_error(message);
  }
  void action(const char *name)
  {
    control<QAction>(name)->trigger();
  }
  void save_as()
  {
    for (auto *value : window->findChildren<QAction *>()) {
      auto text = value->text();
      text.remove('&');
      if (text.startsWith("Save As")) {
        busy = true;
        value->trigger();
        busy = false;
        return;
      }
    }
    throw std::runtime_error("Artist Save As action missing");
  }
  QDoubleSpinBox *mapping_editor(const char *name = "Mapping B")
  {
    const auto state = snapshot();
    for (const auto &set : state["document"].toObject()["texture_sets"].toArray())
      for (const auto &layer : set.toObject()["layers"].toArray())
        for (const auto &value : layer.toObject()["fill_projection"]
                                     .toObject()["native_material"]
                                     .toObject()["parameters"]
                                     .toArray())
        {
          const auto p = value.toObject();
          if (p["name"] == name)
            return window->findChild<QDoubleSpinBox *>("nativeMapping_" + p["id"].toString() +
                                                       "_Offset_0");
        }
    return nullptr;
  }
  void proof()
  {
    timer.stop();
    dialogs.stop();
    const bool cold = qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_REOPEN");
    QJsonObject evidence{{"production_main_callbacks", true},
                         {"input",
                          cold ? "Normal Open QAction, private source hidden, empty cache" :
                                 "Linked Mapping offset Qt key events; actual Undo/Redo QActions"},
                         {"captures", captures},
                         {"precision", "L32F"},
                         {"native_gpu_oracle", true},
                         {"cold_open", cold}};
    if (!cold) {
      evidence["one_gesture"] = true;
      evidence["zero_shader_recompiles"] = true;
      evidence["cpu_recipe_materializations"] = 0;
      evidence["async_save"] = true;
      evidence["unused_mapping_rejected_atomically"] = unused_rejected;
    }
    QFile file(output + "/native-mapping-artist.json");
    require(file.open(QIODevice::WriteOnly), "Cannot write native artist proof");
    file.write(QJsonDocument(evidence).toJson());
    file.close();
    std::puts("BTS_NATIVE_MAPPING_ARTIST PASS");
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
  }
  QJsonObject selected_native(const QJsonObject &state)
  {
    for (const auto &set : state["document"].toObject()["texture_sets"].toArray())
      for (const auto &layer : set.toObject()["layers"].toArray()) {
        const auto description = layer.toObject()["fill_projection"].toObject()["native_material"];
        if (description.isObject())
          return description.toObject();
      }
    throw std::runtime_error("Artist document has no native fill");
  }
  double offset(const QJsonObject &description)
  {
    for (const auto &value : description["parameters"].toArray()) {
      const auto parameter = value.toObject();
      if (parameter["name"] == "Mapping B")
        return parameter.contains("mapping_transform") ? parameter["mapping_transform"]
                                                             .toObject()["offset"]
                                                             .toArray()
                                                             .first()
                                                             .toDouble() :
                                                         0;
    }
    throw std::runtime_error("Linked Mapping B input missing");
  }
  void capture(const char *name, double expected)
  {
    auto state = snapshot();
    const auto description = selected_native(state);
    require(std::abs(offset(description) - expected) < 1e-6,
            "Artist parameter value was not committed");
    const auto samples = state["material_gpu_samples"].toArray();
    require(samples.size() == 3 && samples[2].toArray().size() == 4,
            "Artist GPU sample unavailable");
    const auto sample = samples[2].toArray();
    const double source = qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_UPSTREAM") ?
                              (64.5 / 128) * .5 + .125 :
                              (64.5 / 128);
    const double error = std::abs(sample[0].toDouble() - (source + expected));
    if (error > 2e-6)
      throw std::runtime_error(
          QString("Native Mapping artist GPU stale: stage=%1 descriptor=%2 GPU=%3 error=%4")
              .arg(name)
              .arg(expected, 0, 'g', 9)
              .arg(sample[0].toDouble(), 0, 'g', 9)
              .arg(error, 0, 'g', 9)
              .toStdString());
    require(sample[3].toDouble() > .999, "Native artist fill lost coverage");
    if (!captures.isEmpty())
      require(state["material_gpu_samples"].toArray()[0] ==
                  captures[0].toObject()["material_gpu_samples"].toArray()[0],
              "Editing Mapping B changed the independent Mapping A color branch");
    state["stage"] = name;
    state["native_descriptor"] = description;
    state["gpu_error"] = error;
    captures.append(state);
    capture_viewport_framebuffer(viewport).save(output + "/" + name + ".png");
  }
  void advance()
  {
    if (busy)
      return;
    if (stage != last_logged_stage) {
      std::fprintf(stderr, "BTS_NATIVE_MAPPING_ARTIST_STAGE %d\n", stage);
      last_logged_stage = stage;
    }
    if (++ticks > 1800)
      throw std::runtime_error("Native artist edit timed out");
    const auto state = snapshot();
    if (stage > 1 && (state["composite_pending"].toBool() || state["property_pending"].toBool() ||
                      state["render_frames"].toInt() < after_frame))
      return;
    if (window->property("blendMaterialImporting").toBool())
      return;
    action("guiAeState");
    const auto io = window->property("aeState").value<QJsonObject>();
    require(io["save_error"].toString().isEmpty(), "Artist async Save failed");
    require(window->property("projectOpenError").toString().isEmpty(), "Artist async Open failed");
    if (io["save_pending"].toBool())
      return;
    if (auto *progress = window->findChild<QProgressDialog *>("projectOpenProgress");
        progress && progress->isVisible())
      return;
    switch (stage) {
      case 0:
        ++stage;
        action("newProject");
        return;
      case 1: {
        auto *dialog = control<QDialog>("newProjectDialog");
        dialog->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *size = dialog->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(128));
        ++stage;
        busy = true;
        control<QPushButton>("createProject")->click();
        busy = false;
        break;
      }
      case 2:
        control<QComboBox>("channelPrecision2")->setCurrentIndex(int(ChannelPrecision::L32F));
        ++stage;
        break;
      case 3:
        ++stage;
        QTimer::singleShot(0, this, [this] { action("importNativeBlendMaterialAction"); });
        break;
      case 4: {
        auto *list = window->findChild<QListWidget *>("blendMaterialList");
        if (!list || !list->isEnabled())
          return;
        QListWidgetItem *chosen = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->data(Qt::UserRole) == "Linked Mapping fixture")
            chosen = list->item(i);
        require(chosen, "Native fixture material not listed");
        list->setCurrentItem(chosen);
        ++stage;
        control<QPushButton>("importBlendMaterial")->click();
        break;
      }
      case 5: {
        auto *list = control<QListWidget>("shelfAssets");
        QListWidgetItem *chosen = nullptr;
        for (int i = 0; i < list->count(); ++i)
          if (list->item(i)->text() == "Linked Mapping fixture")
            chosen = list->item(i);
        if (!chosen)
          return;
        list->setCurrentItem(chosen);
        list->itemDoubleClicked(chosen);
        ++stage;
        break;
      }
      case 6: {
        auto *number = mapping_editor();
        if (!number)
          return;
        if (captures.isEmpty()) {
          capture("initial", 0);
          source_descriptor = selected_native(snapshot());
          history = snapshot()["undo_count"].toInt();
          compilations = blender::GPU_pass_global_compilation_count();
          recipes = PaintPixels::recipe_materializations();
        }
        if (qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_UPSTREAM") && !unused_rejected) {
          rejection_before = snapshot();
          auto *unused = mapping_editor("Mapping C");
          require(unused, "Unused Mapping fixture control missing");
          unused->setValue(.3);
          stage = 40;
          break;
        }
        number->setFocus();
        auto *edit = number->findChild<QLineEdit *>();
        require(edit, "Native numeric editor missing");
        QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, "a");
        QApplication::sendEvent(edit, &select);
        QKeyEvent text(QEvent::KeyPress, Qt::Key_0, Qt::NoModifier, "0.2");
        QApplication::sendEvent(edit, &text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(edit, &enter);
        ++stage;
        break;
      }
      case 40: {
        const auto after = snapshot();
        QJsonArray changed;
        for (const char *key : {"document",
                                "undo_count",
                                "redo_count",
                                "document_revision",
                                "native_mapping_queues",
                                "native_mapping_catalog",
                                "native_mapping_property"})
          if (after[key] != rejection_before[key])
            changed.append(key);
        QFile admission(output + "/mapping-admission.json");
        require(admission.open(QIODevice::WriteOnly), "Cannot write Mapping admission snapshots");
        admission.write(QJsonDocument(QJsonObject{{"changed_keys", changed},
                                                  {"before", rejection_before},
                                                  {"after", after}})
                            .toJson());
        admission.close();
        if (!changed.isEmpty())
          throw std::runtime_error(
              "Rejected unused Mapping edit changed: " +
              QJsonDocument(changed).toJson(QJsonDocument::Compact).toStdString());
        require(after["native_mapping_error"].toString().contains("unused") &&
                    blender::GPU_pass_global_compilation_count() == compilations &&
                    PaintPixels::recipe_materializations() == recipes,
                "Unused Mapping error was not explicit/atomic");
        auto *released = mapping_editor("Mapping C");
        require(released && released->value() == 0,
                "Rejected Mapping control did not resynchronize");
        QMetaObject::invokeMethod(released, "editingFinished", Qt::DirectConnection);
        stage = 41;
        break;
      }
      case 41: {
        const auto after = snapshot();
        for (const char *key : {"document",
                                "undo_count",
                                "redo_count",
                                "document_revision",
                                "native_mapping_queues",
                                "native_mapping_catalog",
                                "native_mapping_property"})
          require(after[key] == rejection_before[key],
                  "Rejected Mapping value committed later on release/idle");
        QFile admission(output + "/mapping-admission.json");
        require(admission.open(QIODevice::ReadOnly), "Mapping admission proof missing");
        auto evidence = QJsonDocument::fromJson(admission.readAll()).object();
        admission.close();
        evidence["after_release_idle"] = after;
        require(admission.open(QIODevice::WriteOnly), "Cannot update Mapping admission proof");
        admission.write(QJsonDocument(evidence).toJson());
        admission.close();
        unused_rejected = true;
        stage = 6;
        break;
      }
      case 7:
        capture("edited", .2);
        require(snapshot()["undo_count"].toInt() == history + 1,
                "Native numeric edit was not one history gesture");
        ++stage;
        action("undoDocument");
        break;
      case 8:
        capture("undone", 0);
        require(snapshot()["undo_count"].toInt() == history, "Native Undo history count changed");
        require(selected_native(snapshot()) == source_descriptor,
                "Native Undo did not restore exact descriptor");
        ++stage;
        action("redoDocument");
        break;
      case 9: {
        capture("redone", .2);
        require(snapshot()["undo_count"].toInt() == history + 1,
                "Native Redo history count changed");
        require(blender::GPU_pass_global_compilation_count() == compilations,
                "Artist Mapping edit/Undo/Redo recompiled native shaders");
        require(PaintPixels::recipe_materializations() == recipes,
                "Artist native edit materialized CPU paint recipes");
        if (qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_UPSTREAM")) {
          stage = 30;
          for (const auto &entry : selected_native(snapshot())["parameters"].toArray()) {
            const auto p = entry.toObject();
            if (p["name"] == "Mapping B") {
              auto *reset = window->findChild<QPushButton *>("nativeMappingReset_" +
                                                             p["id"].toString());
              require(reset, "Mapping Reset control missing");
              reset->click();
              break;
            }
          }
          break;
        }
        QFile descriptor(output + "/edited-descriptor.json");
        require(descriptor.open(QIODevice::WriteOnly), "Cannot save Mapping descriptor oracle");
        descriptor.write(QJsonDocument(selected_native(snapshot())).toJson());
        descriptor.close();
        ++stage;
        save_as();
        break;
      }
      case 10:
        require(QFileInfo(qEnvironmentVariable("BTS_NATIVE_MAPPING_PROJECT")).size() > 0,
                "Artist Save As did not finish");
        proof();
        return;
      case 30:
        capture("reset", 0);
        require(selected_native(snapshot()) == source_descriptor &&
                    snapshot()["undo_count"].toInt() == history + 2,
                "Reset did not restore source descriptor in one step");
        stage = 31;
        action("undoDocument");
        break;
      case 31: {
        capture("reset-undone", .2);
        require(snapshot()["undo_count"].toInt() == history + 1 &&
                    blender::GPU_pass_global_compilation_count() == compilations &&
                    PaintPixels::recipe_materializations() == recipes,
                "Reset/Undo recompiled shaders or materialized CPU content");
        QFile descriptor(output + "/edited-descriptor.json");
        require(descriptor.open(QIODevice::WriteOnly), "Cannot save Mapping descriptor oracle");
        descriptor.write(QJsonDocument(selected_native(snapshot())).toJson());
        descriptor.close();
        stage = 10;
        save_as();
        break;
      }
      case 20:
        ++stage;
        busy = true;
        action("openProject");
        busy = false;
        break;
      case 21: {
        if (!mapping_editor() || state["texture_loading"].toBool())
          return;
        capture("cold-packed-open", .2);
        QFile oracle(qEnvironmentVariable("BTS_NATIVE_MAPPING_DESCRIPTOR"));
        require(oracle.open(QIODevice::ReadOnly), "Cold Open descriptor oracle missing");
        require(selected_native(snapshot()) == QJsonDocument::fromJson(oracle.readAll()).object(),
                "Cold Open changed authored Mapping/source descriptor");
        proof();
        return;
      }
    }
    after_frame = snapshot()["render_frames"].toInt() + 8;
  }

 public:
  NativeMappingArtist(QWidget *w,
                      QWidget *v,
                      QString directory,
                      QString source,
                      std::function<QJsonObject()> state)
      : QObject(w),
        window(w),
        viewport(v),
        output(std::move(directory)),
        mesh(std::move(source)),
        library(qEnvironmentVariable("BTS_BLEND_MATERIAL_SOURCE")),
        snapshot(std::move(state))
  {
    if (qEnvironmentVariableIsSet("BTS_NATIVE_MAPPING_REOPEN"))
      stage = 20;
    connect(&timer, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &error) {
        timer.stop();
        dialogs.stop();
        std::fprintf(stderr, "BTS_NATIVE_MAPPING_ARTIST_FAILED %s\n", error.what());
        QCoreApplication::exit(1);
      }
    });
    timer.start(100);
    connect(&dialogs, &QTimer::timeout, this, [this] {
      for (auto *widget : QApplication::topLevelWidgets()) {
        if (auto *file = dynamic_cast<QFileDialog *>(widget); file && file->isVisible()) {
          const bool project = file->windowTitle().contains("Save project") ||
                               file->windowTitle().contains("Open project");
          if (project || file->objectName() == "blendMaterialFileDialog") {
            const auto path = project ? qEnvironmentVariable("BTS_NATIVE_MAPPING_PROJECT") :
                                        library;
            require(!path.isEmpty(), "Mapping chooser target missing");
            const auto key = file->windowTitle() + path;
            if (file->property("mappingTestSelection").toString() != key) {
              file->setDirectory(QFileInfo(path).absolutePath());
              file->setProperty("mappingTestSelection", key);
              std::fprintf(stderr,
                           "BTS_NATIVE_MAPPING_CHOOSER title=%s target=%s\n",
                           qPrintable(file->windowTitle()),
                           qPrintable(path));
            }
            auto *name = file->findChild<QLineEdit *>("fileNameEdit");
            require(name, "Mapping chooser filename editor missing");
            if (name->text() != path)
              name->setText(path);
            QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
          }
        }
        if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible()) {
          if (box->icon() == QMessageBox::Critical) {
            std::fprintf(
                stderr, "BTS_NATIVE_MAPPING_ARTIST_DIALOG_FAILED %s\n", qPrintable(box->text()));
            QCoreApplication::exit(1);
          }
          else if (auto *discard = box->button(QMessageBox::Discard))
            discard->click();
        }
      }
    });
    dialogs.start(30);
  }
};
}  // namespace
void start_native_mapping_artist_demo(QWidget *workspace,
                                      QWidget *viewport,
                                      const QString &directory,
                                      const QString &mesh,
                                      int,
                                      std::function<QJsonObject()> state)
{
  new NativeMappingArtist(workspace, viewport, directory, mesh, std::move(state));
}
}  // namespace bts
int main(int argc, char **argv)
{
  return bts_native_artist_main(argc, argv);
}
