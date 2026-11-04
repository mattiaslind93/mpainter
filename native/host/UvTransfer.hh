#pragma once
#include "SceneSnapshot.hh"
#include <cmath>
namespace bts {
using TangentFrame=std::array<std::array<float,3>,3>;
inline TangentFrame uv_tangent_frame(const UvFaceTriangle &triangle){
  const auto subtract=[](auto a,auto b){for(int c=0;c<3;++c)a[c]-=b[c];return a;};const auto dot=[](auto a,auto b){double value=0;for(int c=0;c<3;++c)value+=double(a[c])*b[c];return value;};const auto cross=[](auto a,auto b){return std::array<float,3>{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};const auto normalize=[&](auto v){const auto length=std::sqrt(dot(v,v));if(length>1e-20)for(auto &c:v)c=float(c/length);return v;};
  const auto a=subtract(triangle.positions[1],triangle.positions[0]),b=subtract(triangle.positions[2],triangle.positions[0]);const double u1=triangle.corners[1][0]-triangle.corners[0][0],v1=triangle.corners[1][1]-triangle.corners[0][1],u2=triangle.corners[2][0]-triangle.corners[0][0],v2=triangle.corners[2][1]-triangle.corners[0][1],det=u1*v2-v1*u2;const auto normal=normalize(cross(a,b));if(std::abs(det)<1e-20)return {{{1,0,0},{0,1,0},{0,0,1}}};std::array<float,3> tangent;for(int c=0;c<3;++c)tangent[c]=float((v2*a[c]-v1*b[c])/det);tangent=normalize(tangent);auto bitangent=normalize(cross(normal,tangent));if(det<0)for(auto &c:bitangent)c=-c;return {tangent,bitangent,normal};
}
inline std::array<float,9> uv_normal_transform(const TangentFrame &source,const TangentFrame &target){std::array<float,9> matrix{};for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int axis=0;axis<3;++axis)matrix[r*3+c]+=target[r][axis]*source[c][axis];return matrix;}
struct UvTransferSample {std::array<float,2> source{};std::array<float,9> normal{1,0,0,0,1,0,0,0,1};bool valid=false;};
struct UvTransferMap {int resolution=0;std::map<int,std::vector<UvTransferSample>> tiles;};
}
