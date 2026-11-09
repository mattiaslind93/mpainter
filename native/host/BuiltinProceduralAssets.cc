#include "BuiltinProceduralAssets.hh"
namespace bts {
const std::vector<StudioAsset> &builtin_procedural_assets()
{
  static const std::vector<StudioAsset> empty;
  return empty;
}
}  // namespace bts
