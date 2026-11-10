// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialImageVariant.hh"
#include "NativeMaterialMappingCompatibility.hh"
#include "NativeMaterialAsset.hh"
#include "NativeMaterialImageDependencies.hh"
#include "NativeMaterialBundle.hh"
#include "NativeMaterialSource.hh"
#include "NativeMaterialSourceFiles.hh"
#include "NativeMaterialArchive.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialImages.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_map.hh"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "IMB_colormanagement.hh"
#include <QCoreApplication>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <map>
#include <set>

namespace bts {
namespace {
using namespace blender;
void worker(const std::atomic_bool &cancel)
{
  if (auto *app = QCoreApplication::instance(); app && QThread::currentThread() == app->thread())
    throw std::logic_error("Native image replacement requires a worker");
  if (cancel.load())
    throw std::runtime_error("Native image replacement cancelled");
}
bNode &node(bNodeTree &tree, int id)
{
  for (auto &n : tree.nodes)
    if (n.identifier == id)
      return n;
  throw std::invalid_argument("Native image replacement node is missing");
}
// Isolate one original link while preserving the selected node's incoming
// Vector links and sampler storage. No pixels are read by this source operation.
bNode *isolate_edge(DetachedNativeMaterial &source,
                    const NativeImageBinding &binding,
                    const std::atomic_bool &cancel)
{
  // Clone only the selected group occurrence. Tree copy preserves native node
  // identifiers, while each copied group receives an independent native ID.
  auto adapted = binding;
  auto *tree = source.material->nodetree;
  for (auto &step : adapted.groups) {
    worker(cancel);
    auto &group = node(*tree, step.node_identifier);
    auto *old = group.id;
    auto *copy = BKE_id_copy(source.main.get(), old);
    if (!copy)
      throw std::runtime_error("Cannot copy native image group occurrence");
    group.id = copy;
    id_us_min(old);
    tree = reinterpret_cast<bNodeTree *>(copy);
    step.group = QString::fromUtf8(copy->name + 2);
  }
  auto resolved = resolve_native_image_binding(*source.material, adapted);
  tree = resolved.tree;
  blender::Map<const bNodeSocket *, bNodeSocket *> socket_map;
  auto *copy = blender::bke::node_copy_with_mapping(
      tree, *resolved.image_node, 0, std::nullopt, std::nullopt, socket_map);
  if (!copy)
    throw std::runtime_error("Cannot copy native image node");
  // Preserve all Vector input links and literal interpolation/projection/extension.
  std::vector<bNodeLink *> incoming;
  for (auto &link : tree->links)
    if (link.tonode == resolved.image_node)
      incoming.push_back(&link);
  for (auto *link : incoming)
    blender::bke::node_add_link(
        *tree, *link->fromnode, *link->fromsock, *copy, *socket_map.lookup(link->tosock));
  bNodeLink *selected = nullptr;
  for (auto &link : tree->links)
    if (link.fromnode == resolved.image_node && link.tonode == resolved.consumer &&
        QString::fromUtf8(link.fromsock->identifier) == binding.output &&
        QString::fromUtf8(link.tosock->identifier) == binding.input)
      selected = &link;
  if (!selected)
    throw std::invalid_argument("Native replacement selected edge changed");
  auto *output = socket_map.lookup(selected->fromsock);
  auto *input = selected->tosock;
  blender::bke::node_remove_link(tree, *selected);
  blender::bke::node_add_link(*tree, *copy, *output, *resolved.consumer, *input);
  return copy;
}
QJsonArray active_uses(const NativeMaterialGraph &graph,
                       const Image &image,
                       const std::atomic_bool &cancel)
{
  QJsonArray result;
  for (const auto &channel : graph.channels) {
    if (!channel.tree)
      continue;
    std::uint64_t nodes = 0, links = 0;
    for ([[maybe_unused]] const auto &n : channel.tree->nodes) {
      worker(cancel);
      nodes = resource_sum(nodes, 1);
    }
    for ([[maybe_unused]] const auto &link : channel.tree->links) {
      worker(cancel);
      links = resource_sum(links, 1);
    }

    std::map<const bNode *, std::vector<const bNode *>> incoming;
    std::vector<const bNode *> work;
    std::set<const bNode *> seen;
    for (const auto &link : channel.tree->links)
      incoming[link.tonode].push_back(link.fromnode);
    for (const auto &n : channel.tree->nodes)
      if (STREQ(n.idname, "ShaderNodeOutputAOV"))
        work.push_back(&n);
    int count = 0;
    while (!work.empty()) {
      worker(cancel);
      auto *n = work.back();
      work.pop_back();
      if (!n || !seen.insert(n).second)
        continue;
      if (STREQ(n->idname, "ShaderNodeTexImage") && n->id == &image.id)
        ++count;
      if (const auto found = incoming.find(n); found != incoming.end())
        work.insert(work.end(), found->second.begin(), found->second.end());
    }
    if (count)
      result.append(QJsonObject{{"channel", channel.channel}, {"uses", count}});
  }
  return result;
}
NativeMaterialDescription retain_instance(const NativeMaterialDescription &old,
                                          const NativeMaterialDescription &source)
{
  old.validate();
  source.validate();
  require_native_height_image_replacement(old.height_policy);
  auto out = source;
  // A copied group receives a new native ID name. Every other source interface
  // field remains strict; only editable instance state crosses the new source.
  auto a = old, b = source;
  a.group = b.group;
  for (auto *d : {&a, &b}) {
    d->warnings.clear();
    d->channels.clear();
    d->color_to_aces = d->color_from_aces = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  }
  if (a.material != b.material || a.surface_type != b.surface_type ||
      a.node_types != b.node_types || a.parameters.size() != b.parameters.size())
    throw std::invalid_argument("Native replacement source interface changed");
  for (size_t i = 0; i < a.parameters.size(); ++i) {
    a.parameters[i].value = b.parameters[i].value;
    a.parameters[i].ramp = b.parameters[i].ramp;
    a.parameters[i].mapping_transform = b.parameters[i].mapping_transform = {};
  }
  if (native_material_description_json(a) != native_material_description_json(b))
    throw std::invalid_argument("Native replacement parameter layout changed");
  for (size_t i = 0; i < out.parameters.size(); ++i) {
    out.parameters[i].value = old.parameters[i].value;
    out.parameters[i].ramp = old.parameters[i].ramp;
    out.parameters[i].mapping_transform = old.parameters[i].mapping_transform;
  }

  out.validate();
  return out;
}
}  // namespace
NativeMaterialDescription retain_native_image_instance(const NativeMaterialDescription &old,
                                                       const NativeMaterialDescription &source)
{
  return retain_instance(old, source);
}
std::optional<NativeImageVariantProvenance> native_image_variant_provenance(
    const StudioAsset &asset)
{
  if (!is_native_material_asset(asset))
    return {};
  return native_material_bundle_provenance(asset.bytes);
}
std::vector<QUuid> native_image_variant_dependencies(const StudioAsset &asset)
{
  const auto p = native_image_variant_provenance(asset);
  return p ? std::vector<QUuid>{p->original_id} : std::vector<QUuid>{};
}
NativeImageBindingAvailability native_material_image_availability(const StudioAsset &asset,
                                                                  const std::atomic_bool &cancel)
{
  worker(cancel);
  if (!is_native_material_asset(asset) || !asset.fill_preset ||
      !asset.fill_preset->native_material)
    throw std::invalid_argument("Native image inventory requires a source archive");
  const auto bundle = read_native_material_bundle(asset.bytes);
  auto scene = asset;
  scene.bytes = bundle.blend;
  const auto path = materialize_native_material_archive(scene, cancel);
  const auto read = [&] {
    return read_native_material_source(path, asset.fill_preset->native_material->material, cancel);
  };
  auto source = read();
  NativeImageBindingAvailability result;
  result.inventory = native_material_image_bindings(*source.material, cancel);
  auto expected = native_material_description_json(*asset.fill_preset->native_material);
  auto described = describe_native_material_source(*source.material, cancel);
  preserve_legacy_vector_math_mapping_policy(described, *asset.fill_preset->native_material);
  auto actual = native_material_description_json(described);
  for (const auto *field : {"warnings", "channels", "color_to_aces", "color_from_aces"}) {
    expected.remove(field);
    actual.remove(field);
  }
  if (expected != actual)
    throw std::invalid_argument(
        "Native image inventory source interface does not match immutable metadata");
  auto used = resource_product(result.inventory.bindings.size(),
                               sizeof(NativeImageBindingStatus) + 256);
  for (const auto &binding : result.inventory.bindings)
    used = resource_sum(used,
                        resource_product(binding.groups.size(), sizeof(NativeImageGroupStep)));
  result.bindings.reserve(result.inventory.bindings.size());
  for (const auto &binding : result.inventory.bindings) {
    worker(cancel);
    NativeImageBindingStatus status;
    status.binding = binding;
    try {
      // Each probe owns an independent source Main, not a runtime/live owner.
      auto probe = read();
      auto *isolated = isolate_edge(probe, binding, cancel);
      auto *old = isolated->id;
      auto *image = reinterpret_cast<Image *>(BKE_id_copy(probe.main.get(), old));
      if (!image)
        throw std::runtime_error("Cannot isolate native image eligibility marker");
      id_us_min(old);
      isolated->id = &image->id;
      BKE_ntree_update(*probe.main);
      auto graph = finish_native_material_source(
          std::move(probe),
          cancel,
          asset.fill_preset->native_material.get(),
          NativeWorldNormalSurfaceContract::PainterUndisplaced);
      status.active_channels = active_uses(*graph, *image, cancel);
      if (status.active_channels.isEmpty()) {
        status.reason = "No supported native channel uses this exact image link";
        for (const auto &channel : graph->channels)
          if (!channel.tree && !channel.error.isEmpty())
            status.reason += QString("; channel %1: %2").arg(channel.channel).arg(channel.error);
      }
      const auto &settings = binding.source_settings;
      if (settings["image_source"].toInt() != IMA_SRC_FILE ||
          settings["image_type"].toInt() != IMA_TYPE_IMAGE || settings["multiview"].toBool())
        status.reason = "Replacement currently supports ordinary single-file images only";
    }
    catch (const std::exception &error) {
      if (cancel.load())
        throw;
      status.reason = QString::fromUtf8(error.what());
    }
    result.bindings.push_back(std::move(status));
  }
  return result;
}
PreparedNativeImageVariant prepare_native_image_variant(const StudioAsset &base,
                                                        const std::vector<StudioAsset> &resources,
                                                        const NativeMaterialDescription &instance,
                                                        const NativeImageBinding &binding,
                                                        const StudioAsset &replacement,
                                                        const NativeImageReplacementPolicy &policy,
                                                        const std::atomic_bool &cancel)
{
  worker(cancel);
  if (!is_native_material_asset(base) || base.bytes.isEmpty() || !base.fill_preset ||
      !base.fill_preset->native_material)
    throw std::invalid_argument("Native replacement requires a validated source asset");
  // Explicit choice is mandatory. Unknown never decodes, caches or mutates IDs.
  if (policy.source_space == NativeImageSourceSpace::Unknown)
    throw std::invalid_argument("Native image source color-space policy is required");
  const auto declared = binding.source_settings["source_space"].toString();
  const auto space = policy.source_space == NativeImageSourceSpace::OriginalDeclared ?
                         declared :
                         policy.named_space;
  if (policy.source_space == NativeImageSourceSpace::OriginalDeclared &&
      !policy.named_space.isEmpty())
    throw std::invalid_argument(
        "Original-declared source policy cannot also specify a named override");
  const auto source_space_bytes = space.toUtf8();
  if (source_space_bytes.size() >= IM_MAX_SPACE)
    throw std::length_error(
        (QString(
             "Native image source-space name needs %1 UTF-8 bytes; Image color-space name stores at most %2 bytes plus NUL")
             .arg(source_space_bytes.size())
             .arg(IM_MAX_SPACE - 1))
            .toStdString());
  if ((policy.source_space != NativeImageSourceSpace::Named &&
       policy.source_space != NativeImageSourceSpace::OriginalDeclared) ||
      space.isEmpty() || space.contains(QChar::Null) ||
      IMB_colormanagement_colorspace_get_named_index(space.toUtf8().constData()) < 0)
    throw std::invalid_argument("Native replacement requires a concrete known source color space");
  if (!replacement.usages.contains("texture") ||
      (!replacement.texture_files.empty() &&
       (replacement.texture_files.size() != 1 || replacement.texture_files.front().tile != 0)))
    throw std::invalid_argument("Native replacement requires one immutable encoded image");

  std::set<QUuid> ids;
  for (const auto &resource : resources)
    if (resource.id.isNull() || !ids.insert(resource.id).second)
      throw std::invalid_argument("Duplicate or empty native replacement resource identity");
  asset_metadata_read(asset_metadata(base));
  asset_metadata_read(asset_metadata(replacement));
  const auto bundle = read_native_material_bundle(base.bytes);
  const auto previous = native_material_bundle_provenance(base.bytes);
  StudioAsset original = base;
  NativeImageVariantProvenance provenance;
  if (previous) {
    original = *native_material_original_source(base, resources);
    provenance = *previous;
  }
  else {
    provenance.original_id = base.id;
    provenance.original_sha256 = base.hash();
  }
  auto scene = base;
  scene.bytes = bundle.blend;
  const auto archive_path = materialize_native_material_archive(scene, cancel);
  auto source = read_native_material_source(
      archive_path, base.fill_preset->native_material->material, cancel);
  bind_native_material_source_images(*source.main, bundle, resources, cancel);
  auto expected = native_material_description_json(*base.fill_preset->native_material);
  auto described = describe_native_material_source(*source.material, cancel);
  preserve_legacy_vector_math_mapping_policy(described, *base.fill_preset->native_material);
  auto actual = native_material_description_json(described);
  for (const auto *field : {"warnings", "channels", "color_to_aces", "color_from_aces"}) {
    expected.remove(field);
    actual.remove(field);
  }
  if (expected != actual)
    throw std::invalid_argument(
        "Native replacement source interface does not match its immutable metadata");
  const auto inventory = native_material_image_bindings(*source.material, cancel);
  if (std::find(inventory.bindings.begin(), inventory.bindings.end(), binding) ==
      inventory.bindings.end())
    throw std::invalid_argument(
        "Native replacement binding is not a reachable original source edge");
  auto resolved = resolve_native_image_binding(*source.material, binding);
  const auto &old_image = *reinterpret_cast<Image *>(resolved.image_node->id);
  if (old_image.source != IMA_SRC_FILE || old_image.type != IMA_TYPE_IMAGE ||
      old_image.flag & IMA_USE_VIEWS)
    throw std::invalid_argument(
        "Native image replacement currently requires a single-file source image");
  const NativeMaterialImageReference reference{
      binding.image, replacement.hash(), replacement.filename};
  reference.validate();
  auto all_resources = resources;
  const auto existing = std::find_if(
      resources.begin(), resources.end(), [&](const auto &a) { return a.id == replacement.id; });
  if (existing != resources.end()) {
    if (existing->hash() != replacement.hash() || existing->usages != replacement.usages)
      throw std::invalid_argument("Native replacement source identity changed");
  }
  else
    all_resources.push_back(replacement);
  const auto replacement_path = materialize_native_material_image(
      reference, all_resources, cancel);
  auto *copy = isolate_edge(source, binding, cancel);
  auto *image = BKE_image_load(source.main.get(), replacement_path.toUtf8().constData());
  if (!image)
    throw std::runtime_error("Cannot load native replacement image");
  if (image->source != IMA_SRC_FILE || image->type != IMA_TYPE_IMAGE ||
      image->flag & IMA_USE_VIEWS)
    throw std::invalid_argument("Replacement bytes must describe one native single-file image");
  image->alpha_mode = old_image.alpha_mode;
  image->flag |= IMA_HIGH_BITDEPTH;  // New replacement always preserves FLOAT32 range/precision.
  STRNCPY(image->colorspace_settings.name, space.toUtf8().constData());
  id_us_min(copy->id);
  copy->id = &image->id;
  worker(cancel);
  BKE_ntree_update(*source.main);
  auto graph = finish_native_material_source(std::move(source),
                                             cancel,
                                             base.fill_preset->native_material.get(),
                                             NativeWorldNormalSurfaceContract::PainterUndisplaced);
  const auto usage = active_uses(*graph, *image, cancel);
  if (usage.isEmpty()) {
    QString reason =
        "Selected image edge does not contribute to any supported prepared native channel. Internal Normal image extraction is unavailable.";
    for (const auto &channel : graph->channels)
      if (!channel.tree)
        reason += QString(" Channel %1: %2.").arg(channel.channel).arg(channel.error);
    throw std::invalid_argument(reason.toStdString());
  }
  const auto updated = retain_instance(instance, graph->description);
  prepare_optional_native_world_normal(graph, cancel);
  auto archive = archive_native_material(*graph, archive_path, cancel, true);
  provenance.operations.push_back(
      {binding,
       replacement.hash(),
       space,
       policy.source_space == NativeImageSourceSpace::Named ? "named" : "original-declared"});
  const auto unpacked = read_native_material_bundle(archive.bytes);
  archive.bytes = native_material_bundle_bytes(
      unpacked.blend, unpacked.images, provenance, unpacked.preview_png);
  // Reuse known immutable encoded sources by SHA. No variant re-embeds the
  // unchanged image bytes or manufactures new texture UUIDs for shared inputs.
  for (auto &texture : archive.textures) {
    const auto found = std::find_if(
        all_resources.begin(), all_resources.end(), [&](const auto &a) {
          return a.usages.contains("texture") && a.hash() == texture.hash();
        });
    if (found != all_resources.end())
      texture = *found;
  }
  const auto image_references = native_material_bundle_image_manifest(archive.bytes);
  auto diagnostics = std::move(archive.diagnostics);
  diagnostics["decoded_images"] = prepare_native_material_images(*graph, cancel);
  if (!graph->tiled_image_resources.empty())
    validate_native_material_tiled_sources(*graph, image_references, cancel);
  auto asset = native_material_asset(
      base.name, std::move(archive.bytes), std::move(archive.description));
  asset.search_terms = base.search_terms;
  asset.search_terms.removeIf([](const auto &tag) { return tag.startsWith("native-channel:"); });
  if (!asset.search_terms.contains("blender-native"))
    asset.search_terms.append("blender-native");
  for (const auto &channel : graph->channels)
    if (channel.tree)
      asset.search_terms.append("native-channel:" + QString::number(channel.channel));
  diagnostics["active_channel_uses"] = usage;
  diagnostics["supported_aov_usage_verified"] = true;
  diagnostics["image_binding"] = native_image_binding_json(binding);
  diagnostics["original_sha256"] = provenance.original_sha256;
  diagnostics["replacement_sha256"] = replacement.hash();
  diagnostics["source_space"] = space;
  diagnostics["source_space_policy"] = provenance.operations.back().source_space_policy;
  diagnostics["precision_policy"] = provenance.operations.back().precision_policy;
  diagnostics["original_high_precision"] = bool(old_image.flag & IMA_HIGH_BITDEPTH);
  prepare_native_world_normal_images(graph, cancel, image_references);
  worker(cancel);
  return {
      {std::move(asset), std::move(graph), std::move(diagnostics), std::move(archive.textures)},
      updated,
      std::move(original),
      std::move(provenance)};
}
}  // namespace bts
