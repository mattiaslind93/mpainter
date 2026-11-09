#pragma once
#include "../host/TextureDocument.hh"
namespace bts::folder_test {
inline TextureSet fixture(LayerBlend mode,
                          float mask = .25f,
                          float opacity = .6f,
                          float child_alpha = 1,
                          float below_alpha = 1)
{
  TextureSet set;
  set.texture_resolution = 128;
  set.ao_channel = true;
  set.precision.fill(ChannelPrecision::RGB32F);
  set.users[0].allocated = set.users[0].present = true;
  set.users[0].precision = ChannelPrecision::L32F;
  set.displacement.allocated = set.displacement.channel = true;
  set.displacement.precision = ChannelPrecision::L32F;
  set.displacement.midlevel = 0;
  TextureLayer below, child, folder;
  below.kind = child.kind = LayerKind::Fill;
  folder.kind = LayerKind::Folder;
  child.parent = folder.id;
  child.depth = 1;
  below.values.base_color = {.8f, .8f, .8f, 1};
  below.values.metallic = below.values.roughness = below.values.bump =
      below.values.ambient_occlusion = .8f;
  child.values.base_color = {.5f, .5f, .5f, 1};
  child.values.metallic = child.values.roughness = child.values.bump =
      child.values.ambient_occlusion = .5f;
  below.opacity.fill(below_alpha);
  below.ao_opacity = below_alpha;
  child.opacity.fill(child_alpha);
  child.ao_opacity = child_alpha;
  child.blending.fill(LayerBlend::Multiply);
  child.ao_blending = LayerBlend::Multiply;
  folder.blending.fill(mode);
  folder.ao_blending = mode;
  folder.opacity.fill(opacity);
  folder.ao_opacity = opacity;
  folder.mask = LayerMask{};
  folder.mask->background = mask;
  for (auto *layer : {&below, &child, &folder}) {
    layer->channels[3] = false;
    layer->users[0].enabled = layer->displacement.enabled = true;
  }
  below.users[0].value = below.displacement.value = .8f;
  below.users[0].opacity = below.displacement.opacity = below_alpha;
  child.users[0].value = child.displacement.value = .5f;
  child.users[0].blending = child.displacement.blending = LayerBlend::Multiply;
  child.users[0].opacity = child.displacement.opacity = child_alpha;
  folder.users[0].blending = folder.displacement.blending = mode;
  folder.users[0].opacity = folder.displacement.opacity = opacity;
  set.layers = {below, child, folder};
  set.selected_layer = folder.id;
  TextureDocument::validate_hierarchy(set);
  return set;
}
}  // namespace bts::folder_test
