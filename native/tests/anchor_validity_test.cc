// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  SceneSnapshot scene;
  MaterialSummary m;
  m.session_id = 1;
  m.name = "Validity";
  m.uniform_editable = true;
  scene.materials = {m};
  ImportSummary imported;
  imported.materials = {{1, m.name}};
  TextureDocument doc({}, imported, scene);
  auto &set = doc.set(1);
  TextureDocument::set_resolution(set, 128);
  set.precision[4] = ChannelPrecision::L32F;
  TextureDocument::add_fill(set);
  const auto source = set.selected_layer;
  TextureDocument::add_mask(set, 0);
  MaskEffect fill;
  fill.kind = MaskEffectKind::Fill;
  fill.fill = .75;
  TextureDocument::layer(set, source).mask->effects.push_back(fill);
  TextureDocument::add_mask_anchor(set);
  const auto anchor = set.selected_effect;
  TextureDocument::rename_mask_anchor(set, anchor, "Shared name");
  TextureDocument::add_fill(set);
  const auto consumer = set.selected_layer;
  TextureDocument::add_mask(set, 0);
  MaskEffect reference;
  reference.kind = MaskEffectKind::Fill;
  TextureDocument::layer(set, consumer).mask->effects.push_back(reference);
  set.selected_effect = reference.id;
  TextureDocument::assign_mask_anchor(set, reference.id, anchor, set.id);
  assert(TextureDocument::mask_value(set, consumer, 64, 64) == .75);
  MaskEffect compare;
  compare.kind = MaskEffectKind::Compare;
  compare.left = 1;
  compare.right = 3;
  compare.constant = 0;
  compare.hardness = 1;
  TextureDocument::layer(set, source).values.bump = .1;
  TextureDocument::layer(set, source).mask->effects.push_back(compare);
  assert(TextureDocument::layer(set, source).mask->uses_material_channels() &&
         TextureDocument::mask_value(set, consumer, 64, 64) == .75);
  std::swap(TextureDocument::layer(set, source).mask->effects[1],
            TextureDocument::layer(set, source).mask->effects[2]);
  assert(TextureDocument::mask_value(set, consumer, 64, 64) == 1);
  TextureDocument::layer(set, source).values.bump = -.2;
  assert(TextureDocument::mask_value(set, consumer, 64, 64) == 0);
  TextureDocument::layer(set, source).mask->effects[1].enabled = false;
  assert(!TextureDocument::layer(set, source).mask->uses_material_channels() &&
         TextureDocument::mask_value(set, consumer, 64, 64) == .75);
  TextureDocument::layer(set, source).mask->effects[2].enabled = false;
  assert(TextureDocument::mask_value(set, consumer, 64, 64) == 0 &&
         TextureDocument::mask_anchor_problem(set, consumer, anchor).contains("disabled"));
  TextureDocument::layer(set, source).mask->effects[2].enabled = true;
  const auto valid = doc;
  TextureDocument::move_layer(set, source, 1);
  assert(TextureDocument::mask_value(set, consumer, 64, 64) == 0 &&
         TextureDocument::mask_anchor_problem(set, consumer, anchor).contains("lower"));
  doc = valid;
  auto &restored = doc.set(1);
  assert(TextureDocument::mask_value(restored, consumer, 64, 64) == .75);
  std::erase_if(restored.layers, [&](const auto &layer) { return layer.id == source; });
  assert(TextureDocument::mask_value(restored, consumer, 64, 64) == 0);
  TextureDocument::add_fill(restored);
  const auto replacement = restored.selected_layer;
  TextureDocument::add_mask(restored, .25);
  TextureDocument::add_mask_anchor(restored);
  const auto new_anchor = restored.selected_effect;
  TextureDocument::rename_mask_anchor(restored, new_anchor, "Shared name");
  assert(new_anchor != anchor && TextureDocument::mask_value(restored, consumer, 64, 64) == 0);
  TextureDocument::move_layer(restored, replacement, -1);
  assert(TextureDocument::mask_value(restored, consumer, 64, 64) == 0);
  auto &mask = *TextureDocument::layer(restored, consumer).mask;
  mask.background = 1;
  mask.effects[0].opacity = .5;
  assert(TextureDocument::mask_value(restored, consumer, 64, 64) == .5);
  restored.selected_layer = consumer;
  restored.selected_mask = true;
  restored.selected_effect = reference.id;
  const auto before = doc.to_json();
  bool rejected = false;
  try {
    TextureDocument::assign_mask_anchor(restored, reference.id, new_anchor, QUuid::createUuid());
  }
  catch (const std::exception &) {
    rejected = true;
  }
  assert(rejected && doc.to_json() == before);
  TextureDocument::assign_mask_anchor(restored, reference.id, new_anchor, restored.id);
  assert(TextureDocument::mask_value(restored, consumer, 64, 64) == .625);
  std::vector<TextureSetBinding> bindings{{restored.id, 1, restored.name}};
  assert(TextureDocument::from_json(doc.to_json(), bindings).to_json() == doc.to_json());
  std::cout
      << "Scalar anchor graph and PBR-comparison fallback, disabled/higher/missing sources, no name rebinding, black input mixing, explicit recovery and atomic cross-set rejection passed\n";
}
