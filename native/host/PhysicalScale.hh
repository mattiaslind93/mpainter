#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace bts {
struct SceneSnapshot;
struct UnitSettings {
  bool internal = true;
  double custom_cm_per_file_unit = 1;
  bool auto_physical = false;
  bool operator==(const UnitSettings &) const = default;
};
struct SourceUnits {
  double native_per_file_unit = 1;
  double cm_per_file_unit = 1;  // OBJ's unitless numbers default to centimeters.
  bool exact = true;
  bool operator==(const SourceUnits &) const = default;
};
void validate_units(const UnitSettings &, const SourceUnits &);
double centimeters_per_native_unit(const UnitSettings &, const SourceUnits &);
SourceUnits inspect_source_units(const std::string &);
// RMS derivative lengths weighted by UV area, measured in world coordinates.
// Authored charts and their rotation remain intact; this measures their density.
std::array<double, 2> native_length_per_uv(const SceneSnapshot &, std::uint32_t material_uid);
}  // namespace bts
