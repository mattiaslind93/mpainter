#pragma once
#include "AssetCatalog.hh"
#include "SpsmBitmapLowering.hh"
#include "SpsmMeshMapLowering.hh"
namespace bts {
// Explicit native interpretation choices, never meanings inferred from source
// colorSpace/alpha ordinals. Unknown keeps Color hydration off.
enum class SpsmBitmapColorInput { Unknown, NativeLinearRec709RGBLevelsIgnoreA,
                                NativeACEScgRGBLevelsIgnoreA };
// GUI-independent native application defaults; no archive/cache IO. Explicit
// Unknown overrides disable the corresponding use; conflicts/unknown values throw.
struct SpsmSourcePolicy {
  QString uniform_input_space = "Linear Rec.709";
  SpsmBitmapLoweringPolicy bitmaps;
  bool roughness_raw_r_ignore_a = false;
  SpsmBitmapColorInput bitmap_color = SpsmBitmapColorInput::Unknown;
  QString bitmap_summary;
  SpsmMeshMapOptions mesh_maps;
  QString mesh_map_summary;
};
SpsmSourcePolicy parse_spsm_source_policy(const QStringList &);
// Host-only worker preparation. Original bytes are immutable/shared COW. The
// explicitly opted-in independent v2 Roughness, named source-linear RGB and
// bounded ordered bitmap mask subsets may hydrate; mask policy is separate.
// no procedural packages execute and no GUI/GPU work occurs here.
struct PreparedSpsmSource {
  PreparedSpsm prepared;
  std::shared_ptr<const QByteArray> original_bytes;
  QString source_sha256, cache_path, uniform_input_space;
  SpsmSourcePolicy policy;
  QStringList policy_tags;
  QString
      report; // Produced on the worker, including original diagnostic paths.
};
void configure_spsm_source_hydration();
// Cache-only GUI-safe accessors: no file IO, source hashing or worker waits.
std::shared_ptr<const PreparedSpsmSource>
prepared_spsm_source(const StudioAsset &);
QString spsm_source_report(const StudioAsset &);
QString spsm_source_summary(const StudioAsset &);
bool spsm_source_partial(const StudioAsset &);
} // namespace bts
