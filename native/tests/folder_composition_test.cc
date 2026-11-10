// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureDocument.hh"
#include "FolderEntryPlan.hh"
#include "FolderPrefix.hh"
#include "FolderCompositionFixture.hh"
#include "FlattenLayer.hh"
#include "TextureMemory.hh"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace bts;
using bts::folder_test::fixture;
namespace {
int checks = 0, queries = 0;
std::uint64_t counted_ram()
{
  ++queries;
  return std::numeric_limits<std::uint64_t>::max();
}
void near(float actual, double expected)
{
  if (std::abs(actual - expected) > 2e-6) {
    std::fprintf(
        stderr, "folder algebra actual=%.9g expected=%.17g check=%d\n", actual, expected, checks);
    std::abort();
  }
  ++checks;
}

void check(const TextureSet &set, double premultiplied, double coverage = 1)
{
  const auto value = TextureDocument::evaluate(set, 39, 51);
  for (int c = 0; c < 3; ++c)
    near(value.authored.base_color[c] * value.coverage[0], premultiplied);
  near(value.authored.metallic * value.coverage[1], premultiplied);
  near(value.authored.roughness * value.coverage[2], premultiplied);
  near(value.authored.bump * value.coverage[4], premultiplied);
  near(value.authored.ambient_occlusion * value.ao_coverage, premultiplied);
  for (int c : {0, 1, 2, 4})
    near(value.coverage[c], coverage);
  near(value.ao_coverage, coverage);
  for (const auto scalar : {TextureDocument::composite_user(set, 0, 39, 51),
                            TextureDocument::composite_displacement(set, 39, 51)})
  {
    near(scalar[0] * scalar[1], premultiplied);
    near(scalar[1], coverage);
  }
}
void bind(FlattenedLayer &bake)
{
  for (int c = 0; c < 15; ++c)
    if (!bake.images[c].empty()) {
      auto texture = std::make_shared<FillTexture>();
      texture->asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      texture->hash = std::string(64, '0');
      texture->name = "folder algebra";
      texture->width = texture->height = 128;
      texture->pixels = bake.images[c].at(1001).pixels;
      texture->raw = c != 0;
      fill_channel_texture(bake.layer.fill, c) = texture;
    }
}
}  // namespace
int main()
{
  // Explicit float32. These numbers are independent Porter-Duff/blend equations.
  for (auto mode : {LayerBlend::Normal, LayerBlend::Passthrough})
    for (float mask : {0.f, .25f, 1.f})
      for (float opacity : {0.f, .6f, 1.f}) {
        auto set = fixture(mode, mask, opacity);
        const double full = mode == LayerBlend::Passthrough ? .8 * .5 : .5;
        check(set, .8 + (full - .8) * double(mask) * opacity);
      }
  check(fixture(LayerBlend::Normal), .755);
  check(fixture(LayerBlend::Passthrough), .74);
  // Partial coverage: below=(.2,.25), full contextual child=(.31,.55).
  // Folder weight=.15 -> premultiplied(.2165,.295), never multiply coverage twice.
  check(fixture(LayerBlend::Passthrough, .25f, .6f, .4f, .25f), .2165, .295);
  check(fixture(LayerBlend::Normal, .25f, .6f, .4f, .25f), .218, .295);
  for (auto outer_mode : {LayerBlend::Normal, LayerBlend::Passthrough}) {
    auto set = fixture(outer_mode);
    auto inner = set.layers.back();
    inner.id = QUuid::createUuid();
    inner.parent = set.layers.back().id;
    inner.depth = 1;
    inner.opacity.fill(.4f);
    inner.ao_opacity = .4f;
    inner.users[0].opacity = inner.displacement.opacity = .4f;
    inner.blending.fill(LayerBlend::Passthrough);
    inner.ao_blending = inner.users[0].blending = inner.displacement.blending =
        LayerBlend::Passthrough;
    inner.mask->background = .5f;
    set.layers[1].parent = inner.id;
    set.layers[1].depth = 2;
    set.layers.insert(set.layers.end() - 1, inner);
    TextureDocument::validate_hierarchy(set);
    check(set, outer_mode == LayerBlend::Passthrough ? .788 : .791);
    const auto entries = folder_entry_plan(set.layers);
    assert(entries.size() == 2 && entries.back().folder == 3 && entries.back().first == 1);
    // Same source -> edit -> Undo; the folder mask is applied after the full subtree.
    const auto before = set;
    set.layers.back().mask->background = 0;
    check(set, .8);
    set = before;
    check(set, outer_mode == LayerBlend::Passthrough ? .788 : .791);
  }
  {
    auto set = fixture(LayerBlend::Passthrough, .25f, .6f, .4f, .25f);
    set.layers.erase(set.layers.begin() + 1);
    check(set, .2, .25);
  }  // Empty passthrough is identity, including coverage.
  {
    auto set = fixture(LayerBlend::Passthrough);
    set.layers[1].visible = false;
    check(set, .8);
    set.layers.back().mask->enabled = false;
    set.layers[1].visible = true;
    check(set, .56);
  }
  // Retain ancestor context when capturing a nested prefix for Flatten.
  {
    auto set = fixture(LayerBlend::Passthrough);
    auto outer = set.layers.back();
    outer.id = QUuid::createUuid();
    outer.mask.reset();
    outer.opacity.fill(1);
    outer.ao_opacity = outer.users[0].opacity = outer.displacement.opacity = 1;
    set.layers.back().parent = outer.id;
    set.layers.push_back(outer);
    TextureDocument::validate_hierarchy(set);
    const auto target = set.layers[2].id;
    const auto prefix = folder_prefix_composite(set, target, 39, 51, 1001);
    near(prefix.authored.roughness * prefix.coverage[2], .74);
    const auto scalar = folder_prefix_user(set, target, 0, 39, 51, 1001);
    near(scalar[0] * scalar[1], .74);
    std::atomic_bool cancel = false;
    auto bake = flatten_layer_pixels(set, target, cancel);
    assert(bake && bake->layer.blending[2] == LayerBlend::Replace);
    bind(*bake);
    install_flattened_layer(set, bake->layer, target);
    check(set, .74);
  }
  {
    auto set = fixture(LayerBlend::Passthrough);
    TextureRamQueryScope scope(counted_ram);
    queries = 0;
    for (int i = 0; i < 4; ++i)
      check(set, .74);
    assert(queries == 0);
  }  // No per-pixel MemoryBudget queries.
  std::printf(
      "folder composition: %d independent scalar/color/AO/coverage assertions; Normal=.755 Passthrough=.74 nested/prefix/Flatten/empty/mask/opacity; RAM queries0 PASS\n",
      checks);
}
