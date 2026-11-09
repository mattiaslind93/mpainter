// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshMapImport.hh"
#include "TextureImportCpuDemo.hh"
#include "TextureLoadController.hh"
#include "TextureExport.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "ResourceIO.hh"
#include "FillTextureJSON.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "CompositeMemory.hh"
#include "NativeCompositeJob.hh"
#include "BKE_main.hh"
#include "BKE_idprop.hh"
#include "BLI_listbase_wrapper.hh"
#include "BKE_scene.hh"
#include "DNA_image_types.h"
#include "DNA_scene_types.h"
#include "DNA_windowmanager_types.h"
#include "BLO_writefile.hh"
#include "BKE_report.hh"
#include "BKE_global.hh"
#include <OpenImageIO/imageio.h>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <future>
#include <tuple>
#include "TextureImportUdimCpuTest.hh"
namespace bts {
namespace {
void require(bool ok, const std::string &message)
{
  if (!ok)
    throw std::runtime_error("Texture import regression: " + message);
}
void fixture(const QString &path,
             int size,
             int channels,
             OIIO::TypeDesc type,
             const char *space,
             const char *compression,
             float value,
             bool pattern = false,
             float alpha = 1)
{
  auto writer = OIIO::ImageOutput::create(path.toStdString());
  require(bool(writer), "writer unavailable " + path.toStdString());
  OIIO::ImageSpec spec(size, size, channels, type);
  spec.attribute("oiio:ColorSpace", space);
  if (path.endsWith(".exr"))
    spec.attribute("colorInteropID", space);
  spec.attribute("compression", compression);
  if (QFileInfo(path).fileName() == "rgba_tiled.exr") {
    spec.tile_width = 16;
    spec.tile_height = 16;
  }
  std::vector<float> pixels(std::size_t(size) * size * channels);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      for (int c = 0; c < channels; ++c) {
        float v = c == 3 ? alpha : value * (c == 1 ? .5f : 1.f);
        if (pattern && c < 3 && x > size / 2) {
          std::uint32_t n = (x + 1) * 73856093u ^ (y + 1) * 19349663u ^ (c + 1) * 83492791u;
          n ^= n >> 13;
          n *= 1274126177u;
          v = float(n & 65535) / 65535.f;
        }
        pixels[(std::size_t(y) * size + x) * channels + c] = v;
      }
  if (!writer->open(path.toStdString(), spec))
    require(false, "open " + path.toStdString() + ": " + writer->geterror());
  if (!writer->write_image(OIIO::TypeDesc::FLOAT, pixels.data()))
    require(false, "write " + path.toStdString() + ": " + writer->geterror());
  if (!writer->close())
    require(false, "close " + path.toStdString() + ": " + writer->geterror());
}
StudioAsset asset(const QString &path)
{
  auto source = reference_image_resource(path);
  StudioAsset a;
  a.id = QUuid::createUuid();
  a.scope = AssetScope::Project;
  a.name = QFileInfo(path).completeBaseName();
  a.filename = source.filename;
  a.path = path;
  a.usages = {"texture"};
  a.texture_files = source.files;
  return a;
}
}  // namespace
int run_texture_import_cpu_demo(const QString &directory)
{
  QDir().mkpath(directory);
  if (qEnvironmentVariableIsSet("BTS_HEIGHT_8K_IMPORT_TEST")) {
    const int size = 8192;
    const auto path = directory + "/height_1001.exr";
    auto writer = OIIO::ImageOutput::create(path.toStdString());
    require(bool(writer), "8K EXR writer");
    OIIO::ImageSpec spec(size, size, 1, OIIO::TypeDesc::FLOAT);
    spec.attribute("compression", "zip");
    spec.attribute("oiio:ColorSpace", "data");
    require(writer->open(path.toStdString(), spec), writer->geterror());
    std::vector<float> row(size);
    for (int x = 0; x < size; ++x)
      row[x] = (x - size / 2) / 8192.f;
    for (int y = 0; y < size; ++y)
      require(writer->write_scanline(y, 0, OIIO::TypeDesc::FLOAT, row.data()), writer->geterror());
    require(writer->close(), writer->geterror());
    auto source = asset(path);
    texture_vram_query_valid = true;
    texture_free_vram = 1;
    auto decoded = decode_fill_texture(source, true);
    require(decoded->width == size && decoded->height == size && decoded->floating_point,
            "8K height dimensions/precision");
    const auto &pixels = decoded->tiles ? decoded->tiles->at(1001)->pixels : decoded->pixels;
    require(bool(pixels), "8K height pixels missing");
    for (std::size_t i = 0; i < pixels->size(); ++i)
      require((*pixels)[i][0] == row[i % size], "8K height sample changed");
    TextureRamQueryScope installation_ram(
        []() -> std::uint64_t { return 9ULL * 1024 * 1024 * 1024; });
    TextureSet set;
    set.name = "8K height import proof";
    set.texture_resolution = set.texture_height = size;
    set.tiles = {1001};
    TextureDocument::add_fill(set);
    TextureDocument::add_displacement_channel(set);
    auto &fill = TextureDocument::layer(set, set.selected_layer);
    fill_channel_texture(fill.fill, 14) = decoded;
    fill.displacement.enabled = true;
    set.displacement.enabled = true;
    set.displacement.midlevel = .5f;
    set.displacement.strength = 2;
    displacement_import_range(set.displacement, *decoded);
    require(set.displacement.channel && set.displacement.allocated &&
                set.displacement.precision == ChannelPrecision::L32F,
            "8K displacement channel installation/precision");
    require(set.width(1001) == size && set.rows(1001) == size &&
                fill_channel_texture(fill.fill, 14)->width == size &&
                fill_channel_texture(fill.fill, 14)->height == size,
            "8K displacement import reduced dimensions");
    require(set.displacement.midlevel == 0 && set.displacement.strength == 1,
            "Signed displacement import lost data-unit range");
    QJsonArray composed_samples;
    for (int y : {1, size / 2, size - 2})
      for (int x : {1, size / 4, size / 2, size - 2}) {
        const auto value = TextureDocument::composite_displacement(set, x, y, 1001);
        const float expected = row[x];
        require(std::abs(value[0] - expected) < 1e-7f && value[1] == 1,
                "8K installed displacement composite changed signed source");
        require(std::abs(TextureDocument::displacement_value(set, x, y, 1001) - expected) < 1e-7f,
                "8K installed displacement evaluation changed source");
        composed_samples.append(QJsonObject{{"x", x},
                                            {"y", y},
                                            {"expected", expected},
                                            {"value", value[0]},
                                            {"coverage", value[1]}});
      }
    QFile proof(directory + "/height-8k-import.json");
    require(proof.open(QIODevice::WriteOnly), "8K EXR proof");
    proof.write(QJsonDocument(QJsonObject{{"file", path},
                                          {"resolution", size},
                                          {"float_bits", 32},
                                          {"udim", 1001},
                                          {"all_pixels_exact", true},
                                          {"displacement_channel_installed", true},
                                          {"installation_available_ram_bytes",
                                           qint64(9ULL * 1024 * 1024 * 1024)},
                                          {"channel_precision", "L32F"},
                                          {"full_project_dimensions", true},
                                          {"signed_import_midlevel", set.displacement.midlevel},
                                          {"signed_import_strength", set.displacement.strength},
                                          {"composite_samples", composed_samples},
                                          {"simulated_free_vram_bytes", 1},
                                          {"blocked", false}})
                    .toJson());
    return 0;
  }

  // Parent directory names must not select an image's storage layout. Exercise
  // the exact regression in every CPU run, including PNG's scanline-only writer.
  const auto fixture_directory = directory + "/tiled-parent";
  require(QDir().mkpath(fixture_directory), "fixture directory");
  for (const auto &name : {"scanline.exr", "image8.png", "image16.png", "rgba_tiled.exr"}) {
    const auto path = fixture_directory + "/" + name;
    const bool tiled = QString::fromLatin1(name) == "rgba_tiled.exr";
    const auto type = path.endsWith(".exr") ?
                          OIIO::TypeDesc::HALF :
                          (QString::fromLatin1(name) == "image16.png" ? OIIO::TypeDesc::UINT16 :
                                                                        OIIO::TypeDesc::UINT8);
    fixture(path, 32, 4, type, "lin_rec709_scene", "zip", .25f);
    auto reader = OIIO::ImageInput::open(path.toStdString());
    require(bool(reader), "fixture header " + path.toStdString());
    const auto &spec = reader->spec();
    require(spec.width == 32 && spec.height == 32 && spec.tile_width == (tiled ? 16 : 0) &&
                spec.tile_height == (tiled ? 16 : 0),
            "fixture layout depends on parent path: " + path.toStdString());
    reader->close();
  }
  if (qEnvironmentVariableIsSet("BTS_GENERATOR_IMPORT_TEST")) {
    const auto path = directory + "/generator-data.exr";
    fixture(path, 4096, 4, OIIO::TypeDesc::FLOAT, "lin_rec709_scene", "none", -.00125f, false, 0);
    require(QFileInfo(path).size() > 256LL * 1024 * 1024,
            "large utility-map fixture must exceed old file cap");
    const auto started = performance::Clock::now();
    const auto plane = load_mesh_map_image(path.toStdString());
    const auto ms = performance::ms(started);
    require(plane.resolution == 4096 && plane.rows() == 4096 && plane.pixels,
            "large utility-map dimensions");
    for (int index : {0, 4096 * 2048, 4096 * 4096 - 1}) {
      const auto &v = plane.pixels->rgba;
      require(v[std::size_t(index) * 4] == -.00125f &&
                  v[std::size_t(index) * 4 + 1] == -.000625f && v[std::size_t(index) * 4 + 3] == 1,
              "utility maps must remain raw/signed and ignore file alpha");
    }
    QFile proof(directory + "/generator-import.json");
    require(proof.open(QIODevice::WriteOnly), "generator import proof");
    proof.write(QJsonDocument(QJsonObject{{"file_bytes", qint64(QFileInfo(path).size())},
                                          {"resolution", 4096},
                                          {"load_ms", ms},
                                          {"raw_signed_exact", true},
                                          {"data_alpha_ignored", true}})
                    .toJson());
    return 0;
  }

  if (qEnvironmentVariableIsSet("BTS_AB_DETAIL_FIXTURES")) {
    // High-frequency signed height vanishes at the source mesh vertices. With
    // level-zero geometry, any visible detail must come from BOTH's bump path.
    for (int size : {4096, 256}) {
      std::vector<float> wave(size);
      if (size == 4096)
        for (int x = 0; x < size; ++x)
          wave[x] = x < 8 || x >= size - 8 ? 0 :
                                             std::sin(2 * 3.14159265358979323846 * (x + .5) / 16);
      std::vector<float> pixels(std::size_t(size) * size * 4);
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          const auto i = (std::size_t(y) * size + x) * 4;
          pixels[i] = pixels[i + 1] = pixels[i + 2] = .0015f * wave[x] * wave[y];
        }
      for (int tile : {1001, 1002}) {
        const auto path = directory + (size == 4096 ? "/detail_" : "/coarse_") +
                          QString::number(tile) + ".exr";
        auto writer = OIIO::ImageOutput::create(path.toStdString());
        require(bool(writer), "detail writer missing");
        OIIO::ImageSpec spec(size, size, 4, OIIO::TypeDesc::FLOAT);
        spec.attribute("oiio:ColorSpace", "lin_rec709_scene");
        spec.attribute("colorInteropID", "data");
        spec.attribute("compression", "zip");
        require(writer->open(path.toStdString(), spec), writer->geterror());
        require(writer->write_image(OIIO::TypeDesc::FLOAT, pixels.data()), writer->geterror());
        require(writer->close(), writer->geterror());
      }
    }
    QFile mesh(directory + "/detail.obj");
    require(mesh.open(QIODevice::WriteOnly), "detail mesh output");
    mesh.write(
        "mtllib detail.mtl\no ABDetail\nv -2 -1 0\nv 0 -1 0\nv 0 1 0\nv -2 1 0\nv 0 -1 0\nv 2 -1 0\nv 2 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvt 1 0\nvt 2 0\nvt 2 1\nvt 1 1\nusemtl ABDetail\ns 1\nf 1/1 2/2 3/3 4/4\nf 5/5 6/6 7/7 8/8\n");
    mesh.close();
    QFile material(directory + "/detail.mtl");
    require(material.open(QIODevice::WriteOnly), "detail material output");
    material.write("newmtl ABDetail\nKd 1 1 1\nPr 0.3\nPm 0\n");
    return 0;
  }
  const bool large = qEnvironmentVariableIsSet("BTS_TEXTURE_IMPORT_LARGE");
  const int size = large ? 4096 : 32;
  QJsonArray formats, timings;
  for (const auto &separator : {QString("."), QString("_"), QString("-")}) {
    const auto prefix = directory + "/color" + separator;
    for (int tile = 1001; tile <= 1002; ++tile)
      fixture(prefix + QString::number(tile) + ".exr",
              size,
              4,
              separator == "." ? OIIO::TypeDesc::HALF : OIIO::TypeDesc::FLOAT,
              "lin_ap1_scene",
              "zip",
              tile == 1001 ? .25f : .75f,
              large);
    auto source = asset(prefix + "1001.exr");
    const auto started = performance::Clock::now();
    double max_ping = 0;
    auto last = started;
    QEventLoop loop;
    QTimer ping, poll;
    QObject::connect(&ping, &QTimer::timeout, &loop, [&] {
      const auto now = performance::Clock::now();
      max_ping = std::max(max_ping, performance::ms(last));
      last = now;
    });
    ping.start(10);
    auto future = std::async(std::launch::async, [&] { return decode_fill_texture(source); });
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        loop.quit();
    });
    poll.start(5);
    loop.exec();
    ping.stop();
    auto decoded = future.get();
    require(decoded->tiles && decoded->tiles->size() == 2, "all UDIM tiles must decode");
    for (int tile = 1001; tile <= 1002; ++tile) {
      const auto p = decoded->tiles->at(tile)->sample(.25, .25, false);
      require(std::abs(p[0] - (tile == 1001 ? .25f : .75f)) < 1e-6,
              "ACEScg interop metadata transformed twice");
    }
    const double elapsed = performance::ms(started);
    timings.append(QJsonObject{{"separator", separator},
                               {"size", size},
                               {"decode_ms", elapsed},
                               {"max_ui_ping_ms", max_ping},
                               {"tile_count", 2}});
    require(max_ping < 50, "worker decode blocked event loop");
    // Save the exact encoded bytes per tile, then remove sources. Hydration must
    // resolve the same descriptor and pixels without a BTSUDIM1 memory bundle.
    auto *main = blender::BKE_main_new();
    auto *scene = blender::BKE_scene_add(main, "Texture import resource proof");
    pack_project_assets(*main, *scene, {source});
    for (const auto &f : source.texture_files)
      require(QFile::rename(f.path, f.path + ".hidden"), "cannot hide synthetic source");
    auto packed = read_project_assets(*scene);
    require(packed.size() == 1 && packed[0].bytes.isEmpty() && packed[0].hash() == source.hash(),
            "per-tile resource packing changed identity");
    auto restored = decode_fill_texture(packed[0]);
    require(fill_texture_json(decoded) == fill_texture_json(restored),
            "packed texture descriptor changed");
    for (int tile = 1001; tile <= 1002; ++tile)
      require(*decoded->tiles->at(tile)->pixels == *restored->tiles->at(tile)->pixels,
              "packed source changed decoded pixels");
    blender::BKE_main_free(main);
    for (const auto &f : source.texture_files)
      require(QFile::rename(f.path + ".hidden", f.path), "cannot restore synthetic source");
  }
  for (const auto &entry :
       std::vector<std::tuple<QString, OIIO::TypeDesc, int, const char *>>{
           {"rgba_float.exr", OIIO::TypeDesc::FLOAT, 4, "none"},
           {"gray.exr", OIIO::TypeDesc::HALF, 1, "piz"},
           {"rgba_tiled.exr", OIIO::TypeDesc::FLOAT, 4, "dwaa"},
           {"image8.png", OIIO::TypeDesc::UINT8, 4, "zip"},
           {"image16.png", OIIO::TypeDesc::UINT16, 4, "zip"},
           {"image8.tif", OIIO::TypeDesc::UINT8, 3, "zip"},
           {"image16.tif", OIIO::TypeDesc::UINT16, 3, "zip"},
           {"image32.tif", OIIO::TypeDesc::FLOAT, 3, "zip"},
           {"radiance.hdr", OIIO::TypeDesc::FLOAT, 3, "none"},
           {"photo.jpg", OIIO::TypeDesc::UINT8, 3, "none"}})
  {
    const auto &[name, type, channels, compression] = entry;
    const auto path = directory + "/" + name;
    fixture(path,
            large && name == "rgba_float.exr" ? 4096 : 32,
            channels,
            type,
            "lin_rec709_scene",
            compression,
            .25);
    auto source = asset(path);
    auto raw = decode_fill_texture(source, true);
    require(std::abs(raw->sample(.5, .5)[0] - .25f) < .015,
            "data channel incorrectly color transformed: " + name.toStdString());
    if (large && name == "rgba_float.exr")
      require(source.texture_files[0].size > 256LL * 1024 * 1024,
              "large EXR does not cover 256MiB regression");
    formats.append(QJsonObject{{"file", name},
                               {"bytes", source.texture_files[0].size},
                               {"raw_sample", raw->sample(.5, .5)[0]}});
  }
  // AA1: no dimension/pixel ceiling in descriptors; actual decode admission
  // uses available RAM (and the device limits published by the owner thread).
  {
    FillTexture descriptor;
    descriptor.asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    descriptor.hash = std::string(64, 'a');
    descriptor.name = "Wide image";
    descriptor.width = 16384;
    descriptor.height = 1;
    descriptor.validate();
    require(texture_decode_available_bytes() <= texture_available_ram_bytes(),
            "decoder availability exceeds live RAM");
  }
  // AB: real encoded float/Y and RGBA UDIMs retain raw RGB even if file alpha
  // is zero. The alpha option changes coverage, never the shared resource.
  QJsonArray displacement_proofs;
  for (int components : {1, 4}) {
    const auto prefix = directory + QString("/ab%1_").arg(components);
    const std::array<int, 3> tiles{1001, 1002, 1011};
    const std::array<float, 3> values{-.001f, .002f, 2.25f};
    for (int i = 0; i < 3; ++i)
      fixture(prefix + QString::number(tiles[i]) + ".exr",
              size,
              components,
              OIIO::TypeDesc::FLOAT,
              "lin_rec709_scene",
              "none",
              values[i],
              false,
              0);
    const auto source = asset(prefix + "1001.exr");
    auto decoded = decode_fill_texture(source, true);
    require(decoded->range_valid && decoded->floating_point && decoded->minimum == -.001f &&
                decoded->maximum == 2.25f,
            "signed decoded range lost");
    auto alias = source;
    alias.id = QUuid::createUuid();
    auto shared = decode_fill_texture(alias, true);
    require(shared->pixels == decoded->pixels && shared->minimum == decoded->minimum &&
                shared->maximum == decoded->maximum,
            "shared decode lost range metadata");
    TextureSet model;
    model.texture_resolution = 128;
    model.tiles = {1001, 1002, 1011};
    model.name = "AB data";
    model.material_session_uid = 1;
    model.precision[4] = ChannelPrecision::L32F;
    TextureDocument::add_displacement_channel(model);
    displacement_import_range(model.displacement, *decoded);
    require(model.displacement.midlevel == 0 && model.displacement.strength == 1,
            "signed import did not choose unit height");
    TextureDocument::add_fill(model);
    auto &layer = TextureDocument::layer(model, model.selected_layer);
    layer.fill.mode = 6;
    layer.displacement.enabled = true;
    fill_channel_texture(layer.fill, 14) = decoded;
    fill_channel_texture(layer.fill, 4) = decoded;
    for (int i = 0; i < 3; ++i) {
      const auto &member = decoded->tiles->at(tiles[i]);
      require(member->input_space == "Non-Color/raw", "data metadata affected conversion");
      for (int filtering = 0; filtering < 3; ++filtering) {
        layer.fill.filtering = filtering;
        const auto result = TextureDocument::composite_displacement(model, 32, 32, tiles[i]);
        require(result[0] == values[i] && result[1] == 1,
                "zero-alpha data lost displacement or wrong tile");
        require(TextureDocument::composite(model, 32, 32, tiles[i]).material.bump == values[i],
                "signed bump clamped");
      }
      layer.fill.alpha_mask[14] = true;
      const auto masked = TextureDocument::composite_displacement(model, 32, 32, tiles[i]);
      require(masked[1] == (components == 4 ? 0 : 1), "explicit file alpha mask ignored");
      layer.fill.alpha_mask[14] = false;
    }
    ExportRequest output;
    output.directory = (directory + QString("/ab-export-%1").arg(components)).toStdString();
    QDir().mkpath(QString::fromStdString(output.directory));
    output.format = ExportFormat::Exr32;
    output.selected_channels = std::array<bool, 9>{
        false, false, false, false, true, false, false, false, true};
    for (int tile : tiles) {
      ExportTile item{model, tile, 128};
      item.normal.resize(128 * 128 * 4);
      output.tiles.push_back(std::move(item));
    }
    std::atomic_bool stop = false;
    write_texture_export(output, stop, [](int, const std::string &) {});
    for (const auto &file : export_file_list(output)) {
      auto source = asset(QString::fromStdString(output.directory + "/" + file.name));
      std::erase_if(source.texture_files,
                    [&](const auto &member) { return member.tile != file.tile; });
      auto exported = decode_fill_texture(source, true);
      const auto index = std::find(tiles.begin(), tiles.end(), file.tile) - tiles.begin();
      require(exported->sample(.25, .25, false, false)[0] == values[index],
              "float32 bump/displacement export changed signed sample");
    }
    displacement_proofs.append(QJsonObject{{"components", components},
                                           {"tiles", 3},
                                           {"size", size},
                                           {"minimum", decoded->minimum},
                                           {"maximum", decoded->maximum},
                                           {"midlevel", model.displacement.midlevel},
                                           {"raw_zero_alpha_coverage_one", true},
                                           {"explicit_alpha_mask", true},
                                           {"export_exact", true},
                                           {"shared_stats", true}});
  }
  QJsonObject artist_copy_proof;
  if (const auto path = qEnvironmentVariable("BTS_AB_SOURCE_COPY"); !path.isEmpty()) {
    if (!QFileInfo(path).canonicalFilePath().startsWith(
            QFileInfo(qEnvironmentVariable("BTS_ROOT") + "/build").canonicalFilePath() + "/"))
      throw std::runtime_error("AB actual source test requires isolated build copy");
    const auto start = performance::Clock::now();
    const auto decoded = decode_fill_texture(asset(path), true);
    require(decoded->tiles && decoded->tiles->size() == 2, "actual copied displacement lost tile");
    QJsonArray samples;
    for (const auto &[tile, member] : *decoded->tiles) {
      require(member->floating_point && member->range_valid &&
                  member->input_space == "Non-Color/raw",
              "copied source interpretation wrong");
      std::size_t covered = 0, nonzero = 0;
      for (const auto &pixel : *member->pixels) {
        covered += pixel[3] > 0;
        nonzero += pixel[0] != 0;
      }
      samples.append(QJsonObject{{"tile", tile},
                                 {"width", member->width},
                                 {"minimum", member->minimum},
                                 {"maximum", member->maximum},
                                 {"file_alpha_nonzero", qint64(covered)},
                                 {"height_nonzero", qint64(nonzero)}});
      if (tile == 1002)
        require(covered == 0 && nonzero > 0,
                "actual source does not exercise zero-alpha displacement");
    }
    DisplacementSettings settings;
    displacement_import_range(settings, *decoded);
    require(settings.midlevel == 0, "actual copied signed height lost units");
    artist_copy_proof = {{"tiles", samples},
                         {"decode_ms", performance::ms(start)},
                         {"midlevel", settings.midlevel},
                         {"strength", settings.strength}};
  }
  QJsonObject memory_proof{{"available_ram_bytes", qint64(texture_decode_available_bytes())}};
  {
    auto source = asset(directory + "/color_1001.exr");
    TextureDecodeControl small;
    auto decoded = decode_fill_texture(source, false, {}, small);
    require(bool(decoded), "Former loading limit blocked full-resolution decode");
    memory_proof["former_limit_ignored"] = true;
  }
  if (large) {
    const auto folder = directory + "/ten-tiles";
    QDir().mkpath(folder);
    for (int tile = 1001; tile <= 1010; ++tile) {
      const auto path = folder + "/large_" + QString::number(tile) + ".exr";
      if (!QFileInfo::exists(path))
        require(QFile::copy(directory + "/color_1001.exr", path), "10-tile fixture copy");
    }
    auto ten = asset(folder + "/large_1001.exr");
    const auto start = performance::Clock::now();
    auto decoded = decode_fill_texture(ten);
    require(decoded->tiles && decoded->tiles->size() == 10, "10×4K float sequence lost tiles");
    for (const auto &[tile, texture] : *decoded->tiles)
      require(texture->sample(.25, .25, false)[0] == .25f, "10-tile sample differs");
    memory_proof["ten_4k_tiles_decoded"] = 10;
    memory_proof["ten_4k_ms"] = performance::ms(start);
  }
  const auto unknown = directory + "/unknown.exr";
  fixture(unknown, 32, 4, OIIO::TypeDesc::FLOAT, "not_a_color_space", "zip", .25);
  auto unknown_asset = asset(unknown);
  require(decode_fill_texture(unknown_asset, true)->sample(.5, .5)[0] == .25f,
          "data should ignore unknown metadata");
  bool rejected = false;
  try {
    decode_fill_texture(unknown_asset);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "unknown color space accepted for color");
  std::atomic_bool cancel = true;
  TextureDecodeControl control;
  control.cancel = &cancel;
  rejected = false;
  try {
    decode_fill_texture(unknown_asset, true, {}, control);
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "cancelled source installed");
  QFile corrupt(directory + "/corrupt.exr");
  require(corrupt.open(QIODevice::WriteOnly), "corrupt fixture");
  corrupt.write("not an EXR");
  corrupt.close();
  rejected = false;
  try {
    decode_fill_texture(asset(corrupt.fileName()));
  }
  catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "corrupt EXR accepted");
  TextureSet set;
  set.texture_resolution = 128;
  set.tiles = {1001, 1002};
  set.material_session_uid = 1;
  set.name = "UdimImported";
  set.channels = {true, false, true, false, false};
  set.precision[0] = ChannelPrecision::RGB32F;
  set.precision[2] = ChannelPrecision::L32F;
  TextureDocument::add_fill(set);
  auto &fill = TextureDocument::layer(set, set.selected_layer);
  fill.fill.mode = 6;
  fill.fill.texture = decode_fill_texture(asset(directory + "/color_1001.exr"));
  fill_channel_texture(fill.fill, 2) = decode_fill_texture(asset(directory + "/color-1001.exr"),
                                                           true);
  for (int tile : set.tiles)
    for (int x : {16, 32, 48}) {
      const auto expected = tile == 1001 ? .25f : .75f;
      const auto value = TextureDocument::composite(set, x, 32, tile);
      require(std::abs(value.material.base_color[0] - expected) < 1e-6 &&
                  std::abs(value.material.roughness - expected) < 1e-6,
              "per-tile CPU composition changed raw/color source");
    }
  ExportRequest request;
  request.directory = (directory + "/export").toStdString();
  QDir().mkpath(QString::fromStdString(request.directory));
  request.format = ExportFormat::Exr32;
  request.selected_channels = std::array<bool, 9>{
      true, false, true, false, false, false, false, false, false};
  for (int tile : set.tiles) {
    ExportTile output{set, tile, 128};
    output.normal.resize(128 * 128 * 4);
    request.tiles.push_back(std::move(output));
  }
  std::atomic_bool exporting = false;
  write_texture_export(request, exporting, [](int, const std::string &) {});
  for (const auto &file : export_file_list(request)) {
    auto exported_asset = asset(QString::fromStdString(request.directory + "/" + file.name));
    std::erase_if(exported_asset.texture_files,
                  [&](const auto &source) { return source.tile != file.tile; });
    auto exported = decode_fill_texture(exported_asset, true);
    for (double u : {.125, .25, .375})
      require(std::abs(exported->sample(u, .25, false)[0] - (file.tile == 1001 ? .25f : .75f)) <
                  1e-6,
              "export changed texture or tile identity");
  }
  qint64 packed_one_bytes = 0, packed_three_bytes = 0;
  // AA shared resources: three independently named/identified usages retain
  // one immutable decoded allocation per physical tile and one packed image.
  {
    auto first = asset(directory + "/color_1001.exr"), second = first, third = first;
    second.id = QUuid::createUuid();
    third.id = QUuid::createUuid();
    second.name = "Second usage";
    third.name = "Third usage";
    auto one = decode_fill_texture(first), two = decode_fill_texture(second),
         three = decode_fill_texture(third);
    require(one->asset != two->asset && two->asset != three->asset,
            "shared pixels lost per-usage resource references");
    for (int tile : {1001, 1002})
      require(one->tiles->at(tile)->pixels.get() == two->tiles->at(tile)->pixels.get() &&
                  one->tiles->at(tile)->pixels.get() == three->tiles->at(tile)->pixels.get(),
              "same texture was decoded/stored more than once");
    TextureDecodeControl no_new_allocation;
    auto reused = decode_fill_texture(third, false, {}, no_new_allocation);
    require(reused->pixels.get() == one->pixels.get(),
            "a cached reference incorrectly requires a new decode allocation");
    auto *main = blender::BKE_main_new();
    auto *scene = blender::BKE_scene_add(main, "Shared resource proof");
    const auto write = [&](const QString &path) {
      blender::BlendFileWriteParams params{};
      params.remap_mode = blender::BLO_WRITE_PATH_REMAP_NONE;
      params.use_save_as_copy = true;
      blender::ReportList reports{};
      blender::BKE_reports_init(&reports, blender::RPT_STORE);
      const auto filename = path.toUtf8();
      const bool ok = blender::BLO_write_file(
          main, filename.constData(), blender::G_FILE_COMPRESS, &params, &reports);
      blender::BKE_reports_free(&reports);
      require(ok, "shared resource bundle write failed");
      return QFileInfo(path).size();
    };
    pack_project_assets(*main, *scene, {first});
    packed_one_bytes = write(directory + "/shared-one.blend");
    pack_project_assets(*main, *scene, {first, second, third});
    packed_three_bytes = write(directory + "/shared-three.blend");
    require(std::abs(packed_three_bytes - packed_one_bytes) < 1024 * 1024,
            "three references duplicated .blend payload");
    int images = 0;
    for (const auto &image : main->images)
      if (QString::fromUtf8(image.id.name + 2).startsWith("BTS Resource "))
        ++images;
    require(images == 2, "three usages duplicated packed per-tile resources");
    blender::BKE_main_free(main);
  }
  // Replacing a resource keeps its UUID but must not reuse the previous
  // packed image. The resource image's content identity guards this cache.
  // Legacy byte-backed aliases share encoded storage too, including upgrades
  // from a previous manifest with separate private images.
  {
    using namespace blender;
    const auto legacy = [&](const QString &file) {
      auto result = asset(directory + "/" + file);
      QFile input(result.texture_files.front().path);
      require(input.open(QIODevice::ReadOnly), "legacy alias fixture");
      result.bytes = input.readAll();
      result.texture_files.clear();
      return result;
    };
    auto first = legacy("image8.png"), second = legacy("image16.png"), third = first;
    third.id = QUuid::createUuid();
    auto *main = blender::BKE_main_new();
    auto *scene = blender::BKE_scene_add(main, "Legacy alias upgrade");
    const auto count = [&] {
      int total = 0;
      for (const auto &image : main->images)
        if (QString::fromUtf8(image.id.name + 2).startsWith("BTS Resource "))
          ++total;
      return total;
    };
    pack_project_assets(*main, *scene, {first, second, third});
    require(count() == 2, "byte aliases duplicated initial packed payload");
    second.bytes = first.bytes;
    pack_project_assets(*main, *scene, {first, second, third});
    require(count() == 1, "old unreferenced private payload survived alias migration");
    const auto opened = read_project_assets(*scene);
    require(opened.size() == 3, "legacy alias identities missing");
    require(opened[0].bytes.constData() == opened[1].bytes.constData() &&
                opened[1].bytes.constData() == opened[2].bytes.constData(),
            "legacy aliases copied encoded RAM bytes");
    second = legacy("image16.png");
    pack_project_assets(*main, *scene, {first, second, third});
    require(count() == 2, "different source payload unexpectedly shared");
    auto *refs = blender::IDP_GetPropertyFromGroup(scene->id.properties, "bts_resource_images");
    const auto key = second.id.toString(QUuid::WithoutBraces).toStdString();
    auto *ref = blender::IDP_GetPropertyFromGroup(refs, key.c_str());
    blender::IDPropertyTemplate foreign{};
    foreign.id = IDP_ID_get(ref);
    blender::IDP_ReplaceInGroup(
        scene->id.properties,
        blender::IDP_New(blender::IDP_ID, &foreign, "external_payload_user"));
    second.bytes = first.bytes;
    pack_project_assets(*main, *scene, {first, second, third});
    require(count() == 2, "alias migration deleted an externally referenced image");
    blender::BKE_main_free(main);
    std::puts(
        "BTS_RESOURCE_ALIAS_UPGRADE legacy_bytes=1 shared_ram=1 orphan_removed=1 external_user_preserved=1");
  }
  {
    auto original = asset(directory + "/image8.png");
    auto *main = blender::BKE_main_new();
    auto *scene = blender::BKE_scene_add(main, "Replaced source");
    pack_project_assets(*main, *scene, {original});
    fixture(directory + "/replacement.png", 32, 4, OIIO::TypeDesc::UINT8, "sRGB", "zip", .75);
    auto replacement = asset(directory + "/replacement.png");
    replacement.id = original.id;
    pack_project_assets(*main, *scene, {replacement});
    auto packed = read_project_assets(*scene);
    require(packed[0].hash() == replacement.hash(), "replaced resource packed stale bytes");
    require(std::abs(decode_fill_texture(packed[0], true)->sample(.5, .5)[0] - .75) < .005,
            "replaced resource pixels stale");
    blender::BKE_main_free(main);
  }
  // Exercise the real queued owner-thread install boundary: failure/cancellation
  // leave the previous document/Undo untouched, and the newest slot wins.
  int installs = 0, errors = 0, statuses = 0;
  bool valid = true;
  TextureLoadHooks hooks;
  hooks.valid = [&](const auto &) { return valid; };
  hooks.busy = [] { return false; };
  hooks.install = [&](LoadedTexture loaded) {
    require(bool(loaded.texture), "unresolved texture installed");
    ++installs;
  };
  hooks.error = [&](const QString &) { ++errors; };
  hooks.status = [&](const auto &, const auto &) { ++statuses; };
  TextureLoadController controller(nullptr, std::move(hooks));
  TextureLoadTarget target;
  target.document = QUuid::createUuid();
  target.set = QUuid::createUuid();
  target.layer = QUuid::createUuid();
  target.channel = 2;
  target.tiles = {1001, 1002};
  const auto drain = [&] {
    QEventLoop events;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &events, [&] {
      if (!controller.pending())
        events.quit();
    });
    QTimer::singleShot(15000, &events, &QEventLoop::quit);
    poll.start();
    events.exec();
    require(!controller.pending(), "texture queue did not drain");
  };
  controller.from_file(target, directory + "/image8.png");
  controller.cancel(target);
  drain();
  require(installs == 0 && errors == 0, "cancelled load created edit/error");
  controller.from_file(target, corrupt.fileName());
  drain();
  require(installs == 0 && errors == 1, "corrupt load created edit or hid failure");
  controller.from_file(target, directory + "/image8.png");
  controller.from_file(target, directory + "/image16.png");
  drain();
  require(installs == 1 && errors == 1, "superseded texture slot was installed");
  controller.from_file(target, directory + "/image8.png");
  valid = false;
  drain();
  require(installs == 1, "stale target was installed");
  valid = true;
  const auto missing = directory + "/missing_1001.exr";
  fixture(missing, 32, 4, OIIO::TypeDesc::HALF, "lin_ap1_scene", "zip", .25);
  controller.from_file(target, missing);
  drain();
  require(installs == 1 && errors == 2, "missing required UV tile was accepted");
  // GPU preparation is a second cancellable loading phase. No document or
  // Undo install is allowed until the exact shared source is ready.
  {
    bool gpu_ready = false, gpu_error = false, valid_target = true;
    int preparing = 0, committed = 0, released = 0, failed = 0;
    TextureLoadHooks stages;
    stages.valid = [&](const auto &) { return valid_target; };
    stages.busy = [] { return false; };
    stages.status = [](const auto &, const auto &) {};
    stages.prepare = [&](const LoadedTexture &loaded) {
      require(bool(loaded.texture), "unresolved preparation");
      ++preparing;
      if (gpu_error)
        throw std::runtime_error("Simulated GPU allocation failure");
      return gpu_ready;
    };
    stages.install = [&](LoadedTexture) { ++committed; };
    stages.release_preparation = [&] { ++released; };
    stages.error = [&](const QString &) { ++failed; };
    TextureLoadController staged(nullptr, std::move(stages));
    const auto until = [&](auto predicate) {
      if (predicate())
        return;
      QEventLoop loop;
      QTimer timer;
      timer.setInterval(5);
      QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        if (predicate())
          loop.quit();
      });
      QTimer::singleShot(5000, &loop, &QEventLoop::quit);
      timer.start();
      loop.exec();
      require(predicate(), "preparation boundary timed out");
    };
    staged.from_file(target, directory + "/image8.png");
    until([&] { return preparing > 1; });
    require(staged.pending() && committed == 0, "GPU-pending source changed document");
    staged.cancel(target);
    until([&] { return !staged.pending(); });
    require(committed == 0 && failed == 0 && released > 0, "cancelled preparation committed");
    const int previous = preparing;
    staged.from_file(target, directory + "/image16.png");
    until([&] { return preparing > previous; });
    gpu_ready = true;
    until([&] { return !staged.pending(); });
    require(committed == 1 && failed == 0, "ready texture was not committed exactly once");
    gpu_error = true;
    staged.from_file(target, directory + "/image8.png");
    until([&] { return !staged.pending(); });
    require(committed == 1 && failed == 1, "preparation error changed document or was hidden");
    gpu_error = false;
    gpu_ready = false;
    const int before_stale = preparing;
    staged.from_file(target, directory + "/image16.png");
    until([&] { return preparing > before_stale; });
    valid_target = false;
    until([&] { return !staged.pending(); });
    require(committed == 1 && failed == 1, "stale GPU preparation committed");
    std::puts(
        "BTS_TEXTURE_PREPARATION_VERIFIED pending_without_install=1 repeated_poll=1 cancel=1 one_commit=1 error=1 stale=1");
  }
  auto legacy = asset(directory + "/image8.png");
  QFile bytes(directory + "/image8.png");
  require(bytes.open(QIODevice::ReadOnly), "legacy source");
  legacy.bytes = bytes.readAll();
  legacy.texture_files = {};
  auto old = decode_fill_texture(legacy, true);
  require(old->width == 32, "legacy encoded resource no longer opens");
  // AA4: a low budget partitions all channels while retaining cross-tile
  // normal/bump and AO dependencies. The final vectors must be bit-exact.
  int channel_batches = 0;
  {
    TextureSet model;
    model.texture_resolution = 128;
    model.tiles = {1001, 1002};
    model.material_session_uid = 44;
    model.ao_channel = true;
    model.ao_mixing = AoMixing::Replace;
    model.normal_mixing = NormalMixing::Replace;
    auto &paint = model.layers.front();
    model.selected_mask = true;
    paint.mask = LayerMask{};
    paint.mask->background = .7f;
    PaintPixels::Stamp ink{{.4, .4}, {.3, 0}, {0, .24}};
    ink.values.base_color = {.2, .6, .4, 1};
    ink.values.normal = {.2f, -.1f, .9746794f};
    ink.values.bump = -.2;
    ink.values.roughness = .64;
    ink.flow = .7;
    for (double offset : {0., 1.}) {
      ink.center[0] = .4 + offset;
      paint.pixels.stamp(128, ink, model.tiles);
      auto scalar = ink;
      scalar.channels = {false, false, true, false, false};
      scalar.values.roughness = .45;
      paint.ao_pixels.stamp(128, scalar, model.tiles);
    }
    std::atomic_bool stop = false;
    CompositeChannels remaining = complete_composite_channels;
    remaining[15] = true;
    // Include the selected mask in the full reference as well.
    auto all = complete_composite_channels;
    all[15] = true;
    const auto reference = native_composite_job(model, true, {}, 1, stop, false, all);
    while (std::any_of(remaining.begin(), remaining.end(), [](bool b) { return b; })) {
      const auto batch = composite_batch(model, true, remaining, 3ULL * 1024 * 1024);
      auto part = native_composite_job(model, true, {}, 1, stop, false, batch.channels);
      require(part.has_value(), "composite batch cancelled");
      for (const auto &[tile, planes] : part->tiles)
        for (int c = 0; c < 17; ++c)
          if (!planes[c].empty())
            require(planes[c] == reference->tiles.at(tile)[c],
                    "channel batch differs from full CPU oracle");
      remaining = batch.remaining;
      require(++channel_batches < 18, "composite batching failed to consume channels");
    }
    require(channel_batches >= 1, "Composite produced no channel batches");
  }
  QFile proof(directory + "/texture-import-cpu.json");
  require(proof.open(QIODevice::WriteOnly), "proof output");
  proof.write(
      QJsonDocument(
          QJsonObject{{"sparse_udim", verify_sparse_udim_import_cpu(directory + "/sparse-udim")},
                      {"ab_actual_source_copy", artist_copy_proof},
                      {"ab_displacement", displacement_proofs},
                      {"shared_packed_one_bytes", packed_one_bytes},
                      {"shared_packed_three_bytes", packed_three_bytes},
                      {"shared_three_usages_one_pixel_allocation_per_tile", true},
                      {"shared_three_usages_packed_images", 2},
                      {"aa4_exact_channel_batches", channel_batches},
                      {"aa1_memory", memory_proof},
                      {"formats", formats},
                      {"timings", timings},
                      {"per_tile_pack_exact", true},
                      {"metadata_ap1_identity", true},
                      {"data_ignores_tag", true},
                      {"unknown_color_rejected", true},
                      {"cancelled_rejected", true},
                      {"corrupt_rejected", true},
                      {"cpu_composite_export_samples_per_tile", 3},
                      {"queue_cancel_failure_latest_stale_missing", true},
                      {"large", large}})
          .toJson());
  return 0;
}
}  // namespace bts
