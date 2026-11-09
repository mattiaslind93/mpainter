// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace bts {
enum class ExportPadding { None, Infinite, Transparent, DefaultBackground, Diffusion };
inline constexpr std::array<const char *, 5> export_padding_names{
    "No padding (passthrough)",
    "Dilation infinite",
    "Dilation + transparent",
    "Dilation + default background color",
    "Dilation + diffusion"};
// Raw unassociated input, native geometric UV coverage, unassociated result (including optional
// alpha).
std::vector<float> pad_texture_export(std::span<const float> rgba,
                                      std::span<const unsigned char> coverage,
                                      int source,
                                      int output,
                                      ExportPadding mode,
                                      int width,
                                      const std::array<float, 3> &background,
                                      int normal_role = -1,
                                      int source_height = 0,
                                      int output_height = 0);
}  // namespace bts
