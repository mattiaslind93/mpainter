#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bts {
// EEVEE has no adaptive tessellation. Choose a uniform Simple level from the
// longest original edge in object space; the geometry budget can reduce it.
inline int edge_length_subdivision(double longest,double maximum){
 if(!std::isfinite(longest)||longest<0||!std::isfinite(maximum)||maximum<=0)throw std::invalid_argument("Invalid subdivision edge length");
 if(longest<=maximum)return 0;
 return int(std::clamp(std::ceil(std::log2(longest)-std::log2(maximum)),0.,11.));
}
}
