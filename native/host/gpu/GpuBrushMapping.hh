#pragma once
#include "BrushStencil.hh"
#include <span>
#include <string>
namespace bts {
// Parameters only. Camera/lens/UV transforms and resource samples run per texel
// in the brush compute pass, including world painting with UV brush alignment.
inline void gpu_brush_mapping(std::span<std::array<float, 4>> rows,
                              const StencilMapping &mapping,
                              const BrushStencil &transform,
                              std::array<double, 2> tile_offset,
                              int triangle_start,
                              int triangle_count)
{
  const auto *alpha = transform.resource ? &*transform.resource : nullptr;
  rows[0] = {
      1, float(mapping.world), alpha ? float(alpha->kind) : -1, float(alpha && alpha->inverted)};
  const auto bitmap = alpha ? alpha->bitmap : nullptr;
  const float aspect = bitmap && bitmap->height ? float(bitmap->width) / bitmap->height : 1;
  rows[1] = {alpha ? alpha->uniform : 1, float(transform.tiling), aspect, 0};
  const double angle = transform.angle * std::acos(-1.) / 180;
  rows[2] = {float(std::cos(angle)), float(std::sin(angle)), float(1 / transform.scale), 0};
  rows[3] = {float(transform.offset[0]),
             float(transform.offset[1]),
             float(mapping.uv_extent),
             float(mapping.viewport_aspect)};
  rows[4] = {float(mapping.uv_center[0]),
             float(mapping.uv_center[1]),
             float(tile_offset[0]),
             float(tile_offset[1])};
  for (int row = 0; row < 4; ++row)
    for (int c = 0; c < 4; ++c)
      rows[5 + row][c] = float(mapping.projection[row * 4 + c]);
  rows[9] = {float(mapping.lens.enabled),
             float(mapping.lens.distortion),
             float(mapping.lens.roundness),
             float(mapping.lens.smoothness)};
  if (alpha && alpha->kind == AlphaKind::DynamicOutline) {
    const auto &d = alpha->dynamic;
    const double phase = d.phase * 2 * std::acos(-1.), angle = std::sin(phase) * .35;
    rows[10] = {float(std::cos(angle)),
                float(std::sin(angle)),
                float(2 + 10 * std::clamp(double(d.roundness) + .5 * std::cos(phase), 0., 1.)),
                d.width};
  }
  rows[12] = {float(triangle_start), float(triangle_count), 0, 0};
}
inline const std::string gpu_brush_mapping_source = R"GLSL(
float mapping_lens_alpha(vec2 p,vec4 lens){
 if(lens.x==0)return 1;vec2 q=abs(p-.5)-(.5-lens.z);float distance_value=length(max(q,vec2(0)))+min(max(q.x,q.y),0.0)-lens.z;
 if(lens.w<=0)return distance_value<0?1:0;float t=clamp((distance_value+.005*lens.w)/(.005*lens.w),0.0,1.0);return 1-t*t*(3-2*t);
}
bool mapping_world_screen(vec3 point,int base,out vec2 screen){
 vec4 p=vec4(point,1),clip=vec4(dot(parameter(base+5),p),dot(parameter(base+6),p),dot(parameter(base+7),p),dot(parameter(base+8),p));if(clip.w<=1e-12)return false;
 vec2 displayed=(clip.xy/clip.w+1)*.5;vec4 lens=parameter(base+9);
 if(lens.x!=0){vec2 delta=displayed-.5;float radial=4*lens.y*dot(delta,delta),denominator=1+radial;if(denominator<=0||radial>1)return false;displayed=delta*(1+lens.y)/denominator+.5;}
 if(mapping_lens_alpha(displayed,lens)<=0)return false;screen=(2*displayed-1)*vec2(parameter(base+3).w,1);return true;
}
bool mapping_uv_screen(vec2 uv,int base,out vec2 screen){
 vec4 view=parameter(base+4);uv+=view.zw;if(parameter(base).y==0){screen=2*(uv-view.xy)/parameter(base+3).z;return true;}
 ivec2 entries=ivec2(parameter(base+12).xy);
 for(int i=0;i<entries.y;i++){int row=entries.x+i;vec2 a=brush_triangle(0,row).xy,b=brush_triangle(2,row).xy,c=brush_triangle(4,row).xy;
  vec2 x=b-a,y=c-a;float determinant=x.x*y.y-x.y*y.x;if(abs(determinant)<1e-16)continue;vec3 weights;weights.y=((uv.x-a.x)*y.y-(uv.y-a.y)*y.x)/determinant;weights.z=(x.x*(uv.y-a.y)-x.y*(uv.x-a.x))/determinant;weights.x=1-weights.y-weights.z;if(min(min(weights.x,weights.y),weights.z)<-1e-7)continue;
  vec3 point=weights.x*brush_triangle(1,row).xyz+weights.y*brush_triangle(3,row).xyz+weights.z*brush_triangle(5,row).xyz;return mapping_world_screen(point,base,screen);
 }return false;
}
bool mapping_coordinates(vec2 screen,int base,out vec2 uv){
 vec4 transform=parameter(base+2),options=parameter(base+1);vec2 d=screen-parameter(base+3).xy;uv=vec2(dot(d,transform.xy),d.y*transform.x-d.x*transform.y)*transform.z/vec2(options.z,1);int tiling=int(options.y);
 if((tiling&1)!=0)uv.x-=2*floor((uv.x+1)/2);if((tiling&2)!=0)uv.y-=2*floor((uv.y+1)/2);return all(lessThanEqual(abs(uv),vec2(1)));
}
float mapping_alpha(vec2 screen,int base){
 vec4 source=parameter(base);if(source.z<0)return 1;vec2 uv;if(!mapping_coordinates(screen,base,uv))return 0;float value=0;
 if(source.z==0){if(length(uv)>=1)return 0;value=1;}
 else if(source.z==1)value=abs(uv.x)<=.22||abs(uv.y)<=.22?1:0;
 else if(source.z==2)value=clamp(parameter(base+1).x,0.0,1.0);
 else if(source.z==3){ivec2 size=textureSize(stencil_img,0);vec2 p=(uv+1)*.5*vec2(size)-.5;ivec2 at=ivec2(floor(p));vec2 f=fract(p);float a=texelFetch(stencil_img,clamp(at,ivec2(0),size-1),0).r,b=texelFetch(stencil_img,clamp(at+ivec2(1,0),ivec2(0),size-1),0).r,c=texelFetch(stencil_img,clamp(at+ivec2(0,1),ivec2(0),size-1),0).r,d=texelFetch(stencil_img,clamp(at+ivec2(1),ivec2(0),size-1),0).r;value=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);}
 else if(source.z==4){vec4 d=parameter(base+10);vec2 q=vec2(dot(uv,d.xy),uv.y*d.x-uv.x*d.y);float radius=pow(pow(abs(q.x),d.z)+pow(abs(q.y),d.z),1/d.z);float t=clamp((d.w*.5+.008-abs(radius-.75))/.016,0.0,1.0);value=t*t*(3-2*t);}
 return source.w!=0?1-value:value;
}
)GLSL";
}  // namespace bts
