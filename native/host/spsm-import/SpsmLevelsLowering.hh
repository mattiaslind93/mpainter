// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SpsmLevelsParser.hh"

namespace bts {
// Destination interpretation policies, never source enum ordinals. Unknown
// preserves disabled source contributions. NativeScalarLevels explicitly opts
// into reciprocal gamma, native signed intermediate values, source clamp 0=off /
// 1=final [0,1], and equal RGB components interpreted as scalar working data.
struct SpsmLevelsOptions {
  SpsmLevelsPolicy policy = SpsmLevelsPolicy::Unknown;
};
// Detached, strongly atomic reconstruction from retained source IR. Supports
// independent uniform Roughness Fill -> Levels chains only; never adds effects
// to bitmap/native-material/anchor bindings or repairs unknown mask/group data.
// Keep original source omissions as provenance; restoration is separately noted.
PreparedSpsm prepare_spsm_levels(const PreparedSpsm &original, SpsmLevelsOptions = {});
}  // namespace bts
