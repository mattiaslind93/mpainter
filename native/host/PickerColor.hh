#pragma once
#include <array>

class QColor;

namespace bts {
using PickerColor = std::array<float, 4>;
// Numeric color conversion does not depend on the picker widget.
PickerColor picker_from_srgb(const QColor &);
QColor picker_to_srgb(const PickerColor &);
}
