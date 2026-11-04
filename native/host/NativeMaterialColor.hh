#pragma once
#include <array>
namespace bts {
// Linear primary conversion only. ColorPicker already owns display/sRGB gamma.
// Keep signed/HDR components and alpha intact; no gamut clipping here.
inline std::array<float,4> native_material_color(const std::array<float,9> &matrix,
                                                const std::array<float,4> &value)
{
  auto result=value;
  for(int row=0;row<3;++row){result[row]=0;for(int column=0;column<3;++column)result[row]+=matrix[column*3+row]*value[column];}
  return result;
}
}
