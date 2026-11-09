// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MaterialLayering.hh"
#include "MaterialLayeringSources.hh"
#include <memory>
namespace blender {
struct Main;
struct Material;
struct bNode;
}  // namespace blender
namespace bts {
// Standard Blender nodes/images only: generic input maps repeat independently;
// authored mesh opacity images remain three separate shader samplers.
class MaterialLayerShader {
 public:
  MaterialLayerShader();
  ~MaterialLayerShader();
  MaterialLayerShader(MaterialLayerShader &&) noexcept;
  MaterialLayerShader &operator=(MaterialLayerShader &&) noexcept;
  bool apply(blender::Main &,
             blender::Material &,
             const MaterialLayering &,
             float normal_intensity,
             float ao_intensity,
             bool normal_directx,
             const MaterialLayerImages &sources = {});
  // Original-backed save Images are raw. Linear sources compose their matrix
  // into AP1→scene rows; Color UDIM uses one original→AP1 matrix per member.
  // Admitted ordinary sRGB uses four closest texels, per-texel
  // moncurve, then explicit float bilinear and the same matrix adapter.
  static void color_source_representation(blender::Main &,
                                          blender::Material &,
                                          int input,
                                          const FillTexture &,
                                          bool encoded,
                                          bool validate_only = false);
  // Exact canonical per-member standard-node graph validation; no pixel work.
  static std::map<int, blender::bNode *> raw_udim_samples(
      blender::Main &, blender::Material &, int input, int channel, const FillTexture &);
  static std::map<int, blender::bNode *> color_udim_samples(blender::Main &,
                                                            blender::Material &,
                                                            int input,
                                                            const FillTexture &);
  static void pack_images(blender::Main &);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
