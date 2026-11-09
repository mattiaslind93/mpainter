#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
namespace bts {
// Immutable internal consumer contract; never an artist channel/export policy.
enum class NativeWorldNormalSurfaceContract { SourceAuthored, PainterUndisplaced };
inline std::string_view native_world_normal_surface_name(NativeWorldNormalSurfaceContract contract)
{
  switch (contract) {
    case NativeWorldNormalSurfaceContract::SourceAuthored:
      return "source-authored";
    case NativeWorldNormalSurfaceContract::PainterUndisplaced:
      return "painter-undisplaced";
  }
  throw std::invalid_argument("Unknown native world Normal surface contract");
}
inline std::string native_world_normal_surface_identity(const std::string &identity,
                                                        NativeWorldNormalSurfaceContract contract)
{
  return identity +
         ";native-world-surface=" + std::string(native_world_normal_surface_name(contract));
}
inline std::string_view native_world_normal_surface_warning()
{
  return "Bump shading uses the current mesh. This material's original displacement is kept separately and is not included; the original displaced result is not reproduced.";
}
}  // namespace bts
