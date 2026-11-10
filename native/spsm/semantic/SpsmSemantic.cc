// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpsmSemantic.hh"
#include "../../host/platform/ResourceAvailability.hh"
#include <algorithm>
#include <limits>
#include <map>
#include <ostream>

namespace bts::spsm::semantic {
namespace {
constexpr ChannelMapping channels[] = {
    {1, Channel::BaseColor, ChannelContract::DirectSlot},
    {2, Channel::Bump, ChannelContract::DirectSlot},
    {4, Channel::Specular, ChannelContract::RequiresWorkflowContract},
    {8, Channel::Opacity, ChannelContract::RequiresWorkflowContract},
    {64, Channel::Glossiness, ChannelContract::RequiresWorkflowContract},
    {128, Channel::Roughness, ChannelContract::DirectSlot},
    {8192, Channel::Metallic, ChannelContract::DirectSlot},
    {4194304, Channel::Normal, ChannelContract::DirectSlot},
    {8388608, Channel::AmbientOcclusion, ChannelContract::DirectSlot},
    {16777216, Channel::Diffuse, ChannelContract::RequiresWorkflowContract}};
struct SizeAccounting {
  std::uint64_t used = 0;
  explicit SizeAccounting(InspectOptions) {}
  void add(std::uint64_t bytes)
  {
    used = resource_sum(used, bytes);
  }
  template<class T> void item(std::size_t count = 1)
  {
    add(resource_product(resource_product(sizeof(T), count), 2));
  }
};
const Node &node(const Preset &p, NodeId id)
{
  if (id >= p.nodes.size())
    throw Error("Invalid semantic source node reference");
  return p.nodes[id];
}
const Object *object(const Preset &p, NodeId id)
{
  return std::get_if<Object>(&node(p, id).value);
}
std::optional<NodeId> field(const Preset &p, NodeId id, std::string_view name)
{
  if (const auto *o = object(p, id))
    for (const auto &f : o->fields)
      if (f.name == name) {
        node(p, f.value);
        return f.value;
      }
  return {};
}
std::string_view class_name(const Preset &p, NodeId id)
{
  const auto *o = object(p, id);
  return o ? o->class_name : std::string_view{};
}
std::optional<std::uint64_t> unsigned_value(const Node &n)
{
  if (const auto *v = std::get_if<std::uint64_t>(&n.value))
    return *v;
  if (const auto *v = std::get_if<std::int64_t>(&n.value); v && *v >= 0)
    return std::uint64_t(*v);
  if (const auto *v = std::get_if<IntValues>(&n.value); v && v->count == 1 && v->values[0] >= 0)
    return std::uint64_t(v->values[0]);
  return {};
}
std::span<const NodeId> array(const Preset &p, std::optional<NodeId> id)
{
  if (id)
    if (const auto *a = std::get_if<Array>(&node(p, *id).value))
      return a->elements;
  return {};
}
std::span<const NodeId> stack(const Preset &p, NodeId id, std::string_view name)
{
  const auto s = field(p, id, name);
  return s ? array(p, field(p, *s, "items")) : std::span<const NodeId>{};
}
void finding(PresetInspection &out,
             SizeAccounting &sizes,
             NodeId source,
             Capability capability,
             std::string_view code,
             std::string_view detail)
{
  sizes.item<Finding>();
  out.findings.push_back({source, capability, code, detail});
}
std::optional<std::size_t> source_channels(const Preset &p,
                                           NodeId id,
                                           PresetInspection &out,
                                           SizeAccounting &sizes)
{
  const auto f = field(p, id, "channelTypes");
  if (!f)
    return {};
  const auto bits = unsigned_value(node(p, *f));
  if (!bits) {
    finding(out,
            sizes,
            *f,
            Capability::Unsupported,
            "channel-mask-type",
            "Channel mask is not a nonnegative source integer; original value retained");
    return {};
  }
  sizes.item<ChannelMapping>(std::size(channels));
  auto result = inspect_channels(*bits);
  if (result.unknown_bits)
    finding(out,
            sizes,
            *f,
            Capability::Unsupported,
            "unknown-channel-bits",
            "Unknown source channel bits are retained without assigning native slots");
  for (const auto &m : result.mappings)
    if (m.contract == ChannelContract::RequiresWorkflowContract)
      finding(
          out,
          sizes,
          *f,
          Capability::RequiresValidation,
          "channel-workflow-contract",
          "Opacity, glossiness, specular and diffuse need explicit shading/conversion contracts");
  sizes.item<ChannelUse>();
  const auto index = out.channel_uses.size();
  out.channel_uses.push_back({id, *f, std::move(result)});
  return index;
}
std::vector<Blending> blendings(const Preset &p,
                                NodeId id,
                                PresetInspection &out,
                                SizeAccounting &sizes)
{
  std::vector<Blending> result;
  for (const auto entry : array(p, field(p, id, "perChannelBlending"))) {
    sizes.item<Blending>();
    result.push_back({entry,
                      field(p, entry, "channelTypes"),
                      field(p, entry, "blendingMode"),
                      field(p, entry, "opacity")});
  }
  return result;
}
std::vector<FieldRef> selected_fields(const Preset &p,
                                      NodeId id,
                                      std::span<const std::string_view> names,
                                      SizeAccounting &sizes)
{
  std::vector<FieldRef> result;
  if (const auto *o = object(p, id))
    for (std::size_t i = 0; i < o->fields.size(); ++i)
      if (std::find(names.begin(), names.end(), o->fields[i].name) != names.end()) {
        sizes.item<FieldRef>();
        result.push_back({id, i});
      }
  return result;
}
bool known_class(std::string_view name)
{
  constexpr std::string_view names[] = {"DataLayersPreset",
                                        "DataSmartMask",
                                        "DataLayerGroup",
                                        "DataLayerColor",
                                        "DataStackLayers",
                                        "DataStackActions",
                                        "DataBlending",
                                        "DataActionGroup",
                                        "DataActionFill",
                                        "DataActionFilterLevels",
                                        "DataActionFilterProcedural",
                                        "DataActionPaint",
                                        "DataSourceUniform",
                                        "DataSourceBitmap",
                                        "DataSourceProcedural",
                                        "Data2DTransformation",
                                        "DataBitmap",
                                        "DataColorSpaceOverride",
                                        "DataGridDeformation",
                                        "DataGridVertex",
                                        "DataLevels",
                                        "DataProcedural",
                                        "DataProceduralInput",
                                        "DataProceduralInputsSource",
                                        "DataProceduralOutput",
                                        "DataBrush",
                                        "DataEmitterStroke",
                                        "DataStroke",
                                        "DataStrokeKey",
                                        "DataTweakFloat",
                                        "DataTweakFloat2",
                                        "DataTweakFloat3",
                                        "DataTweakInt",
                                        "DataTweakInt2"};
  return std::find(std::begin(names), std::end(names), name) != std::end(names);
}
void locate(const Preset &p, PresetInspection &out, SizeAccounting &sizes)
{
  const auto count = p.nodes.size();
  sizes.item<Location>(count);
  sizes.item<NodeId>(count);
  sizes.item<unsigned char>(count);
  out.locations.resize(count);
  std::vector<unsigned char> seen(count);
  std::vector<NodeId> todo;
  todo.reserve(count);
  node(p, p.root);
  todo.push_back(p.root);
  seen[p.root] = 1;
  std::size_t visited = 0;
  while (!todo.empty()) {
    const auto id = todo.back();
    todo.pop_back();
    ++visited;
    const auto child = [&](NodeId c, Location::Edge edge, std::size_t index) {
      node(p, c);
      if (seen[c])
        throw Error("Semantic arena is cyclic or aliases a node");
      seen[c] = 1;
      out.locations[c] = {id, edge, index};
      todo.push_back(c);
    };
    if (const auto *o = object(p, id))
      for (std::size_t i = o->fields.size(); i-- > 0;)
        child(o->fields[i].value, Location::Edge::Field, i);
    else if (const auto *a = std::get_if<Array>(&node(p, id).value))
      for (std::size_t i = a->elements.size(); i-- > 0;)
        child(a->elements[i], Location::Edge::Array, i);
  }
  if (visited != count)
    throw Error("Semantic arena contains unreachable nodes");
}
std::optional<DestinationMeshMap> alias(std::string_view uri)
{
  if (uri == "resourcealias:NormalWorldSpace")
    return DestinationMeshMap::WorldNormal;
  if (uri == "resourcealias:AmbientOcclusion")
    return DestinationMeshMap::AmbientOcclusion;
  if (uri == "resourcealias:Curvature")
    return DestinationMeshMap::Curvature;
  if (uri == "resourcealias:Position")
    return DestinationMeshMap::Position;
  if (uri == "resourcealias:Thickness")
    return DestinationMeshMap::Thickness;
  return {};
}
void quoted(std::ostream &out, std::string_view value)
{
  constexpr char hex[] = "0123456789abcdef";
  out.put('"');
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') {
      out.put('\\');
      out.put(char(c));
    }
    else if (c < 32) {
      out << "\\u00";
      out.put(hex[c >> 4]);
      out.put(hex[c & 15]);
    }
    else
      out.put(char(c));
  }
  out.put('"');
}
}  // namespace
std::span<const ChannelMapping> channel_mapping_table()
{
  return channels;
}
std::string_view channel_name(Channel c)
{
  switch (c) {
    case Channel::BaseColor:
      return "Base Color";
    case Channel::Bump:
      return "Bump/Height";
    case Channel::Specular:
      return "Specular";
    case Channel::Opacity:
      return "Opacity";
    case Channel::Glossiness:
      return "Glossiness";
    case Channel::Roughness:
      return "Roughness";
    case Channel::Metallic:
      return "Metallic";
    case Channel::Normal:
      return "Normal";
    case Channel::AmbientOcclusion:
      return "Ambient Occlusion";
    case Channel::Diffuse:
      return "Diffuse";
  }
  return "Unknown";
}
std::string_view mesh_map_name(DestinationMeshMap r)
{
  switch (r) {
    case DestinationMeshMap::WorldNormal:
      return "World space normal";
    case DestinationMeshMap::AmbientOcclusion:
      return "Ambient occlusion";
    case DestinationMeshMap::Curvature:
      return "Curvature";
    case DestinationMeshMap::Position:
      return "Position";
    case DestinationMeshMap::Thickness:
      return "Thickness";
  }
  return "Unknown";
}
Channels inspect_channels(std::uint64_t bits)
{
  Channels result;
  result.original_bits = result.unknown_bits = bits;
  result.auxiliary = bits == 0;
  for (const auto &m : channels)
    if (bits & m.bit) {
      result.mappings.push_back(m);
      result.unknown_bits &= ~m.bit;
    }
  return result;
}
Inspection inspect_archive(Archive &&source, InspectOptions options)
{
  SizeAccounting sizes(options);
  sizes.item<Archive>();
  Inspection result(std::make_shared<const Archive>(std::move(source)));
  sizes.item<PresetInspection>(result.source().presets.size());
  result.presets.resize(result.source().presets.size());
  for (std::size_t pi = 0; pi < result.source().presets.size(); ++pi) {
    const auto &p = result.source().presets[pi];
    if (p.dataset == "/preset.bin")
      result.primary_preset = pi;
    auto &out = result.presets[pi];
    out.data_version = field(p, p.root, "preset_dataversion");
    locate(p, out, sizes);
    std::map<NodeId, std::size_t> layers, actions;  // Only indexes; source ownership stays flat.
    for (NodeId id = 0; id < p.nodes.size(); ++id) {
      const auto name = class_name(p, id);
      if (name.empty())
        continue;
      const auto channel_index = source_channels(p, id, out, sizes);
      if (!known_class(name))
        finding(out,
                sizes,
                id,
                Capability::Unsupported,
                "unknown-class",
                "Unknown source class and all fields are preserved");
      if (name == "DataLayerGroup" || name == "DataLayerColor") {
        sizes.item<Layer>();
        sizes.add(sizeof(std::pair<const NodeId, std::size_t>) + 4 * sizeof(void *));
        layers[id] = out.layers.size();
        Layer layer{};
        layer.source = id;
        layer.group = name == "DataLayerGroup";
        layer.label = field(p, id, "label");
        layer.uid = field(p, id, "uid");
        layer.enabled = field(p, id, "enabled");
        layer.mask_enabled = field(p, id, "maskEnabled");
        layer.mask_initial = field(p, id, "maskInitial");
        layer.blending = blendings(p, id, out, sizes);
        constexpr std::string_view geometry[] = {"enabledGeometryMask",
                                                 "enabledMeshDefault",
                                                 "enabledMeshList",
                                                 "enabledUVTileDefault",
                                                 "enabledUVTileList",
                                                 "enabledUVTileMask",
                                                 "geometryMaskType"};
        layer.geometry = selected_fields(p, id, geometry, sizes);
        out.layers.push_back(std::move(layer));
        finding(
            out,
            sizes,
            id,
            Capability::NativePrimitive,
            "layer-structure",
            "Native folders/layers and scalar mask background can represent this structure after ordering/semantic validation");
      }
      if (name.starts_with("DataAction")) {
        sizes.item<Action>();
        sizes.add(sizeof(std::pair<const NodeId, std::size_t>) + 4 * sizeof(void *));
        actions[id] = out.actions.size();
        Action action{};
        action.source = id;
        action.label = field(p, id, "label");
        action.uid = field(p, id, "uid");
        action.enabled = field(p, id, "enabled");
        action.channels = channel_index;
        action.blending = blendings(p, id, out, sizes);
        action.kind = name == "DataActionGroup"            ? ActionKind::Group :
                      name == "DataActionFill"             ? ActionKind::Fill :
                      name == "DataActionFilterLevels"     ? ActionKind::Levels :
                      name == "DataActionFilterProcedural" ? ActionKind::ProceduralFilter :
                      name == "DataActionPaint"            ? ActionKind::Paint :
                                                             ActionKind::Unknown;
        const auto raw_sources = array(p, field(p, id, "sources"));
        sizes.item<NodeId>(raw_sources.size());
        action.sources.assign(raw_sources.begin(), raw_sources.end());
        constexpr std::string_view projection[] = {"backfaceCulling",
                                                   "backfaceCullingAngle",
                                                   "backfaceCullingHardness",
                                                   "cylinderCullingAngle",
                                                   "cylinderHideCaps",
                                                   "dddEulerAngles",
                                                   "dddGrid",
                                                   "dddPosition",
                                                   "dddSize",
                                                   "dddWrap",
                                                   "depthCulling",
                                                   "depthCullingHardness",
                                                   "filtering",
                                                   "projection",
                                                   "sideHardness",
                                                   "uvGrid",
                                                   "uvSamplingWrap",
                                                   "uvTransferChannel",
                                                   "uvTransformation",
                                                   "uvTransformation0",
                                                   "uvTransformation1"};
        action.projection = selected_fields(p, id, projection, sizes);
        out.actions.push_back(std::move(action));
        if (name == "DataActionGroup")
          finding(
              out,
              sizes,
              id,
              Capability::Unsupported,
              "nested-action-group",
              "Native flat effect lists lack arbitrary nested action-group isolation and blending");
        else if (name == "DataActionPaint")
          finding(
              out,
              sizes,
              id,
              Capability::Unsupported,
              "source-paint-semantics",
              "Brush/emitter/stroke transforms, pressure, jitter and seeds have not been lowered");
        else if (name == "DataActionFilterProcedural")
          finding(out,
                  sizes,
                  id,
                  Capability::Unsupported,
                  "procedural-filter",
                  "No source procedural filter implementation or verified parameter conversion");
        else if (name == "DataActionFilterLevels")
          finding(
              out,
              sizes,
              id,
              Capability::NativePrimitive,
              "levels-primitive",
              "Native Levels exists; source channel/component and gamma/order contracts still require validation");
        else if (name == "DataActionFill")
          finding(out,
                  sizes,
                  id,
                  Capability::NativePrimitive,
                  "fill-primitive",
                  "Native fill/mask-fill exists; sources and mapping remain separately inspected");
      }
      if (name.starts_with("DataSource")) {
        sizes.item<Source>();
        Source entry{};
        entry.source = id;
        entry.kind = name == "DataSourceUniform"    ? Source::Kind::Uniform :
                     name == "DataSourceBitmap"     ? Source::Kind::Bitmap :
                     name == "DataSourceProcedural" ? Source::Kind::Procedural :
                                                      Source::Kind::Unknown;
        entry.channels = channel_index;
        entry.uid = field(p, id, "uid");
        entry.opacity = field(p, id, "opacity");
        entry.color = field(p, id, "color");
        entry.bitmap = field(p, id, "bitmap");
        entry.procedural = field(p, id, "procedural");
        out.sources.push_back(std::move(entry));
        if (name == "DataSourceUniform")
          finding(
              out,
              sizes,
              id,
              Capability::NativePrimitive,
              "uniform-primitive",
              "Uniform native material values exist; preserve source channel bits and opacity");
      }
      if (name == "DataBlending") {
        if (const auto mode = field(p, id, "blendingMode"))
          finding(out,
                  sizes,
                  *mode,
                  Capability::RequiresValidation,
                  "source-blend-enum",
                  "Source blend enum is preserved; never cast to native LayerBlend");
      }
      if (name == "DataBitmap")
        finding(
            out,
            sizes,
            id,
            Capability::RequiresValidation,
            "bitmap-interpretation",
            "Source color-space, alpha and scalar extraction settings remain raw, including destination-map aliases");
      if (name == "DataActionFill")
        for (const auto key : {"projection", "filtering", "uvSamplingWrap", "dddWrap"})
          if (const auto v = field(p, id, key))
            finding(
                out,
                sizes,
                *v,
                Capability::RequiresValidation,
                "source-projection-enum",
                "Source projection/filter/wrap enum is preserved without native ordinal conversion");
      if (name == "DataProcedural") {
        sizes.item<Procedural>();
        Procedural proc{};
        proc.source = id;
        proc.uid = field(p, id, "uid");
        proc.uri = field(p, id, "urlToSbsRes");
        proc.outputs = field(p, id, "outputs");
        proc.inputs = field(p, id, "sourcesInputs");
        const auto tweaks = array(p, field(p, id, "tweaks"));
        sizes.item<NodeId>(tweaks.size());
        proc.tweaks.assign(tweaks.begin(), tweaks.end());
        out.procedurals.push_back(std::move(proc));
        finding(
            out,
            sizes,
            id,
            Capability::Unsupported,
            "procedural-unimplemented",
            "Named source graph/tweak semantics are unavailable; native mesh generators are not equivalent implementations");
      }
    }
    for (auto &layer : out.layers) {
      for (const auto child : stack(p, layer.source, "subStack")) {
        if (const auto found = layers.find(child); found != layers.end()) {
          sizes.item<std::size_t>();
          layer.children.push_back(found->second);
          if (out.layers[found->second].parent)
            throw Error("Layer belongs to multiple source groups");
          out.layers[found->second].parent = layers.at(layer.source);
        }
        else
          finding(out,
                  sizes,
                  child,
                  Capability::Unsupported,
                  "unknown-layer-item",
                  "Non-layer source stack item is preserved without dropping it");
      }
      for (const auto name : {"actions", "maskActions"}) {
        auto &target = std::string_view(name) == "actions" ? layer.actions : layer.mask_actions;
        for (const auto child : stack(p, layer.source, name)) {
          if (const auto found = actions.find(child); found != actions.end()) {
            sizes.item<std::size_t>();
            target.push_back(found->second);
          }
          else
            finding(out,
                    sizes,
                    child,
                    Capability::Unsupported,
                    "unknown-action-item",
                    "Non-action source stack item is preserved without dropping it");
        }
      }
      std::size_t fills = 0;
      for (const auto index : layer.actions)
        fills += out.actions[index].kind == ActionKind::Fill;
      if (fills > 1)
        finding(
            out,
            sizes,
            layer.source,
            Capability::Unsupported,
            "multiple-content-fills",
            "Native content has one FillProjection and no Fill effect; preserve all source fills in order");
    }
    for (auto &action : out.actions)
      for (const auto child : stack(p, action.source, "subStack")) {
        if (const auto found = actions.find(child); found != actions.end()) {
          sizes.item<std::size_t>();
          action.children.push_back(found->second);
        }
        else
          finding(out,
                  sizes,
                  child,
                  Capability::Unsupported,
                  "unknown-action-item",
                  "Unknown nested action item remains in its exact source location");
      }
    if (class_name(p, p.root) == "DataSmartMask") {
      // A typed semantic owner lets the existing native mask lowerer consume the
      // action stack. No raw source nodes, enums or alternate streams are changed.
      Layer owner{};
      owner.source = p.root;
      owner.group = true;
      owner.uid = field(p, p.root, "uid");
      for (const auto child : stack(p, p.root, "actionsStack")) {
        if (const auto found = actions.find(child); found != actions.end()) {
          sizes.item<std::size_t>();
          owner.mask_actions.push_back(found->second);
        }
        else
          finding(out,
                  sizes,
                  child,
                  Capability::Unsupported,
                  "unknown-mask-action",
                  "Unknown smart-mask stack item is preserved without lowering");
      }
      sizes.item<Layer>();
      sizes.item<std::size_t>();
      out.roots.push_back(out.layers.size());
      out.layers.push_back(std::move(owner));
      finding(
          out,
          sizes,
          p.root,
          Capability::NativePrimitive,
          "smart-mask-actions",
          "Standalone DataSmartMask actions retain their source order and lower through the native mask effect primitives");
    }
    else if (const auto root = field(p, p.root, "layerGroup"); root && layers.contains(*root)) {
      sizes.item<std::size_t>();
      out.roots.push_back(layers.at(*root));
    }
    else
      finding(out,
              sizes,
              p.root,
              Capability::Unsupported,
              "root-layer-shape",
              "Missing/unknown root layerGroup; no native import performed");
    for (std::size_t ri = 0; ri < result.source().references.size(); ++ri) {
      const auto &ref = result.source().references[ri];
      if (ref.preset != pi)
        continue;
      node(p, ref.source);
      sizes.item<ResourceBinding>();
      ResourceBinding binding{};
      binding.reference = ri;
      binding.destination_map = alias(ref.uri);
      binding.state = binding.destination_map       ? ResourceBinding::State::DestinationMeshMap :
                      !ref.resource_dataset.empty() ? ResourceBinding::State::EmbeddedDescriptor :
                                                      ResourceBinding::State::Unresolved;
      out.resources.push_back(binding);
      if (binding.state == ResourceBinding::State::DestinationMeshMap)
        finding(out,
                sizes,
                ref.source,
                Capability::NativePrimitive,
                "destination-mesh-map",
                "Alias maps to destination per-tile mesh-map role; source bakes are not copied");
      else if (binding.state == ResourceBinding::State::EmbeddedDescriptor &&
               ref.field == "urlToBitmapRes")
        finding(out,
                sizes,
                ref.source,
                Capability::Unsupported,
                "image-hydration",
                "Embedded image descriptors are lazy; v1/v2 pixel decoding is not implemented");
      else if (binding.state == ResourceBinding::State::Unresolved)
        finding(out,
                sizes,
                ref.source,
                Capability::Unsupported,
                "unresolved-resource",
                "Resource URI is unresolved; no filesystem/package fallback is attempted");
    }
  }
  return result;
}
const Node &source_value(const Inspection &i, std::size_t p, NodeId id)
{
  return node(i.source().presets.at(p), id);
}
const Field &source_field(const Inspection &i, std::size_t p, FieldRef ref)
{
  const auto *o = object(i.source().presets.at(p), ref.object);
  if (!o || ref.index >= o->fields.size())
    throw Error("Invalid source field reference");
  return o->fields[ref.index];
}
std::string source_path(const Inspection &inspection, std::size_t pi, NodeId id)
{
  const auto &p = inspection.source().presets.at(pi);
  const auto &locations = inspection.presets.at(pi).locations;
  node(p, id);
  std::vector<NodeId> chain;
  std::uint64_t bytes = 1;
  for (auto current = id; locations.at(current).parent; current = *locations[current].parent) {
    if (chain.size() >= locations.size())
      throw Error("Cyclic semantic location");
    chain.push_back(current);
    const auto &edge = locations[current];
    bytes = resource_sum(bytes, 32);
    if (edge.edge == Location::Edge::Field)
      bytes = resource_sum(
          bytes,
          resource_product(
              std::get<Object>(node(p, *edge.parent).value).fields.at(edge.index).name.size(), 6));
  }
  std::string result;
  result.reserve(bytes);
  result = "$";
  constexpr char hex[] = "0123456789abcdef";
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    const auto &edge = locations[*it];
    if (edge.edge == Location::Edge::Array) {
      result += '[';
      result += std::to_string(edge.index);
      result += ']';
    }
    else {
      result += "[\"";
      for (unsigned char c : std::get<Object>(node(p, *edge.parent).value).fields[edge.index].name)
      {
        if (c == '"' || c == '\\') {
          result += '\\';
          result += char(c);
        }
        else if (c < 32) {
          result += "\\u00";
          result += hex[c >> 4];
          result += hex[c & 15];
        }
        else
          result += char(c);
      }
      result += "\"]";
    }
  }
  return result;
}
void write_report(std::ostream &out, const Inspection &i)
{
  out << "{\"source\":";
  quoted(out, i.source().source.string());
  out << ",\"inspection_only\":true,\"native_import_available\":false,\"rendered\":false,\"resource_payloads_read\":false,\"preset_payload_bytes_read\":"
      << i.source().payload_bytes_read << ",\"presets\":[";
  for (std::size_t pi = 0; pi < i.presets.size(); ++pi) {
    if (pi)
      out << ',';
    const auto &p = i.source().presets[pi];
    const auto &s = i.presets[pi];
    out << "{\"dataset\":";
    quoted(out, p.dataset);
    out << ",\"preset_data_version\":";
    if (s.data_version) {
      const auto version = unsigned_value(node(p, *s.data_version));
      if (version)
        out << *version;
      else
        out << "null";
    }
    else
      out << "null";
    out << ",\"wire_version\":" << p.wire_version << ",\"header_flags\":" << p.header_flags
        << ",\"is_primary_stream\":" << (i.primary_preset == pi ? "true" : "false")
        << ",\"layers\":" << s.layers.size() << ",\"actions\":" << s.actions.size()
        << ",\"sources\":" << s.sources.size() << ",\"procedurals\":" << s.procedurals.size()
        << ",\"findings\":[";
    for (std::size_t f = 0; f < s.findings.size(); ++f) {
      if (f)
        out << ',';
      const auto &v = s.findings[f];
      out << "{\"path\":";
      quoted(out, source_path(i, pi, v.source));
      out << ",\"source_node\":" << v.source << ",\"class\":";
      quoted(out, class_name(p, v.source));
      out << ",\"capability\":";
      quoted(out,
             v.capability == Capability::NativePrimitive    ? "native_primitive_only" :
             v.capability == Capability::RequiresValidation ? "requires_validation" :
                                                              "unsupported");
      out << ",\"code\":";
      quoted(out, v.code);
      out << ",\"detail\":";
      quoted(out, v.detail);
      out << '}';
    }
    out << "],\"source_channel_masks\":[";
    for (std::size_t c = 0; c < s.channel_uses.size(); ++c) {
      if (c)
        out << ',';
      const auto &use = s.channel_uses[c];
      out << "{\"path\":";
      quoted(out, source_path(i, pi, use.value));
      out << ",\"original_bits\":" << use.channels.original_bits
          << ",\"unknown_bits\":" << use.channels.unknown_bits
          << ",\"auxiliary\":" << (use.channels.auxiliary ? "true" : "false") << '}';
    }
    out << "],\"resource_bindings\":[";
    for (std::size_t r = 0; r < s.resources.size(); ++r) {
      if (r)
        out << ',';
      const auto &b = s.resources[r];
      const auto &ref = i.source().references.at(b.reference);
      out << "{\"path\":";
      quoted(out, source_path(i, pi, ref.source));
      out << ",\"uri\":";
      quoted(out, ref.uri);
      out << ",\"resource_dataset\":";
      quoted(out, ref.resource_dataset);
      out << ",\"destination_mesh_map\":";
      if (b.destination_map)
        quoted(out, mesh_map_name(*b.destination_map));
      else
        out << "null";
      out << '}';
    }
    out << "]}";
  }
  out << "],\"source_channel_mapping\":[";
  bool first = true;
  for (const auto &m : channels) {
    if (!first)
      out << ',';
    first = false;
    out << "{\"bit\":" << m.bit << ",\"channel\":";
    quoted(out, channel_name(m.channel));
    out << ",\"requires_workflow_contract\":"
        << (m.contract == ChannelContract::RequiresWorkflowContract ? "true" : "false") << '}';
  }
  out << "]}";
}
}  // namespace bts::spsm::semantic
