// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>
namespace bts {
// Same result, bit for bit, as Blender's IMB_filter_extend on float RGBA with a
// mask. That function rescans and copies the whole image once per pass; this one
// only visits the ring of texels each pass actually fills, so an 8K map with a
// wide or infinite margin costs one scan instead of hundreds.
// A non-zero mask byte marks an assigned texel. Filled texels are marked 1.
inline void dilate_rgba(float *pixels, char *mask, int width, int height, int passes)
{
  if (!pixels || !mask || width < 1 || height < 1 || passes < 0)
    throw std::invalid_argument("Invalid dilation input");
  const auto count = std::size_t(width) * height;
  const auto assigned = [&](int x, int y) {
    return x >= 0 && x < width && y >= 0 && y < height && mask[std::size_t(y) * width + x] != 0;
  };
  const auto fillable = [&](int x, int y) {
    return assigned(x - 1, y) || assigned(x + 1, y) || assigned(x, y - 1) || assigned(x, y + 1);
  };
  std::vector<std::size_t> ring, following;
  std::vector<unsigned char> queued(count, 0);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const auto index = std::size_t(y) * width + x;
      if (!mask[index] && fillable(x, y)) {
        ring.push_back(index);
        queued[index] = 1;
      }
    }
  std::vector<std::array<float, 4>> values;
  for (int pass = 0; pass < passes && !ring.empty(); ++pass) {
    // Every texel of a pass reads the image as it was before the pass.
    values.resize(ring.size());
    for (std::size_t i = 0; i < ring.size(); ++i) {
      const int x = int(ring[i] % width), y = int(ring[i] / width);
      float total = 0;
      std::array<float, 4> sum{};
      // Blender's accumulation order: x offset outside, y offset inside.
      for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy) {
          if ((dx == 0 && dy == 0) || !assigned(x + dx, y + dy))
            continue;
          const float weight = dx == 0 || dy == 0 ? 2.f : 1.f;
          const float *source = pixels + (std::size_t(y + dy) * width + (x + dx)) * 4;
          total += weight;
          for (int c = 0; c < 4; ++c)
            sum[c] += weight * source[c];
        }
      for (int c = 0; c < 4; ++c)
        sum[c] /= total;
      values[i] = sum;
    }
    for (std::size_t i = 0; i < ring.size(); ++i) {
      for (int c = 0; c < 4; ++c)
        pixels[ring[i] * 4 + c] = values[i][c];
      mask[ring[i]] = 1;
    }
    following.clear();
    for (const auto index : ring) {
      const int x = int(index % width), y = int(index / width);
      const int neighbours[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
      for (const auto &n : neighbours) {
        if (n[0] < 0 || n[0] >= width || n[1] < 0 || n[1] >= height)
          continue;
        const auto next = std::size_t(n[1]) * width + n[0];
        if (!mask[next] && !queued[next]) {
          queued[next] = 1;
          following.push_back(next);
        }
      }
    }
    ring.swap(following);
  }
}
}  // namespace bts
