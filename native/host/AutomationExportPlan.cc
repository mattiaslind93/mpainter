// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureExtent.hh"
#include "AutomationExportPlan.hh"
#include "ExportNaming.hh"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <cmath>
namespace bts {
namespace {
int number(const QJsonValue &value, int minimum, int maximum)
{
  const double n = value.toDouble(-1);
  if (maximum == max_texture_resolution)
    require_texture_resolution_limit(n, "Export");
  if (!value.isDouble() || !std::isfinite(n) || n < minimum || n > maximum || std::floor(n) != n)
    throw std::invalid_argument("Invalid export numeric option");
  return int(n);
}
ExportFormat encoding(const QJsonValue &value)
{
  if (!value.isString())
    throw std::invalid_argument("Export format must be png8/png16/exr16/exr32/jpeg8");
  const QStringList choices{"png8", "png16", "exr16", "exr32", "jpeg8"};
  const int index = choices.indexOf(value.toString());
  if (index < 0)
    throw std::invalid_argument("Unknown export encoding");
  return ExportFormat(index);
}
}  // namespace
AutomationExportPlan automation_export_plan(const TextureDocument &document,
                                            const QJsonObject &options)
{
  for (auto it = options.begin(); it != options.end(); ++it)
    if (!QStringList{"directory",
                     "format",
                     "preset",
                     "texture_sets",
                     "size",
                     "padding",
                     "padding_width",
                     "mesh_displacement"}
             .contains(it.key()))
      throw std::invalid_argument("Unknown automation export option");
  if (options.contains("preset") && !options["preset"].isString())
    throw std::invalid_argument("preset must be a name or UUID string");
  std::vector<QUuid> ids;
  for (const auto &set : document.texture_sets())
    ids.push_back(set.id);
  auto config = read_export_configuration(document.export_configuration(), ids);
  AutomationExportPlan result;
  auto &request = result.request;
  request.mesh = document.settings().absolute_mesh_path;
  request.directory = (options.contains("directory") ? options["directory"].toString() :
                                                       config.directory)
                          .toStdString();
  if (!QDir::isAbsolutePath(QString::fromStdString(request.directory)) ||
      request.directory.find('\0') != std::string::npos)
    throw std::invalid_argument("Export directory must be absolute");
  request.format = options.contains("format") ? encoding(options["format"]) : config.format;
  request.padding = options.contains("padding") ? ExportPadding(number(options["padding"], 0, 4)) :
                                                  config.padding;
  request.padding_width = options.contains("padding_width") ?
                              number(options["padding_width"], 0, 256) :
                              config.padding_width;
  QString preset = options.contains("preset") ? options["preset"].toString() :
                                                config.preset.toString(QUuid::WithoutBraces);
  if (preset == "specular-glossiness")
    apply_export_preset(request, converted_specular_glossiness_preset());
  else if (preset == "usd-metal-rough")
    apply_export_preset(request, usd_metal_rough_preset());
  else if (!preset.isEmpty() && preset != "document" &&
           preset != QUuid().toString(QUuid::WithoutBraces))
  {
    const auto found = std::find_if(
        config.presets.begin(), config.presets.end(), [&](const auto &p) {
          return p.id == QUuid(preset) || p.name == preset;
        });
    if (found == config.presets.end()) {
      if (QUuid(preset) != studio_preset_id && preset.compare("studio", Qt::CaseInsensitive) != 0)
        throw std::invalid_argument("Unknown export preset");
      apply_export_preset(request, studio_preset());
    }
    else
      apply_export_preset(request, *found);
    result.usd = found->kind == ExportPresetKind::UsdMetalRough;
  }
  if (preset == "usd-metal-rough")
    result.usd = true;
  if (options.contains("mesh_displacement") && !options["mesh_displacement"].isBool())
    throw std::invalid_argument("mesh_displacement must be boolean");
  result.mesh_displacement = options.contains("mesh_displacement") ?
                                 options["mesh_displacement"].toBool() :
                                 config.mesh_displacement;
  auto selected = config.selected;
  if (options.contains("texture_sets")) {
    if (!options["texture_sets"].isArray())
      throw std::invalid_argument("texture_sets must be UUID strings");
    selected.clear();
    for (const auto &entry : options["texture_sets"].toArray()) {
      if (!entry.isString())
        throw std::invalid_argument("Invalid Texture Set UUID");
      const QUuid id(entry.toString());
      if (id.isNull() || std::find(ids.begin(), ids.end(), id) == ids.end() ||
          !selected.insert(id).second)
        throw std::invalid_argument("Unknown or duplicate Texture Set UUID");
    }
  }
  int size = config.size;
  if (options.contains("size")) {
    size = number(options["size"], 0, max_texture_resolution);
    if (size && (size < 128 || (size & (size - 1))))
      throw std::invalid_argument(
          "Export size must inherit (0) or be a representable power of two from 128");
  }
  for (const auto &set : document.texture_sets())
    if (!set.disabled && selected.contains(set.id)) {
      result.selected.push_back(set.id);
      for (int tile : set.tiles) {
        ExportTile output{set, tile, set.width(tile)};
        output.height = set.rows(tile);
        output.output_resolution = options.contains("size")      ? size :
                                   config.sizes.contains(set.id) ? config.sizes.at(set.id) :
                                                                   size;
        request.tiles.push_back(std::move(output));
      }
    }
  if (result.selected.empty())
    throw std::invalid_argument("Select at least one active Texture Set");
  (void)export_file_list(request);
  return result;
}
QJsonObject automation_export_files(const AutomationExportPlan &plan)
{
  QJsonArray files;
  QJsonObject grouped;
  const QDir directory(QString::fromStdString(plan.request.directory));
  for (const auto &file : export_file_list(plan.request)) {
    const auto path = directory.filePath(QString::fromStdString(file.name));
    files.append(path);
    for (const auto &tile : plan.request.tiles)
      if (tile.set.material_session_uid == file.material_uid) {
        const auto key = tile.set.id.toString(QUuid::WithoutBraces);
        auto entries = grouped[key].toArray();
        entries.append(path);
        grouped[key] = entries;
        break;
      }
  }
  if (plan.usd) {
    const auto stem = export_token_component(
        QFileInfo(QString::fromStdString(plan.request.mesh)).completeBaseName());
    files.append(directory.filePath(stem + ".usdc"));
    files.append(directory.filePath(stem + ".usdz"));
  }
  return {{"files", files}, {"by_texture_set", grouped}};
}
}  // namespace bts
