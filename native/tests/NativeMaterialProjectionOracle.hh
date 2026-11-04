#pragma once
#include "FillProjection.hh"
// Independent analytic diagnostic formulas. No product projection/sampler or
// CPU material graph evaluation is used to derive expected GPU pixels.
namespace bts::projection_oracle {
using V=std::array<double,3>;
struct Sample {V point;double weight;};
inline V inverse_rotation(V value,const V &angles){for(int step=0;step<3;++step){const int axis=2-step,x=(axis+1)%3,y=(axis+2)%3;const double a=-angles[axis]*std::numbers::pi/180,c=std::cos(a),s=std::sin(a),vx=value[x],vy=value[y];value[x]=c*vx-s*vy;value[y]=s*vx+c*vy;}return value;}
inline bool covered(std::array<double,2> &uv,int wrap,bool modulo){for(int c=0;c<2;++c){const bool repeat=wrap==0||(c==0?wrap==2:wrap==3);if(!repeat&&(uv[c]<0||uv[c]>1))return false;if(repeat&&modulo)uv[c]-=std::floor(uv[c]);}return true;}
inline std::array<double,2> map(const FillProjection &p,std::array<double,2> uv,bool spatial,std::array<double,2> density,double cm){const double a=-p.rotation*std::numbers::pi/180,c=std::cos(a),s=std::sin(a);double x=uv[0]-.5-(p.mode==1?0:p.offset[0]),y=uv[1]-.5-(p.mode==1?0:p.offset[1]);if(!spatial&&p.physical){x*=density[0]*cm;y*=density[1]*cm;}return {(c*x-s*y)*p.scale[0]/(!spatial&&p.physical?p.custom_size_cm[0]:1)+.5,(s*x+c*y)*p.scale[1]/(!spatial&&p.physical?p.custom_size_cm[1]:1)+.5};}
inline std::vector<Sample> samples(const FillProjection &p,V world,V normal,V original,bool uv_domain,int tile,double extent,std::array<double,2> density,double cm,double warp_u=0,double warp_v=0){
 if(p.mode==6)return {{original,1}};
 if(p.mode==0){auto xy=map(p,{original[0],original[1]},false,density,cm);auto local=xy;if(uv_domain){local[0]-=(tile-1001)%10;local[1]-=(tile-1001)/10;}if(!covered(local,p.wrap,false))return {};return {{{xy[0],xy[1],original[2]},1}};}
 for(int c=0;c<3;++c)world[c]-=p.offset3[c]*(p.mode==1?1:extent);world=inverse_rotation(world,p.rotation3);normal=inverse_rotation(normal,p.rotation3);V denominator;
 for(int c=0;c<3;++c){const int axis=p.mode==1?c:c==1?2:c==2?1:0;denominator[c]=p.scale3[c]*(p.physical?p.custom_size_cm[axis]/cm:extent);world[c]/=denominator[c];}
 const auto finish=[&](std::array<double,2> uv,double depth,double weight)->std::vector<Sample>{uv=map(p,uv,true,density,cm);if(!covered(uv,p.wrap,true))return {};return {{{uv[0],uv[1],uv_domain?0:depth},weight}};};
 if(p.mode==1){V weights;double total=0;for(int c=0;c<3;++c){weights[c]=std::pow(std::abs(normal[c]*p.scale3[c]),1+15*p.hardness);total+=weights[c];}std::vector<Sample> result;for(int axis=0;axis<3;++axis){const int x=(axis+1)%3,y=(axis+2)%3;auto one=finish({world[x]+.5,world[y]+.5},world[axis],weights[axis]/total);result.insert(result.end(),one.begin(),one.end());}return result;}
 if(p.mode==2)return finish({world[0]+.5,world[2]+.5},world[1],1);
 if(p.mode==3){const double r=std::sqrt(world[0]*world[0]+world[1]*world[1]+world[2]*world[2]);return finish(r<1e-12?std::array<double,2>{.5,.5}:std::array<double,2>{.5+std::atan2(world[0],-world[1])/(2*std::numbers::pi),.5+std::asin(world[2]/r)/std::numbers::pi},r,1);}
 if(p.mode==4){const double r=std::hypot(world[0],world[1]);return finish({.5+(r<1e-12?0:std::atan2(world[0],-world[1]))/(p.cylinder_angle*std::numbers::pi/180),world[2]+.5},r,1);}
 // An affine, genuinely deformed Hermite plane has a closed-form metric
 // projection. This expected solution does not call WarpGrid::project.
 const double rx=denominator[0]/denominator[1],rz=denominator[2]/denominator[1],mx=rx*rx,mz=rz*rz,a=mx+warp_u*warp_u,b=warp_u*warp_v,c=mz+warp_v*warp_v,det=a*c-b*b;
 const double x=mx*world[0]+warp_u*(world[1]+.5),y=mz*world[2]+warp_v*(world[1]+.5),u=(c*x-b*y)/det,v=(a*y-b*x)/det;
 V n{warp_u*rz,-rx*rz,rx*warp_v};const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);for(auto &value:n)value/=length;const double depth=(world[0]-u)*rx*n[0]+(world[1]+.5-warp_u*u-warp_v*v)*n[1]+(world[2]-v)*rz*n[2];if(-depth<0||-depth>p.warp_depth)return {};return finish({u+.5,v+.5},depth,1);
}
inline std::array<double,4> coordinate(const std::vector<Sample> &samples){std::array<double,4> result{};for(const auto &s:samples){for(int c=0;c<3;++c)result[c]+=s.point[c]*s.weight;result[3]+=s.weight;}if(result[3]>0)for(int c=0;c<3;++c)result[c]/=result[3];return result;}
}
