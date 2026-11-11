// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include "BlenderHost.hh"
#include "ProjectIO.hh"
#include "SpsmSource.hh"
#include "SpsmAssetDispatch.hh"
#include "FillTextureJSON.hh"
#include "NumericLocale.hh"
#include "BLI_math_half.hh"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QThread>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QSurfaceFormat>
#include <future>
#include <set>
#include <cmath>
#include <cstdio>

using namespace bts;
namespace {
void require(bool ok, const char *text)
{
  if (!ok)
    throw std::runtime_error(text);
}
QStringList policy()
{
  return {"spsm:policy:bitmap-roughness=raw-r-ignore-a",
          "spsm:policy:bitmap-bytes=little-endian",
          "spsm:policy:bitmap-rows=first-row-v0",
          "spsm:policy:bitmap-alpha=data-components",
          "spsm:policy:bitmap-uv=projection0-centered-repeat-factors",
          "spsm:policy:bitmap-sampling=hq-repeat",
          "spsm:policy:levels=native-scalar",
          "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"};
}
const TextureLayer &named(const SmartMaterialPreset &p, const char *name)
{
  for (const auto &l : p.set.layers)
    if (l.name == name)
      return l;
  throw std::runtime_error("Expected source layer missing");
}
spsm::NodeId field(const spsm::Preset &p, spsm::NodeId id, std::string_view name)
{
  for (const auto &f : std::get<spsm::Object>(p.nodes.at(id).value).fields)
    if (f.name == name)
      return f.value;
  throw std::runtime_error("Expected raw source field missing");
}
std::uint64_t integer(const spsm::Value &v)
{
  if (auto n = std::get_if<std::uint64_t>(&v))
    return *n;
  if (auto n = std::get_if<std::int64_t>(&v); n && *n >= 0)
    return *n;
  if (auto n = std::get_if<spsm::IntValues>(&v); n && n->count == 1 && n->values[0] >= 0)
    return n->values[0];
  throw std::runtime_error("Expected source integer");
}
struct Curve {
  float low, high, gamma, out_low, out_high;
  bool clamp;
};
Curve source_curve(const PreparedSpsm &prepared, const TextureLayer &leaf)
{
  const auto &inspection = *prepared.source;
  const auto pi = *inspection.primary_preset;
  const auto &p = inspection.source().presets.at(pi);
  const auto &ir = inspection.presets.at(pi);
  spsm::NodeId layer = 0;
  bool found = false;
  for (const auto &b : prepared.layers)
    if (b.native_layer == leaf.id) {
      require(!found, "Duplicate native source binding");
      layer = b.source;
      found = true;
    }
  require(found, "Source leaf binding missing");
  for (const auto &l : ir.layers)
    if (l.source == layer)
      for (auto ai : l.mask_actions) {
        const auto &a = ir.actions.at(ai);
        if (a.kind != spsm::semantic::ActionKind::Levels)
          continue;
        if (integer(p.nodes.at(field(p, a.source, "channelTypes")).value) != 0)
          continue;
        const auto levels = field(p, a.source, "levels");
        const auto scalar = [&](std::string_view key) {
          const auto &v = std::get<spsm::FloatValues>(p.nodes.at(field(p, levels, key)).value);
          require(v.count == 3 && v.values[0] == v.values[1] && v.values[1] == v.values[2],
                  "Actual scalar source curve changed");
          return v.values[0];
        };
        return {scalar("inputMinimum"),
                scalar("inputMaximum"),
                scalar("gamma"),
                scalar("outputMinimum"),
                scalar("outputMaximum"),
                integer(p.nodes.at(field(p, levels, "clamp")).value) == 1};
      }
  throw std::runtime_error("Actual source mask Levels absent");
}
// Deliberately independent of native Fill sampling and Levels helpers. Only the
// exact image resource is read: no procedural package or compiled graph payload.
struct RawOracle {
  spsm::bitmap::RawImage image;
  Curve curve;
  FillProjection projection;
  float at(int x, int y) const
  {
    const auto &h = *image.encoded->header;
    x = (x % int(h.width) + int(h.width)) % int(h.width);
    y = (y % int(h.height) + int(h.height)) % int(h.height);
    const auto off = std::uint64_t(y) * h.pitch + std::uint64_t(x) * 8;
    const auto *p = image.bytes->data() + off;
    return blender::math::half_to_float(std::uint16_t(p[0] | (std::uint16_t(p[1]) << 8)));
  }
  float sample(double u, double v) const
  {
    const auto &h = *image.encoded->header;
    u -= std::floor(u);
    v -= std::floor(v);
    const auto x = u * h.width - .5, y = v * h.height - .5;
    const int ix = std::floor(x), iy = std::floor(y);
    return std::lerp(std::lerp(at(ix, iy), at(ix + 1, iy), float(x - ix)),
                     std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), float(x - ix)),
                     float(y - iy));
  }
  float raw(int x, int y) const
  {
    float value = 0;
    for (double dx : {.25, .75})
      for (double dy : {.25, .75}) {
        const auto u = ((x + dx) / 128 - .5) * projection.scale[0] + .5,
                   v = ((y + dy) / 128 - .5) * projection.scale[1] + .5;
        value += sample(u, v) * .25f;
      }
    return value;
  }
  float curve_value(float input, bool clamp) const
  {
    double value = (double(input) - curve.low) / (double(curve.high) - curve.low);
    if (value > 0)
      value = std::pow(value, 1 / double(curve.gamma));
    value = curve.out_low + (double(curve.out_high) - curve.out_low) * value;
    value = std::clamp(value, -65504., 65504.);
    if (clamp)
      value = std::clamp(value, 0., 1.);
    return float(value);
  }
  float result(int x, int y, bool clamp) const
  {
    return curve_value(raw(x, y), clamp);
  }
};
std::vector<std::array<int, 2>> positions(const RawOracle &oracle)
{
  std::array<int, 2> low{0, 0}, high{0, 0};
  float lo = oracle.result(0, 0, false), hi = lo;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      auto v = oracle.result(x, y, false);
      if (v < lo) {
        lo = v;
        low = {x, y};
      }
      if (v > hi) {
        hi = v;
        high = {x, y};
      }
    }
  require(hi - lo > 1e-5f, "Actual bitmap mask oracle is constant/background");
  std::vector<std::array<int, 2>> result;
  const auto add = [&](std::array<int, 2> p) {
    if (std::find(result.begin(), result.end(), p) == result.end())
      result.push_back(p);
  };
  add(low);
  add(high);
  for (auto p : {std::array{7, 13},
                 std::array{31, 23},
                 std::array{73, 91},
                 std::array{113, 45},
                 std::array{0, 0},
                 std::array{25, 81},
                 std::array{63, 64},
                 std::array{127, 127},
                 std::array{9, 117},
                 std::array{106, 3},
                 std::array{57, 39},
                 std::array{2, 69}})
    if (result.size() < 12)
      add(p);
  require(result.size() == 12, "Twelve distinct asymmetric probes missing");
  return result;
}
}  // namespace
int main(int argc, char **argv)
{
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QSurfaceFormat format;
  format.setVersion(4, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  format.setDepthBufferSize(24);
  QSurfaceFormat::setDefaultFormat(format);
  initialize_numeric_locale();
  QApplication app(argc, argv);
  try {
    require(argc == 3, "Expected OUTPUT_DIRECTORY and private copied Wood SPSM fixture");
    const auto directory = QString::fromLocal8Bit(argv[1]),
               input = QString::fromLocal8Bit(argv[2]);
    QDir().mkpath(directory);
    require(QFileInfo(input).absoluteFilePath().startsWith(
                QFileInfo(directory).absoluteFilePath() + "/"),
            "Fixture must be the verifier's own private copy");
    qputenv("BTS_RESOURCE_CACHE", (directory + "/first-cache").toUtf8());
    configure_spsm_source_hydration();
    BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "Offscreen GL context failed");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface), "Offscreen activation failed");
    QOpenGLFramebufferObject target(256, 256, QOpenGLFramebufferObject::CombinedDepthStencil);
    require(target.isValid(), "Framebuffer failed");
    struct Close {
      BlenderHost &h;
      ~Close()
      {
        h.shutdown();
      }
    } close{host};
    host.initialize_gpu(context, surface, target.handle());
    require(QString::fromStdString(host.renderer()).contains("NVIDIA", Qt::CaseInsensitive),
            "Hardware native GPU renderer required");
    const auto mesh = directory + "/quad.obj";
    {
      QFile file(mesh);
      require(file.open(QIODevice::WriteOnly), "Cannot write private mesh");
      file.write(
          "o DarkenFixture\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");
    }
    NewProjectOptions options;
    options.resolution = DocumentResolution::Pixels128;
    options.uv_workflow = UvWorkflow::PreserveTiles;
    options.absolute_mesh_path = mesh.toStdString();
    const auto environment = qEnvironmentVariable(
        "BTS_ENVIRONMENT",
        QCoreApplication::applicationDirPath() +
            "/5.2/datafiles/studiolights/world/default-studio.hdr");
    const auto create = [&] {
      auto summary = host.new_project(mesh.toStdString(), environment.toStdString(), &options);
      return TextureDocument(options, summary, host.scene_snapshot());
    };
    auto document = create();
    ViewFrame frame;
    frame.pane = {ViewKind::Scene, 0, 0, 256, 256};
    QMatrix4x4 view, projection;
    view.translate(-.5f, -.5f, -5);
    projection.ortho(-.6f, .6f, -.6f, .6f, .01f, 100.f);
    std::copy_n(view.constData(), 16, frame.view.begin());
    std::copy_n(projection.constData(), 16, frame.projection.begin());
    frame.uv_center = {.5, .5};
    frame.uv_extent = 1.2;
    std::vector<std::shared_ptr<const FillTexture>> source_bindings;
    const auto publish = [&](bool preload = false) {
      auto &set = document.set(document.texture_sets().front().material_session_uid);
      host.set_selected_material(set.material_session_uid);
      host.prepare_paint_texture(set);
      const InspectionMaterial inspected{
          set.material_session_uid, set.base_values, {}, nullptr, 128};
      host.set_inspection(InspectionMode::Material, std::span(&inspected, 1));
      std::vector<std::shared_ptr<const FillTexture>> textures = source_bindings;
      for (const auto &layer : set.layers) {
        if (auto texture = fill_channel_texture(layer.fill, 2))
          textures.push_back(texture);
        if (layer.mask)
          for (const auto &effect : layer.mask->effects)
            if (auto texture = effect.mapping.projection.texture)
              textures.push_back(texture);
      }
      QElapsedTimer elapsed;
      elapsed.start();
      while (preload && !host.prepare_texture_resources(textures)) {
        context.makeCurrent(&surface);
        target.bind();
        host.draw(std::span(&frame, 1), 256, 256);
        QCoreApplication::processEvents();
        QThread::msleep(1);
        require(elapsed.elapsed() < 60000, "Source GPU upload timed out");
      }
      host.apply_texture_set(set, 128);
      do {
        context.makeCurrent(&surface);
        target.bind();
        host.draw(std::span(&frame, 1), 256, 256);
        QCoreApplication::processEvents();
        require(elapsed.elapsed() < 60000, "Native GPU publication timed out");
      } while (host.texture_updates_pending());
    };
    const auto capture_mask = [&](std::span<const std::array<int, 2>> probes, bool mask = true) {
      std::vector<float> values;
      const auto &set = document.texture_sets().front();
      for (auto p : probes) {
        LayerCaptureRequest request;
        request.layer = set.layers.front().id;
        request.channel = 2;
        request.tile = 1001;
        request.x = p[0];
        request.y = p[1];
        request.width = request.height = 1;
        request.canvas_width = request.canvas_height = 128;
        request.mask = mask;
        const auto ticket = host.request_layer_capture(set, request);
        QElapsedTimer timer;
        timer.start();
        while (true) {
          context.makeCurrent(&surface);
          target.bind();
          host.draw(std::span(&frame, 1), 256, 256);
          QCoreApplication::processEvents();
          const auto result = host.layer_capture_result();
          if (result && result->ticket == ticket) {
            require(result->valid && result->pixels && result->pixels->size() == 1,
                    "Native raw mask capture invalid");
            values.push_back(result->pixels->front()[0]);
            break;
          }
          require(timer.elapsed() < 30000, "Native raw mask capture timeout");
          QThread::msleep(1);
        }
      }
      return values;
    };
    std::vector<std::array<int, 2>> probes;
    std::vector<std::array<float, 4>> before;
    std::vector<float> reference, coverages, prefixes;
    QJsonValue descriptor;
    QJsonArray effect_json;
    QString source_hash, storage_identity;
    double cpu_error = 0, gpu_error = 0, prefix_error = 0, synthetic_prefix_error = 0,
           synthetic_composite_error = 0, raw_gpu_error = 0, packet_error = 0,
           actual_cpu_prefix_delta = 0;
    double raw_low = 1e30, raw_high = -1e30, source_prefix_low = 1e30, source_prefix_high = -1e30;
    int synthetic_negative = 0, synthetic_above_one = 0;
    int negative = 0, above_one = 0;
    std::weak_ptr<const PreparedSpsmSource> original_preparation;
    const auto project = directory + "/packed-bitmap-mask.blend";
    {
      StudioAsset source;
      source.filename = "used_varnish_wood.spsm";
      source.name = "Packed original Wood";
      source.id = QUuid::createUuid();
      source.scope = AssetScope::Project;
      source.usages = {"smartmaterial"};
      source.smart_material = true;
      source.search_terms = policy();
      QFile file(input);
      require(file.open(QIODevice::ReadOnly), "Cannot read private fixture");
      source.bytes = file.readAll();
      source_hash = source.hash();
      file.close();
      source = std::async(std::launch::async, [source]() mutable {
                 hydrate_asset_presets(source);
                 return source;
               }).get();
      require(bool(source.material_preset), "Explicit mask source did not hydrate");
      const auto prepared = prepared_spsm_source(source);
      require(prepared && !prepared->prepared.appearance_verified,
              "Source appearance must remain unverified");
      original_preparation = prepared;
      const auto &ancestor = named(*source.material_preset, "Wood_Color_Roughness_base");
      require(ancestor.mask && ancestor.mask->enabled && ancestor.mask->background == 0 &&
                  ancestor.mask->effects.empty(),
              "Original zero ancestor mask changed");
      const auto &leaf = named(*source.material_preset, "Wood_base_XtraRoughness"),
                 &worn = named(*source.material_preset, "Wood_Worn_base");
      require(leaf.visible && leaf.channels[2] && leaf.effects.empty() && leaf.mask &&
                  leaf.mask->enabled && leaf.mask->background == 0 &&
                  leaf.mask->effects.size() == 2 && leaf.opacity[2] == 1 &&
                  leaf.blending[2] == LayerBlend::Normal,
              "Actual atomic mask leaf contract changed");
      const auto &dark = named(*source.material_preset, "Wood_Darken_base");
      require(dark.visible && dark.effects.size() == 1,
              "Independent content Levels policy regressed with mask opt-in");
      const auto binding = fill_channel_texture(leaf.fill, 2),
                 worn_binding = fill_channel_texture(worn.fill, 2),
                 mask_binding = leaf.mask->effects[0].mapping.projection.texture;
      require(binding && binding == worn_binding && binding == mask_binding,
              "Three source bindings are not one owner");
      storage_identity = QString::fromStdString(binding->storage_identity());
      source_bindings = {worn_binding};
      const auto &mask_map = leaf.mask->effects[0].mapping;
      require(leaf.fill.mode == 0 && mask_map.projection.mode == 0 &&
                  leaf.fill.scale == mask_map.projection.scale &&
                  leaf.fill.offset == mask_map.projection.offset &&
                  leaf.fill.rotation == mask_map.projection.rotation && leaf.fill.filtering == 0 &&
                  mask_map.filtering == 0 && mask_map.projection.filtering == 0 &&
                  leaf.fill.wrap == 0 && mask_map.wrap == 0 && !leaf.fill.alpha_mask[2] &&
                  !mask_map.projection.alpha_mask[0],
              "Actual content/mask raw sampler inputs differ");
      RawOracle oracle;
      oracle.curve = source_curve(prepared->prepared, leaf);
      oracle.projection = leaf.mask->effects[0].mapping.projection;
      const auto image = spsm::bitmap::describe_image(
          prepared->prepared.source->source(),
          "/resources/56271f14721261acbab5874fa9f47a97e59ffb39.image");
      oracle.image = spsm::bitmap::decode(spsm::bitmap::load_encoded(image));
      require(image.header->version == "v2" && image.header->pitch == image.header->width * 8,
              "Actual raw-half layout changed");
      auto &set = document.set(document.texture_sets().front().material_session_uid);
      set.layers = {leaf};
      set.layers[0].parent = QUuid();
      set.layers[0].depth = 0;
      set.selected_layer = leaf.id;
      set.precision[2] = ChannelPrecision::L32F;
      descriptor = fill_texture_json(binding);
      effect_json = mask_effects_json(leaf.mask->effects);
      probes = positions(oracle);
      for (auto xy : probes) {
        const auto prefix = oracle.result(xy[0], xy[1], oracle.curve.clamp);
        negative += prefix < 0;
        above_one += prefix > 1;
        raw_low = std::min(raw_low, double(oracle.raw(xy[0], xy[1])));
        raw_high = std::max(raw_high, double(oracle.raw(xy[0], xy[1])));
        source_prefix_low = std::min(source_prefix_low, double(prefix));
        source_prefix_high = std::max(source_prefix_high, double(prefix));
        const auto coverage = std::clamp(prefix, 0.f, 1.f);
        const auto content = std::clamp(oracle.raw(xy[0], xy[1]), 0.f, 1.f);
        const auto expected = content * coverage + set.base_values.roughness * (1 - coverage);
        reference.push_back(expected);
        coverages.push_back(coverage);
        const auto cpu = TextureDocument::composite(set, xy[0], xy[1]);
        cpu_error = std::max(cpu_error, std::abs(double(cpu.material.roughness) - expected));
        require(std::abs(cpu.material.roughness - expected) < 3e-6 &&
                    std::abs(cpu.coverage[2] - coverage) < 3e-6,
                "CPU raw-half mask/Levels/coverage oracle differs");
      }
      std::printf(
          "BTS_BITMAP_MASK_SOURCE_RANGE raw_min=%.9g raw_max=%.9g formula_prefix_min=%.9g formula_prefix_max=%.9g negative=%d above_one=%d\n",
          raw_low,
          raw_high,
          source_prefix_low,
          source_prefix_high,
          negative,
          above_one);
      std::puts("BTS_BITMAP_MASK_RESOURCE_PHASE first");
      publish(true);
      before = host.diagnostic_gpu_pixels(set.material_session_uid, 2, probes);
      prefixes = capture_mask(probes);
      const auto raw_inputs = capture_mask(probes, false);
      // Stage oracles preserve 3e-5 bounds without treating CPU-double sampling
      // and native float32 UV arithmetic as identical. Raw content capture equals
      // this actual mask Fill input: same source/UV/HQ/ignored-A and no content
      // effects; every probe is inside (0,1), so Roughness's clamp is inactive.
      for (std::size_t i = 0; i < probes.size(); ++i) {
        const auto x = probes[i][0], y = probes[i][1];
        const auto raw = oracle.raw(x, y), prefix = oracle.result(x, y, oracle.curve.clamp);
        require(raw > 0 && raw < 1 && raw_inputs[i] > 0 && raw_inputs[i] < 1,
                "Raw-content capture clamp is not inactive");
        const auto independent_curve = oracle.curve_value(raw_inputs[i], oracle.curve.clamp),
                   coverage = std::clamp(independent_curve, 0.f, 1.f);
        raw_gpu_error = std::max(raw_gpu_error, std::abs(double(raw_inputs[i]) - raw));
        prefix_error = std::max(prefix_error, std::abs(double(prefixes[i]) - independent_curve));
        actual_cpu_prefix_delta = std::max(actual_cpu_prefix_delta,
                                           std::abs(double(prefixes[i]) - prefix));
        const auto final = before[i][0] + set.base_values.roughness * (1 - before[i][3]);
        packet_error = std::max({packet_error,
                                 std::abs(double(before[i][0]) - raw_inputs[i] * coverage),
                                 std::abs(double(before[i][3]) - coverage)});
        gpu_error = std::max(gpu_error, std::abs(double(final) - reference[i]));
        std::printf(
            "BTS_BITMAP_MASK_GPU_ORACLE x=%d y=%d raw_cpu=%.9g raw_gpu=%.9g prefix_cpu=%.9g curve_of_gpu_raw=%.9g prefix_gpu=%.9g GPU_R=%.9g GPU_A=%.9g expected_final_cpu=%.9g reconstructed_final=%.9g\n",
            x,
            y,
            raw,
            raw_inputs[i],
            prefix,
            independent_curve,
            prefixes[i],
            before[i][0],
            before[i][3],
            reference[i],
            final);
      }
      std::printf(
          "BTS_BITMAP_MASK_GPU_ERRORS raw_vs_cpu=%.9g curve_of_gpu_raw=%.9g packet=%.9g final_vs_cpu=%.9g direct_prefix_vs_cpu_REPORTED_ONLY=%.9g\n",
          raw_gpu_error,
          prefix_error,
          packet_error,
          gpu_error,
          actual_cpu_prefix_delta);
      require(gpu_error < 3e-5 && prefix_error < 3e-5 && raw_gpu_error < 3e-5 &&
                  packet_error < 3e-5,
              "Native GPU staged raw/curve/packet/final oracle failed");
      if (negative)
        require(*std::min_element(prefixes.begin(), prefixes.end()) < 0,
                "Negative actual source prefix clipped");
      if (above_one)
        require(*std::max_element(prefixes.begin(), prefixes.end()) > 1,
                "Above-one actual source prefix clipped");
      // Independent TEST-ONLY known half-layout witness. It never changes the
      // original source/preparation; restore the exact leaf before packed Save.
      const std::uint16_t halfs[] = {
          0xbc00, 0, 0, 0, 0x3400, 0, 0, 0x4000, 0x3800, 0, 0, 0x4400, 0x4000, 0, 0, 0x3800};
      auto raw = std::make_shared<spsm::ImageBytes>();
      for (auto h : halfs) {
        raw->push_back(h);
        raw->push_back(h >> 8);
      }
      auto header = std::make_shared<spsm::bitmap::Header>();
      header->width = header->height = 2;
      header->pitch = 16;
      auto encoded = std::make_shared<spsm::bitmap::EncodedImage>();
      encoded->header = header;
      auto texture = std::make_shared<FillTexture>();
      texture->asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      texture->hash = QCryptographicHash::hash(
                          QByteArrayView(reinterpret_cast<const char *>(raw->data()), raw->size()),
                          QCryptographicHash::Sha256)
                          .toHex()
                          .toStdString();
      texture->name = "TEST-ONLY signed asymmetric mask";
      texture->raw = texture->floating_point = texture->data_units = texture->range_valid = true;
      texture->minimum = -1;
      texture->maximum = 2;
      texture->width = texture->height = 2;
      texture->pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(
          std::initializer_list<FillTexture::Pixel>{
              {-1, 0, 0, 0}, {.25f, 0, 0, 2}, {.5f, 0, 0, 4}, {2, 0, 0, .5f}});
      texture->validate();
      auto &test = set.layers[0];
      test.fill.scale = {1, 1};
      fill_channel_texture(test.fill, 2) = texture;
      test.mask->effects[0].mapping.projection.scale = {1, 1};
      test.mask->effects[0].mapping.projection.texture = texture;
      auto &curve = test.mask->effects[1].levels;
      curve.input_low = .3f;
      curve.input_high = .8f;
      curve.gamma = 2;
      curve.output_low = 0;
      curve.output_high = 1;
      curve.clamp = false;
      RawOracle synthetic;
      synthetic.image.encoded = encoded;
      synthetic.image.bytes = raw;
      synthetic.projection = test.mask->effects[0].mapping.projection;
      synthetic.curve = {.3f, .8f, 2, 0, 1, false};
      const auto synthetic_positions = positions(synthetic);
      publish(true);
      const auto synthetic_prefixes = capture_mask(synthetic_positions);
      const auto synthetic_pixels = host.diagnostic_gpu_pixels(
          set.material_session_uid, 2, synthetic_positions);
      for (std::size_t i = 0; i < synthetic_positions.size(); ++i) {
        auto p = synthetic_positions[i];
        const auto prefix = synthetic.result(p[0], p[1], false),
                   coverage = std::clamp(prefix, 0.f, 1.f),
                   expected = std::clamp(synthetic.raw(p[0], p[1]), 0.f, 1.f) * coverage +
                              set.base_values.roughness * (1 - coverage);
        synthetic_negative += synthetic_prefixes[i] < 0;
        synthetic_above_one += synthetic_prefixes[i] > 1;
        synthetic_prefix_error = std::max(synthetic_prefix_error,
                                          std::abs(double(synthetic_prefixes[i]) - prefix));
        synthetic_composite_error = std::max(
            {synthetic_composite_error,
             std::abs(double(synthetic_pixels[i][0] +
                             set.base_values.roughness * (1 - synthetic_pixels[i][3])) -
                      expected),
             std::abs(double(synthetic_pixels[i][0]) -
                      std::clamp(synthetic.raw(p[0], p[1]), 0.f, 1.f) * coverage),
             std::abs(double(synthetic_pixels[i][3]) - coverage)});
      }
      require(synthetic_negative > 0 && synthetic_above_one > 0 && synthetic_prefix_error < 3e-5 &&
                  synthetic_composite_error < 3e-5,
              "Synthetic signed/HDR mask prefix/final coverage failed");
      std::printf(
          "BTS_BITMAP_MASK_TEST_ONLY_SYNTHETIC negative=%d above_one=%d prefix_error=%.9g composite_error=%.9g\n",
          synthetic_negative,
          synthetic_above_one,
          synthetic_prefix_error,
          synthetic_composite_error);
      set.layers = {leaf};
      set.layers[0].parent = QUuid();
      set.layers[0].depth = 0;
      publish(true);
      const auto restored = host.diagnostic_gpu_pixels(set.material_session_uid, 2, probes);
      const auto restored_prefixes = capture_mask(probes);
      for (std::size_t i = 0; i < before.size(); ++i) {
        for (int c = 0; c < 4; ++c)
          require(std::abs(restored[i][c] - before[i][c]) < 1e-6,
                  "Restoring original leaf changed GPU pixels");
        require(std::abs(restored_prefixes[i] - prefixes[i]) < 1e-6,
                "Restoring original leaf changed mask prefixes");
      }
      require(mask_effects_json(set.layers[0].mask->effects) == effect_json &&
                  source.hash() == source_hash && ancestor.mask->background == 0,
              "Detached GPU oracle mutated source fields");
      host.save_project(document, project.toStdString(), {source});
    }
    source_bindings.clear();
    document = create();
    publish();
    require(original_preparation.expired(), "Strong preparation survived cold Open");
    require(QFile::rename(input, input + ".held"), "Cannot hide private source");
    require(QDir(directory + "/first-cache").removeRecursively(), "Cannot remove private cache");
    qputenv("BTS_RESOURCE_CACHE", (directory + "/second-cache").toUtf8());
    auto pending = std::async(std::launch::async, [&] {
                     return PendingProject::prepare(project.toStdString());
                   }).get();
    pending->validate();
    require(pending->resources.size() == 1 && pending->resources[0].hash() == source_hash &&
                pending->resources[0].search_terms.contains(
                    "spsm:policy:mask-bitmap=native-ordered-fill-levels-raw-r-ignore-a"),
            "Packed original source/policy changed");
    const auto &reopened = pending->document->texture_sets().front().layers.front();
    require(reopened.mask && mask_effects_json(reopened.mask->effects) == effect_json &&
                fill_texture_json(fill_channel_texture(reopened.fill, 2)) == descriptor,
            "Packed mask effects/descriptor changed");
    const auto &reloaded_worn = named(*pending->resources[0].material_preset, "Wood_Worn_base");
    auto content_binding = fill_channel_texture(reopened.fill, 2),
         mask_binding = reopened.mask->effects[0].mapping.projection.texture,
         worn_binding = fill_channel_texture(reloaded_worn.fill, 2);
    require(content_binding->pixels == mask_binding->pixels &&
                content_binding->pixels == worn_binding->pixels &&
                content_binding->storage_identity() == mask_binding->storage_identity() &&
                content_binding->storage_identity() == worn_binding->storage_identity(),
            "Cold Open lost three binding pixel/storage identity");
    const auto &original_ancestor = named(*pending->resources[0].material_preset,
                                          "Wood_Color_Roughness_base");
    require(original_ancestor.mask && original_ancestor.mask->enabled &&
                original_ancestor.mask->background == 0,
            "Cold Open lost source ancestor0");
    source_bindings = {worn_binding};
    document = host.adopt_project(std::move(pending), project.toStdString());
    std::puts("BTS_BITMAP_MASK_RESOURCE_PHASE reopened");
    publish(true);
    const auto after = host.diagnostic_gpu_pixels(
        document.texture_sets().front().material_session_uid, 2, probes);
    double reopened_error = 0;
    for (std::size_t i = 0; i < before.size(); ++i)
      for (int c = 0; c < 4; ++c)
        reopened_error = std::max(reopened_error, std::abs(double(before[i][c]) - after[i][c]));
    require(reopened_error < 1e-6, "Cold packed Open changed native mask pixels");
    const auto reopened_prefixes = capture_mask(probes);
    double reopened_prefix_error = 0;
    for (std::size_t i = 0; i < prefixes.size(); ++i)
      reopened_prefix_error = std::max(reopened_prefix_error,
                                       std::abs(double(prefixes[i]) - reopened_prefixes[i]));
    require(reopened_prefix_error < 1e-6, "Cold packed Open changed raw mask prefixes");
    QJsonArray points;
    for (std::size_t i = 0; i < probes.size(); ++i)
      points.append(QJsonObject{{"x", probes[i][0]},
                                {"y", probes[i][1]},
                                {"expected_cpu_double", reference[i]},
                                {"coverage_cpu_double", coverages[i]},
                                {"gpu_prefix", prefixes[i]},
                                {"gpu_premultiplied_r", before[i][0]},
                                {"gpu_coverage", before[i][3]}});
    QFile output(directory + "/verified.json");
    require(output.open(QIODevice::WriteOnly), "Cannot write native proof");
    output.write(
        QJsonDocument(
            QJsonObject{{"source_sha256", source_hash},
                        {"source_storage_identity", storage_identity},
                        {"isolated_leaf", "Wood_base_XtraRoughness"},
                        {"original_ancestor_zero_mask_retained", true},
                        {"leaf_mask_fields_preserved", true},
                        {"precision", "L32F"},
                        {"resolution", 128},
                        {"samples", points},
                        {"raw_r_min", raw_low},
                        {"raw_r_max", raw_high},
                        {"source_formula_prefix_min", source_prefix_low},
                        {"source_formula_prefix_max", source_prefix_high},
                        {"negative_prefix_samples", negative},
                        {"above_one_prefix_samples", above_one},
                        {"test_only_synthetic_negative_samples", synthetic_negative},
                        {"test_only_synthetic_above_one_samples", synthetic_above_one},
                        {"test_only_synthetic_prefix_error", synthetic_prefix_error},
                        {"test_only_synthetic_composite_error", synthetic_composite_error},
                        {"original_leaf_restored_before_save", true},
                        {"cpu_raw_formula_error", cpu_error},
                        {"gpu_final_cpu_double_error", gpu_error},
                        {"gpu_curve_captured_raw_error", prefix_error},
                        {"gpu_packet_independent_curve_error", packet_error},
                        {"gpu_raw_sampling_cpu_double_error", raw_gpu_error},
                        {"direct_prefix_cpu_double_delta_REPORTED_ONLY", actual_cpu_prefix_delta},
                        {"source_free_cold_packed_open", true},
                        {"three_bindings_one_pixel_owner", true},
                        {"effects_descriptor_sha_preserved", true},
                        {"reopened_gpu_error", reopened_error},
                        {"reopened_gpu_prefix_error", reopened_prefix_error},
                        {"appearance_verified", false}})
            .toJson());
    std::puts("BTS_SPSM_BITMAP_MASK_GPU PASS");
    return 0;
  }
  catch (const std::exception &error) {
    std::fprintf(stderr, "BTS_SPSM_BITMAP_MASK_GPU_FAILED %s\n", error.what());
    return 1;
  }
}
