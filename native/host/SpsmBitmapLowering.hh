#pragma once
#include "SpsmBitmapTexture.hh"
#include "SpsmMaterialImport.hh"
#include "SpsmLevelsParser.hh"
namespace bts {
// Caller assumptions, deliberately unrelated to source enum ordinals. Unknown
// keeps the original preparation unchanged apart from omission diagnostics.
enum class SpsmBitmapUV { Unknown, Projection0CenteredRepeatFactors };
// Only HQRepeat is supported in this unit. Other choices remain reported gaps.
enum class SpsmBitmapSampling { Unknown, HQRepeat, SharpRepeat, NearestRepeat };
enum class SpsmMaskBitmapPolicy { Unknown, NativeOrderedFillLevelsRawRIgnoreA };
struct SpsmBitmapLoweringPolicy {
  SpsmBitmapPolicy pixels;
  SpsmBitmapUV uv = SpsmBitmapUV::Unknown;
  SpsmBitmapSampling sampling = SpsmBitmapSampling::Unknown;
  SpsmLevelsPolicy levels = SpsmLevelsPolicy::Unknown;
  SpsmMaskBitmapPolicy mask = SpsmMaskBitmapPolicy::Unknown;
};
using SpsmBitmapResolver =
    std::function<std::shared_ptr<const SpsmBitmapTexture>(
        const spsm::bitmap::Descriptor &, const SpsmBitmapPolicy &,
        spsm::ReadOptions)>;
// Per-use interpretation never enters the raw decode/cache identity. Null Color
// is Unknown/off; non-null explicitly chooses native source-linear RGB Levels
// and ignored A under the supplied named transform. The legacy API keeps R use.
struct SpsmBitmapUse {
  bool roughness = true;
  std::shared_ptr<const LinearColorTransform> color;
};
// Detached, strongly atomic enhancement of compiler-disabled single-Fill
// selected Roughness and/or explicitly interpreted RGB bindings. Every selected
// dependency is admitted before decode; publication is one completed layer.
// Distinct records stay distinct; same-record usages share immutable storage.
// Original omissions remain an audit, Normal and unsupported masks/effects stay
// disabled. The caller must retain the complete original SPSM asset separately.
PreparedSpsm enhance_prepared_spsm_bitmaps(const PreparedSpsm &,
                                           const SpsmBitmapLoweringPolicy &,
                                           const SpsmBitmapResolver &,
                                           const SpsmBitmapUse & = {});
} // namespace bts
