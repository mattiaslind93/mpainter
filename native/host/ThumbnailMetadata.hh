// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QUuid>
#include <array>
#include <cstdint>
namespace bts {
struct ThumbnailRequest {
  QUuid layer;
  int channel = 0;
  bool mask = false;
};
struct ThumbnailMetadata {
  static constexpr int side = 48;
  std::array<std::array<float, 4>, side * side> pixels{};
  std::uint64_t ticket = 0;
  bool valid = true;
};
}  // namespace bts
