#include "BuiltinPolyHaven.hh"
#include "NativeMaterialImageDependencies.hh"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <set>
namespace bts {
namespace {
QUuid polyhaven_id(const QString &name) { return QUuid::createUuidV5(QUuid("{fc6defec-cf55-4c61-b8ea-c484de707ac3}"),name.toUtf8()); }
struct PolyHavenLibrary {
 std::vector<StudioAsset> textures;
 std::vector<StudioAsset> materials;
};

QString polyhaven_asset_directory() {
 const auto configured = qEnvironmentVariable("BTS_POLYHAVEN_ASSET_DIR");
 if (!configured.isEmpty()) return configured;
 const QStringList candidates{
  QCoreApplication::applicationDirPath() + "/assets/materials/polyhaven",
  QCoreApplication::applicationDirPath() + "/../share/mpainter/assets/materials/polyhaven",
#ifdef BTS_POLYHAVEN_ASSET_DIR
  QString::fromUtf8(BTS_POLYHAVEN_ASSET_DIR),
#endif
 };
 for (const auto &path : candidates)
  if (QFileInfo::exists(path + "/manifest.json")) return path;
 return {};
}

QByteArray read_polyhaven_resource(const QString &directory, const QString &relative) {
 if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.split('/').contains(".."))
  throw std::invalid_argument("Invalid PolyHaven native resource path");
 QFile file(directory + "/" + relative);
 if (!file.open(QIODevice::ReadOnly))
  throw std::runtime_error(("Cannot read native PolyHaven resource: " + file.fileName()).toStdString());
 return file.readAll();
}

PolyHavenLibrary make_polyhaven_library() {

 PolyHavenLibrary lib;
 const auto directory = polyhaven_asset_directory();
 if (directory.isEmpty()) return lib;
 const auto doc = QJsonDocument::fromJson(read_polyhaven_resource(directory, "manifest.json"));
 if (!doc.isArray()) throw std::invalid_argument("Invalid PolyHaven manifest");
 lib.materials.reserve(doc.array().size());
 std::set<QString> image_hashes;
 for (const auto &entry : doc.array()) {
  const auto row = entry.toObject();
  const auto archive = row["native_archive"].toString();
  if (archive.isEmpty()) throw std::invalid_argument("Incomplete Poly Haven material library");
  const auto metadata = QJsonDocument::fromJson(read_polyhaven_resource(directory, row["native_metadata"].toString())).object();
  auto asset = asset_metadata_read(metadata["material"].toObject());
  if (!is_native_material_asset(asset) || !asset.fill_preset || !asset.fill_preset->native_material)
   throw std::invalid_argument("PolyHaven material is not a native Blender archive");
  asset.bytes = read_polyhaven_resource(directory, archive);
  if (asset.hash() != metadata["material"].toObject()["sha256"].toString())
   throw std::invalid_argument("PolyHaven native archive checksum mismatch");
  const auto slug = row["slug"].toString();
  asset.id = polyhaven_id("polyhaven-mat:" + slug);
  asset.name = row["name"].toString();
  asset.filename = "polyhaven-" + slug + ".btsnative.blend";
  asset.scope = AssetScope::Builtin;
  asset.search_terms.append({"polyhaven", "starter", "cc0", "pbr"});
  asset.search_terms.removeDuplicates();
  asset.fill_preset->material_resource = asset.id.toString(QUuid::WithoutBraces).toStdString();
  for (const auto &texture_entry : metadata["textures"].toArray()) {
   const auto texture_row = texture_entry.toObject();
   const auto texture_metadata = texture_row["metadata"].toObject();
   const auto hash = texture_metadata["sha256"].toString();
   if (!image_hashes.insert(hash).second) continue;
   auto texture = asset_metadata_read(texture_metadata);
   if (texture.texture_files.empty())
    texture.texture_files = {{0, texture.filename, texture_row["file"].toString(), hash, 0}};
   for (auto &file : texture.texture_files) {
    const auto relative = file.path;
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.split('/').contains(".."))
     throw std::invalid_argument("Invalid PolyHaven source image path");
    const QFileInfo source(directory + "/" + relative);
    if (!source.isFile() || source.size() <= 0 || (file.size > 0 && source.size() != file.size))
     throw std::runtime_error(("Missing or changed PolyHaven source image: " + source.filePath()).toStdString());
    file.path = source.absoluteFilePath();
    file.size = source.size();
   }
   texture.path = texture.texture_files.front().path;
   texture.bytes.clear();
   texture.id = polyhaven_id("polyhaven-source:" + hash);
   texture.name = "PolyHaven source " + hash;
   texture.scope = AssetScope::Builtin;
   texture.search_terms = {"internal:polyhaven-texture", "polyhaven", "blender-native"};
   lib.textures.push_back(std::move(texture));
  }
  validate_native_material_dependencies(asset, lib.textures);
  lib.materials.push_back(std::move(asset));
 }
 return lib;
}

const PolyHavenLibrary &polyhaven_lib() {
 static const auto lib = make_polyhaven_library();
 return lib;
}

} // namespace

const std::vector<StudioAsset> &builtin_polyhaven_materials() { return polyhaven_lib().materials; }
const std::vector<StudioAsset> &builtin_polyhaven_textures() { return polyhaven_lib().textures; }
} // namespace bts
