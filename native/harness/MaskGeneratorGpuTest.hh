// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BuiltinSmartMasks.hh"
#include "SmartMaskThumbnailJobs.hh"
#include "Theme.hh"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace bts {
inline int verify_mask_generator_thumbnails(BlenderHost &host, const QString &directory)
{
  const auto &assets = builtin_smart_mask_assets();
  if (assets.size() != 10)
    throw std::runtime_error("Expected ten built-in mask generators");
  QString error;
  SmartMaskThumbnailJobs jobs(
      host, [] { return true; }, [&](QString failure) { error = failure; });
  jobs.prepare_startup();
  QImage sheet(5 * 180, 2 * 200, QImage::Format_RGB32);
  sheet.fill(QColor(theme::Canvas));
  QPainter painter(&sheet);
  painter.setPen(QColor(theme::Text));
  QJsonArray records;
  int index = 0;
  for (const auto &asset : assets) {
    QImage image;
    bool completed = false;
    jobs.request(asset, [&](QImage ready) {
      image = std::move(ready);
      completed = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!completed && error.isEmpty() && timer.elapsed() < 60000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(1);
    }
    if (!completed || image.isNull() || !error.isEmpty())
      throw std::runtime_error(
          (asset.name + ": " + (error.isEmpty() ? "GPU thumbnail did not complete" : error))
              .toStdString());
    bool reference_match = false;
    const auto reference_dir = qEnvironmentVariable("MPAINTER_MASK_REFERENCE");
    if (!reference_dir.isEmpty()) {
      QFile source(reference_dir + QString("/%1.btsmask").arg(index));
      if (!source.open(QIODevice::ReadOnly))
        throw std::runtime_error("Reference mask fixture missing");
      auto reference = asset;
      reference.id = QUuid::createUuid();
      reference.bytes = source.readAll();
      reference.smart_mask = std::make_shared<const LayerMask>(read_mask_preset(reference.bytes));
      QImage reference_image;
      completed = false;
      timer.restart();
      jobs.request(reference, [&](QImage ready) {
        reference_image = std::move(ready);
        completed = true;
      });
      while (!completed && error.isEmpty() && timer.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
      }
      if (!completed || !error.isEmpty() || image != reference_image)
        throw std::runtime_error(
            (asset.name + ": GPU thumbnail differs from reference recipe").toStdString());
      reference_match = true;
    }
    const auto path = directory + "/" + QFileInfo(asset.filename).completeBaseName() + ".png";
    if (!image.save(path))
      throw std::runtime_error("Cannot save generator thumbnail");
    painter.drawImage(QRect((index % 5) * 180 + 6, (index / 5) * 200 + 6, 168, 168), image);
    painter.drawText(
        QRect((index % 5) * 180, (index / 5) * 200 + 176, 180, 24), Qt::AlignCenter, asset.name);
    records.append(QJsonObject{{"name", asset.name},
                               {"gpu_sphere_thumbnail", true},
                               {"reference_pixel_exact", reference_match},
                               {"width", image.width()},
                               {"height", image.height()}});
    ++index;
  }
  painter.end();
  jobs.shutdown();
  if (!sheet.save(directory + "/mask-generators-gpu.png"))
    throw std::runtime_error("Cannot save generator sheet");
  QFile output(directory + "/mask-generators-gpu.json");
  if (!output.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot save generator receipt");
  output.write(QJsonDocument(records).toJson());
  std::puts(
      "PASS: all ten built-in Mask Generators rendered through the GPU sphere thumbnail path");
  return 0;
}
}  // namespace bts
