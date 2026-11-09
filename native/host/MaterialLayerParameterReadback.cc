#include "MaterialLayerParameterReadback.hh"
#include "MaterialLayerParameterIO.hh"
#include "MaterialLayeringSourceSnapshots.hh"
#include "MaterialLayerUdim.hh"
#include "MaterialInspection.hh"
#include "AssetCatalog.hh"
#include "GpuProjectReadback.hh"
#include "TextureDocument.hh"
#include "PaintRecording.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "DNA_scene_types.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QScopeGuard>
#include <future>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace bts {
namespace {
void cleanup_later(QString root)
{
  if (!root.isEmpty())
    QThreadPool::globalInstance()->start(
        [root = std::move(root)] { QDir(root).removeRecursively(); });
}
}  // namespace
struct MaterialParameterExport::Impl {
  MaterialLayerParameters parameters;
  QString directory;
  GpuProjectReadback readback;
  std::vector<MaterialParameterMaskCapture> masks;
  std::atomic_bool cancelled = false;
  std::future<QString> writer;
  bool captured = false;
  void check_cancel() const
  {
    if (cancelled.load())
      throw std::runtime_error("Parameter export cancelled");
  }
  void validate_original_headers()
  {
    using namespace blender;
    using Buffer = std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)>;
    constexpr auto flags = ImBufFlags::Test | ImBufFlags::AlphaChannelPacked |
                           ImBufFlags::NoColorspaceConvert;
    std::map<std::pair<std::string, int>, std::array<int, 2>> expected;
    const auto record = [&](const FillTexture &source, int tile) {
      auto [at, fresh] = expected.emplace(std::pair{source.asset, tile},
                                          std::array<int, 2>{source.width, source.height});
      if (!fresh && at->second != std::array<int, 2>{source.width, source.height})
        throw std::invalid_argument("One source original has conflicting dimensions");
    };
    for (const auto &material : parameters.materials)
      for (const auto &source : material.textures)
        if (source) {
          if (source->tiles) {
            for (const auto &[tile, member] : *source->tiles)
              record(*member, tile);
          }
          else
            record(*source, 0);
        }
    for (const auto &original : parameters.originals)
      for (const auto &file : original.files) {
        check_cancel();
        const auto name = file.descriptor.filename.toUtf8(), path = file.descriptor.path.toUtf8();
        Buffer header(file.bytes.isEmpty() ?
                          IMB_load_image_from_filepath(path.constData(), flags) :
                          IMB_load_image_from_memory(
                              reinterpret_cast<const unsigned char *>(file.bytes.constData()),
                              file.bytes.size(),
                              flags,
                              name.constData(),
                              name.constData()),
                      IMB_freeImBuf);
        const auto size = expected.at(
            {original.asset.toString(QUuid::WithoutBraces).toStdString(), file.descriptor.tile});
        if (!header || header->x != size[0] || header->y != size[1])
          throw std::invalid_argument(
              "Parameter export requires unchanged standard encoded source images: " +
              file.descriptor.filename.toStdString());
      }
  }
  QString encode()
  {
    // Completed CPU arrays only. No Main, node, Image, mapped GL storage or live
    // document is accessed by this worker. Large storage also retires here.
    const auto release = qScopeGuard([&] {
      readback.release_cpu();
      parameters = MaterialLayerParameters{};
    });
    check_cancel();
    validate_original_headers();
    using namespace blender;
    using Buffer = std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)>;
    for (int stack = 0; stack < 3; ++stack) {
      MaterialParameterOriginal original;
      original.asset = QUuid::createUuid();
      auto mask = std::make_shared<FillTexture>();
      mask->asset = original.asset.toString(QUuid::WithoutBraces).toStdString();
      mask->name = "Evaluated opacity " + std::to_string(stack + 1);
      mask->raw = true;
      auto members = std::make_shared<std::map<int, std::shared_ptr<const FillTexture>>>();
      for (const auto &part : masks)
        if (part.stack == stack) {
          check_cancel();
          const auto &source = readback.images().at(part.index);
          Buffer image(IMB_allocImBuf(part.width, part.height, ImBufFlags::FloatData),
                       IMB_freeImBuf);
          if (!image)
            throw std::bad_alloc();
          auto *pixels = image->float_data_for_write();
          for (int y = 0; y < part.height; ++y) {
            check_cancel();
            for (int x = 0; x < part.width; ++x) {
              const auto at = std::size_t(y) * part.width + x;
              const float value = source.pixels[source.uniform ? 0 : at * 4];
              if (!std::isfinite(value) || value < 0 || value > 1)
                throw std::invalid_argument(
                    "Opacity " + std::to_string(stack + 1) +
                    ": raw image R lies outside 0..1; this parameter export does not flatten shader-clamped out-of-range masks");
              pixels[at * 4] = value;
              pixels[at * 4 + 1] = pixels[at * 4 + 2] = 0;
              pixels[at * 4 + 3] = 1;
            }
          }
          image->color_mode = ImColorMode::RGBA;
          image->ftype = IMB_FTYPE_OPENEXR;
          image->foptions.flag = R_IMF_EXR_CODEC_ZIP;
          image->foptions.compress = 15;
          IMB_colormanagement_assign_float_colorspace(
              image.get(), IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
          const auto encoded = IMB_save_image_to_buffer(
              image.get(), ImBufFlags::FloatData | ImBufFlags::AlphaChannelPacked);
          if (encoded.is_empty())
            throw std::runtime_error("Cannot encode parameter opacity EXR");
          MaterialParameterFile file;
          file.bytes = QByteArray(reinterpret_cast<const char *>(encoded.data()), encoded.size());
          file.descriptor = {
              part.tile,
              QString("opacity%1.%2.exr").arg(stack + 1).arg(part.tile),
              {},
              QString::fromLatin1(
                  QCryptographicHash::hash(file.bytes, QCryptographicHash::Sha256).toHex()),
              file.bytes.size()};
          auto member = std::make_shared<FillTexture>(*mask);
          member->hash = file.descriptor.sha256.toStdString();
          member->width = part.width;
          member->height = part.height;
          members->emplace(part.tile, std::move(member));
          original.files.push_back(std::move(file));
        }
      if (members->empty())
        throw std::logic_error("Parameter capture omitted an opacity role");
      std::vector<TextureSourceFile> files;
      for (const auto &file : original.files)
        files.push_back(file.descriptor);
      mask->hash = texture_sources_hash(files).toStdString();
      mask->width = members->begin()->second->width;
      mask->height = members->begin()->second->height;
      mask->tiles = std::move(members);
      parameters.masks[stack] = std::move(mask);
      parameters.originals.push_back(std::move(original));
    }
    check_cancel();
    QTemporaryDir stage(QFileInfo(directory).absolutePath() + "/.bts-parameter-export-XXXXXX");
    if (!stage.isValid())
      throw std::runtime_error("Cannot stage parameter export");
    write_material_layer_parameter_package(stage.path() + "/package", parameters, cancelled);
    check_cancel();
    stage.setAutoRemove(false);
    return stage.path();
  }
};
MaterialParameterExport::MaterialParameterExport(const TextureSet &set,
                                                 const std::vector<StudioAsset> &resources,
                                                 QString directory,
                                                 QString working_space,
                                                 std::array<double, 9> matrix)
    : impl_(std::make_unique<Impl>())
{
  require_material_parameter_main(set);
  if (!set.material_layering.allocated())
    throw std::invalid_argument("Apply four-material mode before parameter export");
  const QFileInfo output(directory);
  if (!output.isAbsolute() || output.exists() || output.fileName().isEmpty() ||
      !QDir(output.absolutePath()).exists())
    throw std::invalid_argument("Choose a new parameter package folder");
  auto &value = impl_->parameters;
  impl_->directory = output.absoluteFilePath();
  value.name = QString::fromStdString(set.name);
  value.enabled = set.material_layering.enabled;
  value.shader = set.shader_parameters;
  value.materials = set.material_layering.materials;
  value.working_space = std::move(working_space);
  value.scene_linear_to_xyz = matrix;
  for (int tile : set.tiles)
    value.tiles.emplace(tile, std::array<int, 2>{set.width(tile), set.rows(tile)});
  std::set<std::string> captured;
  for (int input = 0; input < 4; ++input)
    for (int channel = 0; channel < 6; ++channel)
      if (auto &source = value.materials[input].textures[channel]) {
        if (channel == 0) {
          if (source->tiles) {
            const auto transforms = material_layer_color_source_transforms(*source);
            for (const auto &[tile, transform] : transforms)
              value.color_members[input].emplace(
                  tile,
                  MaterialParameterColorMember{
                      QString::fromStdString(source->tiles->at(tile)->input_space),
                      QString::fromStdString(transform.processor)});
          }
          else {
            value.color_processors[input] = QString::fromStdString(
                material_layer_source_color_transform(*source).processor);
            if (source->source_space.empty()) {
              if (source->input_space.empty())
                throw std::invalid_argument(
                    "Color source has no actual decoded input-space identity");
              auto explicit_space = std::make_shared<FillTexture>(*source);
              explicit_space->source_space = source->input_space;
              source = std::move(explicit_space);
            }
          }
        }
        const auto found = std::find_if(
            resources.begin(), resources.end(), [&](const StudioAsset &asset) {
              return asset.id.toString(QUuid::WithoutBraces).toStdString() == source->asset;
            });
        if (found == resources.end() || !found->usages.contains("texture") ||
            found->hash().toStdString() != source->hash)
          throw std::invalid_argument(
              "Material source original is missing or changed before parameter export");
        if (!captured.insert(source->asset).second)
          continue;
        MaterialParameterOriginal original;
        original.asset = found->id;
        if (!found->texture_files.empty()) {
          require_material_layer_udim_originals(*source, found->texture_files);
          for (const auto &file : found->texture_files)
            original.files.push_back({file, {}});
        }
        else {
          if (source->tiles || found->bytes.isEmpty())
            throw std::invalid_argument("Parameter export requires original per-member files");
          original.files.push_back(
              {{0, found->filename, found->path, found->hash(), found->bytes.size()},
               found->bytes});
        }
        std::vector<TextureSourceFile> files;
        for (const auto &file : original.files)
          files.push_back(file.descriptor);
        validate_texture_sources(files);
        value.originals.push_back(std::move(original));
      }
}
MaterialParameterExport::~MaterialParameterExport()
{
  cancel();
  if (impl_->writer.valid()) {
    try {
      cleanup_later(impl_->writer.get());
    }
    catch (...) {
    }
  }  // Terminal shutdown only; normal poll gets ready futures first.
}
void MaterialParameterExport::capture(MaterialInspection &inspection, const TextureSet &set)
{
  if (impl_->captured)
    throw std::logic_error("Parameter capture already submitted");
  impl_->masks = inspection.capture_material_layer_masks(set, impl_->readback);
  impl_->captured = true;
}
void MaterialParameterExport::cancel() noexcept
{
  impl_->cancelled.store(true);
}
bool MaterialParameterExport::poll(bool current)
{
  auto &s = *impl_;
  if (!current)
    s.cancelled.store(true);
  if (s.writer.valid()) {
    if (s.writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return false;
    QString stage;
    std::exception_ptr error;
    try {
      stage = s.writer.get();
    }
    catch (...) {
      error = std::current_exception();
    }
    s.readback.clear();
    if (error) {
      if (!s.cancelled.load())
        std::rethrow_exception(error);
      return true;
    }
    if (s.cancelled.load() || !current) {
      cleanup_later(std::move(stage));
      return true;
    }
    if (QFileInfo::exists(s.directory) || !QDir().rename(stage + "/package", s.directory)) {
      cleanup_later(std::move(stage));
      throw std::runtime_error(
          "Parameter destination changed; existing files were not overwritten");
    }
    cleanup_later(std::move(stage));
    return true;
  }
  if (!s.captured)
    throw std::logic_error("Parameter capture was not submitted");
  // A cancelled readback is retired by normal nonblocking polls; no PBO wait or
  // future destructor runs from Cancel, revision change, New or project Open.
  if (!s.readback.poll())
    return false;
  s.writer = std::async(std::launch::async, [state = &s] { return state->encode(); });
  return false;
}
}  // namespace bts
