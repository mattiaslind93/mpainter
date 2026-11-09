#pragma once
#include "BlenderHost.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialBundle.hh"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace bts {
// Offline asset authoring only. One selected material per process bounds Blender's caches.
inline int prepare_polyhaven(BlenderHost &host,
                             const QString &directory,
                             const QString &source_directory)
{
  const QDir root(directory), sources(source_directory);
  const auto write = [](const QString &path, const QByteArray &bytes) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
      throw std::runtime_error("Cannot write Poly Haven resource");
  };
  QFile manifest(root.filePath("manifest.json"));
  if (!manifest.open(QIODevice::ReadOnly))
    throw std::runtime_error("Missing Poly Haven manifest");
  auto rows = QJsonDocument::fromJson(manifest.readAll()).array();
  manifest.close();
  if (rows.empty())
    throw std::runtime_error("Empty Poly Haven manifest");
  QDir().mkpath(root.filePath("native/images"));
  const auto selected = qEnvironmentVariable("BTS_POLYHAVEN_MATERIAL");
  int count = 0;
  for (qsizetype i = 0; i < rows.size(); ++i) {
    auto row = rows[i].toObject();
    const auto slug = row["slug"].toString(), name = row["name"].toString();
    if (slug != selected)
      continue;
    auto prepared = host.import_native_material(sources.filePath(row["source_blend"].toString()),
                                                name,
                                                std::make_shared<std::atomic_bool>(false))
                        .get();
    if (!prepared.native_graph || !prepared.native_graph->original_inputs_preserved)
      throw std::runtime_error("Source inputs were not preserved");
    const QImage preview(sources.filePath(slug + ".png"));
    if (preview.size() != QSize(256, 256))
      throw std::runtime_error("Missing JL01 preview");
    set_native_material_preview(prepared.material, preview);
    prepared.material.search_terms.append("preview:blender-source-shaderball");
    QJsonArray textures, channels;
    for (auto texture : prepared.textures) {
      if (texture.texture_files.empty())
        throw std::runtime_error("Missing original texture files");
      for (auto &file : texture.texture_files) {
        const auto relative = "native/images/" + file.sha256 + "." +
                              QFileInfo(file.filename).suffix();
        const auto destination = root.filePath(relative);
        if (!QFileInfo::exists(destination) && !QFile::copy(file.path, destination))
          throw std::runtime_error("Cannot copy original texture");
        if (QFileInfo(destination).size() != file.size ||
            hash_texture_file(destination) != file.sha256)
          throw std::runtime_error("Texture checksum mismatch");
        file.path = relative;
      }
      texture.path = texture.texture_files.front().path;
      textures.append(QJsonObject{{"metadata", asset_metadata(texture)}});
    }
    for (const auto &channel : prepared.native_graph->channels) {
      channels.append(QJsonObject{{"channel", channel.channel},
                                  {"supported", bool(channel.tree)},
                                  {"error", channel.error}});
      if (!channel.tree && channel.channel != 3 && channel.channel != 4)
        throw std::runtime_error(("Unsupported starter channel: " + channel.error).toStdString());
    }
    const auto archive = "native/" + slug + ".btsnative.blend",
               metadata = "native/" + slug + ".json";
    write(root.filePath(archive), prepared.material.bytes);
    write(root.filePath(metadata),
          QJsonDocument(
              QJsonObject{{"material", asset_metadata(prepared.material)},
                          {"textures", textures},
                          {"channels", channels},
                          {"source_inputs_preserved", true},
                          {"world_normal_error", prepared.native_graph->world_normal_error},
                          {"preview",
                           QJsonObject{{"mesh", "Shader_Ball_JL_01"},
                                       {"renderer", "Blender Cycles CPU"},
                                       {"image_sha256",
                                        hash_texture_file(sources.filePath(slug + ".png"))}}}})
              .toJson());
    row["native_archive"] = archive;
    row["native_metadata"] = metadata;
    rows[i] = row;
    ++count;
  }
  if (count != 1)
    throw std::runtime_error("Select exactly one Poly Haven material");
  write(root.filePath("manifest.json"), QJsonDocument(rows).toJson());
  return 0;
}
}  // namespace bts
