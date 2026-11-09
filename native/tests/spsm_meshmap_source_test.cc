// SPDX-License-Identifier: GPL-3.0-or-later
// Keep existing worker ownership/cache/original-source regressions mandatory.
#define main spsm_existing_source_test_main
#include "spsm_source_test.cc"
#undef main
namespace {
QString tag() {
  return "spsm:policy:meshmap-ao-color=native-ao-generator-uv-identity-nearest";
}
std::size_t restored(const PreparedSpsmSource &p) {
  return std::count_if(
      p.prepared.diagnostics.begin(), p.prepared.diagnostics.end(),
      [](const auto &d) { return d.code == "meshmap-ao-color-restored"; });
}
void mesh_maps(const QString &fixtures) {
  require(parse_spsm_source_policy({}).mesh_maps.policy ==
              SpsmMeshMapPolicy::Unknown,
          "Default mesh-map policy enabled");
  require(parse_spsm_source_policy({"spsm:policy:meshmap-ao-color=unknown"})
                  .mesh_maps.policy == SpsmMeshMapPolicy::Unknown,
          "Explicit Unknown enabled");
  require(parse_spsm_source_policy({tag(), tag()}).mesh_maps.policy ==
              SpsmMeshMapPolicy::NativeAOGeneratorUVIdentityNearest,
          "Explicit/repeated policy ignored");
  reject(
      [&] {
        parse_spsm_source_policy(
            {tag(), "spsm:policy:meshmap-ao-color=unknown"});
      },
      "Conflicting");
  reject(
      [&] {
        parse_spsm_source_policy(
            {"spsm:policy:meshmap-ao-color=raw-r-ignore-a"});
      },
      "Unsupported");
  reject([&] { parse_spsm_source_policy({tag() + QChar::Null}); }, "NUL");
  QTemporaryDir cache;
  require(cache.isValid(), "Private worker cache failed");
  qputenv("BTS_RESOURCE_CACHE", cache.path().toUtf8());
  configure_spsm_source_hydration();
  for (auto name : {"dirty-plastic-robot.spsm", "shd-hard-rust.spsm",
                    "sratched-metal-pealing-paint-dirt.spsm",
                    "used_varnish_wood.spsm", "worn leather chest.spsm"}) {
    const auto path = QDir(fixtures).filePath(QString::fromUtf8(name));
    const auto before = file_hash(path);
    auto original = asset(path);
    const auto bytes = original.bytes;
    auto plain_asset = std::async(std::launch::async, worker, original).get();
    const auto plain = prepared_spsm_source(plain_asset);
    auto selected = original;
    selected.search_terms = {tag()};
    require(!hydrate_spsm_asset(selected) && !selected.material_preset,
            "Cold GUI performed mesh-map worker work");
    auto first = std::async(std::launch::async, worker, selected),
         second = std::async(std::launch::async, worker, selected);
    auto ready = first.get(), duplicate = second.get();
    const auto selected_source = prepared_spsm_source(ready);
    require(selected_source &&
                selected_source == prepared_spsm_source(duplicate) &&
                selected_source != plain,
            "Worker duplicate/policy identity broken");
    require(plain->original_bytes == selected_source->original_bytes &&
                plain->cache_path == selected_source->cache_path &&
                ready.bytes.constData() == plain_asset.bytes.constData() &&
                *selected_source->original_bytes == bytes &&
                file_hash(path) == before,
            "Source COW/cache/SHA changed");
    require(
        restored(*plain) == 0 &&
            restored(*selected_source) ==
                (std::string_view(name) == "worn leather chest.spsm" ? 1u : 0u),
        "Wrong worker restoration subset");
    require(selected_source->report.contains("Mesh-map policy:") &&
                selected_source->report.contains(
                    "native-ao-generator-uv-identity-nearest") &&
                selected_source->report.contains("unverified"),
            "Explicit worker policy report missing");
    {
      TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
      require(hydrate_spsm_asset(ready) &&
                  prepared_spsm_source(ready) == selected_source,
              "Ready GUI mesh-map lookup did worker work");
    }
    auto conflict = ready;
    conflict.search_terms.append("spsm:policy:meshmap-ao-color=unknown");
    reject([&] { std::async(std::launch::async, worker, conflict).get(); },
           "Conflicting");
    require(prepared_spsm_source(ready) == selected_source &&
                ready.bytes == bytes,
            "Bad policy poisoned ready state");
    std::cout << "PASS worker mesh-map " << name
              << " restored=" << restored(*selected_source)
              << " COW/shared-cache/original-SHA exact GUI-deferred=true\n";
  }
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  std::cout << std::unitbuf;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--runtime-baseline")
      return 0;
    require(argc == 2, "Expected five-fixture directory");
    CLG_init();
    blender::IMB_init();
    run(QString::fromUtf8(argv[1]));
    mesh_maps(QString::fromUtf8(argv[1]));
    blender::IMB_exit();
    CLG_exit();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SPSM mesh-map source failed: " << e.what() << '\n';
    return 1;
  }
}
