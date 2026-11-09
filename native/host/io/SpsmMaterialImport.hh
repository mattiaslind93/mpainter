#pragma once
#include "SmartMaterialPreset.hh"
#include "../spsm/semantic/SpsmSemantic.hh"
#include <functional>
#include <map>

namespace bts {
enum class SpsmDiagnosticKind { Assumption, Omission, SourceMetadata };
struct SpsmImportDiagnostic {
  std::size_t preset = 0;
  spsm::NodeId source = 0;
  SpsmDiagnosticKind kind = SpsmDiagnosticKind::Omission;
  std::string_view code, detail;  // Static descriptions; original values remain in source.
};
struct SpsmLayerBinding {
  spsm::NodeId source = 0;
  QUuid native_layer;
  bool disabled = false;
};
struct SpsmActionBinding {
  spsm::NodeId source = 0;
  QUuid native_layer;
  bool lowered = false;
};
struct SpsmImportOptions {
  // Explicit source policy supplied by the host, normally configured OCIO
  // Linear Rec.709 -> scene-linear ACEScg. No callback means color is skipped;
  // scalar data channels do not pass through this conversion.
  std::function<std::array<float, 3>(std::array<float, 3>)> uniform_rgb_to_scene_linear;
  bool native_actions = false;  // Final lowering follows optional bitmap hydration.
};
struct PreparedSpsm {
  SmartMaterialPreset material;
  // Owns all unchanged preset streams, blobs, enums and resource descriptors.
  // Resource pixels and the original complete HDF5 file are NOT copied here.
  // The host must retain the source archive when persisting a partial import.
  std::shared_ptr<const spsm::semantic::Inspection> source;
  std::vector<SpsmImportDiagnostic> diagnostics;
  std::vector<SpsmLayerBinding> layers;
  std::vector<SpsmActionBinding> actions;
  std::map<spsm::NodeId, spsm::NodeId> initial_fill_actions;
  std::map<spsm::NodeId, std::uint64_t> bitmap_content_channels;
  std::map<spsm::NodeId, FillProjection>
      bitmap_content_sources;  // Independent ordered UV image sources.
  std::shared_ptr<const LinearColorTransform> bitmap_color_transform;
  std::map<spsm::NodeId, FillProjection>
      bitmap_mask_sources;  // Worker-resolved direct mask images.
  bool partial = false;
  bool appearance_verified = false;
  std::string diagnostic_path(const SpsmImportDiagnostic &) const;
};
// Pure preparation: creates detached native layers, never edits a destination,
// registers assets, opens resources, executes graphs or mutates the inspection.
// Partial output requires an explicit host report/acceptance before applying.
// Source color interpretation is an explicit host policy (normally inferred
// Linear Rec.709), reported as an assumption; this unit does not verify it.
PreparedSpsm prepare_spsm_material(spsm::semantic::Inspection &&, SpsmImportOptions = {});
PreparedSpsm prepare_spsm_material(const std::filesystem::path &, SpsmImportOptions = {});
// Detached adaptation of this unit's canonical uniform Bump subset. Legacy
// origin-zero targets rebase Fill constants and translate Signed Addition into
// Linear Dodge, including folders. Unsupported authored Bump data throws before
// copying/adapting; neither argument is modified or rasterized.
SmartMaterialPreset spsm_material_for_destination(const PreparedSpsm &, const TextureSet &);
}  // namespace bts
