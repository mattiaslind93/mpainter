// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "BlendMaterial.hh"
#include "ResourceAvailability.hh"
#include <QCryptographicHash>
#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <functional>
#include <optional>
#if defined(__linux__)
#  include <sys/stat.h>
#endif

namespace bts {
// A disposable metadata cache, never a source archive or a GPU preparation cache.
// Cache records only contain exact saved PreviewImage pixels and length-prefixed UTF-8.
class BlendLibraryIndex {
 public:
  using Entries = std::vector<BlendMaterialEntry>;
  using Reader = std::function<Entries(const QString &, std::stop_token)>;
  explicit BlendLibraryIndex(QString directory = {})
      : directory_(directory.isEmpty() ? qEnvironmentVariable("BTS_BLEND_LIBRARY_CACHE",
                                                              QStandardPaths::writableLocation(
                                                                  QStandardPaths::CacheLocation) +
                                                                  "/blend-library-v2") :
                                         std::move(directory))
  {
  }
  Entries read(const QString &path,
               std::stop_token stop,
               Reader reader = inspect_blend_materials) const
  {
    check(stop);
    const auto before = identity(path);
    const auto cache = QDir(directory_)
                           .filePath(QString::fromLatin1(
                                         QCryptographicHash::hash(before.path.toUtf8(),
                                                                  QCryptographicHash::Sha256)
                                             .toHex()) +
                                     ".index");
    if (before.inode && (before.modified_ns || before.changed_ns))
      if (auto hit = load(cache, before, stop))
        return std::move(*hit);
    auto result = reader(before.path, stop);
    check(stop);
    for (auto &entry : result)
      if (!entry.preview.isNull())
        entry.preview = entry.preview.convertToFormat(QImage::Format_RGBA8888);
    if (identity(before.path) != before)
      throw std::runtime_error("Blender library changed during indexing");
    if (before.inode && (before.modified_ns || before.changed_ns))
      save(cache, before, result, stop);
    return result;
  }

 private:
  struct Stamp {
    QString path;
    quint64 size = 0, device = 0, inode = 0;
    qint64 modified = 0, changed = 0;
    quint32 modified_ns = 0, changed_ns = 0;
    bool operator==(const Stamp &) const = default;
  };
  QString directory_;
  static void check(std::stop_token stop)
  {
    if (stop.stop_requested())
      throw std::runtime_error("Blender library indexing cancelled");
  }
  static Stamp identity(const QString &path)
  {
    const QFileInfo file(path);
    if (path.contains(QChar::Null) || file.suffix().compare("blend", Qt::CaseInsensitive) != 0 ||
        !file.isAbsolute() || !file.isFile() || !file.isReadable() || file.size() <= 0)
      throw std::invalid_argument("Choose an explicit readable .blend library");
    Stamp stamp{file.canonicalFilePath(), quint64(file.size())};
    stamp.modified = file.lastModified().toMSecsSinceEpoch();
    stamp.changed = file.metadataChangeTime().toMSecsSinceEpoch();
#if defined(__linux__)
    struct stat s{};
    if (::stat(QFile::encodeName(stamp.path).constData(), &s) != 0)
      throw std::runtime_error("Cannot stat Blender library");
    // Linux nanosecond timestamps plus ctime also invalidate a same-size rewrite
    // whose mtime was restored. No source payload is read just to find a cache hit.
    stamp.device = s.st_dev;
    stamp.inode = s.st_ino;
    stamp.modified = s.st_mtim.tv_sec;
    stamp.modified_ns = s.st_mtim.tv_nsec;
    stamp.changed = s.st_ctim.tv_sec;
    stamp.changed_ns = s.st_ctim.tv_nsec;
#endif
    return stamp;
  }
  struct Invalid {};
  static void bytes(QDataStream &out, QByteArrayView value)
  {
    out << quint64(value.size());
    if (out.writeRawData(value.data(), value.size()) != value.size())
      throw Invalid{};
  }
  static void text(QDataStream &out, const QString &value)
  {
    bytes(out, value.toUtf8());
  }
  static QByteArray bytes(QDataStream &in, QIODevice &file)
  {
    quint64 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok || count > quint64(file.bytesAvailable()) ||
        count > quint64(std::numeric_limits<qsizetype>::max()))
      throw Invalid{};
    QByteArray value(qsizetype(count), Qt::Uninitialized);
    if (in.readRawData(value.data(), value.size()) != value.size())
      throw Invalid{};
    return value;
  }
  static QString text(QDataStream &in, QIODevice &file)
  {
    const auto raw = bytes(in, file);
    const auto value = QString::fromUtf8(raw);
    if (value.toUtf8() != raw || value.contains(QChar::Null))
      throw Invalid{};
    return value;
  }
  static std::optional<Entries> load(const QString &cache,
                                     const Stamp &stamp,
                                     std::stop_token stop)
  {
    QFile file(cache);
    if (!file.open(QIODevice::ReadOnly))
      return {};
    try {
      if (file.size() < 32 || quint64(file.size()) > std::numeric_limits<std::uint64_t>::max() / 4)
        throw Invalid{};

      const auto encoded = file.readAll();
      if (encoded.size() < 32 || encoded.size() != file.size())
        throw Invalid{};
      const QByteArrayView body(encoded.constData(), encoded.size() - 32);
      if (QCryptographicHash::hash(body, QCryptographicHash::Sha256) !=
          QByteArrayView(encoded).last(32))
        throw Invalid{};
      QByteArray payload = QByteArray::fromRawData(body.data(), body.size());
      QBuffer input(&payload);
      input.open(QIODevice::ReadOnly);
      QDataStream in(&input);
      in.setVersion(QDataStream::Qt_6_8);
      quint32 magic = 0, version = 0;
      in >> magic >> version;
      if (magic != 0x42545349 || version != 2 ||
          text(in, input) != "Blender-5.2-Qt-6.8-metadata-v2")
        throw Invalid{};
      Stamp saved;
      saved.path = text(in, input);
      in >> saved.size >> saved.device >> saved.inode >> saved.modified >> saved.changed >>
          saved.modified_ns >> saved.changed_ns;
      if (saved != stamp)
        throw Invalid{};
      quint64 count = 0;
      in >> count;
      // Every entry has at least four text lengths, a boolean, two dimensions,
      // a tag count and pixel length; reject impossible counts before allocation.
      if (in.status() != QDataStream::Ok || count > quint64(input.bytesAvailable()) / 57 ||
          count > Entries{}.max_size())
        throw Invalid{};
      Entries result;
      result.reserve(count);
      for (quint64 i = 0; i < count; ++i) {
        check(stop);
        BlendMaterialEntry entry;
        entry.name = text(in, input);
        quint8 marked = 0;
        in >> marked;
        if (marked > 1 || entry.name.isEmpty() || entry.name.toUtf8().size() > 255)
          throw Invalid{};
        entry.marked_asset = marked;
        entry.catalog_id = text(in, input);
        entry.catalog_name = text(in, input);
        entry.description = text(in, input);
        quint64 tags = 0;
        in >> tags;
        if (tags > quint64(input.bytesAvailable()) / 8)
          throw Invalid{};
        for (quint64 t = 0; t < tags; ++t)
          entry.tags.append(text(in, input));
        quint32 width = 0, height = 0;
        in >> width >> height;
        const auto pixels = bytes(in, input);
        if (width > quint32(std::numeric_limits<int>::max() / 4) ||
            height > quint32(std::numeric_limits<int>::max()) || (width == 0) != (height == 0) ||
            quint64(pixels.size()) != resource_product(resource_product(width, height), 4))
          throw Invalid{};
        if (width)
          entry.preview = QImage(reinterpret_cast<const uchar *>(pixels.constData()),
                                 width,
                                 height,
                                 width * 4,
                                 QImage::Format_RGBA8888)
                              .copy();
        result.push_back(std::move(entry));
      }
      if (in.status() != QDataStream::Ok || !input.atEnd() || identity(stamp.path) != stamp)
        throw Invalid{};
      check(stop);
      return result;
    }
    catch (const Invalid &) {
      return {};
    }
  }
  static void save(const QString &cache,
                   const Stamp &stamp,
                   const Entries &entries,
                   std::stop_token stop)
  {
    // Cache write failure must not prevent a successfully inspected library
    // from being browsed (read-only HOME, full disk, etc.). QSaveFile is atomic.
    if (!QDir().mkpath(QFileInfo(cache).absolutePath()))
      return;
    QSaveFile file(cache);
    if (!file.open(QIODevice::WriteOnly))
      return;
    std::uint64_t memory = resource_sum(256, resource_product(stamp.path.size(), 4));
    for (const auto &e : entries) {
      memory = resource_sum(memory, resource_sum(64, e.preview.sizeInBytes()));
      for (const auto &t : {e.name, e.catalog_id, e.catalog_name, e.description})
        memory = resource_sum(memory, resource_product(t.size(), 4));
      for (const auto &t : e.tags)
        memory = resource_sum(memory, resource_sum(8, resource_product(t.size(), 4)));
    }

    try {
      QByteArray encoded;
      QBuffer buffer(&encoded);
      buffer.open(QIODevice::WriteOnly);
      QDataStream out(&buffer);
      out.setVersion(QDataStream::Qt_6_8);
      out << quint32(0x42545349) << quint32(2);
      text(out, "Blender-5.2-Qt-6.8-metadata-v2");
      text(out, stamp.path);
      out << stamp.size << stamp.device << stamp.inode << stamp.modified << stamp.changed
          << stamp.modified_ns << stamp.changed_ns << quint64(entries.size());
      for (const auto &entry : entries) {
        check(stop);
        text(out, entry.name);
        out << quint8(entry.marked_asset);
        text(out, entry.catalog_id);
        text(out, entry.catalog_name);
        text(out, entry.description);
        out << quint64(entry.tags.size());
        for (const auto &tag : entry.tags)
          text(out, tag);
        const auto image = entry.preview;
        out << quint32(image.width()) << quint32(image.height());
        bytes(out,
              QByteArrayView(reinterpret_cast<const char *>(image.constBits()),
                             image.sizeInBytes()));
      }
      check(stop);
      if (identity(stamp.path) != stamp)
        throw std::runtime_error("Blender library changed during indexing");
      if (out.status() == QDataStream::Ok) {
        const auto digest = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256);
        if (file.write(encoded) == encoded.size() && file.write(digest) == digest.size())
          file.commit();
      }
    }
    catch (const Invalid &) {
    }
  }
};
}  // namespace bts
