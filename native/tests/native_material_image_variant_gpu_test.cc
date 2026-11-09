#define BTS_IMAGE_VARIANT_NO_MAIN
#include "native_material_image_variant_test.cc"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialImageUpload.hh"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include "BLI_math_half.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"

namespace {
using Pixel = std::array<float, 4>;
struct VariantGpuFixture {
  std::vector<std::shared_ptr<NativeMaterialGraph>> graphs;
  QStringList selected_inputs;
  QJsonArray cases;
  QString source_sha, image_sha, replacement_sha;
};
const Pixel hdr_pixel{70000.25f, -12345.125f, .31250003f, 1};
void hdr_exr(const QString &path)
{
  auto *buffer = IMB_allocImBuf(3, 2, ImBufFlags::FloatData);
  require(buffer, "Cannot allocate private HDR fixture");
  const auto release = qScopeGuard([&] { IMB_freeImBuf(buffer); });
  buffer->channels = 4;
  buffer->color_mode = ImColorMode::RGBA;
  buffer->ftype = IMB_FTYPE_OPENEXR;
  buffer->flags |= ImBufFlags::AlphaChannelPacked;
  for (int p = 0; p < 6; ++p)
    std::copy(hdr_pixel.begin(), hdr_pixel.end(), buffer->float_data_for_write() + 4 * p);
  require(IMB_save_image(buffer, path.toUtf8().constData(), ImBufFlags::FloatData),
          "Cannot write FLOAT32 EXR");
}
VariantGpuFixture gpu_fixture(const QString &out)
{
  QDir().mkpath(out);
  const auto image = out + "/source.png", replacement_file = out + "/replacement.png",
             library = out + "/source.blend";
  QImage pixels(3, 2, QImage::Format_RGBA8888);
  pixels.fill(QColor(224, 32, 96));
  require(pixels.save(image), "Cannot write own source pixels");
  pixels.fill(QColor(16, 192, 48));
  require(pixels.save(replacement_file), "Cannot write replacement pixels");
  fixture(library, image);
  std::atomic_bool cancel = false;
  auto source = read_native_material_source(library, "Replacement-source", cancel);
  auto graph = finish_native_material_source(std::move(source), cancel);
  auto archive = archive_native_material(*graph, library, cancel, true);
  prepare_native_material_images(*graph, cancel);
  auto original = native_material_asset("Replacement-source", archive.bytes, archive.description);
  auto bindings = inventory(original, archive.textures);
  auto chosen = *std::find_if(bindings.bindings.begin(),
                              bindings.bindings.end(),
                              [](const auto &b) { return b.groups.size() == 2; });
  StudioAsset replacement;
  replacement.name = "Replacement";
  replacement.filename = "replacement.png";
  replacement.usages = {"texture"};
  replacement.bytes = load(replacement_file);
  auto first = prepare_native_image_variant(original,
                                            archive.textures,
                                            graph->description,
                                            chosen,
                                            replacement,
                                            {NativeImageSourceSpace::Named, "sRGB"},
                                            cancel);
  auto raw = prepare_native_image_variant(original,
                                          archive.textures,
                                          graph->description,
                                          chosen,
                                          replacement,
                                          {NativeImageSourceSpace::Named, "Non-Color"},
                                          cancel);
  require(first.prepared.asset.hash() != raw.prepared.asset.hash(),
          "Explicit source color policies share native source identity");
  std::set<const NativeMaterialImageResource *> shared;
  for (const auto &g : {graph, first.prepared.graph, raw.prepared.graph})
    for (const auto &[id, resource] : g->image_resources)
      shared.insert(resource.get());
  require(shared.size() == 3, "CPU image cache failed shared-source/policy separation");
  auto cold = first.prepared.textures;
  for (auto &a : cold)
    if (!a.texture_files.empty())
      a = packed(a);
  cold.push_back(original);
  const auto source_sha = sha(load(library)), image_sha = sha(load(image)),
             replacement_sha = sha(replacement.bytes);
  require(QFile::rename(image, image + ".offline"), "Cannot hide own source");
  const auto restore = qScopeGuard([&] { QFile::rename(image + ".offline", image); });
  qputenv("BTS_RESOURCE_CACHE", (out + "/cold-cache").toUtf8());
  std::unique_ptr<Main, decltype(&BKE_main_free)> reference(BKE_main_new(), BKE_main_free);
  IMB_colormanagement_working_space_init_default(reference.get());
  auto restored = hydrate_native_material_resource(*reference, first.prepared.asset, cancel, cold);
  require(source_sha == sha(load(library)) && image_sha == sha(load(image + ".offline")) &&
              replacement_sha == sha(replacement.bytes),
          "GPU preparation mutated encoded originals");
  // A separate byte source deliberately requests half precision. Its replacement
  // is signed FLOAT32 Data outside half's finite range and includes one FP32 ULP.
  const auto half_library = out + "/half-source.blend", half_image = out + "/half-original.png";
  QImage half_pixels(3, 2, QImage::Format_RGBA8888);
  half_pixels.fill(QColor(224, 32, 96));
  require(half_pixels.save(half_image), "Cannot write private half-source setup");
  fixture(half_library, half_image, false);
  auto half_source = read_native_material_source(half_library, "Replacement-source", cancel);
  auto half_graph = finish_native_material_source(std::move(half_source), cancel);
  auto half_archive = archive_native_material(*half_graph, half_library, cancel, true);
  auto half_asset = native_material_asset(
      "Replacement-source", half_archive.bytes, half_archive.description);
  const auto half_bindings = inventory(half_asset, half_archive.textures);
  const auto half_binding = *std::find_if(half_bindings.bindings.begin(),
                                          half_bindings.bindings.end(),
                                          [](const auto &b) { return b.groups.size() == 2; });
  require(!half_binding.source_settings["high_precision"].toBool(), "Half original setup failed");
  const auto hdr_path = out + "/signed-float32.exr";
  hdr_exr(hdr_path);
  StudioAsset hdr;
  hdr.name = "HDR replacement";
  hdr.filename = "signed-float32.exr";
  hdr.usages = {"texture"};
  hdr.bytes = load(hdr_path);
  const auto hdr_sha = sha(hdr.bytes);
  auto high = prepare_native_image_variant(half_asset,
                                           half_archive.textures,
                                           half_graph->description,
                                           half_binding,
                                           hdr,
                                           {NativeImageSourceSpace::Named, "Non-Color"},
                                           cancel);
  require(high.prepared.diagnostics["precision_policy"] == "native-float32-replacement" &&
              !high.prepared.diagnostics["original_high_precision"].toBool(),
          "Float32 replacement precision contract absent");
  require(sha(hdr.bytes) == hdr_sha, "Encoded FLOAT32 replacement changed");
  auto high_cold = high.prepared.textures;
  for (auto &a : high_cold)
    if (!a.texture_files.empty())
      a = packed(a);
  high_cold.push_back(half_asset);
  require(QFile::rename(half_image, half_image + ".offline"), "Cannot hide private half original");
  const auto half_restore = qScopeGuard(
      [&] { QFile::rename(half_image + ".offline", half_image); });
  qputenv("BTS_RESOURCE_CACHE", (out + "/hdr-cold-cache").toUtf8());
  auto high_open = hydrate_native_material_resource(
      *reference, high.prepared.asset, cancel, high_cold);
  return {
      {graph, first.prepared.graph, raw.prepared.graph, restored, high.prepared.graph, high_open},
      {chosen.input,
       chosen.input,
       chosen.input,
       chosen.input,
       half_binding.input,
       half_binding.input},
      {},
      source_sha,
      image_sha,
      replacement_sha};
}
Pixel oracle(const NativeMaterialGraph &graph,
             const QString &input,
             bool variant,
             bool data,
             bool hdr = false)
{
  Pixel original{224 / 255.f, 32 / 255.f, 96 / 255.f, 1},
      replacement{16 / 255.f, 192 / 255.f, 48 / 255.f, 1};
  const auto *target = graph.main->colorspace.scene_linear_name;
  IMB_colormanagement_transform_v4(original.data(), "sRGB", target);
  if (hdr) {
    for (int c = 0; c < 3; ++c)
      original[c] = math::half_to_float(math::float_to_half(original[c]));
    replacement = hdr_pixel;
  }
  if (!data)
    IMB_colormanagement_transform_v4(replacement.data(), "sRGB", target);
  float weight = 0;
  if (variant) {
    if (input == "Color1")
      weight = .5f * .75f;
    else if (input == "Color2")
      weight = .5f * .25f;
    else
      throw std::runtime_error("Synthetic Mix native socket identity changed");
  }
  for (int c = 0; c < 3; ++c)
    original[c] = original[c] * (1 - weight) + replacement[c] * weight;
  IMB_colormanagement_transform_v4(original.data(), target, "ACEScg");
  return original;
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
  QApplication app(argc, argv);
  std::setlocale(LC_NUMERIC, "C");
  std::cout << std::unitbuf;
  try {
    require(argc == 2, "Expected private output directory");
    BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "Cannot create offscreen context");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(surface.isValid() && context.makeCurrent(&surface),
            "Cannot activate offscreen surface");
    QOpenGLFramebufferObject fbo(64, 64, QOpenGLFramebufferObject::CombinedDepthStencil);
    require(fbo.isValid(), "Cannot create offscreen framebuffer");
    const auto close = qScopeGuard([&] { host.shutdown(); });
    host.initialize_gpu(context, surface, fbo.handle());
    require(QString::fromStdString(host.renderer()).contains("NVIDIA", Qt::CaseInsensitive),
            "Requires NVIDIA native GPU");
    const auto out = QString::fromLocal8Bit(argv[1]);
    auto prepared = std::async(std::launch::async, [&] { return gpu_fixture(out); }).get();
    const auto cleanup = qScopeGuard([&] { host.with_gpu([&] { prepared.graphs.clear(); }); });
    std::uint64_t components = 0;
    double maximum = 0, hdr_error = 0;
    QJsonArray cases;
    const QStringList names{"original",
                            "selected-nested-edge",
                            "explicit-data-source-policy",
                            "source-free-archive-open",
                            "half-original-to-signed-HDR-float32",
                            "cold-open-preserves-float32-replacement"};
    for (size_t i = 0; i < prepared.graphs.size(); ++i) {
      const auto graph = prepared.graphs[i];
      const auto expected = oracle(
          *graph, prepared.selected_inputs[i], i != 0, i == 2 || i >= 4, i >= 4);
      host.with_gpu([&] {
        NativeMaterialImageUpload upload(graph);
        while (!upload.step(256)) {
        }
        if (i >= 4) {
          bool high = false, half = false;
          for (const auto &[image, resource] : graph->image_resources) {
            auto *texture = image->runtime->gputexture[TEXTARGET_2D][0];
            require(texture, "Missing actual source allocation");
            if (image->flag & IMA_HIGH_BITDEPTH) {
              high = true;
              require(GPU_texture_format(texture) == gpu::TextureFormat::SFLOAT_32_32_32_32,
                      "HDR replacement inherited half GPU allocation");
              auto *raw = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
              require(raw, "Cannot read actual replacement texture");
              const auto free = qScopeGuard([&] { MEM_delete(raw); });
              for (int p = 0; p < 6; ++p)
                for (int c = 0; c < 4; ++c) {
                  require(raw[4 * p + c] == hdr_pixel[c],
                          "F32 replacement raw texel lost signed range/precision");
                  ++components;
                }
            }
            else {
              half = true;
              require(GPU_texture_format(texture) == gpu::TextureFormat::SFLOAT_16_16_16_16,
                      "Original half setting changed");
            }
          }
          require(
              high && half,
              "Expected independent preserved-half/original and Float32/replacement allocations");
        }
        const auto channel = std::find_if(graph->channels.begin(),
                                          graph->channels.end(),
                                          [](const auto &c) { return c.channel == 0; });
        require(channel != graph->channels.end() && channel->tree,
                "Native Color channel unavailable");
        NativeMaterialGpuProgram program(*channel->tree, graph->parameters.size(), 0);
        auto *target = GPU_texture_create_2d("Selected image edge oracle",
                                             9,
                                             7,
                                             1,
                                             gpu::TextureFormat::SFLOAT_32_32_32_32,
                                             GPU_TEXTURE_USAGE_GENERAL,
                                             nullptr);
        require(target, "Cannot allocate reference target");
        const auto free = qScopeGuard([&] { GPU_texture_free(target); });
        NativeMaterialGpuContext ctx;
        ctx.color_to_aces = graph->color_to_aces;
        program.dispatch(target, graph->parameters, ctx, {0, 0, 9, 7});
        GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
        auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
        require(pixels, "No genuine native GPU readback");
        const auto release = qScopeGuard([&] { MEM_delete(pixels); });
        double error = 0;
        for (int p = 0; p < 9 * 7; ++p)
          for (int c = 0; c < 4; ++c) {
            require(std::isfinite(pixels[p * 4 + c]), "Nonfinite native sample");
            error = std::max(error, std::abs(double(pixels[p * 4 + c]) - expected[c]));
            ++components;
          }
        if (i >= 4) {
          hdr_error = std::max(hdr_error, error);
          double scale = 1;
          for (float value : expected)
            scale = std::max(scale, std::abs(double(value)));
          require(error <= 3e-6 * scale,
                  "HDR selected edge differs from scale-aware Float32 oracle");
        }
        else {
          maximum = std::max(maximum, error);
          require(error < 3e-5,
                  "Selected edge GPU differs from independent asymmetric OCIO oracle");
        }
        cases.append(QJsonObject{
            {"case", names[i]},
            {"maximum_error", error},
            {"recorded_space", QString::fromUtf8(graph->main->colorspace.scene_linear_name)}});
      });
    }
    QFile proof(out + "/gpu.json");
    require(proof.open(QIODevice::WriteOnly), "Cannot write GPU evidence");
    proof.write(QJsonDocument(QJsonObject{{"cases", cases},
                                          {"components", qint64(components)},
                                          {"maximum_error", maximum},
                                          {"tolerance", 3e-5},
                                          {"genuine_gpu_readback", true},
                                          {"hdr_maximum_error", hdr_error},
                                          {"hdr_relative_tolerance", 3e-6},
                                          {"exact_float32_raw_texture", true},
                                          {"original_half_allocation_preserved", true},
                                          {"same_encoded_source_policy_isolation", true},
                                          {"standard_shared_cpu_resources", 3},
                                          {"original_blend_sha256", prepared.source_sha},
                                          {"original_image_sha256", prepared.image_sha},
                                          {"replacement_image_sha256", prepared.replacement_sha}})
                    .toJson());
    std::cout << "NATIVE_IMAGE_VARIANT_GPU_PASS components=" << components << " max=" << maximum
              << '\n';
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "NATIVE_IMAGE_VARIANT_GPU_FAIL " << e.what() << '\n';
    return 1;
  }
}
