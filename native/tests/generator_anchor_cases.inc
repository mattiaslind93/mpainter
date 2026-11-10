// SPDX-License-Identifier: GPL-3.0-or-later
{
  TextureSet model;
  model.texture_resolution = 128;
  model.precision.fill(ChannelPrecision::RGB32F);
  auto &source = model.layers.front();
  source.kind = LayerKind::Fill;
  source.visible = false;
  source.channels.fill(false);
  source.values.base_color = {.2, .4, .6, 1};
  const auto source_id = source.id;
  LevelsEffect point;
  point.kind = ContentEffectKind::Anchor;
  point.anchor_name = "Generator source";
  source.effects = {point};
  TextureDocument::add_fill(model);
  const auto consumer = model.selected_layer;
  const std::array<float, 6> expected{.6f, .2f, .2f, .2f, .72f, float(.2 / std::sqrt(.44))};
  for (int generator = 1; generator <= 6; ++generator) {
    auto &layer = TextureDocument::layer(model, consumer);
    layer.values.base_color = {1, 1, 1, 1};
    layer.mask = LayerMask{};
    layer.mask->background = .15;
    MaskEffect effect;
    effect.kind = MaskEffectKind::Generator;
    effect.generator = generator;
    effect.input_anchors[0] = {point.id, 0, 0, true};
    if (generator == 5)
      effect.input_anchors[1] = {point.id, 0, 1, true};
    layer.mask->effects = {effect};
    assert(TextureDocument::anchor_dependent(layer));
    assert(std::abs(TextureDocument::mask_value(model, consumer, 64, 64) -
                    expected[generator - 1]) < 1e-6);
    const auto mask_json = mask_effects_json(layer.mask->effects);
    assert(mask_effects_json(read_mask_effects(mask_json)) == mask_json);
    layer.mask.reset();
    layer.values.base_color = {.25, .25, .25, 1};
    LevelsEffect content;
    content.kind = ContentEffectKind::Generator;
    static_cast<MeshGeneratorSettings &>(content) = effect;
    content.channel = 0;
    layer.effects = {content};
    assert(std::abs(TextureDocument::evaluate(model, 64, 64).material.base_color[0] -
                    expected[generator - 1]) < 1e-6);
    assert(effects_json(read_effects(effects_json(layer.effects))) == effects_json(layer.effects));
    layer.effects.front().input_anchors[0].source = QUuid::createUuid();
    assert(TextureDocument::evaluate(model, 64, 64).material.base_color[0] == .25f);
    layer.effects.clear();
  }
  MeshGeneratorSettings dirt;
  dirt.generator = 5;
  MeshMaps missing;
  std::vector<float> blank(32 * 32 * 4);
  auto map_pixels = std::make_shared<MeshMapPixels>(32, std::move(blank));
  MeshMap map;
  map.tiles[1001] = {32, map_pixels->hash, map_pixels};
  missing[3] = map;
  assert(!generated_mask(dirt, &missing, .5, .5, 1001));
  MeshGeneratorSettings legacy;
  const auto json = mesh_generator_json(legacy);
  assert(!json.contains("input_anchors") && !generator_has_anchors(read_mesh_generator(json)));
  auto invalid = json;
  invalid["input_anchors"] = QJsonArray{QJsonObject{}, QJsonValue()};
  bool rejected = false;
  try {
    read_mesh_generator(invalid);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  MaskEffect marker;
  marker.kind = MaskEffectKind::Anchor;
  MaskEffect generator;
  generator.kind = MaskEffectKind::Generator;
  generator.generator = 5;
  generator.input_anchors = {AnchorReference{marker.id}, AnchorReference{marker.id}};
  std::vector<MaskEffect> effects{marker, generator};
  renew_mask_effect_ids(effects);
  assert(effects[1].input_anchors[0].source == effects[0].id &&
         effects[1].input_anchors[1].source == effects[0].id);
}
