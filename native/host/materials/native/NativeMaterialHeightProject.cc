#include <QtCore>
#include "NativeMaterialHeightProject.hh"
#include "NativeMaterialInstance.hh"
#include "NativeMaterialResource.hh"
#include "TextureDocument.hh"
#include <algorithm>
#include <map>
#include <set>

namespace bts {
bool native_height_project(const TextureDocument &document)
{
  for (const auto &set : document.texture_sets())
    for (const auto &layer : evaluation_layers(set))
      if (layer.fill.native_material &&
          layer.fill.native_material->height_policy != NativeHeightPolicy::None)
        return true;
  return false;
}
std::vector<NativeHeightProjectSource> prepare_native_height_project(
    const TextureDocument &document,
    const std::vector<StudioAsset> &resources,
    const std::atomic_bool &cancel)
{
  if (!native_height_project(document))
    return {};
  for (const auto &set : document.texture_sets())
    for (const auto &layer : evaluation_layers(set))
      if (layer.fill.native_material)
        require_native_height_fill(layer.fill.native_material->height_policy,
                                   set.bump_origin,
                                   layer.fill.mode,
                                   layer.channels[3]);
  std::map<QUuid, bool> required;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : evaluation_layers(set))
      if (layer.fill.native_material) {
        const QUuid id(QString::fromStdString(layer.fill.material_resource));
        if (id.isNull())
          throw std::invalid_argument("Saved native Height instance has no source UUID");
        required[id] |= layer.fill.native_material->height_policy ==
                        NativeHeightPolicy::RawAuthoredFloatV1;
      }

  std::vector<NativeHeightProjectSource> prepared;
  prepared.reserve(required.size());
  for (const auto &[id, raw] : required) {
    if (cancel.load())
      throw std::runtime_error("Native Height project preparation cancelled");
    const auto found = std::find_if(
        resources.begin(), resources.end(), [&](const auto &asset) { return asset.id == id; });
    if (found == resources.end())
      throw std::invalid_argument("Saved native Height source is missing from packed resources");
    NativeHeightProjectSource result;
    result.id = id;
    result.hash = found->hash();
    // Admit the selected raw function before ordinary image decode. A failed
    // selection never leaves a partial project or neutral fallback available.
    if (raw)
      result.height = prepare_native_material_height_resource(*found, resources, cancel);
    result.graph = hydrate_native_material_resource(*found, cancel, resources);
    for (const auto &set : document.texture_sets())
      for (const auto &layer : evaluation_layers(set))
        if (layer.fill.native_material &&
            layer.fill.material_resource == id.toString(QUuid::WithoutBraces).toStdString())
        {
          const auto &instance = *layer.fill.native_material;
          validate_native_material_instance(*result.graph, instance);
          auto channels = result.graph->description.channels;
          if (instance.height_policy == NativeHeightPolicy::RawAuthoredFloatV1) {
            if (!result.height)
              throw std::logic_error("Native Height worker result is missing");
            channels.push_back(4);
          }
          const auto supported = [&](int channel) {
            return std::find(channels.begin(), channels.end(), channel) != channels.end();
          };
          for (int channel : {0, 1, 2, 4})
            if (layer.channels[channel] && !supported(channel))
              throw std::invalid_argument(
                  "Saved native instance enables a channel its prepared source does not provide");
          if (layer.channels[3])
            throw std::invalid_argument(
                "Artist Normal channels are not available for native Height instances");
          if ((layer.ao_enabled && !supported(5)) ||
              (layer.displacement.enabled && !supported(14)))
            throw std::invalid_argument(
                "Saved native instance enables an unavailable auxiliary channel");
        }
    prepared.push_back(std::move(result));
  }
  return prepared;
}
}  // namespace bts
