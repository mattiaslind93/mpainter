// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Initialized CPU-owner fixture. No GPU objects, uploaders or programs are created.
#include "NativeHeightImageFixture.hh"
#include "NativeMaterialFragmentNormalFixture.hh"
#include "NativeFragmentGeometryTest.hh"
#include "NativeMaterialFragmentImages.hh"
#include "NativeFragmentImageReadiness.hh"
#include "NativeMaterialFragmentContext.hh"
#include "NativeMaterialImages.hh"
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialResource.hh"
#include "NativeWorldNormalSurface.hh"
#include "NativeWorldNormalGeometryJob.hh"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <array>
#include <future>
#include <set>
#include <tuple>
#include <QFile>
#include <QCryptographicHash>

namespace bts::native_fragment_retained_image_test {
using native_fragment_normal_test::require;
using native_fragment_normal_test::socket;
inline auto work(auto function)
{
  auto future = std::async(std::launch::async, std::move(function));
  QElapsedTimer timer;
  timer.start();
  while (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
    require(timer.elapsed() < 120000, "Retained image CPU worker timed out");
  }
  return future.get();
}
inline void images_reject(auto function, const char *message)
{
  bool rejected = false;
  try {
    function();
  }
  catch (const NativeFragmentContextError &error) {
    rejected = error.code == NativeFragmentContextCode::Images;
  }
  require(rejected, message);
}
inline QString encoded_hash(const QString &path)
{
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Cannot read owned encoded image identity");
  return QString::fromLatin1(
      QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
}
inline std::array<float, 4> pixel(int x, int y)
{
  return {-.5f + .125f * x, 1.25f + .0625f * y, .25f + .03125f * (x + y), (x + y) % 2 ? .5f : 0.f};
}
inline void half_channels(const QString &path)
{
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "Cannot inspect own HALF EXR");
  const auto bytes = file.readAll();
  require(bytes.size() > 24 && qFromLittleEndian<quint32>(bytes.constData()) == 20000630,
          "Invalid own HALF EXR header");
  const QByteArray marker("channels\0chlist\0", 16);
  const auto at = bytes.indexOf(marker, 8);
  require(at >= 8 && at + 20 <= bytes.size(), "HALF EXR channel attribute missing");
  const auto length = qFromLittleEndian<quint32>(bytes.constData() + at + 16);
  qsizetype cursor = at + 20, end = cursor + length;
  require(end <= bytes.size(), "HALF EXR channel list truncated");
  QStringList names;
  while (cursor < end && bytes[cursor]) {
    const auto zero = bytes.indexOf(char(0), cursor);
    require(zero >= cursor && zero + 17 <= end, "Invalid HALF channel record");
    names.append(QString::fromUtf8(bytes.mid(cursor, zero - cursor)));
    cursor = zero + 1;
    require(qFromLittleEndian<quint32>(bytes.constData() + cursor) == 1,
            "HALF fixture did not encode 16-bit components");
    cursor += 16;
  }
  names.sort();
  require(cursor + 1 == end && names == QStringList{"A", "B", "G", "R"},
          "HALF fixture is not actual RGBA16 EXR");
}
inline void write_image(const QString &path, float delta = 0, bool half = false)
{
  using namespace blender;
  auto *buffer = IMB_allocImBuf(11, 7, ImBufFlags::FloatData);
  require(buffer, "Cannot allocate own rectangular image");
  const auto release = qScopeGuard([&] { IMB_freeImBuf(buffer); });
  buffer->channels = 4;
  buffer->color_mode = ImColorMode::RGBA;
  buffer->ftype = IMB_FTYPE_OPENEXR;
  if (half)
    buffer->foptions.flag |= OPENEXR_HALF;
  buffer->flags |= ImBufFlags::AlphaChannelPacked;
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 11; ++x) {
      auto value = pixel(x, y);
      value[0] += delta;
      std::copy(value.begin(), value.end(), buffer->float_data_for_write() + (y * 11 + x) * 4);
    }
  require(IMB_save_image(buffer, path.toUtf8().constData(), ImBufFlags::FloatData),
          "Cannot encode own retained image");
  if (half)
    half_channels(path);
  else
    require(height_image_fixture::exr_channels(path).keys() == QStringList{"A", "B", "G", "R"},
            "Retained image is not actual FLOAT32 RGBA");
}
inline DetachedNativeMaterial fixture(
    const QString &path,
    blender::eImageAlphaMode alpha = blender::IMA_ALPHA_CHANNEL_PACKED,
    const char *space = "Non-Color",
    bool high = true,
    bool nearest = false,
    bool generated = false)
{
  using namespace blender;
  DetachedNativeMaterial source;
  source.main = {BKE_main_new(), BKE_main_free};
  IMB_colormanagement_working_space_init_default(source.main.get());
  source.material = BKE_material_add(source.main.get(), "Retained Normal-only image");
  auto &tree = *source.material->nodetree;
  auto *image = BKE_image_load(source.main.get(), path.toUtf8().constData());
  require(image, "Cannot load own retained image ID");
  BLI_strncpy(image->colorspace_settings.name, space, sizeof(image->colorspace_settings.name));
  image->alpha_mode = alpha;
  if (high)
    image->flag |= IMA_HIGH_BITDEPTH;
  else
    image->flag &= ~IMA_HIGH_BITDEPTH;
  auto *texture = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexImage"));
  texture->id = &image->id;
  id_us_plus(texture->id);
  auto &settings = *static_cast<NodeTexImage *>(texture->storage);
  settings.projection = SHD_PROJ_FLAT;
  settings.interpolation = nearest ? SHD_INTERP_CLOSEST : SHD_INTERP_LINEAR;
  settings.extension = nearest ? SHD_IMAGE_EXTENSION_EXTEND : SHD_IMAGE_EXTENSION_REPEAT;
  auto *normal = bke::node_add_node(nullptr, tree, UString("ShaderNodeNormalMap"));
  auto &map = *static_cast<NodeShaderNormalMap *>(normal->storage);
  map.space = SHD_SPACE_TANGENT;
  map.base = SHD_NORMAL_MAP_BASE_DISPLACED;
  map.convention = SHD_NORMAL_MAP_CONVENTION_OPENGL;
  STRNCPY(map.uv_map, "RenderUV");
  static_cast<bNodeSocketValueFloat *>(socket(*normal, "Strength", true).default_value)->value =
      .65f;
  auto *pbr = bke::node_add_node(nullptr, tree, UString("ShaderNodeBsdfPrincipled"));
  auto *out = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputMaterial"));
  bke::node_add_link(
      tree, *texture, socket(*texture, "Color", false), *normal, socket(*normal, "Color", true));
  bke::node_add_link(
      tree, *normal, socket(*normal, "Normal", false), *pbr, socket(*pbr, "Normal", true));
  bke::node_add_link(tree, *pbr, socket(*pbr, "BSDF", false), *out, socket(*out, "Surface", true));
  if (generated) {
    auto *coords = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexCoord"));
    bke::node_add_link(tree,
                       *coords,
                       socket(*coords, "Generated", false),
                       *texture,
                       socket(*texture, "Vector", true));
  }
  BKE_ntree_update_after_single_tree_change(*source.main, tree);
  return source;
}
inline auto graph(const QString &path,
                  const std::atomic_bool &cancel,
                  blender::eImageAlphaMode alpha = blender::IMA_ALPHA_CHANNEL_PACKED,
                  const char *space = "Non-Color",
                  bool high = true,
                  bool nearest = false,
                  bool generated = false)
{
  auto result = finish_native_material_source(
      fixture(path, alpha, space, high, nearest, generated),
      cancel,
      nullptr,
      NativeWorldNormalSurfaceContract::PainterUndisplaced);
  prepare_optional_native_world_normal(result, cancel);
  require(result->world_normal_tree && result->world_normal_error.isEmpty(),
          "Retained image Normal extraction failed");
  return result;
}
inline QJsonObject settings(const NativeMaterialGraph &graph)
{
  using namespace blender;
  QJsonArray images, nodes;
  for (const auto &image : graph.main->images)
    images.append(QJsonObject{{"name", image.id.name},
                              {"source", int(image.source)},
                              {"type", int(image.type)},
                              {"space", image.colorspace_settings.name},
                              {"alpha", int(image.alpha_mode)},
                              {"high", bool(image.flag & IMA_HIGH_BITDEPTH)}});
  for (const auto &node : graph.material->nodetree->nodes)
    if (STREQ(node.idname, "ShaderNodeTexImage")) {
      const auto &s = *static_cast<const NodeTexImage *>(node.storage);
      nodes.append(QJsonObject{{"name", node.name},
                               {"projection", int(s.projection)},
                               {"interpolation", int(s.interpolation)},
                               {"extension", int(s.extension)},
                               {"blend", s.projection_blend},
                               {"pass", int(s.iuser.pass)},
                               {"layer", int(s.iuser.layer)},
                               {"view", int(s.iuser.view)}});
    }
  return {{"source", native_fragment_normal_test::source_state(*graph.material->nodetree)},
          {"images", images},
          {"image_node_settings", nodes}};
}
inline void raw_pixels(const NativeMaterialImageResource &resource)
{
  const auto *buffer = resource.buffer();
  require(buffer && buffer->x == 11 && buffer->y == 7 && buffer->channels == 4 &&
              buffer->float_data(),
          "Retained image dimensions/raw representation differ");
  for (int y = 0; y < 7; ++y)
    for (int x = 0; x < 11; ++x) {
      const auto expected = pixel(x, y);
      for (int c = 0; c < 4; ++c)
        require(buffer->float_data()[(y * 11 + x) * 4 + c] == expected[c],
                "Non-Color raw signed/HDR/alpha-zero component changed");
    }
}
inline blender::bNode &image_node(blender::bNodeTree &tree)
{
  for (auto &node : tree.nodes)
    if (STREQ(node.idname, "ShaderNodeTexImage"))
      return node;
  throw std::runtime_error("Image fixture lost its reachable texture");
}
}  // namespace bts::native_fragment_retained_image_test

namespace bts {
inline QJsonObject verify_native_fragment_retained_image_context()
{
  using namespace native_fragment_retained_image_test;
  require(QCoreApplication::instance() &&
              QThread::currentThread() == QCoreApplication::instance()->thread(),
          "Image fixture requires initialized CPU owner");
  require(!blender::GPU_context_active_get(), "Image CPU fixture entered with a GPU context");
  const auto compiles = blender::GPU_pass_global_compilation_count();
  std::atomic_bool cancel = false;
  QTemporaryDir directory;
  require(directory.isValid(), "Cannot create owned image fixture directory");
  const auto path = directory.filePath("normal-only.exr");
  const auto ownership = work([&] {
    using namespace blender;
    write_image(path);
    auto source = graph(path, cancel);
    const auto original = settings(*source);
    const auto original_path = QString::fromUtf8(
        reinterpret_cast<blender::Image *>(image_node(*source->material->nodetree).id)->filepath);
    const auto before = native_material_image_pool_statistics();
    require(prepare_native_material_images(*source, cancel).isEmpty() &&
                source->image_resources.empty() &&
                native_material_image_pool_statistics().decodes == before.decodes,
            "Normal-only input was silently decoded from an ordinary root");
    auto metadata = NativeFragmentImageContext::capture({source, source->world_normal_tree},
                                                        false);
    require(metadata.bindings().size() == 1 && !metadata.bindings()[0].decoded &&
                metadata.requires_default_uv(),
            "Undecoded image inventory/default UV differs");
    metadata = {};
    images_reject(
        [&] { NativeFragmentImageContext::capture({source, source->world_normal_tree}); },
        "Missing worker pixels were not typed Images");
    prepare_native_world_normal_images(source, cancel);
    require(source->world_normal_error.isEmpty(), "Actual extra-root worker decoding failed");
    auto context = NativeFragmentImageContext::capture({source, source->world_normal_tree});
    require(context.bindings().size() == 1 && context.bindings()[0].image &&
                context.bindings()[0].decoded &&
                native_material_image_pool_statistics().decodes == before.decodes + 1,
            "Extra-root owner did not perform exactly one shared decode");
    auto decoded = context.bindings()[0].decoded;
    raw_pixels(*decoded);
    auto *image = context.bindings()[0].image;
    context.validate_storage(image, 11, 7, true, NativeFragmentImagePrecision::Float32Rgba);
    int storage_rejections = 0;
    for (const auto &[w, h, single, precision] :
         std::array<std::tuple<int, int, bool, NativeFragmentImagePrecision>, 5>{
             {{10, 7, true, NativeFragmentImagePrecision::Float32Rgba},
              {11, 6, true, NativeFragmentImagePrecision::Float32Rgba},
              {11, 7, false, NativeFragmentImagePrecision::Float32Rgba},
              {11, 7, true, NativeFragmentImagePrecision::Float16Rgba},
              {11, 7, true, NativeFragmentImagePrecision::Unsupported}}})
    {
      images_reject([&] { context.validate_storage(image, w, h, single, precision); },
                    "Changed sampled storage passed retained owner metadata");
      ++storage_rejections;
    }
    images_reject(
        [&] {
          context.validate_storage(
              nullptr, 11, 7, true, NativeFragmentImagePrecision::Float32Rgba);
        },
        "Foreign sampled Image owner passed storage admission");
    ++storage_rejections;
    require(settings(*source) == original &&
                QString::fromUtf8(context.bindings()[0].image->filepath) == original_path,
            "Worker decode changed Image/sampling/NormalMap/source controls");
    auto archive = archive_native_material(
        *source, directory.filePath("retained.blend"), cancel, true);
    auto asset = native_material_asset(
        "Retained Normal image archive", archive.bytes, archive.description);
    asset.scope = AssetScope::Project;
    require(archive.textures.size() == 1 && settings(*source) == original &&
                QString::fromUtf8(context.bindings()[0].image->filepath) == original_path,
            "Normal-only archive omitted encoded dependency or mutated source");
    const auto content = asset.hash();
    const auto asset_before = asset_metadata(asset);
    const auto decoded_before = native_material_image_pool_statistics().decodes;
    require(QFile::rename(path, path + ".hidden"),
            "Cannot hide owned source image for archive restoration");
    const auto restore = qScopeGuard([&] { QFile::rename(path + ".hidden", path); });
    auto first = hydrate_native_material_resource(asset, cancel, archive.textures),
         second = hydrate_native_material_resource(asset, cancel, archive.textures);
    require(first.get() != second.get() && first->main.get() != second->main.get(),
            "Archive fixture did not restore independent source Mains");
    auto a = NativeFragmentImageContext::capture({first, first->world_normal_tree}),
         b = NativeFragmentImageContext::capture({second, second->world_normal_tree});
    require(a.bindings().size() == 1 && b.bindings().size() == 1 &&
                a.bindings()[0].decoded == decoded && b.bindings()[0].decoded == decoded &&
                a.bindings()[0].decoded->buffer() == b.bindings()[0].decoded->buffer() &&
                a.bindings()[0].decoded->buffer()->float_data() ==
                    decoded->buffer()->float_data() &&
                native_material_image_pool_statistics().decodes == decoded_before,
            "Hydration duplicated decoded pixels or lost shared ownership");
    require(settings(*first) == original && settings(*second) == original &&
                asset.hash() == content && asset_metadata(asset) == asset_before,
            "Archive/hydrate changed source settings or immutable asset metadata");
    raw_pixels(*b.bindings()[0].decoded);
    std::weak_ptr<NativeMaterialGraph> first_main = first;
    first.reset();
    require(!first_main.expired() && a.bindings()[0].image,
            "Context failed to retain its Image/Main owner");
    a = {};
    require(first_main.expired(), "Context created a source ownership cycle");
    raw_pixels(*b.bindings()[0].decoded);
    std::weak_ptr<NativeMaterialGraph> original_main = source;
    source.reset();
    require(!original_main.expired(), "Original context lost its source owner");
    context = {};
    require(original_main.expired(), "Retiring original source left a context ownership cycle");
    NativeMaterialGpuLibrary library;
    library.install("retained-images", asset.hash().toStdString(), second);
    require(library.statistics().programs == 0 && library.statistics().world_programs == 0,
            "Image metadata installation created GPU programs");
    return QJsonObject{{"width", 11},
                       {"height", 7},
                       {"typed_storage_metadata_rejections", storage_rejections},
                       {"encoded_float32_rgba", true},
                       {"normal_only_ordinary_decodes", 0},
                       {"extra_root_decodes", 1},
                       {"hydrate_additional_decodes", 0},
                       {"independent_source_mains", 2},
                       {"shared_resource_buffer_and_pixel_pointer", true},
                       {"strong_image_main_owner_no_cycle", true},
                       {"source_settings_normal_controls_preserved", true},
                       {"archive_restores_without_original_file", true},
                       {"signed_hdr_rgb_at_alpha_zero_preserved", true},
                       {"programs", 0},
                       {"world_programs", 0}};
  });
  const auto policies = work([&] {
    std::vector<std::shared_ptr<NativeMaterialGraph>> owners;
    std::set<QString> keys;
    std::shared_ptr<NativeMaterialImageResource> original;
    std::string linear_signature;
    int index = 0;
    for (const auto &[alpha, space, high] :
         std::array<std::tuple<blender::eImageAlphaMode, const char *, bool>, 5>{
             {{blender::IMA_ALPHA_CHANNEL_PACKED, "Non-Color", true},
              {blender::IMA_ALPHA_STRAIGHT, "Non-Color", true},
              {blender::IMA_ALPHA_PREMUL, "Non-Color", true},
              {blender::IMA_ALPHA_CHANNEL_PACKED, "sRGB", true},
              {blender::IMA_ALPHA_CHANNEL_PACKED, "Non-Color", false}}})
    {
      auto source = graph(path, cancel, alpha, space, high);
      const auto before = settings(*source);
      prepare_native_world_normal_images(source, cancel);
      auto context = NativeFragmentImageContext::capture({source, source->world_normal_tree});
      require(context.bindings().size() == 1 && settings(*source) == before,
              "Decode policy rewrote source metadata");
      const auto resource = context.bindings()[0].decoded;
      require(keys.insert(resource->key()).second,
              "Image decode cache aliases space/alpha/precision policy");
      if (std::string_view(space) == "Non-Color")
        raw_pixels(*resource);
      if (index++ == 0) {
        original = resource;
        linear_signature = context.signature();
      }
      owners.push_back(std::move(source));
    }
    const auto count = native_material_image_pool_statistics().decodes;
    auto nearest = graph(path, cancel, blender::IMA_ALPHA_CHANNEL_PACKED, "Non-Color", true, true);
    prepare_native_world_normal_images(nearest, cancel);
    auto context = NativeFragmentImageContext::capture({nearest, nearest->world_normal_tree});
    require(context.bindings()[0].decoded == original && context.signature() != linear_signature &&
                native_material_image_pool_statistics().decodes == count,
            "Sampling identity copied pixels or aliased different sampler controls");
    return QJsonObject{{"decode_policy_keys", 5},
                       {"alpha_modes", 3},
                       {"color_space_and_precision_keys_separate", true},
                       {"sampling_signature_changes_without_pixel_copy", true},
                       {"raw_data_components_preserved", true}};
  });
  const auto half = work([&] {
    const auto half_path = directory.filePath("retained-half.exr");
    write_image(half_path, 0, true);
    auto source = graph(half_path, cancel);
    const auto before = settings(*source);
    prepare_native_world_normal_images(source, cancel);
    auto context = NativeFragmentImageContext::capture({source, source->world_normal_tree});
    require(context.bindings().size() == 1, "HALF image binding missing");
    auto *image = context.bindings()[0].image;
    const auto resource = context.bindings()[0].decoded;
    require(bool(image->flag & blender::IMA_HIGH_BITDEPTH) &&
                bool(resource->buffer()->foptions.flag & OPENEXR_HALF) &&
                settings(*source) == before,
            "HALF retained metadata/source high-bit flag changed");
    raw_pixels(*resource);
    context.validate_storage(image, 11, 7, true, NativeFragmentImagePrecision::Float16Rgba);
    images_reject(
        [&] {
          context.validate_storage(image, 11, 7, true, NativeFragmentImagePrecision::Float32Rgba);
        },
        "HALF retained source falsely required FLOAT32 sampled storage");
    return QJsonObject{{"actual_encoded_rgba_half", true},
                       {"high_bit_flag_preserved", true},
                       {"decoded_half_options_retained", true},
                       {"stock_float16_storage_admitted", true},
                       {"float32_storage_typed_rejected", true},
                       {"raw_data_preserved", true}};
  });
  const auto missing = work([&] {
    using namespace blender;
    auto source = graph(path, cancel);
    auto &node = image_node(*source->world_normal_tree);
    auto *id = node.id;
    auto &settings = *static_cast<NodeTexImage *>(node.storage);
    auto *image = reinterpret_cast<Image *>(id);
    int rejected = 0;
    const auto check = [&](auto alter, auto restore) {
      alter();
      const auto restoration = qScopeGuard(restore);
      images_reject(
          [&] { NativeFragmentImageContext::capture({source, source->world_normal_tree}, false); },
          "Unsupported image inventory was not typed Images");
      ++rejected;
    };
    check([&] { node.id = nullptr; }, [&] { node.id = id; });
    std::unique_ptr<Main, decltype(&BKE_main_free)> foreign(BKE_main_new(), BKE_main_free);
    auto *foreign_image = BKE_image_load(foreign.get(), path.toUtf8().constData());
    require(foreign_image, "Cannot allocate independent foreign Image owner");
    check([&] { node.id = &foreign_image->id; }, [&] { node.id = id; });
    const auto user = settings.iuser;
    check([&] { settings.iuser.layer = 1; }, [&] { settings.iuser = user; });
    const auto projection = settings.projection;
    check([&] { settings.projection = 255; }, [&] { settings.projection = projection; });
    const auto kind = image->source;
    for (auto unsupported : {IMA_SRC_TILED, IMA_SRC_GENERATED, IMA_SRC_MOVIE, IMA_SRC_SEQUENCE})
      check([&] { image->source = unsupported; }, [&] { image->source = kind; });
    const auto flags = image->flag;
    check([&] { image->flag |= IMA_USE_VIEWS; }, [&] { image->flag = flags; });
    images_reject(
        [&] { NativeFragmentImageContext::capture({{}, source->world_normal_tree}, false); },
        "Missing source Main was not typed Images");
    ++rejected;
    return QJsonObject{{"typed_metadata_rejections", rejected}, {"target_touches", 0}};
  });
  std::shared_ptr<const NativeFragmentGeometrySource> captured;
  {
    native_fragment_geometry_test::Fixture scene;
    NativeFragmentGeometryRequest request;
    request.revision = 57;
    request.material_uid = scene.material->id.session_uid;
    request.tiles = {1001};
    request.target_uv = {NativeFragmentUvKind::DisplayActive, {}};
    request.uv_requests = {{NativeFragmentUvKind::DefaultRender, {}},
                           {NativeFragmentUvKind::Named, "RenderUV"},
                           {NativeFragmentUvKind::Named, "NamedUV"}};
    captured = capture_native_fragment_geometry(scene.context, nullptr, {}, request, cancel);
  }
  const auto uv = work([&] {
    auto geometry = prepare_native_fragment_geometry(captured, cancel);
    captured.reset();
    auto source = graph(path, cancel);
    prepare_native_world_normal_images(source, cancel);
    auto context = NativeFragmentImageContext::capture({source, source->world_normal_tree});
    require(context.requires_default_uv() && geometry->objects().size() == 2,
            "Default image UV/physical owner fixture differs");
    context.validate_uv(*geometry, "");
    context.validate_uv(*geometry, "RenderUV");
    const auto actual_layout = native_world_normal_source_layout(
        {source, source->world_normal_tree}, geometry.get());
    require(actual_layout.default_image_uv && actual_layout.uv_name == "RenderUV",
            "Actual source admission lost default image / NormalMap UV request");
    blender::bNode *normal = nullptr;
    for (auto &node : source->world_normal_tree->nodes)
      if (STREQ(node.idname, "ShaderNodeNormalMap"))
        normal = &node;
    require(normal, "Physical UV fixture lost its actual NormalMap");
    auto &map = *static_cast<blender::NodeShaderNormalMap *>(normal->storage);
    blender::STRNCPY(map.uv_map, "NamedUV");
    images_reject(
        [&] {
          native_world_normal_source_layout({source, source->world_normal_tree}, geometry.get());
        },
        "Actual source admission accepted a differing default image and NormalMap UV");
    blender::STRNCPY(map.uv_map, "RenderUV");
    images_reject([&] { context.validate_uv(*geometry, "NamedUV"); },
                  "Named source UV silently replaced the default image stream");
    bool distinct = false;
    for (std::size_t i = 0; i < geometry->streams().size(); ++i)
      for (const auto &vertex : geometry->vertices(i))
        distinct |= vertex.raster_uv != vertex.uv;
    require(distinct, "Painter raster UV silently replaced authored sampling UV");
    auto generated = graph(
        path, cancel, blender::IMA_ALPHA_CHANNEL_PACKED, "Non-Color", true, false, true);
    prepare_native_world_normal_images(generated, cancel);
    auto explicit_context = NativeFragmentImageContext::capture(
        {generated, generated->world_normal_tree});
    require(!explicit_context.requires_default_uv() &&
                native_world_normal_source_request({generated, generated->world_normal_tree})
                    .generated_stream,
            "Explicit Generated was silently replaced with default UV");
    bool typed = false;
    try {
      native_world_normal_source_layout({generated, generated->world_normal_tree});
    }
    catch (const NativeFragmentContextError &error) {
      typed = error.code == NativeFragmentContextCode::GeneratedOrco;
    }
    require(typed, "Explicit Generated bypassed actual ORCO admission");
    return QJsonObject{{"actual_owners", 2},
                       {"source_main_destroyed_before_prepare", true},
                       {"default_and_matching_named_stream_accepted", true},
                       {"actual_source_layout_uses_physical_uv_proof", true},
                       {"distinct_named_stream_typed_rejected", true},
                       {"raster_uv_distinct_from_authored_uv", true},
                       {"explicit_generated_keeps_orco_requirement", true}};
  });
  const auto output_route = work([&] {
    const auto changed_path = directory.filePath("changed-content.exr");
    write_image(changed_path, .25f);
    auto a = graph(path, cancel), b = graph(changed_path, cancel);
    prepare_native_world_normal_images(a, cancel);
    prepare_native_world_normal_images(b, cancel);
    const auto ca = NativeFragmentImageContext::capture({a, a->world_normal_tree}),
               cb = NativeFragmentImageContext::capture({b, b->world_normal_tree});
    require(ca.signature() != cb.signature() &&
                ca.bindings()[0].decoded != cb.bindings()[0].decoded,
            "Encoded image content replacement did not change retained identity");
    NativeMaterialGpuLibrary library;
    library.install("before", encoded_hash(path).toStdString(), a);
    library.install("after", encoded_hash(changed_path).toStdString(), b);
    NativeWorldNormalContextRoute route;
    auto job = std::make_shared<NativeWorldNormalGeometryJob>();
    const auto retained = job;
    std::uint64_t revision = 57;
    int owner_checks = 0, captures = 0, invalidations = 0;
    std::string published, uv = "RenderUV", current = "before";
    const auto queue = [&] {
      return route.queue(
          false,
          [&] {
            ++owner_checks;
            return false;
          },
          [&] {
            ++invalidations;
            ++revision;
            job.reset();
          },
          [&] {
            const auto next_uv = library.world_normal_uv(current);
            const bool generated = library.world_normal_requires_generated(current);
            if (native_fragment_geometry_layout_changed(uv, false, next_uv, generated)) {
              ++revision;
              job.reset();
            }
            uv = next_uv;
            const auto next = library.world_normal_signature(current,
                                                             library.description(current));
            const bool changed = next != published;
            published = next;
            return changed;
          },
          [&] { return !job; },
          [&] {
            ++captures;
            job = std::make_shared<NativeWorldNormalGeometryJob>();
          });
    };
    require(queue(), "Initial image output was not queued");
    const auto before = published;
    current = "after";
    require(queue() && published != before && owner_checks == 2 && captures == 0 &&
                invalidations == 0 && revision == 57 && job == retained && !job->cancelled,
            "Image content changed geometry rather than only output identity");
    require(library.statistics().programs == 0 && library.statistics().world_programs == 0,
            "Image output metadata compiled a program");
    return QJsonObject{{"actual_encoded_content_identity_changed", true},
                       {"actual_output_signature_changed", true},
                       {"owner_checks", owner_checks},
                       {"geometry_provider_calls", captures},
                       {"geometry_invalidations", invalidations},
                       {"geometry_revision_job_unchanged", true},
                       {"programs", 0},
                       {"world_programs", 0}};
  });
  NativeFragmentImageReadiness readiness;
  int advances = 0, touches = 0;
  const auto target = [&] {
    readiness.require_ready(true);
    ++touches;
  };
  images_reject(target, "Missing image readiness reached target");
  require(readiness.step(false,
                         [&] {
                           ++advances;
                           return false;
                         }) &&
              advances == 0,
          "Image-free source invoked upload callback");
  require(!readiness.step(true,
                          [&] {
                            ++advances;
                            return false;
                          }),
          "Partial upload reported ready");
  images_reject(target, "Partial upload reached target");
  require(touches == 0 && !readiness.ready(), "Pending image path changed target");
  require(readiness.step(true,
                         [&] {
                           ++advances;
                           return true;
                         }) &&
              readiness.ready(),
          "Completed upload stayed pending");
  target();
  require(readiness.step(true,
                         [&] {
                           ++advances;
                           return false;
                         }) &&
              advances == 2,
          "Ready image route repeated upload");
  readiness.reset();
  images_reject(target, "Reset upload reached target");
  require(!readiness.ready() && touches == 1, "Readiness reset or ready target count differs");
  NativeFragmentImageReadiness recovery;
  bool available = true;
  int recovery_steps = 0, recovery_targets = 0;
  const auto retained_job = std::make_shared<NativeWorldNormalGeometryJob>();
  auto current_job = retained_job;
  const std::uint64_t revision = 57;
  const auto check = [&] {
    if (!available)
      throw NativeFragmentImagePending("fixture reports actual storage unavailable");
  };
  require(recovery.validate_available(check) && recovery.ready(),
          "Ready cached image state failed validation");
  available = false;
  require(!recovery.validate_available(check) && !recovery.ready(),
          "Lost image cache failed to rearm readiness");
  require(!recovery.step(true,
                         [&] {
                           ++recovery_steps;
                           return false;
                         }),
          "Partial cache recovery became ready");
  images_reject(
      [&] {
        recovery.require_ready(true);
        ++recovery_targets;
      },
      "Partial cache recovery changed target");
  require(recovery.step(true,
                        [&] {
                          ++recovery_steps;
                          available = true;
                          return true;
                        }) &&
              recovery.validate_available(check),
          "Completed bounded cache recovery stayed pending");
  recovery.require_ready(true);
  ++recovery_targets;
  bool hard = false;
  try {
    recovery.validate_available([] {
      throw NativeFragmentContextError(NativeFragmentContextCode::Images,
                                       "storage dimensions mismatch");
    });
  }
  catch (const NativeFragmentContextError &error) {
    hard = error.code == NativeFragmentContextCode::Images;
  }
  require(
      hard && recovery.ready() && recovery_steps == 2 && recovery_targets == 1 &&
          current_job == retained_job && !retained_job->cancelled && revision == 57,
      "Hard metadata failure was treated as transient or recovery replaced geometry/program state");
  require(!blender::GPU_context_active_get() &&
              blender::GPU_pass_global_compilation_count() == compiles,
          "Retained image CPU fixture created a GPU context/program");
  return {
      {"complete", true},
      {"scope",
       "CPU retained pixels, source ownership, actual geometry UV admission and production readiness helper; no sampler/render execution"},
      {"ownership", ownership},
      {"policies", policies},
      {"half", half},
      {"missing", missing},
      {"uv", uv},
      {"output_route", output_route},
      {"readiness",
       QJsonObject{{"partial_rejected_before_target", true},
                   {"ready_callback_calls", 2},
                   {"completed_target_touches", 1},
                   {"ready_deduplicated", true},
                   {"reset_rejected_before_target", true},
                   {"cache_loss_rearmed", true},
                   {"cache_recovery_callback_calls", recovery_steps},
                   {"cache_recovery_target_touches", recovery_targets},
                   {"hard_metadata_error_not_rearmed", true},
                   {"cache_recovery_geometry_job_revision_retained", true}}},
      {"gpu_context_created", false},
      {"gpu_compilations", 0}};
}
}  // namespace bts
