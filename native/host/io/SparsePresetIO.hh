// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "TilePaintPixels.hh"
#include "TextureExtent.hh"
#include <QDataStream>
#include <QtEndian>
#include <set>
namespace bts {
// Preserve each sparse tile's own authoring resolution. Never resample a layer
// merely because another channel or the destination set uses a different size.
inline void write_preset_plane(QDataStream &stream,
                               const TilePaintPixels &source,
                               std::uint64_t &decoded,
                               std::uint64_t budget)
{
  stream << quint32(source.tiles().size());
  for (const auto &[tile, pixels] : source.tiles()) {
    std::uint64_t count = 0;
    pixels.visit_pages([&](int, std::uint32_t, std::span<const PaintPixels::Pixel>) {
      count = resource_sum(count, 1);
    });
    if (count > std::numeric_limits<quint32>::max())
      throw std::length_error("Sparse preset page count exceeds its 32-bit stream representation");
    const auto size = resource_product(count, 64 * 64 * sizeof(PaintPixels::Pixel));
    decoded += size;
    stream << qint32(tile) << qint32(pixels.resolution()) << quint32(count);
    pixels.visit_pages(
        [&](int channel, std::uint32_t key, std::span<const PaintPixels::Pixel> page) {
          stream << quint8(channel) << quint32(key);
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
          if (stream.writeRawData(reinterpret_cast<const char *>(page.data()),
                                  int(page.size_bytes())) != int(page.size_bytes()))
            throw std::runtime_error("Cannot encode smart-material pixels");
#else
          for (const auto &pixel : page)
            for (float value : pixel)
              stream << value;
#endif
        });
  }
}
inline TilePaintPixels read_preset_plane(QDataStream &stream,
                                         const std::set<int> &allowed,
                                         std::uint64_t &decoded,
                                         std::uint64_t budget,
                                         bool scalar = false,
                                         bool mask = false)
{
  const auto fail = []() { throw std::invalid_argument("Invalid smart-material sparse pixels"); };
  TilePaintPixels result;
  quint32 count;
  stream >> count;
  if (count > allowed.size())
    fail();
  std::set<int> seen;
  for (quint32 t = 0; t < count; ++t) {
    qint32 tile, resolution;
    quint32 pages;
    stream >> tile >> resolution >> pages;
    if (!allowed.contains(tile) || !seen.insert(tile).second ||
        !valid_sparse_texture_extent(resolution, 128, true) ||
        std::uint64_t(pages) > resource_product(5, sparse_texture_page_count(resolution)))
      fail();
    const auto size = resource_product(pages, 64 * 64 * sizeof(PaintPixels::Pixel));
    if (decoded > budget || size > budget - decoded)
      fail();
    decoded += size;
    std::set<std::pair<int, std::uint32_t>> keys;
    for (quint32 p = 0; p < pages; ++p) {
      quint8 channel;
      quint32 key;
      stream >> channel >> key;
      if (channel > 4 || (scalar && channel != 2) || !keys.emplace(channel, key).second)
        fail();
      std::array<PaintPixels::Pixel, 64 * 64> page;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
      if (stream.readRawData(reinterpret_cast<char *>(page.data()), int(sizeof(page))) !=
          int(sizeof(page)))
        fail();
#else
      for (auto &pixel : page)
        for (float &value : pixel)
          stream >> value;
#endif
      if (stream.status() != QDataStream::Ok)
        fail();
      for (const auto &pixel : page) {
        for (float v : pixel)
          if (!std::isfinite(v))
            fail();
        if (scalar && (pixel[1] != 0 || pixel[2] != 0))
          fail();
        if (mask && (pixel[0] < 0 || pixel[0] > pixel[3]))
          fail();
      }
      result.assign_page(resolution, channel, key, page, tile);
    }
  }
  return result;
}
}  // namespace bts
