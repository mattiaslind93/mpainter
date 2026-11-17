// SPDX-License-Identifier: GPL-3.0-or-later
#include "DilationFill.hh"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
namespace {
// Blender's IMB_filter_extend for float RGBA with a mask, pass by pass.
void reference(
    std::vector<float> &pixels, std::vector<char> &mask, int width, int height, int passes)
{
  const auto at = [&](int x, int y) {
    return x < 0 || x >= width || y < 0 || y >= height ? -1 : y * width + x;
  };
  const auto assigned = [&](const std::vector<char> &m, int index) {
    return index >= 0 && m[index] != 0;
  };
  const float weight[9] = {1, 2, 1, 2, 0, 2, 1, 2, 1};
  auto target = pixels;
  auto target_mask = mask;
  bool changed = true;
  for (int pass = 0; changed && pass < passes; ++pass) {
    changed = false;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x) {
        const int index = at(x, y);
        if (assigned(mask, index))
          continue;
        if (!(assigned(mask, at(x - 1, y)) || assigned(mask, at(x + 1, y)) ||
              assigned(mask, at(x, y - 1)) || assigned(mask, at(x, y + 1))))
          continue;
        float sum[4] = {0, 0, 0, 0}, total = 0;
        int k = 0;
        for (int i = -1; i <= 1; ++i)
          for (int j = -1; j <= 1; ++j) {
            if (i != 0 || j != 0) {
              const int other = at(x + i, y + j);
              if (assigned(mask, other)) {
                total += weight[k];
                for (int c = 0; c < 4; ++c)
                  sum[c] += weight[k] * pixels[4 * other + c];
              }
            }
            ++k;
          }
        if (total != 0) {
          for (int c = 0; c < 4; ++c)
            target[4 * index + c] = sum[c] / total;
          target_mask[index] = 1;
          changed = true;
        }
      }
    pixels = target;
    mask = target_mask;
  }
}
std::uint32_t state = 12345;
float random_unit()
{
  state = state * 1664525u + 1013904223u;
  return float(state >> 8) / float(1u << 24);
}
void compare(int width, int height, int passes, float density)
{
  std::vector<float> pixels(std::size_t(width) * height * 4, 0);
  std::vector<char> mask(std::size_t(width) * height, 0);
  for (std::size_t i = 0; i < mask.size(); ++i)
    if (random_unit() < density) {
      mask[i] = 2;
      for (int c = 0; c < 3; ++c)
        pixels[i * 4 + c] = random_unit() * 3 - 1;
      pixels[i * 4 + 3] = random_unit();
    }
  auto expected = pixels;
  auto expected_mask = mask;
  reference(expected, expected_mask, width, height, passes);
  bts::dilate_rgba(pixels.data(), mask.data(), width, height, passes);
  assert(std::memcmp(pixels.data(), expected.data(), pixels.size() * sizeof(float)) == 0 &&
         "dilation matches Blender's filter bit for bit");
  for (std::size_t i = 0; i < mask.size(); ++i)
    assert((mask[i] != 0) == (expected_mask[i] != 0) && "dilation fills the same texels");
}
}  // namespace
int main()
{
  for (const float density : {.002f, .05f, .5f, .98f})
    for (const int passes : {0, 1, 2, 7, 32, 1000}) {
      compare(64, 64, passes, density);
      compare(97, 31, passes, density);
      compare(1, 40, passes, density);
      compare(40, 1, passes, density);
    }
  compare(33, 33, 64, 0);
  compare(33, 33, 64, 1);
  compare(1, 1, 5, 0);
  return 0;
}
