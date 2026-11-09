// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ResourceAvailability.hh"
#include "DiskPixelPayload.hh"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <memory>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
// Immutable ACEScg color pixels or raw Non-Color data pixels. The descriptor references a packed
// project resource.
struct FillTexture {
  using Pixel = std::array<float, 4>;
  std::string asset, hash, name, source_space;
  // Runtime tile identity for legacy bundles whose members share one serialized hash.
  std::string storage_key;
  const std::string &storage_identity() const
  {
    return storage_key.empty() ? hash : storage_key;
  }
  std::string input_space;  // Runtime resolved metadata/interop ID, for the resource row.
  int width = 0, height = 0;
  bool raw = false;
  // Decode-time statistics; shared with the pixels, never inferred from alpha.
  bool floating_point = false, data_units = false, range_valid = false;
  float minimum = 0, maximum = 0;
  DiskPixelPayload<Pixel> pixels;
  std::shared_ptr<const std::map<int, std::shared_ptr<const FillTexture>>>
      tiles;  // Immutable UDIM members; first member is the ordinary-slot preview.
  void validate() const
  {
    if (std::any_of(source_space.begin(),
                    source_space.end(),
                    [](unsigned char c) { return c < 32 || c == 127; }) ||
        (raw && !source_space.empty()))
      throw std::invalid_argument("Invalid input color-space override");
    if (asset.size() != 36 || hash.size() != 64 || name.empty() || width < 1 || height < 1 ||
        std::uint64_t(width) * height > std::numeric_limits<std::size_t>::max() / sizeof(Pixel) ||
        (pixels && pixels.size() != std::size_t(width) * height))
      throw std::invalid_argument("Invalid fill texture resource");
    if (tiles) {
      if (tiles->empty())
        throw std::invalid_argument("Invalid texture sequence size");
      for (const auto &[tile, member] : *tiles) {
        if (tile < 1001 || tile > 9999 || !member || member->tiles || member->asset != asset ||
            member->raw != raw || member->source_space != source_space)
          throw std::invalid_argument("Invalid texture sequence member");
        member->validate();
      }
      if (width != tiles->begin()->second->width || height != tiles->begin()->second->height)
        throw std::length_error("Texture sequence has an invalid preview");
    }
  }
  // Bitmap triplanar evaluation uses the native shader's Float32 coordinate
  // and texel-fraction contract. Ordinary UV sampling retains sample(double).
  Pixel sample_projected(float u, float v, bool repeat = true, bool use_alpha = true) const
  {
    if (!pixels)
      throw std::runtime_error("Fill texture is not resolved from the packed project resource");
    if (repeat) {
      u -= std::floor(u);
      v -= std::floor(v);
    }
    else {
      u = std::clamp(u, 0.f, 1.f);
      v = std::clamp(v, 0.f, 1.f);
    }
    const float x = u * float(width) - .5f, y = v * float(height) - .5f;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto at = [&](int a, int b) {
      auto p = (*pixels)[std::size_t(repeat ? (b % height + height) % height :
                                              std::clamp(b, 0, height - 1)) *
                             width +
                         (repeat ? (a % width + width) % width : std::clamp(a, 0, width - 1))];
      if (!use_alpha)
        p[3] = 1;
      for (int c = 0; c < 3; ++c)
        p[c] *= p[3];
      return p;
    };
    const auto a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    Pixel p;
    for (int k = 0; k < 4; ++k)
      p[k] = std::lerp(std::lerp(a[k], b[k], x - float(ix)),
                       std::lerp(c[k], d[k], x - float(ix)),
                       y - float(iy));
    if (p[3] > 1e-8f)
      for (int k = 0; k < 3; ++k)
        p[k] /= p[3];
    return p;
  }
  Pixel sample(double u, double v, bool repeat = true, bool use_alpha = true) const
  {
    if (!pixels)
      throw std::runtime_error("Fill texture is not resolved from the packed project resource");
    if (repeat) {
      u -= std::floor(u);
      v -= std::floor(v);
    }
    else {
      u = std::clamp(u, 0., 1.);
      v = std::clamp(v, 0., 1.);
    }
    const double x = u * width - .5, y = v * height - .5;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto at = [&](int a, int b) {
      auto p = (*pixels)[std::size_t(repeat ? (b % height + height) % height :
                                              std::clamp(b, 0, height - 1)) *
                             width +
                         (repeat ? (a % width + width) % width : std::clamp(a, 0, width - 1))];
      if (!use_alpha)
        p[3] = 1;
      for (int c = 0; c < 3; ++c)
        p[c] *= p[3];
      return p;
    };
    const auto a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    Pixel p;
    for (int k = 0; k < 4; ++k)
      p[k] = std::lerp(std::lerp(a[k], b[k], float(x - ix)),
                       std::lerp(c[k], d[k], float(x - ix)),
                       float(y - iy));
    if (p[3] > 1e-8f)
      for (int k = 0; k < 3; ++k)
        p[k] /= p[3];
    return p;
  }
};
}  // namespace bts
