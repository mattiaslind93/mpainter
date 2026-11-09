#pragma once
// Native-only persistence adapter. Keep Blender sharing types out of the
// model/UI-facing MaterialLayeringSources.hh.
#include "BLI_implicit_sharing_ptr.hh"
#include <cstddef>
#include <vector>
namespace blender {
struct Main;
}
namespace bts {
using MaterialLayerSourceSnapshotOwners = std::vector<blender::ImplicitSharingPtr<>>;
MaterialLayerSourceSnapshotOwners retain_material_layer_source_snapshot_headers(
    blender::Main &live, blender::Main &snapshot);
std::size_t pack_material_layer_source_snapshots(blender::Main &snapshot);
}  // namespace bts

#include "FillTexture.hh"
#include <array>
#include <string>
#include <map>
#include <QJsonArray>
namespace bts {
// Column-major matrix, extracted from the ACTUAL OCIO processor. Only known
// linear AP1/709 matrix processors, or one narrowly validated sRGB curve
// followed by RGB matrices, pass. No post-filter nonlinear conversion.
struct MaterialLayerSourceColorTransform {
  std::array<float, 9> to_aces{};
  std::string processor;
  // New sRGB Color only: explicit nearest taps and float bilinear interpolation.
  // Decoded runtime pixels remain AP1; only the detached encoded graph applies
  // this exact admitted OCIO moncurve before interpolation.
  bool srgb = false;
  double gamma = 1, offset = 0, breakpoint = 0, slope = 1;
};
MaterialLayerSourceColorTransform material_layer_source_color_transform(const FillTexture &);
MaterialLayerSourceColorTransform material_layer_linear_color_transform(const FillTexture &);
// Ordinary source key 0; sequences inspect EVERY original member, never the
// container's preview metadata. Sequence members admit linear AP1/709 only.
using MaterialLayerColorTransforms = std::map<int, MaterialLayerSourceColorTransform>;
MaterialLayerColorTransforms material_layer_color_source_transforms(const FillTexture &);
QJsonArray material_layer_color_members_json(const FillTexture &);
}  // namespace bts

#include <QUuid>
#include <cstdint>
namespace blender {
struct Scene;
namespace bke::blendfile {
class PartialWriteContext;
}
}  // namespace blender
namespace bts {
class TextureDocument;
struct ProjectResourceImages;
struct MaterialLayerSourceBinding {
  QUuid set;
  std::uint32_t material_uid = 0, image_uid = 0;
  int input = 0, channel = 0;
  std::shared_ptr<const FillTexture> source;
  // A sequence binding owns one immutable member plus value-only parent provenance.
  std::shared_ptr<const FillTexture> sequence;
  int tile = 0;
  int node_identifier = 0;
  std::array<int, 4> tap_identifiers{};
  std::array<double, 2> tiling{1, 1}, offset{};
  double rotation = 0;
};
using MaterialLayerSourceBindings = std::vector<MaterialLayerSourceBinding>;
// Owner capture contains values/immutable pixels only, no live Main/node pointers.
MaterialLayerSourceBindings capture_material_layer_source_bindings(blender::Main &,
                                                                   const TextureDocument &);
// Worker-only snapshot conversion. No live graph/cache is changed.
void pack_material_layer_source_bindings(blender::bke::blendfile::PartialWriteContext &,
                                         blender::Scene &,
                                         const MaterialLayerSourceBindings &,
                                         const ProjectResourceImages &);
void prune_unused_material_resource_copies(blender::bke::blendfile::PartialWriteContext &,
                                           const ProjectResourceImages &);
// Same validated resource read pass supplies associations. Legacy files without
// this representation marker retain the previous Open path.
bool validate_packed_material_layer_sources(blender::Main &,
                                            const blender::Scene &,
                                            const TextureDocument &,
                                            const ProjectResourceImages &);
}  // namespace bts
