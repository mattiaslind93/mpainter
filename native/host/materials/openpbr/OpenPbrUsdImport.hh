#pragma once
#include "OpenPbrMaterialX.hh"
#include <vector>
namespace bts {
struct OpenPbrUsdImage {
  OpenPbrImageBinding binding;
  QString authoring_layer;  // Resolve the filename relative to this USD layer.
};
struct OpenPbrUsdProfile {
  QString material_path, name;
  OpenPbrParameters parameters;
  std::map<std::string, OpenPbrUsdImage> images;
  float bump_scale = .001f, displacement_scale = 1, displacement_midlevel = .5f;
  bool displacement_enabled = false;
};
// Metadata only: read our pinned source-definition profiles from loose USD or
// USDZ. Image/arithmetic networks outside the supported transport are rejected,
// never flattened into misleading constants. No texture decode or scene edits.
std::vector<OpenPbrUsdProfile> read_openpbr_usd_profiles(const QString &path);
}  // namespace bts
