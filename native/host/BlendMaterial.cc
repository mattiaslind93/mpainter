// SPDX-License-Identifier: GPL-3.0-or-later
#include <QStandardPaths>
#include "ResourceAvailability.hh"
#include "BlendMaterial.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialMappingNodes.hh"
#include "NativeMaterialMappingCompatibility.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialResource.hh"
#include "NativeMaterialSource.hh"
#include "BlendLibrary.hh"
#include "MaterialValuesJson.hh"
#include "SceneSnapshot.hh"
#include "TextureMemory.hh"
#include "BlendMaterialBindings.hh"
#include "MaterialSpherePreview.hh"
#include "BLI_uuid.h"
#include "DNA_asset_types.h"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QCryptographicHash>
#include "BLI_path_utils.hh"
#include "BLI_string.h"
#include "BLI_task.hh"
#include "BLI_math_matrix.hh"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include <QBuffer>
#include <QImageReader>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <algorithm>
#include <memory>
#include "BKE_blendfile_link_append.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_preview_image.hh"
#include "BKE_report.hh"
#include "BLI_linklist.h"
#include "BLI_listbase.h"
#include "BLO_readfile.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_colorband_types.h"
#include <set>
#include "IMB_colormanagement.hh"

namespace bts {
using namespace blender;
namespace {
QFileInfo source_file(const QString &path)
{
  QFileInfo file(path);
  if (!file.isAbsolute() || !file.isFile() || !file.isReadable() || file.size() <= 0 ||
      !is_blend_library(path.toStdString()))
    throw std::invalid_argument("Choose a readable absolute .blend library");
  return file;
}
void unchanged(const QFileInfo &before)
{
  QFileInfo after(before.absoluteFilePath());
  if (after.size() != before.size() || after.lastModified() != before.lastModified())
    throw std::runtime_error("Blender material library changed during import");
}
using DetachedMaterial = DetachedNativeMaterial;
DetachedMaterial append_material(const Main &live,
                                 const QString &path,
                                 const QString &name,
                                 const std::atomic_bool &cancel,
                                 bool canonical_aces = false)
{
  if (cancel.load())
    throw std::runtime_error("Blender material import cancelled");
  if (name.isEmpty() || name.toUtf8().size() > 255 || name.contains(QChar(0)))
    throw std::invalid_argument("Invalid Blender material name");
  std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
  if (canonical_aces) {
    // Native source snapshots and parameter interfaces must survive importing
    // before New and hydrating after Open. Never change the process working space
    // from this worker, and never make the source depend on the current project.
    if (IMB_colormanagement_working_space_get_named_index("ACEScg") < 0)
      throw std::runtime_error("Native material requires the ACEScg working space");
    IMB_colormanagement_working_space_init_default(main.get());
    float3x3 aces_to_default;
    for (int column = 0; column < 3; ++column) {
      float value[4]{};
      value[column] = value[3] = 1;
      IMB_colormanagement_transform_v4(value, "ACEScg", main->colorspace.scene_linear_name);
      for (int row = 0; row < 3; ++row)
        aces_to_default[column][row] = value[row];
    }
    main->colorspace.scene_linear_to_xyz = main->colorspace.scene_linear_to_xyz * aces_to_default;
    STRNCPY(main->colorspace.scene_linear_name, "ACEScg");
  }
  else
    IMB_colormanagement_working_space_convert(main.get(), &live);
  LibraryLink_Params parameters;
  BLO_library_link_params_init(&parameters, main.get(), BLO_LIBLINK_APPEND_RECURSIVE, 0);
  std::unique_ptr<BlendfileLinkAppendContext, decltype(&BKE_blendfile_link_append_context_free)>
      append(BKE_blendfile_link_append_context_new(&parameters),
             BKE_blendfile_link_append_context_free);
  BKE_blendfile_link_append_context_library_add(append.get(), path.toUtf8().constData(), nullptr);
  auto *item = BKE_blendfile_link_append_context_item_add(
      append.get(), name.toUtf8().constData(), ID_MA, nullptr);
  BKE_blendfile_link_append_context_item_library_index_enable(append.get(), item, 0);
  ReportList reports{};
  BKE_reports_init(&reports, RPT_STORE);
  struct Reports {
    ReportList &value;
    ~Reports()
    {
      BKE_reports_free(&value);
    }
  } cleanup{reports};
  BKE_blendfile_link(append.get(), &reports);
  BKE_blendfile_append(append.get(), &reports);
  auto *id = BKE_blendfile_link_append_context_item_newid_get(append.get(), item);
  if (!id || BKE_reports_contain(&reports, RPT_ERROR))
    throw std::runtime_error("Cannot append selected Blender material");
  return {std::move(main), reinterpret_cast<Material *>(id)};
}
struct PreviewDeleter {
  void operator()(PreviewImage *p) const
  {
    BKE_previewimg_free(&p);
  }
};
QImage preview_pixels(const PreviewImage &preview)
{
  for (int size : {1, 0})
    if (preview.rect[size] && preview.w[size] && preview.h[size])
      try {
        if (preview.w[size] > unsigned(std::numeric_limits<int>::max()) ||
            preview.h[size] > unsigned(std::numeric_limits<int>::max()))
          continue;
        const auto row = resource_product(preview.w[size], sizeof(std::uint32_t));
        const auto bytes = resource_product(row, preview.h[size]);
        if (row > std::uint64_t(std::numeric_limits<qsizetype>::max()) ||
            MEM_allocN_len(preview.rect[size]) < bytes)
          continue;

        QImage image(reinterpret_cast<const uchar *>(preview.rect[size]),
                     int(preview.w[size]),
                     int(preview.h[size]),
                     qsizetype(row),
                     QImage::Format_RGBA8888);
        return image.mirrored(false, true);
      }
      catch (const std::exception &) {
        continue;
      }
  return {};
}
QImage read_preview(BlendHandle *handle, const QByteArray &name)
{
  std::unique_ptr<PreviewImage, PreviewDeleter> preview(
      BLO_blendhandle_get_preview_for_id(handle, ID_MA, name.constData()));
  return preview ? preview_pixels(*preview) : QImage{};
}
QString cache_blend_image(const QString &filename,
                          const QString &hash,
                          QByteArrayView bytes,
                          const QString &source,
                          const std::atomic_bool &cancel)
{
  const auto folder = QDir(qEnvironmentVariable(
                               "BTS_RESOURCE_CACHE",
                               QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                                   "/resources"))
                          .filePath(hash);
  if (filename.contains('/') || filename.contains('\\') || filename == ".." ||
      !QDir().mkpath(folder))
    throw std::invalid_argument("Invalid Blender image cache path");
  const auto destination = QDir(folder).filePath(filename);
  const qint64 size = source.isEmpty() ? bytes.size() : QFileInfo(source).size();
  if (size <= 0)
    throw std::runtime_error("Empty Blender image source");
  if (QFileInfo(destination).size() == size && hash_texture_file(destination, &cancel) == hash)
    return destination;
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot snapshot Blender image");
  QFile input(source);
  if (!source.isEmpty() && !input.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot read Blender image source");
  QCryptographicHash digest(QCryptographicHash::Sha256);
  for (qint64 offset = 0; offset < size;) {
    if (cancel.load())
      throw std::runtime_error("Blender image snapshot cancelled");
    const auto count = std::min<qint64>(4 * 1024 * 1024, size - offset);
    QByteArray chunk;
    if (!source.isEmpty()) {
      chunk = input.read(count);
      if (chunk.size() != count)
        throw std::runtime_error("Blender image changed during snapshot");
    }
    const auto part = source.isEmpty() ? bytes.sliced(offset, count) : QByteArrayView(chunk);
    digest.addData(part);
    if (output.write(part.data(), part.size()) != part.size())
      throw std::runtime_error("Cannot write Blender image snapshot");
    offset += count;
  }
  if (QString::fromLatin1(digest.result().toHex()) != hash || !output.commit())
    throw std::runtime_error("Blender image changed or cannot be committed");
  return destination;
}
void validate_material_image(const FillTexture &texture,
                             const std::atomic_bool &cancel,
                             bool grayscale)
{
  const auto check = [&](const FillTexture &image) {
    std::atomic_bool invalid = false;
    threading::parallel_for(IndexRange(image.height), 32, [&](const IndexRange rows) {
      for (int y : rows) {
        if (cancel.load())
          throw std::runtime_error("Material import cancelled");
        for (int x = 0; x < image.width; ++x) {
          const auto &p = (*image.pixels)[std::size_t(y) * image.width + x];
          if (grayscale) {
            const float tolerance = std::max(1e-6f, std::abs(p[0]) * 1e-6f);
            if (std::abs(p[0] - p[1]) > tolerance || std::abs(p[0] - p[2]) > tolerance)
              invalid = true;
          }
          else if (p[3] != 1)
            invalid = true;
        }
      }
    });
    if (invalid)
      throw std::invalid_argument(
          grayscale ?
              "Scalar Image Texture Color needs a grayscale image; coloured RGB-to-float graphs are not transferred" :
              "Base Color image has alpha-dependent Color output; use an opaque image or Blender Ignore/Channel Packed alpha mode");
  };
  if (texture.tiles)
    for (const auto &[tile, member] : *texture.tiles)
      check(*member);
  else
    check(texture);
}
void require_grayscale_material_image(const FillTexture &image, const std::atomic_bool &cancel)
{
  validate_material_image(image, cancel, true);
}
void require_opaque_material_image(const FillTexture &image, const std::atomic_bool &cancel)
{
  validate_material_image(image, cancel, false);
}
}  // namespace
std::vector<BlendMaterialEntry> inspect_blend_materials(const QString &path, std::stop_token stop)
{
  const auto file = source_file(path);
  BlendFileReadReport reports{};
  std::unique_ptr<BlendHandle, decltype(&BLO_blendhandle_close)> handle(
      BLO_blendhandle_from_file(path.toUtf8().constData(), &reports), BLO_blendhandle_close);
  if (!handle)
    throw std::runtime_error("Cannot read Blender material library");
  int count = 0;
  std::unique_ptr<LinkNode, decltype(&BLO_datablock_info_linklist_free)> entries(
      BLO_blendhandle_get_datablock_info(handle.get(), ID_MA, false, &count),
      BLO_datablock_info_linklist_free);
  if (count < 0)
    throw std::runtime_error("Invalid Blender material count");
  std::vector<BlendMaterialEntry> result;
  for (auto *entry = entries.get(); entry; entry = entry->next) {
    if (stop.stop_requested())
      throw std::runtime_error("Material listing cancelled");
    auto &info = *static_cast<BLODataBlockInfo *>(entry->link);
    result.push_back({QString::fromUtf8(info.name),
                      info.asset_data != nullptr,
                      info.no_preview_found ? QImage{} : read_preview(handle.get(), info.name)});
    if (info.asset_data) {
      auto &entry = result.back();
      entry.catalog_id = QString::fromStdString(UUID(info.asset_data->catalog_id).str());
      entry.catalog_name = QString::fromUtf8(info.asset_data->catalog_simple_name);
      if (info.asset_data->description)
        entry.description = QString::fromUtf8(info.asset_data->description);
      for (const AssetTag &tag : info.asset_data->tags)
        entry.tags.append(QString::fromUtf8(tag.name));
    }
  }
  std::sort(
      result.begin(), result.end(), [](const auto &a, const auto &b) { return a.name < b.name; });
  unchanged(file);
  return result;
}
static PreparedBlendMaterial import_blend_channel_material(const Main &live,
                                                           const QString &path,
                                                           const QString &name,
                                                           const std::atomic_bool &cancel,
                                                           bool preview_only)
{
  const auto check = [&] {
    if (cancel.load())
      throw std::runtime_error("Blender material import cancelled");
  };
  check();
  const auto file = source_file(path);
  if (name.isEmpty() || name.toUtf8().size() > 255 || name.contains(QChar(0)))
    throw std::invalid_argument("Invalid Blender material name");
  auto detached = append_material(live, path, name, cancel);
  auto &main = detached.main;
  auto &material = *detached.material;
  auto *node = native_principled_surface(material);
  if (!node)
    throw std::invalid_argument(
        "A channel preset requires Principled BSDF directly connected to Material Output Surface");
  const auto socket = [&](const char *identifier, int type) -> bNodeSocket & {
    for (bNodeSocket &s : node->inputs)
      if (std::string_view(s.identifier) == identifier && s.type == type && s.default_value) {
        if (s.link && std::string_view(identifier) != "Base Color" &&
            std::string_view(identifier) != "Metallic" &&
            std::string_view(identifier) != "Roughness" &&
            std::string_view(identifier) != "Normal")
          throw std::invalid_argument(std::string("Unsupported linked Principled input: ") +
                                      identifier);
        return s;
      }
    throw std::invalid_argument(std::string("Missing Principled input: ") + identifier);
  };
  // A fill material is a channel preset, not a live BSDF graph. Reject active
  // additional lobes rather than silently presenting an equivalent shader.
  for (const char *input : {"Subsurface Weight",
                            "Transmission Weight",
                            "Coat Weight",
                            "Sheen Weight",
                            "Anisotropic",
                            "Thin Film Thickness"})
  {
    auto &s = socket(input, SOCK_FLOAT);
    if (static_cast<bNodeSocketValueFloat *>(s.default_value)->value != 0)
      throw std::invalid_argument(std::string("Phase 1 cannot transfer active ") + input);
  }
  auto values = new_base_values();
  auto &color = socket("Base Color", SOCK_RGBA);
  std::copy_n(static_cast<bNodeSocketValueRGBA *>(color.default_value)->value,
              4,
              values.base_color.begin());
  if (values.base_color[3] != 1 ||
      static_cast<bNodeSocketValueFloat *>(socket("Alpha", SOCK_FLOAT).default_value)->value != 1)
    throw std::invalid_argument("Phase 1 requires an opaque material");
  auto &emission = socket("Emission Color", SOCK_RGBA);
  const auto *e = static_cast<bNodeSocketValueRGBA *>(emission.default_value)->value;
  const auto strength = static_cast<bNodeSocketValueFloat *>(
                            socket("Emission Strength", SOCK_FLOAT).default_value)
                            ->value;
  if (strength != 0 && (e[0] != 0 || e[1] != 0 || e[2] != 0))
    throw std::invalid_argument("Phase 1 cannot transfer emission");
  socket("Normal", SOCK_VECTOR);
  socket("Coat Normal", SOCK_VECTOR);
  values.metallic =
      static_cast<bNodeSocketValueFloat *>(socket("Metallic", SOCK_FLOAT).default_value)->value;
  values.roughness =
      static_cast<bNodeSocketValueFloat *>(socket("Roughness", SOCK_FLOAT).default_value)->value;
  IMB_colormanagement_transform_v4(
      values.base_color.data(), main->colorspace.scene_linear_name, "ACEScg");
  values = read_material_values(material_values_json(values));
  unchanged(file);
  check();
  StudioAsset result;
  result.name = name;
  result.usages = {"material"};
  result.scope = AssetScope::Session;
  result.material = values;
  result.search_terms = {"blender", "principled"};
  if (material.id.asset_data)
    result.search_terms.append("marked-asset");
  QJsonObject data{{"type", "bts-blend-uniform-material"},
                   {"version", 1},
                   {"name", name},
                   {"source", path},
                   {"marked_asset", material.id.asset_data != nullptr},
                   {"material", material_values_json(values)}};
  BlendFileReadReport preview_report{};
  std::unique_ptr<BlendHandle, decltype(&BLO_blendhandle_close)> handle(
      BLO_blendhandle_from_file(path.toUtf8().constData(), &preview_report),
      BLO_blendhandle_close);
  const auto preview = handle ? read_preview(handle.get(), name.toUtf8()) : QImage{};
  if (!preview.isNull()) {
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    preview.save(&buffer, "PNG");
    data["preview_png"] = QString::fromLatin1(png.toBase64());
  }
  PreparedBlendMaterial prepared;
  prepared.preview_only = preview_only;
  FillProjection mapping;
  mapping.alpha_mask.fill(false);
  bool has_mapping = false;
  QJsonArray sources;
  std::map<Image *, std::size_t> images;
  for (const auto &[identifier, channel] :
       std::array<std::pair<const char *, int>, 4>{
           {{"Base Color", 0}, {"Metallic", 1}, {"Roughness", 2}, {"Normal", 3}}})
  {
    check();
    auto &input = socket(identifier,
                         channel == 0 ? SOCK_RGBA :
                         channel == 3 ? SOCK_VECTOR :
                                        SOCK_FLOAT);
    if (!input.link)
      continue;
    auto *texture_node = input.link->fromnode;
    auto *output = input.link->fromsock;
    if (channel == 3) {
      if (!texture_node || !STREQ(texture_node->idname, "ShaderNodeNormalMap") || !output ||
          !STREQ(output->identifier, "Normal"))
        throw std::invalid_argument("Normal requires native graph evaluation");
      const auto *settings = static_cast<const NodeShaderNormalMap *>(texture_node->storage);
      const auto *strength = bke::node_find_enabled_input_socket(*texture_node, "Strength");
      const auto *color = bke::node_find_enabled_input_socket(*texture_node, "Color");
      if (!settings || settings->space != SHD_SPACE_TANGENT || settings->uv_map[0] ||
          settings->convention != SHD_NORMAL_MAP_CONVENTION_OPENGL || !strength ||
          strength->link ||
          static_cast<const bNodeSocketValueFloat *>(strength->default_value)->value != 1.f ||
          !color || !color->link || (texture_node->flag & NODE_MUTED))
        throw std::invalid_argument("Normal Map requires native graph evaluation");
      texture_node = color->link->fromnode;
      output = color->link->fromsock;
    }
    if (!texture_node || std::string_view(texture_node->idname) != "ShaderNodeTexImage" ||
        (texture_node->flag & NODE_MUTED) || !output ||
        std::string_view(output->identifier) != "Color")
      throw std::invalid_argument(std::string(identifier) +
                                  ": phase 2 supports direct Image Texture Color outputs only");
    auto *image = reinterpret_cast<Image *>(texture_node->id);
    auto *settings = static_cast<NodeTexImage *>(texture_node->storage);
    if (!image || GS(image->id.name) != ID_IM || !settings ||
        (image->source != IMA_SRC_FILE && image->source != IMA_SRC_TILED))
      throw std::invalid_argument(
          std::string(identifier) +
          ": phase 2 needs a file image or packed original; generated/movie images are not supported");
    if (settings->projection != SHD_PROJ_FLAT ||
        (settings->interpolation != SHD_INTERP_LINEAR &&
         settings->interpolation != SHD_INTERP_CLOSEST) ||
        settings->extension > SHD_IMAGE_EXTENSION_EXTEND)
      throw std::invalid_argument(
          std::string(identifier) +
          ": unsupported Image Texture projection/interpolation/extension");
    for (const auto &vector : texture_node->inputs)
      if (std::string_view(vector.identifier) == "Vector" && vector.link)
        throw std::invalid_argument(
            std::string(identifier) +
            ": phase 2 uses the active UV map; remove the linked Vector graph");
    const int filter = settings->interpolation == SHD_INTERP_CLOSEST ? 2 : 1,
              wrap = settings->extension;
    if (has_mapping && (mapping.filtering != filter || mapping.wrap != wrap))
      throw std::invalid_argument(
          "Image Texture inputs need matching filtering/wrap for one fill layer");
    mapping.filtering = filter;
    mapping.wrap = wrap;
    has_mapping = true;
    auto found = images.find(image);
    if (found == images.end()) {
      StudioAsset asset;
      asset.name = QString::fromUtf8(image->id.name + 2);
      asset.usages = {"texture"};
      asset.scope = AssetScope::Session;
      asset.search_terms = {"blender-image-snapshot"};
      if (!image->packedfiles.is_empty()) {
        std::vector<SequenceMember> preview_members;

        for (const ImagePackedFile &part : image->packedfiles) {
          check();
          if (part.view != 0 || !part.packedfile || part.packedfile->size <= 0)
            throw std::invalid_argument("Invalid/multiview packed Image Texture");
          const auto *packed = part.packedfile;
          const QByteArrayView bytes(static_cast<const char *>(packed->data), packed->size);
          const auto hash = QString::fromLatin1(
              QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
          QString filename = QFileInfo(QString::fromUtf8(part.filepath)).fileName();
          const int tile = image->source == IMA_SRC_TILED ? part.tile_number : 0;
          if (filename.isEmpty())
            filename = QFileInfo(QString::fromUtf8(image->filepath)).fileName();
          if (tile && (!sequence_filename(filename) || sequence_filename(filename)->tile != tile))
            filename = QString("image.%1.%2").arg(tile).arg(QFileInfo(filename).suffix());
          if (filename.isEmpty() || filename.contains(QChar::Null))
            throw std::invalid_argument("Packed image has no usable source filename");
          if (preview_only) {
            if (tile)
              preview_members.push_back(
                  {tile, filename, QByteArray::fromRawData(bytes.data(), bytes.size())});
            else {
              asset.filename = filename;
              asset.bytes = QByteArray::fromRawData(bytes.data(), bytes.size());
            }
          }
          else {
            const auto cached = cache_blend_image(filename, hash, bytes, {}, cancel);
            asset.texture_files.push_back({tile, filename, cached, hash, packed->size});
          }
        }
        if (preview_only) {
          if (!preview_members.empty()) {
            std::sort(preview_members.begin(),
                      preview_members.end(),
                      [](const auto &a, const auto &b) { return a.tile < b.tile; });
            std::uint64_t size = 0;
            for (const auto &member : preview_members)
              size += member.bytes.size();
            asset.bytes = make_image_sequence(preview_members);
            asset.filename = asset.name + ".btsseq";
          }
        }
        else {
          std::sort(asset.texture_files.begin(),
                    asset.texture_files.end(),
                    [](const auto &a, const auto &b) { return a.tile < b.tile; });
          validate_texture_sources(asset.texture_files);
        }
      }
      else {
        char absolute[1024];
        STRNCPY(absolute, image->filepath);
        BLI_path_abs(absolute, path.toUtf8().constData());
        const auto source = reference_image_resource(
            QString::fromUtf8(absolute), image->source == IMA_SRC_TILED, &cancel);
        asset.texture_files = source.files;
        if (!preview_only)
          for (auto &source : asset.texture_files)
            source.path = cache_blend_image(
                source.filename, source.sha256, {}, source.path, cancel);
      }
      if (image->source == IMA_SRC_TILED && !(preview_only && asset.texture_files.empty()))
        for (const ImageTile &tile : image->tiles)
          if (std::none_of(asset.texture_files.begin(),
                           asset.texture_files.end(),
                           [&](const auto &file) { return file.tile == tile.tile_number; }))
            throw std::runtime_error("Missing Blender image tile " +
                                     std::to_string(tile.tile_number) + ": " +
                                     asset.name.toStdString());
      if (!asset.texture_files.empty())
        asset.filename = asset.texture_files.size() > 1 ? asset.name + ".btsseq" :
                                                          asset.texture_files.front().filename;
      found = images.emplace(image, prepared.textures.size()).first;
      prepared.textures.push_back(std::move(asset));
    }
    auto &asset = prepared.textures[found->second];
    TextureDecodeControl control;
    control.cancel = &cancel;
    const bool raw = channel != 0;
    const std::string space = raw ? "" : std::string(image->colorspace_settings.name);
    auto decoded = decode_fill_texture(asset, raw, space, control);
    if (channel == 3 && !IMB_colormanagement_space_name_is_data(image->colorspace_settings.name))
      throw std::invalid_argument("Normal image colorspace requires native graph evaluation");
    if (raw && channel != 3)
      require_grayscale_material_image(*decoded, cancel);
    else if (image->alpha_mode != IMA_ALPHA_IGNORE &&
             image->alpha_mode != IMA_ALPHA_CHANNEL_PACKED &&
             !IMB_colormanagement_space_name_is_data(image->colorspace_settings.name))
      require_opaque_material_image(*decoded, cancel);
    fill_channel_texture(mapping, channel) = decoded;
    if (decoded->tiles)
      mapping.mode = 6;
    if (channel == 0)
      result.material->base_color = {1, 1, 1, 1};
    sources.append(QJsonObject{
        {"channel", channel}, {"sha256", asset.hash()}, {"space", QString::fromStdString(space)}});
  }
  if (has_mapping) {
    result.fill_preset = std::move(mapping);
    result.search_terms.append("blender-images");
    data["version"] = 2;
    data["image_sources"] = sources;
    data["filter"] = result.fill_preset->filtering;
    data["wrap"] = result.fill_preset->wrap;
    data["material"] = material_values_json(*result.material);
  }
  if (!data.contains("preview_png")) {
    auto generated = material_sphere_preview(
        *result.material, result.fill_preset ? &*result.fill_preset : nullptr, 64);
    QByteArray png;
    QBuffer output(&png);
    output.open(QIODevice::WriteOnly);
    generated.save(&output, "PNG");
    data["preview_png"] = QString::fromLatin1(png.toBase64());
    data["generated_preview"] = true;
  }
  // fromRawData descriptors may alias the detached Main's packed memory. A
  // preview result never publishes them or retains full decoded image arrays.
  if (preview_only) {
    prepared.textures.clear();
    result.fill_preset.reset();
  }
  unchanged(file);
  check();
  result.bytes = QJsonDocument(data).toJson(QJsonDocument::Compact);
  result.filename = "blend-material-" + result.hash().left(16) + ".btsuniform";
  prepared.material = std::move(result);
  return prepared;
}
PreparedBlendMaterial import_blend_material(const Main &live,
                                            const QString &path,
                                            const QString &name,
                                            const std::atomic_bool &cancel,
                                            bool preview_only)
{
  try {
    return import_blend_channel_material(live, path, name, cancel, preview_only);
  }
  catch (const std::invalid_argument &) {
    if (cancel.load())
      throw;
    auto resource = prepare_native_material_resource(live, path, name, cancel);
    return {std::move(resource.asset),
            std::move(resource.textures),
            preview_only,
            std::move(resource.graph)};
  }
}
NativeMaterialDescription describe_native_material_source(Material &material,
                                                          const std::atomic_bool &cancel)
{
  NativeMaterialDescription result;
  result.material = QString::fromUtf8(material.id.name + 2);
  auto *output = native_material_output(material);
  auto *surface = output ? bke::node_find_enabled_input_socket(*output, "Surface") : nullptr;
  if (!surface || !surface->link || !surface->link->fromnode)
    throw std::invalid_argument("Material has no connected Surface output");
  auto &top = *surface->link->fromnode;
  result.surface_type = top.idname;
  if (top.id && GS(top.id->name) == ID_NT)
    result.group = top.id->name + 2;
  std::set<const bNodeTree *> visited;
  std::set<QString> types;
  const auto scan = [&](auto &&self, const bNodeTree &tree) -> void {
    if (!visited.insert(&tree).second)
      return;
    for (const auto &node : tree.nodes) {
      if (cancel.load())
        throw std::runtime_error("Material inspection cancelled");
      types.insert(QString::fromUtf8(node.idname));
      if (STREQ(node.idname, "ShaderNodeGroup") && node.id && GS(node.id->name) == ID_NT)
        self(self, *reinterpret_cast<const bNodeTree *>(node.id));
    }
  };
  scan(scan, *material.nodetree);
  for (const auto &type : types)
    result.node_types.append(type);
  for (const auto &socket : top.inputs) {
    if (!socket.default_value || socket.flag & SOCK_UNAVAIL)
      continue;
    NativeMaterialParameter p;
    p.identifier = socket.identifier;
    p.name = socket.name;
    if (socket.type == SOCK_FLOAT) {
      p.type = NativeParameterType::Scalar;
      const auto &v = *static_cast<const bNodeSocketValueFloat *>(socket.default_value);
      p.value[0] = v.value;
      p.minimum = v.min;
      p.maximum = v.max;
    }
    else if (socket.type == SOCK_VECTOR) {
      p.type = NativeParameterType::Vector;
      const auto &v = *static_cast<const bNodeSocketValueVector *>(socket.default_value);
      std::copy_n(v.value, 3, p.value.begin());
      p.minimum = v.min;
      p.maximum = v.max;
    }
    else if (socket.type == SOCK_RGBA) {
      p.type = NativeParameterType::Color;
      const auto &v = *static_cast<const bNodeSocketValueRGBA *>(socket.default_value);
      std::copy_n(v.value, 4, p.value.begin());
      p.minimum = 0;
      p.maximum = 65504;
    }
    else {
      result.warnings.append(p.name + ": input type is not editable yet");
      continue;
    }
    if (socket.link && socket.link->fromnode) {
      const auto &from = *socket.link->fromnode;
      p.source_node = from.idname;
      p.source_socket = socket.link->fromsock ? socket.link->fromsock->identifier : "";
      p.mapping = p.type == NativeParameterType::Vector &&
                  (STREQ(from.idname, "ShaderNodeTexCoord") ||
                   STREQ(from.idname, "ShaderNodeMapping") ||
                   (socket.link->fromsock &&
                    native_mapping_vector_math_supported(from, *socket.link->fromsock)));
      if (STREQ(from.idname, "ShaderNodeValToRGB") && from.storage) {
        const auto &ramp = *static_cast<const ColorBand *>(from.storage);
        QJsonArray stops;
        for (int i = 0; i < ramp.tot; ++i) {
          const auto &stop = ramp.data[i];
          stops.append(QJsonObject{{"position", stop.pos},
                                   {"color", QJsonArray{stop.r, stop.g, stop.b, stop.a}}});
        }
        p.ramp = {{"interpolation", int(ramp.ipotype)},
                  {"color_mode", int(ramp.color_mode)},
                  {"hue_interpolation", int(ramp.ipotype_hue)},
                  {"stops", stops}};
      }
      p.editable = p.mapping || !p.ramp.isEmpty() || STREQ(from.idname, "ShaderNodeTexImage");
      if (!p.editable)
        result.warnings.append(p.name + ": linked " + p.source_node +
                               " is retained, not replaced by a constant");
    }
    result.parameters.push_back(std::move(p));
  }
  if (result.surface_type != "ShaderNodeGroup" &&
      result.surface_type != "ShaderNodeBsdfPrincipled" &&
      result.surface_type != "ShaderNodeEmission")
    result.warnings.append("Surface " + result.surface_type +
                           " requires explicit shader-channel extraction");
  result.validate();
  return result;
}
NativeMaterialDescription inspect_blend_material_graph(const Main &live,
                                                       const QString &path,
                                                       const QString &name,
                                                       const std::atomic_bool &cancel)
{
  const auto file = source_file(path);
  auto detached = append_material(live, path, name, cancel);
  auto result = describe_native_material_source(*detached.material, cancel);
  unchanged(file);
  return result;
}
std::shared_ptr<NativeMaterialGraph> finish_native_material_source(
    DetachedNativeMaterial detached,
    const std::atomic_bool &cancel,
    const NativeMaterialDescription *retained_source,
    NativeWorldNormalSurfaceContract surface_contract)
{
  if (!detached.main || !detached.material || !detached.material->nodetree)
    throw std::invalid_argument("Detached native source is missing");
  auto description = describe_native_material_source(*detached.material, cancel);
  if (retained_source)
    preserve_legacy_vector_math_mapping_policy(description, *retained_source);
  auto result = std::make_shared<NativeMaterialGraph>(std::move(detached.main),
                                                      detached.material,
                                                      std::move(description),
                                                      cancel,
                                                      surface_contract);
  for (int column = 0; column < 3; ++column) {
    float to[4]{}, from[4]{};
    to[column] = from[column] = 1;
    to[3] = from[3] = 1;
    IMB_colormanagement_transform_v4(to, result->main->colorspace.scene_linear_name, "ACEScg");
    IMB_colormanagement_transform_v4(from, "ACEScg", result->main->colorspace.scene_linear_name);
    for (int row = 0; row < 3; ++row) {
      result->color_to_aces[column * 3 + row] = to[row];
      result->color_from_aces[column * 3 + row] = from[row];
    }
  }
  result->description.color_to_aces = result->color_to_aces;
  result->description.color_from_aces = result->color_from_aces;
  return result;
}
std::shared_ptr<NativeMaterialGraph> prepare_native_material_graph(
    const Main &live,
    const QString &path,
    const QString &name,
    const std::atomic_bool &cancel,
    NativeWorldNormalSurfaceContract surface_contract)
{
  const auto file = source_file(path);
  auto result = finish_native_material_source(
      append_material(live, path, name, cancel, true), cancel, nullptr, surface_contract);
  prepare_optional_native_world_normal(result, cancel);
  unchanged(file);
  return result;
}
DetachedNativeMaterial read_native_material_source(const QString &path,
                                                   const QString &name,
                                                   const std::atomic_bool &cancel)
{
  if (cancel.load())
    throw std::runtime_error("Native material restore cancelled");
  const auto file = source_file(path);
  ReportList reports{};
  BKE_reports_init(&reports, RPT_STORE);
  struct Reports {
    ReportList &value;
    ~Reports()
    {
      BKE_reports_free(&value);
    }
  } cleanup{reports};
  BlendFileReadReport report{};
  report.reports = &reports;
  // FileGlobal is read even with SKIP_ALL: obtain the encoded working space
  // without loading IDs, user settings, or running ID versioning. Keep the
  // established detached-library append lifecycle, targeting that exact space.
  std::unique_ptr<BlendFileData, decltype(&BLO_blendfiledata_free)> data(
      BLO_read_from_file(path.toUtf8().constData(), BLO_READ_SKIP_ALL, &report),
      BLO_blendfiledata_free);
  if (!data || !data->main || BKE_reports_contain(&reports, RPT_ERROR))
    throw std::runtime_error("Cannot read native material archive working space");
  if (cancel.load())
    throw std::runtime_error("Native material restore cancelled");
  auto detached = append_material(*data->main, path, name, cancel);
  data.reset();
  unchanged(file);
  return detached;
}
std::shared_ptr<NativeMaterialGraph> restore_native_material_graph(
    const QString &path,
    const QString &name,
    const std::atomic_bool &cancel,
    NativeWorldNormalSurfaceContract surface_contract)
{
  const auto file = source_file(path);
  auto result = finish_native_material_source(
      read_native_material_source(path, name, cancel), cancel, nullptr, surface_contract);
  prepare_optional_native_world_normal(result, cancel);
  unchanged(file);
  return result;
}
QImage blend_material_datablock_preview(const Material &material)
{
  return material.preview ? preview_pixels(*material.preview) : QImage{};
}
QImage blend_material_source_preview(const QString &file, const QString &material)
{
  const auto before = source_file(file);
  BlendFileReadReport report{};
  std::unique_ptr<BlendHandle, decltype(&BLO_blendhandle_close)> handle(
      BLO_blendhandle_from_file(file.toUtf8().constData(), &report), BLO_blendhandle_close);
  const auto result = handle ? read_preview(handle.get(), material.toUtf8()) : QImage{};
  unchanged(before);
  return result;
}
void set_native_material_preview(StudioAsset &asset, const QImage &preview)
{
  if (!is_native_material_asset(asset) || preview.isNull())
    throw std::invalid_argument("Missing native material preview");
  QByteArray png;
  QBuffer output(&png);
  output.open(QIODevice::WriteOnly);
  if (!preview.save(&output, "PNG"))
    throw std::runtime_error("Cannot encode native material preview");
  const auto bundle = read_native_material_bundle(asset.bytes);
  asset.bytes = native_material_bundle_bytes(
      bundle.blend, bundle.images, native_material_bundle_provenance(asset.bytes), png);
  if (!asset.search_terms.contains("preview:color-metallic-roughness-sphere"))
    asset.search_terms.append("preview:color-metallic-roughness-sphere");
}
QImage blend_material_preview(const StudioAsset &asset)
{
  if (!asset.filename.endsWith(".btsuniform") && !is_native_material_asset(asset))
    return {};
  try {
    QByteArray png;
    if (is_native_material_asset(asset)) {
      png = native_material_bundle_preview(asset.bytes);
      if (png.isEmpty()) {
        // Legacy archives may retain the original PreviewImage. This runs only on
        // the shared Shelf thumbnail worker, never a synchronous row paint.
        const auto bundle = read_native_material_bundle(asset.bytes);
        if (bundle.blend.size() > std::numeric_limits<int>::max())
          return {};
        BlendFileReadReport report{};
        std::unique_ptr<BlendHandle, decltype(&BLO_blendhandle_close)> handle(
            BLO_blendhandle_from_memory(
                bundle.blend.constData(), int(bundle.blend.size()), &report),
            BLO_blendhandle_close);
        return handle && asset.fill_preset && asset.fill_preset->native_material ?
                   read_preview(handle.get(),
                                asset.fill_preset->native_material->material.toUtf8()) :
                   QImage{};
      }
    }
    else {

      QJsonParseError error;
      const auto document = QJsonDocument::fromJson(asset.bytes, &error);
      if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
      const auto data = document.object();
      if (data["type"] != "bts-blend-uniform-material" ||
          (data["version"] != 1 && data["version"] != 2) || !data["preview_png"].isString())
        return {};
      const auto encoded = data["preview_png"].toString();
      png = QByteArray::fromBase64(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    }
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::ReadOnly))
      return {};
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    if (!size.isValid())
      return {};
    const auto bits = std::max<std::uint64_t>(
        32, QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel());
    const auto row = resource_product(resource_sum(resource_product(size.width(), bits), 31) / 32,
                                      4);

    return reader.read();
  }
  catch (const std::exception &) {
    return {};
  }
}
}  // namespace bts
