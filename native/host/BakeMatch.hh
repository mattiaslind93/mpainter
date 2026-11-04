#pragma once
#include <string_view>
namespace bts {
// external vendor's keyword rule uses the first marker, so Leg_low_top and Leg_high share Leg.
inline std::string_view bake_match_key(std::string_view name,std::string_view marker){
  if(marker.empty())return name;
  const auto found=name.find(marker);return found==std::string_view::npos?name:name.substr(0,found);
}
inline bool bake_names_match(std::string_view low,std::string_view high,std::string_view low_marker="_low",std::string_view high_marker="_high"){
  return bake_match_key(low,low_marker)==bake_match_key(high,high_marker);
}
inline bool bake_ignores_backfaces(std::string_view name,std::string_view marker){return !marker.empty()&&name.find(marker)!=std::string_view::npos;}
}
