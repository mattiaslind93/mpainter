#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace bts {
// Original deterministic value noise. No package bytecode or proprietary graph
// is inspected or evaluated. Keep the integer hash identical to the GLSL path.
inline std::uint32_t grunge_hash(std::uint32_t x,std::uint32_t y,std::uint32_t seed){
 auto n=x*374761393u+y*668265263u+seed*1442695041u;
 n=(n^(n>>13))*1274126177u;return n^(n>>16);
}
inline float grunge_noise(float x,float y,std::uint32_t seed){
 const auto ix=int(std::floor(x)),iy=int(std::floor(y));float u=x-ix,v=y-iy;u=u*u*(3-2*u);v=v*v*(3-2*v);
 const auto sample=[&](int dx,int dy){return float(grunge_hash(std::uint32_t(ix+dx),std::uint32_t(iy+dy),seed)&65535u)/65535.f;};
 return std::lerp(std::lerp(sample(0,0),sample(1,0),u),std::lerp(sample(0,1),sample(1,1),u),v);
}
inline float native_grunge(float u,float v,float scale,int seed,int pattern){
 if(pattern==3){
  const auto s=std::uint32_t(seed);const float wob=(grunge_noise(u*scale*2.2f,v*scale*1.6f,s+5u)-.5f)*.28f;
  const float col=u*scale*6.5f+wob,s1=grunge_noise(col,v*scale*.32f,s+11u),s2=grunge_noise(col*2.15f+1.7f,v*scale*.72f+.9f,s+29u),s3=grunge_noise(col*4.3f+3.1f,v*scale*1.5f+2.3f,s+53u);
  const float streak=std::clamp((.52f*s1+.31f*s2+.17f*s3-.38f)*2.1f,0.f,1.f);
  const float env=std::clamp(.35f+.65f*grunge_noise(u*scale*1.8f+.4f,v*scale*.45f+.2f,s+71u),0.f,1.f);
  const float fine=grunge_noise(u*scale*5.f+1.3f,v*scale*3.5f+.7f,s+97u);
  return std::clamp(streak*env*(.55f+.45f*fine),0.f,1.f);
 }
 float x=u*scale,y=v*scale;if(pattern==2){x*=8;y*=.25f;}
 float result=0,weight=.533333333f;
 for(int i=0;i<4;++i){result+=weight*grunge_noise(x,y,std::uint32_t(seed+i*17));x=x*2.03f+3.1f;y=y*2.03f+1.7f;weight*=.5f;}
 if(pattern==1)result=std::clamp((result-.35f)*2.f,0.f,1.f);
 return result;
}
// Original 2D noise projected from three spatial axes. Position is in one
// shared object-normalized frame, never a UV coordinate or island-local frame.
inline float native_grunge_triplanar(const std::array<float,3> &position,const std::array<float,3> &normal,float scale,int seed,int pattern){
 std::array<float,3> weights{};float total=0;
 for(int c=0;c<3;++c){const float n=std::abs(normal[c]);weights[c]=n*n*n*n;total+=weights[c];}
 if(total<=1e-20f)return .5f;
 if(pattern==3)return (weights[0]*native_grunge(position[2]+.5f,.5f-position[1],scale,seed,pattern)+weights[2]*native_grunge(position[0]+.5f,.5f-position[1],scale,seed,pattern)+weights[1]*.15f*native_grunge(position[0]+.5f,position[2]+.5f,scale,seed,pattern))/total;
 float value=0;for(int c=0;c<3;++c){const int x=c==0?1:0,y=c==2?1:2;value+=weights[c]/total*(c==2&&pattern==3?.15f:1.f)*native_grunge(position[x]+.5f,position[y]+.5f,scale,seed,pattern);}
 return value;
}
}
