// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlenderHost.hh"
#include "BlendAssetBrowser.hh"
#include "BlendLibraryPreferences.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialDependencies.hh"
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialImageUpload.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialPreview.hh"
#include "NativeMaterialPreviewImages.hh"
#include "NativeMaterialImageResource.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "BKE_node.hh"
#include "BKE_main.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include "IMB_colormanagement.hh"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QDateTime>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QScopeGuard>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <cassert>
#include <iostream>
using namespace bts;
namespace {
void require(bool b, const char *why)
{
  if (!b)
    throw std::runtime_error(why);
}
void wait_until(const auto &ready)
{
  QElapsedTimer clock;
  clock.start();
  while (!ready()) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
    require(clock.elapsed() < 120000, "Native preview timeout");
  }
}
auto await(auto future)
{
  wait_until(
      [&] { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
  return future.get();
}
QJsonObject stamp(const QString &file)
{
  QFileInfo f(file);
  return {{"size", f.size()},
          {"modified", f.lastModified().toMSecsSinceEpoch()},
          {"changed", f.metadataChangeTime().toMSecsSinceEpoch()}};
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
  try {
    require(argc == 3, "Expected output and explicit read-only library manifest");
    const QString out = argv[1];
    QDir().mkpath(out);
    QFile manifest(argv[2]);
    require(manifest.open(QIODevice::ReadOnly), "Manifest missing");
    const auto cases = QJsonDocument::fromJson(manifest.readAll()).array();
    require(!cases.isEmpty(), "Explicit source missing");
    const QString source = cases.first().toObject()["file"].toString();
    const auto original_stamp = stamp(source);
    BlenderHost host(QCoreApplication::applicationFilePath().toStdString(), "");
    QOpenGLContext context;
    context.setFormat(format);
    require(context.create(), "GL context failed");
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    require(context.makeCurrent(&surface), "Offscreen activation failed");
    QOpenGLFramebufferObject target(128, 128, QOpenGLFramebufferObject::CombinedDepthStencil);
    struct Close {
      BlenderHost &host;
      ~Close()
      {
        host.shutdown();
      }
    } close{host};
    host.initialize_gpu(context, surface, target.handle());
    const auto selected = host.selected_material();
    const auto resources = host.project_resources().size();
    const auto working = std::string(blender::IMB_colormanagement_working_space_get());
    bool measure_preview = false;
    int pings = 0;
    double maximum_ping_ms = 0;
    QElapsedTimer ping_clock;
    ping_clock.start();
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
      if (measure_preview) {
        maximum_ping_ms = std::max(maximum_ping_ms, ping_clock.nsecsElapsed() / 1e6);
        ++pings;
      }
      ping_clock.restart();
    });
    heartbeat.start();
    QJsonArray report;
    int compared = 0;
    double maximum = 0;
    int display_maximum = 0;
    auto canonical = await(host.import_native_material(
        source, "4k-Cardboard-Damaged", std::make_shared<std::atomic_bool>(false)));
    const auto canonical_id = canonical.material.id;
    auto canonical_graph = canonical.native_graph;
    const auto canonical_pointer = canonical_graph.get();
    const auto canonical_description = native_material_description_json(
        canonical_graph->description);
    const auto canonical_channel_count = canonical_graph->channels.size();
    host.with_gpu([&] {
      NativeMaterialImageUpload upload(canonical_graph);
      while (!upload.step()) {
      }
    });
    host.install_native_material(canonical_id, canonical.material.hash(), canonical_graph);
    TextureSet registry_probe;
    registry_probe.layers.front().kind = LayerKind::Fill;
    registry_probe.layers.front().fill.material_resource =
        canonical_id.toString(QUuid::WithoutBraces).toStdString();
    registry_probe.layers.front().fill.native_material =
        std::make_shared<NativeMaterialDescription>(canonical_graph->description);
    const auto capabilities =
        host.prepare_layer_capture(registry_probe).layers.front().fill.native_material->channels;
    require(capabilities.size() > 1, "Canonical full source fixture lacks channels");
    std::weak_ptr<NativeMaterialGraph> canonical_weak = canonical_graph;
    canonical.native_graph.reset();
    canonical_graph.reset();
    const auto registry_unchanged = [&] {
      auto graph = canonical_weak.lock();
      return graph && graph.get() == canonical_pointer &&
             graph->channels.size() == canonical_channel_count &&
             native_material_description_json(graph->description) == canonical_description &&
             host.has_native_material(canonical_id) &&
             host.prepare_layer_capture(registry_probe)
                     .layers.front()
                     .fill.native_material->channels == capabilities;
    };
    const auto buffers = [&] {
      QCryptographicHash digest(QCryptographicHash::Sha256);
      std::vector<const void *> pointers;
      const auto graph = canonical_weak.lock();
      require(bool(graph), "Canonical source owner expired");
      for (const auto &[image, resource] : graph->image_resources) {
        const auto *buffer = resource->buffer();
        if (buffer->float_data()) {
          pointers.push_back(buffer->float_data());
          digest.addData(QByteArrayView(reinterpret_cast<const char *>(buffer->float_data()),
                                        qsizetype(buffer->x) * buffer->y *
                                            (buffer->channels ? buffer->channels : 4) *
                                            sizeof(float)));
        }
        if (buffer->byte_data()) {
          pointers.push_back(buffer->byte_data());
          digest.addData(QByteArrayView(reinterpret_cast<const char *>(buffer->byte_data()),
                                        qsizetype(buffer->x) * buffer->y * 4));
        }
      }
      return std::pair{pointers, digest.result()};
    };
    const auto source_buffers = buffers();
    require(!source_buffers.first.empty(), "Canonical source image fixture is empty");
    bool registry_preserved = true;
    QTimer registry_timer;
    registry_timer.setInterval(5);
    QObject::connect(&registry_timer, &QTimer::timeout, [&] {
      registry_preserved = registry_preserved && registry_unchanged();
    });
    registry_timer.start();
    for (const QString &name : {QString("Cotton-Clouds-Diffuse.0003"),
                                QString("Cotton-Clouds-Diffuse.0004"),
                                QString("Cotton-Clouds-Diffuse.0005"),
                                QString("Cotton-Clouds-Diffuse.0006"),
                                QString("Cotton-Clouds-Diffuse.0007"),
                                QString("Cotton-Clouds-Diffuse.0008"),
                                QString("Cotton-Clouds-Diffuse.0009"),
                                QString("4k-Cardboard-Damaged")})
    {
      measure_preview = true;
      ping_clock.restart();
      auto preview_future = host.preview_native_material(
          source, name, std::make_shared<std::atomic_bool>(false));
      std::shared_future<PreparedBlendMaterial> concurrent_import;
      if (name == "Cotton-Clouds-Diffuse.0003")
        concurrent_import = host.import_native_material(
            source, "4k-Cardboard-Damaged", std::make_shared<std::atomic_bool>(false));
      const auto result = await(preview_future);
      if (concurrent_import.valid()) {
        const auto imported = await(concurrent_import);
        require(imported.native_graph->channels.size() == canonical_channel_count,
                "Concurrent normal import inherited pruned preview channels");
      }
      app.processEvents();
      measure_preview = false;
      require(!host.native_material_preview_pending(), "Completed preview retained transaction");
      require(result.image.size() == QSize(48, 48), "Wrong packet image size");
      require(result.timings["compiled_channels"].toInt() == 1,
              "Preview compiled unrelated channels");
      result.image.save(out + "/" + name + ".png");
      auto graph = await(
          host.prepare_material_graph(source, name, std::make_shared<std::atomic_bool>(false)));
      await(std::async(std::launch::async, [graph] {
              std::atomic_bool cancel = false;
              std::erase_if(graph->channels, [](const auto &c) { return c.channel != 0; });
              return prepare_native_material_preview_images(*graph, cancel);
            }).share());
      const auto found = std::find_if(graph->channels.begin(),
                                      graph->channels.end(),
                                      [](const auto &c) { return c.channel == 0; });
      require(found != graph->channels.end() && found->tree,
              "Native reference missing Base Color");
      const auto deps = native_material_dependencies(*found->tree,
                                                     graph->description.parameters.size());
      require(deps.uv && !deps.surface && !deps.object && !deps.view && !deps.ao,
              "Actual UV source dependency changed");
      host.with_gpu([&] {
        using namespace blender;
        NativeMaterialImageUpload upload(graph);
        while (!upload.step()) {
        }
        NativeMaterialGpuProgram reference(*found->tree, graph->parameters.size(), 0);
        auto *texture = GPU_texture_create_2d("Independent native preview reference",
                                              48,
                                              48,
                                              1,
                                              gpu::TextureFormat::SFLOAT_32_32_32_32,
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr);
        require(texture, "Reference allocation failed");
        NativeMaterialGpuContext c;
        c.color_to_aces = graph->color_to_aces;
        reference.update_ramps(graph->description);
        reference.dispatch(texture, graph->parameters, c, {0, 0, 48, 48});
        auto *raw = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
        require(raw, "Reference readback failed");
        for (int y = 0; y < 48; ++y)
          for (int x = 0; x < 48; ++x) {
            const auto &actual = result.linear.pixels[y * 48 + x];
            const float *expected = raw + ((47 - y) * 48 + x) * 4;
            float rgb[4]{expected[0], expected[1], expected[2], expected[3]};
            IMB_colormanagement_transform_v4(rgb, "ACEScg", "sRGB");
            for (int ch = 0; ch < 4; ++ch) {
              maximum = std::max(maximum, std::abs(double(actual[ch]) - expected[ch]));
              ++compared;
            }
            const float alpha = std::clamp(expected[3], 0.f, 1.f),
                        checker = ((x / 12 + y / 12) % 2) ? 1.f / 3 : .6f;
            const QColor pixel(result.image.pixel(x, y));
            const int display[3]{pixel.red(), pixel.green(), pixel.blue()};
            for (int ch = 0; ch < 3; ++ch)
              display_maximum = std::max(
                  display_maximum,
                  std::abs(display[ch] - qRound(255 * (std::clamp(rgb[ch], 0.f, 1.f) * alpha +
                                                       checker * (1 - alpha)))));
          }
        MEM_delete(raw);
        GPU_texture_free(texture);
      });
      require(maximum < 2e-6 && display_maximum <= 1,
              "Native preview packet/display oracle differs");
      report.append(QJsonObject{{"material", name},
                                {"uv", true},
                                {"object", false},
                                {"surface", false},
                                {"view", false},
                                {"ao", false},
                                {"packet_components", 48 * 48 * 4},
                                {"source_inputs_preserved", graph->original_inputs_preserved},
                                {"timings", result.timings}});
    }
    // A native image/constant mix has an independent named-space oracle. The
    // process scene-linear role is intentionally different from the graph space;
    // restore it before returning, with no worker alive across either transition.
    QJsonArray color_cases;
    double color_maximum = 0, ordinary_rec709_error = 0;
    int color_checks = 0;
    const std::string previous_space = blender::IMB_colormanagement_working_space_get();
    {
      const auto restore = qScopeGuard([&] {
        blender::IMB_colormanagement_working_space_set_from_name(previous_space.c_str());
      });
      using namespace blender;
      struct ImageCase {
        QString name, path, space;
        int alpha;
        std::array<float, 4> raw;
        bool exr;
      };
      std::vector<ImageCase> images{
          {"straight",
           out + "/straight.png",
           "sRGB",
           IMA_ALPHA_STRAIGHT,
           {195 / 255.f, 87 / 255.f, 31 / 255.f, 128 / 255.f},
           false},
          {"zero-alpha",
           out + "/zero-alpha.png",
           "sRGB",
           IMA_ALPHA_STRAIGHT,
           {195 / 255.f, 87 / 255.f, 31 / 255.f, 0},
           false},
          {"premultiplied",
           out + "/premultiplied.exr",
           QString::fromUtf8(IMB_colormanagement_working_space_get_default()),
           IMA_ALPHA_PREMUL,
           {.7f * .25f, .2f * .25f, 2.1f * .25f, .25f},
           true},
          {"data",
           out + "/data.exr",
           QString::fromUtf8(IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA)),
           IMA_ALPHA_CHANNEL_PACKED,
           {-.3f, .25f, 1.75f, .35f},
           true}};
      {
        auto *one = IMB_allocImBuf(2, 2, ImBufFlags::FloatData);
        const auto release = qScopeGuard([&] { IMB_freeImBuf(one); });
        one->channels = 1;
        one->flags |= ImBufFlags::AlphaChannelPacked;
        const float values[]{-.3f, .25f, 1.75f, .35f};
        std::copy_n(values, 4, one->float_data_for_write());
        float rgba[16]{};
        IMB_colormanagement_imbuf_to_float_texture(rgba, 0, 0, 2, 2, one, true);
        for (int i = 0; i < 4; ++i)
          for (int c = 0; c < 4; ++c)
            require(rgba[i * 4 + c] == values[i],
                    "Native single-channel float expansion changed alpha");
      }
      for (const auto &test : images) {
        if (test.exr) {
          auto *buffer = IMB_allocImBuf(2, 2, ImBufFlags::FloatData);
          const auto release = qScopeGuard([&] { IMB_freeImBuf(buffer); });
          buffer->channels = 4;
          buffer->color_mode = ImColorMode::RGBA;
          buffer->ftype = IMB_FTYPE_OPENEXR;
          buffer->flags |= ImBufFlags::AlphaChannelPacked;
          for (int i = 0; i < 4; ++i)
            std::copy(test.raw.begin(), test.raw.end(), buffer->float_data_for_write() + i * 4);
          require(IMB_save_image(buffer, test.path.toUtf8().constData(), ImBufFlags::FloatData),
                  "Cannot write raw EXR oracle fixture");
        }
        else {
          QImage image(2, 2, QImage::Format_RGBA8888);
          image.fill(QColor::fromRgbF(test.raw[0], test.raw[1], test.raw[2], test.raw[3]));
          require(image.save(test.path), "Cannot write PNG oracle fixture");
        }
      }
      for (const std::string owner_space :
           {std::string(IMB_colormanagement_working_space_get_default()), std::string("ACEScg")})
      {
        IMB_colormanagement_working_space_set_from_name(owner_space.c_str());
        for (const auto &test : images) {
          const auto make_graph = [&] {
            auto graph = await(host.prepare_material_graph(
                source, "Cotton-Clouds-Diffuse.0003", std::make_shared<std::atomic_bool>(false)));
            require(std::string(graph->main->colorspace.scene_linear_name) == "ACEScg",
                    "New native graph must encode canonical AP1");
            auto *tree = bke::node_tree_add_tree(
                graph->main.get(), "Independent image-mix oracle", "ShaderNodeTree");
            tree->flag |= NTREE_IS_GPU_SHADER_INTERNAL;
            auto *texture = bke::node_add_node(nullptr, *tree, UString("ShaderNodeTexImage"));
            texture->id = &BKE_image_load(graph->main.get(), test.path.toUtf8().constData())->id;
            auto *image = reinterpret_cast<Image *>(texture->id);
            STRNCPY(image->colorspace_settings.name, test.space.toUtf8().constData());
            image->alpha_mode = eImageAlphaMode(test.alpha);
            image->flag |= IMA_HIGH_BITDEPTH;
            auto *mix = bke::node_add_node(nullptr, *tree, UString("ShaderNodeMixRGB"));
            mix->custom1 = MA_RAMP_BLEND;
            const auto socket = [](bNode &node, const char *name, bool input) -> bNodeSocket & {
              for (auto &s : input ? node.inputs : node.outputs)
                if (STREQ(s.identifier, name))
                  return s;
              throw std::runtime_error("Image oracle socket missing");
            };
            auto *factor = static_cast<bNodeSocket *>(mix->inputs.first);
            auto *first = factor->next, *second = first->next;
            const float literal[]{.2f, .4f, .8f, 1};
            std::copy_n(
                literal, 4, static_cast<bNodeSocketValueRGBA *>(second->default_value)->value);
            bke::node_add_link(*tree, *texture, socket(*texture, "Alpha", false), *mix, *factor);
            bke::node_add_link(*tree, *texture, socket(*texture, "Color", false), *mix, *first);
            auto *aov = bke::node_add_node(nullptr, *tree, UString("ShaderNodeOutputAOV"));
            STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name, "BTS_Channel");
            bke::node_add_link(
                *tree, *mix, socket(*mix, "Color", false), *aov, socket(*aov, "Color", true));
            BKE_ntree_update_after_single_tree_change(*graph->main, *tree);
            graph->channels = {{0, tree, {}}};
            return graph;
          };
          if (test.name == "straight" && owner_space != "ACEScg") {
            auto baseline = make_graph();
            await(std::async(std::launch::async, [baseline] {
                    std::atomic_bool cancel = false;
                    return prepare_native_material_images(*baseline, cancel);
                  }).share());
            host.with_gpu([&] {
              NativeMaterialImageUpload upload(baseline);
              while (!upload.step()) {
              }
              NativeMaterialGpuProgram program(
                  *baseline->channels[0].tree, baseline->parameters.size(), 0);
              auto *texture = GPU_texture_create_2d(
                  "Existing normal native upload color diagnostic",
                  48,
                  48,
                  1,
                  gpu::TextureFormat::SFLOAT_32_32_32_32,
                  GPU_TEXTURE_USAGE_GENERAL,
                  nullptr);
              NativeMaterialGpuContext context;
              context.color_to_aces = baseline->color_to_aces;
              program.update_ramps(baseline->description);
              program.dispatch(texture, baseline->parameters, context, {0, 0, 48, 48});
              auto *pixels = static_cast<float *>(GPU_texture_read(texture, GPU_DATA_FLOAT, 0));
              float expected[]{test.raw[0], test.raw[1], test.raw[2], test.raw[3]};
              IMB_colormanagement_transform_v4(
                  expected, test.space.toUtf8().constData(), "ACEScg");
              for (int c = 0; c < 3; ++c) {
                expected[c] = (1 - test.raw[3]) * expected[c] +
                              test.raw[3] * std::array{.2f, .4f, .8f}[c];
                ordinary_rec709_error = std::max(ordinary_rec709_error,
                                                 std::abs(double(pixels[c]) - expected[c]));
              }
              MEM_delete(pixels);
              GPU_texture_free(texture);
            });
            std::cout << "ORDINARY_REC709_IMAGE_MIX_ERROR " << ordinary_rec709_error << std::endl;
          }
          auto graph = make_graph();
          const auto decoded = await(std::async(std::launch::async, [graph] {
                                       std::atomic_bool cancel = false;
                                       return prepare_native_material_preview_images(*graph,
                                                                                     cancel);
                                     }).share());
          require(decoded.size() == 1, "Synthetic image was not decoded exactly once");
          NativeMaterialPreview checked(
              host, [graph](const QString &, const QString &, std::shared_ptr<std::atomic_bool>) {
                std::promise<std::shared_ptr<NativeMaterialGraph>> p;
                p.set_value(graph);
                return p.get_future().share();
              });
          const auto actual = await(
              checked.request({}, {}, std::make_shared<std::atomic_bool>(false)));
          float expected[4]{test.raw[0], test.raw[1], test.raw[2], test.raw[3]};
          if (test.alpha == IMA_ALPHA_PREMUL)
            for (int c = 0; c < 3; ++c)
              expected[c] /= expected[3];
          if (test.alpha != IMA_ALPHA_CHANNEL_PACKED)
            IMB_colormanagement_transform_v4(expected, test.space.toUtf8().constData(), "ACEScg");
          for (int c = 0; c < 3; ++c)
            expected[c] = (1 - test.raw[3]) * expected[c] +
                          test.raw[3] * std::array{.2f, .4f, .8f}[c];
          expected[3] = 1;
          double error = 0;
          for (const auto &pixel : actual.linear.pixels)
            for (int c = 0; c < 4; ++c) {
              error = std::max(error, std::abs(double(pixel[c]) - expected[c]));
              ++color_checks;
            }
          color_maximum = std::max(color_maximum, error);
          std::cout << "COLOR_PIXEL actual=" << actual.linear.pixels[0][0] << ","
                    << actual.linear.pixels[0][1] << "," << actual.linear.pixels[0][2] << ","
                    << actual.linear.pixels[0][3] << " expected=" << expected[0] << ","
                    << expected[1] << "," << expected[2] << "," << expected[3] << std::endl;
          std::cout << "COLOR_ORACLE " << test.name.toStdString() << " owner=" << owner_space
                    << " error=" << error << std::endl;
          require(error < 3e-6, "Image/constant mix differs from independent named-space oracle");
          require(owner_space == IMB_colormanagement_working_space_get(),
                  "Preview changed process working space");
          color_cases.append(QJsonObject{{"image", test.name},
                                         {"owner_space", QString::fromStdString(owner_space)},
                                         {"encoded_space", "ACEScg"},
                                         {"decode", decoded},
                                         {"maximum_error", error}});
        }
      }
    }
    // Rejected dependencies remain visible errors, never default geometry or AO.
    int unsupported = 0;
    std::weak_ptr<NativeMaterialGraph> retired;
    for (const auto &attribute :
         {"BTS_Coord_Object", "BTS_Geometry_Position", "BTS_Mesh_AO", "view"})
    {
      // Use the owner API's completed detached worker result, then alter only the
      // private channel adapter; the immutable source tree is never touched.
      auto graph = await(host.prepare_material_graph(
          source, "Cotton-Clouds-Diffuse.0003", std::make_shared<std::atomic_bool>(false)));
      auto *tree = graph->channels[0].tree;
      blender::bNode *aov = nullptr;
      for (auto &n : tree->nodes)
        if (STREQ(n.idname, "ShaderNodeOutputAOV"))
          aov = &n;
      require(aov, "Fixture AOV missing");
      auto *node = blender::bke::node_add_node(
          nullptr,
          *tree,
          blender::UString(std::string_view(attribute) == "view" ? "ShaderNodeLayerWeight" :
                                                                   "ShaderNodeAttribute"));
      if (std::string_view(attribute) != "view")
        blender::STRNCPY(static_cast<blender::NodeShaderAttribute *>(node->storage)->name,
                         attribute);
      auto *to = static_cast<blender::bNodeSocket *>(aov->inputs.first);
      blender::bke::node_remove_socket_links(*tree, *to);
      auto *from = static_cast<blender::bNodeSocket *>(node->outputs.first);
      blender::bke::node_add_link(*tree, *node, *from, *aov, *to);
      blender::BKE_ntree_update_without_main(*tree);
      NativeMaterialPreview checked(
          host, [&](const QString &, const QString &, std::shared_ptr<std::atomic_bool>) {
            std::promise<std::shared_ptr<NativeMaterialGraph>> p;
            p.set_value(graph);
            return p.get_future().share();
          });
      auto future = checked.request({}, {}, std::make_shared<std::atomic_bool>(false));
      bool rejected = false;
      try {
        await(future);
      }
      catch (const std::exception &e) {
        rejected = std::string(e.what()).find("requires object, surface, view or baked AO") !=
                   std::string::npos;
      }
      require(rejected && !checked.pending(), "Unsupported context was silently synthesized");
      ++unsupported;
    }
    // Independent analytic native adapters exercise UV texel centers and signed
    // HDR values. Only the private runtime AOV changes; encoded sources stay intact.
    int analytic_checks = 0;
    for (bool hdr : {false, true}) {
      auto graph = await(host.prepare_material_graph(
          source, "Cotton-Clouds-Diffuse.0003", std::make_shared<std::atomic_bool>(false)));
      auto *tree = graph->channels[0].tree;
      blender::bNode *aov = nullptr;
      for (auto &n : tree->nodes)
        if (STREQ(n.idname, "ShaderNodeOutputAOV"))
          aov = &n;
      require(aov, "Analytic AOV missing");
      auto *to = static_cast<blender::bNodeSocket *>(aov->inputs.first);
      blender::bke::node_remove_socket_links(*tree, *to);
      auto *node = blender::bke::node_add_node(
          nullptr, *tree, blender::UString(hdr ? "ShaderNodeRGB" : "ShaderNodeAttribute"));
      auto *from = static_cast<blender::bNodeSocket *>(node->outputs.first);
      if (hdr) {
        auto *rgba = static_cast<blender::bNodeSocketValueRGBA *>(from->default_value);
        rgba->value[0] = -.25f;
        rgba->value[1] = 1.5f;
        rgba->value[2] = 4.5f;
        rgba->value[3] = 1;
      }
      else {
        blender::STRNCPY(static_cast<blender::NodeShaderAttribute *>(node->storage)->name,
                         "BTS_Coord_UV");
        from = from->next;
      }
      blender::bke::node_add_link(*tree, *node, *from, *aov, *to);
      blender::BKE_ntree_update_without_main(*tree);
      await(std::async(std::launch::async, [graph] {
              std::atomic_bool cancel = false;
              std::erase_if(graph->channels, [](const auto &c) { return c.channel != 0; });
              return prepare_native_material_preview_images(*graph, cancel);
            }).share());
      NativeMaterialPreview checked(
          host, [&](const QString &, const QString &, std::shared_ptr<std::atomic_bool>) {
            std::promise<std::shared_ptr<NativeMaterialGraph>> p;
            p.set_value(graph);
            return p.get_future().share();
          });
      const auto actual = await(
          checked.request({}, {}, std::make_shared<std::atomic_bool>(false)));
      for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 48; ++x) {
          const std::array<float, 4> expected =
              hdr ? std::array<float, 4>{-.25f, 1.5f, 4.5f, 1} :
                    std::array<float, 4>{float(x + .5f) / 48, float(47 - y + .5f) / 48, 0, 1};
          const auto &pixel = actual.linear.pixels[y * 48 + x];
          for (int c = 0; c < 4; ++c) {
            require(std::abs(pixel[c] - expected[c]) < 2e-6,
                    "Analytic native UV/HDR output differs");
            ++analytic_checks;
          }
        }
    }
    auto reimport = await(host.import_native_material(
        source, "4k-Cardboard-Damaged", std::make_shared<std::atomic_bool>(false)));
    require(reimport.native_graph && reimport.native_graph.get() != canonical_pointer &&
                reimport.native_graph->channels.size() == canonical_channel_count,
            "Reimport inherited a pruned preview graph");
    registry_timer.stop();
    require(source_buffers == buffers(),
            "Preview modified shared canonical source buffer identity or bytes");
    require(registry_preserved && registry_unchanged(),
            "Preview altered canonical full-source owner/registry/capabilities");
    // A request can be canceled without waiting on the UI; latest request wins.
    auto canceled = host.preview_native_material(
        source, "Cotton-Clouds-Diffuse.0003", std::make_shared<std::atomic_bool>(false));
    host.cancel_native_material_preview();
    bool cancel_rejected = false;
    try {
      await(canceled);
    }
    catch (...) {
      cancel_rejected = true;
    }
    require(cancel_rejected, "Canceled request published");
    wait_until([&] { return !host.native_material_preview_pending(); });
    require(host.selected_material() == selected && host.project_resources().size() == resources,
            "Preview mutated document selection/resources");
    require(working == blender::IMB_colormanagement_working_space_get(),
            "Preview mutated global working space");
    require(original_stamp == stamp(source), "Original library changed");
    require(pings > 10, "Worker did not yield event loop");
    // Keep a fully prepared detached worker behind a test barrier so rowchange
    // and Close occur while its async future is genuinely pending. The worker
    // deliberately finishes after cancellation, exercising owner retirement.
    const auto space_graph = await(host.prepare_material_graph(
        source, "Cotton-Clouds-Diffuse.0003", std::make_shared<std::atomic_bool>(false)));
    const auto colorspace = space_graph->main->colorspace;
    bool delayed_mode = false;
    std::atomic_bool worker_ready = false, release_worker = false;
    std::weak_ptr<NativeMaterialGraph> retired_graph;
    NativeMaterialPreview delayed(
        host, [&](const QString &file, const QString &name, std::shared_ptr<std::atomic_bool>) {
          return std::async(
                     std::launch::async,
                     [&, file, name] {
                       std::unique_ptr<blender::Main, decltype(&blender::BKE_main_free)> reference(
                           blender::BKE_main_new(), blender::BKE_main_free);
                       reference->colorspace = colorspace;
                       std::atomic_bool keep_preparing = false;
                       auto g = prepare_native_material_graph(
                           *reference, file, name, keep_preparing);
                       retired_graph = g;
                       worker_ready.store(true);
                       while (!release_worker.load())
                         QThread::msleep(1);
                       return g;
                     })
              .share();
        });
    const auto release_on_exit = qScopeGuard([&] { release_worker.store(true); });
    double maximum_cancel_ui_ms = 0;
    // Exercise the actual browser using private QSettings and owner-GPU hooks.
    save_material_library_paths({source});
    QWidget owner;
    int prepares = 0, previews = 0, installs = 0;
    BlendAssetBrowserHooks hooks;
    hooks.native_shader = true;
    hooks.scan = [&](const QString &, std::shared_ptr<std::stop_source> stop, auto progress) {
      return host.inspect_asset_library(source, stop, progress);
    };
    hooks.prepare =
        [&](const QString &, const QString &, std::shared_ptr<std::atomic_bool>, bool) {
          ++prepares;
          throw std::runtime_error("Unexpected import");
          return std::shared_future<PreparedBlendMaterial>{};
        };
    hooks.install = [&](auto) { ++installs; };
    hooks.base_color_preview =
        [&](const QString &file, const QString &name, std::shared_ptr<std::atomic_bool> cancel) {
          ++previews;
          return delayed_mode ? delayed.request(file, name, cancel) :
                                host.preview_native_material(file, name, cancel);
        };
    QTimer chooser;
    QObject::connect(&chooser, &QTimer::timeout, [&] {
      for (auto *w : app.topLevelWidgets())
        if (auto *d = qobject_cast<QInputDialog *>(w); d && d->isVisible()) {
          d->accept();
          chooser.stop();
        }
    });
    chooser.start(5);
    browse_blend_asset_library(&owner, std::move(hooks));
    auto *browser = owner.findChild<QDialog *>("blendAssetLibraryBrowser");
    require(browser, "Browser missing");
    wait_until([&] { return browser->property("rowsReady").toBool(); });
    browser->findChild<QCheckBox *>("blendLibraryAssetsOnly")->setChecked(false);
    wait_until([&] { return browser->property("rowsReady").toBool(); });
    auto *list = browser->findChild<QListWidget *>("blendLibraryMaterials");
    int cotton = -1;
    for (int row = 0; row < list->count(); ++row)
      if (list->item(row)->text().startsWith("Cotton-Clouds-Diffuse.0003\n"))
        cotton = row;
    require(cotton >= 0, "Actual missing-preview row missing");
    list->setCurrentRow(cotton);
    QElapsedTimer browse_time;
    browse_time.start();
    while (browse_time.elapsed() < 150) {
      app.processEvents();
      QThread::msleep(1);
    }
    require(prepares == 0 && previews == 0 && installs == 0, "Browsing prepared a source");
    auto *button = browser->findChild<QPushButton *>("blendLibraryBaseColorPreview");
    require(button, "Explicit preview button missing");
    button->click();
    wait_until([&] { return browser->property("generatedNativeBaseColorPreviews").toInt() == 1; });
    require(list->item(cotton)->toolTip().contains("not a shaded material render"),
            "Generated preview is mislabeled");
    browser->grab().save(out + "/native-base-color-browser.png");
    delayed_mode = true;
    button->click();
    wait_until([&] { return worker_ready.load(); });
    QElapsedTimer cancel_time;
    cancel_time.start();
    list->setCurrentRow((cotton + 1) % list->count());
    maximum_cancel_ui_ms = cancel_time.nsecsElapsed() / 1e6;
    require(maximum_cancel_ui_ms < 20, "Row change waited on a worker");
    release_worker.store(true);
    wait_until([&] { return !delayed.pending(); });
    require(retired_graph.expired(), "Stale detached Main was retained");
    require(browser->property("generatedNativeBaseColorPreviews").toInt() == 1,
            "Stale row published a preview");
    list->setCurrentRow(cotton);
    worker_ready.store(false);
    release_worker.store(false);
    button->click();
    wait_until([&] { return worker_ready.load(); });
    cancel_time.restart();
    browser->reject();
    maximum_cancel_ui_ms = std::max(maximum_cancel_ui_ms, cancel_time.nsecsElapsed() / 1e6);
    require(maximum_cancel_ui_ms < 20, "Close waited on a worker");
    release_worker.store(true);
    wait_until([&] { return !delayed.pending(); });
    require(retired_graph.expired(), "Closed browser retained a detached Main");
    require(prepares == 0 && installs == 0 && previews == 3,
            "Preview published to Shelf or prepared implicitly");
    QFile result(out + "/verified.json");
    require(result.open(QIODevice::WriteOnly), "Cannot write proof");
    result.write(
        QJsonDocument(
            QJsonObject{
                {"canonical_source_image_buffers_identity_and_bytes_preserved", true},
                {"normal_import_before_during_after_preview", true},
                {"canonical_source_registry_owner_and_channels_preserved", registry_preserved},
                {"ordinary_rec709_image_mix_error", ordinary_rec709_error},
                {"color_cases", color_cases},
                {"single_channel_float_expansion",
                 "Y,Y,Y,Y (including alpha), 16 native component checks"},
                {"color_component_checks", color_checks},
                {"color_maximum_error", color_maximum},
                {"cases", report},
                {"component_checks", compared},
                {"analytic_component_checks", analytic_checks},
                {"maximum_error", maximum},
                {"display_maximum_byte_error", display_maximum},
                {"unsupported_contexts", unsupported},
                {"event_pings", pings},
                {"maximum_10ms_ping_gap_ms", maximum_ping_ms},
                {"browse_prepare_calls", prepares},
                {"explicit_preview_requests", previews},
                {"shelf_installs", installs},
                {"cancel_stale_close", true},
                {"genuinely_pending_worker_rowchange_close", true},
                {"retired_detached_graphs_expired", true},
                {"maximum_cancel_ui_ms", maximum_cancel_ui_ms},
                {"source_unchanged", true},
                {"live_binding_mutation", false},
                {"cpu_native_composition", false}})
            .toJson());
    std::cout << "PASS native UV Base Color preview\n";
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << "NATIVE_PREVIEW_FAIL " << e.what() << "\n";
    return 1;
  }
}
