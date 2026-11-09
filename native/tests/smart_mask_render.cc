// SPDX-License-Identifier: GPL-3.0-or-later
#include "MaterialSpherePreview.hh"
#include "SpsmMaterialImport.hh"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPainter>
#include <QCryptographicHash>
#include <cstdio>
using namespace bts;
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  if (argc < 3 || argc > 4)
    return 2;
  const QDir input(QString::fromLocal8Bit(argv[1]));
  const QString output = QString::fromLocal8Bit(argv[2]);
  QDir().mkpath(output);
  LayerMask zero, one;
  zero.background = 0;
  one.background = 1;
  const auto black = smart_mask_sphere_preview(zero, 128),
             white = smart_mask_sphere_preview(one, 128);
  int black_low = 255, black_high = 0, white_low = 255, white_high = 0, probes = 0;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x)
      if (detail::shaderball_jl01_gbuffer().sample((x + .5) / 128, (y + .5) / 128).cov > .999) {
        const int b = qGray(black.pixel(x, y)), w = qGray(white.pixel(x, y));
        black_low = std::min(black_low, b);
        black_high = std::max(black_high, b);
        white_low = std::min(white_low, w);
        white_high = std::max(white_high, w);
        ++probes;
      }
  if (!probes || black_high > 25 || white_low < 215 || white_high > 248)
    throw std::runtime_error("Mask generator black/white display endpoints regressed");
  QFile endpoints(output + "/mpainter_mask_endpoints.json");
  if (!endpoints.open(QIODevice::WriteOnly))
    return 3;
  endpoints.write(QJsonDocument(QJsonObject{{"probes", probes},
                                            {"black_min", black_low},
                                            {"black_max", black_high},
                                            {"white_min", white_low},
                                            {"white_max", white_high}})
                      .toJson());
  const auto files = input.entryList({"*.spmsk"}, QDir::Files, QDir::Name);
  constexpr int side = 224, cell = 244, cols = 5;
  QImage sheet(cols * cell, ((files.size() + cols - 1) / cols) * 272, QImage::Format_RGB32);
  sheet.fill(QColor("#262626"));
  QPainter painter(&sheet);
  painter.setPen(QColor("#dddddd"));
  QJsonArray records;
  int index = 0;
  for (const auto &file : files) {
    SpsmImportOptions options;
    options.native_actions = true;
    auto prepared = prepare_spsm_material(input.filePath(file).toStdString(), options);
    auto mask = *prepared.material.set.layers.front().mask;
    const auto name = QFileInfo(file).completeBaseName();
    QJsonArray variants;
    for (int variant = 0; variant < 3; ++variant) {
      auto sample = mask;
      const auto slug = QString(name).toLower().replace(' ', '_');
      const auto state = QStringList{"low", "default", "high"}[variant];
      QJsonObject live;
      if (argc == 4) {
        QFile file(QString::fromLocal8Bit(argv[3]) + "/sp_live_" + slug + "_" + state + ".json");
        if (file.open(QIODevice::ReadOnly))
          live = QJsonDocument::fromJson(file.readAll()).object();
      }
      // Replay the values saved by Painter after real slider drags.
      if (!live.isEmpty()) {
        const auto generators = live["generators"].toArray();
        if (!generators.empty()) {
          const auto p = generators[0].toObject()["parameters"].toObject();
          for (auto &e : mask_effects_recursive(sample.effects))
            if (e.kind == MaskEffectKind::Generator && e.generator != 7) {
              const auto level = p.contains("global_balance") ? p["global_balance"] :
                                 p.contains("Level")          ? p["Level"] :
                                                                p["Wear_Level"];
              const auto contrast = p.contains("global_contrast") ? p["global_contrast"] :
                                    p.contains("Contrast")        ? p["Contrast"] :
                                                                    p["Wear_Contrast"];
              if (level.isDouble())
                e.bias = float(level.toDouble()) - .5f;
              if (contrast.isDouble())
                e.contrast = 1 + 7 * float(contrast.toDouble());
              break;
            }
        }
      }
      else if (variant != 1)
        for (auto &e : mask_effects_recursive(sample.effects))
          if (e.kind == MaskEffectKind::Generator) {
            e.bias = std::clamp(e.bias + (variant == 0 ? -.25f : .25f), -1.f, 1.f);
            break;
          }
      const auto image = smart_mask_sphere_preview(sample, side);
      const auto label = QStringList{"low", "default", "high"}[variant];
      const auto path = output + "/" + name + "_" + label + ".png";
      if (!image.save(path) || !smart_mask_sphere_preview(sample, side, true)
                                    .save(output + "/" + name + "_" + label + "_material.png"))
        return 3;
      variants.append(QJsonObject{{"variant", label},
                                  {"effects", mask_effects_json(sample.effects)},
                                  {"live_evidence", !live.isEmpty()}});
      if (variant == 1) {
        const int x = (index % cols) * cell + 10, y = (index / cols) * 272 + 8;
        painter.drawImage(x, y, image);
        painter.drawText(QRect(x, y + side + 5, side, 32), Qt::AlignCenter, name);
      }
    }
    records.append(QJsonObject{{"name", name},
                               {"partial", prepared.partial},
                               {"appearance_verified", prepared.appearance_verified},
                               {"variants", variants}});
    ++index;
  }
  painter.end();
  if (!sheet.save(output + "/mpainter_smart_masks_1to1.png"))
    return 3;
  QFile receipt(output + "/mpainter_smart_mask_parameters.json");
  if (!receipt.open(QIODevice::WriteOnly))
    return 3;
  receipt.write(QJsonDocument(records).toJson());
  std::printf(
      "Rendered %d retained-source masks through the common CPU evaluator; pixel equivalence remains unverified.\n",
      index);
  return 0;
}
