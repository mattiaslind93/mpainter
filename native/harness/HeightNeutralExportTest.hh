#pragma once
#include "TextureExport.hh"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include <memory>
namespace bts {
inline QJsonObject verify_neutral_height_exports(const QString &directory)
{
  QJsonArray cases;
  for (int legacy = 0; legacy < 3; ++legacy)
    for (float mid : {0.f, .5f}) {
      TextureSet set;
      set.name = "Neutral";
      set.uniform_editable = true;
      set.texture_resolution = 128;
      set.precision[4] = ChannelPrecision::L32F;
      set.channels = {false, false, false, false, true};
      set.displacement.channel = set.displacement.allocated = true;
      set.displacement.midlevel = mid;
      if (legacy) {
        set.bump_origin = 0;
        set.base_values.bump = legacy == 2 ? .0001f : 0;
        set.layers.front().values.bump = 0;
        if (legacy == 2)
          set.precision[4] = ChannelPrecision::L16F;
      }
      const auto output = directory + QString("/neutral-%1-%2")
                                          .arg(legacy == 2 ? "legacy-small-half" :
                                               legacy      ? "legacy" :
                                                             "new")
                                          .arg(mid == 0 ? "zero" : "gray");
      QDir().mkpath(output);
      ExportRequest request;
      request.directory = output.toStdString();
      request.format = ExportFormat::Exr32;
      request.selected_channels = std::array<bool, 9>{
          false, false, false, false, true, false, false, false, true};
      ExportTile tile;
      tile.set = set;
      tile.resolution = 128;
      tile.normal.resize(128 * 128 * 4);
      for (int i = 0; i < 128 * 128; ++i) {
        tile.normal[i * 4] = tile.normal[i * 4 + 1] = .5f;
        tile.normal[i * 4 + 2] = tile.normal[i * 4 + 3] = 1;
      }
      request.tiles.push_back(tile);
      const auto files = export_file_list(request);
      if (files.size() != 2)
        throw std::runtime_error("Neutral export requires separate Bump and Displacement maps");
      std::atomic_bool cancel = false;
      write_texture_export(request, cancel, [](int, const std::string &) {});
      QJsonArray maps;
      for (const auto &file : files) {
        const auto path = QDir(output).filePath(QString::fromStdString(file.name));
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly))
          throw std::runtime_error("Neutral export is missing");
        const auto bytes = source.readAll();
        char space[64]{};
        using Image = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
        Image image(
            blender::IMB_load_image_from_memory(
                reinterpret_cast<const unsigned char *>(bytes.constData()),
                bytes.size(),
                blender::ImBufFlags::NoColorspaceConvert | blender::ImBufFlags::AlphaChannelPacked,
                "neutral.exr",
                nullptr,
                space),
            blender::IMB_freeImBuf);
        if (!image || !image->float_data() || image->x != 128 || image->y != 128)
          throw std::runtime_error("Invalid neutral EXR");
        const bool bump = QString::fromStdString(file.name).contains("_bump.");
        const float expected = bump ? (legacy == 2 ? .5f + float(qfloat16(.0001f)) : .5f) : mid;
        for (int pixel = 0; pixel < 128 * 128; ++pixel)
          for (int channel = 0; channel < 3; ++channel)
            if (image->float_data()[pixel * 4 + channel] != expected)
              throw std::runtime_error(
                  QString(
                      "Neutral height export pixel mismatch: %1 pixel %2 channel %3: %4 expected %5")
                      .arg(path)
                      .arg(pixel)
                      .arg(channel)
                      .arg(image->float_data()[pixel * 4 + channel], 0, 'g', 9)
                      .arg(expected, 0, 'g', 9)
                      .toStdString());
        maps.append(
            QJsonObject{{"file", path}, {"expected", expected}, {"all_pixels_exact", true}});
      }
      cases.append(QJsonObject{
          {"legacy_storage", bool(legacy)}, {"displacement_midlevel", mid}, {"files", maps}});
    }
  return {{"cases", cases},
          {"bump_neutral", .5},
          {"displacement_default", 0},
          {"empty_coverage_exact", true}};
}
}  // namespace bts
