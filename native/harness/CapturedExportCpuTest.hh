#pragma once
#include "TextureExport.hh"
#include "NativeMaterialParameters.hh"
#include "SmudgeCapture.hh"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonObject>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
// Encoding oracle only: supplied GPU-result fixtures, no GPU/source evaluation.
inline QJsonObject verify_captured_texture_export(const QString &directory)
{
  const auto require = [](bool ok, const char *message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  // Artist names may contain "Normal"; only the exported channel identifies a normal map.
  TextureSet set;
  set.name = "Normal_Captured";
  set.uniform_editable = true;
  set.texture_resolution = 128;
  set.channels = {true, true, true, false, true};
  set.ao_channel = true;
  set.displacement.channel = true;
  set.displacement.allocated = true;
  set.layers.front().blending[0] = LayerBlend::Passthrough;
  require(texture_export_has_adjustment(set), "PT Paint metadata was not admitted for GPU export");
  // A retained adjustment capture still needs GPU export after the PT toggle is off.
  PaintPixels::Stamp stamp{{.5, .5}, {.2, 0}, {0, .2}};
  stamp.channels = {true, false, false, false, false};
  AdjustmentPaintSource adjustment;
  auto token = std::make_shared<AdjustmentPaintSnapshot>();
  token->id = QUuid::createUuid();
  token->tiles = {1001};
  adjustment.snapshot = token;
  adjustment.channels[0] = 0;
  auto payload = std::make_shared<SmudgePayload>();
  payload->side = 1;
  payload->recipe = std::make_shared<SmudgeCapture>(TilePaintPixels{}, stamp, 128, adjustment);
  stamp.cloned = payload;
  {
    PaintRecordingScope recording(true);
    set.layers.front().pixels.stamp(128, stamp, set.tiles);
  }
  set.layers.front().blending.fill(LayerBlend::Normal);
  const auto materializations = PaintPixels::recipe_materializations();
  require(texture_export_has_adjustment(set),
          "Toggled-off retained BM recipe lost GPU export admission");
  constexpr int width = 128, height = 256;
  auto captured = std::make_shared<ExportCapturedPlanes>();
  captured->width = width;
  captured->height = height;
  const auto plane = [&](std::array<float, 3> rgb) {
    auto p = std::make_shared<std::vector<float>>(width * height * 4);
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const int i = (y * width + x) * 4;
        for (int c = 0; c < 3; ++c)
          (*p)[i + c] = rgb[c];
        (*p)[i + 3] = x < 64 ? .25f : .75f;
      }
    return p;
  };
  captured->channels[0] = plane({-.75f, 2.5f, .37f});
  captured->channels[1] = plane({.2f, .2f, .2f});
  captured->channels[2] = plane({.25f, .25f, .25f});
  captured->channels[3] = plane({.5f, .5f, 1});
  captured->channels[4] = plane({-.27f, -.27f, -.27f});
  captured->channels[5] = plane({.61f, .61f, .61f});
  captured->channels[16] = plane({-.44f, -.44f, -.44f});
  captured->mixed_ao = plane({.19f, .19f, .19f});
  ExportTile tile;
  tile.set = set;
  tile.resolution = width;
  tile.height = height;
  tile.captured = captured;
  tile.gpu_capture_planned = true;
  ExportRequest request;
  request.tiles = {tile};
  request.selected_channels = std::array<bool, 9>{
      true, true, true, false, true, true, false, true, true};
  request.packed = PackedOutput{
      "$textureSet_packed",
      {PackedSource::Bump, PackedSource::Displacement, PackedSource::MixedAo}};
  request.outputs = {{"$textureSet_diffuse", ExportMapSource::Diffuse, true},
                     {"$textureSet_gloss", ExportMapSource::Glossiness, false}};
  int checked = 0;
  for (auto format : {ExportFormat::Exr32, ExportFormat::Png8}) {
    request.format = format;
    request.directory = QDir(directory)
                            .filePath(format == ExportFormat::Exr32 ? "captured-float" :
                                                                      "captured-png")
                            .toStdString();
    QDir().mkpath(QString::fromStdString(request.directory));
    std::atomic_bool cancel = false;
    write_texture_export(request, cancel, [](int, const std::string &) {});
    for (const auto &file : export_file_list(request)) {
      const auto path = QDir(QString::fromStdString(request.directory))
                            .filePath(QString::fromStdString(file.name));
      require(file.channel != 3 && file.channel != 6, "Captured export exposed internal Normal");
      if (format == ExportFormat::Exr32) {
        QFile source(path);
        require(source.open(QIODevice::ReadOnly), "Captured EXR missing");
        const auto bytes = source.readAll();
        char space[64]{};
        using Image = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
        Image image(
            blender::IMB_load_image_from_memory(
                reinterpret_cast<const unsigned char *>(bytes.constData()),
                bytes.size(),
                blender::ImBufFlags::NoColorspaceConvert | blender::ImBufFlags::AlphaChannelPacked,
                "captured.exr",
                nullptr,
                space),
            blender::IMB_freeImBuf);
        require(image && image->float_data() && image->x == width && image->y == height,
                "Captured EXR dimensions changed");
        std::array<float, 3> expected{};
        if (file.channel == 0)
          expected = {-.75f, 2.5f, .37f};
        else if (file.channel == 1)
          expected.fill(.2f);
        else if (file.channel == 2)
          expected.fill(.25f);
        else if (file.channel == 4)
          expected.fill(-.27f);
        else if (file.channel == 5)
          expected.fill(.61f);
        else if (file.channel == 7)
          expected.fill(.19f);
        else if (file.channel == 8)
          expected.fill(-.44f);
        else if (file.channel == 9)
          expected = {-.27f, -.44f, .19f};
        else if (file.channel == 10)
          expected = {-.6f, 2.f, .296f};
        else if (file.channel == 11)
          expected.fill(.75f);
        else
          throw std::runtime_error("Unexpected captured output");
        for (int y : {0, height - 1})
          for (int x : {8, 120}) {
            const auto i = (y * width + x) * 4;
            for (int c = 0; c < 3; ++c)
              require(std::abs(image->float_data()[i + c] - expected[c]) < 1e-6f,
                      "Captured float output recomposed, quantized or changed channel");
            if (file.channel == 10)
              require(image->float_data()[i + 3] == (x < 64 ? .25f : .75f),
                      "Captured authored alpha changed");
          }
      }
      else if (file.channel == 2 || file.channel == 5 || file.channel == 7 || file.channel == 11) {
        const QImage image(path);
        require(!image.isNull() && image.width() == width && image.height() == height,
                "Captured PNG dimensions changed");
        const int expected = file.channel == 2 ? 64 :
                             file.channel == 5 ? 156 :
                             file.channel == 7 ? 48 :
                                                 191;
        require(std::abs(image.pixelColor(8, 8).red() - expected) <= 1,
                "Captured integer data encoding changed");
      }
      ++checked;
    }
  }
  require(PaintPixels::recipe_materializations() == materializations,
          "Captured exporter materialized a GPU recipe on CPU");
  // A later bad tile must reject before even the first valid tile is encoded.
  request.directory = QDir(directory).filePath("captured-rejected").toStdString();
  QDir().mkpath(QString::fromStdString(request.directory));
  auto bad = std::make_shared<ExportCapturedPlanes>(*captured);
  bad->channels[4].reset();
  auto invalid = tile;
  invalid.number = 1002;
  invalid.captured = bad;
  request.tiles = {tile, invalid};
  bool rejected = false;
  try {
    std::atomic_bool cancel = false;
    write_texture_export(request, cancel, [](int, const std::string &) {});
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected &&
              QDir(QString::fromStdString(request.directory)).entryList(QDir::Files).empty(),
          "Partial GPU export packet was not atomically rejected");
  auto planned = tile;
  planned.captured.reset();
  request.tiles = {planned};
  require(!export_file_list(request).empty(), "Planned GPU metadata could not name its outputs");
  rejected = false;
  try {
    std::atomic_bool cancel = false;
    write_texture_export(request, cancel, [](int, const std::string &) {});
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Filename-only plan reached encoder without capture");
  // Native shader and spatial-filter layers also plan and export via GPU capture.
  for (int kind : {0, 1}) {
    TextureSet native_or_spatial;
    native_or_spatial.name = kind ? "SpatialFilter" : "NativeShader";
    native_or_spatial.uniform_editable = true;
    native_or_spatial.texture_resolution = width;
    native_or_spatial.channels = {true, true, true, false, true};
    native_or_spatial.layers.clear();
    TextureLayer layer;
    layer.kind = LayerKind::Fill;
    if (kind == 0) {
      auto desc = std::make_shared<NativeMaterialDescription>();
      desc->material = "Source";
      desc->channels = {0, 1, 2, 4};
      layer.fill.native_material = desc;
    }
    else {
      LevelsEffect blur;
      blur.kind = ContentEffectKind::Filter;
      blur.filter.resource = 7;
      blur.channel = 0;
      layer.effects = {blur};
    }
    native_or_spatial.layers.push_back(layer);
    ExportTile uncaptured{native_or_spatial, 1001, width};
    uncaptured.height = height;
    ExportRequest check = request;
    check.tiles = {uncaptured};
    bool cpu_rejected = false;
    try {
      (void)export_file_list(check);
    }
    catch (const std::runtime_error &) {
      cpu_rejected = true;
    }
    require(cpu_rejected, "Uncaptured native/spatial tile bypassed GPU capture requirement");
    uncaptured.gpu_capture_planned = true;
    check.tiles = {uncaptured};
    require(!export_file_list(check).empty(),
            "Planned GPU capture failed to list native/spatial export files");
    bool encode_rejected = false;
    try {
      std::atomic_bool cancel = false;
      write_texture_export(check, cancel, [](int, const std::string &) {});
    }
    catch (const std::runtime_error &) {
      encode_rejected = true;
    }
    require(encode_rejected, "Planned-only native/spatial tile encoded without GPU capture");
    uncaptured.captured = captured;
    check.tiles = {uncaptured};
    check.directory =
        QDir(directory).filePath(kind ? "captured-spatial" : "captured-native").toStdString();
    QDir().mkpath(QString::fromStdString(check.directory));
    std::atomic_bool cancel = false;
    write_texture_export(check, cancel, [](int, const std::string &) {});
    require(!QDir(QString::fromStdString(check.directory)).entryList(QDir::Files).isEmpty(),
            "Captured native/spatial GPU export produced no files");
  }
  return {{"supplied_gpu_packet_only", true},
          {"encoded_files_checked", checked},
          {"rectangular", true},
          {"straight_signed_hdr", true},
          {"authored_and_mixed_ao_distinct", true},
          {"packed_displacement_signed", true},
          {"partial_alpha", true},
          {"toggleoff_recipe_detected", true},
          {"missing_packet_rejected_before_files", true},
          {"cpu_recipe_materializations", 0},
          {"gpu_required", false}};
}
}  // namespace bts
