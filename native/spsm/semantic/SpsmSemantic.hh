// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmReader.hh"
#include <optional>
#include <iosfwd>
#include <memory>
#include <utility>

namespace bts::spsm::semantic {
// These are destination concepts, never source enum ordinals.
enum class Channel {
  BaseColor,
  Bump,
  Specular,
  Opacity,
  Glossiness,
  Roughness,
  Metallic,
  Normal,
  AmbientOcclusion,
  Diffuse
};
enum class ChannelContract { DirectSlot, RequiresWorkflowContract };
struct ChannelMapping {
  std::uint64_t bit;
  Channel channel;
  ChannelContract contract;
};
std::span<const ChannelMapping> channel_mapping_table();
std::string_view channel_name(Channel);
struct Channels {
  std::uint64_t original_bits = 0, unknown_bits = 0;
  bool auxiliary = false;  // A zero source mask is not a disabled output.
  std::vector<ChannelMapping> mappings;
};
Channels inspect_channels(std::uint64_t);
struct ChannelUse {
  NodeId source, value;
  Channels channels;
};

// All references address the owned, unchanged structural source arena. Unknown
// fields, wire tags, float payloads, blobs and source enum values stay there.
struct FieldRef {
  NodeId object;
  std::size_t index;
};
struct Location {
  enum class Edge { Root, Field, Array };
  std::optional<NodeId> parent;
  Edge edge = Edge::Root;
  std::size_t index = 0;
};
struct Blending {
  NodeId source;
  std::optional<NodeId> channel_types, source_mode, opacity;
};
enum class ActionKind { Group, Fill, Levels, ProceduralFilter, Paint, Unknown };
struct Action {
  NodeId source;
  ActionKind kind = ActionKind::Unknown;
  std::optional<NodeId> label, uid, enabled;
  std::optional<std::size_t> channels;  // PresetInspection::channel_uses index.
  std::vector<std::size_t> children;    // Ordered nested group action indices.
  std::vector<NodeId> sources;          // Ordered raw source nodes; never collapsed.
  std::vector<Blending> blending;
  std::vector<FieldRef> projection;  // Original values, including source enums.
};
struct Layer {
  NodeId source;
  bool group = false;
  std::optional<NodeId> label, uid, enabled, mask_enabled, mask_initial;
  std::optional<std::size_t> parent;
  std::vector<std::size_t> children, actions, mask_actions;
  std::vector<Blending> blending;
  std::vector<FieldRef> geometry;
};
struct Source {
  NodeId source;
  enum class Kind { Uniform, Bitmap, Procedural, Unknown };
  Kind kind = Kind::Unknown;
  std::optional<std::size_t> channels;  // PresetInspection::channel_uses index.
  std::optional<NodeId> uid, opacity, color, bitmap, procedural;
};
struct Procedural {
  NodeId source;
  std::optional<NodeId> uid, uri, outputs, inputs;
  std::vector<NodeId> tweaks;  // Ordered DataTweak* nodes with exact value types.
};
enum class DestinationMeshMap { WorldNormal, AmbientOcclusion, Curvature, Position, Thickness };
std::string_view mesh_map_name(DestinationMeshMap);
struct ResourceBinding {
  std::size_t reference;  // Archive::references index retains full URI/version.
  std::optional<DestinationMeshMap> destination_map;
  enum class State { DestinationMeshMap, EmbeddedDescriptor, Unresolved };
  State state = State::Unresolved;
};
enum class Capability { NativePrimitive, RequiresValidation, Unsupported };
struct Finding {
  NodeId source;
  Capability capability;
  std::string_view code, detail;  // Static descriptions; source data is not copied.
};
struct PresetInspection {
  std::vector<Location> locations;
  std::optional<NodeId> data_version;
  std::vector<ChannelUse> channel_uses;
  std::vector<Layer> layers;
  std::vector<Action> actions;
  std::vector<Source> sources;
  std::vector<Procedural> procedurals;
  std::vector<std::size_t> roots;
  std::vector<ResourceBinding> resources;
  std::vector<Finding> findings;
  // This unit has no native lowering or evaluator, even for candidate primitives.
  bool native_import_available = false;
  bool rendered = false;
};
struct InspectOptions {};
class Inspection {
 public:
  const Archive &source() const
  {
    return *source_;
  }
  std::vector<PresetInspection> presets;
  std::optional<std::size_t> primary_preset;  // Exactly /preset.bin, never a merge of variants.
 private:
  std::shared_ptr<const Archive> source_;
  explicit Inspection(std::shared_ptr<const Archive> source) : source_(std::move(source)) {}
  friend Inspection inspect_archive(Archive &&, InspectOptions);
};
Inspection inspect_archive(Archive &&, InspectOptions = {});
std::string source_path(const Inspection &, std::size_t preset, NodeId);
const Node &source_value(const Inspection &, std::size_t preset, NodeId);
const Field &source_field(const Inspection &, std::size_t preset, FieldRef);
// Streaming JSON capability report, not a serialized native material/import.
void write_report(std::ostream &, const Inspection &);
}  // namespace bts::spsm::semantic
