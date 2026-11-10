// SPDX-License-Identifier: GPL-3.0-or-later
// Compile production main unchanged, replacing only its optional demo driver.
#define main cycles_live_production_main
#define start_gui_fix_demo start_cycles_live_artist_demo
#include "../host/app/main.cc"
#undef start_gui_fix_demo
#undef main
#include "BKE_global.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf_types.hh"
#include "ViewportDemoCapture.hh"
extern "C" unsigned long long BTS_cycles_graph_count() __attribute__((weak));
extern "C" unsigned long long BTS_cycles_svm_count() __attribute__((weak));
namespace bts {
namespace {
using blender::G;
class CyclesLiveArtist final : public QObject {
  QWidget *window, *viewport;
  QString output, mesh;
  std::function<QJsonObject()> snapshot;
  QTimer timer;
  int stage = 0, ticks = 0, frames = 0, settled = 0;
  QJsonArray captures;
  template<class T> T *get(const char *name)
  {
    auto *v = window->findChild<T *>(name);
    if (!v)
      throw std::runtime_error(std::string("Missing BA control: ") + name);
    return v;
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  QJsonObject inspect(bool pixels = false)
  {
    // The production diagnostic callback reads GPU probes only on this flag.
    // Ordinary timer/idle checks must not introduce verification readbacks.
    if (pixels)
      qputenv("BTS_BLEND_MATERIAL_DEMO", "1");
    else
      qunsetenv("BTS_BLEND_MATERIAL_DEMO");
    auto result = snapshot();
    qunsetenv("BTS_BLEND_MATERIAL_DEMO");
    return result;
  }
  void number(double value)
  {
    // Fill mode intentionally hides its legacy numeric control; artists edit
    // the visible gradient, which forwards the normal slider gesture callbacks.
    auto *gradient = get<QWidget>("fillGradientRoughness");
    for (auto *parent = gradient->parentWidget(); parent; parent = parent->parentWidget())
      if (auto *scroll = qobject_cast<QScrollArea *>(parent)) {
        scroll->ensureWidgetVisible(gradient);
        break;
      }
    QApplication::processEvents();
    if (!gradient->isVisible() || gradient->visibleRegion().isEmpty())
      throw std::runtime_error("Artist Roughness gradient is not visible");
    const QPointF point(7 + value * std::max(1, gradient->width() - 14), gradient->height() / 2.);
    QMouseEvent press(QEvent::MouseButtonPress,
                      point,
                      gradient->mapToGlobal(point),
                      Qt::LeftButton,
                      Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(gradient, &press);
    QMouseEvent release(QEvent::MouseButtonRelease,
                        point,
                        gradient->mapToGlobal(point),
                        Qt::LeftButton,
                        Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(gradient, &release);
    if (std::abs(get<QDoubleSpinBox>("roughnessControl")->value() - value) > 1e-6)
      throw std::runtime_error("Visible Roughness gesture did not reach expected value");
  }
  bool current_texels()
  {
    if (stage < 4)
      return true;
    const double expected = stage == 6 || stage == 8 || stage == 9 ? .73 : .23;
    const auto sample = inspect(true);
    const auto gpu = sample["material_gpu_samples"].toArray();
    if (gpu.size() != 3 || gpu[2].toArray().size() != 4)
      return false;
    const auto rgba = gpu[2].toArray();
    if (std::abs(rgba[0].toDouble() - expected) > 1e-6 || rgba[3].toDouble() != 1)
      return false;
    if (sample["render_ui"].toBool()) {
      bool found = false;
      for (blender::Image &image : G_MAIN->images) {
        if (!std::string_view(image.id.name + 2).ends_with(" Roughness"))
          continue;
        // Only after the first actual Cycles loader: this diagnostic cannot mask
        // the original free_cache constructor decision on initial entry.
        blender::ImageUser user{};
        user.tile = 1001;
        auto *buffer = blender::BKE_image_acquire_ibuf(&image, &user, nullptr);
        const bool matches =
            buffer && buffer->float_data() &&
            std::abs(
                buffer
                    ->float_data()[(std::size_t(buffer->y / 2) * buffer->x + buffer->x / 2) * 4] -
                expected) < 1e-6;
        blender::BKE_image_release_ibuf(&image, buffer, nullptr);
        if (!matches)
          return false;
        found = true;
      }
      if (!found)
        return false;
    }
    return true;
  }
  void capture(const char *name)
  {
    auto state = inspect(true);
    QJsonArray images;
    for (blender::Image &image : G_MAIN->images) {
      if (!std::string_view(image.id.name + 2).starts_with("BTS Complete") &&
          !std::string_view(image.id.name + 2).starts_with("BTS Composite"))
        continue;
      QJsonArray tiles;
      const bool loaded = blender::BKE_image_has_loaded_ibuf(&image);
      for (blender::ImageTile &tile : image.tiles) {
        if (!state["render_ui"].toBool()) {
          tiles.append(
              QJsonObject{{"tile", tile.tile_number}, {"not_probed_before_cycles", true}});
          continue;
        }
        blender::ImageUser user{};
        user.tile = tile.tile_number;
        auto *buffer = blender::BKE_image_acquire_ibuf(&image, &user, nullptr);
        QJsonObject t{{"tile", tile.tile_number},
                      {"width", buffer ? buffer->x : 0},
                      {"height", buffer ? buffer->y : 0}};
        if (buffer && buffer->float_data()) {
          QJsonArray rgba;
          for (int c = 0; c < 4; ++c)
            rgba.append(
                buffer->float_data()[(std::size_t(buffer->y / 2) * buffer->x + buffer->x / 2) * 4 +
                                     c]);
          t["rgba"] = rgba;
        }
        blender::BKE_image_release_ibuf(&image, buffer, nullptr);
        tiles.append(t);
      }
      images.append(QJsonObject{
          {"name", image.id.name + 2},
          {"loaded_before_probe", loaded},
          {"update_count", QString::number(image.runtime->update_count)},
          {"gpu_2d", QString::number(quintptr(image.runtime->gputexture[0][0]), 16)},
          {"gpu_array", QString::number(quintptr(image.runtime->gputexture[2][0]), 16)},
          {"tiles", tiles}});
    }
    state["stage"] = name;
    state["cpu_images"] = images;
    state["properties_visible"] = get<QWidget>("fillGradientRoughness")->isVisible();
    if (BTS_cycles_graph_count)
      state["cycles_graph_count"] = QString::number(BTS_cycles_graph_count());
    if (BTS_cycles_svm_count)
      state["cycles_svm_count"] = QString::number(BTS_cycles_svm_count());
    if (qEnvironmentVariableIsSet("BTS_BA_EXPECT_FIXED")) {
      if (captures.size() > 0)
        for (const auto &value : images) {
          const auto image = value.toObject();
          for (const auto &old : captures[0].toObject()["cpu_images"].toArray()) {
            const auto baseline = old.toObject();
            if (image["name"] == baseline["name"] && (image["gpu_2d"] != baseline["gpu_2d"] ||
                                                      image["gpu_array"] != baseline["gpu_array"]))
              throw std::runtime_error("Live bridge replaced an authoritative GPU handle");
          }
        }
      if (QString(name).startsWith("cycles-") && QString(name) != "cycles-before") {
        const auto baseline = captures[1].toObject();
        if (state["cycles_graph_count"] != baseline["cycles_graph_count"] ||
            state["cycles_svm_count"] != baseline["cycles_svm_count"])
          throw std::runtime_error("Pixel-only artist edit rebuilt Cycles shaders/SVM");
      }
    }
    const auto framebuffer = capture_viewport_framebuffer(viewport);
    const QRect roi(qRound(framebuffer.width() * .35),
                    qRound(framebuffer.height() * .39),
                    qRound(framebuffer.width() * .12),
                    qRound(framebuffer.height() * .15));
    state["mesh_roi"] = QJsonArray{roi.x(), roi.y(), roi.width(), roi.height()};
    framebuffer.copy(roi).save(output + "/" + name + "-mesh-roi.png");
    captures.append(state);
    framebuffer.save(output + "/" + name + ".png");
    capture_demo_window(window, viewport).save(output + "/" + name + "-workspace.png");
    if (auto *floating = window->findChild<QDialog *>("propertiesFloat"))
      floating->grab().save(output + "/" + name + "-properties.png");
    QFile f(output + "/cycles-live.json");
    if (!f.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot save BA evidence");
    f.write(QJsonDocument(QJsonObject{{"captures", captures},
                                      {"unchanged_production_main", true},
                                      {"negative_reproduction_only",
                                       !qEnvironmentVariableIsSet("BTS_BA_EXPECT_FIXED")}})
                .toJson());
  }
  void advance()
  {
    if (++ticks > 1800)
      throw std::runtime_error("BA artist reproduction timeout");
    const auto state = inspect();
    if (stage > 1 && (state["composite_pending"].toBool() || state["property_pending"].toBool() ||
                      state["render_frames"].toInt() < frames))
      return;
    if (state["render_ui"].toBool()) {
      const auto status = state["render"].toObject()["status"].toString();
      if (status.contains("error", Qt::CaseInsensitive) ||
          status.contains("failed", Qt::CaseInsensitive))
        throw std::runtime_error(status.toStdString());
      if (!status.contains("Rendering Done")) {
        settled = 0;
        return;
      }
      if (++settled < 3)
        return;
    }
    if (qEnvironmentVariableIsSet("BTS_BA_EXPECT_FIXED") && !current_texels()) {
      settled = 0;
      return;
    }
    settled = 0;
    std::printf(
        "BTS_CYCLES_ARTIST_STAGE step=%d frame=%d\n", stage, state["render_frames"].toInt());
    std::fflush(stdout);
    switch (stage++) {
      case 0:
        action("newProject");
        return;
      case 1: {
        auto *d = get<QDialog>("newProjectDialog");
        d->findChild<QLineEdit *>("meshPath")->setText(mesh);
        d->findChild<QComboBox *>("projectTemplate")->setCurrentIndex(0);
        auto *size = d->findChild<QComboBox *>("documentResolution");
        size->setCurrentIndex(size->findData(128));
        get<QPushButton>("createProject")->click();
        break;
      }
      case 2:
        get<QComboBox>("channelPrecision2")->setCurrentIndex(int(ChannelPrecision::L32F));
        get<QToolButton>("addFillLayer")->click();
        break;
      case 3:
        number(.23);
        break;
      case 4:
        capture("painting-before");
        get<QToolButton>("propertiesUndock")->click();
        action("renderingMode");
        break;
      case 5:
        capture("cycles-before");
        if (qEnvironmentVariableIsSet("BTS_BA_TRACE_ONLY")) {
          timer.stop();
          QCoreApplication::quit();
          return;
        }
        number(.73);
        break;
      case 6:
        capture("cycles-edited");
        action("undoDocument");
        break;
      case 7:
        capture("cycles-undone");
        action("redoDocument");
        break;
      case 8:
        capture("cycles-redone");
        action("paintingMode");
        break;
      case 9:
        capture("painting-return");
        timer.stop();
        std::puts("BTS_CYCLES_LIVE_ARTIST_DONE");
        QCoreApplication::quit();
        return;
    }
    frames = state["render_frames"].toInt() + 1;
  }

 public:
  CyclesLiveArtist(
      QWidget *w, QWidget *v, QString dir, QString fixture, std::function<QJsonObject()> state)
      : QObject(w),
        window(w),
        viewport(v),
        output(std::move(dir)),
        mesh(std::move(fixture)),
        snapshot(std::move(state))
  {
    connect(&timer, &QTimer::timeout, this, [this] {
      try {
        advance();
      }
      catch (const std::exception &e) {
        timer.stop();
        std::fprintf(stderr, "BA_REPRO_FAILED %s\n", e.what());
        QCoreApplication::exit(1);
      }
    });
    timer.start(100);
  }
};
}  // namespace
}  // namespace bts
namespace bts {
void start_cycles_live_artist_demo(QWidget *w,
                                   QWidget *v,
                                   const QString &dir,
                                   const QString &mesh,
                                   int,
                                   std::function<QJsonObject()> state)
{
  new CyclesLiveArtist(w, v, dir, mesh, std::move(state));
}
}  // namespace bts
int main(int argc, char **argv)
{
  return cycles_live_production_main(argc, argv);
}
