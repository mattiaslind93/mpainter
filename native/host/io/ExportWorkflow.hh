// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace bts {
enum class ExportNamingConvention { Classic, OpenPbr };
inline constexpr const char *openpbr_channel_suffixes[]{"base_color",
                                                        "base_metalness",
                                                        "specular_roughness",
                                                        "normal",
                                                        "bump",
                                                        "ambient_occlusion",
                                                        "normal_opengl",
                                                        "mixed_ao",
                                                        "displacement"};
}  // namespace bts
