// SPDX-License-Identifier: GPL-3.0-or-later
#include <QStandardPaths>
#include "ResourceAvailability.hh"
#pragma once
#include "TextureMemory.hh"
#include "PackedFileLimit.hh"
#include <QIODevice>
#include <QByteArray>
#include <QDir>
#include <QStorageInfo>
#include <QtEndian>
#include <zlib.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace bts {
namespace history_archive {
inline constexpr quint32 maximum_chunk = 32U * 1024 * 1024;
inline constexpr char magic[] = "BTSHIST2";
[[noreturn]] inline void fail()
{
  throw std::invalid_argument("Invalid compressed spatial history archive");
}
inline QByteArray decompress(const uchar *data, qsizetype size, quint32 expected)
{
  if (size < 4 || !expected)
    fail();
  QByteArray result(qsizetype(expected), Qt::Uninitialized);
  uLongf produced = expected;
  if (uncompress(reinterpret_cast<Bytef *>(result.data()), &produced, data + 4, uLong(size - 4)) !=
          Z_OK ||
      produced != expected)
    fail();
  return result;
}
inline std::uint64_t decoded_budget()
{
  const auto root = qEnvironmentVariable(
      "BTS_RECOVERY_DIR",
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/history");
  QDir().mkpath(root);
  const QStorageInfo storage(root);
  if (storage.isValid() && storage.bytesAvailable() > 0)
    return std::uint64_t(storage.bytesAvailable());
  const auto ram = texture_available_ram_bytes();
  return ram;
}
}  // namespace history_archive
// A stream of independently compressed bounded blocks. The logical length is
// 64-bit; neither encoding nor decoding allocates the entire raw archive.
class HistoryArchiveWriter final : public QIODevice {
  QByteArray block_, encoded_;
  quint64 logical_size_ = 0;
  quint32 block_limit_;
  bool finished_ = false;
  void flush_block()
  {
    if (block_.isEmpty())
      return;

    const auto compressed = qCompress(block_, 1);
    if (compressed.isEmpty())
      throw std::runtime_error("Cannot compress spatial history block");
    const auto next = quint64(encoded_.size()) + 4 + quint64(compressed.size());
    require_packed_file_size(next, "Stroke archive");
    if (next > quint64(std::numeric_limits<qsizetype>::max()))
      throw std::length_error("Compressed history exceeds the file representation");
    if (next > quint64(encoded_.capacity())) {
      encoded_.reserve(qsizetype(next));
    }
    char length[4];
    qToLittleEndian<quint32>(quint32(compressed.size()), reinterpret_cast<uchar *>(length));
    encoded_.append(length, 4);
    encoded_.append(compressed);
    block_.clear();
  }
  qint64 readData(char *, qint64) override
  {
    return -1;
  }
  qint64 writeData(const char *data, qint64 count) override
  {
    if (finished_ || count < 0)
      return -1;
    const auto requested = count;
    while (count) {
      const auto take = std::min<qint64>(count, block_limit_ - block_.size());
      if (quint64(take) > std::numeric_limits<quint64>::max() - logical_size_)
        throw std::length_error("Spatial history logical length overflow");
      block_.append(data, qsizetype(take));
      logical_size_ += quint64(take);
      data += take;
      count -= take;
      if (block_.size() == block_limit_)
        flush_block();
    }
    return requested;
  }

 public:
  HistoryArchiveWriter()
  {
    const auto available = texture_available_ram_bytes();
    block_limit_ = quint32(
        std::clamp<std::uint64_t>(available / 16, 64 * 1024, history_archive::maximum_chunk));
    block_.reserve(block_limit_);
    encoded_.append(history_archive::magic, 8);
    encoded_.resize(20);
    qToLittleEndian<quint32>(block_limit_, reinterpret_cast<uchar *>(encoded_.data() + 16));
    open(QIODevice::WriteOnly);
  }
  bool isSequential() const override
  {
    return true;
  }
  quint64 logical_size() const
  {
    return logical_size_;
  }
  QByteArray finish()
  {
    if (finished_)
      throw std::logic_error("History archive already finalized");
    flush_block();
    qToLittleEndian<quint64>(logical_size_, reinterpret_cast<uchar *>(encoded_.data() + 8));
    finished_ = true;
    close();
    return std::move(encoded_);
  }
};
class HistoryArchiveReader final : public QIODevice {
  const QByteArray &encoded_;
  QByteArray block_;
  qsizetype encoded_at_ = 0, block_at_ = 0;
  quint64 remaining_ = 0;
  quint32 block_limit_ = 0;
  bool legacy_ = false;
  void next_block()
  {
    if (legacy_ || encoded_at_ > encoded_.size() - 4)
      history_archive::fail();
    const auto size = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(encoded_.constData() + encoded_at_));
    encoded_at_ += 4;
    if (size < 4 || quint64(size) > quint64(encoded_.size() - encoded_at_))
      history_archive::fail();
    const auto expected = quint32(std::min<quint64>(remaining_, block_limit_));
    if (qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(encoded_.constData() + encoded_at_)) != expected)
      history_archive::fail();

    block_ = history_archive::decompress(
        reinterpret_cast<const uchar *>(encoded_.constData() + encoded_at_),
        qsizetype(size),
        expected);
    encoded_at_ += size;
    block_at_ = 0;
    if (block_.size() != expected)
      history_archive::fail();
  }
  qint64 writeData(const char *, qint64) override
  {
    return -1;
  }
  qint64 readData(char *destination, qint64 count) override
  {
    if (count <= 0 || !remaining_)
      return 0;
    const auto requested = count;
    while (count && remaining_) {
      if (block_at_ == block_.size())
        next_block();
      const auto take = std::min<quint64>(
          {quint64(count), remaining_, quint64(block_.size() - block_at_)});
      std::memcpy(destination, block_.constData() + block_at_, std::size_t(take));
      destination += take;
      block_at_ += qsizetype(take);
      remaining_ -= take;
      count -= qint64(take);
    }
    return requested - count;
  }

 public:
  explicit HistoryArchiveReader(const QByteArray &encoded) : encoded_(encoded)
  {
    if (encoded.startsWith(QByteArray(history_archive::magic, 8))) {
      if (encoded.size() < 20)
        history_archive::fail();
      remaining_ = qFromLittleEndian<quint64>(
          reinterpret_cast<const uchar *>(encoded.constData() + 8));
      block_limit_ = qFromLittleEndian<quint32>(
          reinterpret_cast<const uchar *>(encoded.constData() + 16));
      encoded_at_ = 20;
      if (block_limit_ < 64 * 1024 || block_limit_ > history_archive::maximum_chunk)
        history_archive::fail();
      if (remaining_ > history_archive::decoded_budget())
        throw std::length_error("Spatial history exceeds the available disk storage");
    }
    else {
      if (encoded.size() < 4)
        history_archive::fail();
      const auto size = qFromBigEndian<quint32>(
          reinterpret_cast<const uchar *>(encoded.constData()));
      block_ = history_archive::decompress(
          reinterpret_cast<const uchar *>(encoded.constData()), encoded.size(), size);
      if (block_.isEmpty() || quint64(block_.size()) != size)
        history_archive::fail();
      remaining_ = quint64(block_.size());
      encoded_at_ = encoded.size();
      legacy_ = true;
    }
    open(QIODevice::ReadOnly);
  }
  bool isSequential() const override
  {
    return true;
  }
  qint64 bytesAvailable() const override
  {
    return qint64(std::min<quint64>(remaining_,
                                    quint64(std::numeric_limits<qint64>::max()) -
                                        quint64(QIODevice::bytesAvailable()))) +
           QIODevice::bytesAvailable();
  }
  bool atEnd() const override
  {
    return remaining_ == 0 && QIODevice::bytesAvailable() == 0;
  }
  bool complete() const
  {
    return atEnd() && encoded_at_ == encoded_.size();
  }
};
}  // namespace bts
