#include "TextureExport.hh"
#include "ExportConfiguration.hh"
#include <OpenImageIO/imageio.h>
#include <QDir>
#include <QJsonArray>
#include <cmath>
namespace bts {
QJsonObject verify_export_depths(const QString &directory)
{
  const auto require = [](bool ok, const char *reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  TextureSet set;
  set.name = "DepthFixture";
  set.texture_resolution = 128;
  set.base_values.base_color = {.18, .32, .12, 1};
  set.base_values.roughness = .371;
  set.base_values.bump = -.23;
  auto &layer = set.layers.front();
  layer.kind = LayerKind::Fill;
  layer.values = set.base_values;
  layer.displacement.enabled = true;
  layer.displacement.value = -.4;
  TextureDocument::add_displacement_channel(set);
  layer.displacement.enabled = true;
  ExportTile tile{set, 1001, 128};
  tile.normal.resize(128 * 128 * 4);
  for (std::size_t p = 0; p < tile.normal.size(); p += 4) {
    tile.normal[p] = tile.normal[p + 1] = .5;
    tile.normal[p + 2] = tile.normal[p + 3] = 1;
  }
  QJsonArray proof;
  std::atomic_bool cancel = false;
  for (int global = 0; global < 5; ++global) {
    ExportRequest inherited;
    inherited.directory =
        QDir(directory).filePath(QString("inherit-%1").arg(global)).toStdString();
    inherited.format = ExportFormat(global);
    inherited.tiles = {tile};
    inherited.selected_channels = std::array<bool, 9>{
        true, true, true, true, true, true, true, true, true};
    inherited.packed = PackedOutput{};
    inherited.outputs = {{"$textureSet_glossiness", ExportMapSource::Glossiness}};
    auto mixed = inherited;
    mixed.directory = QDir(directory).filePath(QString("mixed-%1").arg(global)).toStdString();
    for (int c = 0; c < 9; ++c)
      if (c % 3 != 2)
        mixed.formats[c] = ExportFormat(c % 5);
    mixed.packed->format = ExportFormat((global + 1) % 5);
    mixed.outputs[0].format = ExportFormat((global + 2) % 5);
    auto explicit_global = inherited;
    explicit_global.directory =
        QDir(directory).filePath(QString("explicit-%1").arg(global)).toStdString();
    for (auto &format : explicit_global.formats)
      format = ExportFormat(global);
    explicit_global.packed->format = ExportFormat(global);
    explicit_global.outputs[0].format = ExportFormat(global);
    for (auto *request : {&inherited, &mixed, &explicit_global}) {
      QDir().mkpath(QString::fromStdString(request->directory));
      write_texture_export(*request, cancel, [](int, const std::string &) {});
      for (const auto &file : export_file_list(*request)) {
        const auto path = QDir(QString::fromStdString(request->directory))
                              .filePath(QString::fromStdString(file.name));
        auto input = OIIO::ImageInput::open(path.toStdString());
        require(bool(input), "Cannot inspect exported bit depth");
        const auto spec = input->spec();
        const auto format = file.channel < 9 ?
                                request->formats[file.channel].value_or(request->format) :
                            file.channel == 9 ?
                                request->packed->format.value_or(request->format) :
                                request->outputs[file.channel - 10].format.value_or(
                                    request->format);
        const auto expected = format == ExportFormat::Png16 ? OIIO::TypeDesc::UINT16 :
                              format == ExportFormat::Exr16 ? OIIO::TypeDesc::HALF :
                              format == ExportFormat::Exr32 ? OIIO::TypeDesc::FLOAT :
                                                              OIIO::TypeDesc::UINT8;
        require(spec.format.basetype == expected,
                "Export file precision differs from per-output selection");
        for (const auto &channel : spec.channelformats)
          require(channel.basetype == expected, "Mixed EXR channel precision");
        if (request != &inherited &&
            (request == &explicit_global || (file.channel < 9 && !request->formats[file.channel])))
        {
          const auto baseline = QDir(QString::fromStdString(inherited.directory))
                                    .filePath(QString::fromStdString(file.name));
          auto reference = OIIO::ImageInput::open(baseline.toStdString());
          require(bool(reference), "Inherited baseline missing");
          const auto rs = reference->spec();
          require(rs.nchannels == spec.nchannels, "Inheritance changed layout");
          std::vector<float> a(spec.width * spec.height * spec.nchannels), b(a.size());
          require(
              input->read_image(0, 0, 0, spec.nchannels, OIIO::TypeDesc::FLOAT, a.data()) &&
                  reference->read_image(0, 0, 0, rs.nchannels, OIIO::TypeDesc::FLOAT, b.data()),
              "Cannot decode depth comparison");
          require(a == b, "Inherited encoding changed decoded pixels");
        }
        if (request == &mixed)
          proof.append(QJsonObject{
              {"global", global},
              {"file", path},
              {"format", int(format)},
              {"bits",
               expected == OIIO::TypeDesc::HALF || expected == OIIO::TypeDesc::UINT16 ? 16 :
               expected == OIIO::TypeDesc::FLOAT                                      ? 32 :
                                                                                        8}});
      }
    }
  }
  return {{"outputs", proof},
          {"inherit_pixel_exact", true},
          {"all_five_encodings", true},
          {"packed_and_derived_precision", true}};
}
}  // namespace bts
