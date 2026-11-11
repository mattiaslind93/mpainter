// SPDX-License-Identifier: GPL-3.0-or-later
#include "ViewportDemoCapture.hh"
#include "DerivedExportDemo.hh"
#include <QAction>
#include <array>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include "ViewportWidget.hh"
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString dir, mesh;
  int size, phase = 0, ticks = 0, after_frame = 0;
  QTimer timer;
  QJsonArray captures, results;
  std::function<QJsonObject()> state;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("Derived export control missing: %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *name)
  {
    auto data = state();
    data["stage"] = name;
    captures.append(data);
    QFile file(dir + "/derived-export.json");
    if (!file.open(QIODevice::WriteOnly))
      qFatal("Derived GUI proof unavailable");
    file.write(QJsonDocument(QJsonObject{{"pid", QCoreApplication::applicationPid()},
                                         {"captures", captures},
                                         {"exports", results}})
                   .toJson());
    file.close();
    capture_viewport_framebuffer(v).save(dir + "/" + name + "-viewport.png");
  }
  void paint()
  {
    const auto point = state()["skew_uv_point"].toArray();
    const QPointF from(point[0].toDouble(), point[1].toDouble());
    const auto send = [&](QEvent::Type type, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs) {
      QMouseEvent event(type, p, v->mapToGlobal(p.toPoint()), b, bs, Qt::NoModifier);
      QCoreApplication::sendEvent(v, &event);
    };
    send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 12; ++i)
      send(QEvent::MouseMove, from + QPointF(i * 2, i * .2), Qt::NoButton, Qt::LeftButton);
    send(QEvent::MouseButtonRelease, from + QPointF(24, 2.4), Qt::LeftButton, Qt::NoButton);
  }
  void export_maps(bool converted, bool reopened = false)
  {
    timer.stop();
    QTimer::singleShot(100, this, [this, converted, reopened] {
      auto *dialog = get<QDialog>("textureExportDialog");
      const QString name = reopened  ? "reopened" :
                           converted ? "converted" :
                                       "direct",
                    output = dir + "/" + name;
      QDir().mkpath(output);
      dialog->findChild<QLineEdit *>("exportDirectory")->setText(output);
      auto *sets = dialog->findChild<QListWidget *>("exportTextureSets");
      for (int i = 0; i < sets->count(); ++i)
        sets->item(i)->setCheckState(sets->item(i)->text().contains("Sphere") ? Qt::Checked :
                                                                                Qt::Unchecked);
      const bool jpeg = qEnvironmentVariableIsSet("BTS_JPEG_EXPORT_DEMO");
      dialog->findChild<QComboBox *>("exportFormat")->setCurrentIndex(jpeg ? 2 : 0);
      auto *precision = dialog->findChild<QComboBox *>("exportPrecision");
      precision->setCurrentIndex(precision->findData(jpeg ? 4 : 1));
      auto *resolution = dialog->findChild<QComboBox *>("exportSize");
      resolution->setCurrentIndex(resolution->findData(512));
      dialog->findChild<QComboBox *>("exportPadding")->setCurrentIndex(4);
      dialog->findChild<QSpinBox *>("exportPaddingWidth")->setValue(16);
      auto *presets = dialog->findChild<QComboBox *>("exportPreset");
      const bool usd = qEnvironmentVariableIsSet("BTS_USD_EXPORT_DEMO");
      const int selected =
          converted ? presets->findText(
                          usd ? "USD PBR Metal Roughness" :
                                "PBR Specular Glossiness (converted from Metallic Roughness)") :
                      0;
      if (selected < 0)
        qFatal("Converted preset missing");
      presets->setCurrentIndex(selected);
      if (converted && usd && qEnvironmentVariableIsSet("BTS_USD_DISPLACE_DEMO"))
        dialog->findChild<QCheckBox *>("exportUsdDisplacement")->setChecked(true);
      if (qEnvironmentVariableIsSet("BTS_EXPORT_DEPTH_DEMO")) {
        auto *list = dialog->findChild<QListWidget *>("exportPresets");
        auto *tabs = dialog->findChild<QTabWidget *>("exportTabs");
        tabs->setCurrentIndex(1);
        if (converted) {
          list->setCurrentRow(selected);
          const int maps = dialog->findChild<QTableWidget *>("exportDerivedOutputs")->rowCount();
          for (int n = 0; n < maps; ++n) {
            auto *depth = dialog->findChild<QComboBox *>(QString("exportDerivedFormat%1").arg(n));
            if (!depth)
              qFatal("Derived depth picker missing");
            depth->setCurrentIndex(depth->findData(n + 1 < maps ? n : -1));
          }
          // Also exercise the independent packed-output precision and Displacement source.
          dialog->findChild<QPushButton *>("newPackedExportPreset")->click();
          auto *depth = dialog->findChild<QComboBox *>("exportPackedFormat");
          depth->setCurrentIndex(depth->findData(2));
          auto *red = dialog->findChild<QComboBox *>("exportPackedR");
          red->setCurrentIndex(red->findData(7));
          list->setCurrentRow(selected);
        }
        else {
          list->setCurrentRow(0);
          dialog->findChild<QPushButton *>("duplicateExportPreset")->click();
          const int row = list->currentRow();
          for (int n = 0; n < 9; ++n) {
            auto *depth = dialog->findChild<QComboBox *>(QString("exportChannelFormat%1").arg(n));
            depth->setCurrentIndex(
                depth->findData(std::array<int, 9>{0, 4, 1, 2, 1, 3, 1, -1, 3}[n]));
          }
          presets->setCurrentIndex(row);
        }
        dialog->grab().save(dir + "/" + name + "-depths.png");
        tabs->setCurrentIndex(0);
      }
      if (jpeg && converted && !usd) {
        dialog->findChild<QListWidget *>("exportPresets")->setCurrentRow(selected);
        const int maps = dialog->findChild<QTableWidget *>("exportDerivedOutputs")->rowCount();
        for (int n = 0; n < maps; ++n) {
          auto *depth = dialog->findChild<QComboBox *>(QString("exportDerivedFormat%1").arg(n));
          depth->setCurrentIndex(depth->findData(-1));
        }
      }
      if (converted) {
        auto *list = dialog->findChild<QListWidget *>("exportPresets");
        list->setCurrentRow(selected);
        dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(1);
        if (dialog->findChild<QTableWidget *>("exportDerivedOutputs")->rowCount() != (usd ? 0 : 5))
          qFatal("Converted template output count mismatch");
        dialog->grab().save(dir + "/converted-template.png");
        dialog->findChild<QTabWidget *>("exportTabs")->setCurrentIndex(0);
      }
      dialog->setProperty("basedOnTemplateBeforeExport",
                          dialog->findChild<QLabel *>("exportBasedOnTemplate")->isVisible());
      dialog->grab().save(dir + "/" + name + "-settings.png");
      auto *poll = new QTimer(this);
      connect(poll, &QTimer::timeout, this, [this, dialog, poll, name] {
        if (!dialog->property("exportFinished").toBool())
          return;
        if (!dialog->property("exportError").toString().isEmpty())
          qFatal("Derived GUI export failed: %s",
                 qPrintable(dialog->property("exportError").toString()));
        QJsonArray files;
        auto *list = dialog->findChild<QListWidget *>("exportFileList");
        for (int i = 0; i < list->count(); ++i)
          files.append(list->item(i)->text());
        results.append(QJsonObject{
            {"kind", name},
            {"files", files},
            {"based_on_template", dialog->property("basedOnTemplateBeforeExport").toBool()},
            {"format_disabled", !dialog->findChild<QComboBox *>("exportFormat")->isEnabled()},
            {"displacement_enabled",
             dialog->findChild<QCheckBox *>("exportUsdDisplacement")->isEnabled()},
            {"displacement_checked",
             dialog->findChild<QCheckBox *>("exportUsdDisplacement")->isChecked()}});
        dialog->grab().save(dir + "/" + name + "-complete.png");
        poll->stop();
        poll->deleteLater();
        dialog->accept();
      });
      auto *run = dialog->findChild<QPushButton *>("runTextureExport");
      if (!run->isEnabled())
        qFatal("Derived export disabled");
      run->click();
      poll->start(50);
    });
    action("exportTextures");
    timer.start();
  }
  void advance()
  {
    if (++ticks > 1200)
      qFatal("Derived export demo timed out");
    const auto s = state();
    if (phase > 1 && (s["composite_pending"].toBool() || s["property_pending"].toBool() ||
                      s["render_frames"].toInt() < after_frame))
      return;
    switch (phase++) {
      case 0:
        action("newProject");
        break;
      case 1: {
        auto *dialog = get<QDialog>("newProjectDialog");
        dialog->findChild<QLineEdit *>("meshPath")->setText(mesh);
        auto *resolution = dialog->findChild<QComboBox *>("documentResolution");
        resolution->setCurrentIndex(resolution->findData(size));
        if (qEnvironmentVariableIsSet("BTS_USD_EXPORT_DEMO"))
          dialog->findChild<QComboBox *>("normalMapFormat")->setCurrentIndex(1);
        if (qEnvironmentVariableIsSet("BTS_USD_UDIM_DEMO"))
          dialog->findChild<QCheckBox *>("preserveUvTiles")->setChecked(true);
        dialog->findChild<QPushButton *>("createProject")->click();
        break;
      }
      case 2: {
        action("guiSkewPrepare");
        auto *sets = get<QListWidget>("textureSets");
        for (int i = 0; i < sets->count(); ++i)
          if (sets->item(i)->text().contains("Sphere"))
            sets->setCurrentRow(i);
        action("addChannel5");
        get<QToolButton>("addFillLayer")->click();
        get<QDoubleSpinBox>("roughnessControl")->setValue(.23);
        get<QDoubleSpinBox>("metallicControl")->setValue(.35);
        if (qEnvironmentVariableIsSet("BTS_USD_DISPLACE_DEMO")) {
          get<QCheckBox>("displacementChannel")->setChecked(true);
          get<QToolButton>("brushDisplacement")->setChecked(true);
          get<QDoubleSpinBox>("materialDisplacement")->setValue(.8);
          get<QDoubleSpinBox>("displacementStrength")->setValue(.25);
          get<QSpinBox>("displacementSubdivisionCount")->setValue(1);
        }
        get<QToolButton>("addPaintLayer")->click();
        if (qEnvironmentVariableIsSet("BTS_USD_DISPLACE_DEMO"))
          get<QToolButton>("brushDisplacement")->setChecked(false);
        get<QDoubleSpinBox>("materialBump")->setValue(.2);
        break;
      }
      case 3:
        paint();
        break;
      case 4:
        capture("before");
        export_maps(true);
        break;
      case 5:
        capture("after-converted");
        export_maps(false);
        break;
      case 6:
        capture("after-direct");
        if (qEnvironmentVariableIsSet("BTS_USD_EXPORT_DEMO")) {
          timer.stop();
          QTimer::singleShot(100, this, [this] {
            auto *dialog = get<QDialog>("textureExportDialog");
            auto *presets = dialog->findChild<QComboBox *>("exportPreset");
            presets->setCurrentIndex(presets->findText("USD PBR Metal Roughness"));
            dialog->findChild<QPushButton *>("saveExportSettings")->click();
          });
          action("exportTextures");
          timer.start(250);
        }
        action("guiSkewSave");
        break;
      case 7:
        capture("saved");
        action("guiSkewOpen");
        break;
      case 8:
        capture("opened");
        if (qEnvironmentVariableIsSet("BTS_USD_EXPORT_DEMO")) {
          export_maps(false, true);
          break;
        }
        [[fallthrough]];
      case 9:
        timer.stop();
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
    after_frame = state()["render_frames"].toInt() + 20;
  }

 public:
  Demo(QWidget *window,
       QWidget *viewport,
       QString directory,
       QString fixture,
       int resolution,
       std::function<QJsonObject()> snapshot)
      : QObject(window),
        w(window),
        v(viewport),
        dir(directory),
        mesh(fixture),
        size(resolution),
        state(std::move(snapshot))
  {
    QDir().mkpath(dir);
    timer.setInterval(250);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_derived_export_demo(QWidget *w,
                               QWidget *v,
                               const QString &dir,
                               const QString &mesh,
                               int size,
                               std::function<QJsonObject()> state)
{
  new Demo(w, v, dir, mesh, size, std::move(state));
}
}  // namespace bts
