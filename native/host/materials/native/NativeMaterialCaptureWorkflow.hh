#pragma once
#include "NativeMaterialMultiObjectDemo.hh"
#include "FlattenLayerDialog.hh"
#include "FlattenExport.hh"
#include "TextureExport.hh"
#include "TextureExportDialog.hh"
#include "GpuTextureExportSource.hh"
#include <QLabel>
#include <QListWidget>
#include "NativeMaterialArchive.hh"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
namespace bts {
namespace native_capture_demo {
using Pixel = std::array<float, 4>;
struct Probe {
  BlenderHost &host;
  const std::function<void()> &frame;
  QString output = qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST"),
          library = output + "/native-capture-fixture.blend", failure;
  AssetCatalog catalog{output + "/capture-catalog"};
  std::unique_ptr<TextureDocument> document;
  std::vector<StudioAsset> sources;
  std::vector<QUuid> native;
  NativeMaterialSourceLoader loader{host, nullptr, [] {}, [](const QString &) {}};
  std::array<float, 3> constant_color{};
  std::map<int, std::vector<std::array<int, 2>>> positions;
  std::array<std::uint64_t, 2> owner_samples{};
  std::uint64_t packets = 0, texels = 0, comparisons = 0;
  double error = 0;
  bool saw_tail = false, saw_mask = false, saw_internal_normal = false;
  QStringList exported;
  Probe(BlenderHost &h, const std::function<void()> &f) : host(h), frame(f) {}
  static void require(bool value, const char *message)
  {
    native_object_demo::require(value, message);
  }
  TextureSet &set()
  {
    return document->set(document->texture_sets().front().material_session_uid);
  }
  void step()
  {
    frame();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  }
  void drain()
  {
    QElapsedTimer clock;
    clock.start();
    do {
      step();
      if (clock.elapsed() >= 120000)
        throw std::runtime_error(QString("Native capture work did not drain (loader=%1 GPU=%2)")
                                     .arg(loader.pending())
                                     .arg(host.texture_updates_pending())
                                     .toStdString());
    } while (loader.pending() || host.texture_updates_pending());
    require(failure.isEmpty(), qPrintable(failure));
  }
  void apply()
  {
    host.set_selected_material(set().material_session_uid);
    host.apply_texture_set(set(), set().texture_resolution);
    drain();
  }
  void check_close(float a, float b, const char *message)
  {
    const auto delta = std::abs(double(a) - b);
    error = std::max(error, delta);
    ++comparisons;
    if (!std::isfinite(a) || delta > 3e-5)
      throw std::runtime_error(
          (QString("%1 actual=%2 expected=%3").arg(message).arg(a, 0, 'g', 9).arg(b, 0, 'g', 9))
              .toStdString());
  }
  void setup()
  {
    std::async(std::launch::async, [&] { native_object_demo::fixture(library, true); }).get();
    std::vector<PreparedBlendMaterial> prepared;
    for (const auto *name : {"AW_Generated", "AW_BumpUnavailable"})
      prepared.push_back(
          host.import_native_material(library, name, std::make_shared<std::atomic_bool>(false))
              .get());
    auto legacy =
        std::async(std::launch::async, [&] {
          std::atomic_bool cancel = false;
          auto graph = restore_native_material_graph(library, "AW_BumpUnavailable", cancel);
          auto archive = archive_native_material(*graph, library, cancel, true);
          auto asset = native_material_asset(
              "Legacy default-space", std::move(archive.bytes), archive.description);
          for (const auto &channel : graph->channels)
            if (channel.tree)
              asset.search_terms.append("native-channel:" + QString::number(channel.channel));
          return PreparedBlendMaterial{
              std::move(asset), std::move(archive.textures), false, std::move(graph)};
        }).get();
    NewProjectOptions options;
    options.resolution = DocumentResolution::Pixels128;
    options.uv_workflow = UvWorkflow::PreserveTiles;
    options.blend.objects = {"AW_Object_A", "AW_Object_B"};
    const auto imported = host.new_project(
        library.toStdString(), qEnvironmentVariable("BTS_ENVIRONMENT").toStdString(), &options);
    const auto scene = host.scene_snapshot();
    require(scene.mesh_objects == 2 && scene.materials.size() == 1,
            "Native capture lost its two source owners");
    for (auto &source : prepared) {
      const auto again = host.import_native_material(
                                 library,
                                 source.material.fill_preset->native_material->material,
                                 std::make_shared<std::atomic_bool>(false))
                             .get();
      require(native_material_description_json(*source.material.fill_preset->native_material) ==
                  native_material_description_json(*again.material.fill_preset->native_material),
              "Native interface depends on importing before or after New");
      require(std::string_view(source.native_graph->main->colorspace.scene_linear_name) ==
                  "ACEScg",
              "Native source is not canonical ACEScg");
    }
    auto hydrated = host.prepare_material_resource(legacy.material,
                                                   std::make_shared<std::atomic_bool>(false),
                                                   legacy.textures)
                        .get();
    require(native_material_description_json(legacy.native_graph->description) ==
                native_material_description_json(hydrated->description),
            "Legacy archive source interface changed during hydration");
    require(std::string_view(hydrated->main->colorspace.scene_linear_name) ==
                blender::IMB_colormanagement_working_space_get_default(),
            "Legacy archive working space was converted");
    const auto base = std::find_if(hydrated->description.parameters.begin(),
                                   hydrated->description.parameters.end(),
                                   [](const auto &p) { return p.name == "Base Color"; });
    require(base != hydrated->description.parameters.end() && base->value[0] == -1 &&
                base->value[1] == double(.1f) && base->value[2] == 2,
            "Legacy archive lost its non-gray signed/HDR defaults");
    legacy.native_graph = std::move(hydrated);
    prepared.push_back(std::move(legacy));
    float expected_constant[4]{-1, .1f, 2, 1};
    blender::IMB_colormanagement_transform_v4(
        expected_constant, blender::IMB_colormanagement_working_space_get_default(), "ACEScg");
    std::copy_n(expected_constant, 3, constant_color.begin());
    require(constant_color[0] < 0 && constant_color[2] > 1,
            "Native color fixture lost its signed/HDR range");
    document = std::make_unique<TextureDocument>(options, imported, scene);
    TextureDocument::set_dimensions(set(), 512, 128, false);
    set().tile_resolutions[1002] = 256;
    set().precision[0] = ChannelPrecision::RGB32F;
    set().precision[3] = ChannelPrecision::RGB32F;
    set().precision[4] = ChannelPrecision::L32F;
    // Existing projects may retain authored Normal internally; it stays hidden.
    set().channels[3] = true;
    set().base_values.base_color = {.05f, .1f, .15f, 1};
    for (auto &source : prepared) {
      catalog.add(source.material);
      const auto id = catalog.retain_project(source.material);
      sources.push_back(*std::find_if(catalog.assets().begin(),
                                      catalog.assets().end(),
                                      [&](const auto &asset) { return asset.id == id; }));
      loader.prepared(source, [](const auto &) {}, [&](const QString &value) { failure = value; });
      drain();
      host.alias_native_material(source.material.id, id);
      native.push_back(apply_native_material_asset(set(), sources.back()));
    }
    TextureDocument::layer(set(), native[1]).opacity[0] = .25f;
    TextureDocument::layer(set(), native[2]).visible = false;
    apply();
    for (int tile : set().tiles) {
      const auto width = set().width(tile), height = set().rows(tile);
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
          const float u = (x + .5f) / width, v = (y + .5f) / height;
          const int owner = u > .5f;
          const float left = owner ? .55f : .05f;
          if (u < left + .02f || u > left + .38f || v < .12f || v > .88f)
            continue;
          positions[tile].push_back({x, y});
          ++owner_samples[owner];
        }
    }
  }
  void record(const LayerCaptureRequest &request)
  {
    require(request.width > 0 && request.height > 0 && request.width <= 256 &&
                request.height <= 256,
            "Native capture exceeded its bounded packet");
    ++packets;
    texels += std::uint64_t(request.width) * request.height;
    saw_tail |= request.x > 0 && (request.width < 256 || request.height < 256);
    saw_mask |= request.mask;
    saw_internal_normal |= !request.mask && request.channel == 3;
  }
  template<class Action> auto dialog(Action action)
  {
    std::exception_ptr failed;
    QElapsedTimer deadline;
    deadline.start();
    QTimer render;
    render.setInterval(2);
    QObject::connect(&render, &QTimer::timeout, [&] {
      try {
        require(deadline.elapsed() < 120000, "Native capture dialog exceeded its deadline");
        frame();
      }
      catch (...) {
        failed = std::current_exception();
        if (auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
          modal->reject();
      }
    });
    render.start();
    auto result = action();
    render.stop();
    if (failed)
      std::rethrow_exception(failed);
    return result;
  }
  std::optional<PreparedFlatten> bake(const TextureSet &source,
                                      const QUuid &id,
                                      bool cancel = false)
  {
    bool requested = false;
    auto result = dialog([&] {
      return choose_layer_flatten(
          nullptr,
          host.prepare_layer_capture(source),
          id,
          {[&](const auto &snapshot, const auto &request) {
             record(request);
             const auto ticket = host.request_layer_capture(snapshot, request);
             if (cancel && !requested) {
               requested = true;
               auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
               require(modal, "Native capture cancellation has no progress dialog");
               QTimer::singleShot(0, modal, [modal] { modal->reject(); });
             }
             return ticket;
           },
           [&] { return host.layer_capture_result(); }});
    });
    drain();
    if (cancel) {
      require(requested && !result, "Cancelled native capture installed a result");
    }
    else
      require(bool(result), "Native capture unexpectedly cancelled");
    return result;
  }
  std::array<float, 3> convert(int source, const std::array<float, 3> &value) const
  {
    std::array<float, 3> out{};
    const auto &matrix = sources.at(source).fill_preset->native_material->color_to_aces;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        out[r] += matrix[c * 3 + r] * value[c];
    return out;
  }
  std::array<float, 3> raw_color(int tile, const std::array<int, 2> &p) const
  {
    const auto &current = document->texture_sets().front();
    const float u = (p[0] + .501f) / current.width(tile), v = (p[1] + .502f) / current.rows(tile);
    const int owner = u > .5f;
    const float left = owner ? .55f : .05f;
    const auto generated = convert(0, {(u - left) / .4f, (v - .1f) / .8f, .5f}),
               constant = constant_color;
    std::array<float, 3> out{};
    for (int c = 0; c < 3; ++c)
      out[c] = .76f * (.75f * generated[c] + .25f * constant[c]) +
               .24f * std::array<float, 3>{.2f, .3f, .4f}[c];
    return out;
  }
  void leaf_cases()
  {
    // Resolve legacy/incorrect stored capability lists from the actual prepared
    // source. Resolving metadata must not mutate the document or graph instance.
    auto legacy = set();
    const auto original = TextureDocument::layer(legacy, native[0]).fill.native_material;
    auto empty = std::make_shared<NativeMaterialDescription>(*original);
    empty->channels.clear();
    TextureDocument::layer(legacy, native[0]).fill.native_material = empty;
    const auto resolved = host.prepare_layer_capture(legacy);
    const auto plan = prepare_flatten_layer(resolved, native[0]);
    require(plan.enabled[4] && !plan.enabled[3] && empty->channels.empty(),
            "Prepared native caps lost valid Bump or mutated legacy input");
    const auto before = packets;
    auto available = bake(legacy, native[0]);
    require(!available->bake.images[4].empty() && available->bake.images[3].empty(),
            "Native leaf lost grayscale Bump or captured unsupported Normal");
    for (int tile : set().tiles)
      for (const auto &p : positions.at(tile)) {
        const auto &pixel = available->bake.images[4].at(tile).pixels->at(
            std::size_t(p[1]) * set().width(tile) + p[0]);
        for (int c = 0; c < 3; ++c)
          check_close(pixel[c], .5f, "Native Bump neutral changed");
      }
    require(packets > before, "Native source bypassed GPU capture");
    auto stale = set();
    auto wrong = std::make_shared<NativeMaterialDescription>(
        *TextureDocument::layer(stale, native[1]).fill.native_material);
    wrong->channels = {0, 1, 2, 3, 4};
    TextureDocument::layer(stale, native[1]).fill.native_material = wrong;
    const auto actual = host.prepare_layer_capture(stale);
    const auto unavailable = prepare_flatten_layer(actual, native[1]);
    require(!unavailable.enabled[3] && !unavailable.enabled[4] &&
                wrong->channels == std::vector<int>({0, 1, 2, 3, 4}),
            "Stored capabilities replaced native source truth");
    auto captured = bake(stale, native[1]);
    require(captured->bake.images[3].empty() && captured->bake.images[4].empty(),
            "Unavailable native channels became bitmap data");
    export_group(native[1], false);
    auto old = set();
    TextureDocument::layer(old, native[2]).visible = true;
    auto preserved = bake(old, native[2]);
    for (int tile : set().tiles)
      for (const auto &p : positions.at(tile)) {
        const auto &pixel = preserved->bake.images[0].at(tile).pixels->at(
            std::size_t(p[1]) * set().width(tile) + p[0]);
        for (int c = 0; c < 3; ++c)
          check_close(
              pixel[c], constant_color[c], "Legacy source GPU Color changed after hydrate");
      }
  }
  QUuid group()
  {
    TextureDocument::add_paint(set());
    const auto paint = set().selected_layer;
    auto &layer = TextureDocument::layer(set(), paint);
    layer.channels = {true, false, false, true, true};
    layer.opacity[0] = .4f;
    layer.opacity[3] = .6f;
    layer.opacity[4] = .5f;
    constexpr int canvas = 512;
    for (int tile : set().tiles) {
      std::vector<float> pixels(std::size_t(canvas) * canvas * 4);
      const auto assign = [&](int channel, Pixel value) {
        for (int c = 0; c < 3; ++c)
          value[c] *= value[3];
        for (std::size_t i = 0; i < pixels.size(); i += 4)
          std::copy(value.begin(), value.end(), pixels.begin() + i);
        layer.pixels.assign_channel(canvas, channel, pixels, tile);
      };
      assign(0, {.2f, .3f, .4f, .6f});
      assign(3, {.1f, .2f, std::sqrt(.95f), .4f});
      assign(4, {1.5f, 1.5f, 1.5f, .4f});
    }
    TextureDocument::group_layers(set(), {native[0], native[1], paint});
    const auto id = set().selected_layer;
    auto &folder = TextureDocument::layer(set(), id);
    folder.opacity[0] = .7f;
    folder.opacity[3] = .8f;
    folder.opacity[4] = .6f;
    folder.blending[0] = folder.blending[4] = LayerBlend::Normal;
    folder.mask.emplace();
    folder.mask->background = .35f;
    apply();
    return id;
  }
  using Samples = std::map<std::pair<int, int>, std::vector<Pixel>>;
  Samples live(bool analytic)
  {
    const InspectionMaterial material{set().material_session_uid, set().base_values, {}};
    host.set_inspection(InspectionMode::Material, std::span(&material, 1));
    apply();
    Samples result;
    for (int tile : set().tiles)
      for (int channel : {0, 3, 4}) {
        auto &values = result[{tile, channel}];
        values = host.diagnostic_gpu_pixels(
            set().material_session_uid, channel, positions.at(tile), tile);
        if (analytic)
          for (std::size_t i = 0; i < values.size(); ++i) {
            if (channel == 0) {
              const auto raw = raw_color(tile, positions.at(tile)[i]);
              for (int c = 0; c < 3; ++c)
                check_close(values[i][c] + (1 - values[i][3]) * set().base_values.base_color[c],
                            .245f * raw[c] + .755f * set().base_values.base_color[c],
                            "Mixed native/paint live Color differs from analytic oracle");
            }
            else if (channel == 4)
              for (int c = 0; c < 3; ++c)
                check_close(values[i][c] + (1 - values[i][3]) * .5f,
                            .542f,
                            "Mixed grayscale Bump changed");
          }
      }
    host.set_inspection(InspectionMode::Normal, std::span(&material, 1));
    apply();
    for (int tile : set().tiles)
      result[{tile, 6}] = host.diagnostic_gpu_pixels(
          set().material_session_uid, 6, positions.at(tile), tile);
    host.set_inspection(InspectionMode::Material, std::span(&material, 1));
    return result;
  }
  void same(const Samples &reference)
  {
    const auto actual = live(false);
    for (const auto &[key, values] : reference)
      for (std::size_t i = 0; i < values.size(); ++i)
        for (int c = 0; c < 4; ++c)
          check_close(
              actual.at(key)[i][c], values[i][c], "Native flattened live GPU composite changed");
  }
  void raw_group(const PreparedFlatten &captured)
  {
    require(!captured.bake.images[3].empty() && !captured.bake.images[4].empty(),
            "Mixed group lost ordinary internal Normal or artist Bump");
    bool internal = false;
    for (const auto &asset : captured.assets)
      internal |= asset.search_terms.contains("internal:flattened-normal");
    require(internal, "Authored Normal bitmap lost its internal-only marker");
    for (int tile : set().tiles)
      for (const auto &p : positions.at(tile)) {
        const auto index = std::size_t(p[1]) * set().width(tile) + p[0];
        const auto expected = raw_color(tile, p);
        const auto color = captured.bake.images[0].at(tile).pixels->at(index),
                   bump = captured.bake.images[4].at(tile).pixels->at(index);
        for (int c = 0; c < 3; ++c) {
          check_close(
              color[c], expected[c], "Native raw group Color differs from analytic oracle");
          check_close(bump[c], .7f, "Native raw group Bump changed");
        }
        check_close(color[3], 1, "Native raw group coverage changed");
      }
  }
  static std::vector<int> exr_types(const QByteArray &bytes)
  {
    std::vector<int> types;
    qsizetype cursor = 8;
    const auto integer = [&](qsizetype at) {
      require(at >= 0 && at + 4 <= bytes.size(), "Truncated native output EXR header");
      unsigned value = 0;
      for (int i = 0; i < 4; ++i)
        value |= unsigned(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
      return value;
    };
    const auto text = [&](qsizetype &at) {
      QByteArray value;
      while (at < bytes.size() && bytes[at])
        value.append(bytes[at++]);
      require(at < bytes.size(), "Truncated native output EXR attribute");
      ++at;
      return value;
    };
    while (cursor < bytes.size() && bytes[cursor]) {
      const auto name = text(cursor), type = text(cursor);
      const auto size = integer(cursor);
      cursor += 4;
      require(size <= std::uint64_t(bytes.size() - cursor), "Invalid native output EXR attribute");
      if (name == "channels" && type == "chlist") {
        const auto end = cursor + size;
        while (cursor < end && bytes[cursor]) {
          text(cursor);
          types.push_back(int(integer(cursor)));
          cursor += 16;
        }
        return types;
      }
      cursor += size;
    }
    throw std::runtime_error("Native output EXR has no channels");
  }
  QStringList export_group(const QUuid &id, bool expected_bump = true)
  {
    const auto directory = output + "/bitmap-output";
    QDir().mkpath(directory);
    const auto tiles = host.export_source(*document, {set().id});
    require(!tiles.empty(), "Native output has no deferred coverage");
    QTimer drive, close;
    bool started = false;
    std::exception_ptr failed;
    drive.setInterval(5);
    close.setInterval(5);
    QObject::connect(&drive, &QTimer::timeout, [&] {
      if (started)
        return;
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!modal)
        return;
      auto *path = modal->findChild<QLineEdit *>("flattenExportDirectory");
      auto *run = modal->findChild<QPushButton *>("runFlattenExport");
      if (!path || !run)
        return;
      started = true;
      path->setText(directory);
      try {
        run->click();
      }
      catch (...) {
        failed = std::current_exception();
        modal->reject();
      }
    });
    QObject::connect(&close, &QTimer::timeout, [&] {
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (modal && modal->property("flattenExportFinished").toBool()) {
        exported = modal->property("flattenExportFiles").toStringList();
        modal->accept();
      }
    });
    drive.start();
    close.start();
    dialog([&] {
      choose_flattened_export(nullptr,
                              host.prepare_layer_capture(set()),
                              {id},
                              tiles.front().deferred_padding,
                              false,
                              {[&](const auto &source, const auto &request) {
                                 record(request);
                                 return host.request_layer_capture(source, request);
                               },
                               [&] { return host.layer_capture_result(); }});
      return true;
    });
    drive.stop();
    close.stop();
    if (failed)
      std::rethrow_exception(failed);
    require(started && !exported.empty(), "Native bitmap output did not finish");
    int bump_files = 0;
    for (const auto &name : exported) {
      require(!name.contains("normal", Qt::CaseInsensitive),
              "Native bitmap output exported internal Normal");
      if (!name.contains("_bump."))
        continue;
      ++bump_files;
      QFile file(directory + "/" + name);
      require(file.open(QIODevice::ReadOnly) && exr_types(file.readAll()) == std::vector<int>{2},
              "Native Bump output is not one float32 channel");
      const int tile = name.contains("1002") ? 1002 : 1001;
      const auto path = (directory + "/" + name).toUtf8();
      using Image = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
      Image image(
          blender::IMB_load_image_from_filepath(path.constData(),
                                                blender::ImBufFlags::NoColorspaceConvert |
                                                    blender::ImBufFlags::AlphaChannelPacked),
          blender::IMB_freeImBuf);
      require(image && image->float_data() && image->channels == 4 &&
                  image->x == set().width(tile) && image->y == set().rows(tile),
              "Scalar EXR did not decode to Blender float RGBA at native dimensions");
      for (const auto &p : positions.at(tile))
        for (int c = 0; c < 3; ++c)
          check_close(
              image->float_data()[(std::size_t(p[1]) * image->x + p[0]) * image->channels + c],
              .542f,
              "Native Bump file differs from captured GPU source");
    }
    require(bump_files == (expected_bump ? 2 : 0),
            "Native output omitted valid Bump or fabricated an unavailable channel");
    return exported;
  }
  ExportRequest standard_request(const QString &directory)
  {
    ExportRequest request;
    request.directory = directory.toStdString();
    request.format = ExportFormat::Exr32;
    request.selected_channels = std::array<bool, 9>{
        true, false, false, false, true, false, false, false, false};
    request.tiles = host.export_source(*document, {set().id});
    return request;
  }
  void standard_export(const Samples &reference, const char *stage)
  {
    const auto directory = output + "/standard-" + stage;
    require(QDir().mkpath(directory), "Cannot create standard export fixture directory");
    auto request = standard_request(directory);
    const auto files = export_file_list(request);
    require(files.size() == 4, "Standard flattened export lost its two Color/Bump UDIM pairs");
    auto writer = std::async(std::launch::async, [request] {
      std::atomic_bool cancel = false;
      write_texture_export(request, cancel, [](int, const std::string &) {});
    });
    QElapsedTimer deadline;
    deadline.start();
    while (writer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      step();
      require(deadline.elapsed() < 120000, "Standard flattened export did not complete");
    }
    writer.get();
    for (const auto &file : files) {
      require(file.channel == 0 || file.channel == 4, "Standard export exposed internal Normal");
      const auto path = (directory + "/" + QString::fromStdString(file.name)).toUtf8();
      using Image = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
      Image image(
          blender::IMB_load_image_from_filepath(path.constData(),
                                                blender::ImBufFlags::NoColorspaceConvert |
                                                    blender::ImBufFlags::AlphaChannelPacked),
          blender::IMB_freeImBuf);
      require(image && image->float_data() && image->x == set().width(file.tile) &&
                  image->y == set().rows(file.tile),
              "Standard export file dimensions changed");
      const auto &expected = reference.at({file.tile, file.channel});
      const auto &coordinates = positions.at(file.tile);
      for (std::size_t i = 0; i < coordinates.size(); ++i) {
        const auto p = coordinates[i];
        const auto *actual = image->float_data() +
                             (std::size_t(p[1]) * image->x + p[0]) * image->channels;
        for (int c = 0; c < 3; ++c)
          check_close(actual[c],
                      expected[i][c] +
                          (1 - expected[i][3]) *
                              (file.channel == 0 ? set().base_values.base_color[c] : .5f),
                      "Standard flattened EXR differs from live native GPU oracle");
      }
    }
  }
  void dialog_gpu_export(const Samples &reference)
  {
    const auto directory = output + "/dialog-gpu-export";
    require(QDir().mkpath(directory), "Cannot create dialog GPU export directory");
    const auto prior_config = document->export_configuration();
    QTimer drive, close;
    bool started = false, finished = false;
    std::exception_ptr failed;
    drive.setInterval(5);
    close.setInterval(5);
    QObject::connect(&drive, &QTimer::timeout, [&] {
      if (started)
        return;
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!modal || modal->objectName() != "textureExportDialog")
        return;
      auto *path = modal->findChild<QLineEdit *>("exportDirectory");
      auto *run = modal->findChild<QPushButton *>("runTextureExport");
      auto *count = modal->findChild<QLabel *>("exportCount");
      auto *list = modal->findChild<QListWidget *>("exportFileList");
      if (!path || !run || !count || !list)
        return;
      started = true;
      try {
        require(run->isEnabled() && list->count() > 0 &&
                    !count->text().contains("requires GPU capture", Qt::CaseInsensitive),
                ("Export textures dialog blocked native shader GPU capture: " +
                 count->text().toStdString())
                    .c_str());
        path->setText(directory);
        run->click();
      }
      catch (...) {
        failed = std::current_exception();
        modal->reject();
      }
    });
    QObject::connect(&close, &QTimer::timeout, [&] {
      auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!modal || modal->objectName() != "textureExportDialog" ||
          !modal->property("exportFinished").toBool())
        return;
      try {
        const auto err = modal->property("exportError").toString();
        require(err.isEmpty(), ("Dialog GPU export failed: " + err).toStdString().c_str());
        finished = true;
        modal->accept();
      }
      catch (...) {
        failed = std::current_exception();
        modal->reject();
      }
    });
    drive.start();
    close.start();
    dialog([&] {
      return choose_texture_export(
          nullptr,
          *document,
          [&](const auto &ids) { return host.export_source(*document, ids); },
          {},
          false,
          [&](const ExportRequest &req,
              std::shared_ptr<const std::atomic_bool> cancelled,
              std::function<void(std::vector<ExportTile>)> complete,
              std::function<void(std::string)> fail_cb) {
            new GpuTextureExportSource(
                nullptr,
                host,
                [&] { frame(); },
                *document,
                req,
                std::move(cancelled),
                std::move(complete),
                std::move(fail_cb));
          });
    });
    drive.stop();
    close.stop();
    document->set_export_configuration(prior_config);
    if (failed)
      std::rethrow_exception(failed);
    require(started && finished, "Export textures dialog GPU capture did not complete");
    const auto exported_list = QDir(directory).entryList({"*.exr"}, QDir::Files);
    require(exported_list.size() >= 4, "Dialog GPU export did not write expected EXR files");
    for (const auto &name : exported_list) {
      if (!name.contains("_base_color.") && !name.contains("_bump."))
        continue;
      const int tile = name.contains("1002") ? 1002 : 1001;
      const int channel = name.contains("_base_color.") ? 0 : 4;
      const auto path = (directory + "/" + name).toUtf8();
      using Image = std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)>;
      Image image(
          blender::IMB_load_image_from_filepath(path.constData(),
                                                blender::ImBufFlags::NoColorspaceConvert |
                                                    blender::ImBufFlags::AlphaChannelPacked),
          blender::IMB_freeImBuf);
      require(image && image->float_data() && image->x == set().width(tile) &&
                  image->y == set().rows(tile),
              "Dialog GPU export EXR dimensions changed");
      const auto &expected = reference.at({tile, channel});
      const auto &coordinates = positions.at(tile);
      for (std::size_t i = 0; i < coordinates.size(); ++i) {
        const auto p = coordinates[i];
        const auto *actual = image->float_data() +
                             (std::size_t(p[1]) * image->x + p[0]) * image->channels;
        for (int c = 0; c < 3; ++c)
          check_close(actual[c],
                      expected[i][c] + (1 - expected[i][3]) *
                                           (channel == 0 ? set().base_values.base_color[c] : .5f),
                      "Dialog GPU export EXR differs from live native GPU oracle");
      }
    }
  }
  QJsonObject run()
  {
    setup();
    leaf_cases();
    bool standard_rejected = false;
    try {
      export_file_list(standard_request(output));
    }
    catch (const std::runtime_error &e) {
      standard_rejected = std::string_view(e.what()).find("GPU capture") != std::string_view::npos;
    }
    require(standard_rejected, "Standard export silently approximated visible native sources");
    const auto id = group();
    const auto before = live(true);
    const auto original = document->to_json();
    dialog_gpu_export(before);
    const auto cancelled = bake(set(), id, true);
    require(!cancelled && document->to_json() == original &&
                TextureDocument::layer(set(), id).visible,
            "Cancelled native capture changed its source");
    const auto files = export_group(id);
    auto ready = bake(set(), id);
    raw_group(*ready);
    require(saw_tail && saw_mask && saw_internal_normal,
            "Native capture omitted packet tail, mask, or internal Normal coverage");
    for (auto asset : ready->assets)
      catalog.add(std::move(asset));
    const auto baked = ready->bake.layer.id;
    install_flattened_layer(set(), std::move(ready->bake.layer), id);
    same(before);
    require(!TextureDocument::layer(set(), id).visible &&
                TextureDocument::layer(set(), baked).visible,
            "Flatten did not retain its disabled native source");
    standard_export(before, "flattened");
    const auto saved = output + "/native-flatten-packed.blend";
    host.begin_save_project(*document, saved.toStdString(), catalog.project_assets());
    QElapsedTimer clock;
    clock.start();
    while (host.save_project_pending()) {
      step();
      host.poll_save_project();
      require(clock.elapsed() < 120000, "Native flattened project save did not finish");
    }
    require(QFile::rename(library, library + ".hidden"), "Cannot hide private native source");
    const auto restore = qScopeGuard([&] { QFile::rename(library + ".hidden", library); });
    const auto cache = qEnvironmentVariable("BTS_RESOURCE_CACHE");
    require(!cache.isEmpty() && QFileInfo(cache).absoluteFilePath().startsWith(
                                    QFileInfo(output).absoluteFilePath() + "/"),
            "Native capture cache is not private");
    if (QDir(cache).exists())
      require(QDir().rename(
                  cache, cache + ".hidden-" + QUuid::createUuid().toString(QUuid::WithoutBraces)),
              "Cannot hide private source cache");
    *document = host.open_project(saved.toStdString());
    const auto assets = host.project_resources();
    for (const auto &source : sources) {
      const auto found = std::find_if(
          assets.begin(), assets.end(), [&](const auto &asset) { return asset.id == source.id; });
      require(found != assets.end(), "Packed native source resource was lost");
      loader.ensure(
          *found, assets, [](const auto &) {}, [&](const QString &value) { failure = value; });
    }
    same(before);
    require(!TextureDocument::layer(set(), id).visible &&
                TextureDocument::layer(set(), baked).visible,
            "Packed Open lost source retention or flattened layer");
    standard_export(before, "reopened");
    // A caller explicitly requesting unavailable native data still receives the
    // original source error. Replacing that failed request must recover normally.
    auto isolated = set();
    TextureDocument::layer(isolated, id).visible = true;
    bool denied = false;
    try {
      const auto ticket = host.request_layer_capture(
          isolated,
          {native[1], 4, 1001, 0, 0, 1, 1, set().width(1001), set().rows(1001), false, false});
      for (int i = 0; i < 100; ++i) {
        step();
        const auto result = host.layer_capture_result();
        if (result && result->ticket == ticket)
          break;
      }
    }
    catch (const std::exception &value) {
      denied = std::string_view(value.what()).find("Native bump/normal extraction") !=
               std::string_view::npos;
    }
    require(denied, "Unavailable native Bump was silently approximated");
    host.request_layer_capture(
        isolated,
        {native[0], 0, 1001, 0, 0, 1, 1, set().width(1001), set().rows(1001), false, false});
    drain();
    return {{"objects", 2},
            {"tiles", QJsonArray{1001, 1002}},
            {"owner_samples", QJsonArray{qint64(owner_samples[0]), qint64(owner_samples[1])}},
            {"packets", qint64(packets)},
            {"captured_texels", qint64(texels)},
            {"component_checks", qint64(comparisons)},
            {"maximum_error", error},
            {"native_caps_resolved", true},
            {"pre_new_source_space_stable", true},
            {"legacy_archive_space_preserved", true},
            {"legacy_hydrated_gpu_oracle", true},
            {"signed_hdr_source_color", true},
            {"unavailable_bump_rejected", denied},
            {"unavailable_bump_export_omitted", true},
            {"available_bump_grayscale", true},
            {"internal_normal_preserved", saw_internal_normal},
            {"normal_exported", false},
            {"packet_tail", saw_tail},
            {"mask_and_opacity", true},
            {"cancel_keeps_source", true},
            {"source_free_packed_open", true},
            {"gpu_composite_oracle", true},
            {"cpu_native_composites", 0},
            {"standard_visible_native_rejected", standard_rejected},
            {"standard_flatten_export_gpu_oracle", true},
            {"standard_packed_open_export_gpu_oracle", true},
            {"standard_exported_files", 8},
            {"exported_files", QJsonArray::fromStringList(files)}};
  }
};
}  // namespace native_capture_demo
inline QJsonObject verify_native_capture_workflow(BlenderHost &host,
                                                  const std::function<void()> &frame)
{
  return native_capture_demo::Probe(host, frame).run();
}
}  // namespace bts
