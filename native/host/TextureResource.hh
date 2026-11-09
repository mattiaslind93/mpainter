// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceAvailability.hh"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <atomic>
#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>
namespace bts {
struct TextureSourceFile {
  int tile = 0;  // 0: ordinary image. 1001–9999: an explicit UDIM member.
  QString filename, path, sha256;
  qint64 size = 0;
  bool operator==(const TextureSourceFile &) const = default;
};
inline void validate_texture_sources(const std::vector<TextureSourceFile> &files)
{
  if (files.empty())
    throw std::invalid_argument("A texture resource requires source files");
  int previous = -1;
  const ResourceAvailability text_memory;
  std::uint64_t text_units = 0;
  for (const auto &file : files) {
    if ((file.tile != 0 && (file.tile < 1001 || file.tile > 9999)) || file.tile <= previous ||
        (file.tile == 0 && files.size() != 1) || file.size < 1 || file.filename.isEmpty() ||
        file.filename.contains('/') || file.filename.contains('\\') ||
        file.filename.contains(QChar(0)) || file.path.contains(QChar(0)) ||
        file.sha256.size() != 64 ||
        std::any_of(
            file.sha256.begin(),
            file.sha256.end(),
            [](QChar c) { return !QString("0123456789abcdef").contains(c); }))
      throw std::invalid_argument("Invalid texture file/tile descriptor");
    text_units = resource_sum(text_units, resource_sum(file.filename.size(), file.path.size()));
    previous = file.tile;
  }
}
inline QJsonArray texture_sources_json(const std::vector<TextureSourceFile> &files,
                                       bool paths = true)
{
  QJsonArray result;
  for (const auto &file : files) {
    QJsonObject entry{{"tile", file.tile},
                      {"filename", file.filename},
                      {"sha256", file.sha256},
                      {"size", file.size}};
    if (paths)
      entry["path"] = file.path;
    result.append(entry);
  }
  return result;
}
inline std::vector<TextureSourceFile> read_texture_sources(const QJsonValue &value)
{
  if (!value.isArray() || value.toArray().isEmpty())
    throw std::invalid_argument("Invalid texture file manifest");
  std::vector<TextureSourceFile> files;
  files.reserve(value.toArray().size());
  for (const auto &entry : value.toArray()) {
    if (!entry.isObject())
      throw std::invalid_argument("Invalid texture file manifest entry");
    const auto object = entry.toObject();
    TextureSourceFile file;
    file.tile = object["tile"].toInt(-1);
    file.filename = object["filename"].toString();
    file.path = object["path"].toString();
    file.sha256 = object["sha256"].toString();
    file.size = object["size"].toInteger(-1);
    if (!object["tile"].isDouble() || object["tile"].toDouble() != file.tile ||
        !object["size"].isDouble() || object["size"].toDouble() != double(file.size))
      throw std::invalid_argument("Invalid texture file size/tile");
    files.push_back(std::move(file));
  }
  validate_texture_sources(files);
  return files;
}
inline QString texture_sources_hash(const std::vector<TextureSourceFile> &files)
{
  validate_texture_sources(files);
  if (files.size() == 1 && files.front().tile == 0)
    return files.front().sha256;
  return QString::fromLatin1(
      QCryptographicHash::hash(
          QByteArray("BTSUDIM-FILES-2\n") +
              QJsonDocument(texture_sources_json(files, false)).toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
}
inline QString hash_texture_file(const QString &path,
                                 const std::atomic_bool *cancel = nullptr,
                                 std::function<void(qint64, qint64)> progress = {})
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(
        (QFileInfo(path).fileName() + ": " + file.errorString()).toStdString());
  QCryptographicHash hash(QCryptographicHash::Sha256);
  qint64 read = 0;
  const auto size = file.size();
  while (!file.atEnd()) {
    if (cancel && cancel->load())
      throw std::runtime_error("Texture load cancelled");
    const auto bytes = file.read(4 * 1024 * 1024);
    if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
      throw std::runtime_error(
          (QFileInfo(path).fileName() + ": " + file.errorString()).toStdString());
    hash.addData(bytes);
    read += bytes.size();
    if (progress)
      progress(read, size);
  }
  if (read != size)
    throw std::runtime_error("Texture source changed during read");
  return QString::fromLatin1(hash.result().toHex());
}
}  // namespace bts
