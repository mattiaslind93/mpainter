// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenPbrTexturePackage.hh"
#include "OpenPbrMaterialX.hh"
#include "OpenPbrUsd.hh"
#include "ExportNaming.hh"
#include <QDir>
#include <QSaveFile>
#include <map>
namespace bts {
namespace {
using Groups = std::map<QUuid, const ExportTile *>;
Groups groups(const ExportRequest &request)
{
  Groups result;
  for (const auto &tile : request.tiles) {
    const auto [it, inserted] = result.emplace(tile.set.id, &tile);
    if (!inserted && (it->second->set.material_session_uid != tile.set.material_session_uid ||
                      it->second->set.name != tile.set.name ||
                      it->second->set.shader_parameters != tile.set.shader_parameters ||
                      it->second->set.displacement != tile.set.displacement ||
                      it->second->bump_distance != tile.bump_distance))
      throw std::invalid_argument("Inconsistent Texture Set metadata in MaterialX export");
  }
  return result;
}
std::string filename(const ExportRequest &request, const TextureSet &set)
{
  return export_output_name("$mesh_$textureSet_material",
                            QString::fromStdString(request.mesh),
                            QString::fromStdString(set.name),
                            "material",
                            1001,
                            "mtlx")
      .toStdString();
}
void write(const QDir &directory, const std::string &name, const QByteArray &bytes)
{
  QSaveFile file(directory.filePath(QString::fromStdString(name)));
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error("Cannot write MaterialX package file: " + name);
}
}  // namespace
void validate_openpbr_texture_package(const ExportRequest &request)
{
  if (!request.openpbr_materialx)
    return;
  if (request.tiles.empty() || !request.standard_outputs || request.packed ||
      !request.outputs.empty() || request.selected_channels ||
      request.format != ExportFormat::Exr32)
    throw std::invalid_argument("MaterialX package requires Document channels in EXR32");
  for (const auto &format : request.formats)
    if (format && *format != ExportFormat::Exr32)
      throw std::invalid_argument(
          "MaterialX package channels must use EXR32 to preserve signed/HDR values");
  for (const auto &[id, tile] : groups(request))
    if (!std::isfinite(tile->bump_distance) || tile->bump_distance < 0)
      throw std::invalid_argument("Invalid native Bump Distance");
    else if (!tile->set.shader_parameters.openpbr || !tile->set.uniform_editable ||
             tile->set.disabled)
      throw std::invalid_argument(
          "Assign the OpenPBR compatibility shader to every selected editable Texture Set");
}
std::vector<std::string> openpbr_texture_package_files(const ExportRequest &request)
{
  if (!request.openpbr_materialx)
    return {};
  validate_openpbr_texture_package(request);
  std::vector<std::string> files;
  std::set<QString> unique;
  for (const auto &[id, tile] : groups(request)) {
    auto name = filename(request, tile->set);
    if (!unique.insert(QString::fromStdString(name).toCaseFolded()).second)
      throw std::invalid_argument("MaterialX material filenames collide");
    files.push_back(name);
    files.push_back(QString::fromStdString(name).chopped(5).append(".usda").toStdString());
  }
  files.push_back("OpenPBR-LICENSE.txt");
  return files;
}
std::map<std::uint32_t, std::string> openpbr_usd_material_libraries(const ExportRequest &request)
{
  validate_openpbr_texture_package(request);
  std::map<std::uint32_t, std::string> result;
  if (!request.openpbr_materialx)
    return result;
  for (const auto &[id, tile] : groups(request)) {
    auto name = QString::fromStdString(filename(request, tile->set)).chopped(5) + ".usda";
    if (!result
             .emplace(tile->set.material_session_uid,
                      QDir(QString::fromStdString(request.directory))
                          .absoluteFilePath(name)
                          .toStdString())
             .second)
      throw std::invalid_argument("Duplicate material UID in OpenPBR export");
  }
  return result;
}
std::vector<UsdTextureSetExport> openpbr_usd_preview_textures(const ExportRequest &request)
{
  validate_openpbr_texture_package(request);
  std::map<std::uint32_t, UsdTextureSetExport> sets;
  for (const auto &[id, tile] : groups(request)) {
    auto &set = sets[tile->set.material_session_uid];
    set.material_uid = tile->set.material_session_uid;
    set.scene_linear_color = true;
    set.tiles = tile->set.tiles;
  }
  for (const auto &file : export_file_list(request)) {
    const int role = file.channel == 0 ? 0 :
                     file.channel == 1 ? 1 :
                     file.channel == 2 ? 2 :
                     file.channel == 7 ? 3 :
                                         -1;
    if (role < 0)
      continue;
    auto &set = sets[file.material_uid];
    set.material_uid = file.material_uid;
    set.scene_linear_color = true;
    set.tiles.insert(file.tile);
    set.files[role] = QDir(QString::fromStdString(request.directory))
                          .absoluteFilePath(QString::fromStdString(file.asset_path))
                          .toStdString();
  }
  std::vector<UsdTextureSetExport> result;
  for (auto &[uid, set] : sets) {
    result.push_back(std::move(set));
  }
  return result;
}
void write_openpbr_texture_package(const ExportRequest &request,
                                   std::span<const ExportFile> files,
                                   const std::atomic_bool &cancel,
                                   const std::function<void(int, const std::string &)> &progress)
{
  if (!request.openpbr_materialx)
    return;
  validate_openpbr_texture_package(request);
  const auto planned = openpbr_texture_package_files(request);
  const QDir directory(QString::fromStdString(request.directory));
  if (!directory.isAbsolute() || !directory.exists())
    throw std::invalid_argument("Choose an existing absolute MaterialX output directory");
  if (cancel)
    return;
  write(directory, planned.back(), openpbr_materialx_license());
  progress(int(files.size()), planned.back());
  for (const auto &[id, tile] : groups(request)) {
    if (cancel)
      return;
    const auto &set = tile->set;
    OpenPbrMaterialX material;
    material.parameters = *set.shader_parameters.openpbr;
    material.bump_scale = tile->bump_distance;
    material.material_name = QString::fromStdString(set.name);
    material.tiles.clear();
    material.displacement_scale = set.displacement.strength;
    material.displacement_midlevel = set.displacement.midlevel;
    material.displacement_enabled = set.displacement.enabled && set.displacement.channel;
    for (const auto &entry : request.tiles)
      if (entry.set.id == id)
        material.tiles.insert(entry.number);
    for (const auto &file : files) {
      if (file.material_uid != set.material_session_uid)
        continue;
      const char *parameter = nullptr;
      switch (file.channel) {
        case 0:
          parameter = "base_color";
          break;
        case 1:
          parameter = "base_metalness";
          break;
        case 2:
          parameter = "specular_roughness";
          break;
        case 4:
          parameter = "bump";
          break;
        case 8:
          parameter = "displacement";
          break;
        default:
          break;
      }
      if (!parameter)
        continue;
      const auto relative = QString::fromStdString(file.asset_path.empty() ? file.name :
                                                                             file.asset_path);
      OpenPbrImageBinding binding{
          relative, file.channel == 0 ? OpenPbrImageSpace::AcesCg : OpenPbrImageSpace::Raw};
      const auto found = material.images.find(parameter);
      if (found != material.images.end() && found->second.filename != binding.filename)
        throw std::invalid_argument("MaterialX UDIM filenames do not form a consistent sequence");
      material.images[parameter] = std::move(binding);
    }
    // Disabled channels use the same neutral material as the texture exporter.
    const MaterialValues neutral;
    for (int channel = 0; channel < 3; ++channel) {
      const char *parameter = openpbr_export_channel(channel);
      if (material.images.contains(parameter))
        continue;
      if (set.channels[channel])
        throw std::invalid_argument("MaterialX package is missing an active PBR channel");
      material.parameters.set(
          parameter,
          channel == 0 ?
              OpenPbrParameters::Value{
                  neutral.base_color[0], neutral.base_color[1], neutral.base_color[2]} :
              OpenPbrParameters::Value{channel == 1 ? neutral.metallic : neutral.roughness, 0, 0});
    }
    if (set.channels[4] && !material.images.contains("bump"))
      throw std::invalid_argument("MaterialX package is missing Bump");
    if (set.displacement.channel && !material.images.contains("displacement"))
      throw std::invalid_argument("MaterialX package is missing Displacement");
    auto bytes = openpbr_materialx_document(material);
    if (cancel)
      return;
    const auto name = filename(request, set);
    write(directory, name, bytes);
    progress(int(files.size()), name);
    const auto usd_name = QString::fromStdString(name).chopped(5).append(".usda").toStdString();
    const auto usd = openpbr_usd_document(bytes, QString::fromStdString(name));
    if (cancel)
      return;
    write(directory, usd_name, usd);
    progress(int(files.size()), usd_name);
  }
}
}  // namespace bts
