// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "LayerEditDiff.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Properties";
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials = {{1, "Properties"}};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  TextureDocument document(options, imported, scene);
  auto &set = document.set(1);
  set.channels[3] = true;
  set.precision.fill(ChannelPrecision::RGB32F);
  set.ao_channel = true;
  TextureDocument::add_fill(set);
  set.layers.back().values.base_color = {.2, .4, .6, 1};
  set.layers.back().values.metallic = .4;
  set.layers.back().values.roughness = .8;
  set.layers.back().values.bump = -.2;
  set.layers.back().ao_enabled = true;
  set.layers.back().values.ambient_occlusion = .7;
  TextureDocument::add_fill(set);
  set.layers.back().values.base_color = {.8, .2, .1, 1};
  set.layers.back().opacity.fill(.6);
  set.layers.back().blending[0] = LayerBlend::Multiply;
  TextureDocument::add_mask(set, .35);
  TextureDocument::add_paint(set);
  PaintPixels::Stamp dab{{.5, .5}, {.2, 0}, {0, .2}};
  dab.channels = {true, true, true, true, true};
  dab.values.base_color = {.1, .7, .2, 1};
  dab.values.roughness = .2;
  dab.values.bump = .15;
  set.layers.back().pixels.stamp(128, dab);
  TextureDocument::add_mask(set, .6);
  for (int y : {3, 32, 64, 95, 124})
    for (int x : {3, 32, 64, 95, 124}) {
      const auto full = TextureDocument::evaluate(set, x, y);
      for (int c = 0; c < 6; ++c) {
        std::array<bool, 6> wanted{};
        wanted[c] = true;
        const auto selected = TextureDocument::evaluate_channels(set, wanted, x, y);
        assert(precision_channel(selected.material, c, set.precision[c]) ==
               precision_channel(full.material, c, set.precision[c]));
        assert(precision_channel(selected.authored, c, set.precision[c]) ==
               precision_channel(full.authored, c, set.precision[c]));
        assert((c == 5 ? selected.ao_coverage : selected.coverage[c]) ==
               (c == 5 ? full.ao_coverage : full.coverage[c]));
        for (int other = 0; other < 6; ++other)
          if (other != c)
            assert((other == 5 ? selected.ao_coverage : selected.coverage[other]) == 0);
      }
    }
  MaskEffect compare;
  compare.kind = MaskEffectKind::Compare;
  compare.left = 1;
  compare.right = 3;
  compare.operation = 0;
  compare.compare_channel = 0;
  set.layers.back().mask->effects.push_back(compare);
  std::array<bool, 6> roughness{};
  roughness[2] = true;
  const auto full = TextureDocument::evaluate(set, 64, 64),
             coupled = TextureDocument::evaluate_channels(set, roughness, 64, 64);
  assert(coupled.material == full.material && coupled.authored == full.authored &&
         coupled.coverage == full.coverage && coupled.ao_coverage == full.ao_coverage);
  set.layers.back().mask->effects.clear();
  const auto changes = [&](const TextureDocument &next) {
    return layer_edit_channels(set,
                               next.texture_sets().front(),
                               edit_set_state(document.edit_state(), set.id),
                               edit_set_state(next.edit_state(), set.id));
  };
  auto next = document;
  next.set(1).layers[1].values.base_color[0] = .9;
  auto flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 0));
  next = document;
  next.set(1).layers[1].fill.alpha_mask[2] = true;
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 2));
  next = document;
  next.set(1).layers[1].opacity[2] = .2;
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 2));
  next = document;
  next.set(1).layers[1].values.bump = .3;
  flags = changes(next);
  assert(flags[4] && flags[3] && !flags[6]);
  next = document;
  next.set(1).layers[1].values.normal = {.2, 0, .98};
  flags = changes(next);
  assert(flags[3] && flags[6] && !flags[4]);
  auto texture = std::make_shared<FillTexture>();
  texture->asset = "c0a80001-1234-4000-8000-000000000001";
  texture->hash = std::string(64, '1');
  texture->name = "Independent resources";
  texture->raw = true;
  texture->width = texture->height = 1;
  next = document;
  fill_channel_texture(next.set(1).layers[1].fill, 2) = texture;
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 2));
  document.set(1).layers[1].fill.texture = texture;
  next = document;
  next.set(1).layers[1].fill.scale = {2, 2};
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 0));
  next = document;
  next.set(1).layers[1].fill.gizmo = 1;
  flags = changes(next);
  for (bool changed : flags)
    assert(!changed);
  next = document;
  next.set(1).layers[1].fill.mode = 2;
  flags = changes(next);
  assert(flags[0] && flags[1] && flags[2] && flags[3] && flags[4]);
  document.set(1).layers[1].fill.mode = 2;
  next = document;
  next.set(1).layers[1].fill.filtering = 1;
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 0));
  next = document;
  next.set(1).layers[1].fill.offset = {.2, 0};
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 0));
  next = document;
  next.set(1).layers[1].fill.offset3 = {.2, 0, 0};
  flags = changes(next);
  assert(flags[0] && flags[1] && flags[2] && flags[3] && flags[4]);
  document.set(1).layers[1].fill.mode = 5;
  document.set(1).layers[1].fill.warp = std::make_shared<const WarpGrid>();
  next = document;
  next.set(1).layers[1].fill.warp_edit = true;
  next.set(1).layers[1].fill.warp_handle_size = 10;
  next.set(1).layers[1].fill.warp_color = {.1, .2, .3};
  flags = changes(next);
  for (bool changed : flags)
    assert(!changed);
  next = document;
  next.set(1).layers[1].fill.shape_crop = true;
  flags = changes(next);
  assert(flags[0] && flags[1] && flags[2] && flags[3] && flags[4]);
  next = document;
  next.set(1).layers[1].fill.warp_depth = .2;
  flags = changes(next);
  assert(flags[0] && flags[1] && flags[2] && flags[3] && flags[4]);
  next = document;
  auto warp = std::make_shared<WarpGrid>();
  warp->move(4, {.1, -.4, 0});
  next.set(1).layers[1].fill.warp = warp;
  flags = changes(next);
  assert(flags[0] && flags[1] && flags[2] && flags[3] && flags[4]);

  next = document;
  next.set(1).channels[3] = false;
  flags = changes(next);
  assert(flags[3] && flags[6] && !flags[4]);
  assert(layer_edit_compatible(edit_set_state(document.edit_state(), set.id),
                               edit_set_state(next.edit_state(), set.id)));
  next = document;
  next.set(1).channels[4] = false;
  flags = changes(next);
  assert(flags[4] && flags[3] && !flags[6]);
  next = document;
  next.set(1).ao_channel = false;
  flags = changes(next);
  for (int c = 0; c < 17; ++c)
    assert(flags[c] == (c == 5));
  next = document;
  next.set(1).normal_mixing = NormalMixing::Replace;
  flags = changes(next);
  assert(flags[3] && flags[6]);
  next = document;
  next.set(1).texture_resolution = 256;
  assert(!layer_edit_compatible(edit_set_state(document.edit_state(), set.id),
                                edit_set_state(next.edit_state(), set.id)));
  {
    auto before = document;
    auto &b = before.set(1);
    b.layers = {TextureLayer{}};
    b.selected_layer = b.layers.front().id;
    b.selected_mask = b.selected_geometry = false;
    b.selected_effect = QUuid();
    auto after = before;
    auto &a = after.set(1);
    PaintPixels::Stamp ink{{.5, .5}, {.3, 0}, {0, .3}};
    ink.channels = {true, false, false, false, false};
    ink.values.base_color = {1, 0, 0, 1};
    {
      PaintRecordingScope recording;
      TextureDocument::layer(a, a.selected_layer).pixels.stamp(128, ink, a.tiles);
    }
    const auto count = PaintPixels::recipe_materializations();
    const auto old_state = edit_set_state(before.edit_state(), b.id),
               new_state = edit_set_state(after.edit_state(), a.id);
    const auto undo = layer_edit_channels(a, b, new_state, old_state),
               redo = layer_edit_channels(b, a, old_state, new_state);
    assert(undo[0] && redo[0] && PaintPixels::recipe_materializations() == count);
  }
  std::cout
      << "Requested-channel composition equals full masked/blended stack; cross-channel comparison fallback and precise color/opacity/bump/normal/resource/mapping dirty flags passed\n";
}
