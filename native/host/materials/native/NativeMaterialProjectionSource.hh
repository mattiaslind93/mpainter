// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuLayerParameters.hh"
#include "GpuWarpSource.hh"
#include <cctype>
#include <string>

namespace bts {
// The source graph receives projected attributes; view/AO/owner/displacement
// context stays on the immutable real surface. Numeric edits are SSBO data.
inline std::string native_material_projection_source()
{
  std::string result;
  for (std::size_t i = 0; i < std::size(GpuLayerParameters::fields); ++i) {
    const auto &f = GpuLayerParameters::fields[i];
    result += "#define bts_fill_" + std::string(f.name) + " " + f.type + "(native_projection[" +
              std::to_string(i + 1) + "]." + f.components + ")\n";
  }
  // Reuse the actual bounded Hermite solver, with a distinct symbol namespace
  // and SSBO getter. No projected geometry or raster pixels are copied here.
  std::string warp = gpu_warp::source;
  const std::string getter =
      "return texelFetch(layer_parameters_img,ivec2(WARP_DATA_START+index,0),0);";
  const auto at = warp.find(getter);
  if (at == std::string::npos)
    throw std::logic_error("Native Warp getter contract changed");
  warp.replace(at,
               getter.size(),
               "return native_projection[" +
                   std::to_string(1 + GpuLayerParameters::warp_data_start) + "+index];");
  for (std::size_t i = 0; i < warp.size();) {
    if (std::isalpha(static_cast<unsigned char>(warp[i])) || warp[i] == '_') {
      std::size_t end = i + 1;
      while (end < warp.size() &&
             (std::isalnum(static_cast<unsigned char>(warp[end])) || warp[end] == '_'))
        ++end;
      const auto token = warp.substr(i, end - i);
      std::string replacement;
      if (token.starts_with("warp_") || token == "WarpBasis" || token == "WarpSurfaceValue")
        replacement = "bts_fill_" + token;
      else if (token == "inverse_denominator")
        replacement = "bts_fill_inverse_denominator";
      if (!replacement.empty()) {
        warp.replace(i, end - i, replacement);
        i += replacement.size();
      }
      else
        i = end;
    }
    else
      ++i;
  }
  result += warp;
  result += R"GLSL(
vec3 bts_fill_point,bts_fill_weights;
vec2 bts_fill_center,bts_fill_spread;
float bts_fill_coverage,bts_fill_depth;
int bts_fill_copy;
bool bts_fill_enabled(){return native_projection[0].x!=0;}
vec3 bts_fill_rotate(vec3 p){return vec3(dot(bts_fill_rotation_x,p),dot(bts_fill_rotation_y,p),dot(bts_fill_rotation_z,p));}
int bts_fill_fold(inout vec3 p,inout vec3 n){
 if(bts_fill_projection_symmetry==0)return 0;int axis=int(bts_fill_symmetry_options.x);
 if(bts_fill_symmetry_options.y==0){float side=bts_fill_projection_offset[axis]>=bts_fill_symmetry_options.w?1:-1;if((p[axis]-bts_fill_symmetry_options.w)*side<0){p[axis]=2*bts_fill_symmetry_options.w-p[axis];n[axis]=-n[axis];return 1;}return 0;}
 int i=(axis+1)%3,j=(axis+2)%3;float x=p[i]-bts_fill_symmetry_center[i],y=p[j]-bts_fill_symmetry_center[j];vec2 origin=vec2(bts_fill_projection_offset[i]-bts_fill_symmetry_center[i],bts_fill_projection_offset[j]-bts_fill_symmetry_center[j]);
 float reference=length(origin)<1e-12?0:atan(origin.y,origin.x),sector=6.283185307179586/bts_fill_symmetry_options.z;
 float angle_to=atan(y,x)-reference,relative=angle_to-roundEven(angle_to/6.283185307179586)*6.283185307179586;int copy=int(floor(relative/sector+.5));float angle=-copy*sector,c=cos(angle),s=sin(angle);
 p[i]=bts_fill_symmetry_center[i]+c*x-s*y;p[j]=bts_fill_symmetry_center[j]+s*x+c*y;float ni=n[i],nj=n[j];n[i]=c*ni-s*nj;n[j]=s*ni+c*nj;return int(mod(float(copy),bts_fill_symmetry_options.z));
}
vec2 bts_fill_map(vec2 uv){
 vec3 p=vec3(uv,1);vec2 result=vec2(dot(bts_fill_map_u,p),dot(bts_fill_map_v,p));
 if((bts_fill_copy&1)!=0){if(bts_fill_symmetry_flip.x!=0)result.x=1-result.x;if(bts_fill_symmetry_flip.y!=0)result.y=1-result.y;}return result;
}
bool bts_fill_wrap(inout vec2 uv){
 bool u=bts_fill_projection_wrap==0||bts_fill_projection_wrap==2,v=bts_fill_projection_wrap==0||bts_fill_projection_wrap==3;
 if((!u&&(uv.x<0||uv.x>1))||(!v&&(uv.y<0||uv.y>1)))return false;
 if(u)uv.x=fract(uv.x);if(v)uv.y=fract(uv.y);return true;
}
// UV mode preserves the established unbounded affine source domains on
// repeating axes. In particular, whole-tile shifts reach the retained UDIM
// sampler; the source graph's own image wrapping still runs exactly once.
bool bts_fill_domain_covered(vec2 uv){
 bool u=bts_fill_projection_wrap==0||bts_fill_projection_wrap==2,v=bts_fill_projection_wrap==0||bts_fill_projection_wrap==3;
 return (u||(uv.x>=0&&uv.x<=1))&&(v||(uv.y>=0&&uv.y<=1));
}
float bts_fill_facing(float f){if(bts_fill_backface_options.z<1e-6)return f>=1-1e-8?1:0;return clamp((f-bts_fill_backface_options.x)/bts_fill_backface_options.y,0.0,1.0);}
bool bts_fill_prepare(){
 bts_fill_coverage=1;bts_fill_copy=0;bts_fill_weights=vec3(0,0,1);bts_fill_spread=vec2(0);bts_fill_depth=0;
 if(!bts_fill_enabled()||bts_fill_projection_mode==0||bts_fill_projection_mode==6)return true;
 vec3 world=bts_real_position,normal=bts_real_normal;bts_fill_copy=bts_fill_fold(world,normal);
 vec3 p=bts_fill_rotate(world-bts_fill_projection_offset)*bts_fill_inverse_denominator;
 bts_fill_point=p;
 if(bts_fill_projection_mode==1){
  vec3 n=bts_fill_rotate(normal)*bts_fill_projection_scale;vec3 weights=pow(abs(n),vec3(bts_fill_projection_hardness));float total=(weights.x+weights.y)+weights.z;if(total<1e-20)return false;bts_fill_weights=weights/total;return true;
 }
 vec3 n=bts_fill_rotate(normal)/bts_fill_inverse_denominator;if(dot(n,n)>1e-20)n=normalize(n);
 if(bts_fill_projection_mode==2){
  float depth=abs(p.y);if(bts_fill_projection_flags.x!=0&&(abs(p.x)>.5||abs(p.z)>.5||depth>1))return false;
  if(bts_fill_projection_flags.y!=0)bts_fill_coverage*=bts_fill_depth_hardness>=1?(depth<=1?1:0):clamp((1-depth)/(1-bts_fill_depth_hardness),0.0,1.0);
  if(bts_fill_projection_flags.z!=0)bts_fill_coverage*=bts_fill_facing(-n.y);
  bts_fill_center=p.xz+.5;bts_fill_spread=bts_real_footprint*bts_fill_inverse_denominator.xz;bts_fill_depth=p.y;
 }else if(bts_fill_projection_mode==3){
  float radius=length(p);if(bts_fill_projection_flags.x!=0&&radius>.5)return false;
  bts_fill_center=radius<1e-12?vec2(.5):vec2(.5+atan(p.x,-p.y)/6.283185307179586,.5+asin(clamp(p.z/radius,-1.0,1.0))/3.141592653589793);
  float angular=bts_real_footprint/max(1e-8,abs(1/bts_fill_inverse_denominator.x)*max(radius,.01));bts_fill_spread=angular/vec2(6.283185307179586,3.141592653589793);bts_fill_depth=radius;
 }else if(bts_fill_projection_mode==5){
  float depth;vec3 patch_normal,du,dv;if(!bts_fill_warp_project(p,bts_fill_center,depth,patch_normal,du,dv))return false;
  float reach=bts_fill_warp_info.w!=0?abs(depth):-depth;if(reach<-.000001||reach>bts_fill_warp_info.z)return false;
  if(bts_fill_projection_flags.x!=0&&(any(lessThan(bts_fill_center,vec2(0)))||any(greaterThan(bts_fill_center,vec2(1)))))return false;
  if(bts_fill_projection_flags.y!=0&&bts_fill_depth_hardness<1)bts_fill_coverage*=clamp((bts_fill_warp_info.z-reach)/(bts_fill_warp_info.z*(1-bts_fill_depth_hardness)),0.0,1.0);
  if(bts_fill_projection_flags.z!=0){vec3 sn=n*bts_fill_inverse_denominator;if(dot(sn,sn)>1e-24)sn=normalize(sn);else sn=vec3(0);bts_fill_coverage*=bts_fill_facing(dot(sn,patch_normal*sign(bts_fill_inverse_denominator)));}
  du/=bts_fill_inverse_denominator;dv/=bts_fill_inverse_denominator;float a=dot(du,du),b=dot(du,dv),c=dot(dv,dv),det=a*c-b*b;if(det<1e-18)return false;bts_fill_spread=bts_real_footprint*sqrt(vec2(c,a)/det);bts_fill_depth=depth;
 }else{
  float radius=length(p.xy),angle=bts_fill_cylinder_radians;bts_fill_center=vec2(.5+(radius<1e-12?0:atan(p.x,-p.y))/angle,p.z+.5);
  if(bts_fill_projection_flags.x!=0&&(radius>.5||abs(p.z)>.5||bts_fill_center.x<0||bts_fill_center.x>1))return false;
  if(bts_fill_projection_flags.z!=0){if(radius<1e-12)return false;bts_fill_coverage*=bts_fill_facing(dot(n.xy,p.xy)/radius);}
  bts_fill_spread=vec2(bts_real_footprint/(max(1e-8,min(abs(1/bts_fill_inverse_denominator.x),abs(1/bts_fill_inverse_denominator.y)))*max(.01,radius)*angle),bts_real_footprint/max(1e-8,abs(1/bts_fill_inverse_denominator.z)));bts_fill_depth=radius;
 }return bts_fill_coverage>0;
}
// Return straight source RGB and separate coverage after graph evaluation.
// Projection depth/radius is the third coordinate for planar/curved frames;
// triplanar uses the unused axis as depth. Normals always remain real normals.
bool bts_fill_select(int axis,int tap,out float weight){
 bts_uv=bts_real_uv;bts_position=bts_real_position;bts_object=bts_real_object;bts_generated=bts_real_generated;bts_normal=bts_real_normal;bts_object_normal=bts_real_object_normal;weight=1;
 if(!bts_fill_enabled()||(bts_fill_projection_mode==0&&native_projection[0].z!=0))return true;
 if(bts_fill_projection_mode==0||bts_fill_projection_mode==6){
  if(bts_fill_projection_mode==6)return bts_fill_domain_covered(bts_real_uv.xy-native_tile_offset);
  vec2 uv=bts_fill_map(bts_real_uv.xy);
  if(((bts_fill_domains&1)!=0||bts_fill_domains==0)&&!bts_fill_domain_covered(uv-native_tile_offset))return false;bts_uv.xy=uv;
  if((bts_fill_domains&2)!=0){vec2 q=bts_fill_map(bts_position.xy);if(!bts_fill_domain_covered(q))return false;bts_position.xy=q;}
  if((bts_fill_domains&4)!=0){vec2 q=bts_fill_map(bts_object.xy);if(!bts_fill_domain_covered(q))return false;bts_object.xy=q;}
  if((bts_fill_domains&8)!=0){vec2 q=bts_fill_map(bts_generated.xy);if(!bts_fill_domain_covered(q))return false;bts_generated.xy=q;}return true;
 }
 int count=bts_fill_source_filter==0?4:1;vec2 delta=count==1?vec2(0):vec2((tap&1)!=0?.25:-.25,(tap&2)!=0?.25:-.25);vec2 uv;float depth=bts_fill_depth;
 if(bts_fill_projection_mode==1){int x=(axis+1)%3,y=(axis+2)%3;uv=vec2(bts_fill_point[x]+.5,bts_fill_point[y]+.5)+delta*bts_real_footprint*vec2(bts_fill_inverse_denominator[x],bts_fill_inverse_denominator[y]);depth=bts_fill_point[axis];weight=bts_fill_weights[axis]/count;}
 else{uv=bts_fill_center+delta*bts_fill_spread;weight=bts_fill_coverage/count;}
 uv=bts_fill_map(uv);if(!bts_fill_wrap(uv))return false;
 bts_uv=vec3(uv,0);bts_position=vec3(uv,depth);bts_object=bts_position;bts_generated=bts_position;return weight>0;
}
)GLSL";
  return result;
}
}  // namespace bts
