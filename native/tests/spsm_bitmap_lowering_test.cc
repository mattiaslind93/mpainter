// SPDX-License-Identifier: GPL-3.0-or-later
#include "BLI_math_half.hh"
#include "FillTextureJSON.hh"
#include "SpsmBitmapLowering.hh"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <bit>
#include <hdf5.h>
#include <iostream>
#include <lz4.h>
using namespace bts;
namespace s = bts::spsm;
namespace sem = s::semantic;
namespace bm = s::bitmap;
namespace {
void require(bool yes, const char *why)
{
  if (!yes)
    throw std::runtime_error(why);
}
template<class F> void reject(F f)
{
  bool yes = false;
  try {
    f();
  }
  catch (const std::exception &) {
    yes = true;
  }
  require(yes, "Unsafe input accepted");
}
bool has(const PreparedSpsm &p, std::string_view code)
{
  return std::any_of(
      p.diagnostics.begin(), p.diagnostics.end(), [&](auto &d) { return d.code == code; });
}
const TextureLayer &named(const SmartMaterialPreset &p, std::string_view name)
{
  for (auto &l : p.set.layers)
    if (l.name == name)
      return l;
  throw std::runtime_error("Missing native layer");
}
SpsmBitmapLoweringPolicy policy()
{
  return {{SpsmByteOrder::LittleEndian,
           SpsmRowOrder::FirstRowIsV0,
           SpsmAlpha::DataComponents,
           true,
           {}},
          SpsmBitmapUV::Projection0CenteredRepeatFactors,
          SpsmBitmapSampling::HQRepeat};
}
struct Writer {
  s::ImageBytes data;
  template<class T> void n(T v)
  {
    for (std::size_t i = sizeof(T); i-- > 0;)
      data.push_back(std::uint8_t(std::uint64_t(v) >> (i * 8)));
  }
  void text(std::string_view v)
  {
    n(std::uint32_t(v.size() * 2));
    for (char c : v) {
      n(std::uint8_t(0));
      n(std::uint8_t(c));
    }
  }
  void f(std::string_view k, unsigned type)
  {
    text(k);
    n(std::uint32_t(type));
    n(std::uint8_t(0));
  }
  void integer(std::string_view k, std::uint64_t v)
  {
    f(k, 5);
    n(v);
  }
  void string(std::string_view k, std::string_view v)
  {
    f(k, 10);
    text(v);
  }
};
s::Archive image_file(const std::filesystem::path &path, bool v1 = false, bool signed_hdr = false)
{
  // Asymmetric R values 0,.25,.5,1; A deliberately 2,0,4,0.5 data.
  std::uint16_t values[] = {
      0, 0x3c00, 0x3c00, 0x4000, 0x3400, 0, 0, 0, 0x3800, 0, 0, 0x4400, 0x3c00, 0, 0, 0x3800};
  if (signed_hdr) {
    values[0] = 0xbc00;
    values[12] = 0x4000;
  }  // R=-1,+2; A stays independent.
  s::ImageBytes raw;
  for (auto v : values) {
    raw.push_back(v);
    raw.push_back(v >> 8);
  }
  s::ImageBytes compressed(LZ4_compressBound(raw.size()));
  const int count = LZ4_compress_default(
      (const char *)raw.data(), (char *)compressed.data(), raw.size(), compressed.size());
  require(count > 0, "LZ4 fixture failed");
  compressed.resize(count);
  Writer w;
  w.n(std::uint32_t(11));
  w.integer("width", 2);
  w.integer("height", 2);
  w.integer("pitch", 16);
  w.string("version", v1 ? "v1" : "v2");
  if (v1)
    w.integer("format", 3);
  else
    w.string("format", "RGBA16F");
  w.integer("flags", 0);
  w.integer("dataSizeRaw", raw.size());
  w.integer("dataSizeLZ4", compressed.size());
  w.string("compression", "lz4");
  w.integer("colorSpace", 999);
  w.integer("alphaFormat", 999);
  w.data.insert(w.data.end(), compressed.begin(), compressed.end());
  const auto file = H5Fcreate(path.string().c_str(), H5F_ACC_EXCL, H5P_DEFAULT, H5P_DEFAULT);
  require(file >= 0, "HDF fixture create failed");
  const auto group = H5Gcreate2(file, "/resources", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  const hsize_t size = w.data.size();
  const auto space = H5Screate_simple(1, &size, nullptr);
  const auto ds = H5Dcreate2(
      file, "/resources/test.image", H5T_STD_U8LE, space, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  require(ds >= 0 &&
              H5Dwrite(ds, H5T_NATIVE_UCHAR, H5S_ALL, H5S_ALL, H5P_DEFAULT, w.data.data()) >= 0,
          "HDF fixture write failed");
  H5Dclose(ds);
  H5Sclose(space);
  H5Gclose(group);
  H5Fclose(file);
  s::Archive a;
  a.source = path;
  a.source_stamp = s::ImageSourceStamp{std::filesystem::file_size(path),
                                       std::filesystem::last_write_time(path)};
  a.datasets.push_back({"/resources/test.image", size});
  return a;
}
struct Builder {
  s::Preset p;
  s::NodeId url = 0, src = 0, fill = 0, leaf = 0, root = 0;
  bool enabled = true, parent_enabled = true, fill_enabled = true;
  unsigned levels = 1, fills = 1;
  bool group_mask = false;
  float layer_op = .6f, source_op = .7f;
  template<class T> s::NodeId add(T v)
  {
    auto id = p.nodes.size();
    p.nodes.push_back({18, 0, 1, std::move(v)});
    return id;
  }
  s::NodeId integer(std::uint64_t v)
  {
    return add(v);
  }
  s::NodeId scalar(float v)
  {
    s::FloatValues n;
    n.count = 1;
    n.values[0] = v;
    return add(n);
  }
  s::NodeId pair(float x, float y)
  {
    s::FloatValues n;
    n.count = 2;
    n.values[0] = x;
    n.values[1] = y;
    return add(n);
  }
  s::NodeId flag(bool b)
  {
    return add(b);
  }
  s::NodeId text(std::string v)
  {
    return add(std::move(v));
  }
  s::NodeId object(std::string name, std::initializer_list<s::Field> f)
  {
    return add(s::Object{std::move(name), f});
  }
  s::NodeId array(std::vector<s::NodeId> v)
  {
    return add(s::Array{std::move(v)});
  }
  s::NodeId stack(std::string name, std::vector<s::NodeId> v)
  {
    return object(std::move(name), {{"items", array(std::move(v))}});
  }
  s::NodeId blend(unsigned mode, float op)
  {
    return object("DataBlending",
                  {{"channelTypes", integer(128)},
                   {"blendingMode", integer(mode)},
                   {"opacity", scalar(op)}});
  }
  s::Archive build(s::Archive a)
  {
    url = text("resource://fixture?version=test.image");
    auto bitmap = object("DataBitmap", {{"urlToBitmapRes", url}});
    src = object(
        "DataSourceBitmap",
        {{"channelTypes", integer(128)}, {"bitmap", bitmap}, {"opacity", scalar(source_op)}});
    auto uv = object("Data2DTransformation",
                     {{"GUIPivot", pair(.5, .5)},
                      {"scaleFactors", pair(2.5, 1.5)},
                      {"translation", pair(0, 0)},
                      {"rotation", scalar(0)},
                      {"scaleMode", integer(0)},
                      {"physicalSizeBoundedByScaleFactors", flag(false)}});
    fill = object("DataActionFill",
                  {{"enabled", flag(fill_enabled)},
                   {"channelTypes", integer(UINT64_MAX)},
                   {"sources", array({src})},
                   {"perChannelBlending", array({blend(2, .8f)})},
                   {"projection", integer(0)},
                   {"uvTransformation", uv},
                   {"depthCulling", flag(true)}});
    std::vector<s::NodeId> actions{fill};
    if (fills > 1)
      actions.push_back(object(
          "DataActionFill",
          {{"enabled", flag(true)}, {"channelTypes", integer(128)}, {"sources", array({})}}));
    if (levels)
      actions.push_back(object("DataActionFilterLevels",
                               {{"enabled", flag(true)},
                                {"channelTypes", integer(levels)},
                                {"levels",
                                 object("DataLevels",
                                        {{"inputMinimum", add(s::FloatValues{{0, 0, 0}, 3})},
                                         {"inputMaximum", add(s::FloatValues{{1, 1, 1}, 3})},
                                         {"gamma", add(s::FloatValues{{1, 1, 1}, 3})},
                                         {"outputMinimum", add(s::FloatValues{{0, 0, 0}, 3})},
                                         {"outputMaximum", add(s::FloatValues{{1, 1, 1}, 3})},
                                         {"clamp", integer(0)}})}}));
    std::vector<s::NodeId> masks;
    if (group_mask)
      masks.push_back(object("DataActionGroup", {{"enabled", flag(true)}}));
    leaf = object("DataLayerColor",
                  {{"label", text("leaf")},
                   {"enabled", flag(enabled)},
                   {"actions", stack("DataStackActions", actions)},
                   {"maskActions", stack("DataStackActions", masks)},
                   {"maskEnabled", flag(true)},
                   {"maskInitial", scalar(.4f)},
                   {"perChannelBlending", array({blend(4, layer_op)})}});
    root = object("DataLayerGroup",
                  {{"label", text("root")},
                   {"enabled", flag(parent_enabled)},
                   {"subStack", stack("DataStackLayers", {leaf})},
                   {"maskInitial", scalar(1)}});
    p.root = object("DataLayersPreset", {{"layerGroup", root}});
    p.dataset = "/preset.bin";
    p.encoded = {4, 2};
    a.presets.push_back(std::move(p));
    a.references.push_back({0,
                            url,
                            "urlToBitmapRes",
                            "resource://fixture?version=test.image",
                            "/resources/test.image"});
    return a;
  }
};
PreparedSpsm prepare(Builder &b, const s::Archive &a)
{
  return prepare_spsm_material(sem::inspect_archive(b.build(a)));
}
// Independent raw-half bilinear repeat oracle: no native mapping/sampling
// helper.
float raw_sample(const SpsmBitmapTexture &image, double u, double v)
{
  const auto &h = *image.original.encoded->header;
  u -= std::floor(u);
  v -= std::floor(v);
  const double x = u * h.width - .5, y = v * h.height - .5;
  const int ix = std::floor(x), iy = std::floor(y);
  const auto at = [&](int x, int y) {
    x = (x % int(h.width) + h.width) % h.width;
    y = (y % int(h.height) + h.height) % h.height;
    const auto sy = image.policy.rows == SpsmRowOrder::FirstRowIsV0 ? y : h.height - 1 - y;
    const auto off = sy * h.pitch + x * 8;
    const auto *p = image.original.bytes->data() + off;
    return blender::math::half_to_float(std::uint16_t(p[0] | (p[1] << 8)));
  };
  return std::lerp(std::lerp(at(ix, iy), at(ix + 1, iy), float(x - ix)),
                   std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), float(x - ix)),
                   float(y - iy));
}
void sample_oracle(const PreparedSpsm &p, const char *name, const SpsmBitmapTexture &image)
{
  const auto &l = named(p.material, name);
  auto mapping = l.fill;
  mapping.texture = fill_channel_texture(l.fill, 2);
  mapping.sample_alpha = l.fill.alpha_mask[2];
  double max_error = 0;
  for (int tile : {1001, 1017, 2000})
    for (auto xy : {std::pair(0, 0), std::pair(25, 81), std::pair(63, 64), std::pair(127, 127)}) {
      float expected = 0;
      for (double dx : {.25, .75})
        for (double dy : {.25, .75}) {
          const double u = ((xy.first + dx) / 128 + (tile - 1001) % 10 - .5) * l.fill.scale[0] +
                           .5,
                       v = ((xy.second + dy) / 128 + (tile - 1001) / 10 - .5) * l.fill.scale[1] +
                           .5;
          expected += raw_sample(image, u, v) * .25f;
        }
      auto set = p.material.set;
      set.texture_resolution = 128;
      set.texture_height = 0;
      set.tile_resolutions.clear();
      const auto got = TextureDocument::fill_sample(set, l, 2, xy.first, xy.second, tile);
      max_error = std::max(max_error, double(std::abs(expected - got[0])));
      require(std::abs(expected - got[0]) < 1e-6f && got[3] == 1,
              "Actual UV R sample/ignored data A differs from raw oracle");
    }
  std::cout << "PASS raw half CPU oracle " << name << " scale=" << l.fill.scale[0] << ','
            << l.fill.scale[1] << " max_error=" << max_error << " samples=12\n";
}
void synthetic()
{
  QTemporaryDir dir;
  require(dir.isValid(), "Fixture directory failed");
  const auto archive = image_file(dir.filePath("synthetic.spsm").toStdString());
  SpsmBitmapTextureCache cache;
  unsigned calls = 0;
  std::shared_ptr<const SpsmBitmapTexture> image;
  SpsmBitmapResolver resolver = [&](auto &d, auto &p, auto o) {
    ++calls;
    return image = cache.load(d, p, o);
  };
  Builder b;
  const auto original = prepare(b, archive);
  const auto before = original.material.set.layers;
  auto q = enhance_prepared_spsm_bitmaps(original, {}, resolver);
  require(!calls && has(q, "bitmap-policy-required") && !named(q.material, "leaf").visible,
          "Unknown policy hydrated/enabled");
  auto options = policy();
  auto restored = enhance_prepared_spsm_bitmaps(original, options, resolver);
  require(calls == 1 && image, "Eligible image not resolved");
  const auto &l = named(restored.material, "leaf");
  require(l.visible && l.channels[2] && !l.channels[0] && !l.channels[3] && !l.channels[4] &&
              !l.ao_enabled && l.opacity[2] == .6f * .8f * .7f &&
              l.blending[2] == LayerBlend::Multiply && l.mask->background == .4f &&
              !l.fill.alpha_mask[2],
          "Coverage/blend/channel restoration wrong");
  require(restored.source == original.source && !named(original.material, "leaf").visible &&
              !fill_has_texture(named(original.material, "leaf").fill) &&
              has(restored, "bitmap-roughness-restored") &&
              has(restored, "unsupported-content-stack"),
          "Source/audit mutated");
  sample_oracle(restored, "leaf", *image);
  auto twice = enhance_prepared_spsm_bitmaps(original, options, resolver);
  require(fill_channel_texture(named(twice.material, "leaf").fill, 2) ==
              fill_channel_texture(l.fill, 2),
          "Live repeated binding/pixels not shared");
  auto repeat = enhance_prepared_spsm_bitmaps(restored, options, resolver);
  require(calls == 2 && fill_channel_texture(named(repeat.material, "leaf").fill, 2) ==
                            fill_channel_texture(l.fill, 2),
          "Already restored binding changed");
  TextureSet legacy;
  legacy.bump_origin = 0;
  auto adapted = spsm_material_for_destination(restored, legacy);
  require(fill_channel_texture(named(adapted, "leaf").fill, 2) == fill_channel_texture(l.fill, 2),
          "Legacy helper rejected/copied Roughness texture");
  for (int mode = 0; mode < 8; ++mode) {
    Builder bad;
    if (mode == 0)
      bad.levels = 128;
    if (mode == 1)
      bad.group_mask = true;
    if (mode == 2)
      bad.enabled = false;
    if (mode == 3)
      bad.parent_enabled = false;
    if (mode == 4)
      bad.levels = 0;
    if (mode == 5)
      bad.fills = 2;
    if (mode == 7)
      bad.fill_enabled = false;
    if (mode == 6)
      bad.source_op = std::numeric_limits<float>::quiet_NaN();
    if (mode == 6) {
      auto invalid = prepare(bad, archive);
      reject([&] { enhance_prepared_spsm_bitmaps(invalid, options, resolver); });
      continue;
    }
    auto input = prepare(bad, archive);
    const unsigned start = calls;
    auto output = enhance_prepared_spsm_bitmaps(input, options, resolver);
    const auto &leaf = named(output.material, "leaf");
    if (mode == 0 || mode == 1 || mode == 5 || mode == 7)
      require(calls == start && !leaf.visible,
              "Unsupported operative action/mask hydrated/enabled");
    else if (mode == 2)
      require(calls == start + 1 && !leaf.visible, "Disabled source became visible");
    else if (mode == 3)
      require(calls == start + 1 && !named(output.material, "root").visible,
              "Invisible ancestor became visible");
    else
      require(calls == start + 1 && leaf.visible, "Single Fill not restored");
  }
  const auto count = calls;
  for (auto sampling : {SpsmBitmapSampling::SharpRepeat, SpsmBitmapSampling::NearestRepeat}) {
    auto unknown = options;
    unknown.sampling = sampling;
    auto skipped = enhance_prepared_spsm_bitmaps(original, unknown, resolver);
    require(!named(skipped.material, "leaf").visible && calls == count,
            "Unproven sampling policy hydrated");
  }

  enhance_prepared_spsm_bitmaps(original, options, resolver);
  require(calls > count, "Former memory policy skipped resolver");
  options = policy();
  {
    TextureRamQueryScope low(+[]() -> std::uint64_t { return 32; });
    enhance_prepared_spsm_bitmaps(original, options, resolver);
  }
  reject([&] {
    enhance_prepared_spsm_bitmaps(
        original, options, [&](auto &, auto &, auto) -> std::shared_ptr<const SpsmBitmapTexture> {
          throw std::runtime_error("injected resolver failure");
        });
  });
  require(!fill_has_texture(named(original.material, "leaf").fill),
          "Resolver failure mutated input");
  auto missing_binding = original;
  missing_binding.layers.pop_back();
  reject([&] { enhance_prepared_spsm_bitmaps(missing_binding, options, resolver); });
  auto invalid_ir = *original.source;
  for (auto &layer : invalid_ir.presets[0].layers)
    if (!layer.group)
      layer.actions.push_back(std::numeric_limits<std::size_t>::max());
  auto bad_index = original;
  bad_index.source = std::make_shared<const sem::Inspection>(std::move(invalid_ir));
  reject([&] { enhance_prepared_spsm_bitmaps(bad_index, options, resolver); });
  require(calls == count, "Malformed bindings/IR hydrated before rejection");
  auto wrong = std::make_shared<SpsmBitmapTexture>(*image);
  wrong->source.dataset.path = "/resources/wrong.image";
  reject([&] {
    enhance_prepared_spsm_bitmaps(original, options, [&](auto &, auto &, auto) { return wrong; });
  });
  auto wrong_archive = archive;
  Builder mismatch;
  auto built = mismatch.build(wrong_archive);
  built.references[0].source = mismatch.src;
  auto badref = prepare_spsm_material(sem::inspect_archive(std::move(built)));
  auto skipped = enhance_prepared_spsm_bitmaps(badref, options, resolver);
  require(calls == count && !named(skipped.material, "leaf").visible,
          "Wrong resource source node hydrated");
  for (int mutation = 0; mutation < 4; ++mutation) {
    Builder test;
    auto a = test.build(archive);
    if (mutation == 0)
      a.references[0].uri += "wrong";
    if (mutation == 1)
      a.references[0].resource_dataset = "/resources/different.image";
    if (mutation == 2) {
      for (auto &f : std::get<s::Object>(a.presets[0].nodes[test.fill].value).fields)
        if (f.name == "projection")
          a.presets[0].nodes[f.value].value = std::uint64_t(1);
    }
    if (mutation == 3) {
      for (auto &f : std::get<s::Object>(a.presets[0].nodes[test.fill].value).fields)
        if (f.name == "uvTransformation")
          for (auto &v : std::get<s::Object>(a.presets[0].nodes[f.value].value).fields)
            if (v.name == "scaleFactors")
              std::get<s::FloatValues>(a.presets[0].nodes[v.value].value).values[0] = 0;
    }
    auto input = prepare_spsm_material(sem::inspect_archive(std::move(a)));
    auto omitted = enhance_prepared_spsm_bitmaps(input, options, resolver);
    require(calls == count && !named(omitted.material, "leaf").visible,
            "URI/dataset/unknown projection/invalid native scale hydrated");
  }
  const auto v1 = image_file(dir.filePath("v1.spsm").toStdString(), true);
  Builder old;
  auto oldp = prepare(old, v1);
  auto unsupported = enhance_prepared_spsm_bitmaps(oldp, options, resolver);
  require(calls == count && !named(unsupported.material, "leaf").visible, "v1 hydrated");
  const auto encoded = write_material_preset(restored.material);
  auto reopened = read_material_preset(encoded);
  const auto &saved = named(reopened, "leaf");
  require(fill_channel_texture(saved.fill, 2) && fill_channel_texture(saved.fill, 2)->pixels &&
              *fill_channel_texture(saved.fill, 2)->pixels == *image->texture.pixels &&
              saved.fill.scale == l.fill.scale && !saved.fill.alpha_mask[2],
          "Native preset Save/Open lost texture pixels/mapping/alpha");
  std::cout << "PASS synthetic "
               "eligibility/order/Color-onlyLevels/opacity/Multiply/"
               "scalar-mask/sourcevisibility/ancestorvisibility/atomicerrors/"
               "provenance/v1/lowRAM/sharedallocation/nativepreset roundtrip\n";
}
SmartMaterialPreset disk_roundtrip(const SmartMaterialPreset &material)
{
  QTemporaryDir dir;
  require(dir.isValid(), "Roundtrip directory failed");
  const auto bytes = write_material_preset(material);
  QFile file(dir.filePath("roughness.btsmaterial"));
  require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
          "Native preset Save failed");
  file.close();
  require(file.open(QIODevice::ReadOnly), "Native preset Open failed");
  return read_material_preset(file.readAll());
}
void fixtures(const std::filesystem::path &dir)
{
  SpsmBitmapTextureCache cache;
  unsigned calls = 0;
  std::shared_ptr<const SpsmBitmapTexture> image;
  SpsmBitmapResolver resolve = [&](auto &d, auto &p, auto o) {
    ++calls;
    return image = cache.load(d, p, o);
  };
  for (const auto name : {"dirty-plastic-robot.spsm",
                          "shd-hard-rust.spsm",
                          "sratched-metal-pealing-paint-dirt.spsm",
                          "used_varnish_wood.spsm",
                          "worn leather chest.spsm"})
  {
    auto source = prepare_spsm_material(dir / name);
    const auto before = calls;
    auto output = enhance_prepared_spsm_bitmaps(source, policy(), resolve);
    require(source.source == output.source && output.partial && !output.appearance_verified,
            "Source retained/completeness changed");
    if (std::string_view(name) == "used_varnish_wood.spsm") {
      require(calls == before + 1 &&
                  image->source.dataset.path ==
                      "/resources/56271f14721261acbab5874fa9f47a97e59ffb39.image",
              "Wrong Wood leaf/resource or unsupported hydration");
      const auto &l = named(output.material, "Wood_Worn_base");
      require(l.visible && l.channels[2] && !l.channels[0] && !l.channels[3] &&
                  !named(output.material, "Wood_Darken_base").visible &&
                  !named(output.material, "Wood_base_XtraRoughness").visible,
              "Operative Levels/masks/Color/Normal silently dropped");
      require(l.fill.scale[0] == double(4.659999847412109f), "Source 4.66 scale lost/inverted");
      sample_oracle(output, "Wood_Worn_base", *image);
      auto reopened = disk_roundtrip(output.material);
      const auto &saved = named(reopened, "Wood_Worn_base");
      require(fill_channel_texture(saved.fill, 2)->hash == image->texture.hash &&
                  *fill_channel_texture(saved.fill, 2)->pixels == *image->texture.pixels &&
                  saved.fill.scale == l.fill.scale && !saved.fill.alpha_mask[2],
              "Actual native preset disk Save/Open lost bitmap "
              "pixels/binding/mapping");
      auto persisted = output;
      persisted.material = std::move(reopened);
      sample_oracle(persisted, "Wood_Worn_base", *image);
      std::cout << "PASS actual native preset disk Save/Open raw_R_range="
                << image->texture.minimum << ',' << image->texture.maximum
                << " raw_pixels=" << image->texture.pixels->size() << '\n';
      const auto again = enhance_prepared_spsm_bitmaps(source, policy(), resolve);
      require(fill_channel_texture(named(again.material, "Wood_Worn_base").fill, 2) ==
                  fill_channel_texture(l.fill, 2),
              "Actual repeated resource copied allocation");
    }
    else
      require(calls == before, "Unsupported/no-image file hydrated");
    std::cout << "PASS " << name << " resolver_delta=" << calls - before
              << " diagnostics=" << output.diagnostics.size()
              << " original_payload_bytes=" << source.source->source().payload_bytes_read
              << " appearance_verified=false\n";
  }
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    if (argc > 1 && std::string_view(argv[1]) == "--runtime-baseline")
      return 0;
    synthetic();
    if (argc > 1)
      fixtures(argv[1]);
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "SPSM bitmap lowering failed: " << e.what() << '\n';
    return 1;
  }
}
