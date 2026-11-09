// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#pragma once
#include <QByteArray>
#include "TextureResource.hh"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QtEndian>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>
namespace bts {
struct SequenceFilename {
  QString prefix, separator, extension;
  int tile = 0;
};
inline std::optional<SequenceFilename> sequence_filename(const QString &path)
{
  const QFileInfo info(path);
  static const QRegularExpression pattern("^(.+)([.\\-_])([1-9][0-9]{3})$");
  const auto match = pattern.match(info.completeBaseName());
  if (!match.hasMatch() || match.captured(3).toInt() < 1001)
    return {};
  return SequenceFilename{
      match.captured(1), match.captured(2), info.suffix(), match.captured(3).toInt()};
}
struct SequenceMember {
  int tile = 0;
  QString filename;
  QByteArray bytes;
};
inline bool is_image_sequence(const QByteArray &bytes)
{
  return bytes.startsWith("BTSUDIM1");
}
inline int image_sequence_count(const QByteArray &bytes)
{
  if (!is_image_sequence(bytes) || bytes.size() < 12)
    return 0;
  const auto count = qFromLittleEndian<quint32>(bytes.constData() + 8);
  return count <= quint32(std::numeric_limits<int>::max()) ? int(count) : 0;
}
inline std::vector<SequenceMember> read_image_sequence(const QByteArray &bytes)
{
  if (!is_image_sequence(bytes))
    throw std::invalid_argument("Invalid image sequence bundle");
  qsizetype offset = 8;
  const auto take = [&](qsizetype size) {
    if (size < 0 || size > bytes.size() - offset)
      throw std::invalid_argument("Truncated image sequence");
    const auto data = bytes.sliced(offset, size);
    offset += size;
    return data;
  };
  const auto integer = [&]() {
    const auto data = take(4);
    return qFromLittleEndian<quint32>(data.constData());
  };
  const auto count = integer();
  if (count < 1 || std::uint64_t(count) > std::uint64_t(bytes.size() - offset) / 12)
    throw std::invalid_argument("Invalid image sequence count for encoded size");
  std::vector<SequenceMember> members;
  int previous = 0;
  for (quint32 i = 0; i < count; ++i) {
    SequenceMember member;
    member.tile = int(integer());
    const auto name_size = integer();
    if (name_size < 1)
      throw std::invalid_argument("Invalid sequence filename");
    const auto name = take(name_size);
    member.filename = QString::fromUtf8(name);
    if (member.filename.toUtf8() != name || member.filename.contains('/') ||
        member.filename.contains('\\') || member.filename.contains(QChar(0)))
      throw std::invalid_argument("Invalid sequence filename");
    const auto size = integer();
    if (size < 1)
      throw std::length_error("Invalid sequence image size");
    member.bytes = take(size);
    const auto parsed = sequence_filename(member.filename);
    if (member.tile < 1001 || member.tile > 9999 || member.tile <= previous || !parsed ||
        parsed->tile != member.tile || is_image_sequence(member.bytes))
      throw std::invalid_argument("Invalid sequence tile identity/order");
    previous = member.tile;
    members.push_back(std::move(member));
  }
  if (offset != bytes.size())
    throw std::invalid_argument("Trailing image sequence data");
  return members;
}
inline QByteArray make_image_sequence(const std::vector<SequenceMember> &members)
{
  if (members.size() > std::numeric_limits<quint32>::max())
    throw std::length_error("Legacy UDIM member count exceeds its 32-bit representation");
  std::uint64_t total = 12;
  for (const auto &member : members) {
    total = resource_sum(
        total,
        resource_sum(
            12, resource_sum(resource_product(member.filename.size(), 3), member.bytes.size())));
  }
  QByteArray bytes("BTSUDIM1");
  const auto integer = [&](quint32 value) {
    char data[4];
    qToLittleEndian(value, data);
    bytes.append(data, 4);
  };
  integer(quint32(members.size()));
  for (const auto &member : members) {
    const auto filename = member.filename.toUtf8();
    if (std::uint64_t(filename.size()) > std::numeric_limits<quint32>::max())
      throw std::length_error("Legacy UDIM filename exceeds its 32-bit encoded length");
    if (member.bytes.size() < 1 ||
        std::uint64_t(member.bytes.size()) > std::numeric_limits<quint32>::max())
      throw std::length_error("Legacy UDIM member exceeds its 32-bit encoded length");
    integer(member.tile);
    integer(quint32(filename.size()));
    bytes.append(filename);
    integer(quint32(member.bytes.size()));
    bytes.append(member.bytes);
  }
  read_image_sequence(bytes);
  return bytes;
}
struct ImageResourceSource {
  QString name, filename, path;
  QByteArray bytes;
  int tiles = 0;
  std::vector<TextureSourceFile> files;
};
inline ImageResourceSource read_image_resource(const QString &path, bool discover = true)
{
  const QFileInfo selected(path);
  if (!selected.isAbsolute() || !selected.isFile())
    throw std::invalid_argument("Choose an existing absolute image path");
  const auto read = [](const QFileInfo &info) {
    if (info.size() < 1)
      throw std::length_error("Choose a nonempty image");
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
      throw std::runtime_error("Cannot read image resource");
    auto bytes = file.readAll();
    if (bytes.size() != info.size())
      throw std::runtime_error("Incomplete image resource read");
    return bytes;
  };
  ImageResourceSource result{
      selected.completeBaseName(), selected.fileName(), selected.absoluteFilePath(), {}, 0};
  const auto pattern = discover ? sequence_filename(path) : std::nullopt;
  if (!pattern) {
    result.bytes = read(selected);
    return result;
  }
  std::map<int, QFileInfo> images;
  for (const auto &entry : selected.dir().entryInfoList(QDir::Files, QDir::Name)) {
    const auto candidate = sequence_filename(entry.fileName());
    if (candidate && candidate->prefix == pattern->prefix &&
        candidate->extension.compare(pattern->extension, Qt::CaseInsensitive) == 0)
    {
      if (!images.emplace(candidate->tile, entry).second)
        throw std::invalid_argument("Duplicate image sequence tile");
    }
  }
  if (!images.contains(pattern->tile))
    throw std::runtime_error("Selected image disappeared during sequence discovery");
  std::vector<SequenceMember> members;
  qsizetype total = 0;
  for (const auto &[tile, info] : images) {
    total = resource_sum(total, info.size());
    members.push_back({tile, info.fileName(), read(info)});
  }
  result.name = pattern->prefix;
  result.filename = pattern->prefix + ".btsseq";
  result.bytes = make_image_sequence(members);
  result.tiles = int(members.size());
  return result;
}
// Discovery/hash only; encoded source files stay on disk. Call on a worker.
inline ImageResourceSource reference_image_resource(
    const QString &path,
    bool discover = true,
    const std::atomic_bool *cancel = nullptr,
    std::function<void(int, int, const QString &)> progress = {})
{
  QFileInfo selected(path);
  auto pattern = discover ? sequence_filename(path) : std::nullopt;
  const QString base = selected.completeBaseName();
  if (discover && !pattern && base.endsWith("<UDIM>")) {
    const auto probe = selected.dir().filePath(base.left(base.size() - 6) + "1001." +
                                               selected.suffix());
    pattern = sequence_filename(probe);
    if (pattern)
      pattern->tile = 0;
  }
  if (!selected.isAbsolute() || (!selected.isFile() && !pattern))
    throw std::invalid_argument("Choose an existing absolute image path or UDIM pattern");
  ImageResourceSource result{
      selected.completeBaseName(), selected.fileName(), selected.absoluteFilePath(), {}, 0, {}};
  std::map<int, QFileInfo> images;
  if (pattern) {
    for (const auto &entry : selected.dir().entryInfoList(QDir::Files, QDir::Name)) {
      const auto candidate = sequence_filename(entry.fileName());
      if (candidate && candidate->prefix == pattern->prefix &&
          candidate->separator == pattern->separator &&
          candidate->extension.compare(pattern->extension, Qt::CaseInsensitive) == 0)
      {
        if (!images.emplace(candidate->tile, entry).second)
          throw std::invalid_argument("Duplicate UDIM tile " + std::to_string(candidate->tile));
      }
    }
    if (images.empty() || (pattern->tile && !images.contains(pattern->tile)))
      throw std::runtime_error("Selected UDIM tile is missing");
    result.name = pattern->prefix;
    result.filename = pattern->prefix + ".btsseq";
    result.tiles = int(images.size());
  }
  else
    images.emplace(0, selected);
  int index = 0;
  for (const auto &[tile, info] : images) {
    if (cancel && cancel->load())
      throw std::runtime_error("Texture load cancelled");
    if (progress)
      progress(index, int(images.size()), info.fileName());
    if (info.size() < 1)
      throw std::runtime_error((info.fileName() + ": empty image").toStdString());
    TextureSourceFile file{tile, info.fileName(), info.absoluteFilePath(), {}, info.size()};
    file.sha256 = hash_texture_file(file.path, cancel);
    if (QFileInfo(file.path).size() != file.size)
      throw std::runtime_error((file.filename + ": source changed during import").toStdString());
    result.files.push_back(std::move(file));
    ++index;
  }
  validate_texture_sources(result.files);
  return result;
}

}  // namespace bts
