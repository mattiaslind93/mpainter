// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureExtent.hh"
#include "BumpPrecision.hh"
#include "TextureExport.hh"
#include "NativeExportGuard.hh"
#include "PaintRecipe.hh"
#include "SmudgeCapture.hh"
#include "ExportConfiguration.hh"
#ifdef BTS_WITH_MATERIALX
#  include "OpenPbrTexturePackage.hh"
#endif
#include "EditTiming.hh"
#include "NativeCompositeJob.hh"
#include "ExportNaming.hh"
#include "NormalMix.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "DNA_scene_types.h"
#include "IMB_colormanagement.hh"
#include "BLI_task.hh"
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
namespace bts {
bool texture_export_has_adjustment(const TextureSet &set)
{
  PaintMetadataScope metadata;
  std::vector<const PaintPixels *> pending;
  for (const auto &layer : evaluation_layers(set)) {
    if (layer.kind == LayerKind::Paint && !layer.instance) {
      if (std::any_of(layer.blending.begin(),
                      layer.blending.end(),
                      [](auto v) { return v == LayerBlend::Passthrough; }) ||
          layer.ao_blending == LayerBlend::Passthrough ||
          layer.displacement.blending == LayerBlend::Passthrough ||
          std::any_of(layer.users.begin(), layer.users.end(), [](const auto &u) {
            return u.blending == LayerBlend::Passthrough;
          }))
        return true;
    }
    for (const auto *tiles : {&layer.pixels, &layer.ao_pixels, &layer.displacement.pixels})
      for (const auto &[tile, p] : tiles->tiles())
        pending.push_back(&p);
    for (const auto &u : layer.users)
      for (const auto &[tile, p] : u.pixels.tiles())
        pending.push_back(&p);
  }
  visit_effect_paint(set, [&](QUuid, QUuid, bool, const EffectPaint &paint) {
    for (int i = 0; i < 11; ++i)
      for (const auto &[tile, p] : paint.plane(i).tiles())
        pending.push_back(&p);
  });
  for (const auto &layer : evaluation_layers(set))
    if (layer.mask)
      for (const auto &[tile, p] : layer.mask->pixels.tiles())
        pending.push_back(&p);
  std::set<const PaintRecipe *> recipes;
  std::set<const SmudgePayload *> payloads;
  std::vector<const SmudgePayload *> captures;
  while (!pending.empty() || !captures.empty()) {
    if (!captures.empty()) {
      const auto *payload = captures.back();
      captures.pop_back();
      if (!payload || !payloads.insert(payload).second || !payload->recipe)
        continue;
      if (payload->recipe->kind == SmudgePayloadRecipe::Kind::Capture) {
        const auto &capture = static_cast<const SmudgeCapture &>(*payload->recipe);
        if (capture.adjustment)
          return true;
        for (const auto &[tile, p] : capture.source.tiles())
          pending.push_back(&p);
      }
      else {
        const auto &carry = static_cast<const SmudgeCarry &>(*payload->recipe);
        captures.push_back(carry.current.get());
        captures.push_back(carry.previous.get());
      }
      continue;
    }
    const auto *pixels = pending.back();
    pending.pop_back();
    const auto &recipe = pixels->recipe();
    if (!recipe || !recipes.insert(recipe.get()).second)
      continue;
    pending.push_back(&recipe->base);
    pending.push_back(&recipe->before);
    pending.push_back(&recipe->ink);
    captures.push_back(recipe->stamp.smudge.get());
    captures.push_back(recipe->stamp.cloned.get());
    if (recipe->source) {
      const auto inputs = recipe->source->cpu_inputs();
      pending.insert(pending.end(), inputs.begin(), inputs.end());
    }
  }
  return false;
}
namespace {
const auto &names = export_channel_suffixes;
// Export rows are independent, like the rows of the native composite job. The
// first failure is rethrown on the calling thread; a cancel stops the rest.
template<class Function>
void export_rows(int height, const std::atomic_bool &cancel, const Function &row)
{
  std::mutex guard;
  std::exception_ptr failure;
  std::atomic_bool failed{false};
  blender::threading::parallel_for(
      blender::IndexRange(height), 16, [&](const blender::IndexRange range) {
        for (const int y : range) {
          if (cancel || failed)
            return;
          try {
            row(y);
          }
          catch (...) {
            std::lock_guard lock(guard);
            if (!failure)
              failure = std::current_exception();
            failed = true;
          }
        }
      });
  if (failure)
    std::rethrow_exception(failure);
}
bool png(ExportFormat format)
{
  return format == ExportFormat::Png8 || format == ExportFormat::Png16;
}
bool integer_image(ExportFormat format)
{
  return png(format) || format == ExportFormat::Jpeg8;
}
ExportFormat output_format(const ExportRequest &request, int channel)
{
  if (channel >= 10 && request.outputs.at(channel - 10).source == ExportMapSource::MaterialId)
    return ExportFormat::Png8;
  return channel < 9  ? request.formats.at(channel).value_or(request.format) :
         channel == 9 ? request.packed->format.value_or(request.format) :
                        request.outputs.at(channel - 10).format.value_or(request.format);
}
const char *extension(ExportFormat format)
{
  return format == ExportFormat::Jpeg8 ? "jpg" : png(format) ? "png" : "exr";
}
bool enabled(const TextureSet &set, int channel)
{
  if (channel == 3 || channel == 6)
    return false;
  return channel == 8 ? set.displacement.channel :
         channel < 5  ? set.channels[channel] :
         channel == 5 ? set.ao_channel :
                        set.ao_channel || bool(set.mesh_maps[3]);
}
const ExportMapOutput *mapped_output(const ExportRequest &request, int channel)
{
  return channel >= 10 ? &request.outputs.at(channel - 10) : nullptr;
}
bool output_enabled(const ExportRequest &request, const TextureSet &set, int channel)
{
  if (channel == 3 || channel == 6)
    return false;
  if (channel >= 10 && export_map_normal(request.outputs[channel - 10].source))
    return false;
  if (request.studio && channel < 9 && !export_channel_used(set, channel))
    return false;
  return channel >= 10 ||
         (channel == 9 ? bool(request.packed) :
                         request.standard_outputs &&
                             (request.selected_channels ? request.selected_channels->at(channel) :
                                                          enabled(set, channel)));
}
bool output_color(const ExportRequest &request, int channel)
{
  const auto *map = mapped_output(request, channel);
  return map ? export_map_color(map->source) : channel == 0;
}
bool output_normal(const ExportRequest &request, int channel)
{
  const auto *map = mapped_output(request, channel);
  return map ? export_map_normal(map->source) : channel == 3 || channel == 6;
}
bool gloss_source(const ExportMapOutput *map)
{
  return map && (map->source == ExportMapSource::Glossiness ||
                 map->source == ExportMapSource::GlossinessSquared);
}
bool output_alpha(const ExportRequest &request, int channel)
{
  const auto *map = mapped_output(request, channel);
  if (map && map->source == ExportMapSource::MaterialId)
    return false;
  return output_format(request, channel) != ExportFormat::Jpeg8 &&
         (request.padding == ExportPadding::Transparent || (map && map->alpha));
}
int output_channels(int channel, const ExportRequest &request)
{
  const auto *map = mapped_output(request, channel);
  const int channels = request.studio && channel < 9 &&
                               output_format(request, channel) == ExportFormat::Exr32 ?
                           (channel == 0 ? 3 : 1) :
                       !integer_image(output_format(request, channel)) ?
                           3 :
                       map ?
                           export_map_components(map->source) :
                           (channel == 0 || channel == 3 || channel == 6 || channel == 9 ? 3 : 1);
  return channels + int(output_alpha(request, channel));
}
QString filename(const ExportTile &tile,
                 int channel,
                 const ExportRequest &request,
                 bool udim_asset = false)
{
  const auto *map = mapped_output(request, channel);
  auto pattern = QString::fromStdString(map          ? map->name :
                                        channel == 9 ? request.packed->name :
                                                       request.naming[channel]);
  if (pattern.isEmpty())
    pattern = "$textureSet_$channel";
  if (request.studio && !pattern.contains("$textureSet") && pattern.contains("$mesh") &&
      std::any_of(request.tiles.begin(), request.tiles.end(), [&](const ExportTile &other) {
        return other.set.id != tile.set.id;
      }))
    pattern.replace(pattern.indexOf("$mesh"), 5, "$mesh_$textureSet");
  if (tile.set.tiles != std::set<int>{1001} && !pattern.contains("$udim"))
    pattern += "_$udim";
  const bool openpbr = request.naming_convention == ExportNamingConvention::OpenPbr;
  const auto suffix = map          ? QString::fromLatin1(export_map_suffixes[int(map->source)]) :
                      channel == 9 ? QString("packed") :
                      channel == 6 ? QString(tile.set.normal_format == NormalMapFormat::DirectX ?
                                                 "normal_directx" :
                                                 "normal_opengl") :
                                     QString::fromLatin1(openpbr ?
                                                             openpbr_channel_suffixes[channel] :
                                                             names[channel]);
  if (channel < 9)
    pattern = export_convention_pattern(pattern,
                                        QString::fromLatin1(names[channel]),
                                        QString::fromLatin1(openpbr_channel_suffixes[channel]),
                                        openpbr);
  return export_output_name(pattern,
                            QString::fromStdString(request.mesh),
                            QString::fromStdString(tile.set.name),
                            suffix,
                            tile.number,
                            extension(output_format(request, channel)),
                            udim_asset,
                            openpbr);
}
std::array<float, 3> complete_normal(const ExportTile &tile, std::size_t index)
{
  const auto &base = tile.set.channels[3] ? tile.set.base_values : MaterialValues{};
  std::array<float, 3> pixel;
  for (int c = 0; c < 3; ++c)
    pixel[c] = tile.captured ?
                   (*tile.captured->channels[3])[index + c] :
                   tile.normal[index + c] + precision_channel(base, 3, tile.set.precision[3])[c] *
                                                (1 - tile.normal[index + 3]);
  return pixel;
}
// Studio EXR: float32 ZIP, top-down rows, grayscale data as Y. Color carries
// AP1 chromaticities with the ACES white and the ASWF interop ID lin_ap1_scene.
void write_studio_exr(
    const QString &path, const float *rgba, int width, int height, bool color, bool alpha)
{
  const int channels = (color ? 3 : 1) + int(alpha);
  OIIO::ImageSpec spec(width, height, channels, OIIO::TypeDesc::FLOAT);
  spec.channelnames = color ? std::vector<std::string>{"R", "G", "B"} :
                              std::vector<std::string>{"Y"};
  if (alpha) {
    spec.channelnames.push_back("A");
    spec.alpha_channel = channels - 1;
  }
  spec.attribute("compression", "zip");
  if (color) {
    const float ap1[8]{.713f, .293f, .165f, .830f, .128f, .044f, .32168f, .33767f};
    spec.attribute("chromaticities", OIIO::TypeDesc(OIIO::TypeDesc::FLOAT, 8), ap1);
    spec.attribute("colorInteropID", "lin_ap1_scene");
    spec.attribute("colorspace", "ACEScg");
  }
  else {
    spec.attribute("colorInteropID", "data");
    spec.attribute("colorspace", "Non-Color");
  }
  std::vector<float> rows(std::size_t(width) * height * channels);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float *source = rgba + (std::size_t(height - 1 - y) * width + x) * 4;
      float *target = rows.data() + (std::size_t(y) * width + x) * channels;
      if (color)
        for (int c = 0; c < 3; ++c)
          target[c] = source[c];
      else
        target[0] = source[0];
      if (alpha)
        target[channels - 1] = source[3];
    }
  auto output = OIIO::ImageOutput::create("openexr");
  if (!output || !output->open(path.toStdString(), spec) ||
      !output->write_image(OIIO::TypeDesc::FLOAT, rows.data()) || !output->close())
    throw std::runtime_error("Studio EXR encoder failed: " +
                             (output ? output->geterror() : std::string("no OpenEXR writer")));
}
void validate_captured(const ExportTile &tile)
{
  const auto &packet = *tile.captured;
  const auto count = resource_product(resource_product(tile.resolution, tile.rows()), 4);
  if (packet.width != tile.resolution || packet.height != tile.rows())
    throw std::invalid_argument("GPU export packet dimensions do not match the tile");
  const auto check = [&](const std::shared_ptr<const std::vector<float>> &plane) {
    if (!plane || plane->size() != count)
      throw std::invalid_argument("GPU export packet has a missing or incomplete channel");
    const float *values = plane->data();
    std::atomic_bool invalid{false};
    blender::threading::parallel_for(
        blender::IndexRange(plane->size() / 4), 65536, [&](const blender::IndexRange range) {
          bool bad = false;
          for (const std::size_t texel : range) {
            const float *v = values + texel * 4;
            bad |= !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]) ||
                   !std::isfinite(v[3]) || v[3] < 0 || v[3] > 1;
          }
          if (bad)
            invalid = true;
        });
    if (invalid)
      throw std::invalid_argument("GPU export packet has invalid values or coverage");
  };
  for (int c = 0; c < 6; ++c)
    check(packet.channels[c]);
  check(packet.mixed_ao);
  if (packet.normal_perturbation)
    check(packet.normal_perturbation);
  if (tile.set.displacement.channel)
    check(packet.channels[16]);
  for (const auto &[id, coverage] : packet.material_id_coverage)
    check(coverage);
}
// The layer walk of a Material ID texel, with everything that does not depend
// on the texel looked up once per tile instead of once per texel.
struct CapturedIdLayers {
  struct Layer {
    const std::vector<float> *coverage = nullptr;
    MaterialIdColor id;
    bool visible = false, folder = false, override_children = false;
    int slot = 0;
  };
  std::vector<Layer> layers;
  int slots = 2, root = 0;
  explicit CapturedIdLayers(const ExportTile &tile)
  {
    const auto &source = evaluation_layers(tile.set);
    int lowest = 0, highest = 0;
    for (const auto &layer : source) {
      lowest = std::min(lowest, layer.depth);
      highest = std::max(highest, layer.depth);
    }
    root = -lowest;
    slots = highest - lowest + 2;
    for (std::size_t i = 0; i < source.size(); ++i) {
      const auto &layer = source[i];
      const auto found = tile.captured->material_id_coverage.find(layer.id);
      layers.push_back(
          {found == tile.captured->material_id_coverage.end() ? nullptr : found->second.get(),
           layer.material_id.value_or(material_id_palette[i % material_id_palette.size()]),
           layer.visible,
           layer.kind == LayerKind::Folder,
           layer.material_id_override_children,
           layer.depth - lowest});
    }
  }
  std::array<float, 3> pixel(std::size_t index,
                             std::vector<std::optional<MaterialIdColor>> &ids) const
  {
    ids.assign(std::size_t(slots), std::nullopt);
    for (const auto &layer : layers) {
      const float coverage = layer.coverage ? layer.coverage->at(index) : 0;
      auto id = layer.id;
      bool contributes = layer.visible && coverage >= .5f;
      if (layer.folder) {
        const auto child = ids[layer.slot + 1];
        ids[layer.slot + 1].reset();
        if (!child)
          contributes = false;
        else if (!layer.override_children)
          id = *child;
      }
      if (contributes)
        ids[layer.slot] = id;
    }
    return ids[root] ? material_id_rgb(*ids[root]) : std::array<float, 3>{};
  }
};
MaterialValues captured_value(const ExportTile &tile, std::size_t index)
{
  const auto &p = *tile.captured;
  MaterialValues v;
  for (int c = 0; c < 3; ++c) {
    v.base_color[c] = (*p.channels[0])[index + c];
    v.normal[c] = (*p.channels[3])[index + c] * 2 - 1;
  }
  v.base_color[3] = 1;
  v.metallic = (*p.channels[1])[index];
  v.roughness = (*p.channels[2])[index];
  v.bump = (*p.channels[4])[index];
  v.ambient_occlusion = (*p.channels[5])[index];
  return v;
}
float mixed_ao(const ExportTile &tile, std::size_t index)
{
  return tile.captured ? (*tile.captured->mixed_ao)[index] : tile.ao.empty() ? 1 : tile.ao[index];
}
float displacement_value(const ExportTile &tile, std::size_t index, int x, int y)
{
  return tile.captured ? (tile.captured->channels[16] ? (*tile.captured->channels[16])[index] :
                                                        tile.set.displacement.midlevel) :
                         TextureDocument::displacement_value(tile.set, x, y, tile.number);
}
MaterialValues stored_values(const TextureSet &set, MaterialValues value)
{
  const auto color = precision_channel(value, 0, set.precision[0]);
  std::copy(color.begin(), color.end(), value.base_color.begin());
  value.metallic = precision_channel(value, 1, set.precision[1])[0];
  value.roughness = precision_channel(value, 2, set.precision[2])[0];
  value.bump = precision_bump_output(set, value.bump);
  return value;
}

}  // namespace
std::vector<ExportFile> export_file_list(const ExportRequest &request)
{
  for (const auto &tile : request.tiles) {
    require_texture_resolution_limit(tile.resolution, "Export source width");
    require_texture_resolution_limit(tile.rows(), "Export source height");
    require_texture_resolution_limit(
        tile.output_resolution ? tile.output_resolution : tile.resolution, "Export width");
    require_texture_resolution_limit(tile.output_rows(), "Export height");
    if (texture_export_has_adjustment(tile.set) && !tile.captured && !tile.gpu_capture_planned)
      throw std::runtime_error(
          "Texture export requires asynchronous GPU capture for adjustment paint");
  }
  for (const auto &tile : request.tiles)
    if (texture_export_needs_native_capture(tile.set, tile.number) && !tile.captured &&
        !tile.gpu_capture_planned)
      throw std::runtime_error(
          "Texture export requires GPU capture for native shader or spatial-filter layers; use Export flattened groups to files");
  if (int(request.naming_convention) < 0 || int(request.naming_convention) > 1)
    throw std::invalid_argument("Invalid export naming convention");
#ifdef BTS_WITH_MATERIALX
  if (request.openpbr_materialx)
    openpbr_texture_package_files(request);
#else
  if (request.openpbr_materialx)
    throw std::invalid_argument("This build has no MaterialX support");
#endif
  if (request.outputs.size() > std::size_t(std::numeric_limits<int>::max() - 10))
    throw std::overflow_error("Derived output count exceeds signed-int channel indexing");
  for (const auto &map : request.outputs)
    validate_export_map(map);
  std::set<QString> unique;
  std::vector<ExportFile> result;
  for (const auto &tile : request.tiles)
    for (int channel = 0; channel < int(10 + request.outputs.size()); ++channel)
      if (output_enabled(request, tile.set, channel))
        result.push_back({filename(tile, channel, request, false).toStdString(),
                          tile.output_resolution ? tile.output_resolution : tile.resolution,
                          output_channels(channel, request),
                          tile.output_rows(),
                          tile.set.material_session_uid,
                          tile.number,
                          channel,
                          filename(tile, channel, request, tile.set.tiles != std::set<int>{1001})
                              .toStdString()});
  for (const auto &file : result)
    if (!unique.insert(QString::fromStdString(file.name).toCaseFolded()).second)
      throw std::invalid_argument("Texture outputs produce duplicate filenames");
  return result;
}
void write_texture_export(const ExportRequest &request,
                          const std::atomic_bool &cancel,
                          const std::function<void(int, const std::string &)> &progress)
{
  const bool reference_composite = request.reference_composite ||
                                   std::getenv("BTS_EXPORT_REFERENCE") != nullptr;
  EditTiming timing("BTS_EXPORT_MS");
  using namespace blender;
  const QDir directory(QString::fromStdString(request.directory));
  if (!directory.isAbsolute() || !directory.exists())
    throw std::invalid_argument("Choose an existing absolute output directory");
  if (request.tiles.empty())
    throw std::invalid_argument("Select at least one Texture Set");
  // Admit the entire immutable packet set before creating any output file.
  for (const auto &tile : request.tiles) {
    if (tile.captured)
      validate_captured(tile);
    else if (texture_export_has_adjustment(tile.set))
      throw std::runtime_error(
          "Texture export requires completed GPU capture for adjustment paint");
    else if (texture_export_needs_native_capture(tile.set, tile.number))
      throw std::runtime_error(
          "Texture export requires GPU capture for native shader or spatial-filter layers; use Export flattened groups to files");
    else if (tile.gpu_capture_planned)
      throw std::runtime_error(
          "Texture export requires completed GPU capture for adjustment paint");
  }
  const auto files = export_file_list(request);
  std::set<std::string> unique;
  for (const auto &file : files)
    if (!unique.insert(QString::fromStdString(file.name).toCaseFolded().toStdString()).second)
      throw std::invalid_argument("Texture Set names produce duplicate output filenames");
  int completed = 0;
  struct Derived {
    NativeCompositeResult pixels;
    std::shared_ptr<TexturePadding> padding;
  };
  std::map<QUuid, Derived> derived_sets;
  for (const auto &original : request.tiles) {
    std::optional<ExportTile> prepared;
    if (original.deferred_padding && original.captured) {
      prepared = original;
      prepared->coverage = original.deferred_padding->clone()->uv_coverage(
          original.resolution, original.number, original.rows());
    }
    else if (original.deferred_padding) {
      auto found = derived_sets.find(original.set.id);
      if (found == derived_sets.end()) {
        performance::Scope source_timing("export_source_worker");
        auto padding = original.deferred_padding->clone();
        CompositeChannels channels{};
        channels[3] = true;
        channels[5] = original.set.ao_channel || bool(original.set.mesh_maps[3]);
        auto pixels = native_composite_job(
            original.set, channels[5], padding, 0, cancel, false, channels);
        if (!pixels)
          return;
        found = derived_sets
                    .emplace(original.set.id, Derived{std::move(*pixels), std::move(padding)})
                    .first;
      }
      prepared = original;
      auto &derived = found->second;
      auto &channels = derived.pixels.tiles.at(original.number);
      prepared->normal = std::move(channels[3]);
      prepared->ao = std::move(channels[5]);
      prepared->coverage = derived.padding->uv_coverage(
          original.resolution, original.number, original.rows());
    }
    const auto &tile = prepared ? *prepared : original;
    const int res = tile.resolution, height = tile.rows();
    const auto count = std::size_t(res) * height;
    if (res < 128 || height < 128 || (!tile.captured && tile.normal.size() != count * 4) ||
        (!tile.captured && !tile.ao.empty() && tile.ao.size() != count * 4))
      throw std::invalid_argument("Export source dimensions are invalid");
    // Reuse exact authoritative CPU material values across output maps when a
    // proportional share of current RAM fits; otherwise stream each channel.
    const bool ids_only = !request.standard_outputs && !request.packed &&
                          std::all_of(
                              request.outputs.begin(), request.outputs.end(), [](const auto &map) {
                                return map.source == ExportMapSource::MaterialId;
                              });
    std::vector<MaterialValues> materials;
    std::vector<float> painted_alpha;
    const bool cache_alpha = std::any_of(
        request.outputs.begin(), request.outputs.end(), [](const auto &map) { return map.alpha; });
    if (!ids_only && !tile.captured && !reference_composite &&
        resource_product(count, sizeof(MaterialValues) + (cache_alpha ? sizeof(float) : 0)) <=
            ResourceAvailability{}.ram() / 4)
    {
      performance::Scope composite_timing("export_composite");
      materials.resize(count);
      if (cache_alpha)
        painted_alpha.resize(count);
      export_rows(height, cancel, [&](int y) {
        for (int x = 0; x < res; ++x) {
          const auto result = TextureDocument::evaluate(tile.set, x, y, tile.number);
          materials[std::size_t(y) * res + x] = result.material;
          if (cache_alpha)
            painted_alpha[std::size_t(y) * res + x] = result.coverage[0];
        }
      });
      if (cancel)
        return;
    }
    // Convert once on the export worker. Never use the derived preview normal:
    // that plane already contains scalar Height and would count it twice.
    const bool needs_bump = output_enabled(request, tile.set, 4) || bool(request.packed) ||
                            std::any_of(request.outputs.begin(),
                                        request.outputs.end(),
                                        [](const auto &output) {
                                          return output.source == ExportMapSource::Bump;
                                        });
    auto normal_sources = needs_bump ? tile.normal_contributions :
                                       std::vector<ExportNormalContribution>{};
    if (needs_bump && normal_sources.empty() && tile.captured &&
        tile.captured->normal_perturbation)
      normal_sources.push_back({tile.captured->normal_perturbation});
    if (needs_bump && normal_sources.empty() && !tile.captured &&
        (tile.set.channels[3] || tile.set.mesh_maps[0]))
    {
      auto pixels = std::make_shared<std::vector<float>>(count * 4);
      export_rows(height, cancel, [&](int y) {
        for (int x = 0; x < res; ++x) {
          const auto i = std::size_t(y) * res + x;
          std::array<bool, 6> wanted{};
          wanted[3] = true;
          const auto v = materials.empty() ? TextureDocument::evaluate_channels(
                                                 tile.set, wanted, x, y, tile.number)
                                                 .material :
                                             materials[i];
          const auto n = mix_baked_normal(
              tile.set,
              normalized_normal(v.normal),
              tile.set.channels[3],
              sample_baked_normal(tile.set, (x + .5) / res, (y + .5) / height, tile.number));
          for (int c = 0; c < 3; ++c)
            (*pixels)[i * 4 + c] = n[c] * .5f + .5f;
          (*pixels)[i * 4 + 3] = 1;
        }
      });
      if (cancel)
        return;
      normal_sources.push_back({std::move(pixels)});
    }
    const bool replace_scalar = std::any_of(normal_sources.begin(),
                                            normal_sources.end(),
                                            [](const auto &s) { return s.includes_scalar_bump; });
    if (replace_scalar && normal_sources.size() != 1)
      throw std::invalid_argument("A complete surface-normal bake must be the sole normal source");
    std::vector<float> normal_bump;
    if (!normal_sources.empty()) {
      normal_bump = export_normal_bump(normal_sources, res, height, &cancel);
    }
    for (int channel = 0; channel < int(10 + request.outputs.size()); ++channel) {
      if (!output_enabled(request, tile.set, channel))
        continue;
      const auto *map = mapped_output(request, channel);
      const bool is_normal = output_normal(request, channel),
                 is_color = output_color(request, channel);
      const auto format = output_format(request, channel);
      const auto padding = map && map->source == ExportMapSource::MaterialId ?
                               ExportPadding::None :
                           format == ExportFormat::Jpeg8 &&
                                   request.padding == ExportPadding::Transparent ?
                               ExportPadding::DefaultBackground :
                               request.padding;
      if (cancel)
        return;
      const int planned_width = tile.output_resolution ? tile.output_resolution : res,
                planned_height = tile.output_rows();
      require_texture_resolution(planned_width, "Export width", 128, true);
      require_texture_resolution(planned_height, "Export height", 1, true);
      std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)> image(
          IMB_allocImBuf(res, height, ImBufFlags::FloatData), IMB_freeImBuf);
      if (!image)
        throw std::bad_alloc();
      image->color_mode = (map ? export_map_components(map->source) == 3 :
                                 channel == 0 || channel == 3 || channel == 6 || channel == 9) ?
                              ImColorMode::RGB :
                              ImColorMode::BW;
      if (output_alpha(request, channel))
        image->color_mode = image->color_mode == ImColorMode::RGB ? ImColorMode::RGBA :
                                                                    ImColorMode::BW_A;
      image->ftype = format == ExportFormat::Jpeg8 ? IMB_FTYPE_JPG :
                     png(format)                   ? IMB_FTYPE_PNG :
                                                     IMB_FTYPE_OPENEXR;
      image->foptions.flag = format == ExportFormat::Png16 ? PNG_16BIT :
                             format == ExportFormat::Exr16 ? OPENEXR_HALF :
                                                             0;
      if (format == ExportFormat::Exr16 || format == ExportFormat::Exr32)
        image->foptions.flag |= R_IMF_EXR_CODEC_ZIP;
      image->foptions.compress = 15;
      image->foptions.quality = 100;
      if (request.dither_normals && is_normal && format == ExportFormat::Png8)
        image->dither = 1.f;
      // Already encoded PNG RGB or raw data; prevent the native writer applying another display
      // transform.
      IMB_colormanagement_assign_float_colorspace(
          image.get(),
          is_color && !integer_image(format) ?
              IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_SCENE_LINEAR) :
              "Non-Color");
      auto *out = image->float_data_for_write();
      export_rows(height, cancel, [&](int y) {
        for (int x = 0; x < res; ++x) {
          const auto index = (std::size_t(y) * res + x) * 4;
          if (map && map->source == ExportMapSource::MaterialId) {
            out[index] = out[index + 1] = out[index + 2] = 0;
            out[index + 3] = 1;
            continue;
          }
          MaterialValues value;
          float alpha = 1;
          if (tile.captured) {
            value = captured_value(tile, index);
            alpha = (*tile.captured->channels[0])[index + 3];
          }
          else if (!materials.empty()) {
            value = materials[std::size_t(y) * res + x];
            if (!painted_alpha.empty())
              alpha = painted_alpha[std::size_t(y) * res + x];
          }
          else if (reference_composite || channel == 9 || map) {
            const auto evaluated = TextureDocument::evaluate(tile.set, x, y, tile.number);
            value = evaluated.material;
            alpha = evaluated.coverage[0];
          }
          else if (channel < 6) {
            std::array<bool, 6> wanted{};
            wanted[channel] = true;
            value =
                TextureDocument::evaluate_channels(tile.set, wanted, x, y, tile.number).material;
          }
          if (!normal_bump.empty())
            value.bump = (replace_scalar ? (tile.captured ? .5f : tile.set.bump_origin) :
                                           value.bump) +
                         normal_bump[index / 4];
          std::array<float, 3> pixel{};
          if (gloss_source(map))
            pixel.fill(tile.captured ? value.roughness :
                                       precision_channel(value, 2, tile.set.precision[2])[0]);
          else if (map)
            pixel = export_map_value(
                export_map_normal(map->source) ? ExportMapSource::NormalOpenGL : map->source,
                tile.captured ? value : stored_values(tile.set, value),
                complete_normal(tile, index),
                mixed_ao(tile, index));
          else if (channel == 9) {
            if (!tile.captured) {
              value.metallic = precision_channel(value, 1, tile.set.precision[1])[0];
              value.roughness = precision_channel(value, 2, tile.set.precision[2])[0];
              value.bump = precision_bump_output(tile.set, value.bump);
              value.ambient_occlusion = precision_channel(value, 5, tile.set.precision[5])[0];
            }
            pixel = packed_rgb(*request.packed,
                               value,
                               mixed_ao(tile, index),
                               displacement_value(tile, index, x, y));
          }
          else if (channel == 8) {
            pixel.fill(displacement_value(tile, index, x, y));
          }
          else if (channel == 0) {
            pixel = {value.base_color[0], value.base_color[1], value.base_color[2]};
          }
          else if (channel == 3) {
            const auto normal = normalized_normal(value.normal);
            for (int c = 0; c < 3; ++c)
              pixel[c] = normal[c] * .5f + .5f;
            if (tile.set.normal_format == NormalMapFormat::DirectX)
              pixel[1] = 1 - pixel[1];
          }
          else if (channel == 6) {
            pixel = complete_normal(tile, index);
            if (tile.set.normal_format == NormalMapFormat::DirectX)
              pixel[1] = 1 - pixel[1];
          }
          else {
            const float scalar = channel == 1 ? value.metallic :
                                 channel == 2 ? value.roughness :
                                 channel == 4 ? value.bump :
                                 channel == 5 ? value.ambient_occlusion :
                                                mixed_ao(tile, index);
            pixel.fill(scalar);
          }
          if (!tile.captured) {
            if (channel == 4)
              pixel.fill(precision_bump_output(tile.set, pixel[0]));
            else if (channel < 6)
              pixel = precision_rgb(pixel, tile.set.precision[channel]);
          }
          for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(pixel[c]))
              throw std::runtime_error("Non-finite texture value");
            out[index + c] = pixel[c];
          }
          out[index + 3] = map && map->alpha ? alpha : 1;
        }
      });
      if (cancel)
        return;
      const int output = tile.output_resolution ? tile.output_resolution : res,
                output_height = tile.output_rows();
      require_texture_resolution(output, "Export width", 128, true);
      require_texture_resolution(output_height, "Export height", 1, true);
      if (padding != ExportPadding::None) {
        std::array<float, 3> background{};
        auto base = tile.set.base_values;
        base.bump = precision_bump_base(tile.set);
        base.metallic = precision_channel(base, 1, tile.set.precision[1])[0];
        base.roughness = precision_channel(base, 2, tile.set.precision[2])[0];
        base.ambient_occlusion = precision_channel(base, 5, tile.set.precision[5])[0];
        if (gloss_source(map))
          background.fill(precision_channel(base, 2, tile.set.precision[2])[0]);
        else if (map) {
          std::array<float, 3> normal;
          for (int c = 0; c < 3; ++c)
            normal[c] = base.normal[c] * .5f + .5f;
          background = export_map_value(
              export_map_normal(map->source) ? ExportMapSource::NormalOpenGL : map->source,
              stored_values(tile.set, base),
              normal,
              1);
        }
        else if (channel == 0)
          background = {base.base_color[0], base.base_color[1], base.base_color[2]};
        else if (channel == 3 || channel == 6) {
          for (int c = 0; c < 3; ++c)
            background[c] = base.normal[c] * .5f + .5f;
          if (tile.set.normal_format == NormalMapFormat::DirectX)
            background[1] = 1 - background[1];
        }
        else if (channel == 9)
          background = packed_rgb(*request.packed, base, 1, tile.set.displacement.midlevel);
        else if (channel == 8)
          background.fill(tile.set.displacement.midlevel);
        else
          background.fill(channel == 1 ? base.metallic :
                          channel == 2 ? base.roughness :
                          channel == 4 ? base.bump :
                                         1);
        if (channel < 6)
          background = precision_rgb(background, tile.set.precision[channel]);
        else if (channel == 6)
          background = precision_rgb(background, tile.set.precision[3]);
        const auto padded = pad_texture_export(std::span(out, count * 4),
                                               tile.coverage,
                                               res,
                                               output,
                                               padding,
                                               request.padding_width,
                                               background,
                                               is_normal ? 0 : -1,
                                               height,
                                               output_height);
        if ((output != res || output_height != height) &&
            !IMB_scale(image.get(), output, output_height, IMBScaleFilter::Box))
          throw std::runtime_error("Native export resampling failed");
        std::copy(padded.begin(), padded.end(), image->float_data_for_write());
      }
      else if ((output != res || output_height != height) &&
               !IMB_scale(image.get(), output, output_height, IMBScaleFilter::Box))
        throw std::runtime_error("Native export resampling failed");
      out = image->float_data_for_write();
      if (map && map->source == ExportMapSource::MaterialId) {
        if (tile.captured && tile.captured->material_id_coverage.empty() &&
            std::any_of(evaluation_layers(tile.set).begin(),
                        evaluation_layers(tile.set).end(),
                        [](const auto &l) { return l.visible; }))
          throw std::runtime_error(
              "Material ID requires per-layer GPU coverage; retry Export textures");
        auto id_set = tile.set;
        id_set.texture_resolution = output;
        id_set.texture_height = output_height;
        id_set.tile_resolutions.erase(tile.number);
        const auto captured_ids = tile.captured ? std::optional<CapturedIdLayers>(tile) :
                                                  std::nullopt;
        export_rows(output_height, cancel, [&](int y) {
          std::vector<std::optional<MaterialIdColor>> ids;
          for (int x = 0; x < output; ++x) {
            const auto id = captured_ids ?
                                captured_ids->pixel(
                                    (std::size_t(std::min(
                                         height - 1, int((y + .5) * height / output_height))) *
                                         res +
                                     std::min(res - 1, int((x + .5) * res / output))) *
                                        4,
                                    ids) :
                                TextureDocument::material_id_pixel(id_set, x, y, tile.number);
            const auto i = (std::size_t(y) * output + x) * 4;
            for (int c = 0; c < 3; ++c)
              out[i + c] = id[c];
            out[i + 3] = 1;
          }
        });
        if (cancel)
          return;
      }
      // Derive gloss after padding/resampling. Quantize with the native PNG backend's
      // own conversion so encoded roughness and glossiness complement exactly.
      std::vector<std::uint16_t> rough16;
      std::vector<std::uint8_t> rough8;
      if (map && map->source == ExportMapSource::Glossiness && png(format)) {
        // ImBuf PNG BW first computes OCIO luminance, even for replicated gray.
        // Use that exact native step before matching its integer quantization.
        float weights[4]{};
        IMB_colormanagement_get_luminance_coefficients(weights);
        OIIO::ImageBuf source(OIIO::ImageSpec(output, output_height, 4, OIIO::TypeDesc::FLOAT),
                              out);
        const auto gray = OIIO::ImageBufAlgo::channel_sum(source, OIIO::cspan<float>(weights, 4));
        const auto pixels = std::size_t(output) * output_height;
        std::vector<float> values(pixels);
        if (!gray.get_pixels(OIIO::ROI::All(), OIIO::TypeDesc::FLOAT, values.data()))
          throw std::runtime_error("Cannot prepare native roughness quantization");
        if (format == ExportFormat::Png16) {
          rough16.resize(pixels);
          if (!OIIO::convert_pixel_values(OIIO::TypeDesc::FLOAT,
                                          values.data(),
                                          OIIO::TypeDesc::UINT16,
                                          rough16.data(),
                                          pixels))
            throw std::runtime_error("Native PNG16 quantization failed");
        }
        else {
          rough8.resize(pixels);
          if (!OIIO::convert_pixel_values(OIIO::TypeDesc::FLOAT,
                                          values.data(),
                                          OIIO::TypeDesc::UINT8,
                                          rough8.data(),
                                          pixels))
            throw std::runtime_error("Native PNG8 quantization failed");
        }
      }
      export_rows(output_height, cancel, [&](int y) {
        const auto first = std::size_t(y) * output * 4, last = first + std::size_t(output) * 4;
        for (auto index = first; index < last; index += 4) {
          if (gloss_source(map)) {
            float gloss = 1 - out[index];
            if (!rough16.empty())
              gloss = float(65535 - rough16[index / 4]) / 65535.f;
            else if (!rough8.empty())
              gloss = float(255 - rough8[index / 4]) / 255.f;
            if (map->source == ExportMapSource::GlossinessSquared)
              gloss *= gloss;
            for (int c = 0; c < 3; ++c)
              out[index + c] = gloss;
          }
          if (is_color && integer_image(format))
            IMB_colormanagement_scene_linear_to_srgb_v3(out + index, out + index);
          if (request.studio && is_color)
            IMB_colormanagement_scene_linear_to_acescg(out + index, out + index);
          if (is_normal && out[index + 3] > 0) {
            auto direction = normalized_normal(
                {out[index] * 2 - 1, out[index + 1] * 2 - 1, out[index + 2] * 2 - 1});
            for (int c = 0; c < 3; ++c)
              out[index + c] = direction[c] * .5f + .5f;
          }
          if (map && map->source == ExportMapSource::NormalDirectX)
            out[index + 1] = 1 - out[index + 1];
          if (integer_image(format))
            for (int c = 0; c < 3; ++c)
              out[index + c] = std::clamp(out[index + c], 0.f, 1.f);
        }
      });
      if (cancel)
        return;
      const auto name = filename(tile, channel, request);
      const auto destination = directory.filePath(name);
      const auto parent = QFileInfo(destination).absolutePath();
      const auto root = QFileInfo(directory.absolutePath()).canonicalFilePath();
      auto ancestor = parent;
      while (!QFileInfo::exists(ancestor)) {
        const auto next = QFileInfo(ancestor).absolutePath();
        if (next == ancestor)
          throw std::invalid_argument("Invalid export ancestor");
        ancestor = next;
      }
      const auto resolved = QFileInfo(ancestor).canonicalFilePath();
      if (resolved != root && !resolved.startsWith(root + "/"))
        throw std::invalid_argument("Export subfolder resolves outside the chosen directory");
      if (!QDir().mkpath(parent))
        throw std::runtime_error("Cannot create export subfolder");
      const auto canonical = QFileInfo(parent).canonicalFilePath();
      if (canonical != root && !canonical.startsWith(root + "/"))
        throw std::invalid_argument("Export subfolder resolves outside the chosen directory");
      const auto temporary = directory.filePath(
          ".bts-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
          ("." + QString::fromLatin1(extension(format))));
      struct RemoveTemporary {
        QString path;
        ~RemoveTemporary()
        {
          QFile::remove(path);
        }
      } cleanup{temporary};
      if (request.studio && format == ExportFormat::Exr32)
        write_studio_exr(
            temporary, out, output, output_height, is_color, output_alpha(request, channel));
      else if (!IMB_save_image(image.get(),
                               temporary.toUtf8().constData(),
                               ImBufFlags::FloatData | ImBufFlags::AlphaChannelPacked))
        throw std::runtime_error("Native image encoder failed: " + name.toStdString());
      if (cancel)
        return;
      // POSIX rename replaces one complete output atomically, including an earlier own export.
      if (std::rename(temporary.toUtf8().constData(), destination.toUtf8().constData()) != 0)
        throw std::runtime_error("Cannot install exported file: " + name.toStdString());
      progress(++completed, name.toStdString());
    }
  }
#ifdef BTS_WITH_MATERIALX
  if (request.openpbr_materialx && !cancel)
    write_openpbr_texture_package(request, files, cancel, progress);
#endif
}
}  // namespace bts
