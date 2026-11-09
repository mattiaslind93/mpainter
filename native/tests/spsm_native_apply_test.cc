// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmMaterialImport.hh"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <set>
using namespace bts;
namespace {
void require(bool yes, const char *message)
{
  if (!yes)
    throw std::runtime_error(message);
}
TextureDocument document()
{
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  ImportSummary imported;
  imported.materials = {{1, "Destination", {1001, 1017, 2000}}};
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = 1;
  material.name = "Destination";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  return TextureDocument(options, imported, scene);
}
void same(const UniformComposite &a, const UniformComposite &b)
{
  for (int c = 0; c < 4; ++c)
    require(std::abs(a.material.base_color[c] - b.material.base_color[c]) < 1e-6f,
            "native color changed across codec");
  require(std::abs(a.material.metallic - b.material.metallic) < 1e-6f &&
              std::abs(a.material.roughness - b.material.roughness) < 1e-6f &&
              std::abs(a.material.bump - b.material.bump) < 1e-6f,
          "native scalar changed across codec");
}
void legacy_uniform_contract()
{
  const auto layer = [](LayerKind kind, float bump, int depth, const QUuid &parent) {
    TextureLayer l;
    l.kind = kind;
    l.values.bump = bump;
    l.depth = depth;
    l.parent = parent;
    l.channels = {false, false, false, false, true};
    l.ao_enabled = false;
    l.mask.emplace();
    l.mask->background = .8f;
    return l;
  };
  for (bool first_only : {true, false})
    for (auto root_mode : {LayerBlend::Normal, LayerBlend::Replace, LayerBlend::SignedAddition})
      for (auto inner_mode : {LayerBlend::Normal, LayerBlend::Replace, LayerBlend::SignedAddition})
      {
        PreparedSpsm prepared;
        prepared.material.set.layers.clear();
        auto root = layer(LayerKind::Folder, .5f, 0, {});
        root.name = "root";
        root.opacity[4] = .77f;
        root.blending[4] = root_mode;
        auto inner = layer(LayerKind::Folder, .5f, 1, root.id);
        inner.name = "inner";
        inner.opacity[4] = .47f;
        inner.blending[4] = inner_mode;
        auto first = layer(LayerKind::Fill, .8f, 2, inner.id);
        first.name = "first isolated child";
        first.opacity[4] = .37f;
        first.blending[4] = LayerBlend::SignedAddition;
        PaintPixels::Stamp scalar_mask{{.2, .2}, {.12, 0}, {0, .12}, {}};
        scalar_mask.channels = {false, false, true, false, false};
        scalar_mask.values.roughness = .1f;
        scalar_mask.hardness = 1;
        first.mask->pixels.stamp(128, scalar_mask, {1001});
        prepared.material.set.layers.push_back(first);
        if (!first_only) {
          auto second = layer(LayerKind::Fill, .2f, 2, inner.id);
          second.name = "second child";
          second.opacity[4] = .61f;
          second.blending[4] = LayerBlend::Replace;
          prepared.material.set.layers.push_back(second);
        }
        prepared.material.set.layers.push_back(inner);
        prepared.material.set.layers.push_back(root);
        prepared.material.set.selected_layer = root.id;
        auto canonical = document(), legacy = document();
        for (auto *d : {&canonical, &legacy}) {
          auto &set = d->set(1);
          const float offset = d == &legacy ? -.5f : 0;
          set.bump_origin = d == &legacy ? 0 : .5f;
          set.base_values.bump = .53f + offset;
          set.layers[0].kind = LayerKind::Fill;
          set.layers[0].channels = {false, false, false, false, true};
          set.layers[0].ao_enabled = false;
          set.layers[0].values.bump = .61f + offset;
          set.layers[0].opacity[4] = .29f;
          set.layers[0].mask.emplace();
          set.layers[0].mask->background = .3f;
        }
        const auto original = prepared.material;
        const auto materializations = PaintPixels::recipe_materializations();
        const auto current = spsm_material_for_destination(prepared, canonical.set(1)),
                   old = spsm_material_for_destination(prepared, legacy.set(1));
        require(PaintPixels::recipe_materializations() == materializations,
                "destination adaptation materialized paint recipes");
        require(old.set.layers.front().values.bump == .3f &&
                    old.set.layers.front().blending[4] == LayerBlend::LinearDodge,
                "legacy first child did not rebase/translate Add");
        apply_material_preset(canonical.set(1), current);
        apply_material_preset(legacy.set(1), old);
        float low = 1, high = 0;
        for (int tile : {1001, 1017, 2000})
          for (auto xy : {std::pair(25, 25), std::pair(64, 64), std::pair(100, 83)}) {
            const auto a = TextureDocument::composite(canonical.set(1), xy.first, xy.second, tile);
            const auto b = TextureDocument::composite(legacy.set(1), xy.first, xy.second, tile);
            require(std::abs(a.material.bump - b.material.bump) < 1e-6f,
                    "canonical/legacy visible Bump diverged at partial coverage/nested folder");
            low = std::min(low, a.material.bump);
            high = std::max(high, a.material.bump);
          }
        require(high - low > 1e-5f, "legacy fixture did not exercise varying UV mask coverage");
        for (std::size_t i = 0; i < original.set.layers.size(); ++i)
          require(original.set.layers[i].values.bump ==
                          prepared.material.set.layers[i].values.bump &&
                      original.set.layers[i].blending[4] ==
                          prepared.material.set.layers[i].blending[4],
                  "destination adaptation mutated prepared constants/blends");
        auto invalid = legacy;
        invalid.set(1).bump_origin = .25f;
        const auto before = invalid.to_json();
        bool rejected = false;
        try {
          spsm_material_for_destination(prepared, invalid.set(1));
        }
        catch (const std::exception &) {
          rejected = true;
        }
        require(rejected && invalid.to_json() == before, "invalid destination partially mutated");
      }
  PreparedSpsm unsupported;
  unsupported.material.set.layers.front().kind = LayerKind::Fill;
  TextureSet legacy;
  legacy.bump_origin = 0;
  for (int kind = 0; kind < 3; ++kind) {
    auto bad = unsupported;
    if (kind == 0)
      fill_channel_texture(bad.material.set.layers.front().fill,
                           4) = std::make_shared<FillTexture>();
    if (kind == 1) {
      LevelsEffect levels;
      levels.channel = 4;
      bad.material.set.layers.front().effects.push_back(levels);
    }
    if (kind == 2) {
      PaintPixels::Stamp paint{{.2, .2}, {.1, 0}, {0, .1}, {}};
      paint.channels = {false, false, false, false, true};
      paint.values.bump = .7;
      bad.material.set.layers.front().pixels.stamp(128, paint, {1001});
    }
    const auto count = PaintPixels::recipe_materializations();
    bool rejected = false;
    try {
      spsm_material_for_destination(bad, legacy);
    }
    catch (const std::exception &) {
      rejected = true;
    }
    require(rejected && PaintPixels::recipe_materializations() == count,
            "unsupported Bump data accepted/materialized");
  }
  std::cout
      << "PASS native legacy Bump:18 Normal/Replace/Add nested/first-child scenarios, fractional opacity, varying mask coverage across3UVs/3tiles, unchanged source, atomic invalid destination and unsupported data guards\n";
}
void run(const std::filesystem::path &directory)
{
  legacy_uniform_contract();
  const char *names[] = {"dirty-plastic-robot.spsm",
                         "shd-hard-rust.spsm",
                         "sratched-metal-pealing-paint-dirt.spsm",
                         "used_varnish_wood.spsm",
                         "worn leather chest.spsm"};
  QTemporaryDir files;
  require(files.isValid(), "private fixture directory failed");
  for (const char *name : names) {
    SpsmImportOptions options;
    options.uniform_rgb_to_scene_linear = [](std::array<float, 3> rgb) {
      return rgb;
    };  // Explicit identity test space: no source appearance/OCIO claim.
    auto prepared = prepare_spsm_material(directory / name, std::move(options));
    auto target = document();
    auto before = target;
    std::set<QUuid> original_ids;
    for (const auto &l : prepared.material.set.layers)
      original_ids.insert(l.id);
    const auto root = apply_material_preset(
        target.set(1), spsm_material_for_destination(prepared, target.set(1)));
    require(target.set(1).layers.size() ==
                before.set(1).layers.size() + prepared.material.set.layers.size(),
            "production apply lost native layers");
    for (const auto &l : target.set(1).layers)
      require(!original_ids.contains(l.id), "production apply did not renew native IDs");
    require(TextureDocument::layer(target.set(1), root).kind == LayerKind::Folder,
            "applied native root missing");
    TextureDocument::validate_hierarchy(target.set(1));
    const auto result = TextureDocument::composite(target.set(1));
    for (float value : result.material.base_color)
      require(std::isfinite(value), "native compositor produced nonfinite color");
    // Undo and Redo use independent native document snapshots; preparation is
    // unaffected by authoring edits in the applied target.
    const auto edited = target;
    target = before;
    require(target.to_json() == before.to_json(), "native snapshot Undo mismatch");
    target = edited;
    same(result, TextureDocument::composite(target.set(1)));
    const auto encoded = write_material_preset(prepared.material);
    const QString path = files.filePath(QString::fromUtf8(name) + ".btsmaterial");
    {
      QFile output(path);
      require(output.open(QIODevice::WriteOnly) && output.write(encoded) == encoded.size(),
              "native preset Save failed");
    }
    {
      QFile input(path);
      require(input.open(QIODevice::ReadOnly), "native preset Open failed");
      const auto reopened = read_material_preset(input.readAll());
      auto fresh = document();
      apply_material_preset(fresh.set(1), reopened);
      same(result, TextureDocument::composite(fresh.set(1)));
      require(reopened.set.layers.size() == prepared.material.set.layers.size(),
              "native preset roundtrip lost disabled source layers");
    }
    const auto metadata = target.to_json();
    const TextureSetBinding binding{target.set(1).id, 1, "Destination"};
    auto reopened_document = TextureDocument::from_json(metadata, std::span(&binding, 1));
    same(result, TextureDocument::composite(reopened_document.set(1)));
    auto legacy = document();
    legacy.set(1).bump_origin = 0;
    legacy.set(1).base_values.bump = 0;
    legacy.set(1).layers.front().values.bump = 0;
    const auto retained = prepared.source;
    apply_material_preset(legacy.set(1), spsm_material_for_destination(prepared, legacy.set(1)));
    for (int tile : {1001, 1017, 2000})
      for (auto xy : {std::pair(25, 25), std::pair(64, 64), std::pair(100, 83)})
        same(TextureDocument::composite(target.set(1), xy.first, xy.second, tile),
             TextureDocument::composite(legacy.set(1), xy.first, xy.second, tile));
    require(prepared.source == retained && prepared.material.set.bump_origin == .5f,
            "legacy fixture adaptation changed retained source");
    auto invalid = prepared.material;
    invalid.set.layers.front().parent = QUuid::createUuid();
    const auto unchanged = target.to_json();
    bool rejected = false;
    try {
      apply_material_preset(target.set(1), invalid);
    }
    catch (const std::exception &) {
      rejected = true;
    }
    require(rejected && target.to_json() == unchanged,
            "production failed import partially changed destination");
    auto copied = prepared.material;
    copied.set.layers.front().values.roughness = .99f;
    require(prepared.material.set.layers.front().values.roughness !=
                copied.set.layers.front().values.roughness,
            "detached native authoring changed source preset");
    std::cout << "PASS native apply/codec " << name
              << " tiles=1001,1017,2000 layers=" << prepared.material.set.layers.size()
              << " encoded=" << encoded.size() << " bump=" << result.material.bump
              << " partial=true appearance_verified=false\n";
  }
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    require(argc == 2, "Expected fixture directory");
    run(argv[1]);
    return 0;
  }
  catch (const std::exception &error) {
    std::cerr << "SPSM native apply failed: " << error.what() << '\n';
    return 1;
  }
}
