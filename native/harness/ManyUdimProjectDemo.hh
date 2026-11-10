// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#pragma once
#include "BlenderHost.hh"
#include "TextureDocument.hh"
#include "OrbitCamera.hh"
#include "NativeMaterialGpuProgram.hh"
#include "BKE_main.hh"
#include "BKE_global.hh"
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_ID.h"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include "GPU_texture.hh"
#include "MEM_guardedalloc.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QThread>
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <QOpenGLFramebufferObject>
#include <QScopeGuard>
#include <QTextStream>
#include <QCryptographicHash>
#include <set>
#include <cmath>
namespace bts {
// Diagnostic only: fixtures and captures stay in the caller's private build directory.
inline int verify_many_udim_project(BlenderHost &host,
                                    QOpenGLContext &context,
                                    QOffscreenSurface &surface,
                                    QOpenGLFramebufferObject &target,
                                    const QString &directory)
{
  using namespace blender;
  const auto require = [](bool condition, const char *message) {
    if (!condition)
      throw std::runtime_error(message);
  };
  QDir().mkpath(directory);
  std::set<int> tiles;
  for (int tile = 1001; tile <= 1017; ++tile)
    tiles.insert(tile);
  tiles.insert(2000);
  const auto mesh = directory + "/eighteen-tiles.obj";
  {
    QFile mtl(directory + "/eighteen-tiles.mtl");
    require(mtl.open(QIODevice::WriteOnly), "Cannot create UDIM material fixture");
    mtl.write("newmtl UDIMFixture\nKd 0.5 0.5 0.5\n");
  }
  {
    QFile obj(mesh);
    require(obj.open(QIODevice::WriteOnly), "Cannot create UDIM mesh fixture");
    QTextStream out(&obj);
    out << "mtllib eighteen-tiles.mtl\no UDIMFixture\nusemtl UDIMFixture\n";
    int index = 0;
    for (int tile : tiles) {
      const int u = (tile - 1001) % 10, v = (tile - 1001) / 10;
      const double x = (index % 6) * 1.2, y = (index / 6) * 1.2;
      out << "v " << x << ' ' << y << " 0\nv " << x + 1 << ' ' << y << " 0\nv " << x + 1 << ' '
          << y + 1 << " 0\nv " << x << ' ' << y + 1 << " 0\n";
      for (const auto &uv :
           std::array<std::array<double, 2>, 4>{{{.05, .05}, {.95, .05}, {.95, .95}, {.05, .95}}})
        out << "vt " << u + uv[0] << ' ' << v + uv[1] << '\n';
      out << "f";
      for (int corner = 1; corner <= 4; ++corner)
        out << ' ' << index * 4 + corner << '/' << index * 4 + corner;
      out << '\n';
      ++index;
    }
  }
  NewProjectOptions options;
  options.absolute_mesh_path = mesh.toStdString();
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  const auto environment = qEnvironmentVariable(
      "BTS_ENVIRONMENT",
      QCoreApplication::applicationDirPath() +
          "/5.2/datafiles/studiolights/world/default-studio.hdr");
  const auto imported = host.new_project(mesh.toStdString(), environment.toStdString(), &options);
  require(imported.materials.size() == 1 && imported.materials[0].tiles == tiles &&
              imported.materials[0].udim_valid,
          "Native New lost one of the eighteen UDIM tiles");
  auto scene = host.scene_snapshot();
  TextureDocument document(options, imported, scene);
  auto &set = document.set(imported.materials[0].session_uid);
  require(set.tiles == tiles && set.width() == 128 && set.uniform_editable,
          "New document did not retain eighteen editable 128px tiles");
  auto &layer = set.layers.front();
  layer.author_resolution = 128;
  int ordinal = 0;
  std::map<int, std::uint64_t> hashes;
  for (int tile : tiles) {
    std::vector<float> rgba(128 * 128 * 4);
    const std::array<float, 4> color{
        float(ordinal + 1) / 32, .25f + float(ordinal % 4) / 8, .75f - float(ordinal) / 64, 1};
    for (std::size_t i = 0; i < rgba.size(); i += 4)
      std::copy(color.begin(), color.end(), rgba.begin() + i);
    layer.pixels.assign_channel(128, 0, rgba, tile);
    hashes[tile] = layer.pixels.tile(tile).hash(0);
    ++ordinal;
  }
  host.apply_texture_set(set, 128);
  host.set_selected_material(set.material_session_uid);
  double gpu_raw_max_error = 0, gpu_stored_max_error = 0;
  QJsonArray target_formats, native_frame_hashes;
  int native_draw_calls = 0;
  OrbitCamera camera;
  require(camera.frame_bounds({scene.minimum[0], scene.minimum[1], scene.minimum[2]},
                              {scene.maximum[0], scene.maximum[1], scene.maximum[2]},
                              {target.width(), target.height()}),
          "Cannot frame native UDIM scene");
  const auto capture = [&](const QString &stage) {
    QJsonArray samples;
    std::set<QRgb> unique;
    // Match the workspace: draw initializes residency before publishing queued
    // GPU edits. Flushing first would discard a freshly filled override when the
    // first residency cap is installed, unlike the application's frame order.
    const auto draw = [&](ViewFrame frame, const QString &name) {
      QElapsedTimer publication;
      publication.start();
      do {
        context.makeCurrent(&surface);
        target.bind();
        host.draw(std::span(&frame, 1), target.width(), target.height());
        ++native_draw_calls;
        require(host.shader_failures().empty(), "UDIM viewport shader failed");
        require(publication.elapsed() < 30000, "UDIM GPU publication timed out");
        if (host.texture_updates_pending())
          QThread::msleep(1);
      } while (host.texture_updates_pending());
      const auto image = target.toImage();
      require(!image.isNull(), "UDIM framebuffer read failed");
      require(image.save(directory + "/" + stage + "-" + name + ".png"),
              "Cannot write UDIM framebuffer proof");
      return image;
    };
    ViewFrame frame;
    frame.pane = {ViewKind::Scene, 0, 0, target.width(), target.height()};
    auto view = camera.view_matrix(),
         projection = camera.projection_matrix({target.width(), target.height()});
    std::copy_n(view.constData(), 16, frame.view.begin());
    std::copy_n(projection.constData(), 16, frame.projection.begin());
    auto clip = camera.clipping_range();
    frame.clipping = {clip.first, clip.second};
    const auto native = draw(frame, "native");
    std::set<QRgb> native_colors;
    for (int y = 0; y < native.height(); y += 4)
      for (int x = 0; x < native.width(); x += 4)
        native_colors.insert(native.pixel(x, y));
    require(native_colors.size() > 20, "Native viewport is blank");
    native_frame_hashes.append(QString::fromLatin1(
        QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(native.constBits()),
                                                native.sizeInBytes()),
                                 QCryptographicHash::Sha256)
            .toHex()));
    bool half_target = false;
    host.with_gpu([&] {
      Image *image = nullptr;
      for (Material &material : G_MAIN->materials)
        if (material.id.session_uid == document.texture_sets().front().material_session_uid) {
          auto *node = material.nodetree ?
                           bke::node_find_node_by_name(*material.nodetree, "__BTS_Pixels0") :
                           nullptr;
          require(node && node->id && GS(node->id->name) == ID_IM,
                  "Native composite image binding not found");
          image = reinterpret_cast<Image *>(node->id);
          break;
        }
      require(image, "Native composite material not found");
      const auto textures = BKE_image_gpu_override_texture(image);
      require(textures.texture && *textures.texture, "UDIM native GPU override is absent");
      const auto format = GPU_texture_format(*textures.texture);
      half_target = format == gpu::TextureFormat::SFLOAT_16_16_16_16;
      require(half_target || format == gpu::TextureFormat::SFLOAT_32_32_32_32,
              "Unexpected native UDIM GPU target format");
      target_formats.append(half_target ? "RGBA16F" : "RGBA32F");
    });
    for (int tile : tiles) {
      const double u = (tile - 1001) % 10 + .5, v = (tile - 1001) / 10 + .5;
      view.setToIdentity();
      view.translate(-u, -v, -5);
      projection.setToIdentity();
      projection.ortho(-.6f, .6f, -.6f, .6f, .01f, 100.f);
      frame.pane.kind = ViewKind::Uv;
      frame.uv_center = {u, v};
      frame.uv_extent = 1.2;
      std::copy_n(view.constData(), 16, frame.view.begin());
      std::copy_n(projection.constData(), 16, frame.projection.begin());
      const auto image = draw(frame, QString::number(tile));
      const auto color = image.pixelColor(image.width() / 2, image.height() / 2);
      unique.insert(color.rgba());
      samples.append(
          QJsonObject{{"tile", tile},
                      {"center_rgb", QJsonArray{color.red(), color.green(), color.blue()}},
                      {"authored_hash", QString::number(hashes.at(tile), 16)}});
      const auto &current = document.texture_sets().front();
      const auto actual = host.diagnostic_gpu_pixel(current.material_session_uid, 0, 64, 64, tile);
      require(actual.has_value(), "Native GPU tile is absent");
      auto expected = precision_channel(
          TextureDocument::evaluate(current, 64, 64, tile).material, 0, current.precision[0]);
      for (int c = 0; c < 3; ++c) {
        require(std::isfinite((*actual)[c]), "Native GPU tile contains non-finite pixels");
        gpu_raw_max_error = std::max(gpu_raw_max_error,
                                     double(std::abs((*actual)[c] - expected[c])));
        if (half_target)
          expected[c] = float(qfloat16(expected[c]));
        const auto error = double(std::abs((*actual)[c] - expected[c]));
        gpu_stored_max_error = std::max(gpu_stored_max_error, error);
        if (error >= 2e-5) {
          std::fprintf(stderr,
                       "UDIM_GPU_ORACLE tile=%d channel=%d actual=%.9g expected=%.9g\n",
                       tile,
                       c,
                       (*actual)[c],
                       expected[c]);
          throw std::runtime_error("Native GPU tile differs from authored composite");
        }
      }
    }
    require(unique.size() == tiles.size(), "UV viewport omitted or aliased a UDIM tile");
    const auto uv = host.uv_preview_summary();
    require(uv.uv_triangles == 36 && uv.maximum_uv[1] > 99,
            "UV viewport lost the native2000 boundary tile");
    return samples;
  };
  const auto before = capture("new");
  // Native ID names reserve two type bytes and their final NUL. App-only names
  // have separate storage; this verifies the actual 255 UTF-8 byte boundary.
  for (const auto &name :
       std::array<std::string, 2>{std::string(255, 'x'), std::string(253, 'x') + "\xc3\xa9"})
    require(host.rename_material(set.material_session_uid, name) == name,
            "Native material truncated a valid 255-byte UTF-8 name");
  for (const auto &name :
       std::array<std::string, 2>{std::string(256, 'x'), std::string(254, 'x') + "\xc3\xa9"})
  {
    bool rejected = false;
    try {
      host.rename_material(set.material_session_uid, name);
    }
    catch (const std::length_error &) {
      rejected = true;
    }
    require(rejected, "Native material accepted an overflowing 256-byte UTF-8 name");
  }
  require(host.rename_material(set.material_session_uid, set.name) == set.name,
          "Cannot restore fixture material name");
  const auto saved_id = set.id;
  const auto path = directory + "/eighteen-tiles.blend";
  host.save_project(document, path.toStdString());
  document = host.open_project(path.toStdString());
  require(document.texture_sets().size() == 1, "Open lost the UDIM Texture Set");
  const auto &opened = document.texture_sets().front();
  require(opened.id == saved_id && opened.tiles == tiles && opened.width() == 128,
          "Save/Open changed the UDIM set metadata");
  for (int tile : tiles)
    require(opened.layers.front().pixels.tile(tile).hash(0) == hashes.at(tile),
            "Save/Open changed a tile's authored pixels");
  host.set_selected_material(opened.material_session_uid);
  const auto after = capture("open");
  require(before == after, "Save/Open changed UV viewport samples");
  require(native_frame_hashes[0] == native_frame_hashes[1],
          "Save/Open changed the native viewport framebuffer");
  double context_error = 0;
  host.with_gpu([&] {
    std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
    auto *tree = bke::node_tree_add_tree(main.get(), "UDIM2000 context oracle", "ShaderNodeTree");
    tree->flag |= NTREE_IS_GPU_SHADER_INTERNAL;
    auto *attribute = bke::node_add_node(nullptr, *tree, UString("ShaderNodeAttribute"));
    STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name, "BTS_Coord_UV");
    auto *aov = bke::node_add_node(nullptr, *tree, UString("ShaderNodeOutputAOV"));
    STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name, "BTS_Channel");
    const auto socket = [](bNode &node, const char *id, bool input) -> bNodeSocket & {
      for (auto &s : input ? node.inputs : node.outputs)
        if (STREQ(s.identifier, id))
          return s;
      throw std::runtime_error("UDIM context socket missing");
    };
    bke::node_add_link(
        *tree, *attribute, socket(*attribute, "Color", false), *aov, socket(*aov, "Color", true));
    bke::node_add_node(nullptr, *tree, UString("ShaderNodeOutputMaterial"));
    BKE_ntree_update_after_single_tree_change(*main, *tree);
    NativeMaterialGpuProgram program(*tree, 0, 0);
    std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)> texture(
        GPU_texture_create_2d("UDIM2000 context target",
                              2,
                              2,
                              1,
                              gpu::TextureFormat::SFLOAT_32_32_32_32,
                              GPU_TEXTURE_USAGE_GENERAL,
                              nullptr),
        GPU_texture_free);
    require(bool(texture), "UDIM context allocation failed");
    NativeMaterialGpuContext gpu_context;
    gpu_context.tile = 2000;
    program.dispatch(texture.get(), {}, gpu_context, {0, 0, 2, 2});
    auto *pixels = static_cast<float *>(GPU_texture_read(texture.get(), GPU_DATA_FLOAT, 0));
    require(pixels, "UDIM context readback failed");
    const auto release = qScopeGuard([&] { MEM_delete(pixels); });
    for (int y = 0; y < 2; ++y)
      for (int x = 0; x < 2; ++x) {
        const std::array<float, 4> expected{9 + (x + .5f) / 2, 99 + (y + .5f) / 2, 0, 1};
        for (int c = 0; c < 4; ++c) {
          const auto actual = pixels[(y * 2 + x) * 4 + c];
          require(std::isfinite(actual), "Native UDIM context contains non-finite pixels");
          context_error = std::max(context_error, double(std::abs(actual - expected[c])));
        }
      }
    require(context_error < 2e-6, "Native material GPU UDIM2000 offset differs from oracle");
  });
  QJsonArray numbers;
  for (int tile : tiles)
    numbers.append(tile);
  QJsonObject proof{{"renderer", QString::fromStdString(host.renderer())},
                    {"visible_windows", 0},
                    {"tiles", numbers},
                    {"resolution", 128},
                    {"native_new", true},
                    {"native_open", true},
                    {"exact_authored_hashes", true},
                    {"native_scene_captures", 2},
                    {"native_framebuffer_sha256", native_frame_hashes},
                    {"uv_tile_captures", 36},
                    {"native_draw_calls", native_draw_calls},
                    {"uv_samples", after},
                    {"native_gpu_target_formats", target_formats},
                    {"native_gpu_raw_max_error", gpu_raw_max_error},
                    {"native_gpu_storage_oracle_max_error", gpu_stored_max_error},
                    {"native_material_tile2000_max_error", context_error},
                    {"native_material_name_utf8_255_accepted_256_rejected", true},
                    {"shader_failures", QString::fromStdString(host.shader_failures())}};
  QFile file(directory + "/many-udim-project.json");
  require(file.open(QIODevice::WriteOnly), "Cannot write many UDIM proof");
  file.write(QJsonDocument(proof).toJson());
  std::printf("BTS_MANY_UDIM_PASS tiles=18 native_max=2000 save_open_exact=1 visible_windows=0\n");
  return 0;
}
}  // namespace bts
