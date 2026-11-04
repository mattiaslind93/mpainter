#include "BuiltinSmartMasks.hh"
#include <array>

namespace bts {
namespace {
struct Recipe {
  const char *name;
  const char *slug;
  int generator;
  float contrast, bias, detail, scale;
  int pattern;
};
constexpr std::array recipes{
  Recipe{"Oxide Flecks", "oxide-flecks", 9, 1.8f, -.05f, .72f, 2.8f, 1},
  Recipe{"Exposed Edges", "exposed-edges", 8, 2.f, .20f, .65f, 3.4f, 0},
  Recipe{"Crevice Sediment", "crevice-sediment", 9, 2.f, .30f, .80f, 3.2f, 0},
  Recipe{"Organic Growth", "organic-growth", 9, 1.4f, -.12f, .78f, 4.2f, 0},
  Recipe{"Ground Contact", "ground-contact", 1, 2.f, -.1f, .80f, 3.8f, 0},
  Recipe{"Surface Scuffs", "surface-scuffs", 8, 2.f, .15f, .90f, 2.f, 2},
  Recipe{"Gravity Streaks", "gravity-streaks", 9, 1.4f, .2f, .92f, 2.f, 3},
  Recipe{"Settled Dust", "settled-dust", 6, 1.2f, .02f, .55f, 2.4f, 0},
  Recipe{"Height Fade", "height-fade", 1, 1.f, 0.f, 0.f, 1.f, 0},
  Recipe{"Curvature Ridge", "curvature-ridge", 2, 1.8f, .05f, 0.f, 1.f, 0},
};
std::vector<StudioAsset> make_assets()
{
  std::vector<StudioAsset> assets;
  for (const auto &recipe : recipes) {
    LayerMask mask;
    mask.background = 0;
    MaskEffect effect;
    effect.kind = MaskEffectKind::Generator;
    effect.name = recipe.name;
    effect.generator = recipe.generator;
    effect.response = 1;
    effect.optional_inputs = true;
    effect.surface_fallback = true;
    effect.triplanar_grunge = true;
    effect.map_filter = 1;
    effect.contrast = recipe.contrast;
    effect.bias = recipe.bias;
    effect.grunge_amount = recipe.detail;
    effect.grunge_scale = recipe.scale;
    effect.grunge_pattern = recipe.pattern;
    effect.ao_influence = .8f;
    effect.curvature_influence = .8f;
    if (recipe.generator == 6) {
      effect.light_direction = {0, .35f, .93675f};
      effect.curve = 1;
    }
    const std::string_view slug(recipe.slug);
    // Surface grunge is an independent procedural input: these presets remain
    // useful on smooth/unoccluded areas while mesh maps still influence them.
    if (slug == "oxide-flecks" || slug == "exposed-edges" ||
        slug == "crevice-sediment" || slug == "surface-scuffs") effect.response = 4;
    if (slug == "ground-contact" || slug == "height-fade") effect.axis = 1;
    if (slug == "ground-contact") { effect.invert = true; effect.curve = 1; }
    if (slug == "crevice-sediment") { effect.thickness_influence = .65f; effect.curvature_influence = 1; }
    if (slug == "organic-growth") { effect.response = 3; effect.axis = 1; effect.position_influence = .5f; }
    if (slug == "gravity-streaks") { effect.response = 2; effect.axis = 1; effect.ao_influence = .6f; effect.curvature_influence = 1; }
    mask.effects.push_back(effect);
    StudioAsset asset;
    asset.name = recipe.name;
    asset.filename = QString::fromLatin1(recipe.slug) + ".btsmask";
    asset.id = QUuid::createUuidV5(QUuid("{954c6511-3b9d-4f1f-bbdd-173dfbb99b69}"), asset.filename.toUtf8());
    asset.scope = AssetScope::Builtin;
    asset.usages = {"smartmask"};
    asset.search_terms = {"builtin:native-smart-mask", "mesh-maps", "procedural", "editable"};
    asset.smart_mask = std::make_shared<const LayerMask>(std::move(mask));
    asset.bytes = write_mask_preset(*asset.smart_mask);
    hydrate_asset_presets(asset);
    assets.push_back(std::move(asset));
  }
  return assets;
}
}
const std::vector<StudioAsset> &builtin_smart_mask_assets()
{
  static const auto assets = make_assets();
  return assets;
}
std::uint64_t builtin_smart_mask_rcc_decodes() { return 0; }
}
