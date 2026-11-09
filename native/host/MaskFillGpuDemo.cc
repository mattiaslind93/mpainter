#include "MaskFillGpuDemo.hh"
#include "ViewportDemoCapture.hh"
#include "GuiFileSelection.hh"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
namespace bts {
namespace {
class Demo final : public QObject {
  QWidget *w, *v;
  QString out, mesh, image;
  std::function<QJsonObject()> state;
  QTimer timer;
  int stage = 0, ticks = 0, step = 0, undo = 0;
  bool busy = false;
  QJsonArray captures;
  QJsonArray previous;
  template<class T> T *get(const char *name)
  {
    auto *p = w->findChild<T *>(name);
    if (!p)
      qFatal("AO GPU control missing %s", name);
    return p;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void proof(const char *name)
  {
    action("checkGuiGpuStack");
    action("guiMaskFillSample");
    auto samples = w->property("aoGpuSamples").toJsonArray();
    if (samples.isEmpty())
      qFatal("AO samples unavailable");
    if (!previous.isEmpty() && samples == previous)
      qFatal("AO mapping had no GPU effect stage=%s", name);
    previous = samples;
    captures.append(QJsonObject{{"stage", name}, {"samples", samples}, {"state", state()}});
    capture_demo_window(w, v).save(out + "/" + name + ".png");
  }
  void advance()
  {
    if (busy)
      return;
    if (++ticks > 3000)
      qFatal("AO demo timeout stage=%d", stage);
    auto s = state();
    if (stage > 1 && stage != 5 &&
        (s["composite_pending"].toBool() || s["property_pending"].toBool()))
      return;
    switch (stage) {
      case 0:
        action("newProject");
        stage = 1;
        break;
      case 1:
        get<QComboBox>("projectTemplate")->setCurrentIndex(0);
        get<QLineEdit>("meshPath")->setText(mesh);
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(512));
        busy = true;
        get<QPushButton>("createProject")->click();
        busy = false;
        stage = 2;
        break;
      case 2:
        get<QToolButton>("addFillLayer")->click();
        action("addBlackMask");
        action("addMaskFillEffect");
        stage = 3;
        break;
      case 3:
        get<QComboBox>("maskFillResource")->setCurrentIndex(1);
        get<QDoubleSpinBox>("maskEffectFillValue")->setValue(.8);
        stage = 4;
        break;
      case 4:
        proof("atom");
        undo = s["undo_count"].toInt();
        get<QSlider>("maskSpatial_fillScaleSlider0")->setSliderDown(true);
        step = 0;
        stage = 5;
        break;
      case 5: {
        auto *slider = get<QSlider>("maskSpatial_fillScaleSlider0");
        slider->setValue(100 + 5 * ++step);
        if (step == 20) {
          slider->setSliderDown(false);
          stage = 6;
        }
        break;
      }
      case 6:
        if (s["undo_count"].toInt() != undo + 1)
          qFatal("AO scale drag needs one Undo");
        proof("scaled");
        get<QDoubleSpinBox>("maskFillRotation")->setValue(31);
        get<QDoubleSpinBox>("maskFillOffset0")->setValue(.17);
        stage = 7;
        break;
      case 7:
        proof("rotated");
        get<QComboBox>("maskFillProjection")->setCurrentIndex(1);
        stage = 8;
        break;
      case 8:
        proof("triplanar");
        get<QComboBox>("maskFillProjection")->setCurrentIndex(0);
        get<QComboBox>("maskFillWrap")->setCurrentIndex(2);
        get<QDoubleSpinBox>("maskFillOffset1")->setValue(.8);
        stage = 9;
        break;
      case 9:
        proof("horizontal");
        get<QComboBox>("maskFillWrap")->setCurrentIndex(0);
        get<QPushButton>("maskFillResetMapping")->click();
        select_gui_test_file(w, "fillTextureFileDialog", image);
        busy = true;
        get<QComboBox>("maskFillResource")->setCurrentIndex(2);
        busy = false;
        stage = 10;
        break;
      case 10:
        if (!get<QToolButton>("maskFillResourceButton")->text().contains("ao-checker"))
          return;
        proof("texture");
        get<QCheckBox>("maskSpatial_fillChannelAlphaMask0")->setChecked(true);
        stage = 11;
        break;
      case 11:
        proof("alpha-masked");
        get<QComboBox>("maskFillResource")->setCurrentIndex(0);
        stage = 12;
        break;
      case 12:
        proof("uniform");
        timer.stop();
        {
          QFile file(out + "/mask-fill-gpu.json");
          if (!file.open(QIODevice::WriteOnly))
            qFatal("AO proof unavailable");
          file.write(
              QJsonDocument(QJsonObject{{"captures", captures}, {"scale_drag_undo", 1}}).toJson());
        }
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
    }
  }

 public:
  Demo(QWidget *a, QWidget *b, QString c, QString d, QString e, std::function<QJsonObject()> f)
      : QObject(a), w(a), v(b), out(c), mesh(d), image(e), state(std::move(f))
  {
    timer.setInterval(16);
    connect(&timer, &QTimer::timeout, this, [this] { advance(); });
    timer.start();
  }
};
}  // namespace
void start_mask_fill_gpu_demo(QWidget *w,
                              QWidget *v,
                              const QString &out,
                              const QString &mesh,
                              const QString &image,
                              std::function<QJsonObject()> state)
{
  new Demo(w, v, out, mesh, image, std::move(state));
}
}  // namespace bts
