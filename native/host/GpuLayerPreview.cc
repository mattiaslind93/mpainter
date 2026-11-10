// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetResidency.hh"
#include "LayerSourceChannels.hh"
#include "NativeMaterialPreparationQueue.hh"
#include "GPU_host_texture_memory.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "NativeMaterialHeightGpuPrivate.hh"
#include "GpuLayerPreview.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "SpatialOracleScope.hh"
#include "MaskEffectContext.hh"
#include "GpuProgramWarmup.hh"
#include "GpuProjectReadback.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "GpuStackPlan.hh"
#include "FolderEntryPlan.hh"
#include "AdjustmentPaintPlan.hh"
#include "BumpPrecision.hh"
#include "GpuAdjustmentPages.hh"
#include "../tests/FolderCompositionFixture.hh"
#include "../tests/SmartMaskProbe.hh"
#include "GpuLayerChannel.hh"
#include "GpuLayerEffects.hh"
#include "GpuMaskStages.hh"
#include "GpuGeneratorAnchors.hh"
#include "GpuMaskResolve.hh"
#include "GpuRangeSummary.hh"
#include "GpuHistogram.hh"
#include "GpuMaterialSample.hh"
#include "GpuThumbnail.hh"
#include "SmartMaskThumbnailPacket.hh"
#include "GpuLayerCapture.hh"
#include "GpuFlattenTest.hh"
#include "QuickMaskPreview.hh"
#include <thread>
#include <QJsonDocument>
#include "GpuSpatialParameters.hh"
#include "GpuDerivedShaders.hh"
#include "GpuProjectionSource.hh"
#include "GpuWarpSource.hh"
#include <epoxy/gl.h>
#include "GpuLayerParameters.hh"
#include "GpuGeometryMask.hh"
#include "GpuMarginPlan.hh"
#include "GpuMarginExecutor.hh"
#include "GpuDerivedDirty.hh"
#include "GpuBrushRasterTest.hh"
#include "GpuAuthoringWorkerTest.hh"
#include "GpuTextureProbe.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "AmbientOcclusion.hh"
#include "NativeCompositeJob.hh"
#include "GPU_compute.hh"
#include "GPU_context.hh"
#include "BLI_math_half.hh"
#include "MEM_guardedalloc.h"
#include "GPU_shader.hh"
#include "GPU_shader_hosted.hh"
#include "GPU_capabilities.hh"
#include "GPU_state.hh"
#include "gpu_shader_create_info.hh"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstring>
namespace bts {
using namespace blender;
namespace {
bool testing_stack = false, benchmark_stack = false;
// Transfer scheduling only, never a resource-size or texture-resolution limit.
constexpr std::uint64_t upload_budget = 32ULL * 1024 * 1024;
std::uint64_t source_cache_budget(std::uint64_t owned)
{
  return resource_sum(owned,
                      texture_vram_query_valid.load() ? texture_free_vram.load() :
                                                        texture_available_ram_bytes());
}

using Texture = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
using Shader = std::unique_ptr<gpu::Shader, decltype(&GPU_shader_free)>;
Texture texture(const char *name, int w, int h, gpu::TextureFormat format)
{
  return {GPU_texture_create_2d(name, w, h, 1, format, GPU_TEXTURE_USAGE_GENERAL, nullptr),
          GPU_texture_free};
}
bool active(const TextureLayer &layer, int channel, int tile = 1001)
{
  PaintMetadataScope metadata;
  const auto data = gpu_layer_channel(layer, channel);
  if (!layer.visible || !data.enabled || data.blend == LayerBlend::Disabled)
    return false;
  if (layer.geometry.type == 1 && !layer.geometry.includes_tile(tile))
    return false;
  if (layer.geometry.type == 0 && layer.geometry.meshes.empty() && !layer.geometry.default_include)
    return false;
  if (data.opacity <= 0 && data.blend != LayerBlend::Replace)
    return false;
  if (layer.kind == LayerKind::Paint && !(channel == 0 && layer.quick_overlay) &&
      data.pixels->tile(tile).page_count(data.plane) == 0 && layer.effects.empty() &&
      data.blend != LayerBlend::Replace)
    return false;
  return true;
}
std::string paint_key(
    const TextureSet &s, const TextureLayer &l, int c, bool mask, int tile = 1001)
{
  return s.id.toString().toStdString() + l.id.toString().toStdString() + ":" +
         std::to_string(tile) + ":" + (mask ? "mask" : std::to_string(c));
}
std::string resource_key(const FillTexture &description)
{
  return gpu_texture_resource_key(description);
}
const FillTexture *tile_resource(const FillProjection &fill, int channel, int tile)
{
  const auto &source = fill_channel_texture(fill, channel);
  if (!source)
    return nullptr;
  if (fill.mode == 6 && source->tiles) {
    const auto found = source->tiles->find(tile);
    return found == source->tiles->end() ? nullptr : found->second.get();
  }
  return source.get();
}
const char *quant_source = R"GLSL(
float half_round_even(float value){
 // Keep the narrowing operation explicit. Some GL compilers remove a paired
 // unpackHalf2x16(packHalf2x16(x)) when the destination is float32.
 uint bits=floatBitsToUint(value),sign=bits&0x80000000u,magnitude=bits&0x7fffffffu;
 if(magnitude<0x38800000u){float steps=roundEven(abs(value)*16777216.0);return uintBitsToFloat(floatBitsToUint(steps/16777216.0)|sign);}
 uint discarded=magnitude&0x1fffu,rounded=magnitude&0xffffe000u;
 if(discarded>0x1000u||(discarded==0x1000u&&(rounded&0x2000u)!=0u))rounded+=0x2000u;
 return uintBitsToFloat(sign|rounded);
}
vec3 quantize_value(vec3 v,int mode){
 if(mode==1||mode==3||mode==5||mode==7){precise float gray=(v.x*.2126+v.y*.7152)+v.z*.0722;v=vec3(gray);}
 if(mode==0){for(int k=0;k<3;k++){float a=clamp(v[k],0.0,1.0);float e=a<=.0031308?12.92*a:1.055*pow(a,1.0/2.4)-.055;float q=floor(e*255.0+.5)/255.0;v[k]=q<=.04045?q/12.92:pow((q+.055)/1.055,2.4);}}
 else if(mode==1||mode==2)v=floor(clamp(v,vec3(0),vec3(1))*255.0+vec3(.5))/255.0;
 else if(mode==3||mode==4)v=floor(clamp(v,vec3(0),vec3(1))*65535.0+vec3(.5))/65535.0;
 else if(mode==5||mode==6){v=clamp(v,vec3(-65504),vec3(65504));for(int k=0;k<3;k++)v[k]=half_round_even(v[k]);}
 return v;
}
)GLSL";
const char *layer_source = R"GLSL(
vec4 anchor_value(vec4 raw){
 if(anchor_options.w==0)return vec4(0,0,0,1);
 if(anchor_options.z!=0)return vec4(vec3(clamp(raw.r,0.0,1.0)),1);
 int component=int(anchor_options.x);vec3 rgb=component==0?raw.rgb:vec3(raw[clamp(component-1,0,3)]);return vec4(rgb,anchor_options.y!=0&&component!=4?clamp(raw.a,0.0,1.0):1);
}
float safe_div(float a,float b){return b==0.0?(a>0.0?1.0:0.0):clamp(a/b,-65504.0,65504.0);}
float blend_scalar(float s,float b,int mode){
 if(mode==2)return s*b;if(mode==3)return 1-(1-s)*(1-b);
 if(mode==5)return safe_div(b,s);if(mode==6)return safe_div(s,b);
 if(mode==7)return min(s,b);if(mode==8)return max(s,b);if(mode==9)return b+s;
 if(mode==10)return b-s;if(mode==11)return s-b;if(mode==12)return abs(b-s);
 if(mode==13)return b+s-2*b*s;if(mode==14)return b+s-.5;
 if(mode==15)return b<=.5?2*b*s:1-2*(1-b)*(1-s);
 if(mode==16){if(s<=.5)return b-(1-2*s)*b*(1-b);float d=b<=.25?((16*b-12)*b+4)*b:sqrt(b);return b+(2*s-1)*(d-b);}
 if(mode==17)return s<=.5?2*b*s:1-2*(1-b)*(1-s);
 if(mode==18){if(s<=.5)return b>=1?1:s<=0?0:1-min(1.0,safe_div(1-b,2*s));return b<=0?0:s>=1?1:min(1.0,safe_div(b,2*(1-s)));}
 if(mode>=21&&mode<=23)return b;if(mode==19)return b+2*s-1;if(mode==20)return s<=.5?min(b,2*s):max(b,2*s-1);return s;
}
vec3 normal_unit(vec3 value){float len=length(value);return len<1e-12?vec3(0,0,1):value/len;}
vec3 normal_detail(vec3 base,vec3 detail){base=normal_unit(base);detail=normal_unit(detail);if(base.z<-1+1e-6)return normal_unit(vec3(detail.x,-detail.y,-detail.z));vec3 t=base+vec3(0,0,1),u=vec3(-detail.xy,detail.z);return normal_unit(t*dot(t,u)-u*t.z);}
vec3 rgb_hsv(vec3 rgb){rgb=max(rgb,vec3(0));float high=max(max(rgb.r,rgb.g),rgb.b),low=min(min(rgb.r,rgb.g),rgb.b),delta=high-low,hue=0;if(delta>1e-8){if(high==rgb.r)hue=(rgb.g-rgb.b)/delta;else if(high==rgb.g)hue=(rgb.b-rgb.r)/delta+2;else hue=(rgb.r-rgb.g)/delta+4;hue=fract(hue/6);}return vec3(hue,high>1e-8?delta/high:0,high);}
vec3 hsv_rgb(vec3 hsv){float hue=fract(hsv.x)*6,s=hsv.y,v=hsv.z;int sector=int(hue);float f=hue-sector,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));switch(sector%6){case 0:return vec3(v,t,p);case 1:return vec3(q,v,p);case 2:return vec3(p,v,t);case 3:return vec3(p,q,v);case 4:return vec3(t,p,v);default:return vec3(v,p,q);}}
vec3 blend_rgb(vec3 source,vec3 backdrop,int mode){
 if(mode>=25&&mode<=27){vec3 a=normal_unit(source*2-1),b=normal_unit(backdrop*2-1),value;if(mode==25)value=normal_unit(vec3(b.xy+a.xy,b.z*a.z));else if(mode==26)value=normal_detail(b,a);else value=normal_detail(a,b);return value*.5+.5;}
 if(mode>=21&&mode<=24){vec3 top=rgb_hsv(source),result=rgb_hsv(backdrop);if(mode==21){if(top.y<=1e-8||top.z<=1e-8)return backdrop;result.x=top.x;}else if(mode==22)result.y=top.y;else if(mode==23)result.xy=top.xy;else result.z=top.z;return hsv_rgb(result);}
 return vec3(blend_scalar(source.x,backdrop.x,mode),blend_scalar(source.y,backdrop.y,mode),blend_scalar(source.z,backdrop.z,mode));
}
// The shared source stays straight RGBA, including RGB under zero alpha.
// Premultiply only for this usage's interpolation, never into another CPU/GPU copy.
// Bilinear neighbors are in [-1,size]. Shift before integer modulo: GLSL
// does not define remainder for a negative operand (not C++ remainder).
vec4 resource_texel(ivec2 p){ivec2 n=textureSize(source_img,0);p=source_wrap==0?(p+n)%n:clamp(p,ivec2(0),n-1);vec4 v=texelFetch(source_img,p,0);if(source_ignore_alpha!=0)v.a=1;v.rgb*=v.a;return v;}
vec4 resource_sample(vec2 uv){vec2 p=(source_wrap==0?fract(uv):clamp(uv,vec2(0),vec2(1)))*vec2(textureSize(source_img,0))-.5;ivec2 i=ivec2(floor(p));vec2 f=fract(p);return mix(mix(resource_texel(i),resource_texel(i+ivec2(1,0)),f.x),mix(resource_texel(i+ivec2(0,1)),resource_texel(i+ivec2(1)),f.x),f.y);}
vec4 paint_sample(sampler2D image,ivec2 p,ivec2 output_size){
 ivec2 size=textureSize(image,0);if(all(equal(size,output_size)))return texelFetch(image,p,0);
 bvec2 area=bvec2(size.x>=output_size.x&&size.x%output_size.x==0,size.y>=output_size.y&&size.y%output_size.y==0);
 vec2 location=(vec2(p)+.5)*vec2(size)/vec2(output_size)-.5,fraction=fract(location);ivec2 first=ivec2(floor(location)),count=ivec2(2);
 if(area.x){count.x=size.x/output_size.x;first.x=p.x*count.x;}if(area.y){count.y=size.y/output_size.y;first.y=p.y*count.y;}
 precise vec4 sum=vec4(0),correction=vec4(0);
 for(int y=0;y<count.y;y++)for(int x=0;x<count.x;x++){
  float weight=(area.x?1.0/count.x:(x==0?1-fraction.x:fraction.x))*(area.y?1.0/count.y:(y==0?1-fraction.y:fraction.y));
  precise vec4 term=texelFetch(image,clamp(first+ivec2(x,y),ivec2(0),size-1),0)*weight-correction;precise vec4 next=sum+term;correction=(next-sum)-term;sum=next;
 }return sum;
}
vec4 generator_map_sample(sampler2D image,vec2 uv,bool linear_filter);
vec4 generator_input_sample(sampler2D image,vec2 uv,bool filtering,bool anchored,vec4 options){
 if(!anchored)return generator_map_sample(image,uv,filtering);
 if(options.w==0)return vec4(0);
 vec4 raw=texelFetch(image,min(ivec2(uv*vec2(imageSize(result_img))),textureSize(image,0)-1),0);
 if(options.z!=0)return vec4(vec3(clamp(raw.r,0.0,1.0)),1);
 int component=int(options.x);vec3 rgb=component==0?raw.rgb:vec3(raw[clamp(component-1,0,3)]);float alpha=options.y!=0&&component!=4?clamp(raw.a,0.0,1.0):1;return vec4(rgb*alpha,alpha);
}
vec4 quick_overlay_texel(ivec2 p){
 vec4 mask=paint_sample(source_img,p,ivec2(256));float allowed=clamp(mask.r+1-mask.a,0.0,1.0);if(quick_inverted!=0)allowed=1-allowed;
 float checker=quick_editing!=0?.14:0,gray=((p.x/8+p.y/8)%2)!=0?.3:.12;
 return vec4(vec3(gray*checker*allowed),1-allowed+checker*allowed);
}
vec4 quick_overlay_sample(ivec2 p,ivec2 output_size){
 ivec2 size=ivec2(256);bvec2 area=bvec2(size.x>=output_size.x&&size.x%output_size.x==0,size.y>=output_size.y&&size.y%output_size.y==0);
 vec2 location=(vec2(p)+.5)*vec2(size)/vec2(output_size)-.5,fraction=fract(location);ivec2 first=ivec2(floor(location)),count=ivec2(2);
 if(area.x){count.x=size.x/output_size.x;first.x=p.x*count.x;}if(area.y){count.y=size.y/output_size.y;first.y=p.y*count.y;}
 precise vec4 sum=vec4(0),correction=vec4(0);
 for(int y=0;y<count.y;y++)for(int x=0;x<count.x;x++){float weight=(area.x?1.0/count.x:(x==0?1-fraction.x:fraction.x))*(area.y?1.0/count.y:(y==0?1-fraction.y:fraction.y));
  precise vec4 term=quick_overlay_texel(clamp(first+ivec2(x,y),ivec2(0),size-1))*weight-correction;precise vec4 next=sum+term;correction=(next-sum)-term;sum=next;
 }return sum;
}

uint surface_word(int index){ivec3 dims=textureSize(surface_img,0);int texel=index/4;uvec4 value=texelFetch(surface_img,ivec3(texel%dims.x,(texel/dims.x)%dims.y,texel/(dims.x*dims.y)),0);return value[index%4];}
vec3 surface_vec3(int offset){return vec3(uintBitsToFloat(surface_word(offset)),uintBitsToFloat(surface_word(offset+1)),uintBitsToFloat(surface_word(offset+2)));}
vec3 inverse_rotation(vec3 v){return vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v));}
)GLSL";
const char *projected_layer_source = R"GLSL(
// Bitmap triplanar shares the CPU Float32 packed-frame/fraction contract.
// Keep UV and analytic Atom on their established sampler paths.
float bitmap_dot(vec3 a,vec3 b){precise float xy=a.x*b.x+a.y*b.y;precise float value=xy+a.z*b.z;return value;}
float bitmap_lerp(float a,float b,float t){
 if((a<=0&&b>=0)||(a>=0&&b<=0)){precise float value=t*b+(1-t)*a;return value;}
 if(t==1)return b;precise float delta=b-a;precise float value=a+t*delta;return (t>1)==(b>a)?max(b,value):min(b,value);
}
vec4 bitmap_lerp4(vec4 a,vec4 b,float t){return vec4(bitmap_lerp(a.x,b.x,t),bitmap_lerp(a.y,b.y,t),bitmap_lerp(a.z,b.z,t),bitmap_lerp(a.w,b.w,t));}
vec4 bitmap_resource_sample(vec2 uv){
 precise vec2 wrapped=source_wrap==0?fract(uv):clamp(uv,vec2(0),vec2(1));precise vec2 point=wrapped*vec2(textureSize(source_img,0))-.5;ivec2 first=ivec2(floor(point));precise vec2 fraction=point-vec2(first);
 return bitmap_lerp4(bitmap_lerp4(resource_texel(first),resource_texel(first+ivec2(1,0)),fraction.x),bitmap_lerp4(resource_texel(first+ivec2(0,1)),resource_texel(first+ivec2(1)),fraction.x),fraction.y);
}
vec2 bitmap_projection_map(vec3 point,int copy){precise vec2 uv=vec2(bitmap_dot(map_u,point),bitmap_dot(map_v,point));if((copy&1)!=0){if(symmetry_flip.x!=0)uv.x=1-uv.x;if(symmetry_flip.y!=0)uv.y=1-uv.y;}return uv;}
vec4 bitmap_triplanar_sample(vec2 uv){
 ivec2 at=clamp(ivec2(uv*vec2(surface_size)),ivec2(0),surface_size-1);int first=(at.y*surface_size.x+at.x)*surface_stride;if((surface_word(first+7)&255u)==0u)return vec4(0);
 vec3 world=surface_vec3(first),world_normal=surface_vec3(first+3);int copy=fold_projection(world,world_normal);precise vec3 delta=world-projection_offset;
 precise vec3 position=vec3(bitmap_dot(rotation_x,delta),bitmap_dot(rotation_y,delta),bitmap_dot(rotation_z,delta))*inverse_denominator;
 precise vec3 normal=vec3(bitmap_dot(rotation_x,world_normal),bitmap_dot(rotation_y,world_normal),bitmap_dot(rotation_z,world_normal))*projection_scale;
 precise vec3 weights=pow(abs(normal),vec3(projection_hardness));precise float total=(weights.x+weights.y)+weights.z;if(total<1e-20)return vec4(0);
 float footprint=uintBitsToFloat(surface_word(first+6));precise vec4 result=vec4(0);
 for(int axis=0;axis<3;axis++){int x=axis==0?1:0,y=axis==2?1:2;precise float weight=(weights[axis]/total)*.25;for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++){
  precise vec3 point=vec3((position[x]+.5)+((-.25+.5*ix)*footprint)*inverse_denominator[x],(position[y]+.5)+((-.25+.5*iy)*footprint)*inverse_denominator[y],1);
  precise vec4 pixel=bitmap_resource_sample(bitmap_projection_map(point,copy));result+=pixel*weight;
 }}return result;
}
vec4 triplanar_sample(vec2 uv){
 if(fill_resource_kind==1)return bitmap_triplanar_sample(uv);
 ivec2 at=clamp(ivec2(uv*vec2(surface_size)),ivec2(0),surface_size-1);int first=(at.y*surface_size.x+at.x)*surface_stride;
 if((surface_word(first+7)&255u)==0u)return vec4(0);
 if(fill_resource_kind==0)return vec4(1);
 vec3 position=surface_vec3(first),normal=surface_vec3(first+3);int copy=fold_projection(position,normal);
 position=inverse_rotation(position-projection_offset)*inverse_denominator;normal=inverse_rotation(normal)*projection_scale;
 vec3 weights=pow(abs(normal),vec3(projection_hardness));float total=weights.x+weights.y+weights.z;if(total<1e-20)return vec4(0);weights/=total;
 float footprint=uintBitsToFloat(surface_word(first+6));vec4 result=vec4(0);
 for(int axis=0;axis<3;axis++){int x=axis==0?1:0,y=axis==2?1:2;for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++){
  vec3 point=vec3(position[x]+.5+(-.25+.5*ix)*footprint*inverse_denominator[x],position[y]+.5+(-.25+.5*iy)*footprint*inverse_denominator[y],1);
  result+=fill_resource_sample(projection_map(point,copy))*weights[axis]*.25;
 }}return result;
}
float effect_curve(float value,int row,int component){
 vec4 a=texelFetch(effects_img,ivec2(1+component*2,row),0);float high=texelFetch(effects_img,ivec2(2+component*2,row),0).x;
 if(a.w==high)return high;float mapped=(value-a.x)/(a.y-a.x);if(mapped>0)mapped=pow(mapped,a.z);return clamp(a.w+(high-a.w)*mapped,-65504.0,65504.0);
}
vec4 mask_map_sample(sampler2D image,vec2 uv){
 ivec2 size=textureSize(image,0);vec4 value=texelFetch(image,clamp(ivec2(uv*vec2(size)),ivec2(0),size-1),0);if(value.a>1e-8)value.rgb/=value.a;return value;
}
vec4 generator_map_sample(sampler2D image,vec2 uv,bool linear_filter){
 if(!linear_filter)return mask_map_sample(image,uv);
 ivec2 size=textureSize(image,0);vec2 p=uv*vec2(size)-.5;ivec2 base=ivec2(floor(p));vec2 f=fract(p);
 vec4 a=texelFetch(image,clamp(base,ivec2(0),size-1),0),b=texelFetch(image,clamp(base+ivec2(1,0),ivec2(0),size-1),0),c=texelFetch(image,clamp(base+ivec2(0,1),ivec2(0),size-1),0),d=texelFetch(image,clamp(base+ivec2(1),ivec2(0),size-1),0);
 vec4 value=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);if(value.a>1e-8)value.rgb/=value.a;return value;
}
float mask_smooth(float value){value=clamp(value,0.0,1.0);return value*value*(3-2*value);}
vec2 material_comparison=vec2(0);
vec3 filter_rgb_to_hsv(vec3 c){float maxc=max(c.r,max(c.g,c.b)),minc=min(c.r,min(c.g,c.b)),d=maxc-minc,h=0.0;if(d>1e-8){if(maxc==c.r)h=(c.g-c.b)/d;else if(maxc==c.g)h=(c.b-c.r)/d+2.0;else h=(c.r-c.g)/d+4.0;h=fract(h/6.0);}return vec3(h,maxc<=1e-8?0.0:d/maxc,maxc);}
vec3 filter_hsv_to_rgb(vec3 c){vec3 p=abs(fract(c.xxx+vec3(0.0,2.0/3.0,1.0/3.0))*6.0-3.0);return c.z*mix(vec3(1.0),clamp(p-1.0,0.0,1.0),c.y);}
uint grunge_hash(uint x,uint y,uint seed){uint n=x*374761393u+y*668265263u+seed*1442695041u;n=(n^(n>>13))*1274126177u;return n^(n>>16);}
float grunge_lerp(float a,float b,float t){precise float delta=b-a;precise float scaled=t*delta;precise float result=a+scaled;return result;}
float grunge_noise(vec2 p,uint seed){ivec2 b=ivec2(floor(p));precise vec2 f=fract(p);f=f*f*(3-2*f);precise float a=float(grunge_hash(uint(b.x),uint(b.y),seed)&65535u)/65535.0,bb=float(grunge_hash(uint(b.x+1),uint(b.y),seed)&65535u)/65535.0,c=float(grunge_hash(uint(b.x),uint(b.y+1),seed)&65535u)/65535.0,d=float(grunge_hash(uint(b.x+1),uint(b.y+1),seed)&65535u)/65535.0;return grunge_lerp(grunge_lerp(a,bb,f.x),grunge_lerp(c,d,f.x),f.y);}
float native_grunge(vec2 uv,vec4 settings){if(settings.w==3){
 uint s=uint(settings.z);precise float scale=settings.y;
 precise float wob=(grunge_noise(vec2(uv.x*scale*2.2,uv.y*scale*1.6),s+5u)-.5)*.28;
 precise float col=uv.x*scale*6.5+wob,s1=grunge_noise(vec2(col,uv.y*scale*.32),s+11u),s2=grunge_noise(vec2(col*2.15+1.7,uv.y*scale*.72+.9),s+29u),s3=grunge_noise(vec2(col*4.3+3.1,uv.y*scale*1.5+2.3),s+53u);
 precise float streak=clamp((.52*s1+.31*s2+.17*s3-.38)*2.1,0.0,1.0);
 precise float env=clamp(.35+.65*grunge_noise(vec2(uv.x*scale*1.8+.4,uv.y*scale*.45+.2),s+71u),0.0,1.0);
 precise float fine=grunge_noise(vec2(uv.x*scale*5.0+1.3,uv.y*scale*3.5+.7),s+97u);
 precise float output_value=streak*env*(.55+.45*fine);return clamp(output_value,0.0,1.0);
}precise vec2 p=uv*settings.y;if(settings.w==2)p*=vec2(8,.25);precise float result=0,weight=.533333333;for(int i=0;i<4;++i){result+=weight*grunge_noise(p,uint(settings.z)+uint(i*17));p=p*2.03+vec2(3.1,1.7);weight*=.5;}return settings.w==1?clamp((result-.35)*2,0.0,1.0):result;}
// Spatial generator coordinates are shared across UV islands.
struct BtsGeneratorSurface {vec3 position;vec3 normal;bool valid;};
BtsGeneratorSurface generator_surface_load(vec2 uv,int available,bool primary_anchored,int generator){
 BtsGeneratorSurface s;s.position=vec3(0);s.normal=vec3(0);s.valid=false;
 if(surface_stride>0&&all(greaterThan(surface_size,ivec2(0)))){
  ivec2 p=clamp(ivec2(uv*vec2(surface_size)),ivec2(0),surface_size-1);int first=(p.y*surface_size.x+p.x)*surface_stride;
  if((surface_word(first+7)&255u)!=0u){s.position=(surface_vec3(first)-projection_offset)*inverse_denominator;s.normal=surface_vec3(first+3);s.valid=length(s.normal)>1e-12;}
 }
 if(!s.valid&&(available&32)!=0&&(available&2)!=0){vec4 p=primary_anchored&&generator==1?generator_map_sample(mask_thickness_img,uv,true):generator_map_sample(mask_position_img,uv,true),n=primary_anchored&&generator==6?generator_map_sample(mask_thickness_img,uv,true):generator_map_sample(mask_world_normal_img,uv,true);if(p.a>1e-8&&n.a>1e-8){s.position=p.rgb-.5;s.normal=n.rgb*2-1;s.valid=length(s.normal)>1e-12;}}
 return s;
}
vec4 generator_surface_map(BtsGeneratorSurface s,int role){
 if(!s.valid)return vec4(0);vec3 n=normalize(s.normal);
 if(role==5)return vec4(clamp(s.position+.5,vec3(0),vec3(1)),1);
 if(role==1)return vec4(.5+.5*n,1);
 float up=clamp(.5+.5*n.z,0.0,1.0),height=clamp(s.position.z+.5,0.0,1.0);
 float value=role==3?.15+.3*up+.15*height:role==4?.5+.65*(1-abs(n.z)):role==6?.25+.5*height:.5;
 return vec4(vec3(value),1);
}
float native_grunge_triplanar(BtsGeneratorSurface s,vec4 settings){
 precise vec3 weights=abs(s.normal);weights=weights*weights*weights*weights;precise float total=weights.x+weights.y+weights.z;
 if(total<=1e-20)return .5;
 if(settings.w==3){precise float value=(weights.x*native_grunge(vec2(s.position.z+.5,.5-s.position.y),settings)+weights.z*native_grunge(vec2(s.position.x+.5,.5-s.position.y),settings)+weights.y*.15*native_grunge(s.position.xz+.5,settings))/total;return value;}
 weights/=total;precise float value=weights.x*native_grunge(s.position.yz+.5,settings)+weights.y*native_grunge(s.position.xz+.5,settings)+weights.z*native_grunge(s.position.xy+.5,settings);return value;
}
vec4 generator_optional_map(sampler2D image,vec2 uv,bool filtering,int available,int role,bool fallback,BtsGeneratorSurface surface){
 vec4 value=(available&(1<<role))!=0?generator_map_sample(image,uv,filtering):vec4(0);
 return value.a>1e-8||!fallback?value:generator_surface_map(surface,role);
}
// Match the CPU std::lerp arithmetic with -ffp-contract=off. A contracted
// mix can move a signed value across a later half-precision rounding boundary.
float levels_mix_lerp(float a,float b,float t){
 if((a<=0&&b>=0)||(a>=0&&b<=0)){precise float left=t*b;precise float right=(1-t)*a;precise float result=left+right;return result;}
 if(t==1)return b;
 precise float delta=b-a;precise float scaled=t*delta;precise float result=a+scaled;
 return (t>1)==(b>a)?(b<result?result:b):(b>result?result:b);
}
vec3 point_effect(vec3 value,int row,bool color,bool mask,vec2 uv,ivec2 texel){
 vec4 header=texelFetch(effects_img,ivec2(0,row),0);int kind=int(header.x);
 if(kind<0)return value;
 if(kind==7){vec3 result=vec3(dot(texelFetch(effects_img,ivec2(1,row),0).xyz,value),dot(texelFetch(effects_img,ivec2(2,row),0).xyz,value),dot(texelFetch(effects_img,ivec2(3,row),0).xyz,value));return result*source_value;}
 if(kind==4){
  vec4 data=texelFetch(effects_img,ivec2(1,row),0);int count=int(texelFetch(effects_img,ivec2(2,row),0).x);vec4 sample_value=mask_map_sample(mask_id_img,uv);
  if(data.x==0||count==0||sample_value.a<=1e-8)return vec3(0);
  float distance_value=1e30;for(int i=0;i<count;i++){vec3 color_value=texelFetch(effects_img,ivec2(1+i%8,row+1+i/8),0).rgb;vec3 delta=color_value-sample_value.rgb;distance_value=min(distance_value,sqrt(dot(delta,delta)/3));}
  if(distance_value<=data.w*data.z)return vec3(data.y);if(distance_value>=data.w)return vec3(0);return vec3(data.y*mask_smooth((data.w-distance_value)/max(1e-8,data.w*(1-data.z))));
 }
 if(kind==0){
  if(int(header.y)==4||header.z<=0)return value;
  vec3 adjusted=value;for(int c=0;c<3;c++){adjusted[c]=effect_curve(adjusted[c],row,0);if(!mask)adjusted[c]=effect_curve(adjusted[c],row,c+1);}
  if(header.w!=0)adjusted=clamp(adjusted,vec3(0),vec3(1));
  if(int(header.y)==0&&header.z==1)return adjusted;
  vec3 blended;if(color)blended=blend_rgb(adjusted,value,int(header.y));else for(int c=0;c<3;c++)blended[c]=blend_scalar(adjusted[c],value[c],int(header.y));vec3 mixed;for(int c=0;c<3;c++)mixed[c]=levels_mix_lerp(value[c],blended[c],header.z);return mixed;
 }
 if(int(header.y)==4)return value;
 vec4 data=texelFetch(effects_img,ivec2(1,row),0);vec3 filtered=value;
 if(kind==8)filtered=data.rgb;
 else if(kind==3){
  int response=int(texelFetch(effects_img,ivec2(0,row),0).w);vec4 options=texelFetch(effects_img,ivec2(2,row),0);int behavior=int(options.y);if(data.x==0||(behavior&3)==0)return value;
  vec4 specific=texelFetch(effects_img,ivec2(3,row),0);
  vec4 grunge=texelFetch(effects_img,ivec2(7,row),0),influences=texelFetch(effects_img,ivec2(8,row),0);
  vec4 source_options=texelFetch(effects_img,ivec2(4,row),0),flags=texelFetch(effects_img,ivec2(6,row),0);
  BtsGeneratorSurface surface;surface.position=vec3(0);surface.normal=vec3(0);surface.valid=false;if((behavior&6)!=0)surface=generator_surface_load(uv,int(flags.w),flags.x!=0,int(data.x));bool fallback=(behavior&2)!=0,spatial=(behavior&4)!=0,noise_required=data.x==7||grunge.x>0;
  if(noise_required&&spatial&&!surface.valid)return value;
  vec4 noise_settings=grunge;if(response!=0&&grunge.w!=3)noise_settings.y*=8;
  float noise=!noise_required?.5:spatial?native_grunge_triplanar(surface,noise_settings):native_grunge(uv,noise_settings);
  vec4 sample_value=data.x==6?generator_input_sample(mask_world_normal_img,uv,options.w!=0,flags.x!=0,source_options):data.x==4?generator_input_sample(mask_thickness_img,uv,options.w!=0,flags.x!=0,source_options):(data.x==3||data.x==5||data.x==9)?generator_input_sample(mask_ao_img,uv,options.w!=0,flags.x!=0,source_options):data.x==1?generator_input_sample(mask_position_img,uv,options.w!=0,flags.x!=0,source_options):generator_input_sample(mask_curvature_img,uv,options.w!=0,flags.x!=0,source_options);
  int primary_role=data.x==1?5:data.x==2||data.x==8?4:data.x==4?6:data.x==6?1:3;
  if(sample_value.a<=1e-8&&fallback&&flags.x==0){if(data.x!=7&&!surface.valid)return value;sample_value=generator_surface_map(surface,primary_role);}
  if(data.x==7)sample_value=vec4(noise,noise,noise,1);
  float radius=texelFetch(effects_img,ivec2(9,row),0).x;
  if(data.x==8&&radius>0&&flags.x==0){radius=min(radius,2.5);ivec2 extent=textureSize(mask_curvature_img,0);for(int i=0;i<8;++i){vec2 offset=i<4?(i<2?vec2(i==0?1:-1,0):vec2(0,i==2?1:-1)):vec2(i==4||i==5?1:-1,i==4||i==6?1:-1);vec4 nearby=generator_map_sample(mask_curvature_img,uv+offset*radius/vec2(extent),options.w!=0);if(nearby.a>1e-8)sample_value.r=max(sample_value.r,nearby.r);}}
  float generated=0;if(sample_value.a>1e-8){float input_value=sample_value[data.x==1?int(data.y):0];bool covered=true;
   if(data.x==8){vec4 ao=generator_input_sample(mask_ao_img,uv,options.w!=0,flags.y!=0,texelFetch(effects_img,ivec2(5,row),0));if(ao.a<=1e-8&&fallback&&flags.y==0){if(!surface.valid)return value;ao=generator_surface_map(surface,3);}covered=ao.a>1e-8;float convex=max(0.0,(sample_value.r-.5)*2);if(response!=0&&convex>0.0)convex=min(1.0,pow(convex,.6)*1.32);input_value=convex*influences.y;if(response>0&&response<4&&grunge.w==2&&influences.x<.75)input_value=max(input_value,noise*.46);input_value*=mix(1.0,clamp(ao.r,0.0,1.0),influences.x);}
   else if(data.x==9){vec4 curvature=generator_input_sample(mask_curvature_img,uv,options.w!=0,flags.y!=0,texelFetch(effects_img,ivec2(5,row),0));if(curvature.a<=1e-8&&fallback&&flags.y==0){if(!surface.valid)return value;curvature=generator_surface_map(surface,4);}covered=curvature.a>1e-8;float occ=1-clamp(sample_value.r,0.0,1.0);if(response!=0&&occ>0.0)occ=pow(occ,.72);float conc=max(0.0,(.5-curvature.r)*2);if(response!=0&&conc>0.0)conc=pow(conc,.72);input_value=clamp(occ*influences.x+conc*influences.y,0.0,1.0);}
   else if(data.x==5){vec4 curvature=generator_input_sample(mask_curvature_img,uv,options.w!=0,flags.y!=0,texelFetch(effects_img,ivec2(5,row),0));if(curvature.a<=1e-8&&fallback&&flags.y==0){if(!surface.valid)return value;curvature=generator_surface_map(surface,4);}covered=curvature.a>1e-8;input_value=(1-clamp(sample_value.r,0.0,1.0))*(1-specific.w*clamp(curvature.r,0.0,1.0));}
   else if(data.x==6){vec3 normal_value=sample_value.rgb*2-1;float normal_square=dot(normal_value,normal_value),light_square=dot(specific.xyz,specific.xyz);input_value=normal_square>1e-12&&light_square>1e-12?max(0.0,dot(normal_value,specific.xyz)/sqrt(normal_square*light_square)):0.0;}
   if(covered){if(response==3){vec4 normal_value=generator_optional_map(mask_world_normal_img,uv,options.w!=0,int(flags.w),1,fallback,surface);float up=normal_value.a>1e-8?clamp((normal_value.y*2-1)*.75+.18,0.0,1.0):0.0;input_value=clamp(mix(input_value,up,influences.z)+.12*up,0.0,1.0);}if(data.x==8||data.x==9){int available=int(flags.w);if(influences.z>0&&response!=3){vec4 position=generator_optional_map(mask_position_img,uv,options.w!=0,available,5,fallback,surface);if(position.a>1e-8)input_value=response==4?mix(input_value,clamp(position[int(data.y)],0.0,1.0),influences.z):input_value*mix(1.0,clamp(position[response!=0?int(data.y):2],0.0,1.0),influences.z);}if(influences.w>0){vec4 normal_value=generator_optional_map(mask_world_normal_img,uv,options.w!=0,available,1,fallback,surface);if(normal_value.a>1e-8)input_value*=mix(1.0,(response!=0?max(0.0,normal_value[int(data.y)]*2-1):clamp(normal_value.z,0.0,1.0)),influences.w);}if(flags.z>0){vec4 thickness=generator_optional_map(mask_thickness_img,uv,options.w!=0,available,6,fallback,surface);if(thickness.a>1e-8)input_value*=mix(1.0,1-clamp(thickness.r,0.0,1.0),flags.z);}}if(response==2){if(!surface.valid)return value;float a=native_grunge(vec2(surface.position.x+.5,.5-surface.position.y),vec4(grunge.xyz,3)),b=native_grunge(vec2(surface.position.z+.5,.5-surface.position.y),vec4(grunge.xyz,3)),nx=abs(surface.normal.x),nz=abs(surface.normal.z),streak=nx+nz>1e-8?(a*nz+b*nx)/(nx+nz):a;float vertical=1-clamp(abs(surface.normal.y),0.0,1.0),drip=clamp(streak*1.45*sqrt(vertical)*mix(.65,1.0,clamp(input_value,0.0,1.0)),0.0,1.0);input_value=max(input_value*mix(1.0,2*noise,grunge.x),drip);}else if(data.x!=7&&response==4&&grunge.x>0)input_value=clamp(mix(input_value,noise,grunge.x),0.0,1.0);else if(data.x!=7&&grunge.x>0)input_value=response!=0?clamp(input_value*mix(1.0,2*noise,grunge.x),0.0,1.0):clamp(input_value+(noise-.5)*grunge.x,0.0,1.0);generated=clamp((input_value-.5+(response==4?data.w:0))*data.z+.5+(response==4?0:data.w),0.0,1.0);if(response>0&&response<4&&input_value<=0&&(data.x==8||data.x==9))generated=0;if(options.x!=0)generated=1-generated;if(options.z==1)generated=mask_smooth(generated);else if(options.z==2)generated*=generated;}
  }filtered=vec3(generated);
 }else if(kind==5){
  vec4 options=texelFetch(effects_img,ivec2(2,row),0);float a=data.x<2?material_comparison[int(data.x)]:data.x==3?data.w:value.r,b=data.y<2?material_comparison[int(data.y)]:data.y==3?data.w:value.r;
  vec4 ramp_options=texelFetch(effects_img,ivec2(3,row),0);
  float result_value;if(ramp_options.y>=0){
   float delta=(data.z==0?b-a:data.z==1?options.y-abs(a-b):a-b)-ramp_options.x;
   float t=ramp_options.y<=1e-7?(delta>=0?1.0:0.0):clamp(.5+delta/ramp_options.y,0.0,1.0);
   vec4 points=texelFetch(effects_img,ivec2(4,row),0);int i=min(3,int(t*4));
   result_value=mix(points[i],i==3?texelFetch(effects_img,ivec2(5,row),0).x:points[i+1],t*4-i);
   if(ramp_options.z!=0)result_value=1-result_value;
  }else if(data.z==1){float delta=abs(a-b),inner=options.y*options.x;result_value=delta<=inner?1:delta>=options.y?0:mask_smooth((options.y-delta)/max(1e-8,options.y-inner));}
  else {float delta=data.z==0?b-a:a-b,width=1-options.x;result_value=width<=1e-7?(delta>=0?1:0):mask_smooth((delta+width)/width);}filtered=vec3(result_value);
 }else if(kind==6){
  vec3 u=texelFetch(effects_img,ivec2(2,row),0).xyz,v=texelFetch(effects_img,ivec2(3,row),0).xyz;vec4 options=texelFetch(effects_img,ivec2(4,row),0);
  float sum=0;int count=data.y==1?1:4;
  for(int i=0;i<count;i++){vec2 delta=count==1?vec2(0):vec2((i&1)!=0?.25:-.25,(i&2)!=0?.25:-.25);vec3 p=vec3(uv+delta/vec2(imageSize(result_img)),1);vec2 mapped=vec2(dot(u,p),dot(v,p));
   if((data.z==2&&(any(lessThan(mapped,vec2(0)))||any(greaterThan(mapped,vec2(1)))))||(data.z==3&&(mapped.y<0||mapped.y>1))||(data.z==4&&(mapped.x<0||mapped.x>1)))continue;
   if(data.z==1)mapped=clamp(mapped,vec2(0),vec2(uintBitsToFloat(0x3f7fffffu)));
   sum+=atom_value_options(mapped,options);
  }filtered=vec3(data.x*sum/count);
 }else if(kind==2)filtered=vec3(data.z);else {int filter_kind=int(data.x);if(filter_kind==0)return value;vec4 hsv=texelFetch(effects_img,ivec2(2,row),0),levels=texelFetch(effects_img,ivec2(3,row),0),span=texelFetch(effects_img,ivec2(4,row),0);if(filter_kind==1)filtered=1.0-value;else if(filter_kind==2)filtered=clamp(value,vec3(0),vec3(1));else if(filter_kind==3)filtered=floor(clamp(value,vec3(0),vec3(1))*(data.y-1.0)+.5)/(data.y-1.0);else if(filter_kind==4){vec3 hsv_value=filter_rgb_to_hsv(value);hsv_value.x=fract(hsv_value.x+hsv.x);hsv_value.y=clamp(hsv_value.y*hsv.y,0.0,1.0);hsv_value.z=clamp(hsv_value.z*hsv.z,0.0,1.0);filtered=filter_hsv_to_rgb(hsv_value);}else if(filter_kind==5)filtered=(value-vec3(.5))*levels.x+vec3(.5)+hsv.w;else if(filter_kind==6){float width=levels.z-levels.y,gamma=max(levels.w,.01);vec3 mapped=abs(width)<1e-6?vec3(0):clamp((value-vec3(levels.y))/width,vec3(0),vec3(1));filtered=span.x+(span.y-span.x)*pow(mapped,vec3(1.0/gamma));}else if(filter_kind==9||filter_kind==10){ivec2 limit=textureSize(backdrop_img,0)-ivec2(1);vec2 direction;if(filter_kind==9){vec4 noise=vec4(0,span.z*4,0,0);direction=vec2(native_grunge(uv,noise),native_grunge(uv+vec2(.37,.61),noise))*2-1;float angle=hsv.x*6.28318530718;direction=mat2(cos(angle),sin(angle),-sin(angle),cos(angle))*direction;}else{vec3 dx=texelFetch(backdrop_img,clamp(texel+ivec2(1,0),ivec2(0),limit),0).rgb-texelFetch(backdrop_img,clamp(texel-ivec2(1,0),ivec2(0),limit),0).rgb,dy=texelFetch(backdrop_img,clamp(texel+ivec2(0,1),ivec2(0),limit),0).rgb-texelFetch(backdrop_img,clamp(texel-ivec2(0,1),ivec2(0),limit),0).rgb;direction=mask?vec2(dx.r,dy.r):vec2(dot(dx,vec3(.2126,.7152,.0722)),dot(dy,vec3(.2126,.7152,.0722)));}vec3 sum=vec3(0);int count=filter_kind==10?5:1;for(int i=0;i<count;++i){float amount=hsv.z*12*(filter_kind==10?float(i)/4:1);ivec2 target=clamp(texel+ivec2(round(direction*amount)),ivec2(0),limit);vec4 sample_value=texelFetch(backdrop_img,target,0);sum+=mask?vec3(sample_value.r):sample_value.rgb;}filtered=sum/count;}
else if(filter_kind==7||filter_kind==8){int radius=clamp(int(span.z+.5),1,4);vec3 sum=vec3(0);float count=0;ivec2 limit=textureSize(backdrop_img,0)-ivec2(1);for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++){vec4 sample_value=texelFetch(backdrop_img,clamp(texel+ivec2(dx,dy),ivec2(0),limit),0);sum+=mask?vec3(sample_value.r):sample_value.rgb;count+=1.0;}vec3 average=sum/count;filtered=filter_kind==7?average:value+(value-average);}else return value;}
 vec3 blended;if(color)blended=blend_rgb(filtered,value,int(header.y));else for(int c=0;c<3;c++)blended[c]=blend_scalar(filtered[c],value[c],int(header.y));return mix(value,blended,header.z);
}
bool geometry_included(ivec2 p){
 if(geometry_masked==0)return true;
 uint owner=texelFetch(geometry_owners_img,p,0).r;if(owner==0u)return false;
 int low=0,high=textureSize(geometry_selection_img,0).x;
 while(low<high){int middle=(low+high)/2;uvec2 row=texelFetch(geometry_selection_img,ivec2(middle,0),0).rg;if(row.x<owner)low=middle+1;else high=middle;}
 if(low>=textureSize(geometry_selection_img,0).x)return false;
 uvec2 row=texelFetch(geometry_selection_img,ivec2(low,0),0).rg;return row.x==owner&&row.y!=0u;
}
void main(){
 ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,region_size)))return;ivec2 p=region_origin+local,size=imageSize(result_img);if(any(greaterThanEqual(p,size)))return;
 vec4 old=texelFetch(backdrop_img,min(p,textureSize(backdrop_img,0)-1),0);
 if(mask_pass!=0){
  float value=old.r;vec2 uv=(vec2(p)+.5)/vec2(size);
  if(mask_pass==1){vec4 ink=paint_sample(source_img,p,size);value=clamp(ink.r+mask_background*(1-ink.a),0.0,1.0);}
  else if(mask_pass==2){for(int row=0;row<effect_count;row++)value=point_effect(vec3(value),row,false,true,uv,p).r;}
  else if(mask_pass==7){
   vec4 under=texelFetch(source_img,min(p,textureSize(source_img,0)-1),0),source=texelFetch(mask_img,min(p,textureSize(mask_img,0)-1),0);
   vec3 below=under.rgb+compare_base*(1-under.a),own=source.rgb;
   if(compare_channel==3){below=normal_unit(below)*.5+.5;own=normal_unit(own*2-1)*.5+.5;}
   material_comparison=compare_channel==0||compare_channel==3?vec2(dot(below,vec3(.2126,.7152,.0722)),dot(own,vec3(.2126,.7152,.0722))):vec2(below.r,own.r);
   if(compare_enabled.x==0)material_comparison.x=0;if(compare_enabled.y==0)material_comparison.y=0;
   for(int row=0;row<effect_count;row++)value=point_effect(vec3(value),row,false,true,uv,p).r;
  }
  else if(mask_pass==3){vec4 ink=paint_sample(source_img,p,size);if(ink.a>0)value=mix(value,blend_scalar(ink.r/ink.a,value,blend_mode),ink.a*opacity_value);}
  else if(mask_pass==4){vec4 sampled;if(projection_mode==0||projection_mode==6){if((projection_mode==6&&source_filter!=2)||(projection_mode==0&&source_filter==0)){sampled=vec4(0);for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++){vec3 point=vec3((vec2(p)+vec2(.25+.5*ix,.25+.5*iy))/vec2(size),1);sampled+=spatial_resource(vec2(dot(map_u,point),dot(map_v,point)))*.25;}}else{vec3 point=vec3(uv,1);sampled=spatial_resource(vec2(dot(map_u,point),dot(map_v,point)));}}else sampled=projection_mode==1?triplanar_sample(uv):spatial_sample(uv);float gray=fill_resource_kind==1?sampled.r:mask_background*sampled.r;value=mix(value,blend_scalar(gray,value,blend_mode),opacity_value);}
  else if(mask_pass==6){vec4 sampled=anchor_value(texelFetch(source_img,min(p,textureSize(source_img,0)-1),0));float anchored=clamp(sampled.r*sampled.a,0.0,1.0);value=mix(value,blend_scalar(anchored,value,blend_mode),opacity_value);}
  imageStore(result_img,p,vec4(value,0,0,1));return;
 }
 if(content_pass==2){
  vec2 uv=(vec2(p)+.5)/vec2(size);vec3 value=old.rgb;for(int row=0;row<content_effects;row++)value=point_effect(value,row,scalar_channel==0,false,uv,p);
  imageStore(result_img,p,vec4(value,old.a));return;
 }
 if(content_pass==5){vec4 data=texelFetch(effects_img,ivec2(1,0),0);imageStore(result_img,p,vec4(data.w!=0?old.rgb:quantize_value(data.rgb,value_precision),opacity_value));return;}
 if(content_pass==3||content_pass==4||content_pass==6||content_pass==7){
  vec4 ink;
  if(content_pass==4){vec4 generator_data=texelFetch(effects_img,ivec2(1,0),0),options=texelFetch(effects_img,ivec2(2,0),0);if(texelFetch(effects_img,ivec2(0,0),0).x==3&&(generator_data.x==0||options.y==0)){imageStore(result_img,p,old);return;}vec2 uv=(vec2(p)+.5)/vec2(size);ink=vec4(point_effect(vec3(0),0,scalar_channel==0,false,uv,p),1);if(texelFetch(effects_img,ivec2(0,0),0).x==3)ink.rgb*=source_value;}
  else if(content_pass==6||content_pass==7){
   ink=vec4(0);for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++){vec3 point=vec3((vec2(p)+vec2(.25+.5*ix,.25+.5*iy))/vec2(size),1);ink+=fill_resource_sample(vec2(dot(map_u,point),dot(map_v,point)))*.25;}
   if(ink.a>0)ink.rgb/=ink.a;ink.a=1;
   if(scalar_channel!=0&&scalar_channel!=3)ink.rgb=vec3(ink.r);
   if(content_effects==2)ink.rgb=point_effect(ink.rgb,1,true,false,(vec2(p)+.5)/vec2(size),p);
   ink.rgb+=texelFetch(effects_img,ivec2(1,0),0).rgb;
   if(content_pass==7&&blend_mode==4){imageStore(result_img,p,old);return;}
   if(content_pass==7){imageStore(result_img,p,vec4(quantize_value(ink.rgb,value_precision),opacity_value));return;}
  }
  else ink=paint_sample(source_img,p,size);float alpha=ink.a*opacity_value;vec3 value=old.rgb;
  if(blend_mode==4||opacity_value<=0||(alpha<=0&&blend_mode!=1)){imageStore(result_img,p,old);return;}
  vec3 source=ink.a>0?ink.rgb/ink.a:vec3(0);if(scalar_channel==3&&content_pass==3)source=source*.5+.5;else if(scalar_channel!=0&&scalar_channel!=3)source=vec3(source.r);source=quantize_value(source,value_precision);
  float coverage;vec3 result;
  if(blend_mode==1){coverage=alpha+old.a*(1-opacity_value);result=coverage>0?(source*alpha+value*old.a*(1-opacity_value))/coverage:vec3(0);}
  else {vec3 blended;
   if(scalar_channel==0)blended=blend_rgb(source,value,blend_mode);
   else if(scalar_channel==3&&blend_mode>=25&&blend_mode<=27){vec3 base=value*2-1,detail=source*2-1;
    if(blend_mode==25){base=normal_unit(base);detail=normal_unit(detail);blended=normal_unit(vec3(base.xy+detail.xy,base.z*detail.z));}
    else blended=blend_mode==26?normal_detail(base,detail):normal_detail(detail,base);blended=blended*.5+.5;
   }else for(int k=0;k<3;++k)blended[k]=blend_scalar(source[k],value[k],blend_mode);
   coverage=alpha+old.a*(1-alpha);result=((1-alpha)*value*old.a+alpha*((1-old.a)*source+old.a*blended))/coverage;
  }
  imageStore(result_img,p,vec4(result,coverage));return;
 }
 if(layer_active==0||!geometry_included(p)){imageStore(result_img,p,old);return;}
 vec2 uv=(vec2(p)+vec2(.5))/vec2(size);vec4 sampled=vec4(source_value,1.0);
 if(source_kind==3||source_kind==11){sampled=texelFetch(source_img,min(p,textureSize(source_img,0)-1),0);if(sampled.a>0)sampled.rgb/=sampled.a;else sampled.rgb=vec3(0);}
 if(source_kind==10){sampled=texelFetch(source_img,p,0);}
 if(source_kind==12){sampled=texelFetch(source_img,p,0);sampled.rgb=normal_unit(sampled.rgb*2-1);}
 if(source_kind==1){sampled=paint_sample(source_img,p,size);if(sampled.a>0)sampled.rgb/=sampled.a;else sampled.rgb=vec3(0);}
 if(source_kind==8){sampled=quick_overlay_sample(p,size);if(sampled.a>0)sampled.rgb/=sampled.a;else sampled.rgb=vec3(0);}
 if(source_kind==9){sampled=anchor_value(texelFetch(source_img,min(p,textureSize(source_img,0)-1),0));if(scalar_channel==0&&raw_linear_source==0)sampled.rgb*=source_value;else if(scalar_channel==3)sampled.rgb=normal_unit(sampled.rgb*2-1);else if(scalar_channel==1)sampled.rgb=clamp(sampled.rgb,0.0,1.0);}
 if(source_kind==4)sampled=vec4(0);
 if(source_kind==2){sampled=vec4(0);if(source_filter==2&&fill_resource_kind==1){sampled=resource_texel(ivec2(floor(uv*vec2(textureSize(source_img,0)))));}else for(int ix=0;ix<2;ix++)for(int iy=0;iy<2;iy++){vec3 point=vec3((vec2(p)+vec2(.25+.5*ix,.25+.5*iy))/vec2(size),1.0);vec2 mapped=vec2(dot(map_u,point),dot(map_v,point));sampled+=fill_resource_sample(mapped)*.25;}if(sampled.a>1e-8)sampled.rgb/=sampled.a;if(scalar_channel==0&&raw_linear_source==0)sampled.rgb*=source_value;}
 if(source_kind==5){sampled=projection_mode==1?triplanar_sample(uv):spatial_sample(uv);if(sampled.a>1e-8)sampled.rgb/=sampled.a;if(scalar_channel==0&&raw_linear_source==0)sampled.rgb*=source_value;}
 if(source_kind==6)sampled=texelFetch(source_img,min(p,textureSize(source_img,0)-1),0);
 if(scalar_channel!=0&&(source_kind==2||source_kind==5)&&fill_resource_kind!=1)sampled.rgb=source_value;
 if(scalar_channel==3&&fill_resource_kind==1&&(source_kind==2||source_kind==5))sampled.rgb=precompose_source!=0?sampled.rgb*2-1:normal_unit(sampled.rgb*2-1);
 if(scalar_channel!=0&&scalar_channel!=3)sampled.rgb=vec3((source_kind==2||source_kind==5)&&fill_resource_kind==1&&scalar_channel==1&&precompose_source==0?clamp(sampled.r,0.0,1.0):sampled.r);
 if(scalar_channel==4&&(source_kind==2||source_kind==5)&&fill_resource_kind==1)sampled.rgb+=vec3(fill_bump_bias);
 // Normalize only regular Fill coverage after unpremultiplying with the
 // original weighted alpha. Roundoff in normalized triplanar taps can be
 // one ULP above one; straight signed/HDR RGB and Height stay unchanged.
 if(source_kind==2||source_kind==5)sampled.a=clamp(sampled.a,0.0,1.0);
 vec3 value=source_kind==6||source_kind==11||(raw_linear_source!=0&&scalar_channel==0)?sampled.rgb:scalar_channel==3?quantize_value(sampled.rgb*.5+.5,value_precision)*2-1:quantize_value(sampled.rgb,value_precision);precise float alpha=opacity_value*sampled.a;
 float mask=mask_background;if(has_mask!=0){vec4 ink=paint_sample(mask_img,p,size);mask=clamp(ink.r+mask_background*(1-ink.a),0.0,1.0);}
 for(int row=content_effects;row<effect_count;row++)mask=point_effect(vec3(mask),row,false,true,uv,p).r;mask=clamp(mask,0.0,1.0);
 precise vec3 rgb;precise float coverage;
 if(content_pass==1&&source_zero_opacity!=0)sampled.a=0;
 float passthrough=(source_kind==6||source_kind==11)?sampled.a:sampled.a+old.a*(1-sampled.a);
 if(blend_mode==28&&source_kind!=6&&source_kind!=11)value=passthrough>0?(value*sampled.a+old.rgb*(1-sampled.a))/passthrough:vec3(0);
 if(scalar_channel==3&&source_kind!=6)value=value*.5+.5;
 if(content_pass==1){imageStore(result_img,p,vec4(value,blend_mode==28?passthrough:sampled.a));return;}
 for(int row=0;row<content_effects;row++)value=point_effect(value,row,scalar_channel==0,false,uv,p);
 if(scalar_channel==3)value=value*2-1;
 if(blend_mode==28){float weight=opacity_value*mask;rgb=mix(old.rgb,value*passthrough,weight);coverage=mix(old.a,passthrough,weight);}
 else if(blend_mode==1){rgb=value*alpha*mask+old.rgb*(1-mask);coverage=alpha*mask+old.a*(1-mask);}
 else {alpha*=mask;vec3 backdrop=old.a>0?old.rgb/old.a:vec3(0);vec3 blended;if(scalar_channel==3&&blend_mode>=25&&blend_mode<=27){vec3 base=old.a>0?backdrop:vec3(0,0,1);if(blend_mode==25){base=normal_unit(base);vec3 detail=normal_unit(value);blended=normal_unit(vec3(base.xy+detail.xy,base.z*detail.z));}else blended=blend_mode==26?normal_detail(base,value):normal_detail(value,base);}else if(scalar_channel==0)blended=blend_rgb(value,backdrop,blend_mode);else for(int k=0;k<3;k++)blended[k]=blend_scalar(value[k],backdrop[k],blend_mode);rgb=(1-alpha)*old.rgb+alpha*((1-old.a)*value+old.a*blended);coverage=alpha+old.a*(1-alpha);}
 imageStore(result_img,p,vec4(rgb,coverage));
}
)GLSL";
const char *final_source = R"GLSL(
vec3 final_normal_unit(vec3 v){float length_v=length(v);return length_v<1e-8?vec3(0,0,1):v/length_v;}
void main(){ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,region_size)))return;ivec2 p=region_origin+local,size=imageSize(result_img);if(any(greaterThanEqual(p,size)))return;
 vec4 layer=texelFetch(backdrop_img,min(p,textureSize(backdrop_img,0)-1),0);vec3 full=layer.rgb+base_value*(1-layer.a);vec3 value=layer.a<=0?vec3(0):quantize_value(full,value_precision)-base_value*(1-layer.a);vec4 out_value=vec4(value,layer.a);if(output_kind==1)out_value=vec4(layer.a>0?quantize_value(layer.rgb/layer.a,value_precision)*layer.a:vec3(0),layer.a);
 if(output_kind==2)out_value=vec4(quantize_value(final_normal_unit(full)*.5+.5,value_precision),1);
 out_value.rgb+=output_bias*out_value.a;
 // Explicit round-to-nearest-even: imageStore conversion may truncate on GL drivers.
 if(finite_half!=0){for(int k=0;k<4;k++)if(isnan(out_value[k]))out_value[k]=0;else out_value[k]=half_round_even(clamp(out_value[k],-65504.0,65504.0));}
 imageStore(result_img,p,out_value);
}
)GLSL";
gpu::shader::ShaderCreateInfo layer_info(gpu::TextureFormat format)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "source_img")
      .sampler(1, ImageType::Float2D, "mask_img")
      .sampler(2, ImageType::Float2D, "backdrop_img")
      .sampler(3, ImageType::Uint2DArray, "surface_img")
      .sampler(4, ImageType::Float2D, "effects_img")
      .sampler(9, ImageType::Uint2D, "geometry_owners_img")
      .sampler(10, ImageType::Uint2D, "geometry_selection_img")
      .image(0, format, Qualifier::write, ImageReadWriteType::image2D, "result_img");
  info.sampler(5, ImageType::Float2D, "layer_parameters_img")
      .sampler(6, ImageType::Float2D, "mask_id_img")
      .sampler(7, ImageType::Float2D, "mask_position_img")
      .sampler(8, ImageType::Float2D, "mask_curvature_img")
      .sampler(11, ImageType::Float2D, "mask_ao_img")
      .sampler(12, ImageType::Float2D, "mask_thickness_img")
      .sampler(13, ImageType::Float2D, "mask_world_normal_img");
  info.push_constant(Type::int2_t, "region_origin").push_constant(Type::int2_t, "region_size");
  info.compute_source_generated = GpuLayerParameters::declarations() + quant_source +
                                  layer_source + gpu_warp::source + gpu_projection::source +
                                  projected_layer_source;
  return info;
}
Shader compile_layer(gpu::TextureFormat format = gpu::TextureFormat::SFLOAT_32_32_32_32)
{
  auto info = layer_info(format);
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}
Shader compile_final(gpu::TextureFormat format, bool array)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "backdrop_img")
      .image(0,
             format,
             Qualifier::write,
             array ? ImageReadWriteType::image2DArray : ImageReadWriteType::image2D,
             "result_img")
      .push_constant(Type::float3_t, "base_value")
      .push_constant(Type::float3_t, "output_bias")
      .push_constant(Type::int_t, "value_precision")
      .push_constant(Type::int_t, "finite_half")
      .push_constant(Type::int_t, "output_kind")
      .push_constant(Type::int2_t, "region_origin")
      .push_constant(Type::int2_t, "region_size")
      .push_constant(Type::int2_t, "tile_size")
      .push_constant(Type::int3_t, "target_offset");
  std::string source = final_source;
  const auto size = source.find("size=imageSize(result_img)");
  source.replace(size, std::string("size=imageSize(result_img)").size(), "size=tile_size");
  const auto store = source.find("imageStore(result_img,p,out_value)");
  source.replace(store,
                 std::string("imageStore(result_img,p,out_value)").size(),
                 array ?
                     "imageStore(result_img,ivec3(p+target_offset.xy,target_offset.z),out_value)" :
                     "imageStore(result_img,p+target_offset.xy,out_value)");
  info.compute_source_generated = std::string(quant_source) + source;
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}
Shader compile_world_weight()
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .image(0,
             gpu::TextureFormat::SFLOAT_32,
             Qualifier::read_write,
             ImageReadWriteType::image2D,
             "weight_img")
      .sampler(0, ImageType::Float2D, "mask_img");
  info.compute_source_generated =
      "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(weight_img))))return;float w=imageLoad(weight_img,p).r*clamp(texelFetch(mask_img,p,0).r,0.0,1.0);imageStore(weight_img,p,vec4(w));}";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}
Shader compile_world_apply()
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::read_write,
             ImageReadWriteType::image2D,
             "world_img")
      .sampler(0, ImageType::Float2D, "weight_img");
  info.compute_source_generated =
      "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(world_img))))return;imageStore(world_img,p,imageLoad(world_img,p)*texelFetch(weight_img,p,0).r);}";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}
Shader compile_world_publish(bool array)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::write,
             array ? ImageReadWriteType::image2DArray : ImageReadWriteType::image2D,
             "world_img")
      .sampler(0, ImageType::Float2D, "source_img")
      .sampler(1, ImageType::Float2D, "weight_img")
      .push_constant(Type::int3_t, "target_offset")
      .push_constant(Type::int_t, "apply_weight");
  info.compute_source_generated =
      std::string(
          "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,textureSize(source_img,0))))return;vec4 v=texelFetch(source_img,p,0)*(apply_weight!=0?texelFetch(weight_img,p,0).r:1.0);imageStore(world_img,") +
      (array ? "ivec3(p+target_offset.xy,target_offset.z)" : "p+target_offset.xy") + ",v);}";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}
Shader compile_uniform(gpu::TextureFormat format)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .image(0, format, Qualifier::write, ImageReadWriteType::image2D, "result_img")
      .push_constant(Type::float4_t, "fill_value");
  info.compute_source_generated =
      "void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(all(lessThan(p,imageSize(result_img))))imageStore(result_img,p,fill_value);}";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free};
}

}  // namespace
struct GpuLayerPreview::Impl {
  struct PolygonUndo {
    GpuPolygonRaster::Snapshot pixels;
    std::map<std::uint32_t, std::uint64_t> before, after;
    std::uint64_t used = 0;
  };
  struct Entry {
    Texture image{nullptr, GPU_texture_free};
    int width = 0, height = 0, row = 0, plane = -1;
    bool rejected = false, resource = false;
    std::uint64_t used = 0, patch_bytes = 0;
    std::shared_ptr<const void> residency;
    bool retained() const
    {
      return residency && residency.use_count() > 1;
    }
    std::map<std::uint32_t, std::uint64_t> pages;
    std::map<std::uint64_t, PolygonUndo> polygon_undo;
    std::uint64_t bytes() const
    {
      return (image ? std::uint64_t(width) * height * 16 : 0) + patch_bytes;
    }
  };
  std::map<std::string, Entry> inputs;
  performance::Clock::time_point cache_logged{};
  GpuPaintPages authoring_pages;
  AuthoringCacheBudget input_budget;
  struct EffectEntry {
    Texture image{nullptr, GPU_texture_free};
    std::vector<GpuEffectRow> rows;
  };
  std::map<std::string, EffectEntry> effect_parameters;
  gpu::Texture *effects(const std::string &key, const std::vector<GpuEffectRow> &rows)
  {
    if (rows.empty())
      return zero.get();
    auto &entry = effect_parameters[key];
    if (!entry.image || entry.rows.size() != rows.size()) {
      entry.image = texture("BTS GPU effect parameters",
                            int(GpuEffectRow{}.size()),
                            int(rows.size()),
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      entry.rows.clear();
    }
    if (!entry.image) {
      input_rejected = true;
      return nullptr;
    }
    if (entry.rows != rows) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, rows.data());
      entry.rows = rows;
    }
    return entry.image.get();
  }

  struct ParameterEntry {
    Texture image{nullptr, GPU_texture_free};
    GpuLayerParameters values;
  };
  std::map<std::string, ParameterEntry> layer_parameters;
  gpu::Texture *parameters(const std::string &key, const GpuLayerParameters &values)
  {
    auto &entry = layer_parameters[key];
    bool created = false;
    if (!entry.image) {
      entry.image = texture("BTS GPU layer parameters",
                            GpuLayerParameters{}.values.size(),
                            1,
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      created = true;
    }
    if (!entry.image)
      return nullptr;
    if (created || entry.values.values != values.values) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, values.values.data());
      entry.values = values;
    }
    return entry.image.get();
  }
  struct GeometryEntry {
    Texture image{nullptr, GPU_texture_free};
    std::shared_ptr<const FillSurfaceAtlas> owner;
    int tile = 0, row = 0, width = 0, height = 0, layers = 0;
  };
  std::map<std::pair<const FillSurfaceAtlas *, int>, GeometryEntry> geometry;
  Texture empty_geometry{nullptr, GPU_texture_free};
  gpu::Texture *surface(const std::shared_ptr<const FillSurfaceAtlas> &atlas, int tile)
  {
    performance::Scope timing("gpu_projection_source_upload");
    if (!atlas) {
      if (!empty_geometry) {
        const unsigned int data[4] = {};
        empty_geometry = Texture(GPU_texture_create_2d_array("BTS empty surface",
                                                             1,
                                                             1,
                                                             1,
                                                             1,
                                                             gpu::TextureFormat::UINT_32_32_32_32,
                                                             GPU_TEXTURE_USAGE_GENERAL,
                                                             nullptr),
                                 GPU_texture_free);
        if (empty_geometry)
          GPU_texture_clear(empty_geometry.get(), GPU_DATA_UINT, data);
      }
      return empty_geometry.get();
    }
    const auto &points = atlas->tiles.at(tile);
    const auto bytes = points.size() * sizeof(FillSurfacePoint);
    auto &entry = geometry[{atlas.get(), tile}];
    if (!entry.image) {
      entry.owner = atlas;
      entry.tile = tile;
      entry.width = 1024;
      entry.height = int(
          std::min<std::size_t>(8192, (bytes + entry.width * 16 - 1) / (entry.width * 16)));
      entry.layers = int((bytes + std::size_t(entry.width) * entry.height * 16 - 1) /
                         (std::size_t(entry.width) * entry.height * 16));
      entry.image = Texture(GPU_texture_create_2d_array("BTS shared projection surface",
                                                        entry.width,
                                                        entry.height,
                                                        entry.layers,
                                                        1,
                                                        gpu::TextureFormat::UINT_32_32_32_32,
                                                        GPU_TEXTURE_USAGE_GENERAL,
                                                        nullptr),
                            GPU_texture_free);
      if (!entry.image) {
        input_rejected = true;
        return nullptr;
      }
      std::printf("BTS_GPU_SURFACE_ALLOC tile=%d bytes=%zu shared_channels=1\n", tile, bytes);
    }
    const auto *data = reinterpret_cast<const unsigned char *>(points.data());
    const std::size_t row_bytes = entry.width * 16;
    while (std::size_t(entry.row) * row_bytes < bytes) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min({64,
                                 entry.height - entry.row % entry.height,
                                 int(remaining / row_bytes),
                                 int((bytes - entry.row * row_bytes) / row_bytes)});
      if (!rows) {
        if (remaining < row_bytes)
          return nullptr;
        std::vector<unsigned char> tail(row_bytes);
        std::memcpy(tail.data(), data + entry.row * row_bytes, bytes - entry.row * row_bytes);
        GPU_texture_update_sub(entry.image.get(),
                               GPU_DATA_UINT,
                               tail.data(),
                               0,
                               entry.row % entry.height,
                               entry.row / entry.height,
                               entry.width,
                               1,
                               1);
        remaining -= row_bytes;
        ++entry.row;
        break;
      }
      GPU_texture_update_sub(entry.image.get(),
                             GPU_DATA_UINT,
                             data + entry.row * row_bytes,
                             0,
                             entry.row % entry.height,
                             entry.row / entry.height,
                             entry.width,
                             rows,
                             1);
      remaining -= rows * row_bytes;
      entry.row += rows;
    }
    return entry.image.get();
  }

  std::array<int, 2> oracle_position{24, 24};
  struct Query {
    std::string root;
    bool enabled = false;
    QUuid owner;
    std::set<std::string> nodes;
  };
  struct Stack {
    std::map<std::string, QUuid> node_owners;
    std::map<std::string, Query> queries;
    std::set<std::string> queried_sources;
    std::map<std::string, std::map<std::uint64_t, GpuStackPage>> node_versions;
    std::uint64_t node_clock = 0;
    Texture coverage{nullptr, GPU_texture_free}, height_image{nullptr, GPU_texture_free};
    gpu::Texture *head = nullptr;
    std::string head_id;
    GpuStackPlan plan;
    std::map<std::string, std::shared_ptr<gpu::Texture>> outputs;
    int width = 0, height = 0;
    std::uint64_t last_used = 0, revision = 0;
    std::set<int> output_dirty, height_dirty, last_changes;
  };
  std::map<std::string, GpuComparisonInput> comparison_memo;
  std::map<std::string, GpuAnchorInput> anchor_memo;
  std::map<std::string, Stack> stacks, mask_stacks;
  GpuRangeSummary ranges;
  GpuHistogram histograms;
  GpuMaterialSample material_samples;
  GpuThumbnail thumbnails;
  GpuLayerCapture layer_capture;
  std::map<std::uint32_t, std::uint64_t> displacement_epochs;
  std::uint64_t mask_dispatch_texels = 0;
  std::map<std::pair<std::uintptr_t, std::array<int, 3>>, std::string> target_owners;
  struct Margins {
    std::map<int, std::shared_ptr<const GpuMarginPlan>> extend, adjacent;
  };
  std::map<std::uint32_t, Margins> margin_plans;
  GpuMarginExecutor margins;
  GpuGeometryMask geometry_masks;
  struct DerivedEntry {
    GpuDerivedDirty tracking;
    Texture presence{nullptr, GPU_texture_free};
    std::map<int, Texture> dirty_images;
  };
  struct DerivedUpdate {
    GpuDerivedDirty tracking;
    GpuDerivedDirty::Chunks dirty;
    std::map<int, std::vector<std::uint32_t>> flags;
    bool needed = false;
  };
  std::map<std::string, DerivedEntry> derived;
  bool image_changed = false;
  std::size_t last_derived_chunks = 0, last_derived_total = 0;
  std::string derived_key(const TextureSet &set, int channel) const
  {
    return set.id.toString().toStdString() + ":derived:" + std::to_string(channel);
  }
  DerivedUpdate derived_changes(const TextureSet &set,
                                int channel,
                                gpu::Texture *target,
                                const std::map<int, std::array<int, 3>> &regions,
                                int output_tile = 0)
  {
    const auto key = derived_key(set, channel);
    auto &entry = derived[key];
    DerivedUpdate update;
    update.tracking = entry.tracking;
    std::string parameters;
    const auto append = [&](const auto &v) {
      parameters.append(reinterpret_cast<const char *>(&v), sizeof(v));
    };
    append(set.channels);
    append(set.ao_channel);
    append(set.ao_mixing);
    append(set.normal_mixing);
    append(set.normal_format);
    append(set.bump_method);
    append(set.bump_origin);
    append(set.padding_3d);
    for (int c : {3, 4, 5})
      append(set.precision[c]);
    append(gpu_channel_base(set, 4));
    append(gpu_channel_base(set, 5));
    append(gpu_channel_base(set, 6));
    append(reinterpret_cast<std::uintptr_t>(target));
    append(output_tile);
    GpuDerivedDirty::Sizes sizes;
    bool different_target = false;
    const auto &baked = set.mesh_maps[channel == 3 ? 0 : 3];
    for (const auto &[tile, offset] : regions) {
      sizes[tile] = {set.width(tile), set.rows(tile)};
      append(tile);
      append(offset);
      append(sizes[tile]);
      if (baked)
        if (auto found = baked->tiles.find(tile);
            found != baked->tiles.end() && found->second.pixels)
          parameters += found->second.pixels->hash;
      different_target |=
          target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target), offset}] != key;
      if (auto found = margin_plans.find(set.material_session_uid); found != margin_plans.end()) {
        append(reinterpret_cast<std::uintptr_t>(found->second.extend.at(tile).get()));
        append(reinterpret_cast<std::uintptr_t>(found->second.adjacent.at(tile).get()));
      }
    }
    const bool full = different_target || parameters != entry.tracking.parameters;
    update.tracking.parameters = std::move(parameters);
    for (const auto &[tile, size] : sizes) {
      const int source = channel == 3 ? 6 : 5;
      auto &stack = stacks.at(set.id.toString().toStdString() + ":" + std::to_string(source) +
                              ":" + std::to_string(tile));
      auto dirty = update.tracking.changed(
          source, tile, stack.revision, stack.last_changes, size[0], size[1], full);
      update.dirty[tile].insert(dirty.begin(), dirty.end());
      if (channel == 3 && set.channels[4]) {
        auto &height = stacks.at(set.id.toString().toStdString() + ":4:" + std::to_string(tile));
        auto height_dirty = update.tracking.changed(
            4, tile, height.revision, height.last_changes, size[0], size[1], full);
        const GpuMarginPlan *plan = nullptr;
        if (auto found = margin_plans.find(set.material_session_uid); found != margin_plans.end())
          plan = (set.padding_3d ? found->second.adjacent : found->second.extend).at(tile).get();
        GpuDerivedDirty::normal_halo(
            update.dirty, tile, GpuDerivedDirty::padded(std::move(height_dirty), plan), sizes);
      }
    }
    for (const auto &[tile, size] : sizes) {
      auto &flags = update.flags[tile];
      flags.resize(((size[0] + 255) / 256) * ((size[1] + 255) / 256));
      for (int chunk : update.dirty[tile])
        flags[chunk] = 1;
      update.needed |= !update.dirty[tile].empty();
    }
    return update;
  }
  bool prepare_derived_masks(DerivedEntry &entry,
                             const DerivedUpdate &update,
                             const TextureSet &set)
  {
    if (!entry.presence) {
      entry.presence = texture(
          "BTS cached derived presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!entry.presence)
        return false;
      const float zero[4] = {};
      GPU_texture_clear(entry.presence.get(), GPU_DATA_FLOAT, zero);
    }
    for (const auto &[tile, flags] : update.flags) {
      const int w = (set.width(tile) + 255) / 256, h = (set.rows(tile) + 255) / 256;
      auto &image = entry.dirty_images.try_emplace(tile, nullptr, GPU_texture_free).first->second;
      if (!image || GPU_texture_width(image.get()) != w || GPU_texture_height(image.get()) != h)
        image = texture("BTS derived dirty chunks", w, h, gpu::TextureFormat::UINT_32);
      if (!image)
        return false;
      GPU_texture_update(image.get(), GPU_DATA_UINT, flags.data());
    }
    return true;
  }
  void commit_derived(const TextureSet &set,
                      int channel,
                      gpu::Texture *target,
                      const std::map<int, std::array<int, 3>> &regions,
                      DerivedUpdate update)
  {
    const auto key = derived_key(set, channel);
    derived[key].tracking = std::move(update.tracking);
    for (const auto &[tile, offset] : regions)
      target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target), offset}] = key;
    image_changed = true;
    std::size_t dirty_chunks = 0;
    for (const auto &[tile, dirty] : update.dirty)
      dirty_chunks += dirty.size();
    last_derived_chunks = dirty_chunks;
    last_derived_total = 0;
    for (const auto &[tile, flags] : update.flags)
      last_derived_total += flags.size();
    std::printf("BTS_GPU_DERIVED_DIRTY channel=%d chunks=%zu tiles=%zu global_presence_on_gpu=1\n",
                channel,
                dirty_chunks,
                regions.size());
  }
  struct AdjustmentEntry {
    std::weak_ptr<const AdjustmentPaintSnapshot> source;
    TextureSet set;
    QUuid layer;
    std::vector<std::string> native_aliases;
    std::map<std::tuple<QUuid, int, int>, NativeMaterialGpuContext> contexts;
    std::string failure;
  };
  std::map<QUuid, AdjustmentEntry> adjustment_sources;
  GpuAdjustmentPages adjustment_pages;
  Texture export_final{nullptr, GPU_texture_free};
  Texture world_weight{nullptr, GPU_texture_free};
  Shader world_weight_shader{nullptr, GPU_shader_free};
  Shader world_apply_shader{nullptr, GPU_shader_free};
  std::array<Shader, 2> world_publish_shaders{Shader{nullptr, GPU_shader_free},
                                              Shader{nullptr, GPU_shader_free}};
  std::map<std::tuple<QUuid, QUuid, int>, std::shared_ptr<gpu::Texture>> native_normals;
  std::map<QUuid, std::string> native_normal_errors;
  std::uint64_t native_normal_revision = 0;
  std::string native_image_failure;
  NativeMaterialGpuLibrary native_materials;
  NativeMaterialPreparationQueue native_preparations{native_materials};
  NativeContextProvider native_context_provider;
  Texture native_source{nullptr, GPU_texture_free};
  std::uint64_t last_dispatch_texels = 0, last_cached_nodes = 0, last_active_nodes = 0;
  std::uint64_t frozen_prefix_copies = 0, frozen_prefix_copy_texels = 0;

  std::map<gpu::TextureFormat, Shader> uniform_shaders;
  bool uniform_failed = false;
  Texture scratch[2]{{nullptr, GPU_texture_free}, {nullptr, GPU_texture_free}},
      final_image{nullptr, GPU_texture_free}, zero{nullptr, GPU_texture_free};
  Shader coverage_shader{nullptr, GPU_shader_free}, presence_shader{nullptr, GPU_shader_free};
  Texture ao_presence{nullptr, GPU_texture_free};
  std::map<std::pair<gpu::TextureFormat, bool>, Shader> ao_shaders, normal_shaders;
  struct NeighborEntry {
    Texture image{nullptr, GPU_texture_free};
    std::array<std::array<float, 4>, 9> values{};
  };
  std::map<std::string, NeighborEntry> normal_neighbors;
  Shader layer_shader{nullptr, GPU_shader_free}, mask_shader{nullptr, GPU_shader_free},
      clear_shader{nullptr, GPU_shader_free};
  std::map<std::pair<gpu::TextureFormat, bool>, Shader> final_shaders, mask_output_shaders;
  gpu::Shader *final_shader = nullptr;
  int width = 0, height = 0;
  bool final_array = false;
  gpu::TextureFormat final_format = gpu::TextureFormat::SFLOAT_16_16_16_16;
  performance::Clock::time_point upload_start{};
  bool upload_time_available() const
  {
    return testing_stack || performance::ms(upload_start) < 4;
  }
  bool authoring_time_available() const
  {
    return testing_stack || performance::ms(upload_start) < 8;
  }
  std::uint64_t reported_authoring_pages = 0;
  std::set<std::string> adjustment_input_pins;
  std::uint64_t remaining = upload_budget, remaining_work = 32ULL * 1024 * 1024, used_bytes = 0,
                clock = 0, resource_allocations = 0;
  bool failed = false, input_rejected = false;
  using PreparedProgram =
      std::unique_ptr<GPUHostedShaderCompilation, decltype(&GPU_shader_hosted_compilation_free)>;
  PreparedProgram pending_layer{nullptr, GPU_shader_hosted_compilation_free},
      pending_mask{nullptr, GPU_shader_hosted_compilation_free};
  GpuProgramWarmup program_state;
  bool startup_prepared = false;
  struct DerivedProgram {
    PreparedProgram pending{nullptr, GPU_shader_hosted_compilation_free};
  };
  std::array<DerivedProgram, 10> derived_programs;
  GpuProgramWarmup derived_program_state{10};
  bool derived_programs_started = false;
  static std::pair<gpu::TextureFormat, bool> derived_key(int index)
  {
    return {index < 6 ? gpu::TextureFormat::SFLOAT_16_16_16_16 :
                        gpu::TextureFormat::SFLOAT_32_32_32_32,
            ((index - 2) / 2) % 2 != 0};
  }
  Shader &derived_output(int index)
  {
    if (index == 0)
      return coverage_shader;
    if (index == 1)
      return presence_shader;
    return (index % 2 ? normal_shaders : ao_shaders).at(derived_key(index));
  }
  bool poll_derived_program(int index)
  {
    auto &pending = derived_programs[index].pending;
    if (!pending || !GPU_shader_hosted_compilation_ready(pending.get()))
      return true;
    Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()), GPU_shader_free};
    pending.reset();
    const bool okay = bool(completed);
    if (okay)
      derived_output(index) = std::move(completed);
    return okay;
  }
  bool programs_pending() const
  {
    return authoring_pages.programs_pending() || bool(pending_layer) || bool(pending_mask) ||
           std::any_of(derived_programs.begin(), derived_programs.end(), [](const auto &program) {
             return bool(program.pending);
           });
  }
  bool has_gpu() const
  {
    return adjustment_pages.has_gpu() || authoring_pages.programs_started() ||
           programs_pending() || native_materials.statistics().resources ||
           !uniform_shaders.empty() || bool(layer_shader) || bool(mask_shader) ||
           bool(coverage_shader) || bool(presence_shader) ||
           std::any_of(ao_shaders.begin(),
                       ao_shaders.end(),
                       [](const auto &entry) { return bool(entry.second); }) ||
           std::any_of(normal_shaders.begin(),
                       normal_shaders.end(),
                       [](const auto &entry) { return bool(entry.second); }) ||
           bool(scratch[0]) || !inputs.empty() || !stacks.empty();
  }
  bool poll_program(int index)
  {
    auto &pending = index == 0 ? pending_layer : pending_mask;
    auto &result = index == 0 ? layer_shader : mask_shader;
    if (!pending || !GPU_shader_hosted_compilation_ready(pending.get()))
      return true;
    Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()), GPU_shader_free};
    pending.reset();  // Handle consumed before freeing its prepared sources.
    const bool okay = bool(completed);
    if (okay)
      result = std::move(completed);
    return okay;
  }
  void poll_programs()
  {
    program_state.drain([&](int index) { return poll_program(index); });
    if (derived_programs_started)
      derived_program_state.drain([&](int index) { return poll_derived_program(index); });
    authoring_pages.poll_programs();
  }
  void cancel_programs()
  {
    pending_layer.reset();
    pending_mask.reset();
    for (auto &program : derived_programs)
      program.pending.reset();
    authoring_pages.cancel_programs();
  }  // Terminal owner cleanup only.
  bool use_program_workers() const
  {
    const auto *opt_in = std::getenv("BTS_HOSTED_SHADER_WORKERS");
    // Fixed-frame diagnostic self-tests retain their existing synchronous contract.
    return opt_in && std::string_view(opt_in) == "1" && !testing_stack &&
           !GPU_use_main_context_workaround();
  }
  GpuPreviewUpdate prepare_programs()
  {
    // Poll first even when resource/compile admission has already failed.
    poll_programs();
    if (failed && !programs_pending())
      return GpuPreviewUpdate::Unsupported;
    const bool asynchronous = use_program_workers();
    const auto state = program_state.prepare(
        [&](int index) {
          const auto format = index == 0 ? gpu::TextureFormat::SFLOAT_32_32_32_32 :
                                           gpu::TextureFormat::SFLOAT_32;
          if (asynchronous) {
            auto &pending = index == 0 ? pending_layer : pending_mask;
            auto info = layer_info(format);
            gpu_derived::stabilize_hosted_resource_owners(info);
            pending.reset(
                GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
            if (!pending)
              throw std::runtime_error("Hosted GPU stack worker submission unavailable");
            std::printf(
                "BTS_GPU_STACK_PROGRAM_SUBMITTED output=%s route=THREAD python_preprocessed=1\n",
                index == 0 ? "RGBA" : "mask");
          }
          else {
            auto &result = index == 0 ? layer_shader : mask_shader;
            result = compile_layer(format);
          }
        },
        [&](int index) { return poll_program(index); },
        [&](int index) { return bool(index == 0 ? layer_shader : mask_shader); },
        [&](int index) { return bool(index == 0 ? pending_layer : pending_mask); });
    // Submit the ten measured derived programs alongside the layer/mask pair.
    // All prepared CreateInfos are owned by their existing compiler handles;
    // readiness and ordinary retirement never wait or cancel running workers.
    auto derived_state = GpuProgramWarmup::State::Ready;
    if (asynchronous || derived_programs_started) {
      if (!derived_programs_started) {
        for (int index = 2; index < 10; index += 2) {
          ao_shaders.try_emplace(derived_key(index), nullptr, GPU_shader_free);
          normal_shaders.try_emplace(derived_key(index), nullptr, GPU_shader_free);
        }
        derived_programs_started = true;
      }
      derived_state = derived_program_state.prepare(
          [&](int index) {
            auto info = [&] {
              if (index == 0)
                return gpu_derived::coverage_info();
              if (index == 1)
                return gpu_derived::presence_info();
              const auto [format, array] = derived_key(index);
              return index % 2 ? gpu_derived::normal_info(format, array, quant_source) :
                                 gpu_derived::ao_info(format, array, quant_source);
            }();
            auto &pending = derived_programs[index].pending;
            gpu_derived::stabilize_hosted_resource_owners(info);
            pending.reset(
                GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
            if (!pending)
              throw std::runtime_error("Hosted derived shader submission unavailable at slot " +
                                       std::to_string(index));
            std::printf(
                "BTS_GPU_DERIVED_PROGRAM_SUBMITTED slot=%d route=THREAD python_preprocessed=1\n",
                index);
          },
          [&](int index) { return poll_derived_program(index); },
          [&](int index) { return bool(derived_output(index)); },
          [&](int index) { return bool(derived_programs[index].pending); });
    }
    auto authoring_state = GpuProgramWarmup::State::Ready;
    if (asynchronous || authoring_pages.programs_started())
      authoring_state = authoring_pages.prepare_worker_programs();
    if (state == GpuProgramWarmup::State::Pending ||
        derived_state == GpuProgramWarmup::State::Pending ||
        authoring_state == GpuProgramWarmup::State::Pending)
      return GpuPreviewUpdate::Pending;
    if (state == GpuProgramWarmup::State::Failed ||
        derived_state == GpuProgramWarmup::State::Failed ||
        authoring_state == GpuProgramWarmup::State::Failed)
    {
      failed = true;
      return GpuPreviewUpdate::Unsupported;
    }
    return GpuPreviewUpdate::Updated;
  }
  GpuPreviewUpdate initialize(int w, int h, gpu::TextureFormat format, bool array)
  {
    performance::Scope timing("gpu_shader_prepare");
    if (const auto state = prepare_programs(); state != GpuPreviewUpdate::Updated)
      return state;
    if (!zero) {
      zero = texture("BTS preview zero", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!zero) {
        failed = true;
        return GpuPreviewUpdate::Unsupported;
      }
      const float empty[4] = {};
      GPU_texture_clear(zero.get(), GPU_DATA_FLOAT, empty);
      GPU_texture_filter_mode(zero.get(), true);
    }
    width = w;
    height = h;
    const auto key = std::pair{format, array};
    if (!final_shaders.contains(key))
      final_shaders.emplace(key, compile_final(format, array));
    final_shader = final_shaders.at(key).get();
    if (!final_shader) {
      failed = true;
      return GpuPreviewUpdate::Unsupported;
    }
    return GpuPreviewUpdate::Updated;
  }
  Entry *entry(const std::string &key,
               int w,
               int h,
               bool resource,
               const std::set<std::string> &protected_keys)
  {
    performance::Scope timing("gpu_source_allocate_or_find");
    if (auto found = inputs.find(key); found != inputs.end()) {
      if (found->second.width == w && found->second.height == h &&
          found->second.resource == resource && !found->second.rejected)
      {
        found->second.used = clock;
        return &found->second;
      }
      if (found->second.retained())
        throw std::logic_error("Retained GPU source identity changed");
      used_bytes -= found->second.bytes();
      inputs.erase(found);
    }
    const auto bytes = std::uint64_t(w) * h * 16;
    const auto cache_budget = input_budget.update(
        texture_vram_query_generation.load(),
        used_bytes,
        texture_free_vram.load(),
        texture_vram_query_valid.load() || texture_free_vram.load() != 0,
        texture_vram_query_valid.load() ? 0 : texture_available_ram_bytes());
    while (used_bytes + bytes > cache_budget) {
      auto victim = inputs.end();
      for (auto i = inputs.begin(); i != inputs.end(); ++i)
        if (!i->second.retained() && !protected_keys.contains(i->first) &&
            !adjustment_input_pins.contains(i->first) &&
            (victim == inputs.end() || i->second.used < victim->second.used))
          victim = i;
      if (victim == inputs.end()) {
        break;
      }
      used_bytes -= victim->second.bytes();
      inputs.erase(victim);
    }
    auto [found, inserted] = inputs.try_emplace(key);
    auto &e = found->second;
    e.width = w;
    e.height = h;
    e.resource = resource;
    e.used = clock;
    if (resource)
      e.residency = std::make_shared<const char>(0);
    e.image = texture("BTS cached preview source", w, h, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!e.image) {
      texture_memory_refresh_needed = true;
      e.rejected = true;
      input_rejected = true;
      return &e;
    }
    used_bytes += bytes;
    if (resource) {
      ++resource_allocations;
      std::printf("BTS_GPU_RESOURCE_ALLOC key=%s bytes=%llu count=%llu\n",
                  key.c_str(),
                  (unsigned long long)bytes,
                  (unsigned long long)resource_allocations);
    }
    if (!resource) {
      performance::Scope timing("gpu_paint_clear");
      if (!clear_shader)
        clear_shader = compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!clear_shader) {
        e.rejected = input_rejected = true;
        return &e;
      }
      const float empty[4] = {};
      GPU_shader_bind(clear_shader.get());
      GPU_shader_uniform_4fv(clear_shader.get(), "fill_value", empty);
      GPU_texture_image_bind(e.image.get(), 0);
      GPU_compute_dispatch(clear_shader.get(), (w + 15) / 16, (h + 15) / 16, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
      GPU_texture_image_unbind(e.image.get());
      GPU_shader_unbind();
    }
    GPU_texture_filter_mode(e.image.get(), true);
    GPU_texture_extend_mode(
        e.image.get(), resource ? GPU_SAMPLER_EXTEND_MODE_REPEAT : GPU_SAMPLER_EXTEND_MODE_EXTEND);
    return &e;
  }
  gpu::Texture *paint(const std::string &key,
                      const PaintPixels &pixels,
                      int channel,
                      const std::set<std::string> &protected_keys,
                      int polygon_depth = 0)
  {
    performance::Scope timing("gpu_paint_source_upload");
    PaintMetadataScope metadata;
    if (!pixels.page_count(channel))
      return zero.get();
    auto *e = entry(key, pixels.resolution(), pixels.resolution(), false, protected_keys);
    if (!e || e->rejected) {
      input_rejected = true;
      return nullptr;
    }
    e->plane = channel;
    const auto versions = pixels.page_versions();
    const auto &wanted_versions = versions[channel];
    if (e->pages == wanted_versions)
      return e->image.get();
    auto &polygon = authoring_pages.polygon_raster();
    for (auto &[revision, patch] : e->polygon_undo)
      if (patch.after == e->pages && patch.before == wanted_versions) {
        performance::Scope timing("gpu_polygon_undo");
        polygon.restore(e->image.get(), patch.pixels);
        e->pages = wanted_versions;
        patch.used = ++clock;
        std::printf("BTS_GPU_POLYGON_UNDO pages=%d bytes=%llu cpu_raster=0\n",
                    patch.pixels.count,
                    (unsigned long long)patch.pixels.bytes);
        return e->image.get();
      }
    const auto &root = pixels.recipe();
    const auto ink = root ? root->ink.recipe() : nullptr;
    if (polygon_depth < 32 && root && root->kind == PaintRecipe::Kind::Over &&
        root->complete_stroke && !root->erase && ink && ink->kind == PaintRecipe::Kind::Stamp &&
        ink->base.page_versions()[channel].empty() &&
        (root->before.resolution() == 0 || root->before.resolution() == pixels.resolution()) &&
        GpuPolygonRaster::supported(ink->stamp))
    {
      const auto before_versions = root->before.page_versions()[channel];
      if (!before_versions.empty() && e->pages != before_versions) {
        auto protect = protected_keys;
        protect.insert(key);
        if (!paint(key, root->before, channel, protect, polygon_depth + 1))
          return nullptr;
      }
      if (!authoring_time_available())
        return nullptr;
      if (before_versions.empty() && !e->pages.empty()) {
        std::vector<std::uint32_t> stale;
        for (const auto &[page, version] : e->pages)
          stale.push_back(page);
        polygon.clear(e->image.get(), stale);
        e->pages.clear();
      }
      if (e->pages == before_versions) {
        const auto changed = GpuPolygonRaster::changed_pages(before_versions, wanted_versions);
        if (!before_versions.empty() && !changed.empty() &&
            !e->polygon_undo.contains(root->revision))
        {
          const auto columns = std::uint64_t(std::ceil(std::sqrt(double(changed.size()))));
          const auto bytes = columns * 64 * ((changed.size() + columns - 1) / columns) * 64 * 16 +
                             changed.size() * 4;
          const auto budget = source_cache_budget(used_bytes);
          while (used_bytes + bytes > budget) {
            Entry *victim = nullptr;
            std::uint64_t id = 0, oldest = ~std::uint64_t(0);
            for (auto &[name, entry] : inputs)
              for (auto &[rev, patch] : entry.polygon_undo)
                if (patch.used < oldest) {
                  victim = &entry;
                  id = rev;
                  oldest = patch.used;
                }
            if (!victim)
              break;
            const auto retired = victim->polygon_undo.at(id).pixels.bytes;
            victim->patch_bytes -= retired;
            used_bytes -= retired;
            victim->polygon_undo.erase(id);
          }
          if (used_bytes + bytes <= budget) {
            PolygonUndo patch;
            patch.pixels = polygon.capture(e->image.get(), changed);
            patch.before = before_versions;
            patch.after = wanted_versions;
            patch.used = ++clock;
            e->patch_bytes += patch.pixels.bytes;
            used_bytes += patch.pixels.bytes;
            e->polygon_undo.emplace(root->revision, std::move(patch));
          }
          else
            std::fprintf(
                stderr,
                "BTS_GPU_POLYGON_UNDO_CACHE skipped_bytes=%llu available_budget=%llu; reproducible GPU recipes retained\n",
                (unsigned long long)bytes,
                (unsigned long long)budget);
        }
        polygon.paint(
            e->image.get(), ink->stamp, channel, pixels.resolution(), changed, root->opacity);
        e->pages = wanted_versions;
        std::printf(
            "BTS_GPU_POLYGON_RASTER channel=%d pages=%zu triangles=%zu cpu_raster=0 full_image_copy=0\n",
            channel,
            changed.size(),
            ink->stamp.surface->triangles.size());
        return e->image.get();
      }
    }
    bool complete = true;
    std::set<std::uint32_t> wanted;
    if (pixels.recipe()) {
      const auto versions = pixels.page_versions();
      for (const auto &[page, version] : versions[channel]) {
        wanted.insert(page);
        if (e->pages.contains(page) && e->pages.at(page) == version)
          continue;
        if (!authoring_time_available()) {
          complete = false;
          continue;
        }
        if (!authoring_pages.write(e->image.get(),
                                   pixels,
                                   channel,
                                   page,
                                   testing_stack ? performance::Clock::time_point::max() :
                                                   upload_start + std::chrono::milliseconds(8)))
        {
          complete = false;
          continue;
        }
        e->pages[page] = version;
      }
    }
    else
      pixels.visit_versioned_pages(
          channel, [&](auto page, auto version, std::span<const PaintPixels::Pixel> data) {
            wanted.insert(page);
            if (e->pages.contains(page) && e->pages.at(page) == version)
              return;
            const auto bytes = data.size_bytes();
            if (bytes > remaining) {
              complete = false;
              return;
            }
            const int x = int(page % 65536) * 64, y = int(page / 65536) * 64;
            GPU_texture_update_sub(
                e->image.get(), GPU_DATA_FLOAT, data.data(), x, y, 0, 64, 64, 1);
            remaining -= bytes;
            e->pages[page] = version;
          });
    static const std::array<PaintPixels::Pixel, 64 * 64> empty{};
    for (auto i = e->pages.begin(); i != e->pages.end();) {
      if (wanted.contains(i->first)) {
        ++i;
        continue;
      }
      if (sizeof(empty) > remaining) {
        complete = false;
        ++i;
        continue;
      }
      GPU_texture_update_sub(e->image.get(),
                             GPU_DATA_FLOAT,
                             empty.data(),
                             int(i->first % 65536) * 64,
                             int(i->first / 65536) * 64,
                             0,
                             64,
                             64,
                             1);
      remaining -= sizeof(empty);
      i = e->pages.erase(i);
    }
    return complete ? e->image.get() : nullptr;
  }
  gpu::Texture *resource(const FillTexture &source, const std::set<std::string> &protected_keys)
  {
    performance::Scope timing("gpu_resource_source_upload");
    auto *e = entry(resource_key(source), source.width, source.height, true, protected_keys);
    if (!e || e->rejected || !source.pixels) {
      input_rejected = true;
      return nullptr;
    }
    while (e->row < e->height) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min(
          {64, e->height - e->row, int(remaining / (std::uint64_t(e->width) * 16))});
      if (!rows)
        return nullptr;
      // Decode workers already validate every component. Upload the immutable
      // shared storage directly, with no copy/finite scan/premultiply on the UI.
      const auto *values = source.pixels->data() + std::size_t(e->row) * e->width;
      GPU_texture_update_sub(
          e->image.get(), GPU_DATA_FLOAT, values, 0, e->row, 0, e->width, rows, 1);
      remaining -= std::uint64_t(e->width) * rows * sizeof(FillTexture::Pixel);
      e->row += rows;
    }
    return e->image.get();
  }
  gpu::Texture *baked(const MeshMapPlane *plane, const std::set<std::string> &protected_keys)
  {
    if (!plane || !plane->pixels)
      return zero.get();
    const auto &pixels = *plane->pixels;
    const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) + ":" +
                     std::to_string(pixels.height);
    auto *e = entry(key, pixels.resolution, pixels.height, true, protected_keys);
    if (!e || e->rejected)
      return nullptr;
    GPU_texture_extend_mode(e->image.get(), GPU_SAMPLER_EXTEND_MODE_EXTEND);
    while (e->row < e->height) {
      if (!upload_time_available())
        return nullptr;
      const int rows = std::min(
          {64, e->height - e->row, int(remaining / (std::uint64_t(e->width) * 16))});
      if (!rows)
        return nullptr;
      GPU_texture_update_sub(e->image.get(),
                             GPU_DATA_FLOAT,
                             pixels.rgba.data() + std::size_t(e->row) * e->width * 4,
                             0,
                             e->row,
                             0,
                             e->width,
                             rows,
                             1);
      remaining -= std::uint64_t(e->width) * rows * 16;
      e->row += rows;
    }
    return e->image.get();
  }

  gpu::Texture *mask_prefix(const std::string &key,
                            const std::vector<GpuMaskStage> &stages,
                            const std::array<gpu::Texture *, 6> &maps,
                            int tile,
                            int w,
                            int h,
                            const std::set<std::string> &protected_keys,
                            const std::string &map_signature,
                            const GpuComparisonResolver &resolve,
                            const GpuAnchorResolver &resolve_anchor,
                            bool partial = false);
};
gpu::Texture *GpuLayerPreview::Impl::mask_prefix(const std::string &key,
                                                 const std::vector<GpuMaskStage> &stages,
                                                 const std::array<gpu::Texture *, 6> &maps,
                                                 int tile,
                                                 int w,
                                                 int h,
                                                 const std::set<std::string> &protected_keys,
                                                 const std::string &map_signature,
                                                 const GpuComparisonResolver &resolve,
                                                 const GpuAnchorResolver &resolve_anchor,
                                                 bool partial)
{
  performance::Scope timing("gpu_mask_prefix");
  for (const auto &stage : stages)
    if (stage.kind == GpuMaskStage::SpatialFill && stage.surface_required &&
        (!stage.geometry || !stage.geometry->tiles.contains(tile)))
      return nullptr;
  auto &cache = mask_stacks[key];
  if (cache.width != w || cache.height != h) {
    cache.output_dirty.clear();
    cache.node_versions.clear();
    cache.width = w;
    cache.height = h;
  }
  std::vector<GpuStackNode> nodes;
  const auto append = [](std::string &bytes, const auto &value) {
    bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  for (const auto &stage : stages) {
    bool comparison_ready = true;
    GpuStackNode node;
    node.id = stage.id;
    if (!nodes.empty())
      node.dependencies = {nodes.back().id};
    int slot = 0;
    gpu_visit_mask_stage(stage, [&](const auto &part) {
      node.parameters += part.owner_key + part.id + part.anchor_key + part.anchor_id;
      if (part.bitmap)
        node.parameters += part.bitmap->asset + part.bitmap->hash;
      append(node.parameters, part.kind);
      append(node.parameters, part.background);
      append(node.parameters, part.opacity);
      append(node.parameters, part.blend);
      for (const auto &row : part.rows)
        append(node.parameters, row);
      if (part.spatial) {
        append(node.parameters, part.spatial->values);
        append(node.parameters, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
      }
      if (part.kind == GpuMaskStage::Points)
        node.parameters += map_signature;
      if (part.generator) {
        auto values = gpu_generator_inputs(*part.generator, part.comparison_layer, resolve_anchor);
        if (!values)
          comparison_ready = false;
        else
          gpu_generator_dependencies(node, *part.generator, *values, slot);
      }
      if (part.kind == GpuMaskStage::Compare) {
        auto values = resolve(part);
        if (!values)
          comparison_ready = false;
        else
          gpu_comparison_dependencies(node, *values, slot);
      }
      if (part.kind == GpuMaskStage::AnchorFill) {
        append(node.parameters, part.reference.channel);
        append(node.parameters, part.reference.component);
        append(node.parameters, part.reference.coverage);
        append(node.parameters, part.content_anchor);
        if (part.content_anchor) {
          auto value = resolve_anchor(part.comparison_layer, part.reference);
          if (!value)
            comparison_ready = false;
          else
            gpu_anchor_dependencies(node, *value, slot);
        }
      }
      if (part.pixels) {
        const int index = slot++;
        const auto &pixels = part.pixels->tile(tile);
        const int resolution = pixels.resolution();
        append(node.parameters, resolution);
        if (resolution) {
          const double sx = double(w) / resolution, sy = double(h) / resolution;
          const auto versions = pixels.page_versions();
          for (const auto &[page, revision] : versions[2]) {
            const int px = int(page % 65536) * 64, py = int(page / 65536) * 64;
            const int x = int(std::floor(px * sx)) - 1, y = int(std::floor(py * sy)) - 1,
                      right = int(std::ceil((px + 64) * sx)) + 1,
                      bottom = int(std::ceil((py + 64) * sy)) + 1;
            node.pages[(std::uint64_t(index) << 32) | page] = {
                x, y, right - x, bottom - y, revision};
          }
        }
      }
    });
    if (!comparison_ready)
      return nullptr;
    nodes.push_back(std::move(node));
  }
  auto transaction = cache.plan.prepare(w, h, nodes);
  std::set<std::string> live;
  for (const auto &node : nodes)
    live.insert(node.id);
  if (!partial)
    std::erase_if(cache.outputs, [&](const auto &entry) { return !live.contains(entry.first); });
  struct Input {
    gpu::Texture *source = nullptr, *comparison = nullptr, *effects = nullptr,
                 *parameters = nullptr, *surface = nullptr;
    std::array<gpu::Texture *, 6> maps{};
    bool anchor_valid = false, anchor_mask = true;
  };
  std::vector<Input> resolved(stages.size());
  for (std::size_t i = 0; i < stages.size(); ++i) {
    if (transaction.work[i].regions.empty())
      continue;
    const auto &stage = stages[i];
    const auto identity = key + ":" + stage.id;
    auto &in = resolved[i];
    in.source = stage.bitmap ? resource(*stage.bitmap, protected_keys) :
                stage.pixels ? paint(identity, stage.pixels->tile(tile), 2, protected_keys) :
                               zero.get();
    if (!in.source)
      return nullptr;
    if (stage.anchor_stages) {
      if (!mask_prefix(stage.anchor_key,
                       *stage.anchor_stages,
                       maps,
                       tile,
                       w,
                       h,
                       protected_keys,
                       map_signature,
                       resolve,
                       resolve_anchor,
                       true))
        return nullptr;
      in.source = mask_stacks.at(stage.anchor_key).outputs.at(stage.anchor_id).get();
      in.anchor_valid = true;
    }
    if (stage.content_anchor) {
      auto value = resolve_anchor(stage.comparison_layer, stage.reference);
      if (!value)
        return nullptr;
      in.source = value->image;
      in.anchor_valid = value->enabled;
      in.anchor_mask = value->mask;
    }
    in.maps = maps;
    auto rows = stage.rows;
    if (stage.generator) {
      auto values = gpu_generator_inputs(*stage.generator, stage.comparison_layer, resolve_anchor);
      if (!values)
        return nullptr;
      gpu_generator_options(rows.front(), *stage.generator, *values);
      gpu_generator_maps(in.maps, *stage.generator, *values);
    }
    in.effects = effects(identity, rows);
    if (!in.effects)
      return nullptr;
    in.surface = surface(stage.geometry, tile);
    if (!in.surface)
      return nullptr;
    GpuLayerParameters params = stage.spatial.value_or(GpuLayerParameters{});
    const float anchor_options[4] = {float(stage.reference.component),
                                     float(stage.reference.coverage),
                                     float(in.anchor_mask),
                                     float(in.anchor_valid)};
    params.set4("anchor_options", anchor_options);
    params.set("mask_pass", stage.kind);
    params.set("mask_background", stage.background);
    params.set("opacity_value", stage.opacity);
    params.set("blend_mode", int(stage.blend));
    params.set("effect_count", stage.rows.size());
    in.comparison = zero.get();
    if (stage.kind == GpuMaskStage::Compare) {
      auto values = resolve(stage);
      if (!values)
        return nullptr;
      in.source = (*values)[0].image;
      in.comparison = (*values)[1].image;
      params.set("compare_channel", stage.comparison_channel);
      const int enabled[2] = {(*values)[0].enabled, (*values)[1].enabled};
      params.set2("compare_enabled", enabled);
      params.set3("compare_base", (*values)[0].base.data());
    }
    in.parameters = parameters(identity, params);
    if (!in.parameters) {
      input_rejected = true;
      return nullptr;
    }
  }
  auto *shader = mask_shader.get();
  GPU_shader_bind(shader);
  for (std::size_t i = 0; i < stages.size(); ++i) {
    const auto &work = transaction.work[i];
    if (work.regions.empty())
      continue;
    auto found = cache.outputs.find(work.id);
    if (found == cache.outputs.end() || GPU_texture_width(found->second.get()) != w ||
        GPU_texture_height(found->second.get()) != h)
    {
      auto output = texture("BTS shared GPU mask prefix", w, h, gpu::TextureFormat::SFLOAT_32);
      if (!output) {
        input_rejected = true;
        GPU_shader_unbind();
        return nullptr;
      }
      cache.outputs.insert_or_assign(work.id, std::move(output));
    }
    auto *output = cache.outputs.at(work.id).get();
    const auto &in = resolved[i];
    GPU_texture_bind(in.source, 0);
    GPU_texture_bind(in.comparison, 1);
    GPU_texture_bind(i ? cache.outputs.at(stages[i - 1].id).get() : zero.get(), 2);
    GPU_texture_bind(in.surface, 3);
    GPU_texture_bind(in.effects, 4);
    GPU_texture_bind(in.parameters, 5);
    for (int m = 0; m < 6; ++m)
      GPU_texture_bind(in.maps[m], gpu_mask_map_slot(m));
    GPU_texture_bind(geometry_masks.empty(), 9);
    GPU_texture_bind(geometry_masks.empty(), 10);
    GPU_texture_image_bind(output, 0);
    for (const auto &r : work.regions) {
      const int origin[2] = {r.x, r.y}, size[2] = {r.width, r.height};
      GPU_shader_uniform_2iv(shader, "region_origin", origin);
      GPU_shader_uniform_2iv(shader, "region_size", size);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
    GPU_texture_image_unbind(output);
  }
  GPU_shader_unbind();
  for (const auto &work : transaction.work)
    if (!work.regions.empty()) {
      const auto version = ++cache.node_clock;
      for (const auto &r : work.regions) {
        const auto chunk = std::uint64_t(r.y / 256) * ((w + 255) / 256) + r.x / 256;
        cache.node_versions[work.id][chunk] = {r.x, r.y, r.width, r.height, version};
      }
    }
  if (partial) {
    mask_dispatch_texels += transaction.texels;
    cache.plan.commit(std::move(transaction), true);
    return cache.outputs.at(stages.back().id).get();
  }
  if (cache.head_id != stages.back().id) {
    for (int y = 0; y < h; y += 256)
      for (int x = 0; x < w; x += 256)
        cache.output_dirty.insert(y / 256 * ((w + 255) / 256) + x / 256);
    cache.head_id = stages.back().id;
  }
  for (const auto &region : transaction.work.back().regions)
    cache.output_dirty.insert(region.y / 256 * ((w + 255) / 256) + region.x / 256);
  mask_dispatch_texels += transaction.texels;
  cache.plan.commit(std::move(transaction));
  cache.head = cache.outputs.at(stages.back().id).get();
  return cache.head;
}

// Main-thread retirement; disposal happens only on the active GPU owner.
std::vector<std::unique_ptr<GpuLayerPreview::Impl>> &GpuLayerPreview::retired()
{
  static std::vector<std::unique_ptr<Impl>> values;
  return values;
}
GpuLayerPreview::GpuLayerPreview() : impl_(std::make_unique<Impl>())
{
  bind_adjustment_resolver();
}
void GpuLayerPreview::bind_adjustment_resolver()
{
  impl_->authoring_pages.adjustment_resolver([this](const SmudgeCapture &capture,
                                                    int plane,
                                                    int tile,
                                                    std::uint32_t page,
                                                    gpu::Texture *own,
                                                    auto deadline) {
    return adjustment_page(capture, plane, tile, page, own, deadline);
  });
}
GpuLayerPreview::~GpuLayerPreview()
{
  if (impl_ && impl_->has_gpu())
    retired().push_back(std::move(impl_));
}
std::uint64_t GpuLayerPreview::release_bake_scratch()
{
  auto &s = *impl_;
  std::uint64_t bytes = 0;
  for (const auto *cache : {&s.stacks, &s.mask_stacks})
    for (const auto &[key, stack] : *cache)
      for (const auto &[id, image] : stack.outputs)
        if (image.use_count() == 1)
          bytes = resource_sum(
              bytes,
              resource_product(resource_product(GPU_texture_width(image.get()),
                                                GPU_texture_height(image.get())),
                               4 * GPU_texture_component_len(GPU_texture_format(image.get()))));
  s.comparison_memo.clear();
  s.anchor_memo.clear();
  s.stacks.clear();
  s.mask_stacks.clear();
  for (auto i = s.inputs.begin(); i != s.inputs.end();)
    if (!i->second.retained() && !s.adjustment_input_pins.contains(i->first)) {
      bytes = resource_sum(bytes, i->second.bytes());
      s.used_bytes -= i->second.bytes();
      i = s.inputs.erase(i);
    }
    else
      ++i;
  texture_memory_refresh_needed = true;
  std::printf("BTS_BAKE_RELEASE reusable_bytes=%llu\n", (unsigned long long)bytes);
  return bytes;
}
void GpuLayerPreview::evict_residency(const std::vector<TextureSet> &resident)
{
  release_bake_scratch();
  auto &s = *impl_;
  s.authoring_pages.evict();
  s.adjustment_pages.evict();
  std::set<std::string> resources;
  std::set<QUuid> live_sets;
  for (const auto &set : resident)
    if (!set.loaded_tiles().empty()) {
      live_sets.insert(set.id);
      for (const auto &layer : evaluation_layers(set))
        if (layer.fill.native_material)
          resources.insert(layer.fill.material_resource);
    }
  for (const auto &[id, entry] : s.adjustment_sources)
    if (auto token = entry.source.lock(); token && live_sets.contains(token->set))
      resources.insert(entry.native_aliases.begin(), entry.native_aliases.end());
  for (const auto &[id, entry] : s.adjustment_sources)
    if (auto token = entry.source.lock(); token && !live_sets.contains(token->set))
      visit_set_textures(entry.set, [](const FillTexture &texture) {
        if (texture.tiles) {
          for (const auto &[tile, image] : *texture.tiles)
            image->pixels.spill();
        }
        else
          texture.pixels.spill();
      });
  s.native_materials.evict_except(resources);
  s.geometry.clear();
  s.effect_parameters.clear();
  s.layer_parameters.clear();
  texture_memory_refresh_needed = true;
}
void GpuLayerPreview::reset(bool terminal)
{
  auto previous = std::move(impl_);
  impl_ = std::make_unique<Impl>();
  if (!terminal) {
    impl_->adjustment_sources = std::move(previous->adjustment_sources);
    impl_->adjustment_pages = std::move(previous->adjustment_pages);
    impl_->native_context_provider = previous->native_context_provider;
    for (const auto &[id, entry] : impl_->adjustment_sources)
      for (const auto &key : entry.native_aliases)
        impl_->native_materials.retain_alias_from(previous->native_materials, key);
  }
  bind_adjustment_resolver();
  if (previous->has_gpu())
    retired().push_back(std::move(previous));
}
void GpuLayerPreview::collect_retired(bool terminal)
{
  std::erase_if(retired(), [&](const auto &state) {
    // Ordinary retirement only polls: freeing a running prepared-info owner would
    // wait inside the compiler cancellation API. GUI shutdown may explicitly wait.
    if (terminal)
      state->cancel_programs();
    else
      state->poll_programs();
    // Terminal owner shutdown has joined/detached all image users. Metadata
    // residency leases cannot keep GPU allocations alive past GPU_exit.
    return !state->programs_pending() &&
           (terminal || std::none_of(state->inputs.begin(),
                                     state->inputs.end(),
                                     [](const auto &entry) { return entry.second.retained(); }));
  });
}
void GpuLayerPreview::poll_programs()
{
  collect_retired();
  impl_->poll_programs();
}
bool GpuLayerPreview::programs_pending() const
{
  return impl_->programs_pending() ||
         std::any_of(retired().begin(), retired().end(), [](const auto &state) {
           return state->programs_pending();
         });
}
std::string GpuLayerPreview::take_program_error()
{
  if (impl_->programs_pending())
    return {};
  auto error = impl_->program_state.take_error();
  if (error.empty())
    if (auto derived = impl_->derived_program_state.take_error(); !derived.empty())
      error = "Derived warmup: " + derived;
  if (error.empty())
    if (auto authoring = impl_->authoring_pages.take_program_error(); !authoring.empty())
      error = "Authoring warmup: " + authoring;
  return error;
}
void GpuLayerPreview::begin_frame()
{
  impl_->authoring_pages.retire_payloads();
  impl_->native_materials.step_image_uploads();
  for (auto i = impl_->adjustment_sources.begin(); i != impl_->adjustment_sources.end();) {
    if (!i->second.source.expired()) {
      ++i;
      continue;
    }
    const auto id = i->first;
    const auto prefix = i->second.set.id.toString().toStdString();
    impl_->adjustment_pages.retire(id);
    for (const auto &key : i->second.native_aliases)
      impl_->native_materials.erase(key);
    std::erase_if(impl_->stacks, [&](const auto &p) { return p.first.starts_with(prefix + ":"); });
    std::erase_if(impl_->mask_stacks, [&](const auto &p) { return p.first.starts_with(prefix); });
    i = impl_->adjustment_sources.erase(i);
  }
  impl_->comparison_memo.clear();
  impl_->anchor_memo.clear();
  if (std::getenv("BTS_RESOLUTION_LOG") &&
      performance::Clock::now() - impl_->cache_logged >= std::chrono::seconds(2))
  {
    auto &s = *impl_;
    s.cache_logged = performance::Clock::now();
    std::uint64_t prefix = 0, mask = 0;
    for (const auto &[key, stack] : s.stacks)
      for (const auto &[id, image] : stack.outputs)
        prefix += std::uint64_t(GPU_texture_width(image.get())) * GPU_texture_height(image.get()) *
                  16;
    for (const auto &[key, stack] : s.mask_stacks)
      for (const auto &[id, image] : stack.outputs)
        mask += std::uint64_t(GPU_texture_width(image.get())) * GPU_texture_height(image.get()) *
                4;
    std::printf(
        "BTS_GPU_CACHE authoring_pages=%zu authoring_bytes=%llu computed=%llu evicted=%llu page_allocations=%llu page_reuses=%llu source_bytes=%llu prefix_bytes=%llu mask_bytes=%llu all_texture_bytes=%llu driver_free_bytes=%llu\n",
        s.authoring_pages.resident_pages(),
        (unsigned long long)(s.authoring_pages.resident_pages() * 64ULL * 64 * 16),
        (unsigned long long)s.authoring_pages.computed_pages(),
        (unsigned long long)s.authoring_pages.evicted_pages(),
        (unsigned long long)s.authoring_pages.image_allocations(),
        (unsigned long long)s.authoring_pages.image_reuses(),
        (unsigned long long)s.used_bytes,
        (unsigned long long)prefix,
        (unsigned long long)mask,
        (unsigned long long)blender::gpu::host_texture_memory::live_bytes(),
        (unsigned long long)texture_free_vram.load());
    auto groups = blender::gpu::host_texture_memory::snapshot();
    std::vector<std::pair<std::string, blender::gpu::host_texture_memory::Group>> sorted(
        groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
      return a.second.bytes > b.second.bytes;
    });
    QJsonArray top;
    for (std::size_t i = 0; i < std::min<std::size_t>(12, sorted.size()); ++i)
      top.append(QJsonObject{{"label", QString::fromStdString(sorted[i].first)},
                             {"bytes", qint64(sorted[i].second.bytes)},
                             {"count", qint64(sorted[i].second.count)}});
    std::printf("BTS_GPU_TEXTURE_MEMORY %s\n",
                QJsonDocument(top).toJson(QJsonDocument::Compact).constData());
  }
  if (performance::enabled()) {
    auto &s = *impl_;
    const auto count = s.authoring_pages.computed_pages();
    if (count != s.reported_authoring_pages) {
      std::printf("BTS_GPU_AUTHORING pages=%llu total=%llu evicted=%llu resident=%zu\n",
                  (unsigned long long)(count - s.reported_authoring_pages),
                  (unsigned long long)count,
                  (unsigned long long)s.authoring_pages.evicted_pages(),
                  s.authoring_pages.resident_pages());
      s.reported_authoring_pages = count;
    }
  }
  static bool tested = false;
  if (!tested && std::getenv("BTS_VERIFY_GPU_STACK")) {
    tested = true;
    self_test();
  }
  collect_retired();
  impl_->poll_programs();
  if (!impl_->startup_prepared && !impl_->failed &&
      (!(impl_->programs_pending() || impl_->use_program_workers()) ||
       impl_->prepare_programs() == GpuPreviewUpdate::Updated))
  {
    performance::Scope timing("gpu_stack_program_warmup");
    {
      performance::Scope part("gpu_stack_warmup_authoring_pages");
      impl_->authoring_pages.prepare();
    }
    {
      performance::Scope part("gpu_adjustment_source_warmup");
      impl_->adjustment_pages.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_ranges");
      impl_->ranges.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_histograms");
      impl_->histograms.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_margins");
      if (!impl_->margins.prepare_shaders())
        throw std::runtime_error("GPU margin shader preparation failed");
    }
    for (auto format :
         {gpu::TextureFormat::SFLOAT_16_16_16_16, gpu::TextureFormat::SFLOAT_32_32_32_32})
    {
      {
        performance::Scope part("gpu_stack_warmup_finals");
        for (bool array : {false, true})
          impl_->initialize(1, 1, format, array);
      }
      if (!impl_->uniform_shaders.contains(format)) {
        performance::Scope part("gpu_stack_warmup_uniform");
        impl_->uniform_shaders.emplace(format, compile_uniform(format));
      }
    }
    if (!impl_->clear_shader) {
      performance::Scope part("gpu_stack_warmup_uniform");
      impl_->clear_shader = compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);
    }
    if (!impl_->derived_programs_started) {
      performance::Scope part("gpu_stack_warmup_derived");
      impl_->coverage_shader = Shader(gpu_derived::coverage_shader(), GPU_shader_free);
      impl_->presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
      for (auto format :
           {gpu::TextureFormat::SFLOAT_16_16_16_16, gpu::TextureFormat::SFLOAT_32_32_32_32})
        for (bool array : {false, true}) {
          const auto key = std::pair{format, array};
          impl_->ao_shaders.emplace(
              key, Shader(gpu_derived::ao_shader(format, array, quant_source), GPU_shader_free));
          impl_->normal_shaders.emplace(
              key,
              Shader(gpu_derived::normal_shader(format, array, quant_source), GPU_shader_free));
        }
    }
    impl_->startup_prepared = true;
  }
  // Explicit functional readback suite, after the separately measured owner
  // warmup block. Never startup/performance evidence, never enabled by default.
  static bool authoring_verified = false;
  if (!authoring_verified && std::getenv("BTS_GPU_AUTHORING_PROGRAM_TEST") &&
      impl_->startup_prepared)
  {
    if (!impl_->authoring_pages.programs_started())
      throw std::runtime_error("Authoring oracle requires the hosted worker route");
    performance::Scope verification("gpu_authoring_worker_verification");
    verify_gpu_authoring_workers(impl_->authoring_pages);
    authoring_verified = true;
  }
  impl_->remaining = upload_budget;
  impl_->upload_start = performance::Clock::now();
  impl_->remaining_work = 32ULL * 1024 * 1024;
}
NativeMaterialGpuLibrary &GpuLayerPreview::native_world_sources()
{
  return impl_->native_materials;
}
std::string GpuLayerPreview::world_normal_mask_signature(const TextureSet &set,
                                                         int tile,
                                                         std::span<const QUuid> masks,
                                                         std::uint64_t dependent_epoch) const
{
  PaintMetadataScope metadata;
  std::string result;
  GpuMaskBuilder builder(set, tile);
  const auto &layers = evaluation_layers(set);
  bool dependent = false;
  const auto append = [](std::string &key, const auto &value) {
    key.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  for (const auto &id : masks) {
    const auto layer = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == id; });
    if (layer == layers.end() || !layer->mask)
      throw std::invalid_argument("Native world Normal mask owner missing");
    const auto stages = builder.build(*layer, true);
    for (const auto &stage : stages)
      gpu_visit_mask_stage(
          stage,
          [&](const auto &part) {
            result += part.owner_key + part.id + part.anchor_key + part.anchor_id;
            append(result, part.kind);
            append(result, part.background);
            append(result, part.opacity);
            append(result, part.blend);
            for (const auto &row : part.rows)
              append(result, row);
            if (part.spatial) {
              append(result, part.spatial->values);
              append(result, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
            }
            if (part.bitmap)
              result += part.bitmap->asset + part.bitmap->hash;
            if (part.pixels) {
              const auto &pixels = part.pixels->tile(tile);
              append(result, pixels.resolution());
              pixels.visit_page_versions(2, [&](auto key, auto revision) {
                append(result, key);
                append(result, revision);
              });
            }
            dependent |= part.kind == GpuMaskStage::Compare ||
                         part.kind == GpuMaskStage::AnchorFill || bool(part.generator) ||
                         bool(part.anchor_stages) || part.content_anchor;
          },
          true);
  }
  // External anchor/comparison prefixes may depend on a different channel. Keep
  // their existing conservative document epoch; ordinary masks remain tile/page
  // specific and unrelated source Color/Roughness edits do not rerender W.
  if (dependent)
    append(result, dependent_epoch);
  for (const auto &map : set.mesh_maps)
    if (map)
      if (const auto member = map->tiles.find(tile);
          member != map->tiles.end() && member->second.pixels)
        result += member->second.pixels->hash;
  return result;
}
GpuPreviewUpdate GpuLayerPreview::prepare_world_normal_weight(
    const TextureSet &set, int tile, float opacity, std::span<const QUuid> masks, bool array)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  if (!std::isfinite(opacity))
    return GpuPreviewUpdate::Unsupported;
  if (!s.world_weight || GPU_texture_width(s.world_weight.get()) != w ||
      GPU_texture_height(s.world_weight.get()) != h)
  {
    s.world_weight = texture(
        "Native world Normal mask weight", w, h, gpu::TextureFormat::SFLOAT_32);
  }
  std::vector<gpu::Texture *> resolved;
  for (const auto &id : masks) {
    auto *mask = mask_texture(set, id, tile);
    if (!mask)
      return GpuPreviewUpdate::Pending;
    resolved.push_back(mask);
  }
  const float weight[4]{std::clamp(opacity, 0.0f, 1.0f), 0, 0, 0};
  GPU_texture_clear(s.world_weight.get(), GPU_DATA_FLOAT, weight);
  if (!resolved.empty()) {
    if (!s.world_weight_shader)
      s.world_weight_shader = compile_world_weight();
    if (!s.world_weight_shader)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.world_weight_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_image_bind(s.world_weight.get(), 0);
    for (auto *mask : resolved) {
      GPU_texture_bind(mask, 0);
      GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    }
    GPU_texture_image_unbind(s.world_weight.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
  }
  const auto plans = s.margin_plans.find(set.material_session_uid);
  if (plans == s.margin_plans.end() || !plans->second.extend.contains(tile))
    return GpuPreviewUpdate::Pending;
  if (!s.margins.prepare(
          plans->second.extend.at(tile), s.remaining, s.upload_start, testing_stack))
    return GpuPreviewUpdate::Pending;
  if (!s.world_apply_shader)
    s.world_apply_shader = compile_world_apply();
  if (!s.world_apply_shader)
    return GpuPreviewUpdate::Unsupported;
  for (bool kind : {false, array}) {
    auto &program = s.world_publish_shaders[kind];
    if (!program)
      program = compile_world_publish(kind);
    if (!program)
      return GpuPreviewUpdate::Unsupported;
  }
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::publish_world_normal(const TextureSet &set,
                                                       int tile,
                                                       gpu::Texture *source,
                                                       gpu::Texture *destination,
                                                       std::array<int, 3> offset,
                                                       bool apply_weight)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  if (!source || !destination || !s.world_weight || GPU_texture_width(s.world_weight.get()) != w ||
      GPU_texture_height(s.world_weight.get()) != h)
    return GpuPreviewUpdate::Unsupported;
  if (source == destination) {
    if (!apply_weight || GPU_texture_is_array(destination) || !s.world_apply_shader)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.world_apply_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(s.world_weight.get(), 0);
    GPU_texture_image_bind(destination, 0);
    GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                       GPU_BARRIER_TEXTURE_UPDATE);
    GPU_texture_image_unbind(destination);
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    return GpuPreviewUpdate::Updated;
  }
  const bool array = GPU_texture_is_array(destination);
  auto &program = s.world_publish_shaders[array];
  if (!program)
    return GpuPreviewUpdate::Unsupported;
  auto *shader = program.get();
  GPU_shader_bind(shader);
  GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
  GPU_shader_uniform_1i(shader, "apply_weight", apply_weight);
  GPU_texture_bind(source, 0);
  GPU_texture_bind(s.world_weight.get(), 1);
  GPU_texture_image_bind(destination, 0);
  GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
  GPU_texture_image_unbind(destination);
  GPU_texture_unbind_all();
  GPU_shader_unbind();
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::pad_world_normal(const TextureSet &set,
                                                   int tile,
                                                   gpu::Texture *image,
                                                   int side)
{
  auto &s = *impl_;
  const auto plans = s.margin_plans.find(set.material_session_uid);
  if (plans == s.margin_plans.end() || !plans->second.extend.contains(tile))
    return GpuPreviewUpdate::Pending;
  // Geometry-only extend donors touch outside UV islands, never an interior
  // mask hole. Signed vector and coverage use the same generic RGBA operation.
  const int w = set.width(tile), h = set.rows(tile);
  std::vector<std::uint32_t> dirty(((w + 255) / 256) * ((h + 255) / 256), 1);
  const auto key = set.id.toString().toStdString() + ":world:" + std::to_string(side) + ":" +
                   std::to_string(tile);
  return s.margins.apply(key, plans->second.extend.at(tile), image, {}, std::move(dirty)) ?
             GpuPreviewUpdate::Updated :
             GpuPreviewUpdate::Unsupported;
}
void GpuLayerPreview::publish_native_material_preview(const std::string &resource,
                                                      const std::string &hash)
{
  impl_->native_preparations.publish_preview(resource, hash);
}
void GpuLayerPreview::prepare_native_material(std::string resource,
                                              std::string content,
                                              std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->native_preparations.request(std::move(resource), std::move(content), std::move(graph));
}
void GpuLayerPreview::cancel_native_preparation(const std::string &resource)
{
  impl_->native_preparations.cancel(resource);
}
void GpuLayerPreview::discard_native_candidate(const std::string &resource)
{
  impl_->native_preparations.cancel(resource);
  impl_->native_materials.erase(resource);
}
void GpuLayerPreview::alias_native_material(const std::string &source,
                                            const std::string &destination)
{
  impl_->native_materials.alias(source, destination);
}
void GpuLayerPreview::validate_native_mapping_instances(const TextureSet &set,
                                                        const TextureSet *before) const
{
  for (const auto &layer : evaluation_layers(set))
    if (layer.kind == LayerKind::Fill && layer.fill.native_material) {
      const auto &after = *layer.fill.native_material;
      require_native_height_fill(
          after.height_policy, set.bump_origin, layer.fill.mode, layer.channels[3]);
      if (after.height_policy != NativeHeightPolicy::None)
        impl_->native_materials.validate_instance(layer.fill.material_resource, after);
      if (std::none_of(after.parameters.begin(), after.parameters.end(), [](const auto &p) {
            return !p.mapping_transform.identity();
          }))
        continue;
      const NativeMaterialDescription *old = nullptr;
      bool same_resource = false;
      if (before) {
        const auto &layers = evaluation_layers(*before);
        const auto found = std::find_if(
            layers.begin(), layers.end(), [&](const auto &value) { return value.id == layer.id; });
        if (found != layers.end() && found->fill.native_material) {
          const auto &candidate = *found->fill.native_material;
          bool same_interface = candidate.group == after.group &&
                                candidate.surface_type == after.surface_type &&
                                candidate.parameters.size() == after.parameters.size();
          if (same_interface)
            for (std::size_t i = 0; i < after.parameters.size(); ++i) {
              const auto &a = candidate.parameters[i], &b = after.parameters[i];
              same_interface = same_interface && a.identifier == b.identifier &&
                               a.type == b.type && a.source_node == b.source_node &&
                               a.source_socket == b.source_socket && a.mapping == b.mapping;
            }
          if (same_interface) {
            old = &candidate;
            same_resource = found->fill.material_resource == layer.fill.material_resource;
          }
        }
        // This preserves prior authored row state, not source lineage/equivalence.
        // A different prepared source still passes its actual layout/edge checks.
        if (old && same_resource) {
          bool unchanged = true;
          for (std::size_t i = 0; i < after.parameters.size(); ++i)
            unchanged = unchanged && old->parameters[i].mapping_transform ==
                                         after.parameters[i].mapping_transform;
          if (unchanged)
            continue;
        }
      }
      // Packed Open may queue a saved instance while its source is still preparing.
      // Once ready, signature/render enforce the same original-edge safety again.
      if (!impl_->native_materials.contains(layer.fill.material_resource)) {
        if (before)
          throw std::invalid_argument(
              "Native material is still preparing; retry the Mapping edit when ready");
        continue;
      }
      if (before)
        impl_->native_materials.validate_mapping_edit(layer.fill.material_resource, old, after);
      else
        impl_->native_materials.validate_instance(layer.fill.material_resource, after);
    }
}
bool GpuLayerPreview::has_native_material(const std::string &resource) const
{
  return impl_->native_materials.contains(resource);
}
bool GpuLayerPreview::native_material_uses_object(const std::string &resource) const
{
  return impl_->native_materials.uses_object(resource);
}
bool GpuLayerPreview::native_material_uses_ao(const std::string &resource) const
{
  return impl_->native_materials.uses_ao(resource);
}
NativeMaterialDependencies GpuLayerPreview::native_requirements(
    const std::string &resource, const NativeMaterialDescription &instance) const
{
  return impl_->native_materials.requirements(resource, instance);
}
void GpuLayerPreview::prepare_native_height(const std::string &resource,
                                            NativeMaterialHeightResource value)
{
  impl_->native_materials.prepare_height(resource, std::move(value));
}
void GpuLayerPreview::stage_native_height(NativeMaterialHeightGpu &work,
                                          NativeMaterialHeightResource resource)
{
  work.impl_->candidate = impl_->native_materials.stage_height(work.impl_->key,
                                                               std::move(resource));
}
bool GpuLayerPreview::step_native_height(NativeMaterialHeightGpu &work)
{
  if (work.ready_)
    return true;
  if (!work.impl_->candidate)
    throw std::logic_error("Native Height candidate is missing");
  return work.ready_ = impl_->native_materials.step_height_candidate(work.impl_->key,
                                                                     *work.impl_->candidate);
}
void GpuLayerPreview::install_native_height(NativeMaterialHeightGpu &work)
{
  if (!work.ready_)
    throw std::logic_error("Native Height candidate is not ready");
  if (work.impl_->change)
    throw std::logic_error("Raw authored Height publication already began");
  work.impl_->change = impl_->native_materials.install_height_candidate(
      work.impl_->key, std::move(work.impl_->candidate));
}
bool GpuLayerPreview::set_native_height_policy(TextureSet &set,
                                               const QUuid &id,
                                               NativeHeightPolicy policy) const
{
  auto &layer = TextureDocument::layer(set, id);
  require_native_height_fill(
      policy, set.bump_origin, layer.fill.mode, layer.channels[3]);  // Before any candidate edit.
  if (layer.kind != LayerKind::Fill || layer.instance || !layer.fill.native_material)
    throw std::invalid_argument("Raw authored Height requires an editable native Fill instance");
  if (layer.fill.native_material->height_policy == policy)
    return false;
  if (policy != NativeHeightPolicy::None && !set.channels[4])
    throw std::invalid_argument(
        "Enable the Texture Set Bump channel before selecting raw authored Height");
  auto instance = std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);
  instance->height_policy = policy;
  // Actual prepared source/sidecar state is the only capability authority.
  // Saved descriptor channels and source names never grant raw Height support.
  instance->channels = impl_->native_materials.effective_channels(layer.fill.material_resource,
                                                                  *instance);
  const bool bump = std::find(instance->channels.begin(), instance->channels.end(), 4) !=
                    instance->channels.end();
  layer.fill.native_material = std::move(instance);
  layer.channels[4] = bump;
  return true;
}
void GpuLayerPreview::probe_native_height(const std::string &key,
                                          const NativeMaterialDescription &instance,
                                          NativeMaterialGpuContext context)
{
  impl_->native_materials.probe_height(key, instance, std::move(context));
}
CompositeChannels GpuLayerPreview::native_view_channels(const TextureSet &set) const
{
  CompositeChannels result{};
  for (const auto &layer : evaluation_layers(set))
    if (layer.visible && layer.fill.native_material &&
        impl_->native_materials.contains(layer.fill.material_resource))
      for (int channel : impl_->native_materials.view_channels(layer.fill.material_resource,
                                                               *layer.fill.native_material))
        result[channel] = true;
  return result;
}
void GpuLayerPreview::step_native_preparations()
{
  impl_->native_preparations.step();
}
bool GpuLayerPreview::native_preparations_pending() const
{
  return impl_->native_preparations.pending() || impl_->native_materials.image_uploads_pending();
}
NativeMaterialPreparationStatus GpuLayerPreview::native_preparation_status(
    const std::string &resource) const
{
  auto state = impl_->native_preparations.status(resource);
  if (const auto error = impl_->native_materials.image_error(resource); !error.isEmpty()) {
    state.pending = false;
    state.ready = false;
    state.error = error;
    state.message = "Material source unavailable";
  }
  return state;
}
std::string GpuLayerPreview::native_image_error() const
{
  return impl_->native_image_failure;
}
void GpuLayerPreview::install_native_material(std::string resource,
                                              std::string content,
                                              std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->native_materials.install(resource, std::move(content), std::move(graph));
  impl_->native_materials.prepare_world_normal(resource);
}
void GpuLayerPreview::set_native_context_provider(NativeContextProvider provider)
{
  impl_->native_context_provider = std::move(provider);
}
NativeMaterialViewContext GpuLayerPreview::native_world_view(const TextureSet &set,
                                                             const TextureLayer &layer,
                                                             int tile) const
{
  if (!impl_->native_context_provider)
    throw NativeFragmentContextError(
        NativeFragmentContextCode::View,
        "Native fragment requires a real validated view: provider is unavailable");
  NativeMaterialGpuContext context;
  NativeMaterialDependencies required;
  required.view = true;
  impl_->native_context_provider(set, layer, tile, required, context);
  context.view.validate();
  return context.view;
}
TextureSet GpuLayerPreview::prepare_layer_capture(const TextureSet &source) const
{
  auto result = source;
  if (source.instance_layers) {
    result.layers = evaluation_layers(source);
    result.instance_layers.reset();
  }
  for (auto &layer : result.layers)
    if (layer.kind == LayerKind::Fill && layer.fill.native_material) {
      layer.fill.native_material
          ->validate();  // Admit the detached parameter copy before allocation.
      auto description = std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);
      description->channels = impl_->native_materials.effective_channels(
          layer.fill.material_resource, *description);
      if (description->channels.empty())
        throw std::runtime_error("Native source has no prepared channel capabilities");
      layer.fill.native_material = std::move(description);
    }
  return result;
}
std::shared_ptr<const AdjustmentPaintSnapshot> GpuLayerPreview::begin_adjustment_source(
    const TextureSet &source, const QUuid &layer_id, const QUuid &document)
{
  PaintMetadataScope metadata;
  const auto &layers = evaluation_layers(source);
  const auto selected = std::find_if(
      layers.begin(), layers.end(), [&](const auto &item) { return item.id == layer_id; });
  if (selected == layers.end())
    throw std::invalid_argument("Adjustment layer is missing");
  const auto &layer = *selected;
  if (!adjustment_layer(layer))
    throw std::invalid_argument("Adjustment source requires Passthrough Paint channels");
  auto token = std::make_shared<AdjustmentPaintSnapshot>();
  token->id = QUuid::createUuid();
  token->set = source.id;
  token->layer = layer_id;
  token->document = document;
  token->tiles = source.tiles;
  auto &s = *impl_;
  Impl::AdjustmentEntry entry;
  entry.source = token;
  entry.set = prepare_layer_capture(source);
  entry.set.id = token->id;
  entry.layer = layer_id;
  try {
    for (auto &item : entry.set.layers)
      if (item.kind == LayerKind::Fill && item.fill.native_material) {
        const auto original = item.fill.material_resource;
        const auto alias = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        s.native_materials.alias(original, alias);
        entry.native_aliases.push_back(alias);
        item.fill.material_resource = alias;
        for (int tile : source.tiles)
          for (int channel :
               s.native_materials.effective_channels(alias, *item.fill.native_material))
          {
            auto context = s.native_materials.default_context(alias);
            context.tile = tile;
            const auto &dependencies = s.native_materials.dependencies(
                alias, channel, *item.fill.native_material);
            if (s.native_context_provider)
              s.native_context_provider(source, item, tile, dependencies, context);
            else if (dependencies.object || dependencies.view)
              throw std::runtime_error("Native material object/view context is not installed");
            entry.contexts[{item.id, tile, channel}] = std::move(context);
          }
      }
    s.adjustment_sources.emplace(token->id, std::move(entry));
    return token;
  }
  catch (...) {
    for (const auto &alias : entry.native_aliases)
      s.native_materials.erase(alias);
    throw;
  }
}
gpu::Texture *GpuLayerPreview::adjustment_page(const SmudgeCapture &capture,
                                               int plane,
                                               int tile,
                                               std::uint32_t page,
                                               gpu::Texture *own,
                                               performance::Clock::time_point deadline)
{
  if (!capture.adjustment.enabled(plane))
    return own;
  if (performance::Clock::now() >= deadline)
    return nullptr;
  auto &s = *impl_;
  const auto &token = *capture.adjustment.snapshot;
  try {
    const int channel = capture.adjustment.channels[plane];
    const auto found = s.adjustment_sources.find(token.id);
    if (found == s.adjustment_sources.end())
      throw std::runtime_error("Adjustment source snapshot is no longer resident");
    auto &entry = found->second;
    if (!entry.failure.empty()) {
      s.input_rejected = true;
      return nullptr;
    }
    gpu::Texture *prefix = nullptr;
    if (!s.adjustment_pages.contains(token, channel, tile, capture.resolution, page)) {
      const auto snapshot_key = entry.set.id.toString().toStdString() + ":" +
                                std::to_string(channel) + ":" + std::to_string(tile);
      if (!s.stacks.contains(snapshot_key)) {
        const auto live_key = token.set.toString().toStdString() + ":" + std::to_string(channel) +
                              ":" + std::to_string(tile);
        if (auto live = s.stacks.find(live_key); live != s.stacks.end()) {
          const auto &before = live->second;
          auto &snapshot = s.stacks[snapshot_key];
          snapshot.plan = before.plan;
          snapshot.queried_sources = before.queried_sources;
          std::set<QUuid> below;
          for (const auto &layer : evaluation_layers(entry.set)) {
            if (layer.id == entry.layer)
              break;
            below.insert(layer.id);
          }
          std::set<std::string> retained;
          for (const auto &[id, owner] : before.node_owners)
            if (below.contains(owner) && before.outputs.contains(id)) {
              snapshot.outputs.emplace(id, before.outputs.at(id));
              snapshot.node_owners.emplace(id, owner);
              retained.insert(id);
            }
          snapshot.plan.retain(retained);
          snapshot.node_versions = before.node_versions;
          std::erase_if(snapshot.node_versions,
                        [&](const auto &v) { return !retained.contains(v.first); });
          snapshot.node_clock = before.node_clock;
          snapshot.width = before.width;
          snapshot.height = before.height;
        }
      }
      auto saved_pins = s.adjustment_input_pins;
      struct RestorePins {
        std::set<std::string> &pins, saved;
        ~RestorePins()
        {
          pins = std::move(saved);
        }
      } restore{s.adjustment_input_pins, std::move(saved_pins)};
      for (const auto &[key, input] : s.inputs)
        s.adjustment_input_pins.insert(key);
      const auto result = update_tile(
          entry.set, channel, s.zero.get(), tile, {}, true, &entry.layer, false);
      if (result == GpuPreviewUpdate::Pending)
        return nullptr;
      if (result != GpuPreviewUpdate::Updated)
        throw std::runtime_error("Adjustment lower stack is unavailable on GPU");
      auto &stack = s.stacks.at(entry.set.id.toString().toStdString() + ":" +
                                std::to_string(channel) + ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + entry.layer.toString().toStdString() +
                                           "/below");
      prefix = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    }
    auto *result = s.adjustment_pages.compose(
        token, channel, tile, capture.resolution, page, prefix, own);
    for (const auto &id : s.adjustment_pages.take_discarded()) {
      const auto key = id.toString().toStdString();
      std::erase_if(s.stacks, [&](const auto &p) { return p.first.starts_with(key + ":"); });
      std::erase_if(s.mask_stacks, [&](const auto &p) { return p.first.starts_with(key); });
    }
    return result;
  }
  catch (const AdjustmentPaintMemoryError &error) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id, token.set, error.what(), error.needed, error.available};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = error.what();
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
  catch (const std::bad_alloc &) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id,
        token.set,
        "Adjustment GPU source allocation failed (active pages need 0.125 MiB); available " +
            std::to_string(double(texture_free_vram.load()) / (1024 * 1024)) +
            " MiB. Stroke cancelled; prior image retained."};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = pending_adjustment_failure->message;
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
  catch (const std::exception &error) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id,
        token.set,
        std::string("Adjustment GPU source failed: ") + error.what() +
            ". Stroke cancelled; prior image retained."};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = pending_adjustment_failure->message;
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
}
void GpuLayerPreview::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit)
{
  if (!qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO") &&
      !qEnvironmentVariableIsSet("BTS_BM_GPU_ADJUSTMENT_TEST"))
    throw std::logic_error(
        "Adjustment budget injection is restricted to the explicit diagnostic actor");
  impl_->adjustment_pages.diagnostic_budget(limit);
}
std::array<std::uint64_t, 2> GpuLayerPreview::adjustment_copy_statistics() const
{
  return {impl_->frozen_prefix_copies, impl_->frozen_prefix_copy_texels};
}
std::array<std::uint64_t, 3> GpuLayerPreview::native_statistics() const
{
  const auto s = impl_->native_materials.statistics();
  return {s.sources, s.programs, s.dispatches};
}
bool GpuLayerPreview::prefetch_geometry(
    const std::map<int, std::shared_ptr<const FillSurfaceAtlas>> &atlases)
{
  for (const auto &[tile, atlas] : atlases)
    if (!impl_->surface(atlas, tile))
      return false;
  return true;
}
void GpuLayerPreview::set_padding_geometry(
    std::uint32_t uid,
    const std::map<int, std::shared_ptr<const GpuMarginPlan>> &extend,
    const std::map<int, std::shared_ptr<const GpuMarginPlan>> &adjacent)
{
  impl_->margin_plans[uid] = {extend, adjacent};
}
void GpuLayerPreview::set_native_normal_error(const QUuid &set, std::string error)
{
  impl_->native_normal_errors[set] = std::move(error);
}
void GpuLayerPreview::clear_native_normals(const QUuid &set)
{
  impl_->native_normal_errors.erase(set);
  std::erase_if(impl_->native_normals, [&](const auto &v) { return std::get<0>(v.first) == set; });
}
void GpuLayerPreview::set_native_normal(const QUuid &set,
                                        const QUuid &layer,
                                        int tile,
                                        std::shared_ptr<gpu::Texture> image)
{
  impl_->native_normals[{set, layer, tile}] = std::move(image);
  ++impl_->native_normal_revision;
}
TextureSet GpuLayerPreview::native_normal_stack(const TextureSet &source) const
{
  TextureSet set = source;
  set.layers = evaluation_layers(source);
  set.instance_layers.reset();
  bool any = false;
  for (auto &layer : set.layers) {
    if (!source.channels[3])
      layer.channels[3] = false;
    if (layer.fill.native_material) {
      const bool normal = layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
                          impl_->native_materials.contains(layer.fill.material_resource) &&
                          impl_->native_materials.world_normal_perturbed(
                              layer.fill.material_resource);
      layer.channels[3] = normal && set.channels[4] && layer.channels[4];
      if (normal) {
        any = true;
        layer.opacity[3] = layer.opacity[4];
        layer.blending[3] = layer.blending[4];
      }
    }
  }
  if (any) {
    set.channels[3] = true;
    for (auto &layer : set.layers)
      if (layer.kind == LayerKind::Folder) {
        layer.channels[3] = layer.channels[4];
        layer.opacity[3] = layer.opacity[4];
        layer.blending[3] = layer.blending[4];
      }
  }
  return set;
}
bool GpuLayerPreview::uniform(gpu::Texture *target, const std::array<float, 4> &value)
{
  auto &s = *impl_;
  const auto format = GPU_texture_format(target);
  if (s.uniform_failed || GPU_texture_dimensions(target) != 2 || GPU_texture_is_array(target) ||
      (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
       format != gpu::TextureFormat::SFLOAT_32_32_32_32))
    return false;
  if (!s.uniform_shaders.contains(format))
    s.uniform_shaders.emplace(format, compile_uniform(format));
  auto *shader = s.uniform_shaders.at(format).get();
  if (!shader) {
    s.uniform_failed = true;
    return false;
  }
  // Update the canonical image directly: no full-size broadcast temporary/copy.
  performance::Scope timing("gpu_uniform_broadcast");
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_shader_bind(shader);
  GPU_shader_uniform_4fv(shader, "fill_value", value.data());
  GPU_texture_image_bind(target, 0);
  GPU_compute_dispatch(
      shader, (GPU_texture_width(target) + 15) / 16, (GPU_texture_height(target) + 15) / 16, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_image_unbind(target);
  GPU_shader_unbind();
  return true;
}
bool GpuLayerPreview::supported(const TextureSet &set, int channel)
{
  if (channel == 15) {
    if (std::getenv("BTS_GPU_STACK_DISABLE"))
      return false;
    const auto &layers = evaluation_layers(set);
    const auto selected = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == set.selected_layer; });
    if (selected == layers.end() || !selected->mask)
      return false;
    for (const auto &layer : layers)
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (!gpu_mask_effect_supported(effect))
            return false;
    return true;
  }
  if (channel == 3)
    return supported(set, 6) && (!set.channels[4] || supported(set, 4));
  if (!gpu_authored_channel(set, channel) || std::getenv("BTS_GPU_STACK_DISABLE"))
    return false;
  const auto unsupported = [&](const TextureLayer &layer, const char *reason) {
    static std::set<std::string> logged;
    const auto key = set.id.toString().toStdString() + ":" + layer.id.toString().toStdString() +
                     ":" + std::to_string(channel) + ":" + reason;
    if (logged.insert(key).second)
      std::fprintf(stderr,
                   "BTS_GPU_STACK_FALLBACK uid=%u layer=%s channel=%d reason=%s background=1\n",
                   set.material_session_uid,
                   layer.name.c_str(),
                   channel,
                   reason);
    return false;
  };
  const bool anchor_dependencies = std::any_of(
      evaluation_layers(set).begin(), evaluation_layers(set).end(), [](const auto &layer) {
        return TextureDocument::anchor_dependent(layer);
      });
  if (anchor_dependencies)
    for (const auto &layer : evaluation_layers(set))
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (!gpu_mask_effect_supported(effect))
            return unsupported(layer, "anchor-producer-mask-effects");
  for (int tile : set.tiles)
    for (const auto &layer : evaluation_layers(set)) {
      const auto info = gpu_layer_channel(layer, channel);
      if (!gpu_channel_enabled(set, channel) || !active(layer, channel, tile))
        continue;

      if (int(info.blend) > 28)
        return unsupported(layer, "blend-mode");
      if (layer.mask && (layer.mask->enabled || anchor_dependencies)) {
        if (std::any_of(layer.mask->effects.begin(), layer.mask->effects.end(), [](const auto &e) {
              return !gpu_mask_effect_supported(e);
            }))
          return unsupported(layer, "mask-effects");
      }
      if (layer.kind == LayerKind::Fill) {
        if (const auto &source = fill_channel_texture(layer.fill, info.fill_channel);
            source && !source->pixels)
          return unsupported(layer, "source-not-ready");
      }
    }
  return true;
}
std::uint64_t GpuLayerPreview::texture_resource_allocations() const
{
  return impl_->resource_allocations;
}
blender::gpu::Texture *GpuLayerPreview::diagnostic_composite_texture(const TextureSet &set,
                                                                     int channel,
                                                                     int tile) const
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto found = impl_->stacks.find(key);
  return found == impl_->stacks.end() ? nullptr : found->second.head;
}
std::uint64_t GpuLayerPreview::content_node_revision(const TextureSet &set,
                                                     int channel,
                                                     int tile,
                                                     const std::string &node) const
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto stack = impl_->stacks.find(key);
  if (stack == impl_->stacks.end())
    return 0;
  const auto found = stack->second.node_versions.find(node);
  if (found == stack->second.node_versions.end())
    return 0;
  std::uint64_t revision = 0;
  for (const auto &[chunk, page] : found->second)
    revision = std::max(revision, page.revision);
  return revision;
}
GpuPreviewUpdate GpuLayerPreview::update_tile(const TextureSet &source_set,
                                              int channel,
                                              gpu::Texture *target,
                                              int tile,
                                              std::array<int, 3> target_offset,
                                              bool prefix_only,
                                              const QUuid *query_layer,
                                              bool query_source,
                                              const QUuid *query_effect,
                                              bool thumbnail,
                                              bool raw_anchor,
                                              bool authored_ao_output)
{

  const auto normal_set = channel == 6 ?
                              std::optional<TextureSet>(native_normal_stack(source_set)) :
                              std::nullopt;
  auto view = normal_set ? *normal_set : source_set;
  if (view.residency_view && !view.tile_loaded(tile)) {
    view.layers = {TextureLayer{}};
    view.layers.front().channels.fill(false);
    view.layers.front().ao_enabled = false;
    view.instance_layers.reset();
    view.mesh_maps = {};
    view.projection_geometry.reset();
    view.projection_tiles.clear();
    view.geometry_ownership.clear();
  }
  const auto &set = view;
  PaintMetadataScope metadata;
  if (!supported(set, channel) || !set.tiles.contains(tile) ||
      GPU_texture_dimensions(target) != 2 || target_offset[0] < 0 || target_offset[1] < 0 ||
      target_offset[2] < 0 ||
      (!query_layer && !prefix_only &&
       (GPU_texture_width(target) < target_offset[0] + set.width(tile) ||
        GPU_texture_height(target) < target_offset[1] + set.rows(tile))) ||
      target_offset[2] >= std::max(1, GPU_texture_layer_count(target)))
    return GpuPreviewUpdate::Unsupported;
  const auto format = GPU_texture_format(target);
  if (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
      format != gpu::TextureFormat::SFLOAT_32_32_32_32)
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  performance::Scope timing("gpu_stack_submit");
  s.input_rejected = false;
  s.native_image_failure.clear();
  ++s.clock;
  const auto upload_before = s.remaining;
  const int width = set.width(tile), height = set.rows(tile);
  if (const auto state = s.initialize(width, height, format, GPU_texture_is_array(target));
      state != GpuPreviewUpdate::Updated)
    return state;
  // Keep disabled masks cached, but release deleted masks/layers and old tiles.
  const auto set_prefix = set.id.toString().toStdString();
  std::set<std::string> live_masks;
  for (const auto &layer : evaluation_layers(set))
    if (layer.mask)
      for (int number : set.tiles)
        live_masks.insert(paint_key(set, layer, 2, true, number));
  std::erase_if(s.mask_stacks, [&](const auto &item) {
    return item.first.starts_with(set_prefix) && !live_masks.contains(item.first);
  });
  // Resolving an adjustment payload can recursively initialize a 2D source
  // query. Keep this caller's target-format/array variant across that recursion.
  auto *const target_final_shader = s.final_shader;
  const auto stack_key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                         std::to_string(tile) +
                         (raw_anchor ? ":anchor:" + query_layer->toString().toStdString() :
                                       std::string{});
  const auto target_key = std::pair{reinterpret_cast<std::uintptr_t>(target), target_offset};
  auto &stack = s.stacks[stack_key];
  stack.last_used = s.clock;
  const bool target_changed = !prefix_only && s.target_owners[target_key] != stack_key;
  if (stack.width != width || stack.height != height) {
    stack.outputs.clear();
    stack.node_owners.clear();
    stack.queries.clear();
    stack.node_versions.clear();
    stack.coverage.reset();
    stack.height_image.reset();
    stack.output_dirty.clear();
    stack.height_dirty.clear();
    stack.head = nullptr;
    stack.plan.reset();
    stack.width = width;
    stack.height = height;
  }
  std::shared_ptr<const GpuMarginPlan> margin;
  if (!query_layer)
    if (const auto found = s.margin_plans.find(set.material_session_uid);
        found != s.margin_plans.end())
    {
      const auto &plans = set.padding_3d && channel != 3 && channel != 6 ? found->second.adjacent :
                                                                           found->second.extend;
      if (!plans.contains(tile))
        return GpuPreviewUpdate::Pending;
      margin = plans.at(tile);
      if (margin->width != width || margin->height != height)
        return GpuPreviewUpdate::Pending;
      if (!s.margins.prepare(margin, s.remaining, s.upload_start, testing_stack))
        return GpuPreviewUpdate::Pending;
    }
  struct Input {
    std::shared_ptr<NativeMaterialGpuContext> native_context;
    const MeshMapPlane *native_ao = nullptr;
    const TextureLayer *layer;
    GpuLayerChannel channel;
    GpuAnchorInput anchor;
    std::optional<MeshGeneratorSettings> generator;
    GpuGeneratorInputs generator_inputs;
    gpu::Texture *source = nullptr, *mask = nullptr, *surface = nullptr, *effects = nullptr,
                 *ownership_image = nullptr, *selection_image = nullptr;
    std::shared_ptr<const GeometryOwnershipTile> ownership;
    std::array<const MeshMapPlane *, 6> mask_planes{};
    std::array<gpu::Texture *, 6> mask_maps{};
    std::vector<GpuEffectRow> effect_rows;
    std::vector<GpuMaskStage> mask_stages;
    std::string mask_signature;
    int content_effects = 0, content_pass = 0, effect_plane = 0;
    const FillProjection *effect_fill = nullptr;
    const TilePaintPixels *effect_pixels = nullptr;
    std::string effect_key;
    std::shared_ptr<const FillSurfaceAtlas> geometry;
    int kind = 0;
    bool enabled = false, constant = false, zero_opacity = false, raw_linear_source = false;
    std::string below, group;
    std::array<float, 3> map_u{}, map_v{};
  };
  GpuMaskBuilder mask_builder(set, tile);
  const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
    return comparison_inputs(set, stage, target, tile);
  };
  const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer, const AnchorReference &ref) {
    return anchor_input(set, consumer, ref, tile);
  };
  const std::string query_id = query_layer ?
                                   "query/" + query_layer->toString().toStdString() +
                                       (query_source ? "/source" : "/below") +
                                       (query_effect ? "/before-selected-effect" : "") +
                                       (thumbnail ? "/thumbnail" : "") +
                                       (raw_anchor ?
                                            "/anchor/" + query_effect->toString().toStdString() :
                                            std::string{}) :
                                   std::string{};
  std::string query_head;
  bool query_enabled = false;
  if (query_layer && query_source)
    stack.queried_sources.insert(query_layer->toString().toStdString());
  std::vector<Input> inputs;
  std::vector<GpuStackNode> descriptors;
  std::set<std::string> protected_keys;
  QVarLengthArray<std::string, 17> heads;
  heads.resize(17);
  std::map<std::string, bool> constant_nodes;
  const auto append = [](std::string &data, const auto &value) {
    data.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  const auto pages = [&](GpuStackNode &node, const PaintPixels &pixels, int plane, int slot) {
    if (!pixels.resolution())
      return;
    const double sx = double(width) / pixels.resolution(),
                 sy = double(height) / pixels.resolution();
    const auto versions = pixels.page_versions();
    for (const auto &[page, revision] : versions[plane]) {
      const int px = int(page % 65536) * 64, py = int(page / 65536) * 64;
      const int x = int(std::floor(px * sx)) - 1, y = int(std::floor(py * sy)) - 1,
                right = int(std::ceil((px + 64) * sx)) + 1,
                bottom = int(std::ceil((py + 64) * sy)) + 1;
      node.pages[(std::uint64_t(slot) << 32) | page] = {x, y, right - x, bottom - y, revision};
    }
  };
  const auto &layers = evaluation_layers(set);
  auto folder_entries = folder_entry_plan(layers);
  const int first_layer = precompose_evaluation_start(layers, query_effect, -1, query_layer);
  while (!folder_entries.empty() && folder_entries.back().first < first_layer)
    folder_entries.pop_back();
  for (int layer_number = first_layer; layer_number < int(layers.size()); ++layer_number) {
    while (!folder_entries.empty() && folder_entries.back().first == layer_number) {
      const auto &folder = layers[folder_entries.back().folder];
      folder_entries.pop_back();
      if (folder.depth + 1 >= heads.size())
        heads.resize(folder.depth + 2);
      const bool raw_folder = query_layer && folder.id == *query_layer &&
                              (thumbnail || raw_anchor);
      heads[folder.depth + 1] = !raw_folder && gpu_layer_channel(folder, channel).blend ==
                                                   LayerBlend::Passthrough ?
                                    heads[folder.depth] :
                                    std::string{};
    }
    const auto &layer = layers[layer_number];
    // An actual source Normal contribution has no authored scalar H. Bump4 is
    // its AG enable/opacity control, not an opaque invented 0.5 Fill over others.
    if (channel == 4 && layer.fill.native_material &&
        layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
        s.native_materials.contains(layer.fill.material_resource) &&
        s.native_materials.world_normal_perturbed(layer.fill.material_resource) &&
        layer.effects.empty() && layer.pixels.page_count(4) == 0)
      continue;
    const bool querying = query_layer && layer.id == *query_layer;
    if (querying) {
      query_enabled = raw_anchor || gpu_channel_enabled(set, channel);
      if (!query_source) {
        query_head = heads.at(layer.depth);
        break;
      }
      const auto values = gpu_layer_channel(layer, channel);
      if (!raw_anchor)
        query_enabled &= values.enabled && (thumbnail || values.blend != LayerBlend::Disabled);
      if (!query_enabled)
        break;
    }
    if (!querying && (!gpu_channel_enabled(set, channel) || !active(layer, channel, tile))) {
      if (layer.kind == LayerKind::Folder)
        heads[layer.depth + 1].clear();
      continue;
    }
    GpuStackNode node;
    node.id = layer.id.toString().toStdString();
    Input in;
    in.layer = &layer;
    in.channel = gpu_layer_channel(layer, channel);
    in.raw_linear_source = channel == 0 && raw_linear_color_source(layer);
    // Ordinary source queries share canonical pixels. A raw thumbnail of a
    // zero-opacity/Passthrough source needs different coverage/compositing; keep
    // those scratch outputs distinct from Compare's already-cached inputs.
    const bool isolated_thumbnail = querying && thumbnail &&
                                    (in.channel.opacity <= 0 ||
                                     in.channel.blend == LayerBlend::Passthrough ||
                                     in.channel.blend == LayerBlend::Disabled);
    if (isolated_thumbnail)
      node.id += "/thumbnail-raw";
    if (querying && (thumbnail || raw_anchor)) {
      in.channel.enabled = true;
      in.channel.opacity = 1;
      in.channel.blend = LayerBlend::Normal;
    }
    in.enabled = querying || active(layer, channel, tile);
    in.below = heads.at(layer.depth);
    if (!in.below.empty())
      node.dependencies.push_back(in.below);
    if (layer.kind == LayerKind::Folder) {
      in.kind = in.channel.blend == LayerBlend::Passthrough ? 11 : 3;
      in.group = heads.at(layer.depth + 1);
      heads[layer.depth + 1].clear();
      if (!in.group.empty())
        node.dependencies.push_back(in.group);
    }
    auto &parameters = node.parameters;
    append(parameters, layer.kind);
    append(parameters, layer.precompose_below);
    append(parameters, in.enabled);
    const auto source_opacity_offset = parameters.size();
    append(parameters, in.channel.opacity);
    const auto source_blend_offset = parameters.size();
    append(parameters, in.channel.blend);
    append(parameters, gpu_channel_precision(set, channel));
    const auto source_value_offset = parameters.size();
    const auto value = in.channel.value;
    append(parameters, value);
    append(parameters, in.raw_linear_source);
    if (channel == 0 && layer.quick_overlay) {
      in.kind = 8;
      const auto &overlay = *layer.quick_overlay;
      append(parameters, overlay.editing);
      append(parameters, overlay.field && overlay.field->inverted);
      if (overlay.field) {
        protected_keys.insert(paint_key(set, layer, 0, false, tile));
        const auto &pixels = overlay.field->pixels.tile(tile);
        append(parameters, pixels.resolution());
        pages(node, pixels, 2, 0);
        const int halo_x = int(std::ceil(double(width) / std::max(1, pixels.resolution()) +
                                         double(width) / 256)),
                  halo_y = int(std::ceil(double(height) / std::max(1, pixels.resolution()) +
                                         double(height) / 256));
        for (auto &[id, page] : node.pages) {
          page.x -= halo_x;
          page.y -= halo_y;
          page.width += 2 * halo_x;
          page.height += 2 * halo_y;
        }
      }
    }
    else if (layer.kind == LayerKind::Paint) {
      in.kind = 1;
      protected_keys.insert(paint_key(set, layer, channel, false, tile));
      pages(node, in.channel.pixels->tile(tile), in.channel.plane, 0);
    }
    else if (layer.kind == LayerKind::Fill) {
      if (channel == 6 && layer.fill.native_material) {
        const auto found = s.native_normals.find({set.id, layer.id, tile});
        in.kind = 12;
        in.source = found == s.native_normals.end() ? nullptr : found->second.get();
        append(parameters, reinterpret_cast<std::uintptr_t>(in.source));
        append(parameters, s.native_normal_revision);
      }
      else if (channel == 4 && layer.fill.native_material &&
               layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
               s.native_materials.contains(layer.fill.material_resource) &&
               s.native_materials.world_normal_perturbed(layer.fill.material_resource))
      {
        // Keep authored scalar Paint/effects over an unavailable native scalar
        // source. A private world-vector contribution is never converted into H.
        in.kind = 1;
        protected_keys.insert(paint_key(set, layer, channel, false, tile));
        pages(node, in.channel.pixels->tile(tile), in.channel.plane, 0);
      }
      else if (layer.fill.native_material) {
        require_native_height_fill(layer.fill.native_material->height_policy,
                                   set.bump_origin,
                                   layer.fill.mode,
                                   layer.channels[3]);
        if (!s.native_materials.contains(layer.fill.material_resource))
          return GpuPreviewUpdate::Pending;
        if (!s.native_materials.prepare_images(layer.fill.material_resource)) {
          const auto error = s.native_materials.image_error(layer.fill.material_resource);
          if (error.isEmpty())
            return GpuPreviewUpdate::Pending;
          s.native_image_failure = error.toStdString();
          s.input_rejected = true;
          return GpuPreviewUpdate::Unsupported;
        }
        in.kind = 10;
        in.native_context = std::make_shared<NativeMaterialGpuContext>(
            s.native_materials.default_context(layer.fill.material_resource));
        in.native_context->tile = tile;
        const auto &dependencies = s.native_materials.dependencies(
            layer.fill.material_resource, in.channel.fill_channel, *layer.fill.native_material);
        parameters += s.native_materials.signature(
            layer.fill.material_resource, in.channel.fill_channel, *layer.fill.native_material);
        if (dependencies.surface || fill_surface_projection(layer.fill)) {
          in.geometry = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                              set.projection_geometry;
          if (!in.geometry || !in.geometry->tiles.contains(tile))
            return GpuPreviewUpdate::Pending;
          append(parameters, reinterpret_cast<std::uintptr_t>(in.geometry.get()));
        }
        if (dependencies.ao) {
          if (set.mesh_maps[3])
            if (auto found = set.mesh_maps[3]->tiles.find(tile);
                found != set.mesh_maps[3]->tiles.end() && found->second.pixels)
              in.native_ao = &found->second;
          if (!in.native_ao)
            throw std::runtime_error("Native material requires a baked AO map — Bake Mesh Maps…");
          const auto &pixels = *in.native_ao->pixels;
          const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) + ":" +
                           std::to_string(pixels.height);
          protected_keys.insert(key);
          parameters += key;
        }
        in.native_context->projection = native_material_projection(
            layer.fill, in.geometry.get(), set);
        if (auto snapshot = s.adjustment_sources.find(set.id);
            snapshot != s.adjustment_sources.end())
        {
          const auto saved = snapshot->second.contexts.find(
              {layer.id, tile, in.channel.fill_channel});
          if (saved == snapshot->second.contexts.end())
            throw std::logic_error("Frozen native material context missing");
          auto projection = in.native_context->projection;
          *in.native_context = saved->second;
          in.native_context->projection = std::move(projection);
        }
        else if (s.native_context_provider)
          s.native_context_provider(set, layer, tile, dependencies, *in.native_context);
        else if (dependencies.object || dependencies.view)
          throw std::runtime_error("Native material object/view context is not installed");
        const auto projection_rows = in.native_context->projection->encoded();
        for (std::size_t row = 0; row < in.native_context->projection->row_count(); ++row)
          append(parameters, projection_rows[row]);
        if (dependencies.object) {
          append(parameters, in.native_context->object_to_world);
          append(parameters, in.native_context->world_to_object);
          append(parameters, in.native_context->bounds_min);
          append(parameters, in.native_context->bounds_max);
          append(parameters, in.native_context->object_color);
          append(parameters, in.native_context->object_random);
          append(parameters,
                 in.native_context->objects ? in.native_context->objects->hash() :
                                              std::uint64_t(0));
        }
        if (dependencies.view) {
          append(parameters, in.native_context->camera_position);
          append(parameters, in.native_context->camera_forward);
          append(parameters, in.native_context->perspective);
        }
      }
      else {

        const auto &reference = layer.fill.anchors.at(in.channel.fill_channel);
        if (!reference.source.isNull()) {
          auto input = resolve_anchor(layer.id, reference);
          if (!input)
            return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
          in.kind = 9;
          in.anchor = *input;
          append(parameters, reference.channel);
          append(parameters, reference.component);
          append(parameters, reference.coverage);
          int slot = 0;
          gpu_anchor_dependencies(node, *input, slot);
        }
        else {
          append(parameters, layer.fill.alpha_mask.at(in.channel.fill_channel));
          if (fill_channel_texture(layer.fill, in.channel.fill_channel)) {
            const auto *resource = tile_resource(layer.fill, in.channel.fill_channel, tile);
            in.kind = resource ? (fill_surface_projection(layer.fill) ? 5 : 2) : 4;
            if (resource) {
              const auto key = resource_key(*resource);
              protected_keys.insert(key);
              parameters += key;
            }
            else
              parameters += "missing tile";
          }
          if (in.kind != 4 &&
              (fill_spatial_projection(layer.fill) || (channel == 0 && layer.fill.atom)))
            in.kind = fill_surface_projection(layer.fill) ? 5 : 2;
          if (layer.fill.mode == 5) {
            const auto grid = fill_warp_grid(layer.fill);
            append(parameters, grid->width());
            append(parameters, grid->height());
            for (double v : grid->columns)
              append(parameters, v);
            for (double v : grid->rows)
              append(parameters, v);
            for (const auto &node : grid->nodes) {
              append(parameters, node.position);
              append(parameters, node.du);
              append(parameters, node.dv);
              append(parameters, node.duv);
            }
            append(parameters, layer.fill.warp_depth);
            append(parameters,
                   bool(layer.fill.warp_geometry && (layer.fill.warp_geometry->enabled ||
                                                     layer.fill.warp_geometry->frozen_reference)));
          }
          append(parameters, in.channel.fill_channel == 4 ? layer.fill.bump_bias : 0.f);
          append(parameters, layer.fill.atom);
          append(parameters, layer.fill.border_width);
          append(parameters, layer.fill.border_dots);
          append(parameters, layer.fill.middle_dot);
          append(parameters, layer.fill.invert);
          append(parameters, layer.fill.wrap);
          append(parameters, layer.fill.shape_crop);
          append(parameters, layer.fill.depth_culling);
          append(parameters, layer.fill.depth_hardness);
          append(parameters, layer.fill.backface_culling);
          append(parameters, layer.fill.backface_angle);
          append(parameters, layer.fill.backface_hardness);
          append(parameters, layer.fill.cylinder_angle);
          if (layer.fill.symmetry) {
            const auto &sym = *layer.fill.symmetry;
            append(parameters, sym.constraint.enabled);
            append(parameters, sym.constraint.mode);
            append(parameters, sym.constraint.axis);
            append(parameters, sym.constraint.offset);
            append(parameters, sym.constraint.center);
            append(parameters, sym.constraint.copies);
            append(parameters, sym.flip_u);
            append(parameters, sym.flip_v);
          }
          auto mapping = layer.fill;
          if (mapping.mode == 1) {
            mapping.offset = {};
            mapping.physical = false;
          }
          if (fill_spatial_projection(mapping))
            mapping.physical = false;
          mapping.native_per_uv = set.native_per_uv;
          mapping.cm_per_native = set.cm_per_native;
          const double u = fill_surface_projection(mapping) ? 0 : (tile - 1001) % 10,
                       v = fill_surface_projection(mapping) ? 0 : (tile - 1001) / 10;
          const auto a = mapping.mode == 6 ? std::array<double, 2>{0, 0} :
                                             fill_uv_map(mapping, u, v),
                     b = mapping.mode == 6 ? std::array<double, 2>{1, 0} :
                                             fill_uv_map(mapping, u + 1, v),
                     c = mapping.mode == 6 ? std::array<double, 2>{0, 1} :
                                             fill_uv_map(mapping, u, v + 1);
          in.map_u = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
          in.map_v = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
          append(parameters, in.map_u);
          append(parameters, in.map_v);
          append(parameters, layer.fill.mode);
          append(parameters, layer.fill.filtering);
          if (in.kind == 5) {
            in.geometry = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                                set.projection_geometry;
            const auto identity = reinterpret_cast<std::uintptr_t>(in.geometry.get());
            append(parameters, identity);
            append(parameters, layer.fill.offset3);
            append(parameters, layer.fill.rotation3);
            append(parameters, layer.fill.scale3);
            append(parameters, layer.fill.hardness);
            append(parameters, layer.fill.physical);
            append(parameters, fill_physical_size(layer.fill));
            append(parameters, set.cm_per_native);
          }
        }
      }
    }
    if (in.kind == 5 && (!in.geometry || !in.geometry->tiles.contains(tile)))
      return GpuPreviewUpdate::Pending;
    const bool staged_content =
        (querying && query_source) || stack.queried_sources.contains(node.id) ||
        (layer.kind == LayerKind::Fill && layer.fill.mode == 5) ||
        std::any_of(layer.effects.begin(), layer.effects.end(), [&](const auto &e) {
          return e.enabled &&
                 (e.kind == ContentEffectKind::Paint || e.kind == ContentEffectKind::Fill ||
                  (e.kind == ContentEffectKind::Generator &&
                   e.channel == in.channel.fill_channel) ||
                  (e.kind == ContentEffectKind::Filter && e.channel == in.channel.fill_channel &&
                   e.filter.resource >= 7));
        });
    if (staged_content) {
      auto source_node = node;
      source_node.id = node.id + "/source";
      auto source_input = in;
      source_input.content_pass = 1;
      if (in.raw_linear_source) {
        source_input.channel.value = {1, 1, 1};
        source_node.parameters.replace(
            source_value_offset,
            sizeof(source_input.channel.value),
            reinterpret_cast<const char *>(source_input.channel.value.data()),
            sizeof(source_input.channel.value));
      }
      // Authoring source pixels are independent of layer opacity and ordinary
      // blend modes. Keep the expensive Warp source across those final-blend edits.
      source_input.zero_opacity = in.channel.opacity <= 0;
      append(source_node.parameters, source_input.zero_opacity);
      source_input.channel.opacity = 1;
      source_node.parameters.replace(source_opacity_offset,
                                     sizeof(float),
                                     reinterpret_cast<const char *>(&source_input.channel.opacity),
                                     sizeof(float));
      if (in.channel.blend != LayerBlend::Passthrough) {
        source_input.channel.blend = LayerBlend::Normal;
        source_node.parameters.replace(source_blend_offset,
                                       sizeof(LayerBlend),
                                       reinterpret_cast<const char *>(&source_input.channel.blend),
                                       sizeof(LayerBlend));
      }
      if (in.channel.blend != LayerBlend::Passthrough) {
        source_input.below.clear();
        source_node.dependencies.clear();
        if (!in.group.empty())
          source_node.dependencies.push_back(in.group);
      }
      source_input.constant = (in.kind == 0 ||
                               (in.kind == 9 && GPU_texture_width(in.anchor.image) == 1 &&
                                GPU_texture_height(in.anchor.image) == 1) ||
                               ((in.kind == 3 || in.kind == 11) &&
                                (in.group.empty() || constant_nodes.at(in.group)))) &&
                              (source_input.below.empty() ||
                               constant_nodes.at(source_input.below));
      append(source_node.parameters, source_input.content_pass);
      append(source_node.parameters, source_input.constant);
      constant_nodes[source_node.id] = source_input.constant;
      std::string previous = source_node.id;
      descriptors.push_back(std::move(source_node));
      inputs.push_back(std::move(source_input));
      for (const auto &effect : layer.effects) {
        if (querying && query_effect && effect.id == *query_effect)
          break;
        if (effect.kind == ContentEffectKind::Anchor)
          continue;
        if ((!effect.enabled && effect.kind != ContentEffectKind::LinearColorTransform) ||
            (effect.kind != ContentEffectKind::Paint && effect.channel != in.channel.fill_channel))
          continue;
        GpuStackNode effect_node;
        effect_node.id = node.id + "/effect/" + effect.id.toString().toStdString();
        effect_node.dependencies = {previous};
        Input effect_input;
        effect_input.layer = &layer;
        effect_input.channel = in.channel;
        effect_input.enabled = true;
        effect_input.below = previous;
        effect_input.content_pass = effect.kind == ContentEffectKind::Paint ? 3 : 2;
        append(effect_node.parameters, effect_input.content_pass);
        append(effect_node.parameters, gpu_channel_precision(set, channel));
        if (effect.kind == ContentEffectKind::Paint) {
          effect_input.kind = 7;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          const int c = in.channel.fill_channel;
          effect_input.effect_pixels = c < 5   ? &effect.paint.pixels :
                                       c == 5  ? &effect.paint.ao :
                                       c == 14 ? &effect.paint.displacement :
                                                 &effect.paint.users.at(c - 6);
          effect_input.effect_plane = c < 5 ? c : 2;
          effect_input.effect_key = stack_key + ":" + effect_node.id;
          protected_keys.insert(effect_input.effect_key);
          pages(effect_node, effect_input.effect_pixels->tile(tile), effect_input.effect_plane, 0);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect.blending);
        }
        else if (effect.kind == ContentEffectKind::Fill) {
          effect_input.content_pass = effect.initialize_content ? 5 : 4;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          effect_input.effect_rows = {gpu_effect_row(effect, in.channel.fill_channel)};
          effect_input.effect_rows.front()[1][3] = float(effect.source_coverage);
          effect_input.effect_rows.front()[0][1] = float(LayerBlend::Normal);
          effect_input.effect_rows.front()[0][2] = 1;
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.content_pass);
          append(effect_node.parameters, effect.blending);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect_input.effect_rows.front());
          effect_input.constant = constant_nodes.at(previous);
          if (effect.bitmap_fill) {
            const auto &mapping = effect.bitmap_fill->projection;
            effect_input.effect_fill = &mapping;
            effect_input.kind = 2;
            effect_input.content_pass = effect.initialize_content ? 7 : 6;
            effect_input.constant = false;
            const auto key = resource_key(*mapping.texture);
            protected_keys.insert(key);
            effect_node.parameters += key;
            const double u = (tile - 1001) % 10, v = (tile - 1001) / 10;
            const auto a = fill_uv_map(mapping, u, v), b = fill_uv_map(mapping, u + 1, v),
                       c = fill_uv_map(mapping, u, v + 1);
            effect_input.map_u = {float(b[0] - a[0]), float(c[0] - a[0]), float(a[0])};
            effect_input.map_v = {float(b[1] - a[1]), float(c[1] - a[1]), float(a[1])};
            append(effect_node.parameters, effect_input.content_pass);
            append(effect_node.parameters, effect_input.map_u);
            append(effect_node.parameters, effect_input.map_v);
            if (effect.linear_color) {
              effect_input.channel.value = {1, 1, 1};
              LevelsEffect transform;
              transform.kind = ContentEffectKind::LinearColorTransform;
              transform.linear_color = effect.linear_color;
              effect_input.effect_rows.push_back(gpu_effect_row(transform, 0));
              effect_input.content_effects = 2;
              append(effect_node.parameters, effect_input.effect_rows.back());
            }
          }
        }
        else if (effect.kind == ContentEffectKind::Generator) {
          effect_input.content_pass = 4;
          effect_input.channel.blend = effect.blending;
          effect_input.channel.opacity = effect.opacity;
          effect_input.channel.value = content_generator_color(effect);
          append(effect_node.parameters, effect_input.channel.value);
          effect_input.effect_rows = {gpu_generator_row(effect, set.mesh_maps)};
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.content_pass);
          append(effect_node.parameters, effect.blending);
          append(effect_node.parameters, effect.opacity);
          append(effect_node.parameters, effect_input.effect_rows.front());
          effect_input.constant = !generator_resources_present(set.mesh_maps, effect) &&
                                  !generator_has_anchors(effect) && constant_nodes.at(previous);
          if (effect.triplanar_grunge || effect.surface_fallback) {
            effect_input.geometry = set.projection_tiles.contains(tile) ?
                                        set.projection_tiles.at(tile) :
                                        set.projection_geometry;
            effect_input.constant = false;
            append(effect_node.parameters,
                   reinterpret_cast<std::uintptr_t>(effect_input.geometry.get()));
          }
          if (generator_has_anchors(effect)) {
            auto values = gpu_generator_inputs(effect, layer.id, resolve_anchor);
            if (!values)
              return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
            effect_input.generator = effect;
            effect_input.generator_inputs = *values;
            gpu_generator_options(effect_input.effect_rows.front(), effect, *values);
            int slot = 1;
            gpu_generator_dependencies(effect_node, effect, *values, slot);
            append(effect_node.parameters, effect_input.effect_rows.front());
          }
          std::array<bool, 6> needed{};
          gpu_need_generator_maps(needed, effect);
          for (int i = 0; i < 6; ++i)
            if (needed[i])
              if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map)
                if (const auto member = map->tiles.find(tile);
                    member != map->tiles.end() && member->second.pixels)
                {
                  effect_input.mask_planes[i] = &member->second;
                  const auto &pixels = *member->second.pixels;
                  const auto key = "baked:" + pixels.hash + ":" +
                                   std::to_string(pixels.resolution) + ":" +
                                   std::to_string(pixels.height);
                  protected_keys.insert(key);
                  effect_node.parameters += key;
                  effect_input.mask_signature += key;
                }
        }
        else {
          effect_input.effect_rows = {gpu_effect_row(effect, in.channel.fill_channel)};
          effect_input.content_effects = 1;
          append(effect_node.parameters, effect_input.effect_rows.front());
          if (effect.kind == ContentEffectKind::LinearColorTransform)
            append(effect_node.parameters, effect_input.channel.value);
          effect_input.constant = constant_nodes.at(previous);
        }
        append(effect_node.parameters, effect_input.constant);
        constant_nodes[effect_node.id] = effect_input.constant;
        previous = effect_node.id;
        descriptors.push_back(std::move(effect_node));
        inputs.push_back(std::move(effect_input));
      }
      if (querying) {
        query_head = previous;
        break;
      }
      in.kind = 6;
      in.group = previous;
      node.dependencies.clear();
      if (!in.below.empty())
        node.dependencies.push_back(in.below);
      node.dependencies.push_back(previous);
      node.pages.clear();
    }
    if (!staged_content)
      in.effect_rows = gpu_content_effects(layer, in.channel.fill_channel);
    in.content_effects = int(in.effect_rows.size());
    in.mask_stages = mask_builder.build(layer);
    if (in.mask_stages.empty())
      gpu_append_mask_effects(in.effect_rows, layer, set, tile);
    append(parameters, in.content_effects);
    for (const auto &row : in.effect_rows)
      append(parameters, row);
    const bool masked = layer.mask && layer.mask->enabled;
    append(parameters, masked);
    if (masked) {
      append(parameters, layer.mask->background);
      protected_keys.insert(paint_key(set, layer, 2, true, tile));
      pages(node, layer.mask->pixels.tile(tile), 2, 1);
      bool comparisons_ready = true;
      int slot = 2;
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(stage, [&](const auto &part) {
          parameters += part.owner_key + part.id + part.anchor_key + part.anchor_id;
          if (part.bitmap)
            parameters += part.bitmap->asset + part.bitmap->hash;
          append(parameters, part.kind);
          append(parameters, part.background);
          append(parameters, part.opacity);
          append(parameters, part.blend);
          if (part.kind == GpuMaskStage::AnchorFill) {
            append(parameters, part.reference.channel);
            append(parameters, part.reference.component);
            append(parameters, part.reference.coverage);
          }
          if (part.generator) {
            auto values = gpu_generator_inputs(
                *part.generator, part.comparison_layer, resolve_anchor);
            if (!values)
              comparisons_ready = false;
            else
              gpu_generator_dependencies(node, *part.generator, *values, slot);
          }
          if (part.kind == GpuMaskStage::Compare) {
            auto values = resolve(part);
            if (!values)
              comparisons_ready = false;
            else
              gpu_comparison_dependencies(node, *values, slot);
          }
          if (part.kind == GpuMaskStage::AnchorFill && part.content_anchor) {
            append(parameters, part.reference.channel);
            append(parameters, part.reference.component);
            append(parameters, part.reference.coverage);
            auto value = resolve_anchor(part.comparison_layer, part.reference);
            if (!value)
              comparisons_ready = false;
            else
              gpu_anchor_dependencies(node, *value, slot);
          }
          for (const auto &row : part.rows)
            append(parameters, row);
          if (part.spatial) {
            append(parameters, part.spatial->values);
            append(parameters, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
          }
          if (part.pixels)
            pages(node, part.pixels->tile(tile), 2, slot++);
        });
      if (!comparisons_ready)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(
            stage,
            [&](const auto &part) {
              if (part.pixels)
                protected_keys.insert(part.owner_key + ":" + part.id);
            },
            true);
    }
    if (masked) {
      std::array<bool, 6> needed{};
      for (const auto &effect : layer.mask->effects)
        if (effect.enabled) {
          needed[0] |= effect.kind == MaskEffectKind::ColorSelection && effect.use_id_map;
          if (effect.kind == MaskEffectKind::Generator)
            gpu_need_generator_maps(needed, effect);
        }
      for (const auto &stage : in.mask_stages)
        gpu_visit_mask_stage(
            stage,
            [&](const auto &part) {
              for (const auto &row : part.rows) {
                needed[0] |= row[0][0] == 4 && row[1][0] != 0;
                if (row[0][0] == 3)
                  gpu_need_generator_row_maps(needed, row);
              }
            },
            true);
      for (int i = 0; i < 6; ++i)
        if (needed[i]) {
          const auto &map = set.mesh_maps[gpu_mask_map_roles[i]];
          if (map)
            if (auto found = map->tiles.find(tile);
                found != map->tiles.end() && found->second.pixels)
            {
              in.mask_planes[i] = &found->second;
              const auto &pixels = *found->second.pixels;
              const auto key = "baked:" + pixels.hash + ":" + std::to_string(pixels.resolution) +
                               ":" + std::to_string(pixels.height);
              protected_keys.insert(key);
              parameters += key;
              in.mask_signature += key;
            }
        }
    }
    if (layer.geometry.type == 0 && !layer.geometry.meshes.empty()) {
      if (auto found = set.geometry_ownership.find(tile); found != set.geometry_ownership.end())
        in.ownership = found->second;
      if (!in.ownership || in.ownership->width != width || in.ownership->height != height)
        return GpuPreviewUpdate::Pending;
      append(parameters, reinterpret_cast<std::uintptr_t>(in.ownership.get()));
      append(parameters, layer.geometry.default_include);
      for (const auto &[id, name] : set.geometry_names) {
        append(parameters, id);
        append(parameters, layer.geometry.includes(name));
      }
    }
    // Uniform prefixes are represented by one GPU texel, never by a CPU blend.
    // An opaque normal/replacement fill has no dependency on hidden lower work.
    const bool unmasked = (!layer.mask || !layer.mask->enabled) &&
                          (layer.geometry.type == 1 || layer.geometry.meshes.empty());
    const bool uniform_source = in.kind == 0 ||
                                (in.kind == 9 && GPU_texture_width(in.anchor.image) == 1 &&
                                 GPU_texture_height(in.anchor.image) == 1) ||
                                ((in.kind == 3 || in.kind == 6 || in.kind == 11) &&
                                 (in.group.empty() || constant_nodes.at(in.group)));
    if (unmasked && uniform_source) {
      // A source query (for example a thumbnail) can stage a uniform fill as
      // kind 6. Its RGB effects preserve full coverage; keep the child dependency
      // while pruning the hidden backdrop. Fractional folders are not opaque fills.
      if (in.enabled && (in.kind == 0 || (in.kind == 6 && layer.kind == LayerKind::Fill)) &&
          (in.channel.blend == LayerBlend::Replace ||
           (in.channel.blend == LayerBlend::Normal && in.channel.opacity == 1)))
      {
        in.below.clear();
        node.dependencies.clear();
        if (!in.group.empty())
          node.dependencies.push_back(in.group);
      }
      in.constant = in.below.empty() || constant_nodes.at(in.below);
    }
    append(parameters, in.constant);
    constant_nodes[node.id] = in.constant;
    heads[layer.depth] = node.id;
    descriptors.push_back(std::move(node));
    inputs.push_back(std::move(in));
  }
  if (query_layer && query_layer->isNull()) {
    query_head = heads[0];
    query_enabled = gpu_channel_enabled(set, channel);
  }
  GpuStackNode final_node;
  final_node.id = query_layer ? query_id : "output";
  const auto output_head = query_layer ? query_head : heads[0];
  if (!output_head.empty())
    final_node.dependencies.push_back(output_head);
  append(final_node.parameters, query_enabled);
  const auto base = gpu_channel_base(set, channel);
  append(final_node.parameters, base);
  append(final_node.parameters, gpu_channel_precision(set, channel));
  append(final_node.parameters, reinterpret_cast<std::uintptr_t>(margin.get()));
  descriptors.push_back(std::move(final_node));
  // Inactive folders disconnect their children. Do not allocate/evaluate a
  // prefix that cannot contribute to the visible channel output.
  std::vector<std::string> logical_order;
  for (const auto &node : descriptors)
    logical_order.push_back(node.id);
  std::set<std::string> required{query_layer ? query_id : "output"};
  for (auto node = descriptors.rbegin(); node != descriptors.rend(); ++node)
    if (required.contains(node->id))
      required.insert(node->dependencies.begin(), node->dependencies.end());
  for (std::size_t i = inputs.size(); i-- > 0;)
    if (!required.contains(descriptors[i].id)) {
      inputs.erase(inputs.begin() + i);
      descriptors.erase(descriptors.begin() + i);
    }
  for (std::size_t i = 0; i < inputs.size(); ++i)
    stack.node_owners.try_emplace(descriptors[i].id, inputs[i].layer->id);
  auto transaction = stack.plan.prepare(width, height, descriptors);
  std::set<std::string> live;
  for (const auto &node : descriptors)
    live.insert(node.id);
  if (query_layer)
    stack.queries[query_id] = {query_head, query_enabled, *query_layer, live};
  else {
    std::set<QUuid> owners;
    for (const auto &layer : evaluation_layers(set))
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.enabled && effect.kind == MaskEffectKind::Compare &&
              (effect.left < 2 || effect.right < 2))
            owners.insert(layer.id);
    std::erase_if(stack.queries, [&](const auto &item) {
      return item.first.ends_with("/thumbnail") || !owners.contains(item.second.owner);
    });
  }
  for (const auto &[id, query] : stack.queries)
    live.insert(query.nodes.begin(), query.nodes.end());
  // Keep auxiliary Compare/Anchor query closures: they are sampled inputs,
  // not edges in this compositor transaction. The snapshot seed already owns
  // only below-layer nodes, including when an auxiliary query defers early.
  if (query_layer && !query_source && !raw_anchor) {
    if (auto frozen = s.adjustment_sources.find(set.id);
        frozen != s.adjustment_sources.end() && frozen->second.layer == *query_layer)
    {
      std::erase_if(stack.outputs, [&](const auto &item) { return !live.contains(item.first); });
      std::erase_if(stack.node_versions,
                    [&](const auto &item) { return !live.contains(item.first); });
      std::erase_if(stack.node_owners,
                    [&](const auto &item) { return !live.contains(item.first); });
      stack.plan.retain(live);
      stack.head = nullptr;
    }
  }
  if (!query_layer) {
    // Occlusion is not invalidation. Keep already computed lower/folder prefixes
    // so the first opacity/visibility edit can reuse them. Never evaluate hidden
    // work just to populate this cache. Prefer the nearest complete prefix and
    // admit inactive storage against current free VRAM, not a fixed byte cap.
    const auto budget = source_cache_budget(0);
    std::uint64_t retained_bytes = 0, retained_nodes = 0;
    for (auto id = logical_order.rbegin(); id != logical_order.rend(); ++id) {
      if (live.contains(*id))
        continue;
      auto found = stack.outputs.find(*id);
      if (found == stack.outputs.end())
        continue;
      const auto bytes = std::uint64_t(GPU_texture_width(found->second.get())) *
                         GPU_texture_height(found->second.get()) * 16;
      if (bytes <= budget - retained_bytes) {
        live.insert(*id);
        retained_bytes += bytes;
        ++retained_nodes;
      }
    }
    if (performance::enabled() && retained_nodes)
      std::printf("BTS_GPU_PREFIX_RETAIN nodes=%llu bytes=%llu budget=%llu\n",
                  (unsigned long long)retained_nodes,
                  (unsigned long long)retained_bytes,
                  (unsigned long long)budget);
    std::erase_if(stack.outputs, [&](const auto &item) { return !live.contains(item.first); });
    // Upload can defer below. Retire revision records now, together with their
    // textures, so an intervening source query cannot reuse a freed output.
    stack.plan.retain(live);
  }
  // Resolve/uploads before committing any new revision. A partially uploaded
  // resource leaves the previous full-resolution result untouched.
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    auto &in = inputs[index];
    const auto &layer = *in.layer;
    if (transaction.work[index].regions.empty())
      continue;
    in.ownership_image = s.geometry_masks.empty();
    in.selection_image = s.geometry_masks.empty();
    if (in.ownership) {
      in.ownership_image = s.geometry_masks.atlas(
          in.ownership,
          s.remaining,
          testing_stack ? performance::Clock::time_point::max() :
                          s.upload_start + std::chrono::milliseconds(4));
      if (!in.ownership_image)
        return GpuPreviewUpdate::Pending;
      in.selection_image = s.geometry_masks.selection(
          stack_key + ":" + descriptors[index].id, set.geometry_names, layer.geometry);
    }

    for (int i = 0; i < 6; ++i) {
      in.mask_maps[i] = s.baked(in.mask_planes[i], protected_keys);
      if (!in.mask_maps[i])
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    if (in.generator)
      gpu_generator_maps(in.mask_maps, *in.generator, in.generator_inputs);
    if (in.kind != 12)
      in.source = s.zero.get();
    in.mask = s.zero.get();
    in.surface = s.surface({}, tile);
    in.effects = s.effects(stack_key + ":" + descriptors[index].id, in.effect_rows);
    if (!in.effects)
      return GpuPreviewUpdate::Unsupported;
    if (in.enabled && in.kind == 10) {
      if (in.geometry) {
        in.native_context->surface = s.surface(in.geometry, tile);
        if (!in.native_context->surface)
          return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
        in.native_context->surface_size = {in.geometry->resolution,
                                           in.geometry->height ? in.geometry->height :
                                                                 in.geometry->resolution};
        in.native_context->surface_stride = sizeof(FillSurfacePoint) / 4;
      }
      if (in.native_ao) {
        in.native_context->ambient_occlusion = s.baked(in.native_ao, protected_keys);
        if (!in.native_context->ambient_occlusion)
          return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      }
    }
    else if (in.enabled && in.kind == 9)
      in.source = in.anchor.image;
    else if (in.enabled && in.kind == 8 && layer.quick_overlay->field)
      in.source = s.paint(paint_key(set, layer, 0, false, tile),
                          layer.quick_overlay->field->pixels.tile(tile),
                          2,
                          protected_keys);
    else if (in.enabled && in.kind == 7)
      in.source = s.paint(
          in.effect_key, in.effect_pixels->tile(tile), in.effect_plane, protected_keys);
    else if (in.enabled && in.kind == 1)
      in.source = s.paint(paint_key(set, layer, channel, false, tile),
                          in.channel.pixels->tile(tile),
                          in.channel.plane,
                          protected_keys);
    else if (in.enabled && in.effect_fill)
      in.source = s.resource(*in.effect_fill->texture, protected_keys);
    else if (in.enabled && (in.kind == 2 || in.kind == 5) &&
             fill_channel_texture(layer.fill, in.channel.fill_channel))
      in.source = s.resource(*tile_resource(layer.fill, in.channel.fill_channel, tile),
                             protected_keys);
    if (in.enabled && (in.kind == 5 || (in.content_pass == 4 && in.geometry))) {
      in.surface = s.surface(in.geometry, tile);
      if (!in.surface)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    if (in.kind == 12 && !in.source) {
      if (const auto error = s.native_normal_errors.find(set.id);
          error != s.native_normal_errors.end())
      {
        s.native_image_failure = error->second;
        s.input_rejected = true;
        return GpuPreviewUpdate::Unsupported;
      }
      return GpuPreviewUpdate::Pending;
    }
    if (!in.source)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    if (!in.content_pass && in.enabled && layer.mask && layer.mask->enabled) {
      in.mask = in.mask_stages.empty() ? s.paint(paint_key(set, layer, 2, true, tile),
                                                 layer.mask->pixels.tile(tile),
                                                 2,
                                                 protected_keys) :
                                         s.mask_prefix(paint_key(set, layer, 2, true, tile),
                                                       in.mask_stages,
                                                       in.mask_maps,
                                                       tile,
                                                       width,
                                                       height,
                                                       protected_keys,
                                                       in.mask_signature,
                                                       resolve,
                                                       resolve_anchor);
      if (!in.mask)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
  }
  s.last_dispatch_texels = transaction.texels;
  s.last_cached_nodes = 0;
  s.last_active_nodes = inputs.size();
  const auto region_uniforms = [](gpu::Shader *shader, const GpuStackRegion &r) {
    const int origin[2] = {r.x, r.y}, size[2] = {r.width, r.height};
    GPU_shader_uniform_2iv(shader, "region_origin", origin);
    GPU_shader_uniform_2iv(shader, "region_size", size);
  };
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const auto &work = transaction.work[index];
    if (work.regions.empty()) {
      ++s.last_cached_nodes;
      continue;
    }
    const auto &in = inputs[index];
    const auto &layer = *in.layer;
    const int output_width = in.constant ? 1 : width, output_height = in.constant ? 1 : height;
    if (!stack.outputs.contains(work.id) ||
        GPU_texture_width(stack.outputs.at(work.id).get()) != output_width ||
        GPU_texture_height(stack.outputs.at(work.id).get()) != output_height)
    {
      auto output = texture("BTS GPU cached layer prefix",
                            output_width,
                            output_height,
                            gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!output)
        return GpuPreviewUpdate::Unsupported;
      stack.outputs.insert_or_assign(work.id, std::move(output));
    }
    // A stroke snapshot can share an unchanged cached prefix. Detach on the GPU
    // before writing; never let later lower-layer edits mutate its read source.
    if (stack.outputs.at(work.id).use_count() > 1) {
      auto replacement = texture("BTS COW frozen prefix",
                                 output_width,
                                 output_height,
                                 gpu::TextureFormat::SFLOAT_32_32_32_32);
      if (!replacement)
        return GpuPreviewUpdate::Unsupported;
      GPU_texture_copy(replacement.get(), stack.outputs.at(work.id).get());
      ++s.frozen_prefix_copies;
      s.frozen_prefix_copy_texels += std::uint64_t(output_width) * output_height;
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                         GPU_BARRIER_TEXTURE_UPDATE);
      stack.outputs.at(work.id) = std::move(replacement);
    }
    auto *output = stack.outputs.at(work.id).get();
    auto *backdrop = in.below.empty() ? s.zero.get() : stack.outputs.at(in.below).get();
    auto *source = (in.kind == 3 || in.kind == 6 || in.kind == 11) ?
                       (in.group.empty() ? s.zero.get() : stack.outputs.at(in.group).get()) :
                       in.source;
    if (in.kind == 10) {
      if (!s.native_source || GPU_texture_width(s.native_source.get()) != width ||
          GPU_texture_height(s.native_source.get()) != height)
      {
        s.native_source = texture("BTS shared native source scratch",
                                  width,
                                  height,
                                  gpu::TextureFormat::SFLOAT_32_32_32_32);
        if (!s.native_source)
          return GpuPreviewUpdate::Unsupported;
      }
      performance::Scope timing("gpu_native_material_source");
      try {
        for (const auto &region : work.regions)
          s.native_materials.render(layer.fill.material_resource,
                                    in.channel.fill_channel,
                                    *layer.fill.native_material,
                                    s.native_source.get(),
                                    *in.native_context,
                                    {region.x, region.y, region.width, region.height});
      }
      catch (const NativeFragmentImagePending &) {
        return GpuPreviewUpdate::Pending;
      }
      catch (const std::exception &error) {
        s.native_image_failure = error.what();
        s.input_rejected = true;
        return GpuPreviewUpdate::Unsupported;
      }
      source = s.native_source.get();
    }
    GpuLayerParameters parameters;
    auto *shader = s.layer_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(source, 0);
    GPU_texture_bind(in.mask, 1);
    GPU_texture_bind(backdrop, 2);
    GPU_texture_bind(in.surface, 3);
    GPU_texture_bind(in.effects, 4);
    for (int i = 0; i < 6; ++i)
      GPU_texture_bind(in.mask_maps[i], gpu_mask_map_slot(i));
    GPU_texture_bind(in.ownership_image, 9);
    GPU_texture_bind(in.selection_image, 10);
    GPU_texture_image_bind(output, 0);
    parameters.set("precompose_source", layer.precompose_below);
    parameters.set("raw_linear_source", in.raw_linear_source);
    parameters.set("source_zero_opacity", in.zero_opacity);
    parameters.set("content_pass", in.content_pass);
    parameters.set("geometry_masked", bool(in.ownership));
    parameters.set("content_effects", in.content_effects);
    parameters.set("effect_count", int(in.effect_rows.size()));
    if (in.kind == 9) {
      const auto &ref = layer.fill.anchors.at(in.channel.fill_channel);
      const float options[4] = {float(ref.component),
                                float(ref.coverage),
                                float(in.anchor.mask),
                                float(in.anchor.enabled)};
      parameters.set4("anchor_options", options);
    }
    parameters.set("quick_editing", layer.quick_overlay && layer.quick_overlay->editing);
    parameters.set("quick_inverted",
                   layer.quick_overlay && layer.quick_overlay->field &&
                       layer.quick_overlay->field->inverted);
    parameters.set("fill_bump_bias", in.channel.fill_channel == 4 ? layer.fill.bump_bias : 0);
    parameters.set("source_ignore_alpha",
                   in.effect_fill || !layer.fill.alpha_mask.at(in.channel.fill_channel));
    parameters.set("source_wrap", !in.effect_fill && layer.fill.mode == 6);
    parameters.set("source_filter",
                   in.effect_fill       ? 0 :
                   layer.fill.mode >= 2 ? layer.fill.filtering :
                                          0);
    parameters.set("fill_resource_kind",
                   in.effect_fill || fill_channel_texture(layer.fill, in.channel.fill_channel) ?
                       1 :
                   channel == 0 && layer.fill.atom ? 2 :
                                                     0);
    parameters.set("projection_mode", layer.fill.mode);
    parameters.set("projection_wrap", layer.fill.wrap);
    const float atom_options[4] = {float(layer.fill.border_width),
                                   float(layer.fill.middle_dot),
                                   float(layer.fill.border_dots),
                                   float(layer.fill.invert)};
    parameters.set4("atom_options", atom_options);
    parameters.set("layer_active", in.enabled);
    parameters.set("source_kind", in.kind);
    parameters.set("scalar_channel",
                   channel == 6                                   ? 3 :
                   channel == 0                                   ? 0 :
                   (channel == 1 || channel == 2 || channel == 5) ? 1 :
                                                                    2);
    parameters.set("value_precision",
                   int(layer.precompose_below ? ChannelPrecision::RGB32F :
                                                gpu_channel_precision(set, channel)));
    parameters.set("blend_mode", int(in.channel.blend));
    parameters.set("opacity_value", in.channel.opacity);
    const auto value = in.channel.value;
    parameters.set3("source_value", value.data());
    parameters.set("has_mask",
                   !in.content_pass &&
                       (!in.mask_stages.empty() ||
                        (layer.mask && layer.mask->enabled && !layer.mask->pixels.empty())));
    parameters.set("mask_background",
                   in.content_pass                   ? 1 :
                   !in.mask_stages.empty()           ? 0 :
                   layer.mask && layer.mask->enabled ? layer.mask->background :
                                                       1);
    parameters.set3("map_u", in.map_u.data());
    parameters.set3("map_v", in.map_v.data());
    gpu_spatial_parameters(
        parameters, layer.fill, in.kind == 5 ? in.geometry.get() : nullptr, set);
    if (in.content_pass == 4 && in.geometry)
      gpu_generator_surface_parameters(parameters, in.geometry.get());
    auto *parameter_image = s.parameters(stack_key + ":" + work.id, parameters);
    if (!parameter_image)
      return GpuPreviewUpdate::Unsupported;
    GPU_texture_bind(parameter_image, 5);
    if (in.constant) {
      region_uniforms(shader, {0, 0, 1, 1});
      GPU_compute_dispatch(shader, 1, 1, 1);
    }
    else
      for (const auto &r : work.regions) {
        region_uniforms(shader, r);
        GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
      }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
    GPU_texture_image_unbind(output);
  }
  // The versions belong to GPU output chunks, not to CPU source pixels.
  for (const auto &work : transaction.work)
    if (!work.regions.empty()) {
      const auto version = ++stack.node_clock;
      for (const auto &r : work.regions) {
        const auto chunk = std::uint64_t(r.y / 256) * ((width + 255) / 256) + r.x / 256;
        stack.node_versions[work.id][chunk] = {r.x, r.y, r.width, r.height, version};
      }
    }
  if (query_layer) {
    GPU_texture_unbind_all();
    GPU_texture_image_unbind_all();
    GPU_shader_unbind();
    stack.plan.commit(std::move(transaction), true);
    return GpuPreviewUpdate::Updated;
  }
  auto final_work = transaction.work.back();
  const int chunk_columns = (width + 255) / 256;
  if (!final_work.regions.empty()) {
    ++stack.revision;
    stack.last_changes.clear();
    for (const auto &r : final_work.regions)
      stack.last_changes.insert((r.y / 256) * chunk_columns + r.x / 256);
  }
  for (const auto &r : final_work.regions) {
    const int index = (r.y / 256) * chunk_columns + r.x / 256;
    stack.output_dirty.insert(index);
    stack.height_dirty.insert(index);
  }
  stack.head = heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get();
  if ((channel == 5 || channel == 6) && !final_work.regions.empty()) {
    if (!s.coverage_shader)
      s.coverage_shader = Shader(gpu_derived::coverage_shader(), GPU_shader_free);
    if (!stack.coverage)
      stack.coverage = texture("BTS cached AO coverage",
                               (width + 15) / 16,
                               (height + 15) / 16,
                               gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!s.coverage_shader || !stack.coverage)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.coverage_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(stack.head, 0);
    GPU_texture_image_bind(stack.coverage.get(), 0);
    const int size[2] = {width, height};
    GPU_shader_uniform_2iv(shader, "size", size);
    for (const auto &r : final_work.regions) {
      const int origin[2] = {r.x, r.y};
      GPU_shader_uniform_2iv(shader, "origin", origin);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(stack.coverage.get());
  }
  if (channel == 4 && margin) {
    const auto float_format = gpu::TextureFormat::SFLOAT_32_32_32_32;
    if (!stack.height_image) {
      stack.height_image = texture("BTS padded bump for normals", width, height, float_format);
      if (!stack.height_image)
        return GpuPreviewUpdate::Unsupported;
      for (int i = 0; i < chunk_columns * ((height + 255) / 256); ++i)
        stack.height_dirty.insert(i);
    }
    if (!stack.height_dirty.empty()) {
      if (const auto state = s.initialize(width, height, float_format, false);
          state != GpuPreviewUpdate::Updated)
        return state;
      auto *shader = s.final_shader;
      GPU_shader_bind(shader);
      GPU_texture_bind(stack.head, 0);
      GPU_texture_image_bind(stack.height_image.get(), 0);
      const int size[2] = {width, height}, offset[3] = {};
      GPU_shader_uniform_2iv(shader, "tile_size", size);
      GPU_shader_uniform_3iv(shader, "target_offset", offset);
      GPU_shader_uniform_3fv(shader, "base_value", base.data());
      GPU_shader_uniform_1i(shader, "value_precision", int(gpu_channel_precision(set, channel)));
      GPU_shader_uniform_3fv(shader, "output_bias", std::array<float, 3>{}.data());
      GPU_shader_uniform_1i(shader, "output_kind", 0);
      GPU_shader_uniform_1i(shader, "finite_half", 0);
      std::vector<std::uint32_t> dirty(chunk_columns * ((height + 255) / 256));
      for (auto chunk : stack.height_dirty) {
        dirty[chunk] = 1;
        const int x = chunk % chunk_columns * 256, y = chunk / chunk_columns * 256;
        const GpuStackRegion r{x, y, std::min(256, width - x), std::min(256, height - y)};
        region_uniforms(shader, r);
        GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
      }
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
      GPU_texture_image_unbind_all();
      GPU_texture_unbind_all();
      GPU_shader_unbind();
      if (!s.margins.apply(stack_key + ":height", margin, stack.height_image.get(), {}, dirty))
        return GpuPreviewUpdate::Unsupported;
      stack.height_dirty.clear();
      if (const auto state = s.initialize(width, height, format, GPU_texture_is_array(target));
          state != GpuPreviewUpdate::Updated)
        return state;
    }
  }
  if (!prefix_only) {
    final_work.regions.clear();
    if (target_changed)
      for (int i = 0; i < chunk_columns * ((height + 255) / 256); ++i)
        stack.output_dirty.insert(i);
    for (auto chunk : stack.output_dirty) {
      const int x = chunk % chunk_columns * 256, y = chunk / chunk_columns * 256;
      final_work.regions.push_back({x, y, std::min(256, width - x), std::min(256, height - y)});
    }
  }
  if (!prefix_only && (channel != 5 || authored_ao_output) && !final_work.regions.empty()) {
    auto *shader = target_final_shader;
    GPU_shader_bind(shader);
    GPU_texture_bind(heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get(), 0);
    GPU_texture_image_bind(target, 0);
    const int logical_size[2] = {width, height};
    GPU_shader_uniform_2iv(shader, "tile_size", logical_size);
    GPU_shader_uniform_3iv(shader, "target_offset", target_offset.data());
    GPU_shader_uniform_3fv(shader, "base_value", base.data());
    GPU_shader_uniform_1i(shader, "value_precision", int(gpu_channel_precision(set, channel)));
    const float bump_bias = channel == 4 ? set.bump_offset() : 0;
    GPU_shader_uniform_3fv(
        shader, "output_bias", std::array<float, 3>{bump_bias, bump_bias, bump_bias}.data());
    GPU_shader_uniform_1i(shader,
                          "output_kind",
                          channel == 6                ? 2 :
                          gpu_scalar_channel(channel) ? 1 :
                                                        0);
    GPU_shader_uniform_1i(shader, "finite_half", format == gpu::TextureFormat::SFLOAT_16_16_16_16);
    for (const auto &r : final_work.regions) {
      region_uniforms(shader, r);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(target);
    if (margin) {
      std::vector<std::uint32_t> dirty(chunk_columns * ((height + 255) / 256));
      for (auto chunk : stack.output_dirty)
        dirty[chunk] = 1;
      if (!s.margins.apply(
              stack_key + ":output:" + std::to_string(reinterpret_cast<std::uintptr_t>(target)),
              margin,
              target,
              target_offset,
              dirty))
        return GpuPreviewUpdate::Unsupported;
    }
    stack.output_dirty.clear();
    s.image_changed = true;
  }
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  stack.plan.commit(std::move(transaction), true);
  stack.plan.retain(live);
  std::erase_if(stack.node_versions,
                [&](const auto &entry) { return !live.contains(entry.first); });
  if (!prefix_only)
    s.target_owners[target_key] = stack_key;
  if (!benchmark_stack && !prefix_only && channel != 5 && !margin &&
      std::getenv("BTS_VERIFY_GPU_STACK") &&
      !std::any_of(evaluation_layers(set).begin(),
                   evaluation_layers(set).end(),
                   [](const auto &layer) { return bool(layer.fill.native_material); }))
  {
    std::string signature = set.id.toString().toStdString() + std::to_string(channel) + ":" +
                            std::to_string(tile);
    for (const auto &layer : evaluation_layers(set))
      if (active(layer, channel, tile)) {
        signature += std::to_string(int(layer.kind)) + ":" +
                     std::to_string(int(gpu_layer_channel(layer, channel).blend)) + ":" +
                     std::to_string(gpu_layer_channel(layer, channel).opacity < 1) + ":" +
                     std::to_string(bool(layer.mask)) + ":" +
                     std::to_string(layer.mask && !layer.mask->pixels.empty()) + ":" +
                     std::to_string(layer.fill.scale != std::array<double, 2>{1, 1}) + ":" +
                     std::to_string(layer.fill.rotation != 0) + ":" +
                     std::to_string(layer.fill.offset != std::array<double, 2>{});
      }
    if (std::any_of(
            evaluation_layers(set).begin(), evaluation_layers(set).end(), [](const auto &layer) {
              return layer.fill.mode == 5 ||
                     (layer.mask && any_mask_effect(layer.mask->effects, [](const auto &effect) {
                        return effect.enabled && effect.kind == MaskEffectKind::Fill &&
                               effect.mapping.projection.mode == 5;
                      }));
            }))
      signature += ":warp:" + std::to_string(stack.revision);
    static std::set<std::string> verified;
    const bool neighborhood =
        spatial_oracle_unavailable(set) ||
        std::any_of(
            evaluation_layers(set).begin(), evaluation_layers(set).end(), [&](const auto &layer) {
              const bool content = std::any_of(
                  layer.effects.begin(), layer.effects.end(), [&](const auto &effect) {
                    return effect.enabled && effect.kind == ContentEffectKind::Filter &&
                           effect.filter.resource >= 7 &&
                           effect.channel == gpu_layer_channel(layer, channel).fill_channel;
                  });
              const bool mask = layer.mask &&
                                std::any_of(layer.mask->effects.begin(),
                                            layer.mask->effects.end(),
                                            [](const auto &effect) {
                                              return effect.enabled &&
                                                     effect.kind == MaskEffectKind::Filter &&
                                                     effect.filter.resource >= 7;
                                            });
              return content || mask;
            });
    if (neighborhood)
      std::printf("BTS_GPU_STACK_NEIGHBORHOOD skipped_cpu_oracle=1\n");
    else if (testing_stack || verified.insert(signature).second) {
      auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
      double error = 0;
      bool valid = true;
      std::array<bool, 6> wanted{};
      if (channel < 6)
        wanted[channel] = true;
      else if (channel == 6)
        wanted[3] = true;
      for (int gy = 0; gy < 8; ++gy)
        for (int gx = 0; gx < 8; ++gx) {
          const int x = std::min(s.width - 1, int((gx + .5) * s.width / 8)),
                    y = std::min(s.height - 1, int((gy + .5) * s.height / 8));
          std::array<float, 4> expected{};
          if (channel == 6) {
            const auto result = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
            const auto full = precision_channel(result.material, 3, set.precision[3]);
            expected = {full[0], full[1], full[2], 1};
          }
          else if (gpu_scalar_channel(channel)) {
            const auto value = channel == 16 ?
                                   TextureDocument::composite_displacement(set, x, y, tile) :
                                   TextureDocument::composite_user(set, channel - 7, x, y, tile);
            expected = {value[0] * value[1], value[0] * value[1], value[0] * value[1], value[1]};
          }
          else {
            const auto result = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
            const auto full = precision_channel(
                result.material, channel, gpu_channel_precision(set, channel));
            const float a = result.coverage[channel];
            expected = {full[0] - base[0] * (1 - a),
                        full[1] - base[1] * (1 - a),
                        full[2] - base[2] * (1 - a),
                        a};
          }
          for (int k = 0; k < 4; ++k) {
            if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
              expected[k] = math::half_to_float(math::float_to_half_make_finite(expected[k]));
            const float actual =
                pixels[((std::size_t(target_offset[2]) * GPU_texture_height(target) + y +
                         target_offset[1]) *
                            GPU_texture_width(target) +
                        x + target_offset[0]) *
                           4 +
                       k];
            const double difference = std::abs(double(actual) - expected[k]);
            const auto precision = gpu_channel_precision(set, channel);
            double tolerance = (format == gpu::TextureFormat::SFLOAT_32_32_32_32 ? 1e-6 : .001) *
                               std::max(1.f, std::abs(expected[k]));
            if (k < 3 &&
                (precision == ChannelPrecision::SRGB8 || precision == ChannelPrecision::RGB8 ||
                 precision == ChannelPrecision::L8))
              tolerance += precision == ChannelPrecision::SRGB8 ? .009 : 1. / 255;
            if (valid && difference > tolerance && channel == 4) {
              const auto cpu = TextureDocument::evaluate_channels(set, wanted, x, y, tile);
              auto *input = heads[0].empty() ? s.zero.get() : stack.outputs.at(heads[0]).get();
              const std::array<std::array<int, 3>, 1> point{
                  {{std::min(x, GPU_texture_width(input) - 1),
                    std::min(y, GPU_texture_height(input) - 1),
                    0}}};
              const auto raw = probe_gpu_texture(input, point).front();
              std::fprintf(
                  stderr,
                  "BTS_GPU_BUMP_FAILURE_RAW cpu=%.12g cpu_coverage=%.12g gpu_prefix=%.12g,%.12g,%.12g,%.12g base=%.12g bias=%.12g precision=%d target_format=%d\n",
                  cpu.material.bump,
                  cpu.coverage[4],
                  raw[0],
                  raw[1],
                  raw[2],
                  raw[3],
                  base[0],
                  set.bump_offset(),
                  int(precision),
                  int(format));
            }
            if (valid && difference > tolerance)
              s.oracle_position = {x, y};
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_STACK_DIFFERENCE channel=%d mode=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",
                  channel,
                  set.layers.back().fill.mode,
                  x,
                  y,
                  k,
                  expected[k],
                  actual);
            valid &= std::isfinite(actual) && difference <= tolerance;
            error = std::max(error, difference);
          }
        }
      MEM_delete(pixels);
      std::printf("BTS_GPU_STACK_VERIFIED channel=%d probes=64 max_error=%.9g valid=%d\n",
                  channel,
                  error,
                  int(valid));
      if (!valid)
        throw std::runtime_error("GPU layer preview differs from CPU reference");
    }
  }
  std::printf("BTS_GPU_STACK_CACHE dispatch_texels=%llu cached_nodes=%llu nodes=%zu\n",
              (unsigned long long)s.last_dispatch_texels,
              (unsigned long long)s.last_cached_nodes,
              inputs.size());
  std::printf("BTS_GPU_STACK channel=%d layers=%zu texels=%llu source_upload_bytes=%llu\n",
              channel,
              inputs.size(),
              static_cast<unsigned long long>(std::uint64_t(s.width) * s.height),
              static_cast<unsigned long long>(upload_before - s.remaining));
  return GpuPreviewUpdate::Updated;
}
std::optional<GpuComparisonInputs> GpuLayerPreview::comparison_inputs(const TextureSet &set,
                                                                      const GpuMaskStage &stage,
                                                                      gpu::Texture *target,
                                                                      int tile)
{
  auto &s = *impl_;
  if (stage.rows.empty()) {
    s.input_rejected = true;
    return std::nullopt;
  }
  GpuComparisonInputs result;
  for (auto &input : result)
    input.image = s.zero.get();
  const int channel = stage.comparison_channel == 3 ? 6 : stage.comparison_channel;
  const auto &row = stage.rows.front();
  for (int operand = 0; operand < 2; ++operand) {
    if (int(row[1][0]) != operand && int(row[1][1]) != operand)
      continue;
    const auto memo_key = std::to_string(reinterpret_cast<std::uintptr_t>(&set)) + ":" +
                          stage.comparison_layer.toString().toStdString() + ":" +
                          std::to_string(channel) + ":" + std::to_string(tile) + ":" +
                          std::to_string(operand);
    if (const auto cached = s.comparison_memo.find(memo_key); cached != s.comparison_memo.end()) {
      result[operand] = cached->second;
      continue;
    }
    const auto updated = update_tile(
        set, channel, target, tile, {}, true, &stage.comparison_layer, operand == 1);
    if (updated != GpuPreviewUpdate::Updated) {
      s.input_rejected = updated == GpuPreviewUpdate::Unsupported;
      return std::nullopt;
    }
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile);
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + stage.comparison_layer.toString().toStdString() +
                          (operand ? "/source" : "/below");
    const auto &query = stack.queries.at(query_id);
    auto &input = result[operand];
    input.key = key + query_id;
    input.enabled = query.enabled;
    input.base = gpu_channel_base(set, channel);
    input.image = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    input.pages = stack.node_versions.at(query_id);
    s.comparison_memo.emplace(memo_key, input);
  }
  return result;
}
std::optional<GpuAnchorInput> GpuLayerPreview::anchor_input(const TextureSet &set,
                                                            const QUuid &consumer,
                                                            const AnchorReference &ref,
                                                            int tile)
{
  auto &s = *impl_;
  GpuAnchorInput input;
  input.image = s.zero.get();
  input.key = "broken:" + ref.source.toString().toStdString();
  if (!TextureDocument::anchor_reference_problem(set, consumer, ref).isEmpty())
    return input;
  const auto point = TextureDocument::anchor_point(set, ref.source);
  if (!point)
    return input;
  const auto memo = std::to_string(reinterpret_cast<std::uintptr_t>(&set)) + ":" +
                    ref.source.toString().toStdString() + ":" + std::to_string(ref.channel) + ":" +
                    std::to_string(tile);
  if (const auto found = s.anchor_memo.find(memo); found != s.anchor_memo.end())
    return found->second;
  if (point->mask) {
    auto *image = mask_texture(set, point->layer, tile, &point->id);
    if (!image)
      return std::nullopt;
    const auto &layers = evaluation_layers(set);
    const auto layer = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == point->layer; });
    const auto key = paint_key(set, *layer, 2, true, tile);
    auto &stack = s.mask_stacks.at(key);
    input.image = image;
    input.key = key + point->id.toString().toStdString();
    input.pages = stack.node_versions[point->id.toString().toStdString()];
    input.enabled = true;
    input.mask = true;
  }
  else {
    const int channel = ref.channel == 3  ? 6 :
                        ref.channel == 14 ? 16 :
                        ref.channel >= 6  ? ref.channel + 1 :
                                            ref.channel;
    const auto updated = update_tile(
        set, channel, s.zero.get(), tile, {}, true, &point->layer, true, &point->id, false, true);
    if (updated != GpuPreviewUpdate::Updated) {
      s.input_rejected = updated == GpuPreviewUpdate::Unsupported;
      return std::nullopt;
    }
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile) + ":anchor:" + point->layer.toString().toStdString();
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + point->layer.toString().toStdString() +
                          "/source/before-selected-effect/anchor/" +
                          point->id.toString().toStdString();
    const auto &query = stack.queries.at(query_id);
    input.image = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    input.key = key + query_id;
    input.pages = stack.node_versions[query_id];
    input.enabled = query.enabled;
  }
  s.anchor_memo.emplace(memo, input);
  return input;
}
GpuPreviewUpdate GpuLayerPreview::histogram(const TextureSet &set,
                                            const HistogramRequest &request,
                                            std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_ui_histogram");
  auto &s = *impl_;
  if (set.tiles.empty())
    return GpuPreviewUpdate::Unsupported;
  const int tile = *set.tiles.begin(), w = set.width(tile), h = set.rows(tile);
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  GpuHistogram::Input input{s.zero.get(), s.zero.get(), s.zero.get()};
  input.owners = input.selection = s.geometry_masks.empty();
  input.width = w;
  input.height = h;
  input.channel = request.channel;
  input.compare = request.compare;
  input.component = request.component;
  input.curve = request.curve;
  if (request.mask) {
    if (!found->mask)
      return GpuPreviewUpdate::Unsupported;
    const auto *effect = find_mask_effect(found->mask->effects, request.effect);
    const auto input_mask = mask_effect_input(*found->mask, request.effect);
    if (!effect || !input_mask)
      return GpuPreviewUpdate::Unsupported;
    auto prefix = *found;
    prefix.mask = *input_mask;
    const auto selected = *effect;
    GpuMaskBuilder builder(set, tile);
    const auto stages = builder.build(prefix, true);
    std::set<std::string> protected_keys;
    std::array<bool, 6> needed{};
    std::array<gpu::Texture *, 6> maps{};
    std::string signature;
    for (const auto &stage : stages)
      gpu_visit_mask_stage(
          stage,
          [&](const auto &part) {
            if (part.pixels)
              protected_keys.insert(part.owner_key + ":" + part.id);
            for (const auto &row : part.rows) {
              needed[0] |= row[0][0] == 4 && row[1][0] != 0;
              if (row[0][0] == 3)
                gpu_need_generator_row_maps(needed, row);
            }
          },
          true);
    std::array<const MeshMapPlane *, 6> planes{};
    for (int i = 0; i < 6; ++i)
      if (needed[i])
        if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map) {
          const auto member = map->tiles.find(tile);
          if (member != map->tiles.end() && member->second.pixels) {
            planes[i] = &member->second;
            const auto &p = *planes[i]->pixels;
            const auto key = "baked:" + p.hash + ":" + std::to_string(p.resolution) + ":" +
                             std::to_string(p.height);
            protected_keys.insert(key);
            signature += key;
          }
        }
    for (int i = 0; i < 6; ++i) {
      maps[i] = s.baked(planes[i], protected_keys);
      if (!maps[i])
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
      return comparison_inputs(set, stage, s.zero.get(), tile);
    };
    const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer,
                                                 const AnchorReference &ref) {
      return anchor_input(set, consumer, ref, tile);
    };
    input.mask = s.mask_prefix(paint_key(set, *found, 2, true, tile),
                               stages,
                               maps,
                               tile,
                               w,
                               h,
                               protected_keys,
                               signature,
                               resolve,
                               resolve_anchor,
                               true);
    if (!input.mask)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    if (request.compare) {
      GpuMaskStage stage;
      stage.comparison_layer = found->id;
      stage.comparison_channel = selected.compare_channel;
      TextureLayer description;
      description.mask = LayerMask{};
      description.mask->effects = {selected};
      description.mask->effects.back().enabled = true;
      description.mask->effects.back().blending = LayerBlend::Normal;
      gpu_append_mask_effects(stage.rows, description, set, tile);
      if (stage.rows.empty())
        return GpuPreviewUpdate::Unsupported;
      const auto values = resolve(stage);
      if (!values)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      input.below = (*values)[0].image;
      input.own = (*values)[1].image;
      input.enabled = {(*values)[0].enabled, (*values)[1].enabled};
      input.base = (*values)[0].base;
      input.operands = {selected.left, selected.right};
      input.constant = selected.constant;
      input.channel = selected.compare_channel;
    }
    else {
      input.channel = -1;
      input.component = 0;
    }
  }
  else {
    const int channel = request.channel == 3  ? 6 :
                        request.channel == 14 ? 16 :
                        request.channel >= 6  ? request.channel + 1 :
                                                request.channel;
    if (request.channel >= 6) {
      const auto data = gpu_layer_channel(*found, channel);
      const bool included = found->visible && data.enabled && data.blend != LayerBlend::Disabled &&
                            (found->geometry.type == 1 ? found->geometry.includes_tile(tile) :
                                                         !found->geometry.meshes.empty() ||
                                                             found->geometry.default_include);
      input.inclusion = included ? 0 : 1;
      if (input.inclusion == 0 && found->geometry.type == 0 && !found->geometry.meshes.empty()) {
        const auto ownership = set.geometry_ownership.find(tile);
        if (ownership == set.geometry_ownership.end())
          return GpuPreviewUpdate::Pending;
        input.owners = s.geometry_masks.atlas(ownership->second,
                                              s.remaining,
                                              testing_stack ?
                                                  performance::Clock::time_point::max() :
                                                  s.upload_start + std::chrono::milliseconds(4));
        if (!input.owners)
          return GpuPreviewUpdate::Pending;
        input.selection = s.geometry_masks.selection("histogram:" +
                                                         request.layer.toString().toStdString(),
                                                     set.geometry_names,
                                                     found->geometry);
        input.inclusion = 2;
      }
    }
    const auto updated = update_tile(
        set, channel, s.zero.get(), tile, {}, true, &request.layer, true, &request.effect);
    if (updated != GpuPreviewUpdate::Updated)
      return updated;
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile);
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + request.layer.toString().toStdString() +
                          "/source/before-selected-effect";
    const auto &query = stack.queries.at(query_id);
    input.own = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
  }
  s.histograms.update(request.effect.toString().toStdString(), input, ticket);
  return GpuPreviewUpdate::Updated;
}
gpu::Texture *GpuLayerPreview::mask_texture(const TextureSet &set,
                                            const QUuid &layer,
                                            int tile,
                                            const QUuid *point)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == layer; });
  if (found == layers.end() || !found->mask) {
    s.input_rejected = true;
    return nullptr;
  }
  GpuMaskBuilder builder(set, tile);
  auto stages = builder.build(*found, true);
  if (point) {
    const auto stop = std::find_if(stages.begin(), stages.end(), [&](const auto &stage) {
      return stage.id == point->toString().toStdString();
    });
    if (stop == stages.end()) {
      s.input_rejected = true;
      return nullptr;
    }
    stages.erase(stop + 1, stages.end());
  }
  if (stages.empty()) {
    s.input_rejected = true;
    return nullptr;
  };
  std::set<std::string> protected_keys;
  std::array<bool, 6> needed{};
  std::array<gpu::Texture *, 6> maps{};
  std::string signature;
  for (const auto &stage : stages)
    gpu_visit_mask_stage(
        stage,
        [&](const auto &part) {
          if (part.pixels)
            protected_keys.insert(part.owner_key + ":" + part.id);
          for (const auto &row : part.rows) {
            needed[0] |= row[0][0] == 4 && row[1][0] != 0;
            if (row[0][0] == 3)
              gpu_need_generator_row_maps(needed, row);
          }
        },
        true);
  std::array<const MeshMapPlane *, 6> planes{};
  for (int i = 0; i < 6; ++i)
    if (needed[i])
      if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map) {
        const auto member = map->tiles.find(tile);
        if (member != map->tiles.end() && member->second.pixels) {
          planes[i] = &member->second;
          const auto &p = *planes[i]->pixels;
          const auto key = "baked:" + p.hash + ":" + std::to_string(p.resolution) + ":" +
                           std::to_string(p.height);
          protected_keys.insert(key);
          signature += key;
        }
      }
  for (int i = 0; i < 6; ++i) {
    maps[i] = s.baked(planes[i], protected_keys);
    if (!maps[i])
      return nullptr;
  }
  const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
    return comparison_inputs(set, stage, s.zero.get(), tile);
  };
  const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer, const AnchorReference &ref) {
    return anchor_input(set, consumer, ref, tile);
  };
  const auto key = paint_key(set, *found, 2, true, tile);
  auto *mask = s.mask_prefix(key,
                             stages,
                             maps,
                             tile,
                             w,
                             h,
                             protected_keys,
                             signature,
                             resolve,
                             resolve_anchor,
                             point != nullptr);
  if (!mask)
    return nullptr;
  return mask;
}
GpuPreviewUpdate GpuLayerPreview::sample_material(const TextureSet &set,
                                                  const MaterialSampleRequest &request,
                                                  std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_material_pick");
  auto &s = *impl_;
  if (s.material_samples.pending())
    return GpuPreviewUpdate::Pending;
  const int tile = request.tile;
  if (!set.tiles.contains(tile) || request.x < 0 || request.y < 0 ||
      request.x >= set.width(tile) || request.y >= set.rows(tile))
    return GpuPreviewUpdate::Unsupported;
  const int w = set.width(tile), h = set.rows(tile);
  s.input_rejected = false;
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  std::array<GpuMaterialSample::Input, 6> inputs;
  if (!request.mask.isNull()) {
    const auto &layers = evaluation_layers(set);
    const auto found = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.mask; });
    if (found == layers.end() || !found->mask)
      return GpuPreviewUpdate::Unsupported;
    auto *mask = mask_texture(set, found->id, tile);
    if (!mask)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    inputs[0].image = mask;
  }
  else {
    const QUuid whole_stack;
    for (int c = 0; c < 6; ++c) {
      if (!request.channels[c])
        continue;
      const int channel = c == 3 ? 6 : c;
      const auto state = update_tile(set, channel, s.zero.get(), tile, {}, true, &whole_stack);
      if (state != GpuPreviewUpdate::Updated)
        return state;
      auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                                ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + whole_stack.toString().toStdString() +
                                           "/below");
      inputs[c] = {query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get(),
                   gpu_channel_base(set, channel)};
    }
  }
  s.material_samples.update(request, inputs, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<MaterialSample> GpuLayerPreview::material_sample_result() const
{
  return impl_->material_samples.get();
}
GpuPreviewUpdate GpuLayerPreview::capture_author_chunk(const std::string &key,
                                                       const PaintPixels &pixels,
                                                       int channel,
                                                       int tile,
                                                       int x,
                                                       int y,
                                                       int width,
                                                       int height,
                                                       GpuProjectReadback &readback)
{
  PaintMetadataScope metadata;
  auto &s = *impl_;
  s.input_rejected = false;
  const auto *source = s.paint(key, pixels, channel, {key});
  if (!source)
    return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  readback.submit(0, tile, const_cast<gpu::Texture *>(source), x, y, 0, width, height);
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::capture_layer(const TextureSet &set,
                                                const LayerCaptureRequest &request,
                                                std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_capture");
  auto &s = *impl_;
  s.input_rejected = false;
  if (s.layer_capture.pending())
    return GpuPreviewUpdate::Pending;
  if (!set.tiles.contains(request.tile) || request.channel < 0 ||
      request.channel > (request.final_composite ? 16 : 14) || request.channel == 15)
    return GpuPreviewUpdate::Unsupported;
  const int tile = request.tile, w = set.width(tile), h = set.rows(tile);
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  if (request.final_composite) {
    if (!s.export_final || GPU_texture_width(s.export_final.get()) != w ||
        GPU_texture_height(s.export_final.get()) != h)
      s.export_final = texture(
          "BTS explicit export final GPU channel", w, h, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!s.export_final)
      return GpuPreviewUpdate::Unsupported;
    const int channel = request.normal_perturbation ? 3 : request.channel;
    GpuPreviewUpdate status;
    if (channel == 5 && request.authored_ao)
      status = update_tile(set,
                           channel,
                           s.export_final.get(),
                           tile,
                           {},
                           false,
                           nullptr,
                           false,
                           nullptr,
                           false,
                           false,
                           true);
    else if (channel == 3 || channel == 5) {
      std::map<int, std::array<int, 3>> regions;
      for (int number : set.tiles)
        regions[number] = {};
      status = request.normal_perturbation ?
                   resolve_normal(set, s.export_final.get(), regions, tile, true) :
                   update_tiles_impl(set, channel, s.export_final.get(), regions, tile);
    }
    else
      status = update(set, channel, s.export_final.get(), tile);
    if (status != GpuPreviewUpdate::Updated)
      return status;
    GpuLayerCapture::Input final{
        s.export_final.get(), s.geometry_masks.empty(), s.geometry_masks.empty(), w, h};
    if (channel == 0 || channel == 1 || channel == 2 || channel == 3 || channel == 4 ||
        channel == 16 || (channel == 5 && request.authored_ao))
    {
      final.completion = 1;
      if (channel == 3)
        final.completion_base = precision_channel(
            set.channels[3] ? set.base_values : MaterialValues{}, 3, set.precision[3]);
      else if (channel == 16)
        final.completion_base.fill(set.displacement.midlevel);
      else {
        final.completion_base = gpu_channel_base(set, channel);
        if (channel == 4)
          final.completion_base.fill(precision_bump_base(set));
      }
    }
    else if (gpu_scalar_channel(channel)) {
      final.completion = 2;
      final.completion_base = gpu_channel_base(set, channel);
    }
    s.layer_capture.update(request, final, ticket);
    return GpuPreviewUpdate::Updated;
  }
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  GpuLayerCapture::Input input{
      s.zero.get(), s.geometry_masks.empty(), s.geometry_masks.empty(), w, h};
  if (request.mask) {
    input.image = mask_texture(set, request.layer, tile);
    if (!input.image)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  else {
    const int channel = request.channel == 3  ? 6 :
                        request.channel == 14 ? 16 :
                        request.channel >= 6  ? request.channel + 1 :
                                                request.channel;
    TextureSet normalized = set;
    normalized.instance_layers.reset();
    normalized.layers = layers;
    auto &root = normalized.layers.at(found - layers.begin());
    root.mask.reset();
    QUuid query = request.layer;
    std::string suffix = "/source";
    if (request.contextual) {
      TextureLayer sentinel;
      sentinel.id = QUuid("{318aff28-684e-4183-aeb9-72e1fa400a81}");
      sentinel.depth = found->depth;
      sentinel.parent = found->parent;
      query = sentinel.id;
      normalized.layers.insert(normalized.layers.begin() + (found - layers.begin()) + 1, sentinel);
      suffix = "/below";
    }
    else {
      root.channels = layer_source_channels(
          root.channels, root.kind == LayerKind::Fill && bool(root.fill.native_material));
      root.opacity.fill(1);
      root.blending.fill(LayerBlend::Normal);
      root.ao_enabled = true;
      root.ao_opacity = 1;
      root.ao_blending = LayerBlend::Normal;
      for (auto &user : root.users) {
        user.enabled = true;
        user.opacity = 1;
        user.blending = LayerBlend::Normal;
      }
      root.displacement.enabled = true;
      root.displacement.opacity = 1;
      root.displacement.blending = LayerBlend::Normal;
    }
    const auto state = update_tile(
        normalized, channel, s.zero.get(), tile, {}, true, &query, !request.contextual);
    if (state != GpuPreviewUpdate::Updated)
      return state;
    auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                              ":" + std::to_string(tile));
    const auto &value = stack.queries.at("query/" + query.toString().toStdString() + suffix);
    input.image = value.root.empty() ? s.zero.get() : stack.outputs.at(value.root).get();
    if (!request.contextual) {
      const auto &g = found->geometry;
      input.inclusion = g.type == 1      ? (g.includes_tile(tile) ? 0 : 1) :
                        g.meshes.empty() ? (g.default_include ? 0 : 1) :
                                           2;
      if (input.inclusion == 2) {
        auto owner = set.geometry_ownership.find(tile);
        if (owner == set.geometry_ownership.end())
          return GpuPreviewUpdate::Pending;
        input.owners = s.geometry_masks.atlas(owner->second,
                                              s.remaining,
                                              testing_stack ?
                                                  performance::Clock::time_point::max() :
                                                  s.upload_start + std::chrono::milliseconds(4));
        if (!input.owners)
          return GpuPreviewUpdate::Pending;
        input.selection = s.geometry_masks.selection(
            "capture:" + request.layer.toString().toStdString(), set.geometry_names, g);
      }
    }
  }
  s.layer_capture.update(request, input, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<LayerCaptureResult> GpuLayerPreview::layer_capture_result() const
{
  return impl_->layer_capture.get();
}
GpuPreviewUpdate GpuLayerPreview::thumbnail(const TextureSet &set,
                                            const ThumbnailRequest &request,
                                            std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_thumbnail");
  auto &s = *impl_;
  s.input_rejected = false;
  if (s.thumbnails.pending())
    return GpuPreviewUpdate::Pending;
  if (set.tiles.empty() || request.channel < 0 || request.channel > 14)
    return GpuPreviewUpdate::Unsupported;
  const int channel = request.channel == 3  ? 6 :
                      request.channel == 14 ? 16 :
                      request.channel >= 6  ? request.channel + 1 :
                                              request.channel;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  std::vector<GpuThumbnail::Input> inputs;
  for (int tile : set.tiles) {
    const int w = set.width(tile), h = set.rows(tile);
    if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
        state != GpuPreviewUpdate::Updated)
      return state;
    gpu::Texture *source = s.zero.get();
    if (request.mask) {
      source = mask_texture(set, request.layer, tile);
      if (!source)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    else if (gpu_channel_enabled(set, channel) && gpu_layer_channel(*found, channel).enabled) {
      const auto state = update_tile(
          set, channel, s.zero.get(), tile, {}, true, &request.layer, true, nullptr, true);
      if (state != GpuPreviewUpdate::Updated)
        return state;
      auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                                ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + request.layer.toString().toStdString() +
                                           "/source/thumbnail");
      source = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    }
    inputs.push_back({source, w, h});
  }
  s.thumbnails.update(request, inputs, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<ThumbnailMetadata> GpuLayerPreview::thumbnail_result() const
{
  return impl_->thumbnails.get();
}
void GpuLayerPreview::diagnose_pixel(const TextureSet &set, int channel, int tile, int x, int y)
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto found = impl_->stacks.find(key);
  if (found == impl_->stacks.end())
    return;
  for (const auto &layer : evaluation_layers(set))
    if (layer.kind == LayerKind::Fill && layer.fill.mode == 5) {
      const auto atlas = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                               set.projection_geometry;
      if (!atlas || !atlas->tiles.contains(tile))
        continue;
      using namespace gpu::shader;
      ShaderCreateInfo info("pyGPU_Shader");
      info.local_group_size(1, 1)
          .sampler(5, ImageType::Float2D, "layer_parameters_img")
          .image(0,
                 gpu::TextureFormat::SFLOAT_32_32_32_32,
                 Qualifier::write,
                 ImageReadWriteType::image2D,
                 "result_img")
          .push_constant(Type::float3_t, "world_point");
      info.compute_source_generated = GpuLayerParameters::declarations() + gpu_warp::source +
                                      R"GLSL(
void main(){vec3 v=world_point-projection_offset,p=vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v))*inverse_denominator;vec2 uv=vec2(-99);float depth=0;vec3 normal,du,dv;bool valid=warp_project(p,uv,depth,normal,du,dv);imageStore(result_img,ivec2(0),vec4(uv,depth,float(valid)));imageStore(result_img,ivec2(1,0),vec4(du,warp_projection_error));imageStore(result_img,ivec2(2,0),vec4(dv,0));}
)GLSL";
      Shader shader(
          GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free);
      GpuLayerParameters parameters;
      gpu_spatial_parameters(parameters, layer.fill, atlas.get(), set);
      auto input = texture("BTS warp projection diagnostic",
                           parameters.values.size(),
                           1,
                           gpu::TextureFormat::SFLOAT_32_32_32_32);
      GPU_texture_update(input.get(), GPU_DATA_FLOAT, parameters.values.data());
      auto output = texture(
          "BTS warp projection diagnostic result", 3, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      const auto &point = atlas->tiles.at(tile)[y * atlas->resolution + x];
      GPU_shader_bind(shader.get());
      GPU_texture_bind(input.get(), 5);
      GPU_texture_image_bind(output.get(), 0);
      GPU_shader_uniform_3fv(shader.get(), "world_point", point.position.data());
      GPU_compute_dispatch(shader.get(), 1, 1, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
      auto *actual = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
      const auto frame = fill_planar_frame(layer.fill, point, *atlas);
      const auto expected = fill_warp_coordinate(layer.fill, frame);
      std::printf(
          "BTS_GPU_WARP_NATIVE_DIAGNOSTIC x=%d y=%d gpu_uv=%.9g,%.9g depth=%.9g valid=%g error=%.9g cpu_uv=%.9g,%.9g depth=%.9g error=%.9g coverage=%g\n",
          x,
          y,
          actual[0],
          actual[1],
          actual[2],
          actual[3],
          actual[7],
          expected ? expected->u : -99,
          expected ? expected->v : -99,
          expected ? expected->depth : -99,
          expected ? expected->error : -1,
          expected ? fill_warp_coverage(layer.fill, frame, *expected) : 0);
      QJsonArray rows;
      for (const auto &row : parameters.values)
        rows.append(QJsonArray{row[0], row[1], row[2], row[3]});
      std::printf(
          "BTS_GPU_WARP_NATIVE_INPUT %s\n",
          QJsonDocument(
              QJsonObject{
                  {"parameters", rows},
                  {"point", QJsonArray{point.position[0], point.position[1], point.position[2]}}})
              .toJson(QJsonDocument::Compact)
              .constData());
      MEM_delete(actual);
      GPU_texture_unbind_all();
      GPU_texture_image_unbind_all();
      GPU_shader_unbind();
    }
  for (const auto &[id, image] : found->second.outputs) {
    const std::array<std::array<int, 3>, 1> locations{
        {{std::min(x, GPU_texture_width(image.get()) - 1),
          std::min(y, GPU_texture_height(image.get()) - 1),
          0}}};
    const auto p = probe_gpu_texture(image.get(), locations).front();
    std::printf("BTS_GPU_ORACLE_PREFIX id=%s rgba=%.9g,%.9g,%.9g,%.9g\n",
                id.c_str(),
                p[0],
                p[1],
                p[2],
                p[3]);
  }
}
void GpuLayerPreview::set_displacement_epoch(std::uint32_t uid, std::uint64_t epoch)
{
  impl_->displacement_epochs[uid] = epoch;
}
void GpuLayerPreview::retire_layer_channel(
    const TextureDocument &document,
    const TextureSet &set,
    const TextureLayer &previous,
    int channel,
    std::span<const std::shared_ptr<const FillTexture>> pending)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_channel_retire");
  auto &s = *impl_;
  const auto before = blender::gpu::host_texture_memory::live_bytes();
  const int gpu_channel = channel == 14 ? 16 :
                          channel >= 6  ? channel + 1 :
                          channel == 3  ? 6 :
                                          channel;
  const int plane = channel < 5 ? channel : 2;
  const auto owner = previous.id.toString().toStdString();
  std::set<std::string> protected_resources, removed_resources;
  std::function<void(const std::shared_ptr<const FillTexture> &, std::set<std::string> &)> keys;
  keys = [&](const auto &texture, auto &result) {
    if (!texture)
      return;
    if (texture->tiles) {
      for (const auto &[tile, member] : *texture->tiles)
        keys(member, result);
    }
    else
      result.insert(resource_key(*texture));
  };
  keys(fill_channel_texture(previous.fill, channel), removed_resources);
  const auto keep_projection = [&](const FillProjection &projection) {
    for (int c = 0; c < 15; ++c)
      keys(fill_channel_texture(projection, c), protected_resources);
  };
  for (const auto &candidate : document.texture_sets())
    for (const auto &layer : evaluation_layers(candidate)) {
      for (int c = 0; c < 15; ++c) {
        const bool enabled = c < 5   ? layer.channels[c] :
                             c == 5  ? layer.ao_enabled :
                             c == 14 ? layer.displacement.enabled :
                                       layer.users[c - 6].enabled;
        if (enabled)
          keys(fill_channel_texture(layer.fill, c), protected_resources);
      }
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          keep_projection(effect.mapping.projection);
    }
  for (const auto &texture : pending)
    keys(texture, protected_resources);
  std::vector<std::string> input_keys, stack_keys;
  for (int tile : set.tiles) {
    input_keys.push_back(paint_key(set, previous, gpu_channel, false, tile));
    const auto stack = set.id.toString().toStdString() + ":" + std::to_string(gpu_channel) + ":" +
                       std::to_string(tile);
    stack_keys.push_back(stack);
    input_keys.push_back(stack + ":" + owner);
  }
  const auto owned_node = [&](const std::string &key) {
    return key == owner || key.starts_with(owner + "/");
  };
  std::uint64_t source_bytes = 0;
  std::size_t source_count = 0;
  for (auto it = s.inputs.begin(); it != s.inputs.end();) {
    const auto &key = it->first;
    bool retire = removed_resources.contains(key) && !protected_resources.contains(key);
    for (const auto &prefix : input_keys)
      retire |= key == prefix || key.starts_with(prefix + "/");
    if (!retire || it->second.retained()) {
      ++it;
      continue;
    }
    source_bytes += it->second.bytes();
    s.used_bytes -= it->second.bytes();
    ++source_count;
    it = s.inputs.erase(it);
  }
  for (auto &[key, stack] : s.stacks) {
    if (std::none_of(stack_keys.begin(), stack_keys.end(), [&](const auto &prefix) {
          return key == prefix || key.starts_with(prefix + ":");
        }))
      continue;
    if (owned_node(stack.head_id)) {
      stack.head = nullptr;
      stack.head_id.clear();
    }
    std::erase_if(stack.outputs, [&](const auto &item) { return owned_node(item.first); });
    std::erase_if(stack.node_versions, [&](const auto &item) { return owned_node(item.first); });
    std::erase_if(stack.queries, [&](const auto &item) {
      return item.second.owner == previous.id || owned_node(item.second.root);
    });
    stack.queried_sources.erase(owner);
    std::set<std::string> live;
    for (const auto &[id, image] : stack.outputs)
      live.insert(id);
    stack.plan.retain(live);
  }
  std::set<std::tuple<std::uint64_t, std::uint32_t, int>> keep_pages;
  for (const auto &[key, entry] : s.inputs)
    if (entry.plane >= 0)
      for (const auto &[page, version] : entry.pages)
        keep_pages.emplace(version, page, entry.plane);
  const auto keep_pixels = [&](const TilePaintPixels &pixels) {
    for (const auto &[tile, data] : pixels.tiles())
      data.visit_page_versions(
          plane, [&](auto page, auto version) { keep_pages.emplace(version, page, plane); });
  };
  const auto keep_effect = [&](const EffectPaint &paint) {
    keep_pixels(paint.pixels);
    if (plane == 2) {
      keep_pixels(paint.ao);
      keep_pixels(paint.displacement);
      for (const auto &pixels : paint.users)
        keep_pixels(pixels);
    }
  };
  for (const auto &candidate : document.texture_sets()) {
    for (const auto &layer : evaluation_layers(candidate)) {
      keep_pixels(layer.pixels);
      if (plane == 2) {
        keep_pixels(layer.ao_pixels);
        keep_pixels(layer.displacement.pixels);
        for (const auto &user : layer.users)
          keep_pixels(user.pixels);
        if (layer.mask)
          keep_pixels(layer.mask->pixels);
      }
      for (const auto &effect : layer.effects)
        if (effect.kind == ContentEffectKind::Paint)
          keep_effect(effect.paint);
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.kind == MaskEffectKind::Paint)
            keep_effect(effect.paint);
    }
  }
  const auto retired_pages = s.authoring_pages.retire_unused(plane, keep_pages);
  s.comparison_memo.clear();
  s.anchor_memo.clear();
  const auto after = blender::gpu::host_texture_memory::live_bytes();
  std::size_t shared = 0;
  for (const auto &key : removed_resources)
    shared += protected_resources.contains(key);
  std::printf(
      "BTS_CHANNEL_RELEASE channel=%d source_count=%zu source_bytes=%llu authoring_pages=%zu shared_preserved=%zu storage_before=%llu storage_after=%llu\n",
      channel,
      source_count,
      (unsigned long long)source_bytes,
      retired_pages,
      shared,
      (unsigned long long)before,
      (unsigned long long)after);
}
bool GpuLayerPreview::texture_resource_ready(const FillTexture &texture) const
{
  const auto ready = [&](const FillTexture &source) {
    const auto found = impl_->inputs.find(resource_key(source));
    return found != impl_->inputs.end() && found->second.resource && found->second.image &&
           !found->second.rejected && found->second.width == source.width &&
           found->second.height == source.height && found->second.row == source.height;
  };
  if (texture.tiles) {
    for (const auto &[tile, image] : *texture.tiles)
      if (!ready(*image))
        return false;
    return true;
  }
  return ready(texture);
}
GpuPreviewUpdate GpuLayerPreview::prepare_baked_resource(int tile,
                                                         const MeshMapPlane &plane,
                                                         NativeBakedAoLease &lease)
{
  const auto key = native_baked_ao_key(plane, tile);
  if (!GPU_context_active_get())
    throw std::logic_error("Native baked AO upload requires the GPU owner");
  auto &s = *impl_;
  s.input_rejected = false;
  ++s.clock;
  const std::set<std::string> protected_keys{key};
  const bool ready = lease.state.prepare(
      plane,
      tile,
      [&] {
        auto *entry = s.entry(key, plane.resolution, plane.rows(), true, protected_keys);
        if (!entry || entry->rejected || !entry->image)
          native_baked_ao_reject(tile,
                                 "existing upload pool could not admit the baked allocation");
        lease.resource = {entry->image.get(), entry->width, entry->height, key, entry->residency};
        return NativeBakedAoPin{key, entry->width, entry->height, entry->residency};
      },
      [&](const NativeBakedAoPin &pin) {
        const auto found = s.inputs.find(pin.key);
        if (found == s.inputs.end() || found->second.residency != pin.residency ||
            found->second.image.get() != lease.resource.texture)
          native_baked_ao_reject(tile,
                                 "pinned baked allocation is no longer owned by the upload pool");
        auto *image = s.baked(&plane, protected_keys);
        if (s.input_rejected)
          native_baked_ao_reject(tile, "baked upload was rejected");
        return image != nullptr;
      });
  const auto found = s.inputs.find(key);
  if (found == s.inputs.end() || found->second.residency != lease.state.pin().residency ||
      found->second.image.get() != lease.resource.texture)
    native_baked_ao_reject(tile, "retained baked resource belongs to another upload pool");
  lease.validate(plane, tile, false);
  return ready ? GpuPreviewUpdate::Updated : GpuPreviewUpdate::Pending;
}
std::optional<GpuTextureResource> GpuLayerPreview::retain_texture_resource(
    const FillTexture &source) const
{
  if (source.tiles || !texture_resource_ready(source))
    return std::nullopt;
  const auto key = resource_key(source);
  const auto &entry = impl_->inputs.at(key);
  return GpuTextureResource{entry.image.get(), entry.width, entry.height, key, entry.residency};
}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resource(const FillTexture &texture)
{
  const std::array<const FillTexture *, 1> group{&texture};
  return prepare_texture_resources(group);
}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resources(
    std::span<const FillTexture *const> textures)
{
  auto &s = *impl_;
  s.input_rejected = false;
  ++s.clock;
  std::set<std::string> protected_keys;
  for (const auto *texture : textures) {
    if (!texture)
      throw std::invalid_argument("Empty GPU material input");
    if (texture->tiles)
      for (const auto &[tile, image] : *texture->tiles)
        protected_keys.insert(resource_key(*image));
    else
      protected_keys.insert(resource_key(*texture));
  }
  bool ready = true;
  const auto upload = [&](const FillTexture &image) {
    if (!s.resource(image, protected_keys))
      ready = false;
  };
  for (const auto *texture : textures) {
    if (texture->tiles)
      for (const auto &[tile, image] : *texture->tiles)
        upload(*image);
    else
      upload(*texture);
  }
  return s.input_rejected ? GpuPreviewUpdate::Unsupported :
         ready            ? GpuPreviewUpdate::Updated :
                            GpuPreviewUpdate::Pending;
}

bool GpuLayerPreview::metadata_pending() const
{
  return impl_->ranges.pending() || impl_->histograms.pending() ||
         impl_->material_samples.pending() || impl_->thumbnails.pending() ||
         impl_->layer_capture.pending();
}
std::optional<HistogramMetadata> GpuLayerPreview::histogram_result(const QUuid &id) const
{
  return impl_->histograms.get(id.toString().toStdString());
}
std::vector<GpuLayerPreview::DisplacementSummary> GpuLayerPreview::displacement_summaries()
{
  impl_->histograms.poll();
  impl_->material_samples.poll();
  impl_->thumbnails.poll();
  impl_->layer_capture.poll();
  std::vector<DisplacementSummary> result;
  for (const auto &v : impl_->ranges.poll())
    result.push_back({v.uid, v.minimum, v.maximum, v.present, v.epoch});
  return result;
}
bool GpuLayerPreview::wrote_image() const
{
  return impl_->image_changed;
}
GpuPreviewUpdate GpuLayerPreview::update(
    const TextureSet &set, int channel, gpu::Texture *target, int tile, std::array<int, 3> offset)
{
  return update_tiles(set, channel, target, {{tile, offset}});
}
GpuPreviewUpdate GpuLayerPreview::resolve_mask(const TextureSet &set,
                                               gpu::Texture *target,
                                               int tile,
                                               std::array<int, 3> offset)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_mask_inspection");
  if (!supported(set, 15) || !set.tiles.contains(tile))
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  s.input_rejected = false;
  const auto format = GPU_texture_format(target);
  const bool array = GPU_texture_is_array(target);
  const int w = set.width(tile), h = set.rows(tile);
  if (GPU_texture_dimensions(target) != 2 || offset[0] < 0 || offset[1] < 0 || offset[2] < 0 ||
      GPU_texture_width(target) < offset[0] + w || GPU_texture_height(target) < offset[1] + h ||
      offset[2] >= std::max(1, GPU_texture_layer_count(target)))
    return GpuPreviewUpdate::Unsupported;
  if (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
      format != gpu::TextureFormat::SFLOAT_32_32_32_32)
    return GpuPreviewUpdate::Unsupported;
  if (const auto state = s.initialize(w, h, format, array); state != GpuPreviewUpdate::Updated)
    return state;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == set.selected_layer; });
  if (found == layers.end() || !found->mask)
    return GpuPreviewUpdate::Unsupported;
  const auto key = paint_key(set, *found, 2, true, tile);
  auto *mask = mask_texture(set, found->id, tile);
  if (!mask)
    return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  auto &cache = s.mask_stacks.at(key);
  const auto target_key = std::pair{reinterpret_cast<std::uintptr_t>(target), offset};
  const auto identity = "mask:" + key;
  const int columns = (w + 255) / 256;
  if (s.target_owners[target_key] != identity)
    for (int y = 0; y < h; y += 256)
      for (int x = 0; x < w; x += 256)
        cache.output_dirty.insert((y / 256) * columns + x / 256);
  if (!cache.output_dirty.empty()) {
    const auto shader_key = std::pair{format, array};
    if (!s.mask_output_shaders.contains(shader_key))
      s.mask_output_shaders.emplace(shader_key,
                                    Shader(compile_mask_output(format, array), GPU_shader_free));
    auto *shader = s.mask_output_shaders.at(shader_key).get();
    if (!shader)
      return GpuPreviewUpdate::Unsupported;
    GPU_shader_bind(shader);
    GPU_texture_bind(mask, 0);
    GPU_texture_image_bind(target, 0);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    for (int chunk : cache.output_dirty) {
      const int x = chunk % columns * 256, y = chunk / columns * 256, origin[2] = {x, y},
                size[2] = {std::min(256, w - x), std::min(256, h - y)};
      GPU_shader_uniform_2iv(shader, "region_origin", origin);
      GPU_shader_uniform_2iv(shader, "region_size", size);
      GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(target);
    GPU_shader_unbind();
    s.image_changed = true;
    std::printf("BTS_GPU_MASK_INSPECTION tile=%d width=%d height=%d chunks=%zu cpu_composite=0\n",
                tile,
                w,
                h,
                cache.output_dirty.size());
    cache.output_dirty.clear();
  }
  s.target_owners[target_key] = identity;
  if (std::getenv("BTS_VERIFY_GPU_STACK")) {
    std::vector<std::array<int, 3>> positions;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        positions.push_back(
            {offset[0] + (2 * x + 1) * w / 16, offset[1] + (2 * y + 1) * h / 16, offset[2]});
    const auto pixels = probe_gpu_texture(target, positions);
    double maximum = 0;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      const float value = TextureDocument::mask_value(
          set, set.selected_layer, positions[i][0] - offset[0], positions[i][1] - offset[1], tile);
      for (int c = 0; c < 4; ++c)
        maximum = std::max(maximum, double(std::abs(pixels[i][c] - (c == 3 ? 1 : value))));
    }
    const bool valid = maximum < .001;
    std::printf("BTS_GPU_STACK_VERIFIED channel=15 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU mask inspection differs from CPU oracle");
  }
  return GpuPreviewUpdate::Updated;
}

GpuPreviewUpdate GpuLayerPreview::update_tiles(const TextureSet &set,
                                               int channel,
                                               gpu::Texture *target,
                                               const std::map<int, std::array<int, 3>> &regions)
{
  return update_tiles_impl(set, channel, target, regions, 0);
}
GpuPreviewUpdate GpuLayerPreview::update_tiles_impl(
    const TextureSet &set,
    int channel,
    gpu::Texture *target,
    const std::map<int, std::array<int, 3>> &regions,
    int output_tile)
{
  impl_->image_changed = false;
  if (channel == 15) {
    bool pending = false;
    for (const auto &[tile, offset] : regions) {
      const auto result = resolve_mask(set, target, tile, offset);
      if (result == GpuPreviewUpdate::Unsupported)
        return result;
      pending |= result == GpuPreviewUpdate::Pending;
    }
    return pending ? GpuPreviewUpdate::Pending : GpuPreviewUpdate::Updated;
  }
  if ((channel == 3 || channel == 5) &&
      (regions.size() != set.tiles.size() ||
       std::any_of(
           set.tiles.begin(), set.tiles.end(), [&](int tile) { return !regions.contains(tile); })))
    return GpuPreviewUpdate::Unsupported;
  if (channel == 3)
    return resolve_normal(set, target, regions, output_tile);
  auto &s = *impl_;
  bool pending = false;
  for (const auto &[tile, offset] : regions) {
    const auto result = update_tile(set, channel, target, tile, offset, channel == 5);
    if (result == GpuPreviewUpdate::Unsupported)
      return result;
    pending |= result == GpuPreviewUpdate::Pending;
  }
  if (pending)
    return GpuPreviewUpdate::Pending;
  // GpuRangeSummary consumes a complete tile set and removes omitted sources.
  // A single-tile pixel update cannot claim new whole-material bounds, even if
  // older stacks happen to exist for the other UDIMs. The normal all-tile owner
  // submits one coherent complete range after all requested prefixes are ready.
  const bool complete_regions = regions.size() == set.tiles.size() &&
                                std::all_of(set.tiles.begin(), set.tiles.end(), [&](int tile) {
                                  return regions.contains(tile);
                                });
  if (channel == 16 && !testing_stack && complete_regions) {
    std::vector<GpuRangeSummary::Source> sources;
    for (int tile : set.tiles) {
      const auto key = set.id.toString().toStdString() + ":16:" + std::to_string(tile);
      const auto &stack = s.stacks.at(key);
      sources.push_back(
          {tile, set.width(tile), set.rows(tile), stack.head, stack.revision, stack.last_changes});
    }
    s.ranges.update(
        set.material_session_uid, sources, s.displacement_epochs[set.material_session_uid]);
  }
  if (channel != 5)
    return GpuPreviewUpdate::Updated;
  performance::Scope timing("gpu_ao_resolve");
  std::map<int, gpu::Texture *> bakes;
  std::set<std::string> protected_keys;
  if (set.mesh_maps[3])
    for (const auto &[tile, plane] : set.mesh_maps[3]->tiles)
      if (plane.pixels)
        protected_keys.insert("baked:" + plane.pixels->hash + ":" +
                              std::to_string(plane.pixels->resolution) + ":" +
                              std::to_string(plane.pixels->height));
  for (const auto &[tile, offset] : regions) {
    const MeshMapPlane *plane = nullptr;
    if (set.mesh_maps[3])
      if (auto i = set.mesh_maps[3]->tiles.find(tile); i != set.mesh_maps[3]->tiles.end())
        plane = &i->second;
    bakes[tile] = s.baked(plane, protected_keys);
    if (!bakes[tile])
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  auto dirty_update = s.derived_changes(set, 5, target, regions, output_tile);
  if (!dirty_update.needed)
    return GpuPreviewUpdate::Updated;
  auto &derived = s.derived[s.derived_key(set, 5)];
  if (!s.prepare_derived_masks(derived, dirty_update, set))
    return GpuPreviewUpdate::Unsupported;
  if (!s.presence_shader)
    s.presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
  if (!derived.presence)
    derived.presence = texture(
        "BTS cross-tile AO presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
  if (!s.presence_shader || !derived.presence)
    return GpuPreviewUpdate::Unsupported;
  auto *reduce = s.presence_shader.get();
  GPU_shader_bind(reduce);
  GPU_texture_image_bind(derived.presence.get(), 0);
  bool first = true;
  const auto stack_key = [&](int tile) {
    return set.id.toString().toStdString() + ":5:" + std::to_string(tile);
  };
  for (const auto &[tile, offset] : regions) {
    const auto &stack = s.stacks.at(stack_key(tile));
    GPU_texture_bind(stack.coverage.get(), 0);
    GPU_shader_uniform_1i(reduce, "first_tile", first);
    GPU_compute_dispatch(reduce, 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    first = false;
  }
  GPU_texture_image_unbind(derived.presence.get());
  const auto format = GPU_texture_format(target);
  const auto key = std::pair{format, GPU_texture_is_array(target)};
  if (!s.ao_shaders.contains(key))
    s.ao_shaders.emplace(
        key, Shader(gpu_derived::ao_shader(format, key.second, quant_source), GPU_shader_free));
  auto *shader = s.ao_shaders.at(key).get();
  if (!shader)
    return GpuPreviewUpdate::Unsupported;
  GPU_shader_bind(shader);
  GPU_texture_bind(derived.presence.get(), 2);
  GPU_texture_image_bind(target, 0);
  GPU_shader_uniform_1f(shader, "base_value", gpu_channel_base(set, 5)[0]);
  GPU_shader_uniform_1i(shader, "authored_enabled", set.ao_channel);
  GPU_shader_uniform_1i(shader, "replace_mode", set.ao_mixing == AoMixing::Replace);
  GPU_shader_uniform_1i(shader, "value_precision", int(set.precision[5]));
  for (const auto &[tile, offset] : regions) {
    if (output_tile && tile != output_tile)
      continue;
    auto &stack = s.stacks.at(stack_key(tile));
    GPU_texture_bind(stack.head, 0);
    GPU_texture_bind(bakes.at(tile), 1);
    GPU_texture_bind(derived.dirty_images.at(tile).get(), 3);
    const int size[2] = {set.width(tile), set.rows(tile)};
    GPU_shader_uniform_2iv(shader, "size", size);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  if (const auto found = s.margin_plans.find(set.material_session_uid);
      found != s.margin_plans.end())
    for (const auto &[tile, offset] : regions) {
      if (output_tile && tile != output_tile)
        continue;
      const auto &plan = (set.padding_3d ? found->second.adjacent : found->second.extend).at(tile);
      if (!s.margins.apply(stack_key(tile) + ":ao",
                           plan,
                           target,
                           offset,
                           dirty_update.flags.at(tile),
                           false,
                           derived.presence.get()))
        return GpuPreviewUpdate::Unsupported;
    }
  s.commit_derived(set, 5, target, regions, std::move(dirty_update));
  if (!output_tile && std::getenv("BTS_VERIFY_GPU_STACK") &&
      !s.margin_plans.contains(set.material_session_uid))
  {
    std::array<bool, 6> wanted{};
    wanted[5] = true;
    bool authored = false;
    for (int tile : set.tiles) {
      for (int y = 0; y < set.rows(tile) && !authored; ++y)
        for (int x = 0; x < set.width(tile) && !authored; ++x)
          authored = TextureDocument::evaluate_channels(set, wanted, x, y, tile).ao_coverage > 0;
    }
    auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
    double maximum = 0;
    bool valid = true;
    for (const auto &[tile, offset] : regions)
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
          const int px = int((x + .5) * set.width(tile) / 8),
                    py = int((y + .5) * set.rows(tile) / 8);
          const auto composite = TextureDocument::evaluate_channels(set, wanted, px, py, tile);
          const auto baked = sample_baked_ao(
              set, (px + .5) / set.width(tile), (py + .5) / set.rows(tile), tile);
          const float mixed = mix_ambient_occlusion(
              set, composite.material.ambient_occlusion, authored, baked);
          float expected = precision_rgb({mixed, mixed, mixed}, set.precision[5])[0];
          if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
            expected = math::half_to_float(math::float_to_half_make_finite(expected));
          const auto index = ((std::size_t(offset[2]) * GPU_texture_height(target) + py +
                               offset[1]) *
                                  GPU_texture_width(target) +
                              px + offset[0]) *
                             4;
          for (int c = 0; c < 4; ++c) {
            const double difference = std::abs(pixels[index + c] - (c == 3 ? 1 : expected));
            maximum = std::max(maximum, difference);
            const double tolerance = set.precision[5] == ChannelPrecision::L8 ? 1. / 255 + .001 :
                                     set.precision[5] == ChannelPrecision::L16 ?
                                                                                1. / 65535 + .001 :
                                     format == gpu::TextureFormat::SFLOAT_16_16_16_16 ? .001 :
                                                                                        1e-6;
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_AO_DIFFERENCE tile=%d x=%d y=%d precision=%d mode=%d authored=%d coverage=%.9g material=%.9g bake=%.9g expected=%.9g actual=%.9g\n",
                  tile,
                  px,
                  py,
                  int(set.precision[5]),
                  int(set.ao_mixing),
                  int(authored),
                  composite.ao_coverage,
                  composite.material.ambient_occlusion,
                  baked.value,
                  expected,
                  pixels[index + c]);
            valid &= std::isfinite(pixels[index + c]) && difference <= tolerance;
          }
        }
    MEM_delete(pixels);
    std::printf("BTS_GPU_STACK_VERIFIED channel=5 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU AO differs from CPU cross-tile reference");
  }
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::resolve_normal(const TextureSet &source_set,
                                                 gpu::Texture *target,
                                                 const std::map<int, std::array<int, 3>> &regions,
                                                 int output_tile,
                                                 bool without_height)
{
  auto set = native_normal_stack(source_set);
  if (!supported(set, 3))
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  bool pending = false;
  for (const auto &[tile, offset] : regions) {
    for (int c : {6, 4}) {
      if (c == 4 && (!set.channels[4] || without_height))
        continue;
      const auto updated = update_tile(set, c, target, tile, offset, true);
      if (updated == GpuPreviewUpdate::Unsupported)
        return updated;
      pending |= updated == GpuPreviewUpdate::Pending;
    }
  }
  if (pending)
    return GpuPreviewUpdate::Pending;
  if (without_height)
    set.channels[4] = false;
  performance::Scope timing("gpu_normal_resolve");
  const auto stack_key = [&](int channel, int tile) {
    return set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
           std::to_string(tile);
  };
  std::map<int, gpu::Texture *> bakes;
  std::set<std::string> protected_keys;
  if (set.mesh_maps[0])
    for (const auto &[tile, plane] : set.mesh_maps[0]->tiles)
      if (plane.pixels)
        protected_keys.insert("baked:" + plane.pixels->hash + ":" +
                              std::to_string(plane.pixels->resolution) + ":" +
                              std::to_string(plane.pixels->height));
  for (const auto &[tile, offset] : regions) {
    const MeshMapPlane *plane = nullptr;
    if (set.mesh_maps[0])
      if (auto i = set.mesh_maps[0]->tiles.find(tile); i != set.mesh_maps[0]->tiles.end())
        plane = &i->second;
    bakes[tile] = s.baked(plane, protected_keys);
    if (!bakes[tile])
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  auto dirty_update = s.derived_changes(set, 3, target, regions, output_tile);
  if (!dirty_update.needed)
    return GpuPreviewUpdate::Updated;
  auto &derived = s.derived[s.derived_key(set, 3)];
  if (!s.prepare_derived_masks(derived, dirty_update, set))
    return GpuPreviewUpdate::Unsupported;
  if (!s.presence_shader)
    s.presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
  if (!derived.presence)
    derived.presence = texture(
        "BTS cross-tile authored presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
  if (!s.presence_shader || !derived.presence)
    return GpuPreviewUpdate::Unsupported;
  auto *reduce = s.presence_shader.get();
  GPU_shader_bind(reduce);
  GPU_texture_image_bind(derived.presence.get(), 0);
  bool first = true;
  for (const auto &[tile, offset] : regions) {
    GPU_texture_bind(s.stacks.at(stack_key(6, tile)).coverage.get(), 0);
    GPU_shader_uniform_1i(reduce, "first_tile", first);
    GPU_compute_dispatch(reduce, 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    first = false;
  }
  GPU_texture_image_unbind(derived.presence.get());
  const auto format = GPU_texture_format(target);
  const auto key = std::pair{format, GPU_texture_is_array(target)};
  if (!s.normal_shaders.contains(key))
    s.normal_shaders.emplace(
        key,
        Shader(gpu_derived::normal_shader(format, key.second, quant_source), GPU_shader_free));
  auto *shader = s.normal_shaders.at(key).get();
  if (!shader)
    return GpuPreviewUpdate::Unsupported;
  GPU_shader_bind(shader);
  GPU_texture_bind(derived.presence.get(), 2);
  GPU_texture_image_bind(target, 0);
  const auto basis = set.channels[3] ? set.base_values : MaterialValues{};
  const std::array<float, 3> base{
      basis.normal[0] * .5f + .5f, basis.normal[1] * .5f + .5f, basis.normal[2] * .5f + .5f};
  const auto quantized = precision_channel(basis, 3, set.precision[3]);
  GPU_shader_uniform_3fv(shader, "normal_base", base.data());
  GPU_shader_uniform_3fv(shader, "quantized_base", quantized.data());
  GPU_shader_uniform_1f(shader, "height_base", gpu_channel_base(set, 4)[0]);
  GPU_shader_uniform_1i(shader, "height_precision", int(set.precision[4]));
  GPU_shader_uniform_1i(shader, "height_enabled", set.channels[4]);
  GPU_shader_uniform_1i(shader, "authored_enabled", set.channels[3]);
  GPU_shader_uniform_1i(shader, "directx", set.normal_format == NormalMapFormat::DirectX);
  GPU_shader_uniform_1i(shader, "replace_mode", set.normal_mixing == NormalMixing::Replace);
  GPU_shader_uniform_1i(shader, "sobel", set.bump_method == BumpMethod::SmoothSobel);
  GPU_shader_uniform_1i(shader, "value_precision", int(set.precision[3]));
  for (const auto &[tile, offset] : regions) {
    if (output_tile && tile != output_tile)
      continue;
    std::array<std::array<float, 4>, 9> meta{};
    const int col = (tile - 1001) % 10, row = (tile - 1001) / 10;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int i = (dy + 1) * 3 + dx + 1, neighbor = 1001 + col + dx + 10 * (row + dy);
        const bool exists = col + dx >= 0 && col + dx < 10 && row + dy >= 0 && row + dy < 100 &&
                            set.tiles.contains(neighbor);
        const auto *height_stack = exists && set.channels[4] ?
                                       &s.stacks.at(stack_key(4, neighbor)) :
                                       nullptr;
        const bool padded = height_stack && bool(height_stack->height_image);
        meta[i] = {float(exists ? set.width(neighbor) : 1),
                   float(exists ? set.rows(neighbor) : 1),
                   float(exists),
                   float(padded)};
        GPU_texture_bind(height_stack ?
                             (padded ? height_stack->height_image.get() : height_stack->head) :
                             s.zero.get(),
                         4 + i);
      }
    auto &entry = s.normal_neighbors[stack_key(3, tile)];
    if (!entry.image)
      entry.image = texture(
          "BTS normal UDIM neighbors", 9, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!entry.image)
      return GpuPreviewUpdate::Unsupported;
    if (entry.values != meta) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, meta.data());
      entry.values = meta;
    }
    GPU_texture_bind(entry.image.get(), 3);
    GPU_texture_bind(s.stacks.at(stack_key(6, tile)).head, 0);
    GPU_texture_bind(bakes.at(tile), 1);
    GPU_texture_bind(derived.dirty_images.at(tile).get(), 13);
    const int size[2] = {set.width(tile), set.rows(tile)};
    GPU_shader_uniform_2iv(shader, "size", size);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  if (const auto found = s.margin_plans.find(set.material_session_uid);
      found != s.margin_plans.end())
    for (const auto &[tile, offset] : regions) {
      if (output_tile && tile != output_tile)
        continue;
      const auto &plan = found->second.extend.at(tile);
      if (!s.margins.apply(stack_key(3, tile) + ":normal",
                           plan,
                           target,
                           offset,
                           dirty_update.flags.at(tile),
                           false,
                           derived.presence.get()))
        return GpuPreviewUpdate::Unsupported;
    }
  s.commit_derived(set, 3, target, regions, std::move(dirty_update));
  if (!output_tile && std::getenv("BTS_VERIFY_GPU_STACK") && testing_stack &&
      !s.margin_plans.contains(set.material_session_uid))
  {
    CompositeChannels channels{};
    channels[3] = true;
    std::atomic_bool cancel = false;
    const auto reference = native_composite_job(set, false, nullptr, 1, cancel, false, channels);
    if (!reference)
      throw std::runtime_error("Normal CPU oracle cancelled");
    auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
    double maximum = 0;
    bool valid = true;
    for (const auto &[tile, offset] : regions)
      for (int y = 0; y < set.rows(tile); ++y)
        for (int x = 0; x < set.width(tile); ++x) {
          const auto index = ((std::size_t(offset[2]) * GPU_texture_height(target) + y +
                               offset[1]) *
                                  GPU_texture_width(target) +
                              x + offset[0]) *
                             4;
          const auto local = (std::size_t(y) * set.width(tile) + x) * 4;
          for (int c = 0; c < 4; ++c) {
            float expected = reference->tiles.at(tile)[3][local + c];
            if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
              expected = math::half_to_float(math::float_to_half_make_finite(expected));
            const double difference = std::abs(pixels[index + c] - expected),
                         tolerance = (format == gpu::TextureFormat::SFLOAT_16_16_16_16 ||
                                      set.precision[3] == ChannelPrecision::RGB16F) ?
                                         .001 :
                                         2e-5;
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_NORMAL_DIFFERENCE tile=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",
                  tile,
                  x,
                  y,
                  c,
                  expected,
                  pixels[index + c]);
            maximum = std::max(maximum, difference);
            valid &= std::isfinite(pixels[index + c]) && difference <= tolerance;
          }
        }
    MEM_delete(pixels);
    std::printf("BTS_GPU_STACK_VERIFIED channel=3 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU normal differs from CPU derived reference");
  }
  return GpuPreviewUpdate::Updated;
}
#include "GpuLayerPreviewSelfTest.inc"

}  // namespace bts
