#pragma once
#include "GpuPathMath.hh"
#include <string>
namespace bts {
inline std::string gpu_ribbon_shader()
{
  return std::string(R"GLSL(
vec4 geometry(int i){ivec2 s=textureSize(geometry_img,0);return texelFetch(geometry_img,ivec2(i%s.x,i/s.x),0);}
int triangle_index(int i){ivec2 s=textureSize(indices_img,0);return int(texelFetch(indices_img,ivec2(i%s.x,i/s.x),0).x);}
)GLSL") + gpu_path_math +
         R"GLSL(
vec2 number(int base,int row,int component){return vec2(geometry(base+row*2)[component],geometry(base+row*2+1)[component]);}
vec2 dot_row(vec2 x,vec2 y,vec2 z,int base,int row){return ds_dot(x,y,z,geometry(base+row*2).xyz,geometry(base+row*2+1).xyz);}
vec2 ds_abs(vec2 value){return ds_less(value,vec2(0))?-value:value;}
vec2 ds_clamp01(vec2 value){return ds_less(value,vec2(0))?vec2(0):ds_less(vec2(1,0),value)?vec2(1,0):value;}
vec2 ds_mix(vec2 a,vec2 b,vec2 t){return ds_add(a,ds_mul(ds_sub(b,a),t));}
int ds_floor(vec2 value){float v=floor(value.x);if(ds_less(value,vec2(v,0)))v-=1;return int(v);}
bool ribbon_coordinate(int piece,vec2 px,vec2 py,vec2 pz,out vec2 arc,out vec2 across,out vec2 flow){
 vec4 a=geometry(piece),a_lo=geometry(piece+1),b=geometry(piece+2),b_lo=geometry(piece+3);
 vec2 dx=ds_sub(px,vec2(a.x,a_lo.x)),dy=ds_sub(py,vec2(a.y,a_lo.y)),dz=ds_sub(pz,vec2(a.z,a_lo.z));
 vec2 length=vec2(a.w,a_lo.w),along=dot_row(dx,dy,dz,piece,2),t=ds_clamp01(ds_div(along,length));
 vec2 radius=ds_mul(ds_mix(number(piece,2,3),number(piece,3,3),t),vec2(.5,0));
 if(!ds_less(vec2(1e-12,0),radius))return false;
 vec2 side=dot_row(dx,dy,dz,piece,3),depth=dot_row(dx,dy,dz,piece,4);
 if(ds_less(radius,ds_abs(side))||ds_less(radius,ds_abs(depth)))return false;
 int corner=configuration&3;bool first=geometry(piece+12).w!=0,last=geometry(piece+14).w!=0;
 if(corner==3){if(ds_less(along,vec2(0))||ds_less(length,along))return false;}
 else if(corner==1){
  if((first&&ds_less(along,vec2(0)))||(last&&ds_less(length,along)))return false;
  vec2 outside=ds_less(along,vec2(0))?along:ds_less(length,along)?ds_sub(along,length):vec2(0);
  if(ds_less(ds_mul(radius,radius),ds_add(ds_mul(outside,outside),ds_mul(side,side))))return false;
 }else{
  vec2 bx=ds_sub(px,vec2(b.x,b_lo.x)),by=ds_sub(py,vec2(b.y,b_lo.y)),bz=ds_sub(pz,vec2(b.z,b_lo.z));
  vec2 margin=ds_mul(radius,vec2(4,0));
  if((first&&ds_less(along,vec2(0)))||(last&&ds_less(length,along))||ds_less(along,-margin)||ds_less(ds_add(length,margin),along)||ds_less(dot_row(dx,dy,dz,piece,5),vec2(-1e-9,0))||ds_less(vec2(1e-9,0),dot_row(bx,by,bz,piece,6)))return false;
  if(corner==2){
   for(int end=0;end<2;++end){if(end==0?first:last)continue;
    int row=end==0?7:8;vec4 sh=geometry(piece+6),sl=geometry(piece+7),eh=geometry(piece+row*2),el=geometry(piece+row*2+1);
    vec2 sx=ds_add(vec2(sh.x,sl.x),vec2(eh.x,el.x)),sy=ds_add(vec2(sh.y,sl.y),vec2(eh.y,el.y)),sz=ds_add(vec2(sh.z,sl.z),vec2(eh.z,el.z));
    vec2 edge=ds_add(ds_add(ds_mul(end==0?dx:bx,sx),ds_mul(end==0?dy:by,sy)),ds_mul(end==0?dz:bz,sz));
    vec2 cosine=ds_dot(vec2(sh.x,sl.x),vec2(sh.y,sl.y),vec2(sh.z,sl.z),eh.xyz,el.xyz);
    if(ds_less(ds_add(ds_mul(radius,ds_add(vec2(1,0),cosine)),vec2(1e-9,0)),ds_abs(edge)))return false;
   }
  }
 }
 // Use the same stored double endpoint as the CPU, avoiding a second
 // rounding at clamped ends of a mitered or closed curve.
 arc=all(equal(t,vec2(0)))?vec2(b.w,b_lo.w):all(equal(t,vec2(1,0)))?number(piece,8,3):ds_add(vec2(b.w,b_lo.w),ds_mul(t,length));across=ds_div(side,radius);
 flow=ds_mix(number(piece,4,3),number(piece,5,3),ds_clamp01(ds_div(ds_sub(arc,vec2(b.w,b_lo.w)),length)));
 return true;
}
bool image_coordinate(int field,vec2 arc,vec2 across,out vec2 image_u,out vec2 image_v){
 vec2 period=number(field,0,0),start=number(field,0,2),total=number(field,0,3);
 int count=int(geometry(field+2).x);bool preserve=geometry(field+2).y!=0;
 if(!ds_less(vec2(0),period)||ds_less(vec2(1,0),ds_abs(across))||ds_less(arc,vec2(0))||ds_less(total,arc))return false;
 vec2 u=all(equal(ds_sub(arc,total),vec2(0)))?number(field,1,3):ds_div(ds_sub(arc,start),period);int tile=ds_floor(u);
 if(all(equal(ds_sub(u,vec2(float(count),0)),vec2(0)))){tile=count-1;u=vec2(float(count),0);}
 if(tile<0||tile>=count)return false;
 if((configuration&(1<<7))!=0&&(configuration&(1<<9))!=0&&(configuration&(1<<8))==0){
  if(ds_less(ds_add(start,ds_mul(vec2(float(tile),0),period)),vec2(-1e-8,0))||ds_less(ds_add(total,vec2(1e-8,0)),ds_add(start,ds_mul(vec2(float(tile+1),0),period))))return false;
 }
 u=ds_sub(u,vec2(float(tile),0));
 if(preserve){vec2 natural=number(field,1,2),a=ds_mul(offsets.xy,natural),b=ds_mul(offsets.zw,natural);
  if(ds_less(ds_add(a,b),period)){vec2 x=ds_mul(u,period);
   if(ds_less(x,a)&&ds_less(vec2(0),a))u=ds_div(x,natural);
   else if(ds_less(ds_sub(period,b),x)&&ds_less(vec2(0),b))u=ds_sub(vec2(1,0),ds_div(ds_sub(period,x),natural));
   else u=ds_add(offsets.xy,ds_mul(ds_div(ds_sub(x,a),ds_sub(ds_sub(period,a),b)),ds_sub(ds_sub(vec2(1,0),offsets.xy),offsets.zw)));
  }
 }
 if((configuration&(1<<10))!=0&&tile%2!=0)u=ds_sub(vec2(1,0),u);
 vec2 v=ds_mul(ds_add(across,vec2(1,0)),vec2(.5,0));if((configuration&(1<<11))!=0)v=ds_sub(vec2(1,0),v);
 for(int i=0;i<((configuration>>5)&3);++i){vec2 old=u;u=v;v=ds_sub(vec2(1,0),old);}
 image_u=u;image_v=v;return true;
}
vec4 source_texel(ivec2 p){vec4 value=texelFetch(source_img,clamp(p,ivec2(0),textureSize(source_img,0)-1),0);return vec4(value.rgb*value.a,value.a);}
vec4 ribbon_image(vec2 u,vec2 v){
 ivec2 size=textureSize(source_img,0);vec2 x=ds_sub(ds_mul(ds_clamp01(u),vec2(float(size.x),0)),vec2(.5,0)),y=ds_sub(ds_mul(ds_clamp01(v),vec2(float(size.y),0)),vec2(.5,0));
 ivec2 at=ivec2(ds_floor(x),ds_floor(y));vec2 fx=ds_sub(x,vec2(float(at.x),0)),fy=ds_sub(y,vec2(float(at.y),0));
 vec4 color=mix(mix(source_texel(at),source_texel(at+ivec2(1,0)),fx.x+fx.y),mix(source_texel(at+ivec2(0,1)),source_texel(at+ivec2(1)),fx.x+fx.y),fy.x+fy.y);
 if(color.a>1e-8)color.rgb/=color.a;return color;
}
float coverage(ivec2 p){
 if(mask_mode==0)return 1;
 ivec2 size=textureSize(coverage_img,0),output_size=ivec2(paint_size);
 bvec2 area=bvec2(size.x>=output_size.x&&size.x%output_size.x==0,size.y>=output_size.y&&size.y%output_size.y==0);
 vec2 location=(vec2(p)+.5)*vec2(size)/vec2(output_size)-.5,fraction=fract(location);ivec2 first=ivec2(floor(location)),count=ivec2(2);
 if(area.x){count.x=size.x/output_size.x;first.x=p.x*count.x;}if(area.y){count.y=size.y/output_size.y;first.y=p.y*count.y;}
 precise vec4 sum=vec4(0),correction=vec4(0);
 for(int y=0;y<count.y;y++)for(int x=0;x<count.x;x++){float weight=(area.x?1.0/count.x:(x==0?1-fraction.x:fraction.x))*(area.y?1.0/count.y:(y==0?1-fraction.y:fraction.y));precise vec4 term=texelFetch(coverage_img,clamp(first+ivec2(x,y),ivec2(0),size-1),0)*weight-correction;precise vec4 next=sum+term;correction=(next-sum)-term;sum=next;}
 float allowed=clamp(sum.r+1-sum.a,0.0,1.0);return mask_mode==2?1-allowed:allowed;
}

bool closer(vec2 next,vec2 current){return ds_less(ds_add(next,vec2(1e-12,0)),current);}
bool candidate_sample(int index,vec2 px,vec2 py,vec2 pz,out vec4 color,out vec2 distance){
 ivec4 entry=ivec4(geometry(index));vec2 arc,across,flow,u,v;
 if(!ribbon_coordinate(entry.y,px,py,pz,arc,across,flow)||!image_coordinate(entry.x,arc,across,u,v))return false;
 color=ribbon_image(u,v);vec2 alpha=ds_mul(vec2(color.a,0),flow);color.a=alpha.x+alpha.y;
 distance=ds_abs(across);return color.a>1e-8;
}
void composite_patch(inout vec4 selected,inout vec2 distance,inout bool found,vec4 color,vec2 next_distance){
 int policy=(configuration>>2)&3;
 if(found){
  if(policy==1&&color.a<selected.a)return;
  if(policy==0){float combined=color.a+selected.a*(1-color.a);color.rgb=(color.rgb*color.a+selected.rgb*selected.a*(1-color.a))/combined;color.a=combined;}
  if(policy==2){float combined=min(1.0,color.a+selected.a);color.rgb=(color.rgb*color.a+selected.rgb*selected.a)/(color.a+selected.a);color.a=combined;}
 }selected=color;distance=next_distance;found=true;
}

bool connected(int previous,int next,bool wrap){
 ivec4 a=ivec4(geometry(previous)),b=ivec4(geometry(next));
 vec2 gap=ds_sub(number(b.y,1,3),number(a.y,8,3));if(wrap)gap=ds_add(gap,number(a.x,0,3));
 vec2 radius=ds_mul(ds_add(number(a.y,3,3),number(b.y,2,3)),vec2(.5,0));
 return !ds_less(ds_add(radius,vec2(1e-12,0)),gap);
}
bool sample_patches(ivec2 candidates,vec2 px,vec2 py,vec2 pz,out vec4 selected,out vec2 distance){
 selected=vec4(0);distance=vec2(1e30,0);bool found=false;
 for(int begin=candidates.x;begin<candidates.x+candidates.y;){
  ivec4 head=ivec4(geometry(begin));int end=begin+1;
  while(end<candidates.x+candidates.y&&int(geometry(end).x)==head.x)++end;
  bool merge=false;int tail_entry=end,first_entry=-1,first_last=-1,tail_last=-1;
  vec4 seam_color=vec4(0),tail_color=vec4(0);vec2 seam_distance=vec2(1e30,0),tail_distance=vec2(1e30,0);
  bool has_patch=false;int first=-1,last=-1;vec4 patch_color=vec4(0);vec2 patch_distance=vec2(1e30,0);
  // One sampling call site avoids duplicating the expensive geometric/image
  // kernel four times in GPU compiler inlining. The uniform phase count is
  // one for open ribbons and four for the closed-seam preparation and pass.
  for(int phase=0;phase<patch_phases;++phase){
   int mode=patch_phases==1?3:phase;if(mode==2&&!merge)continue;
   int start=mode==1?end-1:mode==2?tail_entry:begin;
   int stop=mode==1?begin-1:end,step=mode==1?-1 : 1;
   for(int k=start;k!=stop;k+=step){
    if(mode==3&&merge&&k>=tail_entry)continue;
    vec4 color;vec2 d;if(!candidate_sample(k,px,py,pz,color,d))continue;
    if(mode==0){
     if(first_last>=0&&!connected(first_last,k,false))break;
     if(first_entry<0)first_entry=k;first_last=k;if(closer(d,seam_distance)){seam_color=color;seam_distance=d;}
    }else if(mode==1){
     if(tail_entry<end&&!connected(k,tail_entry,false))break;
     tail_entry=k;if(tail_last<0)tail_last=k;
    }else if(mode==2){
     if(closer(d,tail_distance)){tail_color=color;tail_distance=d;}
    }else{
     if(!has_patch||!connected(last,k,false)){
      if(has_patch)composite_patch(selected,distance,found,merge&&first==first_entry?seam_color:patch_color,merge&&first==first_entry?seam_distance:patch_distance);
      has_patch=true;first=k;patch_color=color;patch_distance=d;
     }else if(closer(d,patch_distance)){patch_color=color;patch_distance=d;}
     last=k;
    }
   }
   if(mode==1)merge=first_last>=0&&tail_last>=0&&first_last<tail_entry&&connected(tail_last,first_entry,true);
   if(mode==2&&closer(tail_distance,seam_distance)){seam_color=tail_color;seam_distance=tail_distance;}
   if(mode==3&&has_patch)composite_patch(selected,distance,found,merge&&first==first_entry?seam_color:patch_color,merge&&first==first_entry?seam_distance:patch_distance);
  }
  begin=end;
 }return found;
}

void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy),at=p+page_origin;vec4 result=vec4(0);vec2 nearest=vec2(1e30,0);
 if(all(lessThan(at,ivec2(paint_size)))){
 vec2 uvx=ds_add(ds_div(vec2(float(at.x)+.5,0),vec2(float(paint_size),0)),vec2(float(tile_offset.x),0));
 vec2 uvy=ds_add(ds_div(vec2(float(at.y)+.5,0),vec2(float(paint_size),0)),vec2(float(tile_offset.y),0));float gate=coverage(at);
 for(int i=0;i<triangle_range.y&&gate>0;++i){int tri=triangle_index(triangle_range.x+i)*10;
  vec4 a=geometry(tri),b=geometry(tri+1),c=geometry(tri+2);
  vec2 dx=ds_sub(uvx,a.xz),dy=ds_sub(uvy,a.yw),ux=ds_sub(b.xz,a.xz),uy=ds_sub(b.yw,a.yw),vx=ds_sub(c.xz,a.xz),vy=ds_sub(c.yw,a.yw);
  vec2 determinant=ds_sub(ds_mul(ux,vy),ds_mul(uy,vx));
  vec2 w1=ds_div(ds_sub(ds_mul(dx,vy),ds_mul(dy,vx)),determinant),w2=ds_div(ds_sub(ds_mul(ux,dy),ds_mul(uy,dx)),determinant),w0=ds_sub(ds_sub(vec2(1,0),w1),w2);
  if(ds_less(w0,vec2(-1e-9,0))||ds_less(w1,vec2(-1e-9,0))||ds_less(w2,vec2(-1e-9,0)))continue;
  vec2 px=ds_add(ds_add(ds_mul(number(tri+3,0,0),w0),ds_mul(number(tri+3,1,0),w1)),ds_mul(number(tri+3,2,0),w2));
  vec2 py=ds_add(ds_add(ds_mul(number(tri+3,0,1),w0),ds_mul(number(tri+3,1,1),w1)),ds_mul(number(tri+3,2,1),w2));
  vec2 pz=ds_add(ds_add(ds_mul(number(tri+3,0,2),w0),ds_mul(number(tri+3,1,2),w1)),ds_mul(number(tri+3,2,2),w2));
  ivec2 candidates=ivec2(geometry(tri+9).xy);vec4 selected;vec2 distance;
  bool found=sample_patches(candidates,px,py,pz,selected,distance);
  selected.a*=gate;
  if(found&&selected.a>1e-8&&ds_less(distance,nearest)){nearest=distance;result=vec4((color_channel!=0?selected.rgb:paint_value)*selected.a,selected.a);}
 }
 }imageStore(result_img,p,result);
}
)GLSL";
}
}  // namespace bts
