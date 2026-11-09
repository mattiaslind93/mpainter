// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmNativeBitmaps.hh"
#include "SpsmNativeSemantics.hh"
#include "MaskPreset.hh"
#include <cmath>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <hdf5.h>
#include <lz4.h>
#include <cstdio>
namespace s = bts::spsm;
namespace sem = s::semantic;
using namespace bts;
namespace {
void require(bool value, const char *why)
{
  if (!value)
    throw std::runtime_error(why);
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
  template<class T> s::NodeId add(T value)
  {
    const auto id = p.nodes.size();
    p.nodes.push_back({18, 0, 1, std::move(value)});
    return id;
  }
  s::NodeId object(std::string name, std::initializer_list<s::Field> fields)
  {
    return add(s::Object{std::move(name), fields});
  }
  s::NodeId array(std::vector<s::NodeId> values)
  {
    return add(s::Array{std::move(values)});
  }
  s::NodeId integer(std::uint64_t value)
  {
    return add(value);
  }
  s::NodeId scalar(float value)
  {
    return add(s::FloatValues{{value}, 1});
  }
  s::NodeId text(std::string value)
  {
    return add(std::move(value));
  }
};
void test()
{
  QTemporaryDir directory;
  require(directory.isValid(), "SPMSK image fixture directory");
  auto archive = image_file(directory.filePath("bitmap.spmsk").toStdString());
  Builder b;
  const auto url = b.text("resource://fixture?version=test.image");
  const auto bitmap = b.object("DataBitmap", {{"urlToBitmapRes", url}});
  const auto source = b.object(
      "DataSourceBitmap",
      {{"channelTypes", b.integer(0)}, {"bitmap", bitmap}, {"opacity", b.scalar(.8f)}});
  const auto blend = b.object("DataBlending",
                              {{"channelTypes", b.integer(0)},
                               {"blendingMode", b.integer(4)},
                               {"opacity", b.scalar(.37f)}});
  const auto fill = b.object("DataActionFill",
                             {{"enabled", b.add(true)},
                              {"sources", b.array({source})},
                              {"projection", b.integer(0)},
                              {"perChannelBlending", b.array({blend})}});
  b.p.root = b.object(
      "DataSmartMask",
      {{"actionsStack", b.object("DataStackActions", {{"items", b.array({fill})}})}});
  b.p.dataset = "/preset.bin";
  b.p.encoded = {17, 43};
  archive.presets.push_back(std::move(b.p));
  archive.references.push_back({0,
                                url,
                                "urlToBitmapRes",
                                "resource://fixture?version=test.image",
                                "/resources/test.image"});
  SpsmImportOptions options;
  options.native_actions = true;
  auto prepared = prepare_spsm_material(sem::inspect_archive(std::move(archive)), options);
  require(prepared.source->presets.front().layers.front().group,
          "Fixture does not use the synthetic smart-mask Group owner");
  SpsmBitmapLoweringPolicy policy;
  policy.pixels = {SpsmByteOrder::LittleEndian,
                   SpsmRowOrder::FirstRowIsV0,
                   SpsmAlpha::DataComponents,
                   true,
                   {}};
  policy.uv = SpsmBitmapUV::Projection0CenteredRepeatFactors;
  policy.sampling = SpsmBitmapSampling::HQRepeat;
  policy.mask = SpsmMaskBitmapPolicy::NativeOrderedFillLevelsRawRIgnoreA;
  int calls = 0;
  std::shared_ptr<SpsmBitmapTexture> decoded;
  // This isolated boundary probe uses the production HDF header/LZ4 reader and
  // an explicit little-endian/raw RGBA16F test resolver. Production color/cache
  // interpretation is covered separately; no Blender globals are initialized.
  SpsmBitmapResolver resolve = [&](const auto &descriptor,
                                   const auto &pixels,
                                   auto budget) -> std::shared_ptr<const SpsmBitmapTexture> {
    ++calls;
    require(pixels == policy.pixels && descriptor.resource.dataset.path == "/resources/test.image",
            "Unexpected native bitmap policy/source");
    decoded = std::make_shared<SpsmBitmapTexture>();
    decoded->original = s::bitmap::decode(s::bitmap::load_encoded(descriptor, budget), budget);
    decoded->source = descriptor.resource;
    decoded->policy = pixels;
    auto &texture = decoded->texture;
    texture.asset = "21cb7e46-6e9d-45e3-8585-b8ef50835fd1";
    texture.hash = std::string(64, 'a');
    texture.name = "Embedded raw mask";
    texture.width = texture.height = 2;
    texture.raw = true;
    texture.input_space = "Non-Color/raw";
    std::vector<FillTexture::Pixel> values(4);
    for (int i = 0; i < 4; ++i)
      for (int c = 0; c < 4; ++c) {
        const auto offset = (i * 4 + c) * 2;
        const auto &raw = *decoded->original.bytes;
        values[i][c] = [&] {
          const unsigned bits = unsigned(raw[offset]) | (unsigned(raw[offset + 1]) << 8),
                         exponent = (bits >> 10) & 31, fraction = bits & 1023;
          require(exponent < 31, "Nonfinite raw-half fixture");
          return (bits & 32768 ? -1.f : 1.f) *
                 std::ldexp(float(exponent ? 1024 + fraction : fraction),
                            exponent ? int(exponent) - 25 : -24);
        }();
      }
    texture.pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(std::move(values));
    texture.validate();
    return decoded;
  };
  hydrate_spsm_native_bitmaps(prepared, policy, resolve, {});
  require(calls == 1 && prepared.bitmap_mask_sources.size() == 1,
          "Synthetic Group owner skipped embedded smart-mask pixels");
  lower_spsm_native_actions(prepared, {});
  const auto &layer = prepared.material.set.layers.front();
  require(layer.mask && layer.mask->effects.size() == 1, "Hydrated ordered mask Fill absent");
  const auto &effect = layer.mask->effects.front();
  require(effect.kind == MaskEffectKind::Fill && effect.blending == LayerBlend::Multiply &&
              std::abs(effect.opacity - .8f * .37f) < 1e-7f && effect.mapping.projection.texture &&
              effect.mapping.projection.texture->pixels == decoded->texture.pixels &&
              !effect.mapping.projection.alpha_mask[0],
          "SPMSK hydration lost source mix, shared pixels or raw-R policy");
  const auto &actual = *effect.mapping.projection.texture->pixels;
  require(actual[0][0] == 0 && actual[1][0] == .25f && actual[2][0] == .5f && actual[3][0] == 1 &&
              actual[0][3] == 2 && actual[2][3] == 4,
          "Embedded HDF pixels changed");
  const auto reopened = read_mask_preset(write_mask_preset(*layer.mask));
  require(reopened.effects.size() == 1 && reopened.effects.front().mapping.projection.texture &&
              *reopened.effects.front().mapping.projection.texture->pixels == actual &&
              reopened.effects.front().opacity == effect.opacity,
          "Embedded smart-mask preset lost pixels or mix");
  std::puts(
      "PASS DataSmartMask embedded bitmap: actual HDF/LZ4 input, synthetic Group owner hydration, editable ordered Multiply/opacity, raw R with ignored data alpha, native preset pixels roundtrip");
  {
    // Two later image actions share immutable decoded pixels, while each keeps
    // its own UV transform, source/action opacity and noncommuting blend order.
    auto material_archive = image_file(directory.filePath("ordered.spsm").toStdString());
    Builder c;
    const auto rgb = [&](float v) { return c.add(s::FloatValues{{v, v, v}, 3}); };
    const auto stack = [&](std::vector<s::NodeId> values) {
      return c.object("DataStackActions", {{"items", c.array(std::move(values))}});
    };
    const auto blend = [&](unsigned mode, float opacity) {
      return c.object("DataBlending",
                      {{"channelTypes", c.integer(129)},
                       {"blendingMode", c.integer(mode)},
                       {"opacity", c.scalar(opacity)}});
    };
    const auto uniform = c.object("DataSourceUniform",
                                  {{"channelTypes", c.integer(129)}, {"color", rgb(.2f)}});
    const auto initial = c.object("DataActionFill",
                                  {{"enabled", c.add(true)},
                                   {"channelTypes", c.integer(129)},
                                   {"sources", c.array({uniform})}});
    std::vector<s::NodeId> urls;
    const auto image_action = [&](float scale, float offset, unsigned mode, float opacity) {
      const auto url = c.text("resource://fixture?version=test.image");
      urls.push_back(url);
      const auto bitmap = c.object("DataBitmap", {{"urlToBitmapRes", url}});
      const auto source = c.object(
          "DataSourceBitmap",
          {{"channelTypes", c.integer(129)}, {"bitmap", bitmap}, {"opacity", c.scalar(.8f)}});
      const auto uv = c.object("DataUvTransformation",
                               {{"scaleFactors", c.add(s::FloatValues{{scale, 1}, 2})},
                                {"translation", c.add(s::FloatValues{{offset, 0}, 2})}});
      return c.object("DataActionFill",
                      {{"enabled", c.add(true)},
                       {"channelTypes", c.integer(129)},
                       {"sources", c.array({source})},
                       {"projection", c.integer(0)},
                       {"uvTransformation", uv},
                       {"perChannelBlending", c.array({blend(mode, opacity)})}});
    };
    const auto first = image_action(2, .125f, 11, .37f), second = image_action(3, -.1f, 4, .6f);
    const auto curve = c.object("DataLevels",
                                {{"clamp", c.integer(0)},
                                 {"gamma", rgb(2)},
                                 {"inputMinimum", rgb(0)},
                                 {"inputMaximum", rgb(1)},
                                 {"outputMinimum", rgb(0)},
                                 {"outputMaximum", rgb(1)}});
    const auto levels = c.object(
        "DataActionFilterLevels",
        {{"enabled", c.add(true)}, {"channelTypes", c.integer(128)}, {"levels", curve}});
    const auto leaf = c.object("DataLayerColor",
                               {{"label", c.text("Ordered images")},
                                {"enabled", c.add(true)},
                                {"actions", stack({initial, first, second, levels})}});
    const auto root = c.object(
        "DataLayerGroup",
        {{"subStack", c.object("DataStackLayers", {{"items", c.array({leaf})}})}});
    c.p.root = c.object("DataLayersPreset", {{"layerGroup", root}});
    c.p.dataset = "/preset.bin";
    c.p.encoded = {17, 44};
    material_archive.presets.push_back(std::move(c.p));
    for (auto url : urls)
      material_archive.references.push_back({0,
                                             url,
                                             "urlToBitmapRes",
                                             "resource://fixture?version=test.image",
                                             "/resources/test.image"});
    const auto identity = [](std::array<float, 3> rgb) { return rgb; };
    SpsmImportOptions material_options;
    material_options.uniform_rgb_to_scene_linear = identity;
    auto material = prepare_spsm_material(sem::inspect_archive(std::move(material_archive)),
                                          material_options);
    std::shared_ptr<const SpsmBitmapTexture> shared;
    SpsmBitmapResolver shared_resolve = [&](const auto &d, const auto &p, auto budget) {
      if (!shared)
        shared = resolve(d, p, budget);
      return shared;
    };
    const auto before_unknown = calls;
    auto deferred = material;
    hydrate_spsm_native_bitmaps(deferred, policy, shared_resolve, {false, {}});
    require(calls == before_unknown && deferred.bitmap_content_sources.empty(),
            "Unknown bitmap channel policies decoded content");
    lower_spsm_native_actions(deferred, identity);
    require(std::none_of(deferred.material.set.layers.front().effects.begin(),
                         deferred.material.set.layers.front().effects.end(),
                         [](const auto &e) { return bool(e.bitmap_fill); }),
            "Unknown bitmap channel policy became canonical data");
    auto scalar_only = material;
    hydrate_spsm_native_bitmaps(scalar_only, policy, shared_resolve, {true, {}});
    lower_spsm_native_actions(scalar_only, identity);
    int scalar_images = 0;
    for (const auto &e : scalar_only.material.set.layers.front().effects)
      if (e.bitmap_fill) {
        require(e.channel == 2 && !e.linear_color,
                "Roughness hydration implicitly opted into Color");
        ++scalar_images;
      }
    require(scalar_images == 2, "Roughness-only ordered sources lost");
    auto color_matrix = std::make_shared<LinearColorTransform>();
    color_matrix->to_aces = {2, 0, 0, 0, 3, 0, 0, 0, 4};
    hydrate_spsm_native_bitmaps(material, policy, shared_resolve, {true, color_matrix});
    require(material.bitmap_content_sources.size() == 2,
            "Secondary embedded content sources were not hydrated");
    lower_spsm_native_actions(material, identity);
    const auto &content = material.material.set.layers.front();
    require(content.effects.size() == 7 && !content.fill.texture,
            "Ordered images replaced initial uniform or lost channels/Levels");
    const auto &a = content.effects[3], &b = content.effects[5];
    require(a.bitmap_fill && b.bitmap_fill && a.channel == 2 && b.channel == 2 &&
                a.blending == LayerBlend::Overlay && b.blending == LayerBlend::Multiply,
            "Ordered image source types/blends changed");
    require(std::abs(a.opacity - .8f * .37f) < 1e-7f && std::abs(b.opacity - .8f * .6f) < 1e-7f &&
                a.bitmap_fill->projection.scale[0] == 2 && b.bitmap_fill->projection.scale[0] == 3,
            "Per-source mapping/opacity lost");
    require(a.bitmap_fill->projection.texture->pixels == b.bitmap_fill->projection.texture->pixels,
            "Same bitmap action record duplicated decoded pixels");
    float coverage = 1;
    const auto actual = apply_content_effects(
        content.effects, 2, {.2f, .2f, .2f}, coverage, 7, 3, 1011, 16, 32, ChannelPrecision::L32F);
    // Independent source sampling uses the raw texture directly; the chain's
    // expected arithmetic is deliberately evaluated outside apply_content_effects.
    const auto sample = [&](const auto &effect) {
      float value = 0;
      const auto &p = effect.bitmap_fill->projection;
      for (double dx : {.25, .75})
        for (double dy : {.25, .75}) {
          const double u = ((7 + dx) / 16 - .5 - p.offset[0]) * p.scale[0] + .5,
                       v = (3 + dy) / 32 + 1;
          value += p.texture->sample(u, v, true, false)[0] * .25f;
        }
      return value;
    };
    const float u = sample(a), v = sample(b);
    const float over = .2f <= .5f ? 2 * u * .2f : 1 - 2 * (1 - u) * .8f;
    const float mixed = .2f + (.8f * .37f) * (over - .2f),
                multiplied = mixed + (.8f * .6f) * (v * mixed - mixed);
    const float expected = std::sqrt(multiplied);
    require(std::abs(actual[0] - expected) < 2e-6f && coverage == 1,
            "Secondary images/Levels changed source order or ignored rectangular/tile UVs");
    float rgb_coverage = 1;
    const auto rgb_result = apply_content_effects(content.effects,
                                                  0,
                                                  {.2f, .2f, .2f},
                                                  rgb_coverage,
                                                  7,
                                                  3,
                                                  1011,
                                                  16,
                                                  32,
                                                  ChannelPrecision::RGB32F);
    const float first_rgb = .2f + .8f * .37f * (2 * (2 * u) * .2f - .2f),
                expected_rgb = first_rgb + .8f * .6f * ((2 * v) * first_rgb - first_rgb);
    require(std::abs(rgb_result[0] - expected_rgb) < 2e-6f,
            "Independent bitmap Color transform was lost or tinted by initial uniform");
    auto reopened = read_effects(effects_json(content.effects));
    require(reopened[3].bitmap_fill && reopened[5].bitmap_fill &&
                reopened[3].bitmap_fill->projection.scale[0] == 2 &&
                reopened[5].bitmap_fill->projection.scale[0] == 3,
            "Ordered bitmap metadata lost during codec");
    require(reopened[2].linear_color && reopened[2].linear_color->to_aces == color_matrix->to_aces,
            "Bitmap Color interpretation lost on codec");
    std::puts(
        "PASS SPSM ordered embedded images: initial uniform, two independent UV transforms, shared raw HDF pixels, Overlay/Multiply opacity, following Levels, rectangular/tile CPU oracle and metadata codec");
  }
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  try {
    test();
    return 0;
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "SPMSK embedded bitmap failed: %s\n", error.what());
    return 1;
  }
}
