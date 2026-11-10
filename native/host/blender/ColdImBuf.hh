// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <memory>
#include <stdexcept>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
// Keep the small ImBuf header (including color/alpha metadata), but persist its
// exact bytes. No image encoder, color conversion, or full-image staging copy.
class ColdImBuf {
  std::unique_ptr<QTemporaryFile> file_;
  qint64 floats_ = 0, bytes_ = 0;
  QByteArray digest_;
  static void transfer(QFile &file, char *data, qint64 size, bool write, QCryptographicHash &hash)
  {
    for (qint64 offset = 0; offset < size;) {
      const auto count = std::min<qint64>(size - offset, 1024 * 1024);
      const auto n = write ? file.write(data + offset, count) : file.read(data + offset, count);
      if (n <= 0)
        throw std::runtime_error("Lossless texture cache I/O failed; original retained");
      hash.addData(QByteArrayView(data + offset, n));
      offset += n;
    }
  }

 public:
  bool cold() const
  {
    return bool(file_);
  }
  void spill(blender::ImBuf &buffer)
  {
    if (file_)
      return;
    const auto folder = qEnvironmentVariable(
                            "BTS_RECOVERY_DIR",
                            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)) +
                        "/unloaded-textures";
    if (!QDir().mkpath(folder))
      throw std::runtime_error("Cannot create lossless texture cache");
    auto file = std::make_unique<QTemporaryFile>(folder + "/native-pixels-XXXXXX.raw");
    if (!file->open())
      throw std::runtime_error("Cannot create native pixel cache");
    floats_ = buffer.float_data() ? qint64(buffer.x) * buffer.y * buffer.channels * sizeof(float) :
                                    0;
    bytes_ = buffer.byte_data() ? qint64(buffer.x) * buffer.y * 4 : 0;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    transfer(*file,
             reinterpret_cast<char *>(const_cast<float *>(buffer.float_data())),
             floats_,
             true,
             hash);
    transfer(*file,
             reinterpret_cast<char *>(const_cast<unsigned char *>(buffer.byte_data())),
             bytes_,
             true,
             hash);
    if (!file->flush())
      throw std::runtime_error("Cannot flush native pixel cache");
    digest_ = hash.result();
    file_ = std::move(file);
    auto *float_space = buffer.float_buffer.colorspace,
         *byte_space = buffer.byte_buffer.colorspace;
    blender::IMB_free_float_pixels(&buffer);
    blender::IMB_free_byte_pixels(&buffer);
    buffer.float_buffer.colorspace = float_space;
    buffer.byte_buffer.colorspace = byte_space;
  }
  void restore(blender::ImBuf &buffer)
  {
    if (!file_)
      return;
    // Populate an independent header first: read errors never publish partial pixels.
    std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)> candidate(
        blender::IMB_dupImBuf(&buffer), blender::IMB_freeImBuf);
    if (!candidate || !file_->seek(0))
      throw std::runtime_error("Native pixel cache unavailable");
    if (floats_ && !blender::IMB_alloc_float_pixels(candidate.get(), buffer.channels, false))
      throw std::bad_alloc();
    if (bytes_ && !blender::IMB_alloc_byte_pixels(candidate.get(), false))
      throw std::bad_alloc();
    candidate->float_buffer.colorspace = buffer.float_buffer.colorspace;
    candidate->byte_buffer.colorspace = buffer.byte_buffer.colorspace;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    transfer(
        *file_, reinterpret_cast<char *>(candidate->float_data_for_write()), floats_, false, hash);
    transfer(
        *file_, reinterpret_cast<char *>(candidate->byte_data_for_write()), bytes_, false, hash);
    if (hash.result() != digest_)
      throw std::runtime_error("Native pixel cache checksum mismatch");
    std::swap(buffer.float_buffer, candidate->float_buffer);
    std::swap(buffer.byte_buffer, candidate->byte_buffer);
    file_.reset();
  }
};
}  // namespace bts
