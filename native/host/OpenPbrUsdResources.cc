#include "OpenPbrUsdResources.hh"
#include "TextureResource.hh"
#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/packageUtils.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/layerUtils.h>
#include <pxr/usd/usdShade/udimUtils.h>
#include <filesystem>
#include <limits>
namespace bts {
namespace {
void check(const std::atomic_bool &cancel)
{
  if (cancel.load())
    throw std::runtime_error("USD texture import cancelled");
}
QString member_name(const std::string &path)
{
  return QFileInfo(QString::fromStdString(pxr::ArIsPackageRelativePath(path) ?
                                              pxr::ArSplitPackageRelativePathInner(path).second :
                                              path))
      .fileName();
}
TextureSourceFile packaged_file(const std::string &path,
                                int tile,
                                const QString &cache,
                                const std::atomic_bool &cancel)
{
  auto asset = pxr::ArGetResolver().OpenAsset(pxr::ArResolvedPath(path));
  if (!asset || !asset->GetSize() ||
      asset->GetSize() > std::size_t(std::numeric_limits<qint64>::max()))
    throw std::runtime_error("Cannot read USD texture member: " + path);
  const auto outer = pxr::ArSplitPackageRelativePathOuter(path).first;
  std::error_code error;
  const auto stamp = std::filesystem::last_write_time(std::filesystem::u8path(outer), error);
  if (error)
    throw std::runtime_error("Cannot inspect USD package version: " + outer);
  const auto identity = QByteArray::fromStdString(path) + ":" +
                        QByteArray::number(qlonglong(stamp.time_since_epoch().count())) + ":" +
                        QByteArray::number(QFileInfo(QString::fromStdString(outer)).size()) + ":" +
                        QByteArray::number(qulonglong(asset->GetSize()));
  const QString key = QString::fromLatin1(
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
  const QDir directory(QDir(cache).filePath("usd/" + key));
  if (!QDir().mkpath(directory.absolutePath()))
    throw std::runtime_error("Cannot create private USD texture cache");
  const auto name = member_name(path), filename = directory.filePath(name),
             manifest = directory.filePath("source.json");
  QFile cached(manifest);
  if (cached.open(QIODevice::ReadOnly)) {
    const auto record = QJsonDocument::fromJson(cached.readAll()).object();
    if (record["identity"].toString() == key &&
        record["size"].toInteger() == qint64(asset->GetSize()) &&
        QFileInfo(filename).size() == qint64(asset->GetSize()))
    {
      TextureSourceFile result{
          tile, name, filename, record["sha256"].toString(), qint64(asset->GetSize())};
      validate_texture_sources({result});
      return result;
    }
  }
  QSaveFile output(filename);
  if (!output.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot cache USD texture: " + name.toStdString());
  QCryptographicHash hash(QCryptographicHash::Sha256);
  QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);
  std::size_t offset = 0;
  while (offset < asset->GetSize()) {
    check(cancel);
    const auto count = std::min(std::size_t(buffer.size()), asset->GetSize() - offset);
    if (asset->Read(buffer.data(), count, offset) != count ||
        output.write(buffer.constData(), qint64(count)) != qint64(count))
      throw std::runtime_error("USD texture read/write failed: " + path);
    hash.addData(QByteArrayView(buffer.constData(), qsizetype(count)));
    offset += count;
  }
  check(cancel);
  const auto sha = QString::fromLatin1(hash.result().toHex());
  if (!output.commit())
    throw std::runtime_error("Cannot commit cached USD texture");
  QSaveFile metadata(manifest);
  const auto bytes = QJsonDocument(QJsonObject{{"identity", key},
                                               {"size", qint64(asset->GetSize())},
                                               {"sha256", sha}})
                         .toJson(QJsonDocument::Compact);
  if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() ||
      !metadata.commit())
    throw std::runtime_error("Cannot commit USD texture cache manifest");
  return {tile, name, filename, sha, qint64(asset->GetSize())};
}
}  // namespace
StudioAsset prepare_openpbr_usd_resource(
    const OpenPbrUsdImage &image,
    const std::set<int> &required,
    const QString &cache,
    const std::atomic_bool &cancel,
    const std::function<void(int, int, const QString &)> &progress)
{
  check(cancel);
  if (!QDir(cache).isAbsolute())
    throw std::invalid_argument("Use an absolute private USD resource cache");
  auto layer = pxr::SdfLayer::FindOrOpen(image.authoring_layer.toStdString());
  if (!layer)
    throw std::invalid_argument("USD texture authoring layer is missing");
  const auto pattern = image.binding.filename.toStdString();
  std::vector<std::pair<std::string, int>> sources;
  if (pxr::UsdShadeUdimUtils::IsUdimIdentifier(pattern)) {
    for (const auto &[path, tile] : pxr::UsdShadeUdimUtils::ResolveUdimTilePaths(pattern, layer))
      sources.emplace_back(path, std::stoi(tile));
  }
  else {
    const auto anchored = pxr::SdfComputeAssetPathRelativeToLayer(layer, pattern);
    const auto resolved = pxr::ArGetResolver().Resolve(anchored);
    if (!resolved.empty())
      sources.emplace_back(resolved.GetPathString(), 0);
  }
  if (sources.empty())
    throw std::invalid_argument("USD image cannot be resolved: " + pattern);
  std::sort(sources.begin(), sources.end(), [](const auto &a, const auto &b) {
    return a.second < b.second;
  });
  if (sources.front().second)
    for (int tile : required)
      if (std::none_of(sources.begin(), sources.end(), [&](const auto &entry) {
            return entry.second == tile;
          }))
        throw std::invalid_argument("USD image tile " + std::to_string(tile) +
                                    " is missing: " + pattern);
  StudioAsset result;
  result.scope = AssetScope::Project;
  result.usages = {"texture"};
  result.path = QString::fromStdString(
      pxr::UsdShadeUdimUtils::IsUdimIdentifier(pattern) ?
          pxr::UsdShadeUdimUtils::ResolveUdimPath(pattern, layer) :
          sources.front().first);
  result.filename = member_name(pattern);
  result.name = QFileInfo(result.filename).completeBaseName();
  int index = 0;
  for (const auto &[path, tile] : sources) {
    check(cancel);
    if (progress)
      progress(index, int(sources.size()), member_name(path));
    if (pxr::ArIsPackageRelativePath(path))
      result.texture_files.push_back(packaged_file(path, tile, cache, cancel));
    else {
      const QFileInfo file(QString::fromStdString(path));
      result.texture_files.push_back({tile,
                                      file.fileName(),
                                      file.absoluteFilePath(),
                                      hash_texture_file(file.absoluteFilePath(), &cancel),
                                      file.size()});
    }
    ++index;
  }
  validate_texture_sources(result.texture_files);
  result.id = QUuid::createUuidV5(QUuid("8b278f20-5a58-4382-9e98-1de5719a4b58"),
                                  texture_sources_hash(result.texture_files).toLatin1());
  if (progress)
    progress(index, index, result.filename);
  return result;
}
}  // namespace bts
