#pragma once
namespace bts::gpu_projection {
// The CPU projection contracts are PlanarProjection/SpatialProjection/FillProjection.
// Resource pixels remain shared; transforms and coverage are evaluated at the dirty texel.
inline const char *source = R"GLSL(
int fold_projection(inout vec3 position,inout vec3 normal){
 if(projection_symmetry==0)return 0;int axis=int(symmetry_options.x);
 if(symmetry_options.y==0){float side=projection_offset[axis]>=symmetry_options.w?1:-1;if((position[axis]-symmetry_options.w)*side<0){position[axis]=2*symmetry_options.w-position[axis];normal[axis]=-normal[axis];return 1;}return 0;}
 int i=(axis+1)%3,j=(axis+2)%3;float x=position[i]-symmetry_center[i],y=position[j]-symmetry_center[j];vec2 origin=vec2(projection_offset[i]-symmetry_center[i],projection_offset[j]-symmetry_center[j]);
 float reference=length(origin)<1e-12?0:atan(origin.y,origin.x),sector=6.283185307179586/symmetry_options.z;
 float angle_to=atan(y,x)-reference;float relative=angle_to-roundEven(angle_to/6.283185307179586)*6.283185307179586;
 int copy=int(floor(relative/sector+.5));float angle=-copy*sector,c=cos(angle),sn=sin(angle);position[i]=symmetry_center[i]+c*x-sn*y;position[j]=symmetry_center[j]+sn*x+c*y;float ni=normal[i],nj=normal[j];normal[i]=c*ni-sn*nj;normal[j]=sn*ni+c*nj;
 // GLSL integer % is undefined for negative operands; C++ permits them.
 return int(mod(float(copy),symmetry_options.z));
}
vec2 projection_map(vec3 point,int copy){vec2 uv=vec2(dot(map_u,point),dot(map_v,point));if((copy&1)!=0){if(symmetry_flip.x!=0)uv.x=1-uv.x;if(symmetry_flip.y!=0)uv.y=1-uv.y;}return uv;}
float atom_value_options(vec2 uv,vec4 options){
 vec2 p=(fract(uv)-.5)*2;float distance=1e9;
 for(int i=0;i<3;i++){float angle=float(i)*3.141592653589793/3,c=cos(angle),s=sin(angle);vec2 q=vec2(c*p.x-s*p.y,s*p.x+c*p.y);distance=min(distance,abs(length(q/vec2(.82,.31))-1)*.31);}
 float value=distance<(.008+.035*options.x)?1:0;
 if(options.y!=0&&length(p)<.09)value=1;
 if(options.z!=0)for(int i=0;i<3;i++){float angle=float(i)*6.283185307179586/3;if(length(p-.82*vec2(cos(angle),sin(angle)))<.07)value=1;}
 return options.w!=0?1-value:value;
}
float atom_value(vec2 uv){return atom_value_options(uv,atom_options);}
vec4 fill_resource_sample(vec2 uv){if(fill_resource_kind==2)return vec4(vec3(atom_value(uv)),1);if(fill_resource_kind==0)return vec4(1);return resource_sample(uv);}
vec4 spatial_resource(vec2 uv){
 if(projection_wrap==4)uv=clamp(uv,vec2(0),vec2(1));
 bool repeat_u=projection_wrap==0||projection_wrap==2,repeat_v=projection_wrap==0||projection_wrap==3;
 if((!repeat_u&&(uv.x<0||uv.x>1))||(!repeat_v&&(uv.y<0||uv.y>1)))return vec4(0);
 if(fill_resource_kind!=1)return fill_resource_sample(uv);
 vec2 size=vec2(textureSize(source_img,0));uv.x=repeat_u?fract(uv.x):clamp(uv.x,.5/size.x,1-.5/size.x);uv.y=repeat_v?fract(uv.y):clamp(uv.y,.5/size.y,1-.5/size.y);
 return source_filter==2?resource_texel(ivec2(floor(uv*size))):resource_sample(uv);
}
float facing_coverage(float facing){
 if(backface_options.z<1e-6)return facing>=1-1e-8?1:0;
 return clamp((facing-backface_options.x)/backface_options.y,0.0,1.0);
}
vec4 spatial_sample(vec2 uv){
 ivec2 at=clamp(ivec2(uv*vec2(surface_size)),ivec2(0),surface_size-1);int first=(at.y*surface_size.x+at.x)*surface_stride;
 if((surface_word(first+7)&255u)==0u)return vec4(0);
 vec3 position=surface_vec3(first),normal=surface_vec3(first+3);int copy=fold_projection(position,normal);
 vec3 p=inverse_rotation(position-projection_offset)*inverse_denominator;
 vec3 n=inverse_rotation(normal)/inverse_denominator;if(dot(n,n)>1e-20)n=normalize(n);
 float footprint=uintBitsToFloat(surface_word(first+6)),coverage=1;vec2 center,spread;
 if(projection_mode==2){
  float depth=abs(p.y);if(projection_flags.x!=0&&(abs(p.x)>.5||abs(p.z)>.5||depth>1))return vec4(0);
  if(projection_flags.y!=0)coverage*=depth_hardness>=1?(depth<=1?1:0):clamp((1-depth)/(1-depth_hardness),0.0,1.0);
  if(projection_flags.z!=0)coverage*=facing_coverage(-n.y);
  center=p.xz+.5;spread=footprint*inverse_denominator.xz;
 }else if(projection_mode==3){
  float radius=length(p);if(projection_flags.x!=0&&radius>.5)return vec4(0);
  center=radius<1e-12?vec2(.5):vec2(.5+atan(p.x,-p.y)/6.283185307179586,.5+asin(clamp(p.z/radius,-1.0,1.0))/3.141592653589793);
  float angular=footprint/max(1e-8,abs(1/inverse_denominator.x)*max(radius,.01));spread=angular/vec2(6.283185307179586,3.141592653589793);
 }else if(projection_mode==5){
  float depth;vec3 patch_normal,du,dv;if(!warp_project(p,center,depth,patch_normal,du,dv))return vec4(0);
  float reach=warp_info.w!=0?abs(depth):-depth;if(reach<-.000001||reach>warp_info.z)return vec4(0);
  if(projection_flags.x!=0&&(any(lessThan(center,vec2(0)))||any(greaterThan(center,vec2(1)))))return vec4(0);
  if(projection_flags.y!=0&&depth_hardness<1)coverage*=clamp((warp_info.z-reach)/(warp_info.z*(1-depth_hardness)),0.0,1.0);
  if(projection_flags.z!=0){vec3 source_normal=n*inverse_denominator;if(dot(source_normal,source_normal)>1e-24)source_normal=normalize(source_normal);else source_normal=vec3(0);coverage*=facing_coverage(dot(source_normal,patch_normal*sign(inverse_denominator)));}
  if(fill_resource_kind!=0){du/=inverse_denominator;dv/=inverse_denominator;float a=dot(du,du),b=dot(du,dv),c=dot(dv,dv),det=a*c-b*b;if(det<1e-18)return vec4(0);spread=footprint*sqrt(vec2(c,a)/det);}else spread=vec2(0);
 }else{
  float radius=length(p.xy),angle=cylinder_radians;center=vec2(.5+(radius<1e-12?0:atan(p.x,-p.y))/angle,p.z+.5);
  if(projection_flags.x!=0&&(radius>.5||abs(p.z)>.5||center.x<0||center.x>1))return vec4(0);
  if(projection_flags.z!=0){if(radius<1e-12)return vec4(0);coverage*=facing_coverage(dot(n.xy,p.xy)/radius);}
  spread=vec2(footprint/(max(1e-8,min(abs(1/inverse_denominator.x),abs(1/inverse_denominator.y)))*max(.01,radius)*angle),footprint/max(1e-8,abs(1/inverse_denominator.z)));
 }
 if(coverage<=0)return vec4(0);if(fill_resource_kind==0)return vec4(vec3(coverage),coverage);
 int count=source_filter==0?4:1;vec4 result=vec4(0);
 for(int i=0;i<count;i++){vec2 delta=count==1?vec2(0):vec2((i&1)!=0?.25:-.25,(i&2)!=0?.25:-.25);vec3 point=vec3(center+delta*spread,1);result+=spatial_resource(projection_map(point,copy))/count;}
 return result*coverage;
}
)GLSL";
}  // namespace bts::gpu_projection
