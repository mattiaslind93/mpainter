// SPDX-License-Identifier: GPL-3.0-or-later
#define main spsm_previous_source_bitmap_main
#include "spsm_source_bitmap_test.cc"
#undef main
namespace {
const TextureLayer &mask_named(const SmartMaterialPreset &p, const char *name)
{
  for (const auto &l : p.set.layers)
    if (l.name == name)
      return l;
  throw std::runtime_error("Missing mask source layer");
}
void mask_source(const QString &fixtures)
{
  const QString tag = "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a";
  require(parse_spsm_source_policy({}).bitmaps.mask == SpsmMaskBitmapPolicy::Unknown,
          "Default mask enabled");
  require(parse_spsm_source_policy({tag}).bitmaps.mask ==
              SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA,
          "Mask tag not parsed");
  reject([&] { parse_spsm_source_policy({tag, "spsm:policy:mask-bitmap=unknown"}); },
         "Conflicting");
  reject([&] { parse_spsm_source_policy({"spsm:policy:mask-bitmap=guessed-vendor"}); },
         "Unsupported");
  QTemporaryDir cache;
  require(cache.isValid(), "Mask cache directory failed");
  qputenv("BTS_RESOURCE_CACHE", cache.path().toUtf8());
  configure_spsm_source_hydration();
  auto original = asset(fixtures + "/used_varnish_wood.spsm");
  const auto sha = original.hash();
  auto unselected = original;
  unselected.search_terms = bitmap_tags();
  auto old = std::async(std::launch::async, worker, unselected).get();
  require(!mask_named(*old.material_preset, "Wood_base_XtraRoughness").visible,
          "Existing bitmap policy enabled mask");
  auto selected = original;
  selected.search_terms = bitmap_tags();
  selected.search_terms.append(tag);
  require(!hydrate_spsm_asset(selected) && !selected.material_preset,
          "GUI cold mask miss did work");
  auto output = std::async(std::launch::async, worker, selected).get();
  const auto prepared = prepared_spsm_source(output);
  require(prepared && prepared->policy.bitmaps.mask ==
                          SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA,
          "Worker lost mask policy");
  const auto &l = mask_named(*output.material_preset, "Wood_base_XtraRoughness"),
             &w = mask_named(*output.material_preset, "Wood_Worn_base");
  auto binding = fill_channel_texture(w.fill, 2);
  require(l.visible && l.mask && l.mask->effects.size() == 2 &&
              fill_channel_texture(l.fill, 2) == binding &&
              l.mask->effects[0].mapping.projection.texture == binding,
          "Source mask/content/Worn owners not shared");
  require(prepared->original_bytes == prepared_spsm_source(old)->original_bytes &&
              output.bytes.constData() == old.bytes.constData() && output.hash() == sha,
          "Policy variants copied/changed original bytes");
  require(prepared->report.contains("bitmap-mask-restored") &&
              prepared->report.contains("bitmap-mask-empty-group-dominated") &&
              prepared->report.contains("/preset.bin") && !prepared->prepared.appearance_verified,
          "Mask provenance/assumption report missing");
  auto warm = selected;
  {
    TextureRamQueryScope no_ram(+[]() -> std::uint64_t { return 0; });
    require(hydrate_spsm_asset(warm) && prepared_spsm_source(warm) == prepared &&
                warm.hash() == sha,
            "Warm GUI mask ready lookup did IO/memory admission/hash");
  }
  auto incomplete = original;
  incomplete.search_terms = {tag};
  auto skipped = std::async(std::launch::async, worker, incomplete).get();
  require(!mask_named(*skipped.material_preset, "Wood_base_XtraRoughness").visible &&
              !texture_present(*prepared_spsm_source(skipped)),
          "Mask tag alone authorized bitmap decode");
  auto denied = original;
  denied.search_terms = bitmap_tags();
  denied.search_terms.append(tag);
  denied.search_terms.append("spsm:policy:uniform-input=ACEScg");
  auto accepted = std::async(std::launch::async, [denied]() mutable {
                    TextureRamQueryScope low(+[]() -> std::uint64_t { return 32; });
                    return worker(denied);
                  }).get();
  require(hydrate_spsm_asset(accepted), "RAM telemetry blocked bitmap source");
  require(file_hash(fixtures + "/used_varnish_wood.spsm") == sha, "Original fixture changed");
  std::cout
      << "PASS Source mask: explicit policy/defaultUnknown, partial/path report, 3bindings/1owner, shared original across policies, GUIdeferred/warm noIO, incomplete optins skip, lowRAM; sourceSHA="
      << sha.toStdString() << " appearance_verified=false\n";
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  std::cout << std::unitbuf;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--runtime-baseline")
      return 0;
    require(argc == 2, "Expected five fixture directory");
    CLG_init();
    blender::IMB_init();
    policy_parse();
    run(argv[1]);
    mask_source(argv[1]);
    blender::IMB_exit();
    CLG_exit();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SPSM mask Source failed: " << e.what() << '\n';
    return 1;
  }
}
