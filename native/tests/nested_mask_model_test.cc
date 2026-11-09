#include "TextureDocument.hh"
#include "MaskEffectPath.hh"
#include "RenameCommand.hh"
#include "DocumentEditCommit.hh"
#include "StrokeArchive.hh"
#include "MaskPreset.hh"
#include <cstdio>
using namespace bts;
namespace {
int checks = 0;
int ram_queries = 0;
std::uint64_t counted_ram()
{
  ++ram_queries;
  return std::numeric_limits<std::uint64_t>::max();
}
void require(bool value, const char *message)
{
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}
template<class F> void rejects(F &&run, const char *message)
{
  bool failed = false;
  try {
    run();
  }
  catch (const std::invalid_argument &) {
    failed = true;
  }
  require(failed, message);
}
MaskEffect group(std::vector<MaskEffect> children)
{
  MaskEffect result;
  result.kind = MaskEffectKind::Group;
  result.group_effects = std::make_shared<const std::vector<MaskEffect>>(std::move(children));
  return result;
}
struct Fixture {
  std::unique_ptr<TextureDocument> document;
  QUuid layer, paint, levels, anchor, fill, outer, inner;
  Fixture()
  {
    SceneSnapshot scene;
    MaterialSummary material;
    material.session_id = 17;
    material.name = "Nested mask model";
    material.uniform_editable = true;
    scene.materials = {material};
    ImportSummary imported;
    imported.materials = {{17, material.name}};
    document = std::make_unique<TextureDocument>(NewProjectOptions{}, imported, scene);
    auto &s = set();
    s.texture_resolution = 128;
    s.texture_height = 128;
    layer = s.selected_layer;
    s.selected_mask = true;
    MaskEffect p, l, a, f;
    p.kind = MaskEffectKind::Paint;
    l.kind = MaskEffectKind::Levels;
    a.kind = MaskEffectKind::Anchor;
    a.anchor_name = "Nested producer";
    f.kind = MaskEffectKind::Fill;
    paint = p.id;
    levels = l.id;
    anchor = a.id;
    fill = f.id;
    auto nested = group({l, p, a, f});
    inner = nested.id;
    auto root = group({nested, group({MaskEffect{}})});
    outer = root.id;
    s.layers.front().mask = LayerMask{};
    s.layers.front().mask->effects = {root, group({MaskEffect{}})};
    s.selected_effect = paint;
  }
  TextureSet &set()
  {
    return document->set(17);
  }
  std::vector<MaskEffect> &effects()
  {
    return TextureDocument::layer(set(), layer).mask->effects;
  }
};
void lookups_and_history()
{
  Fixture f;
  const auto old_parent = f.effects()[0].group_effects,
             old_inner = old_parent->front().group_effects,
             old_sibling = f.effects()[1].group_effects,
             old_cousin = old_parent->at(1).group_effects;
  require(TextureDocument::paint_effect(std::as_const(f.set())) != nullptr,
          "Const nested Paint lookup failed");
  require(f.effects()[0].group_effects == old_parent, "Const Paint lookup detached ancestor");
  int visited = 0;
  visit_effect_paint(f.set(), [&](QUuid, QUuid, bool, const EffectPaint &) { ++visited; });
  require(visited == 1 && f.effects()[0].group_effects == old_parent,
          "Const Paint visitor mutated owners or omitted nested ink");
  f.set().selected_effect = f.levels;
  require(!TextureDocument::paint_effect(f.set()), "Mutable Paint lookup accepted Levels");
  require(f.effects()[0].group_effects == old_parent, "Wrong-kind Paint lookup detached ancestor");
  f.set().selected_effect = f.paint;
  struct Entry {
    std::unique_ptr<TextureDocument> state;
    std::uint32_t material_uid;
  };
  std::vector<Entry> undo, redo;
  const auto original = f.document->to_json();
  auto candidate = std::make_unique<TextureDocument>(*f.document);
  rename_layer_item(candidate->set(17), f.layer, f.paint, "  Nested ink  ");
  auto *paint = TextureDocument::paint_effect(candidate->set(17));
  paint->resolution = 128;
  PaintPixels::Stamp stamp;
  stamp.center = {.5, .5};
  stamp.axis_x = {.1, 0};
  stamp.axis_y = {0, .1};
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = .25f;
  paint->pixels.stamp(128, stamp);
  auto prepared = prepare_document_commit(undo, 17);
  publish_document_commit(
      f.document, candidate, undo, redo, std::move(prepared), []() noexcept {});
  require(undo.size() == 1 && redo.empty(), "Nested edit was not one existing Undo commit");
  require(undo.back().state->to_json() == original, "Nested edit modified Undo snapshot");
  require(find_mask_effect(f.effects(), f.paint)->name == "Nested ink",
          "Nested rename did not target exact UUID");
  require(!TextureDocument::paint_effect(std::as_const(f.set()))->pixels.empty(),
          "Nested Paint stroke was lost");
  require(f.effects()[0].group_effects != old_parent &&
              f.effects()[0].group_effects->front().group_effects != old_inner,
          "Nested edit did not detach ancestors");
  require(f.effects()[1].group_effects == old_sibling &&
              f.effects()[0].group_effects->at(1).group_effects == old_cousin,
          "Nested edit detached unrelated Groups");
  const auto edited = f.document->to_json();
  redo.push_back({std::move(f.document), 17});
  f.document = std::move(undo.back().state);
  undo.pop_back();
  require(f.document->to_json() == original, "Undo did not restore exact nested tree");
  undo.push_back({std::move(f.document), 17});
  f.document = std::move(redo.back().state);
  redo.pop_back();
  require(f.document->to_json() == edited, "Redo did not restore exact nested tree");
  const std::array<TextureSetBinding, 1> bindings{{{f.set().id, 17, f.set().name}}};
  auto reopened = TextureDocument::from_json(edited, bindings);
  read_stroke_archive(reopened, write_stroke_archive(*f.document));
  require(reopened.to_json() == edited, "Nested Paint archive changed metadata");
  require(TextureDocument::paint_effect(std::as_const(reopened.set(17)))->pixels.hash(2) ==
              TextureDocument::paint_effect(std::as_const(f.set()))->pixels.hash(2),
          "Nested Paint archive omitted stroke pixels");
  rejects([&] { rename_layer_item(f.set(), f.layer, f.paint, " "); },
          "Empty nested rename accepted");
}
void move_and_remove()
{
  Fixture f;
  const auto snapshot = f.set();
  const auto sibling = f.effects()[1].group_effects;
  TextureDocument::move_effect(f.set(), -1);
  require(find_mask_effect_path(f.effects(), f.paint) == std::optional<MaskEffectPath>{{0, 0, 0}},
          "Nested move escaped its sibling scope");
  const auto at_boundary = f.effects()[0].group_effects;
  TextureDocument::move_effect(f.set(), -1);
  require(f.effects()[0].group_effects == at_boundary,
          "Boundary move detached an unchanged ancestor");
  TextureDocument::move_effect(f.set(), 1);
  require(find_mask_effect_path(f.effects(), f.paint) == std::optional<MaskEffectPath>{{0, 0, 1}},
          "Nested move back changed order");
  TextureDocument::remove_effects(f.set(), {{f.layer, f.paint}});
  require(!find_mask_effect(f.effects(), f.paint) && f.set().selected_effect.isNull(),
          "Nested delete missed UUID/selection");
  require(find_mask_effect(f.effects(), f.levels) && find_mask_effect(f.effects(), f.fill),
          "Nested delete removed siblings");
  require(f.effects()[1].group_effects == sibling, "Nested move/delete detached unrelated root");
  f.set() = snapshot;
  TextureDocument::remove_effects(f.set(), {{f.layer, f.inner}});
  require(!find_mask_effect(f.effects(), f.inner) && f.set().selected_effect.isNull(),
          "Deleting ancestor retained dangling selected child");
  require(find_mask_effect(*snapshot.layers.front().mask->effects[0].group_effects, f.paint),
          "Nested delete modified original snapshot");
}
void anchors_and_paste()
{
  Fixture f;
  TextureDocument::rename_mask_anchor(f.set(), f.anchor, " Renamed nested anchor ");
  require(TextureDocument::anchor_point(f.set(), f.anchor)->name == "Renamed nested anchor",
          "Nested anchor metadata/rename missing");
  TextureLayer consumer;
  consumer.kind = LayerKind::Fill;
  const auto consumer_id = consumer.id;
  f.set().layers.push_back(consumer);
  require(TextureDocument::mask_anchor_problem(f.set(), consumer_id, f.anchor)
              .contains("Expose an Anchor after the Group"),
          "Nested producer was offered unsupported cross-Group semantics");
  TextureLayer producer;
  producer.kind = LayerKind::Fill;
  producer.visible = false;
  producer.mask = LayerMask{};
  MaskEffect anchor;
  anchor.kind = MaskEffectKind::Anchor;
  anchor.anchor_name = "Supported lower anchor";
  producer.mask->effects = {anchor};
  f.set().layers.insert(f.set().layers.begin(), producer);
  TextureDocument::assign_mask_anchor(f.set(), f.fill, anchor.id);
  require(find_mask_effect(f.effects(), f.fill)->anchor_source == anchor.id,
          "Nested Fill rejected supported lower-layer anchor");
  LevelsEffect content;
  content.kind = ContentEffectKind::Anchor;
  content.anchor_name = "Content producer";
  f.set().layers.front().effects.push_back(content);
  require(
      TextureDocument::mask_effect_anchor_reference_problem(f.set(), f.layer, f.fill, {content.id})
          .contains("top-level Fill"),
      "Nested content anchor limitation was not diagnosed");
  rejects([&] { TextureDocument::assign_mask_anchor(f.set(), f.fill, content.id); },
          "Nested Fill admitted unsupported content anchor context");
  MaskEffect generator;
  generator.kind = MaskEffectKind::Generator;
  auto *parent = edit_mask_effect_siblings(f.effects(),
                                           *find_mask_effect_path(f.effects(), f.fill));
  parent->push_back(generator);
  require(TextureDocument::mask_effect_anchor_reference_problem(
              f.set(), f.layer, generator.id, {anchor.id})
              .contains("top-level Generator"),
          "Nested Generator anchor limitation was not diagnosed");
  const auto owner = f.effects()[0].group_effects;
  rejects([&] { TextureDocument::assign_mask_anchor(f.set(), f.paint, anchor.id); },
          "Anchor assignment accepted wrong nested kind");
  require(f.effects()[0].group_effects == owner, "Wrong-kind anchor edit detached ancestor");
  rejects(
      [&] {
        TextureDocument::assign_mask_anchor(f.set(), f.fill, anchor.id, QUuid::createUuid());
      },
      "Cross-set anchor assignment accepted");
  LayerMask invalid;
  MaskEffect paint;
  paint.kind = MaskEffectKind::Paint;
  std::vector<float> pixels(128 * 128 * 4, .5f);
  paint.paint.pixels.assign_channel(128, 2, pixels, 1002);
  invalid.effects = {group({paint})};
  const auto before = f.document->to_json();
  rejects([&] { TextureDocument::paste_mask(f.set(), f.layer, invalid); },
          "Paste missed nested Paint UV tile");
  rejects([&] { TextureDocument::apply_mask_preset(f.set(), f.layer, invalid, true); },
          "Preset paste missed nested Paint UV tile");
  require(f.document->to_json() == before, "Invalid nested paste changed document");
}
void selected_reopen()
{
  Fixture f;
  const auto json = f.document->to_json();
  const std::array<TextureSetBinding, 1> bindings{{{f.set().id, 17, f.set().name}}};
  auto reopened = TextureDocument::from_json(json, bindings);
  require(reopened.set(17).selected_effect == f.paint && reopened.to_json() == json,
          "Nested selected UUID did not survive document JSON reopen");
  auto duplicate = *f.document;
  auto &effects = duplicate.set(17).layers.front().mask->effects;
  edit_mask_effect(effects, f.paint)->id = f.outer;
  duplicate.set(17).selected_effect = QUuid();
  rejects([&] { TextureDocument::from_json(duplicate.to_json(), bindings); },
          "Nested UUID collision across levels accepted");
}
void instances()
{
  SceneSnapshot scene;
  ImportSummary imported;
  for (int uid : {21, 22}) {
    MaterialSummary material;
    material.session_id = uid;
    material.name = "Instance " + std::to_string(uid);
    material.uniform_editable = true;
    scene.materials.push_back(material);
    imported.materials.push_back({std::uint32_t(uid), material.name});
  }
  TextureDocument document({}, imported, scene);
  auto &source = document.set(21), &target = document.set(22);
  auto &layer = source.layers.front();
  layer.name = "Nested source";
  layer.mask = LayerMask{};
  MaskEffect paint, levels;
  paint.kind = MaskEffectKind::Paint;
  levels.kind = MaskEffectKind::Levels;
  levels.levels.id = levels.id;
  layer.mask->effects = {group({levels, group({paint})})};
  const auto source_layer = layer.id, source_set = source.id;
  const auto source_owner = layer.mask->effects[0].group_effects;
  const auto same_a = TextureDocument::add_instance(source, source_set, source_layer, "Same A");
  const auto same_b = TextureDocument::add_instance(source, source_set, source_layer, "Same B");
  const auto cross = TextureDocument::add_instance(target, source_set, source_layer, "Cross set");
  document.resolve_instances();
  const auto expanded = [&](const TextureSet &set, const QUuid &id) -> const TextureLayer & {
    const auto &rows = evaluation_layers(set);
    return *std::find_if(rows.begin(), rows.end(), [&](const auto &row) { return row.id == id; });
  };
  for (const auto &id : {same_a, same_b}) {
    const auto &copy = expanded(source, id);
    const auto expected = QUuid::createUuidV5(id, paint.id.toByteArray());
    require(find_mask_effect(copy.mask->effects, expected) != nullptr,
            "Same-set instance did not remap nested Paint UUID");
    require(!find_mask_effect(copy.mask->effects, paint.id),
            "Instance retained original nested UUID");
    const auto *mapped = find_mask_effect(copy.mask->effects,
                                          QUuid::createUuidV5(id, levels.id.toByteArray()));
    require(mapped && mapped->levels.id == mapped->id,
            "Nested Levels secondary UUID was not remapped");
  }
  const auto &copied = expanded(target, cross);
  require(copied.mask.has_value(), "Cross-set instance lost mask Group");
  for (const auto &entry : mask_effect_entries(copied.mask->effects))
    require(entry.effect->kind != MaskEffectKind::Paint,
            "Cross-set instance retained nested Paint");
  require(TextureDocument::layer(source, source_layer).mask->effects[0].group_effects ==
              source_owner,
          "Instance remapping changed source Group ownership");
  const auto json = document.to_json();
  std::vector<TextureSetBinding> bindings;
  for (const auto &set : document.texture_sets())
    bindings.push_back({set.id, set.material_session_uid, set.name});
  auto reopened = TextureDocument::from_json(json, bindings);
  read_stroke_archive(reopened, write_stroke_archive(document));
  reopened.resolve_instances();
  require(reopened.to_json() == json, "Nested instance metadata changed on archive reopen");
  require(find_mask_effect(expanded(reopened.set(21), same_a).mask->effects,
                           QUuid::createUuidV5(same_a, paint.id.toByteArray())) != nullptr,
          "Reopen lost deterministic nested instance identity");
}
void flat_anchor_budget()
{
  TextureSet set;
  set.texture_resolution = 128;
  set.layers.clear();
  TextureLayer producer, consumer;
  producer.kind = consumer.kind = LayerKind::Fill;
  producer.values.base_color = {.2f, .3f, .4f, 1};
  producer.mask = LayerMask{};
  producer.mask->background = .37f;
  MaskEffect point;
  point.kind = MaskEffectKind::Anchor;
  producer.mask->effects = {point};
  LevelsEffect content;
  content.kind = ContentEffectKind::Anchor;
  producer.effects = {content};
  set.layers = {producer, consumer};
  const AnchorReference mask_ref{point.id}, content_ref{content.id};
  const AnchorSampleContext context{
      &set, consumer.id, 64, 64, 1001, &TextureDocument::anchor_sample};
  TextureRamQueryScope memory(counted_ram);
  ram_queries = 0;
  const auto found = TextureDocument::anchor_point(set, point.id);
  require(found && found->mask && ram_queries == 0, "Flat mask Anchor lookup queried RAM");
  ram_queries = 0;
  require(TextureDocument::mask_anchor_problem(set, consumer.id, point.id).isEmpty() &&
              ram_queries == 0,
          "Flat mask Anchor validation allocated a path or queried RAM");
  ram_queries = 0;
  require(TextureDocument::anchor_reference_problem(set, consumer.id, mask_ref).isEmpty() &&
              ram_queries == 0,
          "Flat mask reference validation queried RAM");
  ram_queries = 0;
  const auto mask = TextureDocument::anchor_sample(context, mask_ref);
  require(mask.valid && mask.mask && std::abs(mask.rgba[0] - .37f) < 1e-6 && ram_queries == 0,
          "Flat MASK anchor_sample chain queried RAM or changed its value");
  ram_queries = 0;
  const auto color = TextureDocument::anchor_sample(context, content_ref);
  std::fprintf(stderr,
               "FLAT_CONTENT_ANCHOR valid=%d mask=%d rgba=%.9g,%.9g,%.9g,%.9g ram_queries=%d\n",
               color.valid,
               color.mask,
               color.rgba[0],
               color.rgba[1],
               color.rgba[2],
               color.rgba[3],
               ram_queries);
  require(ram_queries == 0,
          "Flat CONTENT anchor_sample chain queried RAM");  // Default SRGB8 stores .2/.3/.4 as
                                                            // encoded bytes 124/149/170.
                                                            // Independently decode the raw prefix.
  const auto decode = [](int byte) {
    const double encoded = byte / 255.;
    return std::pow((encoded + .055) / 1.055, 2.4);
  };
  require(color.valid && !color.mask && std::abs(color.rgba[0] - decode(124)) < 1e-6 &&
              std::abs(color.rgba[1] - decode(149)) < 1e-6 &&
              std::abs(color.rgba[2] - decode(170)) < 1e-6,
          "Flat CONTENT anchor_sample changed raw SRGB8 RGB");
}
void nested_preset_and_dependencies()
{
  Fixture f;
  auto *paint = TextureDocument::paint_effect(f.set());
  paint->resolution = 128;
  PaintPixels::Stamp stamp;
  stamp.center = {.5, .5};
  stamp.axis_x = {.1, 0};
  stamp.axis_y = {0, .1};
  stamp.channels = {false, false, true, false, false};
  stamp.values.roughness = .29f;
  paint->pixels.stamp(128, stamp);
  const auto expected = paint->pixels.hash(2);
  auto &authored_mask = *TextureDocument::layer(f.set(), f.layer).mask;
  MaskEffect top;
  top.kind = MaskEffectKind::Paint;
  top.paint.resolution = 128;
  stamp.values.roughness = .73f;
  top.paint.pixels.stamp(128, stamp);
  const auto top_hash = top.paint.pixels.hash(2);
  authored_mask.effects.push_back(top);
  const auto &mask = authored_mask;
  const auto owner = mask.effects[0].group_effects;
  const auto bytes = write_mask_preset(mask);
  const auto reopened = read_mask_preset(bytes);
  const auto *restored = find_mask_effect(reopened.effects, f.paint);
  require(restored && restored->paint.loaded && restored->paint.pixels.hash(2) == expected,
          "Smart-mask preset lost nested Paint raster payload");
  require(find_mask_effect(reopened.effects, top.id)->paint.pixels.hash(2) == top_hash,
          "Smart-mask raster order confused top-level and nested Paint payloads");
  require(mask.effects[0].group_effects == owner,
          "Writing nested preset detached the source Group");
  MaskEffect fill;
  fill.kind = MaskEffectKind::Fill;
  fill.anchor_source = QUuid::createUuid();
  LayerMask dependency;
  dependency.effects = {group({group({fill})})};
  require(dependency.channel_dependent(), "Nested supported mask Anchor input missing dependency");
  require(!dependency.uses_material_channels(),
          "Mask Anchor falsely classified as material Compare");
  dependency.effects.front().enabled = false;
  require(!dependency.channel_dependent(), "Disabled ancestor Group retained dependency");
  dependency.effects.front().enabled = true;
  dependency.effects.front().opacity = 0;
  require(!dependency.channel_dependent(), "Zero-opacity ancestor Group retained dependency");
  dependency.effects.front().opacity = 1;
  dependency.effects.front().blending = LayerBlend::Disabled;
  require(!dependency.channel_dependent(), "Disabled Group blend retained dependency");
  MaskEffect compare;
  compare.kind = MaskEffectKind::Compare;
  compare.left = 0;
  dependency.effects = {group({group({compare})})};
  require(dependency.uses_material_channels() && dependency.channel_dependent(),
          "Nested Compare material dependency omitted");
  edit_mask_effect(dependency.effects, compare.id)->enabled = false;
  require(!dependency.uses_material_channels() && !dependency.channel_dependent(),
          "Disabled nested Compare retained dependency");
}
}  // namespace
int main()
{
  try {
    lookups_and_history();
    move_and_remove();
    anchors_and_paste();
    selected_reopen();
    instances();
    nested_preset_and_dependencies();
    flat_anchor_budget();
    std::printf("NESTED_MASK_MODEL PASS checks=%d existing_document_undo=1\n", checks);
    return 0;
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "NESTED_MASK_MODEL FAILED: %s\n", error.what());
    return 1;
  }
}
