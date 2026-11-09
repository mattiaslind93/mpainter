// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialSpherePreview.hh"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTimer>

namespace bts {
// Bounded integration proof: both bundled buttons, 128px project, save/open,
// CPU thumbnails and the actual Xvfb window. No texture stress/unload workload.
class ShaderballDemo final : public QObject {
  QWidget *window_;
  QString directory_;
  std::function<QJsonObject()> state_;
  QTimer timer_, dialogs_;
  QJsonArray captures_;
  int stage_ = 0, ticks_ = 0, frame_ = 0;
  bool busy_ = false;
  template<class T> T *get(const char *name)
  {
    auto *result = window_->findChild<T *>(name);
    if (!result)
      qFatal("Shaderball control missing: %s", name);
    return result;
  }
  static void require(bool ok, const char *message)
  {
    if (!ok)
      qFatal("Shaderball: %s", message);
  }
  void action(const char *name)
  {
    get<QAction>(name)->trigger();
  }
  void capture(const char *stage)
  {
    auto data = state_();
    data["stage"] = stage;
    captures_.append(data);
  }
  void thumbnails()
  {
    MaterialValues material;
    material.base_color = {.65f, .12f, .025f, 1};
    material.metallic = .5f;
    material.roughness = .3f;
    const auto regular = material_sphere_preview(material, nullptr, 128);
    const auto sphere = material_sphere_preview(material, nullptr, 128, true);
    const auto smart = smart_material_sphere_preview(nullptr, 128);
    LayerMask mask;
    mask.background = 1;
    const auto masked = smart_mask_sphere_preview(mask, 128);
    require(regular != sphere, "Material preview still uses the sphere");
    // The base below the sphere is a distinctive part of JL01's silhouette.
    const auto &gb = detail::shaderball_jl01_gbuffer();
    require(gb.w > 0, "JL01 resource missing");
    int base_pixels = 0;
    for (int y = 112; y < 128; ++y)
      for (int x = 0; x < 128; ++x)
        if (gb.sample((x + .5) / 128, (y + .5) / 128).cov > .99 &&
            sphere.pixelColor(x, y) == QColor(bts::theme::Surface))
        {
          require(regular.pixelColor(x, y) != QColor(bts::theme::Surface),
                  "Material lost JL01 base");
          require(smart.pixelColor(x, y) != QColor(bts::theme::Surface),
                  "Smart material lost JL01 base");
          require(masked.pixelColor(x, y) != QColor(bts::theme::Surface), "Mask lost JL01 base");
          ++base_pixels;
        }
    require(base_pixels > 20, "JL01 silhouette oracle did not find base pixels");
    require(regular.save(directory_ + "/material.png") &&
                smart.save(directory_ + "/smart-material.png") &&
                masked.save(directory_ + "/mask.png"),
            "Cannot save previews");
  }
  void advance()
  {
    if (busy_)
      return;
    busy_ = true;
    require(++ticks_ < 1600, "Timed out");
    const auto s = state_();
    if (stage_ > 2 && (s["composite_pending"].toBool() || s["texture_loading"].toBool() ||
                       s["open_pending"].toBool() || s["save_pending"].toBool() ||
                       s["render_frames"].toInt() < frame_))
    {
      busy_ = false;
      return;
    }
    require(s["shader_failures"].toString().isEmpty(), "Shader compilation failed");
    require(s["open_error"].toString().isEmpty() && s["save_error"].toString().isEmpty(),
            "Project I/O failed");
    qInfo("SHADERBALL_STAGE %d", stage_);
    switch (stage_++) {
      case 0:
        thumbnails();
        action("newProject");
        break;
      case 1: {
        auto *sphere = get<QPushButton>("useSphereMesh"),
             *ball = get<QPushButton>("useShaderballMesh");
        require(sphere->isEnabled() && ball->isEnabled(), "Bundled meshes unavailable");
        sphere->click();
        require(get<QLineEdit>("meshPath")->text().endsWith("/sphere.obj"),
                "Sphere selection failed");
        ball->click();
        const auto path = get<QLineEdit>("meshPath")->text();
        require(path.endsWith("/Shader_Ball_JL_01.obj") && QFileInfo(path).size() > 1000000,
                "Shaderball selection failed");
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        require(get<QPushButton>("createProject")->isEnabled(),
                "Selected mesh cannot create project");
        break;
      }
      case 2:
        require(get<QDialog>("newProjectDialog")
                    ->screen()
                    ->grabWindow(get<QDialog>("newProjectDialog")->winId())
                    .save(directory_ + "/new-project.png"),
                "Dialog capture failed");
        get<QPushButton>("createProject")->click();
        break;
      case 3:
        require(s["has_document"].toBool() && s["mesh_triangles"].toInt() == 37292,
                "Wrong imported geometry");
        require(s["missing_uv_meshes"].toInt() == 0, "Shaderball lost UVs");
        capture("created");
        action("guiSkewSave");
        break;
      case 4:
        require(QFileInfo(directory_ + "/skew.blend").size() > 0, "Project was not saved");
        action("guiSkewOpen");
        break;
      case 5:
        require(s["has_document"].toBool() && s["mesh_triangles"].toInt() == 37292,
                "Save/open lost shaderball");
        require(s["missing_uv_meshes"].toInt() == 0, "Save/open lost UVs");
        capture("reopened");
        require(
            window_->screen()->grabWindow(window_->winId()).save(directory_ + "/workspace.png"),
            "Window screenshot failed");
        action("newProject");
        break;
      case 6:
        get<QPushButton>("useSphereMesh")->click();
        get<QComboBox>("documentResolution")
            ->setCurrentIndex(get<QComboBox>("documentResolution")->findData(128));
        get<QPushButton>("createProject")->click();
        break;
      case 7: {
        require(s["has_document"].toBool() && s["mesh_triangles"].toInt() > 1000 &&
                    s["mesh_triangles"].toInt() < 5000,
                "Sphere project regressed");
        capture("sphere");
        timer_.stop();
        dialogs_.stop();
        QFile file(directory_ + "/validation.json");
        require(file.open(QIODevice::WriteOnly), "Cannot write receipt");
        file.write(QJsonDocument(QJsonObject{{"captures", captures_},
                                             {"thumbnail_silhouette_checks", true},
                                             {"both_builtin_buttons", true},
                                             {"resolution", 128}})
                       .toJson());
        QTimer::singleShot(100, qApp, &QCoreApplication::quit);
        break;
      }
    }
    frame_ = state_()["render_frames"].toInt() + 24;
    busy_ = false;
  }

 public:
  ShaderballDemo(QWidget *window, QString directory, std::function<QJsonObject()> state)
      : QObject(window), window_(window), directory_(directory), state_(std::move(state))
  {
    QDir().mkpath(directory_);
    dialogs_.setInterval(40);
    connect(&dialogs_, &QTimer::timeout, this, [this] {
      for (auto *box : window_->findChildren<QMessageBox *>())
        if (box->isVisible()) {
          if (box->standardButtons() & QMessageBox::Discard)
            box->button(QMessageBox::Discard)->click();
          else
            qFatal("Shaderball dialog: %s", qPrintable(box->text()));
        }
    });
    dialogs_.start();
    timer_.setInterval(150);
    connect(&timer_, &QTimer::timeout, this, [this] { advance(); });
    timer_.start();
  }
};
}  // namespace bts
