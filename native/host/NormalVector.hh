#pragma once
#include <array>
#include <cmath>
namespace bts {
using NormalVector=std::array<float,3>;
inline NormalVector normalized_normal(NormalVector value){const float length=std::sqrt(value[0]*value[0]+value[1]*value[1]+value[2]*value[2]);if(length<1e-12f)return {0,0,1};for(auto &component:value)component/=length;return value;}
inline NormalVector reoriented_normal(NormalVector base,NormalVector detail){
  base=normalized_normal(base);detail=normalized_normal(detail);
  if(base[2]<-1+1e-6f)return normalized_normal({detail[0],-detail[1],-detail[2]});
  const NormalVector t{base[0],base[1],base[2]+1},u{-detail[0],-detail[1],detail[2]};float dot=0;for(int c=0;c<3;++c)dot+=t[c]*u[c];NormalVector result;for(int c=0;c<3;++c)result[c]=t[c]*dot-u[c]*t[2];return normalized_normal(result);
}
inline NormalVector whiteout_normal(NormalVector base,NormalVector detail){base=normalized_normal(base);detail=normalized_normal(detail);return normalized_normal({base[0]+detail[0],base[1]+detail[1],base[2]*detail[2]});}
}
