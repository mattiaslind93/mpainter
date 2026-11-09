// SPDX-License-Identifier: GPL-3.0-or-later
// The existing five-source backend suite remains mandatory in this runner.
#pragma push_macro("main")
#undef main
#define main spsm_existing_source_test_main
#include "spsm_source_test.cc"
#pragma pop_macro("main")
namespace {
QStringList bitmap_tags() {
  return {"spsm:policy:bitmap-roughness=raw-r-ignore-a",
          "spsm:policy:bitmap-bytes=little-endian",
          "spsm:policy:bitmap-rows=first-row-v0",
          "spsm:policy:bitmap-alpha=data-components",
          "spsm:policy:bitmap-uv=projection0-centered-repeat-factors",
          "spsm:policy:bitmap-sampling=hq-repeat"};
}
bool texture_present(const PreparedSpsmSource &p) {
  for (const auto &l : p.prepared.material.set.layers)
    if (fill_channel_texture(l.fill, 2))
      return true;
  return false;
}
std::shared_ptr<const FillTexture> wood_texture(const PreparedSpsmSource &p) {
  for (const auto &l : p.prepared.material.set.layers)
    if (l.name == "Wood_Worn_base")
      return fill_channel_texture(l.fill, 2);
  throw std::runtime_error("Wood layer missing");
}
void policy_parse() {
  const auto empty = parse_spsm_source_policy({});
  require(!empty.roughness_raw_r_ignore_a &&
              empty.bitmaps.pixels.bytes == SpsmByteOrder::Unknown &&
              empty.bitmaps.uv == SpsmBitmapUV::Unknown,
          "Defaults enabled bitmaps");
  const auto tags = bitmap_tags();
  const auto p = parse_spsm_source_policy(tags);
  require(p.bitmaps.levels==SpsmLevelsPolicy::Unknown,"Missing Levels policy became enabled");
  auto native_levels=tags;native_levels.append("spsm:policy:levels=native-scalar");
  require(parse_spsm_source_policy(native_levels).bitmaps.levels==SpsmLevelsPolicy::NativeScalarLevels,"Native scalar Levels policy not parsed");
  auto unknown_levels=tags;unknown_levels.append("spsm:policy:levels=unknown");
  require(parse_spsm_source_policy(unknown_levels).bitmaps.levels==SpsmLevelsPolicy::Unknown,"Explicit Unknown Levels enabled");
  auto conflicting_levels=native_levels;conflicting_levels.append("spsm:policy:levels=unknown");
  reject([&]{parse_spsm_source_policy(conflicting_levels);},"Conflicting");
  auto invalid_levels=tags;invalid_levels.append("spsm:policy:levels=source-gamma-guessed");
  reject([&]{parse_spsm_source_policy(invalid_levels);},"Unsupported");
  require(p.roughness_raw_r_ignore_a && p.bitmaps.pixels.raw &&
              p.bitmaps.pixels.input_space.empty() &&
              p.bitmaps.pixels.bytes == SpsmByteOrder::LittleEndian &&
              p.bitmaps.pixels.rows == SpsmRowOrder::FirstRowIsV0 &&
              p.bitmaps.pixels.alpha == SpsmAlpha::DataComponents &&
              p.bitmaps.uv == SpsmBitmapUV::Projection0CenteredRepeatFactors &&
              p.bitmaps.sampling == SpsmBitmapSampling::HQRepeat,
          "Explicit policy parse changed selectors");
  for (qsizetype i = 0; i < tags.size(); ++i) {
    auto missing = tags;
    missing.removeAt(i);
    auto parsed = parse_spsm_source_policy(missing);
    require(parsed.bitmaps.pixels.bytes == SpsmByteOrder::Unknown ||
                parsed.bitmaps.pixels.rows == SpsmRowOrder::Unknown ||
                parsed.bitmaps.pixels.alpha == SpsmAlpha::Unknown ||
                parsed.bitmaps.uv == SpsmBitmapUV::Unknown ||
                parsed.bitmaps.sampling == SpsmBitmapSampling::Unknown,
            "Partial policy became complete");
    auto unknown = tags;
    unknown[i] = unknown[i].left(unknown[i].indexOf('=') + 1) + "unknown";
    parsed = parse_spsm_source_policy(unknown);
    require(parsed.bitmap_summary.contains("unknown"),
            "Explicit unknown not reported");
    auto conflict = tags;
    conflict.append(unknown[i]);
    reject([&] { parse_spsm_source_policy(conflict); }, "Conflicting");
    auto invalid = tags;
    invalid[i] = invalid[i].left(invalid[i].indexOf('=') + 1) + "guessed-value";
    reject([&] { parse_spsm_source_policy(invalid); }, "Unsupported");
  }
  auto same = tags;
  same.append(tags.front());
  require(parse_spsm_source_policy(same).roughness_raw_r_ignore_a,
          "Identical duplicate rejected");
  auto unknown = tags;
  unknown.append("spsm:policy:bitmap-kernel=guess");
  reject([&] { parse_spsm_source_policy(unknown); }, "Unsupported");
  auto nul = tags;
  nul[0] += QChar::Null;
  reject([&] { parse_spsm_source_policy(nul); }, "NUL");
  auto reordered = tags;
  std::reverse(reordered.begin(), reordered.end());
  require(parse_spsm_source_policy(reordered).bitmap_summary ==
              p.bitmap_summary,
          "Tag order changed semantics");
  std::cout << "PASS GUI-independent parser "
               "complete/missing/explicit-unknown/all-field-conflicts/invalid/"
               "NUL/duplicate/reordered boundaries\n";
}
void bitmaps(const QString &fixtures) {
  QTemporaryDir cache;
  require(cache.isValid(), "Private bitmap source cache failed");
  qputenv("BTS_RESOURCE_CACHE", cache.path().toUtf8());
  configure_spsm_source_hydration();
  for (const auto name :
       {"dirty-plastic-robot.spsm", "shd-hard-rust.spsm",
        "sratched-metal-pealing-paint-dirt.spsm", "used_varnish_wood.spsm",
        "worn leather chest.spsm"}) {
    const auto path = QDir(fixtures).filePath(QString::fromUtf8(name));
    auto original = asset(path);
    const auto bytes = original.bytes;
    auto baseline = std::async(std::launch::async, worker, original).get();
    const auto plain = prepared_spsm_source(baseline);
    require(plain && !texture_present(*plain) &&
                plain->report.contains("Bitmap policy:") &&
                plain->report.contains("unknown (missing)"),
            "Default bitmap policy hydrated/missing report");
    auto denied_policy = parse_spsm_source_policy(bitmap_tags()).bitmaps;

    bool resolved=false;
    enhance_prepared_spsm_bitmaps(plain->prepared,denied_policy,[&](const auto &,const auto &,auto)->std::shared_ptr<const SpsmBitmapTexture>{resolved=true;return {};});
    require(resolved,"Former memory policy skipped bitmap resolution");
    auto selected = original;
    selected.search_terms = bitmap_tags();
    require(!hydrate_spsm_asset(selected) && !selected.material_preset,
            "GUI cold bitmap policy did worker work");
    auto first = std::async(std::launch::async, worker, selected),
         second = std::async(std::launch::async, worker, selected);
    auto ready = first.get(), duplicate = second.get();
    auto prepared = prepared_spsm_source(ready);
    require(prepared && prepared == prepared_spsm_source(duplicate) &&
                ready.material_preset.get() == &prepared->prepared.material,
            "Duplicate worker/preset alias mismatch");
    require(prepared != plain &&
                prepared->original_bytes == plain->original_bytes &&
                prepared->cache_path == plain->cache_path &&
                ready.bytes.constData() == baseline.bytes.constData() &&
                *prepared->original_bytes == bytes &&
                file_hash(prepared->cache_path) == prepared->source_sha256,
            "Bitmap variant changed/copied original source/cache");
    require(prepared->report.contains("Roughness=raw-r-ignore-a") &&
                prepared->report.contains("rows=first-row-v0") &&
                prepared->report.contains("appearance unverified"),
            "Explicit bitmap policy/report missing");
    const bool wood = QString::fromUtf8(name) == "used_varnish_wood.spsm";
    require(texture_present(*prepared) == wood,
            "Unexpected bitmap file/unsupported v1 restored");
    {
      TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
      require(hydrate_spsm_asset(ready) &&
                  prepared_spsm_source(ready) == prepared &&
                  spsm_source_report(ready) == prepared->report,
              "GUI ready bitmap miss performed work");
    }
    auto invalid = ready;
    invalid.search_terms.append("spsm:policy:bitmap-rows=first-row-v1");
    reject([&] { std::async(std::launch::async, worker, invalid).get(); },
           "Conflicting");
    require(ready.bytes == bytes && prepared_spsm_source(ready) == prepared,
            "Conflicting parse corrupted valid ready state");
    if (wood) {
      auto texture = wood_texture(*prepared);
      require(texture && texture->raw && texture->pixels &&
                  texture->width == 4096 && texture->height == 4096,
              "Hydrated4096 native Roughness missing");
      auto with_levels=original;with_levels.search_terms=bitmap_tags();with_levels.search_terms.append("spsm:policy:levels=native-scalar");
      require(!hydrate_spsm_asset(with_levels)&&!with_levels.material_preset,"GUI cold Levels policy performed preparation");
      auto levels_ready=std::async(std::launch::async,worker,with_levels).get();
      const auto levels_source=prepared_spsm_source(levels_ready);
      require(levels_source&&levels_source!=prepared&&wood_texture(*levels_source)==texture&&levels_source->original_bytes==prepared->original_bytes,"Levels policy copied Worn pixels/original source or reused wrong preparation");
      bool darken=false;
      for(const auto &l:levels_source->prepared.material.set.layers)if(l.name=="Wood_Darken_base"){
        const auto image=fill_channel_texture(l.fill,2);
        require(l.visible&&l.channels[2]&&!l.channels[0]&&!l.channels[3]&&l.effects.size()==1&&image&&image->asset!=texture->asset&&image->pixels!=texture->pixels,"Joint worker did not preserve independent Darken bitmap identity/Levels");
        darken=true;
      }
      require(darken&&levels_source->report.contains("bitmap-native-scalar-levels-policy")&&levels_source->report.contains("bitmap-zero-ancestor-mask")&&levels_source->report.contains("Levels=native-scalar"),"Joint policy/masked ancestor not reported");
      {TextureRamQueryScope no_ram(+[]()->std::uint64_t{return 0;});require(hydrate_spsm_asset(levels_ready)&&prepared_spsm_source(levels_ready)==levels_source,"GUI ready Levels variant performed worker work");}
      for (const auto &l : prepared->prepared.material.set.layers)
        if (l.name == "Wood_Worn_base")
          require(l.channels[2] && !l.channels[0] && !l.channels[3] &&
                      !l.fill.alpha_mask[2],
                  "Restoration enabled Color/Normal/alpha");
      auto missing = original;
      missing.search_terms = bitmap_tags();
      missing.search_terms.removeFirst();
      auto skipped = std::async(std::launch::async, worker, missing).get();
      require(!texture_present(*prepared_spsm_source(skipped)) &&
                  prepared_spsm_source(skipped)->report.contains(
                      "bitmap-policy-required"),
              "Missing Roughness opt-in hydrated");
      auto unsupported = original;
      unsupported.search_terms = bitmap_tags();
      unsupported.search_terms.replaceInStrings("hq-repeat", "nearest-repeat");
      auto ignored = std::async(std::launch::async, worker, unsupported).get();
      require(
          !texture_present(*prepared_spsm_source(ignored)) &&
              prepared_spsm_source(ignored)->report.contains("Sharp/Nearest"),
          "Unproven sampling hydrated");
      auto same_pixels = original;
      same_pixels.search_terms = bitmap_tags();
      same_pixels.search_terms.append("spsm:policy:uniform-input=ACEScg");
      auto color_variant =
          std::async(std::launch::async, worker, same_pixels).get();
      const auto color_source = prepared_spsm_source(color_variant);
      require(color_source != prepared &&
                  wood_texture(*color_source) == texture &&
                  color_source->original_bytes == prepared->original_bytes,
              "Uniform-only policy copied bitmap allocation/original");
      auto flipped = original;
      flipped.search_terms = bitmap_tags();
      flipped.search_terms.replaceInStrings("first-row-v0", "first-row-v1");
      auto row_variant = std::async(std::launch::async, worker, flipped).get();
      const auto row_source = prepared_spsm_source(row_variant);
      auto reverse = wood_texture(*row_source);
      require(row_source != prepared &&
                  row_source->original_bytes == prepared->original_bytes &&
                  row_source->cache_path == prepared->cache_path && reverse &&
                  reverse->pixels != texture->pixels &&
                  reverse->hash != texture->hash &&
                  reverse->asset == texture->asset,
              "Row policy conflated pixel/native/source identities");
      for (int y : {0, 1, 100, 4095})
        for (int x : {0, 51, 4095})
          require((*reverse->pixels)[std::size_t(y) * 4096 + x] ==
                      (*texture->pixels)[std::size_t(4095 - y) * 4096 + x],
                  "Row policy failed exact native pixel reversal");
      auto moved = ready;
      auto moved_source = prepared_spsm_source(moved);
      const auto oldhash = moved.hash();
      moved.bytes[0] ^= 1;
      require(moved.bytes.constData() != ready.bytes.constData() &&
                  !prepared_spsm_source(moved) && moved.hash() != oldhash &&
                  ready.bytes == bytes &&
                  *moved_source->original_bytes == bytes,
              "Original byte COW mutation reused prepared identity");
      reject([&] { std::async(std::launch::async, worker, moved).get(); },
             "HDF5");
      require(prepared_spsm_source(ready) == prepared &&
                  wood_texture(*prepared) == texture,
              "Corrupt worker poisoned existing preparation");
      std::weak_ptr<const std::vector<FillTexture::Pixel>> weak_pixels =
          reverse->pixels;
      row_variant = StudioAsset{};
      reverse.reset();
      require(!weak_pixels.expired(), "Live prepared source lost pixels");
      std::cout << "PASS Wood source policy variants: native shared "
                   "bitmap/rawR/ignoredA, GUI defer/ready, uniform-policy "
                   "shared pixels, row-policy exact reversal and separate "
                   "hash, original COW/cache bytes immutable\n";
    }
    std::cout << "BITMAP_SOURCE " << name
              << " original_sha=" << prepared->source_sha256.toStdString()
              << " bitmap=" << wood
              << " report_paths=" << prepared->prepared.diagnostics.size()
              << " appearance_verified=0\n";
  }
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  std::cout << std::unitbuf;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--runtime-baseline")
      return 0;
    require(argc == 2, "Expected fixture directory");
    CLG_init();
    blender::IMB_init();
    policy_parse();
    run(QString::fromUtf8(argv[1]));
    bitmaps(QString::fromUtf8(argv[1]));
    blender::IMB_exit();
    CLG_exit();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Source bitmap policy failed: " << e.what() << '\n';
    return 1;
  }
}
