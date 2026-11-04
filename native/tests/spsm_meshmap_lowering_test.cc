#include "SpsmMeshMapFixture.hh"
#include <QCoreApplication>
using namespace bts;
using namespace bts::spsm_meshmap_test;
namespace {
template <class F> void reject(F f) {
  bool yes = false;
  try {
    f();
  } catch (const std::exception &) {
    yes = true;
  }
  require(yes, "Unsafe input accepted");
}
void verify(const TextureSet &set, int revision = 0) {
  for (int tile : set.tiles)
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) {
        const auto actual = TextureDocument::evaluate(set, x, y, tile);
        const auto want = expected(tile, x, y, revision);
        for (int c = 0; c < 3; ++c)
          near(actual.material.base_color[c], want[c],
               "Native CPU tile/oracle");
      }
}
void tests() {
  Builder builder;
  auto input = builder.prepare();
  require(!leaf(input).visible && leaf(input).effects.empty(),
          "Base preparer restored nonuniform source");
  auto unknown = enhance_prepared_spsm_mesh_maps(input);
  require(!leaf(unknown).visible &&
              has(unknown, "meshmap-ao-color-policy-required"),
          "Unknown policy enabled alias");
  auto prepared = enhance_prepared_spsm_mesh_maps(input, policy());
  require(leaf(prepared).visible && leaf(prepared).channels[0] &&
              leaf(prepared).effects.size() == 1,
          "AO Color not restored");
  require(prepared.source == input.source && !leaf(input).visible &&
              input.source->source().presets[0].encoded ==
                  s::ImageBytes({1, 2, 3, 4}),
          "Source/preparation changed");
  require(prepared.partial && !prepared.appearance_verified &&
              has(prepared, "meshmap-ao-color-restored"),
          "Missing explicit partial policy diagnostics");
  const auto &e = leaf(prepared).effects[0];
  require(e.kind == ContentEffectKind::Generator && e.generator == 3 &&
              e.channel == 0 && e.map_filter == 0 && e.contrast == 1 &&
              e.bias == 0 && !e.invert && e.curve == 0 && e.opacity == 1 &&
              e.blending == LayerBlend::Normal && !generator_has_anchors(e),
          "Native live AO policy fields differ");
  require(leaf(prepared).blending[0] == LayerBlend::Multiply &&
              !leaf(prepared).ao_enabled,
          "AO routed to wrong channel");
  for (int c : {1, 2, 3, 4})
    require(!leaf(prepared).channels[c], "Unsupported sibling enabled");
  auto set = destination();
  apply_material_preset(set, prepared.material);
  verify(set);
  auto first = set.mesh_maps[3]->tiles.at(1001).hash;
  maps(set, 1);
  require(first != set.mesh_maps[3]->tiles.at(1001).hash,
          "Rebake hash unchanged");
  verify(set, 1);
  auto written = write_material_preset(prepared.material);
  auto round = read_material_preset(written);
  require(!round.set.mesh_maps[3] &&
              effects_json(leaf(PreparedSpsm{round}).effects) ==
                  effects_json(leaf(prepared).effects),
          "Preset retains maps or changes generator binding roundtrip");
  auto opened = destination();
  apply_material_preset(opened, round);
  verify(opened);
  for (bool partial : {false, true}) {
    auto missing = destination();
    if (partial)
      missing.mesh_maps[3]->tiles.erase(1002);
    else
      missing.mesh_maps[3].reset();
    auto before = missing.layers;
    const auto selected = missing.selected_layer;
    bool rejected = false;
    try {
      apply_material_preset(missing, prepared.material);
    } catch (const std::invalid_argument &error) {
      rejected = std::string_view(error.what()).find("Ambient occlusion") !=
                 std::string_view::npos;
    }
    require(rejected && missing.layers.size() == before.size() &&
                missing.selected_layer == selected,
            "Missing AO silently applied/mutated");
  }
  reject([&] { enhance_prepared_spsm_mesh_maps(prepared, policy()); });
  reject([&] { enhance_prepared_spsm_mesh_maps(prepared); });
  auto legacy = destination();
  legacy.bump_origin = 0;
  auto adapted = spsm_material_for_destination(prepared, legacy);
  apply_material_preset(legacy, adapted);
  verify(legacy);
  auto low = policy();
  low.memory_budget_bytes = 1;
  reject([&] { enhance_prepared_spsm_mesh_maps(input, low); });
  auto invalid = input;
  invalid.source.reset();
  reject([&] { enhance_prepared_spsm_mesh_maps(invalid, policy()); });
  const std::vector<std::function<void(Builder &)>> unsafe{
      [](auto &b) { b.set(b.leaf_id, "futureDependency", b.scalar(1)); },
      [](auto &b) { b.set(b.bitmap, "futureDependency", b.scalar(1)); },
      [](auto &b) { b.set(b.src, "futureDependency", b.scalar(1)); },
      [](auto &b) { b.set(b.fill, "futureDependency", b.scalar(1)); },
      [](auto &b) { b.set(b.root, "futureDependency", b.scalar(1)); },
      [](auto &b) { b.set(b.uv0, "rotation", b.scalar(1)); },
      [](auto &b) { b.set(b.uv1, "scale", b.pair(2, 1)); },
      [](auto &b) { b.set(b.fill, "projection", b.integer(1)); },
      [](auto &b) { b.set(b.fill, "backfaceCulling", b.flag(true)); },
      [](auto &b) { b.set(b.fill, "depthCulling", b.flag(true)); },
      [](auto &b) { b.set(b.fill, "tags", b.integer(1)); },
      [](auto &b) {
        s::Blob v;
        v.size = 1;
        b.set(b.fill, "selection", b.add(v));
      },
      [](auto &b) {
        b.set(b.leaf_id, "enabledUVTileList", b.array({b.integer(1001)}));
      },
      [](auto &b) { b.set(b.root, "enabledMeshDefault", b.flag(false)); },
      [](auto &b) { b.set(b.root, "gammaCompensation", b.flag(true)); },
      [](auto &b) { b.set(b.layerblend, "blendingMode", b.integer(77)); },
      [](auto &b) { b.set(b.actionblend, "blendingMode", b.integer(4)); },
      [](auto &b) { b.set(b.src, "channelTypes", b.integer(129)); },
      [](auto &b) { b.set(b.src, "opacity", b.scalar(2)); },
      [](auto &b) { b.set(b.fill, "sources", b.array({b.src, b.src})); },
      [](auto &b) {
        b.set(b.leaf_id, "actions",
              b.stack("DataStackActions", {b.fill, b.fill}));
      },
      [](auto &b) {
        auto a = b.object("DataActionGroup", {{"enabled", b.flag(true)}});
        b.set(b.leaf_id, "maskActions", b.stack("DataStackActions", {a}));
      },
      [](auto &b) {
        auto a = b.object("DataActionPaint", {{"enabled", b.flag(true)}});
        b.set(b.root, "maskActions", b.stack("DataStackActions", {a}));
      },
  };
  for (const auto &change : unsafe) {
    Builder b;
    change(b);
    auto in = b.prepare();
    auto out = enhance_prepared_spsm_mesh_maps(in, policy());
    require(!leaf(out).visible && leaf(out).effects.empty() &&
                !has(out, "meshmap-ao-color-restored"),
            "Unsupported dependency partially restored");
  }
  for (bool parent : {false, true}) {
    Builder b;
    b.set(parent ? b.root : b.leaf_id, "enabled", b.flag(false));
    auto in = b.prepare();
    auto out = enhance_prepared_spsm_mesh_maps(in, policy());
    require(has(out, "meshmap-ao-color-restored"),
            "Disabled state prevented otherwise valid restoration");
    const auto &l = parent ? out.material.set.layers.back() : leaf(out);
    require(!l.visible, "Disabled source made visible");
  }
  Builder inactive;
  inactive.set(inactive.fill, "enabled", inactive.flag(false));
  auto inactive_input = inactive.prepare();
  auto inactive_output =
      enhance_prepared_spsm_mesh_maps(inactive_input, policy());
  require(!leaf(inactive_output).visible &&
              leaf(inactive_output).effects.empty() &&
              !has(inactive_output, "meshmap-ao-color-restored"),
          "Disabled Fill action activated");
  // Source-initial mask and per-channel action opacity must remain operative.
  Builder zero;
  zero.set(zero.root, "maskInitial", zero.scalar(0));
  auto in = zero.prepare();
  auto out = enhance_prepared_spsm_mesh_maps(in, policy());
  require(out.material.set.layers.back().mask->background == 0,
          "Source ancestor mask reset");
  Builder op;
  op.set(op.actionblend, "opacity", op.scalar(.25));
  auto op_in = op.prepare();
  auto op_out = enhance_prepared_spsm_mesh_maps(op_in, policy());
  near(leaf(op_out).opacity[0], .6f * .25f * .7f, "Action opacity ignored");
  for (const auto &d : prepared.diagnostics)
    if (d.code == "meshmap-ao-color-restored")
      require(!prepared.diagnostic_path(d).empty(),
              "No source path diagnostic");
  std::cout
      << "PASS two-tile native CPU oracle/live rebake, genuine premultiplied "
         "map/zero coverage, missing-map atomicity, preset roundtrip, 23 "
         "dependency gates, source states and dynamic RAM\n";
}
void fixtures(const QString &directory) {
  for (auto name : {"dirty-plastic-robot.spsm", "shd-hard-rust.spsm",
                    "sratched-metal-pealing-paint-dirt.spsm",
                    "used_varnish_wood.spsm", "worn leather chest.spsm"}) {
    const auto path = directory + "/" + name;
    auto sha = file_hash(path);
    auto in = prepare_spsm_material(std::filesystem::path(path.toStdString()),
                                    color_policy());
    auto out = enhance_prepared_spsm_mesh_maps(in, policy());
    std::size_t count = 0;
    for (const auto &d : out.diagnostics)
      if (d.code == "meshmap-ao-color-restored")
        ++count;
    require(count ==
                (std::string_view(name) == "worn leather chest.spsm" ? 1u : 0u),
            "Unexpected real alias restoration count");
    require(out.source == in.source && file_hash(path) == sha,
            "Fixture/source bytes changed");
    std::uint64_t metadata = 0;
    for (const auto &stream : out.source->source().presets)
      metadata = resource_sum(metadata, stream.encoded.size());
    require(out.source->source().payload_bytes_read == metadata,
            "Resource payload read during alias preparation");
    if (count) {
      const auto &l = leaf(out);
      require(l.blending[0] == LayerBlend::Multiply && l.opacity[0] == 1 &&
                  l.mask->background == 1,
              "Actual Leather blend/mask/opacity not retained");
      auto set = destination();
      apply_material_preset(set, out.material);
      require(set.mesh_maps[3]->tiles.size() == 2,
              "Actual preset copied/replaced destination maps");
    }
    std::cout << "PASS fixture " << name
              << " direct AO-Color restored=" << count
              << " source SHA=" << sha.constData()
              << " appearance_verified=false payloads_read=false\n";
  }
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    if (argc > 1 && std::string_view(argv[1]) == "--runtime-baseline")
      return 0;
    tests();
    if (argc > 1)
      fixtures(QString::fromLocal8Bit(argv[1]));
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
