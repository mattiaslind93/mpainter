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
bool testing_stack=false,benchmark_stack=false;
// Transfer scheduling only, never a resource-size or texture-resolution limit.
constexpr std::uint64_t upload_budget=32ULL*1024*1024;
std::uint64_t source_cache_budget(std::uint64_t owned){return resource_sum(owned,texture_vram_query_valid.load()?texture_free_vram.load():texture_available_ram_bytes());}

using Texture=std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)>;
using Shader=std::unique_ptr<gpu::Shader,decltype(&GPU_shader_free)>;
Texture texture(const char *name,int w,int h,gpu::TextureFormat format){return {GPU_texture_create_2d(name,w,h,1,format,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};}
bool active(const TextureLayer &layer,int channel,int tile=1001){
 PaintMetadataScope metadata;
 const auto data=gpu_layer_channel(layer,channel);
 if(!layer.visible||!data.enabled||data.blend==LayerBlend::Disabled)return false;
 if(layer.geometry.type==1&&!layer.geometry.includes_tile(tile))return false;
 if(layer.geometry.type==0&&layer.geometry.meshes.empty()&&!layer.geometry.default_include)return false;
 if(data.opacity<=0&&data.blend!=LayerBlend::Replace)return false;
 if(layer.kind==LayerKind::Paint&&!(channel==0&&layer.quick_overlay)&&data.pixels->tile(tile).page_count(data.plane)==0&&layer.effects.empty()&&data.blend!=LayerBlend::Replace)return false;
 return true;
}
std::string paint_key(const TextureSet &s,const TextureLayer &l,int c,bool mask,int tile=1001){return s.id.toString().toStdString()+l.id.toString().toStdString()+":"+std::to_string(tile)+":"+(mask?"mask":std::to_string(c));}
std::string resource_key(const FillTexture &description){
 return gpu_texture_resource_key(description);
}
const FillTexture *tile_resource(const FillProjection &fill,int channel,int tile){const auto &source=fill_channel_texture(fill,channel);if(!source)return nullptr;if(fill.mode==6&&source->tiles){const auto found=source->tiles->find(tile);return found==source->tiles->end()?nullptr:found->second.get();}return source.get();}
const char *quant_source=R"GLSL(
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
const char *layer_source=R"GLSL(
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
const char *projected_layer_source=R"GLSL(
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
const char *final_source=R"GLSL(
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
gpu::shader::ShaderCreateInfo layer_info(gpu::TextureFormat format){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).sampler(0,ImageType::Float2D,"source_img").sampler(1,ImageType::Float2D,"mask_img").sampler(2,ImageType::Float2D,"backdrop_img").sampler(3,ImageType::Uint2DArray,"surface_img").sampler(4,ImageType::Float2D,"effects_img").sampler(9,ImageType::Uint2D,"geometry_owners_img").sampler(10,ImageType::Uint2D,"geometry_selection_img").image(0,format,Qualifier::write,ImageReadWriteType::image2D,"result_img");
 info.sampler(5,ImageType::Float2D,"layer_parameters_img").sampler(6,ImageType::Float2D,"mask_id_img").sampler(7,ImageType::Float2D,"mask_position_img").sampler(8,ImageType::Float2D,"mask_curvature_img").sampler(11,ImageType::Float2D,"mask_ao_img").sampler(12,ImageType::Float2D,"mask_thickness_img").sampler(13,ImageType::Float2D,"mask_world_normal_img");
 info.push_constant(Type::int2_t,"region_origin").push_constant(Type::int2_t,"region_size");
 info.compute_source_generated=GpuLayerParameters::declarations()+quant_source+layer_source+gpu_warp::source+gpu_projection::source+projected_layer_source;return info;}
Shader compile_layer(gpu::TextureFormat format=gpu::TextureFormat::SFLOAT_32_32_32_32){auto info=layer_info(format);return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}
Shader compile_final(gpu::TextureFormat format,bool array){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).sampler(0,ImageType::Float2D,"backdrop_img").image(0,format,Qualifier::write,array?ImageReadWriteType::image2DArray:ImageReadWriteType::image2D,"result_img").push_constant(Type::float3_t,"base_value").push_constant(Type::float3_t,"output_bias").push_constant(Type::int_t,"value_precision").push_constant(Type::int_t,"finite_half").push_constant(Type::int_t,"output_kind").push_constant(Type::int2_t,"region_origin").push_constant(Type::int2_t,"region_size").push_constant(Type::int2_t,"tile_size").push_constant(Type::int3_t,"target_offset");
 std::string source=final_source;const auto size=source.find("size=imageSize(result_img)");source.replace(size,std::string("size=imageSize(result_img)").size(),"size=tile_size");const auto store=source.find("imageStore(result_img,p,out_value)");source.replace(store,std::string("imageStore(result_img,p,out_value)").size(),array?"imageStore(result_img,ivec3(p+target_offset.xy,target_offset.z),out_value)":"imageStore(result_img,p+target_offset.xy,out_value)");info.compute_source_generated=std::string(quant_source)+source;return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}
Shader compile_world_weight(){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).image(0,gpu::TextureFormat::SFLOAT_32,Qualifier::read_write,ImageReadWriteType::image2D,"weight_img").sampler(0,ImageType::Float2D,"mask_img");info.compute_source_generated="void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(weight_img))))return;float w=imageLoad(weight_img,p).r*clamp(texelFetch(mask_img,p,0).r,0.0,1.0);imageStore(weight_img,p,vec4(w));}";return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}
Shader compile_world_apply(){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::read_write,ImageReadWriteType::image2D,"world_img").sampler(0,ImageType::Float2D,"weight_img");info.compute_source_generated="void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(world_img))))return;imageStore(world_img,p,imageLoad(world_img,p)*texelFetch(weight_img,p,0).r);}";return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}
Shader compile_world_publish(bool array){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,array?ImageReadWriteType::image2DArray:ImageReadWriteType::image2D,"world_img").sampler(0,ImageType::Float2D,"source_img").sampler(1,ImageType::Float2D,"weight_img").push_constant(Type::int3_t,"target_offset").push_constant(Type::int_t,"apply_weight");info.compute_source_generated=std::string("void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,textureSize(source_img,0))))return;vec4 v=texelFetch(source_img,p,0)*(apply_weight!=0?texelFetch(weight_img,p,0).r:1.0);imageStore(world_img,")+(array?"ivec3(p+target_offset.xy,target_offset.z)":"p+target_offset.xy")+",v);}";return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}
Shader compile_uniform(gpu::TextureFormat format){using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(16,16).image(0,format,Qualifier::write,ImageReadWriteType::image2D,"result_img").push_constant(Type::float4_t,"fill_value");info.compute_source_generated="void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(all(lessThan(p,imageSize(result_img))))imageStore(result_img,p,fill_value);}";return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};}

}
struct GpuLayerPreview::Impl {
 struct PolygonUndo {GpuPolygonRaster::Snapshot pixels;std::map<std::uint32_t,std::uint64_t> before,after;std::uint64_t used=0;};
 struct Entry {
  Texture image{nullptr,GPU_texture_free};int width=0,height=0,row=0,plane=-1;bool rejected=false,resource=false;std::uint64_t used=0,patch_bytes=0;
  std::shared_ptr<const void> residency;
  bool retained()const{return residency&&residency.use_count()>1;}
  std::map<std::uint32_t,std::uint64_t> pages;
  std::map<std::uint64_t,PolygonUndo> polygon_undo;
  std::uint64_t bytes()const{return (image?std::uint64_t(width)*height*16:0)+patch_bytes;}
 };
 std::map<std::string,Entry> inputs;
 performance::Clock::time_point cache_logged{};
 GpuPaintPages authoring_pages;
 AuthoringCacheBudget input_budget;
 struct EffectEntry{Texture image{nullptr,GPU_texture_free};std::vector<GpuEffectRow> rows;};
 std::map<std::string,EffectEntry> effect_parameters;
 gpu::Texture *effects(const std::string &key,const std::vector<GpuEffectRow> &rows){
  if(rows.empty())return zero.get();auto &entry=effect_parameters[key];
  if(!entry.image||entry.rows.size()!=rows.size()){entry.image=texture("BTS GPU effect parameters",int(GpuEffectRow{}.size()),int(rows.size()),gpu::TextureFormat::SFLOAT_32_32_32_32);entry.rows.clear();}
  if(!entry.image){input_rejected=true;return nullptr;}if(entry.rows!=rows){GPU_texture_update(entry.image.get(),GPU_DATA_FLOAT,rows.data());entry.rows=rows;}return entry.image.get();
 }

 struct ParameterEntry {Texture image{nullptr,GPU_texture_free};GpuLayerParameters values;};
 std::map<std::string,ParameterEntry> layer_parameters;
 gpu::Texture *parameters(const std::string &key,const GpuLayerParameters &values){
  auto &entry=layer_parameters[key];bool created=false;if(!entry.image){entry.image=texture("BTS GPU layer parameters",GpuLayerParameters{}.values.size(),1,gpu::TextureFormat::SFLOAT_32_32_32_32);created=true;}if(!entry.image)return nullptr;
  if(created||entry.values.values!=values.values){GPU_texture_update(entry.image.get(),GPU_DATA_FLOAT,values.values.data());entry.values=values;}return entry.image.get();
 }
 struct GeometryEntry {Texture image{nullptr,GPU_texture_free};std::shared_ptr<const FillSurfaceAtlas> owner;int tile=0,row=0,width=0,height=0,layers=0;};
 std::map<std::pair<const FillSurfaceAtlas *,int>,GeometryEntry> geometry;
 Texture empty_geometry{nullptr,GPU_texture_free};
 gpu::Texture *surface(const std::shared_ptr<const FillSurfaceAtlas> &atlas,int tile){performance::Scope timing("gpu_projection_source_upload");
  if(!atlas){if(!empty_geometry){const unsigned int data[4]={};empty_geometry=Texture(GPU_texture_create_2d_array("BTS empty surface",1,1,1,1,gpu::TextureFormat::UINT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(empty_geometry)GPU_texture_clear(empty_geometry.get(),GPU_DATA_UINT,data);}return empty_geometry.get();}
  const auto &points=atlas->tiles.at(tile);const auto bytes=points.size()*sizeof(FillSurfacePoint);
  auto &entry=geometry[{atlas.get(),tile}];
  if(!entry.image){entry.owner=atlas;entry.tile=tile;entry.width=1024;entry.height=int(std::min<std::size_t>(8192,(bytes+entry.width*16-1)/(entry.width*16)));entry.layers=int((bytes+std::size_t(entry.width)*entry.height*16-1)/(std::size_t(entry.width)*entry.height*16));
   entry.image=Texture(GPU_texture_create_2d_array("BTS shared projection surface",entry.width,entry.height,entry.layers,1,gpu::TextureFormat::UINT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!entry.image){input_rejected=true;return nullptr;}
   std::printf("BTS_GPU_SURFACE_ALLOC tile=%d bytes=%zu shared_channels=1\n",tile,bytes);
  }
  const auto *data=reinterpret_cast<const unsigned char *>(points.data());const std::size_t row_bytes=entry.width*16;
  while(std::size_t(entry.row)*row_bytes<bytes){if(!upload_time_available())return nullptr;const int rows=std::min({64,entry.height-entry.row%entry.height,int(remaining/row_bytes),int((bytes-entry.row*row_bytes)/row_bytes)});
   if(!rows){if(remaining<row_bytes)return nullptr;std::vector<unsigned char> tail(row_bytes);std::memcpy(tail.data(),data+entry.row*row_bytes,bytes-entry.row*row_bytes);GPU_texture_update_sub(entry.image.get(),GPU_DATA_UINT,tail.data(),0,entry.row%entry.height,entry.row/entry.height,entry.width,1,1);remaining-=row_bytes;++entry.row;break;}
   GPU_texture_update_sub(entry.image.get(),GPU_DATA_UINT,data+entry.row*row_bytes,0,entry.row%entry.height,entry.row/entry.height,entry.width,rows,1);remaining-=rows*row_bytes;entry.row+=rows;
  }return entry.image.get();
 }

 std::array<int,2> oracle_position{24,24};
 struct Query {std::string root;bool enabled=false;QUuid owner;std::set<std::string> nodes;};
 struct Stack {std::map<std::string,QUuid> node_owners;std::map<std::string,Query> queries;std::set<std::string> queried_sources;std::map<std::string,std::map<std::uint64_t,GpuStackPage>> node_versions;std::uint64_t node_clock=0;Texture coverage{nullptr,GPU_texture_free},height_image{nullptr,GPU_texture_free};gpu::Texture *head=nullptr;std::string head_id;GpuStackPlan plan;std::map<std::string,std::shared_ptr<gpu::Texture>> outputs;int width=0,height=0;std::uint64_t last_used=0,revision=0;std::set<int> output_dirty,height_dirty,last_changes;};
 std::map<std::string,GpuComparisonInput> comparison_memo;std::map<std::string,GpuAnchorInput> anchor_memo;std::map<std::string,Stack> stacks,mask_stacks;GpuRangeSummary ranges;GpuHistogram histograms;GpuMaterialSample material_samples;GpuThumbnail thumbnails;GpuLayerCapture layer_capture;std::map<std::uint32_t,std::uint64_t> displacement_epochs;
 std::uint64_t mask_dispatch_texels=0;
 std::map<std::pair<std::uintptr_t,std::array<int,3>>,std::string> target_owners;
 struct Margins {std::map<int,std::shared_ptr<const GpuMarginPlan>> extend,adjacent;};
 std::map<std::uint32_t,Margins> margin_plans;
 GpuMarginExecutor margins;
 GpuGeometryMask geometry_masks;
 struct DerivedEntry {GpuDerivedDirty tracking;Texture presence{nullptr,GPU_texture_free};std::map<int,Texture> dirty_images;};
 struct DerivedUpdate {GpuDerivedDirty tracking;GpuDerivedDirty::Chunks dirty;std::map<int,std::vector<std::uint32_t>> flags;bool needed=false;};
 std::map<std::string,DerivedEntry> derived;
 bool image_changed=false;
 std::size_t last_derived_chunks=0,last_derived_total=0;
 std::string derived_key(const TextureSet &set,int channel)const{return set.id.toString().toStdString()+":derived:"+std::to_string(channel);}
 DerivedUpdate derived_changes(const TextureSet &set,int channel,gpu::Texture *target,const std::map<int,std::array<int,3>> &regions,int output_tile=0){
  const auto key=derived_key(set,channel);auto &entry=derived[key];DerivedUpdate update;update.tracking=entry.tracking;
  std::string parameters;const auto append=[&](const auto &v){parameters.append(reinterpret_cast<const char *>(&v),sizeof(v));};
  append(set.channels);append(set.ao_channel);append(set.ao_mixing);append(set.normal_mixing);append(set.normal_format);append(set.bump_method);append(set.bump_origin);append(set.padding_3d);
  for(int c:{3,4,5})append(set.precision[c]);append(gpu_channel_base(set,4));append(gpu_channel_base(set,5));append(gpu_channel_base(set,6));
  append(reinterpret_cast<std::uintptr_t>(target));append(output_tile);GpuDerivedDirty::Sizes sizes;bool different_target=false;
  const auto &baked=set.mesh_maps[channel==3?0:3];
  for(const auto &[tile,offset]:regions){
   sizes[tile]={set.width(tile),set.rows(tile)};append(tile);append(offset);append(sizes[tile]);
   if(baked)if(auto found=baked->tiles.find(tile);found!=baked->tiles.end()&&found->second.pixels)parameters+=found->second.pixels->hash;
   different_target|=target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target),offset}]!=key;
   if(auto found=margin_plans.find(set.material_session_uid);found!=margin_plans.end()){append(reinterpret_cast<std::uintptr_t>(found->second.extend.at(tile).get()));append(reinterpret_cast<std::uintptr_t>(found->second.adjacent.at(tile).get()));}
  }
  const bool full=different_target||parameters!=entry.tracking.parameters;update.tracking.parameters=std::move(parameters);
  for(const auto &[tile,size]:sizes){
   const int source=channel==3?6:5;auto &stack=stacks.at(set.id.toString().toStdString()+":"+std::to_string(source)+":"+std::to_string(tile));
   auto dirty=update.tracking.changed(source,tile,stack.revision,stack.last_changes,size[0],size[1],full);update.dirty[tile].insert(dirty.begin(),dirty.end());
   if(channel==3&&set.channels[4]){
    auto &height=stacks.at(set.id.toString().toStdString()+":4:"+std::to_string(tile));
    auto height_dirty=update.tracking.changed(4,tile,height.revision,height.last_changes,size[0],size[1],full);
    const GpuMarginPlan *plan=nullptr;if(auto found=margin_plans.find(set.material_session_uid);found!=margin_plans.end())plan=(set.padding_3d?found->second.adjacent:found->second.extend).at(tile).get();
    GpuDerivedDirty::normal_halo(update.dirty,tile,GpuDerivedDirty::padded(std::move(height_dirty),plan),sizes);
   }
  }
  for(const auto &[tile,size]:sizes){auto &flags=update.flags[tile];flags.resize(((size[0]+255)/256)*((size[1]+255)/256));for(int chunk:update.dirty[tile])flags[chunk]=1;update.needed|=!update.dirty[tile].empty();}
  return update;
 }
 bool prepare_derived_masks(DerivedEntry &entry,const DerivedUpdate &update,const TextureSet &set){
  if(!entry.presence){entry.presence=texture("BTS cached derived presence",1,1,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!entry.presence)return false;const float zero[4]={};GPU_texture_clear(entry.presence.get(),GPU_DATA_FLOAT,zero);}
  for(const auto &[tile,flags]:update.flags){const int w=(set.width(tile)+255)/256,h=(set.rows(tile)+255)/256;auto &image=entry.dirty_images.try_emplace(tile,nullptr,GPU_texture_free).first->second;
   if(!image||GPU_texture_width(image.get())!=w||GPU_texture_height(image.get())!=h)image=texture("BTS derived dirty chunks",w,h,gpu::TextureFormat::UINT_32);
   if(!image)return false;GPU_texture_update(image.get(),GPU_DATA_UINT,flags.data());
  }return true;
 }
 void commit_derived(const TextureSet &set,int channel,gpu::Texture *target,const std::map<int,std::array<int,3>> &regions,DerivedUpdate update){
  const auto key=derived_key(set,channel);derived[key].tracking=std::move(update.tracking);
  for(const auto &[tile,offset]:regions)target_owners[std::pair{reinterpret_cast<std::uintptr_t>(target),offset}]=key;
  image_changed=true;std::size_t dirty_chunks=0;for(const auto &[tile,dirty]:update.dirty)dirty_chunks+=dirty.size();
  last_derived_chunks=dirty_chunks;last_derived_total=0;for(const auto &[tile,flags]:update.flags)last_derived_total+=flags.size();
  std::printf("BTS_GPU_DERIVED_DIRTY channel=%d chunks=%zu tiles=%zu global_presence_on_gpu=1\n",channel,dirty_chunks,regions.size());
 }
 struct AdjustmentEntry{std::weak_ptr<const AdjustmentPaintSnapshot> source;TextureSet set;QUuid layer;std::vector<std::string> native_aliases;std::map<std::tuple<QUuid,int,int>,NativeMaterialGpuContext> contexts;std::string failure;};
 std::map<QUuid,AdjustmentEntry> adjustment_sources;GpuAdjustmentPages adjustment_pages;Texture export_final{nullptr,GPU_texture_free};
 Texture world_weight{nullptr,GPU_texture_free};Shader world_weight_shader{nullptr,GPU_shader_free};Shader world_apply_shader{nullptr,GPU_shader_free};std::array<Shader,2> world_publish_shaders{Shader{nullptr,GPU_shader_free},Shader{nullptr,GPU_shader_free}};
 std::map<std::tuple<QUuid,QUuid,int>,std::shared_ptr<gpu::Texture>> native_normals;
 std::map<QUuid,std::string> native_normal_errors;std::uint64_t native_normal_revision=0;std::string native_image_failure;NativeMaterialGpuLibrary native_materials;NativeMaterialPreparationQueue native_preparations{native_materials};NativeContextProvider native_context_provider;Texture native_source{nullptr,GPU_texture_free};
 std::uint64_t last_dispatch_texels=0,last_cached_nodes=0,last_active_nodes=0;
 std::uint64_t frozen_prefix_copies=0,frozen_prefix_copy_texels=0;

 std::map<gpu::TextureFormat,Shader> uniform_shaders;bool uniform_failed=false;
 Texture scratch[2]{{nullptr,GPU_texture_free},{nullptr,GPU_texture_free}},final_image{nullptr,GPU_texture_free},zero{nullptr,GPU_texture_free};
 Shader coverage_shader{nullptr,GPU_shader_free},presence_shader{nullptr,GPU_shader_free};Texture ao_presence{nullptr,GPU_texture_free};std::map<std::pair<gpu::TextureFormat,bool>,Shader> ao_shaders,normal_shaders;struct NeighborEntry{Texture image{nullptr,GPU_texture_free};std::array<std::array<float,4>,9> values{};};std::map<std::string,NeighborEntry> normal_neighbors;
 Shader layer_shader{nullptr,GPU_shader_free},mask_shader{nullptr,GPU_shader_free},clear_shader{nullptr,GPU_shader_free};std::map<std::pair<gpu::TextureFormat,bool>,Shader> final_shaders,mask_output_shaders;gpu::Shader *final_shader=nullptr;
 int width=0,height=0;bool final_array=false;gpu::TextureFormat final_format=gpu::TextureFormat::SFLOAT_16_16_16_16;
 performance::Clock::time_point upload_start{};
 bool upload_time_available()const{return testing_stack||performance::ms(upload_start)<4;}
 bool authoring_time_available()const{return testing_stack||performance::ms(upload_start)<8;}
 std::uint64_t reported_authoring_pages=0;
 std::set<std::string> adjustment_input_pins;
 std::uint64_t remaining=upload_budget,remaining_work=32ULL*1024*1024,used_bytes=0,clock=0,resource_allocations=0;bool failed=false,input_rejected=false;
 using PreparedProgram=std::unique_ptr<GPUHostedShaderCompilation,decltype(&GPU_shader_hosted_compilation_free)>;
 PreparedProgram pending_layer{nullptr,GPU_shader_hosted_compilation_free},pending_mask{nullptr,GPU_shader_hosted_compilation_free};
 GpuProgramWarmup program_state;bool startup_prepared=false;
 struct DerivedProgram {PreparedProgram pending{nullptr,GPU_shader_hosted_compilation_free};};
 std::array<DerivedProgram,10> derived_programs;
 GpuProgramWarmup derived_program_state{10};bool derived_programs_started=false;
 static std::pair<gpu::TextureFormat,bool> derived_key(int index){
  return {index<6?gpu::TextureFormat::SFLOAT_16_16_16_16:gpu::TextureFormat::SFLOAT_32_32_32_32,((index-2)/2)%2!=0};
 }
 Shader &derived_output(int index){
  if(index==0)return coverage_shader;if(index==1)return presence_shader;
  return (index%2?normal_shaders:ao_shaders).at(derived_key(index));
 }
 bool poll_derived_program(int index){
  auto &pending=derived_programs[index].pending;
  if(!pending||!GPU_shader_hosted_compilation_ready(pending.get()))return true;
  Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()),GPU_shader_free};
  pending.reset();
  const bool okay=bool(completed);if(okay)derived_output(index)=std::move(completed);return okay;
 }
 bool programs_pending()const{return authoring_pages.programs_pending()||bool(pending_layer)||bool(pending_mask)||
  std::any_of(derived_programs.begin(),derived_programs.end(),[](const auto &program){return bool(program.pending);});}
 bool has_gpu()const{return adjustment_pages.has_gpu()||authoring_pages.programs_started()||programs_pending()||native_materials.statistics().resources||!uniform_shaders.empty()||bool(layer_shader)||bool(mask_shader)||bool(coverage_shader)||bool(presence_shader)||
  std::any_of(ao_shaders.begin(),ao_shaders.end(),[](const auto &entry){return bool(entry.second);})||
  std::any_of(normal_shaders.begin(),normal_shaders.end(),[](const auto &entry){return bool(entry.second);})||bool(scratch[0])||!inputs.empty()||!stacks.empty();}
 bool poll_program(int index){
  auto &pending=index==0?pending_layer:pending_mask;auto &result=index==0?layer_shader:mask_shader;
  if(!pending||!GPU_shader_hosted_compilation_ready(pending.get()))return true;
  Shader completed{GPU_shader_hosted_compilation_finalize(pending.get()),GPU_shader_free};
  pending.reset(); // Handle consumed before freeing its prepared sources.
  const bool okay=bool(completed);if(okay)result=std::move(completed);return okay;
 }
 void poll_programs(){
  program_state.drain([&](int index){return poll_program(index);});
  if(derived_programs_started)derived_program_state.drain([&](int index){return poll_derived_program(index);});
  authoring_pages.poll_programs();
 }
 void cancel_programs(){
  pending_layer.reset();pending_mask.reset();
  for(auto &program:derived_programs)program.pending.reset();
  authoring_pages.cancel_programs();
 } // Terminal owner cleanup only.
 bool use_program_workers()const{
  const auto *opt_in=std::getenv("BTS_HOSTED_SHADER_WORKERS");
  // Fixed-frame diagnostic self-tests retain their existing synchronous contract.
  return opt_in&&std::string_view(opt_in)=="1"&&!testing_stack&&!GPU_use_main_context_workaround();
 }
 GpuPreviewUpdate prepare_programs(){
  // Poll first even when resource/compile admission has already failed.
  poll_programs();if(failed&&!programs_pending())return GpuPreviewUpdate::Unsupported;
  const bool asynchronous=use_program_workers();
  const auto state=program_state.prepare([&](int index){
   const auto format=index==0?gpu::TextureFormat::SFLOAT_32_32_32_32:gpu::TextureFormat::SFLOAT_32;
   if(asynchronous){
    auto &pending=index==0?pending_layer:pending_mask;auto info=layer_info(format);
    gpu_derived::stabilize_hosted_resource_owners(info);
    pending.reset(GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
    if(!pending)throw std::runtime_error("Hosted GPU stack worker submission unavailable");
    std::printf("BTS_GPU_STACK_PROGRAM_SUBMITTED output=%s route=THREAD python_preprocessed=1\n",index==0?"RGBA":"mask");
   }else{auto &result=index==0?layer_shader:mask_shader;result=compile_layer(format);}
  },[&](int index){return poll_program(index);},
    [&](int index){return bool(index==0?layer_shader:mask_shader);},
    [&](int index){return bool(index==0?pending_layer:pending_mask);});
  // Submit the ten measured derived programs alongside the layer/mask pair.
  // All prepared CreateInfos are owned by their existing compiler handles;
  // readiness and ordinary retirement never wait or cancel running workers.
  auto derived_state=GpuProgramWarmup::State::Ready;
  if(asynchronous||derived_programs_started){
   if(!derived_programs_started){
    for(int index=2;index<10;index+=2){
     ao_shaders.try_emplace(derived_key(index),nullptr,GPU_shader_free);
     normal_shaders.try_emplace(derived_key(index),nullptr,GPU_shader_free);
    }
    derived_programs_started=true;
   }
   derived_state=derived_program_state.prepare([&](int index){
    auto info=[&]{
     if(index==0)return gpu_derived::coverage_info();
     if(index==1)return gpu_derived::presence_info();
     const auto [format,array]=derived_key(index);
     return index%2?gpu_derived::normal_info(format,array,quant_source):gpu_derived::ao_info(format,array,quant_source);
    }();
    auto &pending=derived_programs[index].pending;
    gpu_derived::stabilize_hosted_resource_owners(info);
    pending.reset(GPU_shader_hosted_python_async(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
    if(!pending)throw std::runtime_error("Hosted derived shader submission unavailable at slot "+std::to_string(index));
    std::printf("BTS_GPU_DERIVED_PROGRAM_SUBMITTED slot=%d route=THREAD python_preprocessed=1\n",index);
   },[&](int index){return poll_derived_program(index);},
     [&](int index){return bool(derived_output(index));},
     [&](int index){return bool(derived_programs[index].pending);});
  }
  auto authoring_state=GpuProgramWarmup::State::Ready;
  if(asynchronous||authoring_pages.programs_started())authoring_state=authoring_pages.prepare_worker_programs();
  if(state==GpuProgramWarmup::State::Pending||derived_state==GpuProgramWarmup::State::Pending||authoring_state==GpuProgramWarmup::State::Pending)return GpuPreviewUpdate::Pending;
  if(state==GpuProgramWarmup::State::Failed||derived_state==GpuProgramWarmup::State::Failed||authoring_state==GpuProgramWarmup::State::Failed){failed=true;return GpuPreviewUpdate::Unsupported;}
  return GpuPreviewUpdate::Updated;
 }
 GpuPreviewUpdate initialize(int w,int h,gpu::TextureFormat format,bool array){performance::Scope timing("gpu_shader_prepare");
  if(const auto state=prepare_programs();state!=GpuPreviewUpdate::Updated)return state;
  if(!zero){zero=texture("BTS preview zero",1,1,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!zero){failed=true;return GpuPreviewUpdate::Unsupported;}const float empty[4]={};GPU_texture_clear(zero.get(),GPU_DATA_FLOAT,empty);GPU_texture_filter_mode(zero.get(),true);}
  width=w;height=h;
  const auto key=std::pair{format,array};if(!final_shaders.contains(key))final_shaders.emplace(key,compile_final(format,array));final_shader=final_shaders.at(key).get();
  if(!final_shader){failed=true;return GpuPreviewUpdate::Unsupported;}return GpuPreviewUpdate::Updated;
 }
 Entry *entry(const std::string &key,int w,int h,bool resource,const std::set<std::string> &protected_keys){performance::Scope timing("gpu_source_allocate_or_find");
  if(auto found=inputs.find(key);found!=inputs.end()){if(found->second.width==w&&found->second.height==h&&found->second.resource==resource&&!found->second.rejected){found->second.used=clock;return &found->second;}if(found->second.retained())throw std::logic_error("Retained GPU source identity changed");used_bytes-=found->second.bytes();inputs.erase(found);}
  const auto bytes=std::uint64_t(w)*h*16;
  const auto cache_budget=input_budget.update(texture_vram_query_generation.load(),used_bytes,texture_free_vram.load(),texture_vram_query_valid.load()||texture_free_vram.load()!=0,texture_vram_query_valid.load()?0:texture_available_ram_bytes());
  while(used_bytes+bytes>cache_budget){auto victim=inputs.end();for(auto i=inputs.begin();i!=inputs.end();++i)if(!i->second.retained()&&!protected_keys.contains(i->first)&&!adjustment_input_pins.contains(i->first)&&(victim==inputs.end()||i->second.used<victim->second.used))victim=i;if(victim==inputs.end()){break;}used_bytes-=victim->second.bytes();inputs.erase(victim);}
  auto [found,inserted]=inputs.try_emplace(key);auto &e=found->second;e.width=w;e.height=h;e.resource=resource;e.used=clock;if(resource)e.residency=std::make_shared<const char>(0);e.image=texture("BTS cached preview source",w,h,gpu::TextureFormat::SFLOAT_32_32_32_32);
  if(!e.image){texture_memory_refresh_needed=true;e.rejected=true;input_rejected=true;return &e;}used_bytes+=bytes;if(resource){++resource_allocations;std::printf("BTS_GPU_RESOURCE_ALLOC key=%s bytes=%llu count=%llu\n",key.c_str(),(unsigned long long)bytes,(unsigned long long)resource_allocations);}if(!resource){performance::Scope timing("gpu_paint_clear");if(!clear_shader)clear_shader=compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);if(!clear_shader){e.rejected=input_rejected=true;return &e;}const float empty[4]={};GPU_shader_bind(clear_shader.get());GPU_shader_uniform_4fv(clear_shader.get(),"fill_value",empty);GPU_texture_image_bind(e.image.get(),0);GPU_compute_dispatch(clear_shader.get(),(w+15)/16,(h+15)/16,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_image_unbind(e.image.get());GPU_shader_unbind();}GPU_texture_filter_mode(e.image.get(),true);GPU_texture_extend_mode(e.image.get(),resource?GPU_SAMPLER_EXTEND_MODE_REPEAT:GPU_SAMPLER_EXTEND_MODE_EXTEND);return &e;
 }
 gpu::Texture *paint(const std::string &key,const PaintPixels &pixels,int channel,const std::set<std::string> &protected_keys,int polygon_depth=0){performance::Scope timing("gpu_paint_source_upload");
  PaintMetadataScope metadata;
  if(!pixels.page_count(channel))return zero.get();
  auto *e=entry(key,pixels.resolution(),pixels.resolution(),false,protected_keys);if(!e||e->rejected){input_rejected=true;return nullptr;}
  e->plane=channel;
  const auto versions=pixels.page_versions();const auto &wanted_versions=versions[channel];
  if(e->pages==wanted_versions)return e->image.get();
  auto &polygon=authoring_pages.polygon_raster();
  for(auto &[revision,patch]:e->polygon_undo)if(patch.after==e->pages&&patch.before==wanted_versions){
    performance::Scope timing("gpu_polygon_undo");polygon.restore(e->image.get(),patch.pixels);e->pages=wanted_versions;patch.used=++clock;std::printf("BTS_GPU_POLYGON_UNDO pages=%d bytes=%llu cpu_raster=0\n",patch.pixels.count,(unsigned long long)patch.pixels.bytes);return e->image.get();
  }
  const auto &root=pixels.recipe();const auto ink=root?root->ink.recipe():nullptr;
  if(polygon_depth<32&&root&&root->kind==PaintRecipe::Kind::Over&&root->complete_stroke&&!root->erase&&ink&&ink->kind==PaintRecipe::Kind::Stamp&&ink->base.page_versions()[channel].empty()&&(root->before.resolution()==0||root->before.resolution()==pixels.resolution())&&GpuPolygonRaster::supported(ink->stamp)){
    const auto before_versions=root->before.page_versions()[channel];
    if(!before_versions.empty()&&e->pages!=before_versions){auto protect=protected_keys;protect.insert(key);if(!paint(key,root->before,channel,protect,polygon_depth+1))return nullptr;}
    if(!authoring_time_available())return nullptr;
    if(before_versions.empty()&&!e->pages.empty()){std::vector<std::uint32_t> stale;for(const auto &[page,version]:e->pages)stale.push_back(page);polygon.clear(e->image.get(),stale);e->pages.clear();}
    if(e->pages==before_versions){
      const auto changed=GpuPolygonRaster::changed_pages(before_versions,wanted_versions);
      if(!before_versions.empty()&&!changed.empty()&&!e->polygon_undo.contains(root->revision)){
        const auto columns=std::uint64_t(std::ceil(std::sqrt(double(changed.size()))));const auto bytes=columns*64*((changed.size()+columns-1)/columns)*64*16+changed.size()*4;
        const auto budget=source_cache_budget(used_bytes);
        while(used_bytes+bytes>budget){Entry *victim=nullptr;std::uint64_t id=0,oldest=~std::uint64_t(0);for(auto &[name,entry]:inputs)for(auto &[rev,patch]:entry.polygon_undo)if(patch.used<oldest){victim=&entry;id=rev;oldest=patch.used;}if(!victim)break;const auto retired=victim->polygon_undo.at(id).pixels.bytes;victim->patch_bytes-=retired;used_bytes-=retired;victim->polygon_undo.erase(id);}
        if(used_bytes+bytes<=budget){PolygonUndo patch;patch.pixels=polygon.capture(e->image.get(),changed);patch.before=before_versions;patch.after=wanted_versions;patch.used=++clock;e->patch_bytes+=patch.pixels.bytes;used_bytes+=patch.pixels.bytes;e->polygon_undo.emplace(root->revision,std::move(patch));}
        else std::fprintf(stderr,"BTS_GPU_POLYGON_UNDO_CACHE skipped_bytes=%llu available_budget=%llu; reproducible GPU recipes retained\n",(unsigned long long)bytes,(unsigned long long)budget);
      }
      polygon.paint(e->image.get(),ink->stamp,channel,pixels.resolution(),changed,root->opacity);e->pages=wanted_versions;
      std::printf("BTS_GPU_POLYGON_RASTER channel=%d pages=%zu triangles=%zu cpu_raster=0 full_image_copy=0\n",channel,changed.size(),ink->stamp.surface->triangles.size());return e->image.get();
    }
  }
  bool complete=true;std::set<std::uint32_t> wanted;
  if(pixels.recipe()){
   const auto versions=pixels.page_versions();
   for(const auto &[page,version]:versions[channel]){wanted.insert(page);if(e->pages.contains(page)&&e->pages.at(page)==version)continue;if(!authoring_time_available()){complete=false;continue;}if(!authoring_pages.write(e->image.get(),pixels,channel,page,testing_stack?performance::Clock::time_point::max():upload_start+std::chrono::milliseconds(8))){complete=false;continue;}e->pages[page]=version;}
  }else pixels.visit_versioned_pages(channel,[&](auto page,auto version,std::span<const PaintPixels::Pixel> data){wanted.insert(page);if(e->pages.contains(page)&&e->pages.at(page)==version)return;const auto bytes=data.size_bytes();if(bytes>remaining){complete=false;return;}const int x=int(page%65536)*64,y=int(page/65536)*64;GPU_texture_update_sub(e->image.get(),GPU_DATA_FLOAT,data.data(),x,y,0,64,64,1);remaining-=bytes;e->pages[page]=version;});
  static const std::array<PaintPixels::Pixel,64*64> empty{};
  for(auto i=e->pages.begin();i!=e->pages.end();){if(wanted.contains(i->first)){++i;continue;}if(sizeof(empty)>remaining){complete=false;++i;continue;}GPU_texture_update_sub(e->image.get(),GPU_DATA_FLOAT,empty.data(),int(i->first%65536)*64,int(i->first/65536)*64,0,64,64,1);remaining-=sizeof(empty);i=e->pages.erase(i);}
  return complete?e->image.get():nullptr;
 }
 gpu::Texture *resource(const FillTexture &source,const std::set<std::string> &protected_keys){performance::Scope timing("gpu_resource_source_upload");
  auto *e=entry(resource_key(source),source.width,source.height,true,protected_keys);if(!e||e->rejected||!source.pixels){input_rejected=true;return nullptr;}
  while(e->row<e->height){if(!upload_time_available())return nullptr;const int rows=std::min({64,e->height-e->row,int(remaining/(std::uint64_t(e->width)*16))});if(!rows)return nullptr;
    // Decode workers already validate every component. Upload the immutable
    // shared storage directly, with no copy/finite scan/premultiply on the UI.
    const auto *values=source.pixels->data()+std::size_t(e->row)*e->width;
    GPU_texture_update_sub(e->image.get(),GPU_DATA_FLOAT,values,0,e->row,0,e->width,rows,1);remaining-=std::uint64_t(e->width)*rows*sizeof(FillTexture::Pixel);e->row+=rows;
  }return e->image.get();
 }
 gpu::Texture *baked(const MeshMapPlane *plane,const std::set<std::string> &protected_keys){
  if(!plane||!plane->pixels)return zero.get();const auto &pixels=*plane->pixels;const auto key="baked:"+pixels.hash+":"+std::to_string(pixels.resolution)+":"+std::to_string(pixels.height);
  auto *e=entry(key,pixels.resolution,pixels.height,true,protected_keys);if(!e||e->rejected)return nullptr;
  GPU_texture_extend_mode(e->image.get(),GPU_SAMPLER_EXTEND_MODE_EXTEND);
  while(e->row<e->height){if(!upload_time_available())return nullptr;const int rows=std::min({64,e->height-e->row,int(remaining/(std::uint64_t(e->width)*16))});if(!rows)return nullptr;
   GPU_texture_update_sub(e->image.get(),GPU_DATA_FLOAT,pixels.rgba.data()+std::size_t(e->row)*e->width*4,0,e->row,0,e->width,rows,1);remaining-=std::uint64_t(e->width)*rows*16;e->row+=rows;
  }return e->image.get();
 }

 gpu::Texture *mask_prefix(const std::string &key,const std::vector<GpuMaskStage> &stages,
                          const std::array<gpu::Texture *,6> &maps,int tile,int w,int h,
                          const std::set<std::string> &protected_keys,const std::string &map_signature,const GpuComparisonResolver &resolve,const GpuAnchorResolver &resolve_anchor,bool partial=false);
};
gpu::Texture *GpuLayerPreview::Impl::mask_prefix(
    const std::string &key,const std::vector<GpuMaskStage> &stages,
    const std::array<gpu::Texture *,6> &maps,int tile,int w,int h,
    const std::set<std::string> &protected_keys,const std::string &map_signature,const GpuComparisonResolver &resolve,const GpuAnchorResolver &resolve_anchor,bool partial)
{
 performance::Scope timing("gpu_mask_prefix");
 for(const auto &stage:stages)if(stage.kind==GpuMaskStage::SpatialFill&&stage.surface_required&&(!stage.geometry||!stage.geometry->tiles.contains(tile)))return nullptr;
 auto &cache=mask_stacks[key];if(cache.width!=w||cache.height!=h){cache.output_dirty.clear();cache.node_versions.clear();cache.width=w;cache.height=h;}
 std::vector<GpuStackNode> nodes;
 const auto append=[](std::string &bytes,const auto &value){bytes.append(reinterpret_cast<const char *>(&value),sizeof(value));};
 for(const auto &stage:stages){
  bool comparison_ready=true;
  GpuStackNode node;node.id=stage.id;if(!nodes.empty())node.dependencies={nodes.back().id};
  int slot=0;gpu_visit_mask_stage(stage,[&](const auto &part){
   node.parameters+=part.owner_key+part.id+part.anchor_key+part.anchor_id;if(part.bitmap)node.parameters+=part.bitmap->asset+part.bitmap->hash;append(node.parameters,part.kind);append(node.parameters,part.background);append(node.parameters,part.opacity);append(node.parameters,part.blend);
   for(const auto &row:part.rows)append(node.parameters,row);if(part.spatial){append(node.parameters,part.spatial->values);append(node.parameters,reinterpret_cast<std::uintptr_t>(part.geometry.get()));}
   if(part.kind==GpuMaskStage::Points)node.parameters+=map_signature;
   if(part.generator){auto values=gpu_generator_inputs(*part.generator,part.comparison_layer,resolve_anchor);if(!values)comparison_ready=false;else gpu_generator_dependencies(node,*part.generator,*values,slot);}if(part.kind==GpuMaskStage::Compare){auto values=resolve(part);if(!values)comparison_ready=false;else gpu_comparison_dependencies(node,*values,slot);}if(part.kind==GpuMaskStage::AnchorFill){append(node.parameters,part.reference.channel);append(node.parameters,part.reference.component);append(node.parameters,part.reference.coverage);append(node.parameters,part.content_anchor);if(part.content_anchor){auto value=resolve_anchor(part.comparison_layer,part.reference);if(!value)comparison_ready=false;else gpu_anchor_dependencies(node,*value,slot);}}
   if(part.pixels){const int index=slot++;const auto &pixels=part.pixels->tile(tile);const int resolution=pixels.resolution();append(node.parameters,resolution);
    if(resolution){const double sx=double(w)/resolution,sy=double(h)/resolution;const auto versions=pixels.page_versions();
     for(const auto &[page,revision]:versions[2]){const int px=int(page%65536)*64,py=int(page/65536)*64;const int x=int(std::floor(px*sx))-1,y=int(std::floor(py*sy))-1,right=int(std::ceil((px+64)*sx))+1,bottom=int(std::ceil((py+64)*sy))+1;node.pages[(std::uint64_t(index)<<32)|page]={x,y,right-x,bottom-y,revision};}
    }
   }
  });
  if(!comparison_ready)return nullptr;nodes.push_back(std::move(node));
 }
 auto transaction=cache.plan.prepare(w,h,nodes);
 std::set<std::string> live;for(const auto &node:nodes)live.insert(node.id);
 if(!partial)std::erase_if(cache.outputs,[&](const auto &entry){return !live.contains(entry.first);});
 struct Input {gpu::Texture *source=nullptr,*comparison=nullptr,*effects=nullptr,*parameters=nullptr,*surface=nullptr;std::array<gpu::Texture *,6> maps{};bool anchor_valid=false,anchor_mask=true;};
 std::vector<Input> resolved(stages.size());
 for(std::size_t i=0;i<stages.size();++i){
  if(transaction.work[i].regions.empty())continue;
  const auto &stage=stages[i];const auto identity=key+":"+stage.id;auto &in=resolved[i];
  in.source=stage.bitmap?resource(*stage.bitmap,protected_keys):stage.pixels?paint(identity,stage.pixels->tile(tile),2,protected_keys):zero.get();if(!in.source)return nullptr;
  if(stage.anchor_stages){if(!mask_prefix(stage.anchor_key,*stage.anchor_stages,maps,tile,w,h,protected_keys,map_signature,resolve,resolve_anchor,true))return nullptr;in.source=mask_stacks.at(stage.anchor_key).outputs.at(stage.anchor_id).get();in.anchor_valid=true;}if(stage.content_anchor){auto value=resolve_anchor(stage.comparison_layer,stage.reference);if(!value)return nullptr;in.source=value->image;in.anchor_valid=value->enabled;in.anchor_mask=value->mask;}
  in.maps=maps;auto rows=stage.rows;if(stage.generator){auto values=gpu_generator_inputs(*stage.generator,stage.comparison_layer,resolve_anchor);if(!values)return nullptr;gpu_generator_options(rows.front(),*stage.generator,*values);gpu_generator_maps(in.maps,*stage.generator,*values);}
  in.effects=effects(identity,rows);if(!in.effects)return nullptr;
  in.surface=surface(stage.geometry,tile);if(!in.surface)return nullptr;
  GpuLayerParameters params=stage.spatial.value_or(GpuLayerParameters{});const float anchor_options[4]={float(stage.reference.component),float(stage.reference.coverage),float(in.anchor_mask),float(in.anchor_valid)};params.set4("anchor_options",anchor_options);params.set("mask_pass",stage.kind);params.set("mask_background",stage.background);params.set("opacity_value",stage.opacity);params.set("blend_mode",int(stage.blend));params.set("effect_count",stage.rows.size());
  in.comparison=zero.get();if(stage.kind==GpuMaskStage::Compare){auto values=resolve(stage);if(!values)return nullptr;in.source=(*values)[0].image;in.comparison=(*values)[1].image;params.set("compare_channel",stage.comparison_channel);const int enabled[2]={(*values)[0].enabled,(*values)[1].enabled};params.set2("compare_enabled",enabled);params.set3("compare_base",(*values)[0].base.data());}
  in.parameters=parameters(identity,params);if(!in.parameters){input_rejected=true;return nullptr;}
 }
 auto *shader=mask_shader.get();GPU_shader_bind(shader);
 for(std::size_t i=0;i<stages.size();++i){
  const auto &work=transaction.work[i];if(work.regions.empty())continue;
  auto found=cache.outputs.find(work.id);
  if(found==cache.outputs.end()||GPU_texture_width(found->second.get())!=w||GPU_texture_height(found->second.get())!=h){
   auto output=texture("BTS shared GPU mask prefix",w,h,gpu::TextureFormat::SFLOAT_32);if(!output){input_rejected=true;GPU_shader_unbind();return nullptr;}
   cache.outputs.insert_or_assign(work.id,std::move(output));
  }
  auto *output=cache.outputs.at(work.id).get();const auto &in=resolved[i];
  GPU_texture_bind(in.source,0);GPU_texture_bind(in.comparison,1);GPU_texture_bind(i?cache.outputs.at(stages[i-1].id).get():zero.get(),2);
  GPU_texture_bind(in.surface,3);GPU_texture_bind(in.effects,4);GPU_texture_bind(in.parameters,5);
  for(int m=0;m<6;++m)GPU_texture_bind(in.maps[m],gpu_mask_map_slot(m));
  GPU_texture_bind(geometry_masks.empty(),9);GPU_texture_bind(geometry_masks.empty(),10);
  GPU_texture_image_bind(output,0);
  for(const auto &r:work.regions){const int origin[2]={r.x,r.y},size[2]={r.width,r.height};GPU_shader_uniform_2iv(shader,"region_origin",origin);GPU_shader_uniform_2iv(shader,"region_size",size);GPU_compute_dispatch(shader,(r.width+15)/16,(r.height+15)/16,1);}
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);GPU_texture_image_unbind(output);
 }
 GPU_shader_unbind();for(const auto &work:transaction.work)if(!work.regions.empty()){const auto version=++cache.node_clock;for(const auto &r:work.regions){const auto chunk=std::uint64_t(r.y/256)*((w+255)/256)+r.x/256;cache.node_versions[work.id][chunk]={r.x,r.y,r.width,r.height,version};}}if(partial){mask_dispatch_texels+=transaction.texels;cache.plan.commit(std::move(transaction),true);return cache.outputs.at(stages.back().id).get();}if(cache.head_id!=stages.back().id){for(int y=0;y<h;y+=256)for(int x=0;x<w;x+=256)cache.output_dirty.insert(y/256*((w+255)/256)+x/256);cache.head_id=stages.back().id;}for(const auto &region:transaction.work.back().regions)cache.output_dirty.insert(region.y/256*((w+255)/256)+region.x/256);mask_dispatch_texels+=transaction.texels;cache.plan.commit(std::move(transaction));cache.head=cache.outputs.at(stages.back().id).get();return cache.head;
}

// Main-thread retirement; disposal happens only on the active GPU owner.
std::vector<std::unique_ptr<GpuLayerPreview::Impl>> &GpuLayerPreview::retired(){static std::vector<std::unique_ptr<Impl>> values;return values;}
GpuLayerPreview::GpuLayerPreview():impl_(std::make_unique<Impl>()){bind_adjustment_resolver();}
void GpuLayerPreview::bind_adjustment_resolver(){impl_->authoring_pages.adjustment_resolver([this](const SmudgeCapture &capture,int plane,int tile,std::uint32_t page,gpu::Texture *own,auto deadline){return adjustment_page(capture,plane,tile,page,own,deadline);});}
GpuLayerPreview::~GpuLayerPreview(){if(impl_&&impl_->has_gpu())retired().push_back(std::move(impl_));}
std::uint64_t GpuLayerPreview::release_bake_scratch(){
 auto &s=*impl_;std::uint64_t bytes=0;
 for(const auto *cache:{&s.stacks,&s.mask_stacks})for(const auto &[key,stack]:*cache)for(const auto &[id,image]:stack.outputs)if(image.use_count()==1)bytes=resource_sum(bytes,resource_product(resource_product(GPU_texture_width(image.get()),GPU_texture_height(image.get())),4*GPU_texture_component_len(GPU_texture_format(image.get()))));
 s.comparison_memo.clear();s.anchor_memo.clear();s.stacks.clear();s.mask_stacks.clear();
 for(auto i=s.inputs.begin();i!=s.inputs.end();)if(!i->second.retained()&&!s.adjustment_input_pins.contains(i->first)){bytes=resource_sum(bytes,i->second.bytes());s.used_bytes-=i->second.bytes();i=s.inputs.erase(i);}else ++i;
 texture_memory_refresh_needed=true;std::printf("BTS_BAKE_RELEASE reusable_bytes=%llu\n",(unsigned long long)bytes);return bytes;
}
void GpuLayerPreview::reset(bool terminal){auto previous=std::move(impl_);impl_=std::make_unique<Impl>();
 if(!terminal){impl_->adjustment_sources=std::move(previous->adjustment_sources);impl_->adjustment_pages=std::move(previous->adjustment_pages);impl_->native_context_provider=previous->native_context_provider;for(const auto &[id,entry]:impl_->adjustment_sources)for(const auto &key:entry.native_aliases)impl_->native_materials.retain_alias_from(previous->native_materials,key);}
 bind_adjustment_resolver();if(previous->has_gpu())retired().push_back(std::move(previous));}
void GpuLayerPreview::collect_retired(bool terminal){std::erase_if(retired(),[&](const auto &state){
 // Ordinary retirement only polls: freeing a running prepared-info owner would
 // wait inside the compiler cancellation API. GUI shutdown may explicitly wait.
 if(terminal)state->cancel_programs();else state->poll_programs();
  // Terminal owner shutdown has joined/detached all image users. Metadata
 // residency leases cannot keep GPU allocations alive past GPU_exit.
 return !state->programs_pending()&&(terminal||std::none_of(state->inputs.begin(),state->inputs.end(),[](const auto &entry){return entry.second.retained();}));
});}
void GpuLayerPreview::poll_programs(){collect_retired();impl_->poll_programs();}
bool GpuLayerPreview::programs_pending()const{return impl_->programs_pending()||std::any_of(retired().begin(),retired().end(),[](const auto &state){return state->programs_pending();});}
std::string GpuLayerPreview::take_program_error(){
 if(impl_->programs_pending())return {};
 auto error=impl_->program_state.take_error();
 if(error.empty())if(auto derived=impl_->derived_program_state.take_error();!derived.empty())error="Derived warmup: "+derived;
 if(error.empty())if(auto authoring=impl_->authoring_pages.take_program_error();!authoring.empty())error="Authoring warmup: "+authoring;
 return error;
}
void GpuLayerPreview::begin_frame(){impl_->authoring_pages.retire_payloads();impl_->native_materials.step_image_uploads();
 for(auto i=impl_->adjustment_sources.begin();i!=impl_->adjustment_sources.end();){if(!i->second.source.expired()){++i;continue;}const auto id=i->first;const auto prefix=i->second.set.id.toString().toStdString();impl_->adjustment_pages.retire(id);for(const auto &key:i->second.native_aliases)impl_->native_materials.erase(key);std::erase_if(impl_->stacks,[&](const auto &p){return p.first.starts_with(prefix+":");});std::erase_if(impl_->mask_stacks,[&](const auto &p){return p.first.starts_with(prefix);});i=impl_->adjustment_sources.erase(i);}
 impl_->comparison_memo.clear();impl_->anchor_memo.clear();
 if(std::getenv("BTS_RESOLUTION_LOG")&&performance::Clock::now()-impl_->cache_logged>=std::chrono::seconds(2)){
  auto &s=*impl_;s.cache_logged=performance::Clock::now();
  std::uint64_t prefix=0,mask=0;for(const auto &[key,stack]:s.stacks)for(const auto &[id,image]:stack.outputs)prefix+=std::uint64_t(GPU_texture_width(image.get()))*GPU_texture_height(image.get())*16;
  for(const auto &[key,stack]:s.mask_stacks)for(const auto &[id,image]:stack.outputs)mask+=std::uint64_t(GPU_texture_width(image.get()))*GPU_texture_height(image.get())*4;
  std::printf("BTS_GPU_CACHE authoring_pages=%zu authoring_bytes=%llu computed=%llu evicted=%llu page_allocations=%llu page_reuses=%llu source_bytes=%llu prefix_bytes=%llu mask_bytes=%llu all_texture_bytes=%llu driver_free_bytes=%llu\n",s.authoring_pages.resident_pages(),(unsigned long long)(s.authoring_pages.resident_pages()*64ULL*64*16),(unsigned long long)s.authoring_pages.computed_pages(),(unsigned long long)s.authoring_pages.evicted_pages(),(unsigned long long)s.authoring_pages.image_allocations(),(unsigned long long)s.authoring_pages.image_reuses(),(unsigned long long)s.used_bytes,(unsigned long long)prefix,(unsigned long long)mask,(unsigned long long)blender::gpu::host_texture_memory::live_bytes(),(unsigned long long)texture_free_vram.load());
  auto groups=blender::gpu::host_texture_memory::snapshot();std::vector<std::pair<std::string,blender::gpu::host_texture_memory::Group>> sorted(groups.begin(),groups.end());std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b){return a.second.bytes>b.second.bytes;});QJsonArray top;for(std::size_t i=0;i<std::min<std::size_t>(12,sorted.size());++i)top.append(QJsonObject{{"label",QString::fromStdString(sorted[i].first)},{"bytes",qint64(sorted[i].second.bytes)},{"count",qint64(sorted[i].second.count)}});std::printf("BTS_GPU_TEXTURE_MEMORY %s\n",QJsonDocument(top).toJson(QJsonDocument::Compact).constData());
 }
if(performance::enabled()){auto &s=*impl_;const auto count=s.authoring_pages.computed_pages();if(count!=s.reported_authoring_pages){std::printf("BTS_GPU_AUTHORING pages=%llu total=%llu evicted=%llu resident=%zu\n",(unsigned long long)(count-s.reported_authoring_pages),(unsigned long long)count,(unsigned long long)s.authoring_pages.evicted_pages(),s.authoring_pages.resident_pages());s.reported_authoring_pages=count;}}static bool tested=false;if(!tested&&std::getenv("BTS_VERIFY_GPU_STACK")){tested=true;self_test();}collect_retired();impl_->poll_programs();
 if(!impl_->startup_prepared&&!impl_->failed&&(!(impl_->programs_pending()||impl_->use_program_workers())||impl_->prepare_programs()==GpuPreviewUpdate::Updated)){
  performance::Scope timing("gpu_stack_program_warmup");
  {performance::Scope part("gpu_stack_warmup_authoring_pages");impl_->authoring_pages.prepare();}
  {performance::Scope part("gpu_adjustment_source_warmup");impl_->adjustment_pages.prepare();}
  {performance::Scope part("gpu_stack_warmup_ranges");impl_->ranges.prepare();}
  {performance::Scope part("gpu_stack_warmup_histograms");impl_->histograms.prepare();}
  {performance::Scope part("gpu_stack_warmup_margins");if(!impl_->margins.prepare_shaders())throw std::runtime_error("GPU margin shader preparation failed");}
  for(auto format:{gpu::TextureFormat::SFLOAT_16_16_16_16,gpu::TextureFormat::SFLOAT_32_32_32_32}){
   {performance::Scope part("gpu_stack_warmup_finals");for(bool array:{false,true})impl_->initialize(1,1,format,array);}
   if(!impl_->uniform_shaders.contains(format)){performance::Scope part("gpu_stack_warmup_uniform");impl_->uniform_shaders.emplace(format,compile_uniform(format));}
  }
  if(!impl_->clear_shader){performance::Scope part("gpu_stack_warmup_uniform");impl_->clear_shader=compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);}
  if(!impl_->derived_programs_started){performance::Scope part("gpu_stack_warmup_derived");
   impl_->coverage_shader=Shader(gpu_derived::coverage_shader(),GPU_shader_free);impl_->presence_shader=Shader(gpu_derived::presence_shader(),GPU_shader_free);
   for(auto format:{gpu::TextureFormat::SFLOAT_16_16_16_16,gpu::TextureFormat::SFLOAT_32_32_32_32})for(bool array:{false,true}){const auto key=std::pair{format,array};impl_->ao_shaders.emplace(key,Shader(gpu_derived::ao_shader(format,array,quant_source),GPU_shader_free));impl_->normal_shaders.emplace(key,Shader(gpu_derived::normal_shader(format,array,quant_source),GPU_shader_free));}
  }
  impl_->startup_prepared=true;
 }
 // Explicit functional readback suite, after the separately measured owner
 // warmup block. Never startup/performance evidence, never enabled by default.
 static bool authoring_verified=false;
 if(!authoring_verified&&std::getenv("BTS_GPU_AUTHORING_PROGRAM_TEST")&&impl_->startup_prepared){
  if(!impl_->authoring_pages.programs_started())throw std::runtime_error("Authoring oracle requires the hosted worker route");
  performance::Scope verification("gpu_authoring_worker_verification");verify_gpu_authoring_workers(impl_->authoring_pages);authoring_verified=true;
 }
 impl_->remaining=upload_budget;impl_->upload_start=performance::Clock::now();impl_->remaining_work=32ULL*1024*1024;}
NativeMaterialGpuLibrary &GpuLayerPreview::native_world_sources(){return impl_->native_materials;}
std::string GpuLayerPreview::world_normal_mask_signature(const TextureSet &set,int tile,std::span<const QUuid> masks,std::uint64_t dependent_epoch)const{
 PaintMetadataScope metadata;std::string result;GpuMaskBuilder builder(set,tile);const auto &layers=evaluation_layers(set);bool dependent=false;
 const auto append=[](std::string &key,const auto &value){key.append(reinterpret_cast<const char *>(&value),sizeof(value));};
 for(const auto &id:masks){const auto layer=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==id;});if(layer==layers.end()||!layer->mask)throw std::invalid_argument("Native world Normal mask owner missing");const auto stages=builder.build(*layer,true);for(const auto &stage:stages)gpu_visit_mask_stage(stage,[&](const auto &part){result+=part.owner_key+part.id+part.anchor_key+part.anchor_id;append(result,part.kind);append(result,part.background);append(result,part.opacity);append(result,part.blend);for(const auto &row:part.rows)append(result,row);if(part.spatial){append(result,part.spatial->values);append(result,reinterpret_cast<std::uintptr_t>(part.geometry.get()));}if(part.bitmap)result+=part.bitmap->asset+part.bitmap->hash;if(part.pixels){const auto &pixels=part.pixels->tile(tile);append(result,pixels.resolution());pixels.visit_page_versions(2,[&](auto key,auto revision){append(result,key);append(result,revision);});}dependent|=part.kind==GpuMaskStage::Compare||part.kind==GpuMaskStage::AnchorFill||bool(part.generator)||bool(part.anchor_stages)||part.content_anchor;},true);}
 // External anchor/comparison prefixes may depend on a different channel. Keep
 // their existing conservative document epoch; ordinary masks remain tile/page
 // specific and unrelated source Color/Roughness edits do not rerender W.
 if(dependent)append(result,dependent_epoch);
 for(const auto &map:set.mesh_maps)if(map)if(const auto member=map->tiles.find(tile);member!=map->tiles.end()&&member->second.pixels)result+=member->second.pixels->hash;
 return result;
}
GpuPreviewUpdate GpuLayerPreview::prepare_world_normal_weight(const TextureSet &set,int tile,float opacity,std::span<const QUuid> masks,bool array){
 auto &s=*impl_;const int w=set.width(tile),h=set.rows(tile);if(!std::isfinite(opacity))return GpuPreviewUpdate::Unsupported;
 if(!s.world_weight||GPU_texture_width(s.world_weight.get())!=w||GPU_texture_height(s.world_weight.get())!=h){s.world_weight=texture("Native world Normal mask weight",w,h,gpu::TextureFormat::SFLOAT_32);}
 std::vector<gpu::Texture *> resolved;for(const auto &id:masks){auto *mask=mask_texture(set,id,tile);if(!mask)return GpuPreviewUpdate::Pending;resolved.push_back(mask);}
 const float weight[4]{std::clamp(opacity,0.0f,1.0f),0,0,0};GPU_texture_clear(s.world_weight.get(),GPU_DATA_FLOAT,weight);
 if(!resolved.empty()){if(!s.world_weight_shader)s.world_weight_shader=compile_world_weight();if(!s.world_weight_shader)return GpuPreviewUpdate::Unsupported;auto *shader=s.world_weight_shader.get();GPU_shader_bind(shader);GPU_texture_image_bind(s.world_weight.get(),0);for(auto *mask:resolved){GPU_texture_bind(mask,0);GPU_compute_dispatch(shader,(w+15)/16,(h+15)/16,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);}GPU_texture_image_unbind(s.world_weight.get());GPU_texture_unbind_all();GPU_shader_unbind();}
 const auto plans=s.margin_plans.find(set.material_session_uid);if(plans==s.margin_plans.end()||!plans->second.extend.contains(tile))return GpuPreviewUpdate::Pending;
 if(!s.margins.prepare(plans->second.extend.at(tile),s.remaining,s.upload_start,testing_stack))return GpuPreviewUpdate::Pending;
 if(!s.world_apply_shader)s.world_apply_shader=compile_world_apply();if(!s.world_apply_shader)return GpuPreviewUpdate::Unsupported;for(bool kind:{false,array}){auto &program=s.world_publish_shaders[kind];if(!program)program=compile_world_publish(kind);if(!program)return GpuPreviewUpdate::Unsupported;}return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::publish_world_normal(const TextureSet &set,int tile,gpu::Texture *source,gpu::Texture *destination,std::array<int,3> offset,bool apply_weight){
 auto &s=*impl_;const int w=set.width(tile),h=set.rows(tile);if(!source||!destination||!s.world_weight||GPU_texture_width(s.world_weight.get())!=w||GPU_texture_height(s.world_weight.get())!=h)return GpuPreviewUpdate::Unsupported;
 if(source==destination){if(!apply_weight||GPU_texture_is_array(destination)||!s.world_apply_shader)return GpuPreviewUpdate::Unsupported;auto *shader=s.world_apply_shader.get();GPU_shader_bind(shader);GPU_texture_bind(s.world_weight.get(),0);GPU_texture_image_bind(destination,0);GPU_compute_dispatch(shader,(w+15)/16,(h+15)/16,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS|GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_image_unbind(destination);GPU_texture_unbind_all();GPU_shader_unbind();return GpuPreviewUpdate::Updated;}
 const bool array=GPU_texture_is_array(destination);auto &program=s.world_publish_shaders[array];if(!program)return GpuPreviewUpdate::Unsupported;auto *shader=program.get();GPU_shader_bind(shader);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());GPU_shader_uniform_1i(shader,"apply_weight",apply_weight);GPU_texture_bind(source,0);GPU_texture_bind(s.world_weight.get(),1);GPU_texture_image_bind(destination,0);GPU_compute_dispatch(shader,(w+15)/16,(h+15)/16,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_image_unbind(destination);GPU_texture_unbind_all();GPU_shader_unbind();return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::pad_world_normal(const TextureSet &set,int tile,gpu::Texture *image,int side){
 auto &s=*impl_;const auto plans=s.margin_plans.find(set.material_session_uid);if(plans==s.margin_plans.end()||!plans->second.extend.contains(tile))return GpuPreviewUpdate::Pending;
 // Geometry-only extend donors touch outside UV islands, never an interior
 // mask hole. Signed vector and coverage use the same generic RGBA operation.
 const int w=set.width(tile),h=set.rows(tile);std::vector<std::uint32_t> dirty(((w+255)/256)*((h+255)/256),1);
 const auto key=set.id.toString().toStdString()+":world:"+std::to_string(side)+":"+std::to_string(tile);
 return s.margins.apply(key,plans->second.extend.at(tile),image,{},std::move(dirty))?GpuPreviewUpdate::Updated:GpuPreviewUpdate::Unsupported;
}
void GpuLayerPreview::publish_native_material_preview(const std::string &resource,const std::string &hash){impl_->native_preparations.publish_preview(resource,hash);}
void GpuLayerPreview::prepare_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph> graph){impl_->native_preparations.request(std::move(resource),std::move(content),std::move(graph));}
void GpuLayerPreview::cancel_native_preparation(const std::string &resource){impl_->native_preparations.cancel(resource);}
void GpuLayerPreview::discard_native_candidate(const std::string &resource){impl_->native_preparations.cancel(resource);impl_->native_materials.erase(resource);}
void GpuLayerPreview::alias_native_material(const std::string &source,const std::string &destination){impl_->native_materials.alias(source,destination);}
void GpuLayerPreview::validate_native_mapping_instances(const TextureSet &set,const TextureSet *before)const{
 for(const auto &layer:evaluation_layers(set))if(layer.kind==LayerKind::Fill&&layer.fill.native_material){
  const auto &after=*layer.fill.native_material;
  require_native_height_fill(after.height_policy,set.bump_origin,layer.fill.mode,layer.channels[3]);
  if(after.height_policy!=NativeHeightPolicy::None)impl_->native_materials.validate_instance(layer.fill.material_resource,after);
  if(std::none_of(after.parameters.begin(),after.parameters.end(),[](const auto &p){return !p.mapping_transform.identity();}))continue;
  const NativeMaterialDescription *old=nullptr;bool same_resource=false;
  if(before){const auto &layers=evaluation_layers(*before);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &value){return value.id==layer.id;});
   if(found!=layers.end()&&found->fill.native_material){const auto &candidate=*found->fill.native_material;bool same_interface=candidate.group==after.group&&candidate.surface_type==after.surface_type&&candidate.parameters.size()==after.parameters.size();
    if(same_interface)for(std::size_t i=0;i<after.parameters.size();++i){const auto &a=candidate.parameters[i],&b=after.parameters[i];same_interface=same_interface&&a.identifier==b.identifier&&a.type==b.type&&a.source_node==b.source_node&&a.source_socket==b.source_socket&&a.mapping==b.mapping;}
    if(same_interface){old=&candidate;same_resource=found->fill.material_resource==layer.fill.material_resource;}
   }
   // This preserves prior authored row state, not source lineage/equivalence.
   // A different prepared source still passes its actual layout/edge checks.
   if(old&&same_resource){bool unchanged=true;for(std::size_t i=0;i<after.parameters.size();++i)unchanged=unchanged&&old->parameters[i].mapping_transform==after.parameters[i].mapping_transform;if(unchanged)continue;}
  }
  // Packed Open may queue a saved instance while its source is still preparing.
  // Once ready, signature/render enforce the same original-edge safety again.
  if(!impl_->native_materials.contains(layer.fill.material_resource)){if(before)throw std::invalid_argument("Native material is still preparing; retry the Mapping edit when ready");continue;}
  if(before)impl_->native_materials.validate_mapping_edit(layer.fill.material_resource,old,after);else impl_->native_materials.validate_instance(layer.fill.material_resource,after);
 }
}
bool GpuLayerPreview::has_native_material(const std::string &resource)const{return impl_->native_materials.contains(resource);}
bool GpuLayerPreview::native_material_uses_object(const std::string &resource)const{return impl_->native_materials.uses_object(resource); }
bool GpuLayerPreview::native_material_uses_ao(const std::string &resource)const{return impl_->native_materials.uses_ao(resource);}
NativeMaterialDependencies GpuLayerPreview::native_requirements(const std::string &resource,const NativeMaterialDescription &instance)const{return impl_->native_materials.requirements(resource,instance);}
void GpuLayerPreview::prepare_native_height(const std::string &resource,NativeMaterialHeightResource value){impl_->native_materials.prepare_height(resource,std::move(value));}
void GpuLayerPreview::stage_native_height(NativeMaterialHeightGpu &work,NativeMaterialHeightResource resource){work.impl_->candidate=impl_->native_materials.stage_height(work.impl_->key,std::move(resource));}
bool GpuLayerPreview::step_native_height(NativeMaterialHeightGpu &work){if(work.ready_)return true;if(!work.impl_->candidate)throw std::logic_error("Native Height candidate is missing");return work.ready_=impl_->native_materials.step_height_candidate(work.impl_->key,*work.impl_->candidate);}
void GpuLayerPreview::install_native_height(NativeMaterialHeightGpu &work){if(!work.ready_)throw std::logic_error("Native Height candidate is not ready");if(work.impl_->change)throw std::logic_error("Raw authored Height publication already began");work.impl_->change=impl_->native_materials.install_height_candidate(work.impl_->key,std::move(work.impl_->candidate));}
bool GpuLayerPreview::set_native_height_policy(TextureSet &set,const QUuid &id,NativeHeightPolicy policy)const{
 auto &layer=TextureDocument::layer(set,id);
 require_native_height_fill(policy,set.bump_origin,layer.fill.mode,layer.channels[3]); // Before any candidate edit.
 if(layer.kind!=LayerKind::Fill||layer.instance||!layer.fill.native_material)throw std::invalid_argument("Raw authored Height requires an editable native Fill instance");
 if(layer.fill.native_material->height_policy==policy)return false;
 if(policy!=NativeHeightPolicy::None&&!set.channels[4])throw std::invalid_argument("Enable the Texture Set Bump channel before selecting raw authored Height");
 auto instance=std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);instance->height_policy=policy;
 // Actual prepared source/sidecar state is the only capability authority.
 // Saved descriptor channels and source names never grant raw Height support.
 instance->channels=impl_->native_materials.effective_channels(layer.fill.material_resource,*instance);
 const bool bump=std::find(instance->channels.begin(),instance->channels.end(),4)!=instance->channels.end();
 layer.fill.native_material=std::move(instance);layer.channels[4]=bump;
 return true;
}
void GpuLayerPreview::probe_native_height(const std::string &key,const NativeMaterialDescription &instance,NativeMaterialGpuContext context){impl_->native_materials.probe_height(key,instance,std::move(context));}
CompositeChannels GpuLayerPreview::native_view_channels(const TextureSet &set)const{CompositeChannels result{};for(const auto &layer:evaluation_layers(set))if(layer.visible&&layer.fill.native_material&&impl_->native_materials.contains(layer.fill.material_resource))for(int channel:impl_->native_materials.view_channels(layer.fill.material_resource,*layer.fill.native_material))result[channel]=true;return result;}
void GpuLayerPreview::step_native_preparations(){impl_->native_preparations.step();}
bool GpuLayerPreview::native_preparations_pending()const{return impl_->native_preparations.pending()||impl_->native_materials.image_uploads_pending();}
NativeMaterialPreparationStatus GpuLayerPreview::native_preparation_status(const std::string &resource)const{auto state=impl_->native_preparations.status(resource);if(const auto error=impl_->native_materials.image_error(resource);!error.isEmpty()){state.pending=false;state.ready=false;state.error=error;state.message="Material source unavailable";}return state;}
std::string GpuLayerPreview::native_image_error()const{return impl_->native_image_failure;}
void GpuLayerPreview::install_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph> graph){impl_->native_materials.install(resource,std::move(content),std::move(graph));impl_->native_materials.prepare_world_normal(resource);}
void GpuLayerPreview::set_native_context_provider(NativeContextProvider provider){impl_->native_context_provider=std::move(provider);}
NativeMaterialViewContext GpuLayerPreview::native_world_view(const TextureSet &set,const TextureLayer &layer,int tile)const{
 if(!impl_->native_context_provider)throw NativeFragmentContextError(NativeFragmentContextCode::View,"Native fragment requires a real validated view: provider is unavailable");NativeMaterialGpuContext context;NativeMaterialDependencies required;required.view=true;impl_->native_context_provider(set,layer,tile,required,context);context.view.validate();return context.view;
}
TextureSet GpuLayerPreview::prepare_layer_capture(const TextureSet &source)const{
 auto result=source;
 if(source.instance_layers){result.layers=evaluation_layers(source);result.instance_layers.reset();}
 for(auto &layer:result.layers)if(layer.kind==LayerKind::Fill&&layer.fill.native_material){
  layer.fill.native_material->validate(); // Admit the detached parameter copy before allocation.
  auto description=std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);
  description->channels=impl_->native_materials.effective_channels(layer.fill.material_resource,*description);
  if(description->channels.empty())throw std::runtime_error("Native source has no prepared channel capabilities");
  layer.fill.native_material=std::move(description);
 }
 return result;
}
std::shared_ptr<const AdjustmentPaintSnapshot> GpuLayerPreview::begin_adjustment_source(const TextureSet &source,const QUuid &layer_id,const QUuid &document){
 PaintMetadataScope metadata;const auto &layers=evaluation_layers(source);const auto selected=std::find_if(layers.begin(),layers.end(),[&](const auto &item){return item.id==layer_id;});if(selected==layers.end())throw std::invalid_argument("Adjustment layer is missing");const auto &layer=*selected;if(!adjustment_layer(layer))throw std::invalid_argument("Adjustment source requires Passthrough Paint channels");
 auto token=std::make_shared<AdjustmentPaintSnapshot>();token->id=QUuid::createUuid();token->set=source.id;token->layer=layer_id;token->document=document;token->tiles=source.tiles;
 auto &s=*impl_;Impl::AdjustmentEntry entry;entry.source=token;entry.set=prepare_layer_capture(source);entry.set.id=token->id;entry.layer=layer_id;
 try{for(auto &item:entry.set.layers)if(item.kind==LayerKind::Fill&&item.fill.native_material){const auto original=item.fill.material_resource;const auto alias=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();s.native_materials.alias(original,alias);entry.native_aliases.push_back(alias);item.fill.material_resource=alias;
   for(int tile:source.tiles)for(int channel:s.native_materials.effective_channels(alias,*item.fill.native_material)){auto context=s.native_materials.default_context(alias);context.tile=tile;const auto &dependencies=s.native_materials.dependencies(alias,channel,*item.fill.native_material);if(s.native_context_provider)s.native_context_provider(source,item,tile,dependencies,context);else if(dependencies.object||dependencies.view)throw std::runtime_error("Native material object/view context is not installed");entry.contexts[{item.id,tile,channel}]=std::move(context);}}
  s.adjustment_sources.emplace(token->id,std::move(entry));return token;
 }catch(...){for(const auto &alias:entry.native_aliases)s.native_materials.erase(alias);throw;}
}
gpu::Texture *GpuLayerPreview::adjustment_page(const SmudgeCapture &capture,int plane,int tile,std::uint32_t page,gpu::Texture *own,performance::Clock::time_point deadline){
 if(!capture.adjustment.enabled(plane))return own;if(performance::Clock::now()>=deadline)return nullptr;auto &s=*impl_;const auto &token=*capture.adjustment.snapshot;try{const int channel=capture.adjustment.channels[plane];const auto found=s.adjustment_sources.find(token.id);if(found==s.adjustment_sources.end())throw std::runtime_error("Adjustment source snapshot is no longer resident");auto &entry=found->second;if(!entry.failure.empty()){s.input_rejected=true;return nullptr;}
 gpu::Texture *prefix=nullptr;if(!s.adjustment_pages.contains(token,channel,tile,capture.resolution,page)){
  const auto snapshot_key=entry.set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);
  if(!s.stacks.contains(snapshot_key)){const auto live_key=token.set.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);if(auto live=s.stacks.find(live_key);live!=s.stacks.end()){const auto &before=live->second;auto &snapshot=s.stacks[snapshot_key];snapshot.plan=before.plan;snapshot.queried_sources=before.queried_sources;std::set<QUuid> below;for(const auto &layer:evaluation_layers(entry.set)){if(layer.id==entry.layer)break;below.insert(layer.id);}std::set<std::string> retained;for(const auto &[id,owner]:before.node_owners)if(below.contains(owner)&&before.outputs.contains(id)){snapshot.outputs.emplace(id,before.outputs.at(id));snapshot.node_owners.emplace(id,owner);retained.insert(id);}snapshot.plan.retain(retained);snapshot.node_versions=before.node_versions;std::erase_if(snapshot.node_versions,[&](const auto &v){return !retained.contains(v.first);});snapshot.node_clock=before.node_clock;snapshot.width=before.width;snapshot.height=before.height;}}
  auto saved_pins=s.adjustment_input_pins;struct RestorePins{std::set<std::string> &pins,saved;~RestorePins(){pins=std::move(saved);}} restore{s.adjustment_input_pins,std::move(saved_pins)};for(const auto &[key,input]:s.inputs)s.adjustment_input_pins.insert(key);
  const auto result=update_tile(entry.set,channel,s.zero.get(),tile,{},true,&entry.layer,false);if(result==GpuPreviewUpdate::Pending)return nullptr;if(result!=GpuPreviewUpdate::Updated)throw std::runtime_error("Adjustment lower stack is unavailable on GPU");
  auto &stack=s.stacks.at(entry.set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile));const auto &query=stack.queries.at("query/"+entry.layer.toString().toStdString()+"/below");prefix=query.root.empty()?s.zero.get():stack.outputs.at(query.root).get();
 }
 auto *result=s.adjustment_pages.compose(token,channel,tile,capture.resolution,page,prefix,own);
 for(const auto &id:s.adjustment_pages.take_discarded()){const auto key=id.toString().toStdString();std::erase_if(s.stacks,[&](const auto &p){return p.first.starts_with(key+":");});std::erase_if(s.mask_stacks,[&](const auto &p){return p.first.starts_with(key);});}
 return result;
 }catch(const AdjustmentPaintMemoryError &error){s.input_rejected=true;pending_adjustment_failure=AdjustmentPaintFailure{token.id,token.set,error.what(),error.needed,error.available};if(auto found=s.adjustment_sources.find(token.id);found!=s.adjustment_sources.end())found->second.failure=error.what();pending_adjustment_failure->document=token.document;pending_adjustment_failure->layer=token.layer;return nullptr;}
 catch(const std::bad_alloc &){s.input_rejected=true;pending_adjustment_failure=AdjustmentPaintFailure{token.id,token.set,"Adjustment GPU source allocation failed (active pages need 0.125 MiB); available "+std::to_string(double(texture_free_vram.load())/(1024*1024))+" MiB. Stroke cancelled; prior image retained."};if(auto found=s.adjustment_sources.find(token.id);found!=s.adjustment_sources.end())found->second.failure=pending_adjustment_failure->message;pending_adjustment_failure->document=token.document;pending_adjustment_failure->layer=token.layer;return nullptr;}
 catch(const std::exception &error){s.input_rejected=true;pending_adjustment_failure=AdjustmentPaintFailure{token.id,token.set,std::string("Adjustment GPU source failed: ")+error.what()+". Stroke cancelled; prior image retained."};if(auto found=s.adjustment_sources.find(token.id);found!=s.adjustment_sources.end())found->second.failure=pending_adjustment_failure->message;pending_adjustment_failure->document=token.document;pending_adjustment_failure->layer=token.layer;return nullptr;}
}
void GpuLayerPreview::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit){if(!qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO")&&!qEnvironmentVariableIsSet("BTS_BM_GPU_ADJUSTMENT_TEST"))throw std::logic_error("Adjustment budget injection is restricted to the explicit diagnostic actor");impl_->adjustment_pages.diagnostic_budget(limit);}
std::array<std::uint64_t,2> GpuLayerPreview::adjustment_copy_statistics()const{return {impl_->frozen_prefix_copies,impl_->frozen_prefix_copy_texels};}
std::array<std::uint64_t,3> GpuLayerPreview::native_statistics()const{const auto s=impl_->native_materials.statistics();return {s.sources,s.programs,s.dispatches};}
bool GpuLayerPreview::prefetch_geometry(const std::map<int,std::shared_ptr<const FillSurfaceAtlas>> &atlases){for(const auto &[tile,atlas]:atlases)if(!impl_->surface(atlas,tile))return false;return true;}
void GpuLayerPreview::set_padding_geometry(std::uint32_t uid,const std::map<int,std::shared_ptr<const GpuMarginPlan>> &extend,const std::map<int,std::shared_ptr<const GpuMarginPlan>> &adjacent){impl_->margin_plans[uid]={extend,adjacent};}
void GpuLayerPreview::set_native_normal_error(const QUuid &set,std::string error){impl_->native_normal_errors[set]=std::move(error);}
void GpuLayerPreview::clear_native_normals(const QUuid &set){impl_->native_normal_errors.erase(set);std::erase_if(impl_->native_normals,[&](const auto &v){return std::get<0>(v.first)==set;});}
void GpuLayerPreview::set_native_normal(const QUuid &set,const QUuid &layer,int tile,std::shared_ptr<gpu::Texture> image){impl_->native_normals[{set,layer,tile}]=std::move(image);++impl_->native_normal_revision;}
TextureSet GpuLayerPreview::native_normal_stack(const TextureSet &source)const{
 TextureSet set=source;set.layers=evaluation_layers(source);set.instance_layers.reset();bool any=false;
 for(auto &layer:set.layers){if(!source.channels[3])layer.channels[3]=false;if(layer.fill.native_material){
  const bool normal=layer.fill.native_material->height_policy==NativeHeightPolicy::None&&impl_->native_materials.contains(layer.fill.material_resource)&&impl_->native_materials.world_normal_perturbed(layer.fill.material_resource);
  layer.channels[3]=normal&&set.channels[4]&&layer.channels[4];
  if(normal){any=true;layer.opacity[3]=layer.opacity[4];layer.blending[3]=layer.blending[4];}
 }}
 if(any){set.channels[3]=true;for(auto &layer:set.layers)if(layer.kind==LayerKind::Folder){layer.channels[3]=layer.channels[4];layer.opacity[3]=layer.opacity[4];layer.blending[3]=layer.blending[4];}}
 return set;
}
bool GpuLayerPreview::uniform(gpu::Texture *target,const std::array<float,4> &value){
 auto &s=*impl_;const auto format=GPU_texture_format(target);if(s.uniform_failed||GPU_texture_dimensions(target)!=2||GPU_texture_is_array(target)||(format!=gpu::TextureFormat::SFLOAT_16_16_16_16&&format!=gpu::TextureFormat::SFLOAT_32_32_32_32))return false;
 if(!s.uniform_shaders.contains(format))s.uniform_shaders.emplace(format,compile_uniform(format));auto *shader=s.uniform_shaders.at(format).get();if(!shader){s.uniform_failed=true;return false;}
 // Update the canonical image directly: no full-size broadcast temporary/copy.
 performance::Scope timing("gpu_uniform_broadcast");GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_shader_bind(shader);GPU_shader_uniform_4fv(shader,"fill_value",value.data());GPU_texture_image_bind(target,0);
 GPU_compute_dispatch(shader,(GPU_texture_width(target)+15)/16,(GPU_texture_height(target)+15)/16,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(target);GPU_shader_unbind();return true;
}
bool GpuLayerPreview::supported(const TextureSet &set,int channel){
 if(channel==15){if(std::getenv("BTS_GPU_STACK_DISABLE"))return false;const auto &layers=evaluation_layers(set);const auto selected=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==set.selected_layer;});if(selected==layers.end()||!selected->mask)return false;
  for(const auto &layer:layers)if(layer.mask)for(const auto &effect:layer.mask->effects)if(!gpu_mask_effect_supported(effect))return false;return true;
 }
 if(channel==3)return supported(set,6)&&(!set.channels[4]||supported(set,4));
 if(!gpu_authored_channel(set,channel)||std::getenv("BTS_GPU_STACK_DISABLE"))return false;
 const auto unsupported=[&](const TextureLayer &layer,const char *reason){static std::set<std::string> logged;const auto key=set.id.toString().toStdString()+":"+layer.id.toString().toStdString()+":"+std::to_string(channel)+":"+reason;if(logged.insert(key).second)std::fprintf(stderr,"BTS_GPU_STACK_FALLBACK uid=%u layer=%s channel=%d reason=%s background=1\n",set.material_session_uid,layer.name.c_str(),channel,reason);return false;};
 const bool anchor_dependencies=std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return TextureDocument::anchor_dependent(layer);});
 if(anchor_dependencies)for(const auto &layer:evaluation_layers(set))if(layer.mask)for(const auto &effect:layer.mask->effects)if(!gpu_mask_effect_supported(effect))return unsupported(layer,"anchor-producer-mask-effects");
 for(int tile:set.tiles)for(const auto &layer:evaluation_layers(set)){
  const auto info=gpu_layer_channel(layer,channel);
  if(!gpu_channel_enabled(set,channel)||!active(layer,channel,tile))continue;

  if(int(info.blend)>28)return unsupported(layer,"blend-mode");
  if(layer.mask&&(layer.mask->enabled||anchor_dependencies)){if(std::any_of(layer.mask->effects.begin(),layer.mask->effects.end(),[](const auto &e){return !gpu_mask_effect_supported(e);}))return unsupported(layer,"mask-effects");}
  if(layer.kind==LayerKind::Fill){
   if(const auto &source=fill_channel_texture(layer.fill,info.fill_channel);source&&!source->pixels)return unsupported(layer,"source-not-ready");
  }
 }
 return true;
}
std::uint64_t GpuLayerPreview::texture_resource_allocations()const{return impl_->resource_allocations;}
blender::gpu::Texture *GpuLayerPreview::diagnostic_composite_texture(const TextureSet &set,int channel,int tile)const{
 const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);
 const auto found=impl_->stacks.find(key);return found==impl_->stacks.end()?nullptr:found->second.head;
}
std::uint64_t GpuLayerPreview::content_node_revision(const TextureSet &set,int channel,int tile,const std::string &node)const{
 const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);
 const auto stack=impl_->stacks.find(key);if(stack==impl_->stacks.end())return 0;
 const auto found=stack->second.node_versions.find(node);if(found==stack->second.node_versions.end())return 0;
 std::uint64_t revision=0;for(const auto &[chunk,page]:found->second)revision=std::max(revision,page.revision);return revision;
}
GpuPreviewUpdate GpuLayerPreview::update_tile(const TextureSet &source_set,int channel,gpu::Texture *target,int tile,std::array<int,3> target_offset,bool prefix_only,const QUuid *query_layer,bool query_source,const QUuid *query_effect,bool thumbnail,bool raw_anchor,bool authored_ao_output){
 const auto normal_set=channel==6?std::optional<TextureSet>(native_normal_stack(source_set)):std::nullopt;const auto &set=normal_set?*normal_set:source_set;
 PaintMetadataScope metadata;
 if(!supported(set,channel)||!set.tiles.contains(tile)||GPU_texture_dimensions(target)!=2||target_offset[0]<0||target_offset[1]<0||target_offset[2]<0||(!query_layer&&!prefix_only&&(GPU_texture_width(target)<target_offset[0]+set.width(tile)||GPU_texture_height(target)<target_offset[1]+set.rows(tile)))||target_offset[2]>=std::max(1,GPU_texture_layer_count(target)))return GpuPreviewUpdate::Unsupported;
 const auto format=GPU_texture_format(target);if(format!=gpu::TextureFormat::SFLOAT_16_16_16_16&&format!=gpu::TextureFormat::SFLOAT_32_32_32_32)return GpuPreviewUpdate::Unsupported;
 auto &s=*impl_;performance::Scope timing("gpu_stack_submit");s.input_rejected=false;s.native_image_failure.clear();++s.clock;const auto upload_before=s.remaining;
 const int width=set.width(tile),height=set.rows(tile);
 if(const auto state=s.initialize(width,height,format,GPU_texture_is_array(target));state!=GpuPreviewUpdate::Updated)return state;
 // Keep disabled masks cached, but release deleted masks/layers and old tiles.
 const auto set_prefix=set.id.toString().toStdString();std::set<std::string> live_masks;
 for(const auto &layer:evaluation_layers(set))if(layer.mask)for(int number:set.tiles)live_masks.insert(paint_key(set,layer,2,true,number));
 std::erase_if(s.mask_stacks,[&](const auto &item){return item.first.starts_with(set_prefix)&&!live_masks.contains(item.first);});
 // Resolving an adjustment payload can recursively initialize a 2D source
 // query. Keep this caller's target-format/array variant across that recursion.
 auto *const target_final_shader=s.final_shader;
 const auto stack_key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile)+(raw_anchor?":anchor:"+query_layer->toString().toStdString():std::string{});
 const auto target_key=std::pair{reinterpret_cast<std::uintptr_t>(target),target_offset};auto &stack=s.stacks[stack_key];stack.last_used=s.clock;const bool target_changed=!prefix_only&&s.target_owners[target_key]!=stack_key;
 if(stack.width!=width||stack.height!=height){stack.outputs.clear();stack.node_owners.clear();stack.queries.clear();stack.node_versions.clear();stack.coverage.reset();stack.height_image.reset();stack.output_dirty.clear();stack.height_dirty.clear();stack.head=nullptr;stack.plan.reset();stack.width=width;stack.height=height;}
 std::shared_ptr<const GpuMarginPlan> margin;
 if(!query_layer)if(const auto found=s.margin_plans.find(set.material_session_uid);found!=s.margin_plans.end()){
  const auto &plans=set.padding_3d&&channel!=3&&channel!=6?found->second.adjacent:found->second.extend;
  if(!plans.contains(tile))return GpuPreviewUpdate::Pending;margin=plans.at(tile);
  if(margin->width!=width||margin->height!=height)return GpuPreviewUpdate::Pending;
  if(!s.margins.prepare(margin,s.remaining,s.upload_start,testing_stack))return GpuPreviewUpdate::Pending;
 }
 struct Input {std::shared_ptr<NativeMaterialGpuContext> native_context;const MeshMapPlane *native_ao=nullptr;const TextureLayer *layer;GpuLayerChannel channel;GpuAnchorInput anchor;std::optional<MeshGeneratorSettings> generator;GpuGeneratorInputs generator_inputs;gpu::Texture *source=nullptr,*mask=nullptr,*surface=nullptr,*effects=nullptr,*ownership_image=nullptr,*selection_image=nullptr;std::shared_ptr<const GeometryOwnershipTile> ownership;std::array<const MeshMapPlane *,6> mask_planes{};std::array<gpu::Texture *,6> mask_maps{};std::vector<GpuEffectRow> effect_rows;std::vector<GpuMaskStage> mask_stages;std::string mask_signature;int content_effects=0,content_pass=0,effect_plane=0;const FillProjection *effect_fill=nullptr;const TilePaintPixels *effect_pixels=nullptr;std::string effect_key;std::shared_ptr<const FillSurfaceAtlas> geometry;int kind=0;bool enabled=false,constant=false,zero_opacity=false,raw_linear_source=false;std::string below,group;std::array<float,3> map_u{},map_v{};};
 GpuMaskBuilder mask_builder(set,tile);
 const GpuComparisonResolver resolve=[&](const GpuMaskStage &stage){return comparison_inputs(set,stage,target,tile);};
 const GpuAnchorResolver resolve_anchor=[&](const QUuid &consumer,const AnchorReference &ref){return anchor_input(set,consumer,ref,tile);};
 const std::string query_id=query_layer?"query/"+query_layer->toString().toStdString()+(query_source?"/source":"/below")+(query_effect?"/before-selected-effect":"")+(thumbnail?"/thumbnail":"")+(raw_anchor?"/anchor/"+query_effect->toString().toStdString():std::string{}):std::string{};
 std::string query_head;bool query_enabled=false;
 if(query_layer&&query_source)stack.queried_sources.insert(query_layer->toString().toStdString());
 std::vector<Input> inputs;std::vector<GpuStackNode> descriptors;std::set<std::string> protected_keys;QVarLengthArray<std::string,17> heads;heads.resize(17);std::map<std::string,bool> constant_nodes;
 const auto append=[](std::string &data,const auto &value){data.append(reinterpret_cast<const char *>(&value),sizeof(value));};
 const auto pages=[&](GpuStackNode &node,const PaintPixels &pixels,int plane,int slot){
  if(!pixels.resolution())return;const double sx=double(width)/pixels.resolution(),sy=double(height)/pixels.resolution();
  const auto versions=pixels.page_versions();for(const auto &[page,revision]:versions[plane]){const int px=int(page%65536)*64,py=int(page/65536)*64;const int x=int(std::floor(px*sx))-1,y=int(std::floor(py*sy))-1,right=int(std::ceil((px+64)*sx))+1,bottom=int(std::ceil((py+64)*sy))+1;node.pages[(std::uint64_t(slot)<<32)|page]={x,y,right-x,bottom-y,revision};}
 };
 const auto &layers=evaluation_layers(set);auto folder_entries=folder_entry_plan(layers);
 const int first_layer=precompose_evaluation_start(layers,query_effect,-1,query_layer);
 while(!folder_entries.empty()&&folder_entries.back().first<first_layer)folder_entries.pop_back();
 for(int layer_number=first_layer;layer_number<int(layers.size());++layer_number){
  while(!folder_entries.empty()&&folder_entries.back().first==layer_number){
   const auto &folder=layers[folder_entries.back().folder];folder_entries.pop_back();if(folder.depth+1>=heads.size())heads.resize(folder.depth+2);
   const bool raw_folder=query_layer&&folder.id==*query_layer&&(thumbnail||raw_anchor);
   heads[folder.depth+1]=!raw_folder&&gpu_layer_channel(folder,channel).blend==LayerBlend::Passthrough?heads[folder.depth]:std::string{};
  }
  const auto &layer=layers[layer_number];
  // An actual source Normal contribution has no authored scalar H. Bump4 is
  // its AG enable/opacity control, not an opaque invented 0.5 Fill over others.
  if(channel==4&&layer.fill.native_material&&layer.fill.native_material->height_policy==NativeHeightPolicy::None&&s.native_materials.contains(layer.fill.material_resource)&&s.native_materials.world_normal_perturbed(layer.fill.material_resource)&&layer.effects.empty()&&layer.pixels.page_count(4)==0)continue;
  const bool querying=query_layer&&layer.id==*query_layer;
  if(querying){query_enabled=raw_anchor||gpu_channel_enabled(set,channel);if(!query_source){query_head=heads.at(layer.depth);break;}const auto values=gpu_layer_channel(layer,channel);if(!raw_anchor)query_enabled&=values.enabled&&(thumbnail||values.blend!=LayerBlend::Disabled);if(!query_enabled)break;}
  if(!querying&&(!gpu_channel_enabled(set,channel)||!active(layer,channel,tile))){if(layer.kind==LayerKind::Folder)heads[layer.depth+1].clear();continue;}
  GpuStackNode node;node.id=layer.id.toString().toStdString();Input in;in.layer=&layer;in.channel=gpu_layer_channel(layer,channel);in.raw_linear_source=channel==0&&raw_linear_color_source(layer);
  // Ordinary source queries share canonical pixels. A raw thumbnail of a
  // zero-opacity/Passthrough source needs different coverage/compositing; keep
  // those scratch outputs distinct from Compare's already-cached inputs.
  const bool isolated_thumbnail=querying&&thumbnail&&(in.channel.opacity<=0||in.channel.blend==LayerBlend::Passthrough||in.channel.blend==LayerBlend::Disabled);
  if(isolated_thumbnail)node.id+="/thumbnail-raw";
  if(querying&&(thumbnail||raw_anchor)){in.channel.enabled=true;in.channel.opacity=1;in.channel.blend=LayerBlend::Normal;}in.enabled=querying||active(layer,channel,tile);in.below=heads.at(layer.depth);if(!in.below.empty())node.dependencies.push_back(in.below);
  if(layer.kind==LayerKind::Folder){in.kind=in.channel.blend==LayerBlend::Passthrough?11:3;in.group=heads.at(layer.depth+1);heads[layer.depth+1].clear();if(!in.group.empty())node.dependencies.push_back(in.group);}
  auto &parameters=node.parameters;append(parameters,layer.kind);append(parameters,layer.precompose_below);append(parameters,in.enabled);const auto source_opacity_offset=parameters.size();append(parameters,in.channel.opacity);const auto source_blend_offset=parameters.size();append(parameters,in.channel.blend);append(parameters,gpu_channel_precision(set,channel));const auto source_value_offset=parameters.size();const auto value=in.channel.value;append(parameters,value);append(parameters,in.raw_linear_source);
  if(channel==0&&layer.quick_overlay){in.kind=8;const auto &overlay=*layer.quick_overlay;append(parameters,overlay.editing);append(parameters,overlay.field&&overlay.field->inverted);if(overlay.field){protected_keys.insert(paint_key(set,layer,0,false,tile));const auto &pixels=overlay.field->pixels.tile(tile);append(parameters,pixels.resolution());pages(node,pixels,2,0);const int halo_x=int(std::ceil(double(width)/std::max(1,pixels.resolution())+double(width)/256)),halo_y=int(std::ceil(double(height)/std::max(1,pixels.resolution())+double(height)/256));for(auto &[id,page]:node.pages){page.x-=halo_x;page.y-=halo_y;page.width+=2*halo_x;page.height+=2*halo_y;}}}
  else if(layer.kind==LayerKind::Paint){in.kind=1;protected_keys.insert(paint_key(set,layer,channel,false,tile));pages(node,in.channel.pixels->tile(tile),in.channel.plane,0);}
  else if(layer.kind==LayerKind::Fill){
   if(channel==6&&layer.fill.native_material){
    const auto found=s.native_normals.find({set.id,layer.id,tile});
    in.kind=12;in.source=found==s.native_normals.end()?nullptr:found->second.get();append(parameters,reinterpret_cast<std::uintptr_t>(in.source));append(parameters,s.native_normal_revision);
   }else if(channel==4&&layer.fill.native_material&&layer.fill.native_material->height_policy==NativeHeightPolicy::None&&s.native_materials.contains(layer.fill.material_resource)&&s.native_materials.world_normal_perturbed(layer.fill.material_resource)){
    // Keep authored scalar Paint/effects over an unavailable native scalar
    // source. A private world-vector contribution is never converted into H.
    in.kind=1;protected_keys.insert(paint_key(set,layer,channel,false,tile));pages(node,in.channel.pixels->tile(tile),in.channel.plane,0);
   }else if(layer.fill.native_material){
    require_native_height_fill(layer.fill.native_material->height_policy,set.bump_origin,layer.fill.mode,layer.channels[3]);
    if(!s.native_materials.contains(layer.fill.material_resource))return GpuPreviewUpdate::Pending;
    if(!s.native_materials.prepare_images(layer.fill.material_resource)){const auto error=s.native_materials.image_error(layer.fill.material_resource);if(error.isEmpty())return GpuPreviewUpdate::Pending;s.native_image_failure=error.toStdString();s.input_rejected=true;return GpuPreviewUpdate::Unsupported;}
    in.kind=10;in.native_context=std::make_shared<NativeMaterialGpuContext>(s.native_materials.default_context(layer.fill.material_resource));in.native_context->tile=tile;
    const auto &dependencies=s.native_materials.dependencies(layer.fill.material_resource,in.channel.fill_channel,*layer.fill.native_material);parameters+=s.native_materials.signature(layer.fill.material_resource,in.channel.fill_channel,*layer.fill.native_material);
    if(dependencies.surface||fill_surface_projection(layer.fill)){in.geometry=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;if(!in.geometry||!in.geometry->tiles.contains(tile))return GpuPreviewUpdate::Pending;append(parameters,reinterpret_cast<std::uintptr_t>(in.geometry.get()));}
    if(dependencies.ao){if(set.mesh_maps[3])if(auto found=set.mesh_maps[3]->tiles.find(tile);found!=set.mesh_maps[3]->tiles.end()&&found->second.pixels)in.native_ao=&found->second;if(!in.native_ao)throw std::runtime_error("Native material requires a baked AO map — Bake Mesh Maps…");const auto &pixels=*in.native_ao->pixels;const auto key="baked:"+pixels.hash+":"+std::to_string(pixels.resolution)+":"+std::to_string(pixels.height);protected_keys.insert(key);parameters+=key;}
    in.native_context->projection=native_material_projection(layer.fill,in.geometry.get(),set);
    if(auto snapshot=s.adjustment_sources.find(set.id);snapshot!=s.adjustment_sources.end()){
     const auto saved=snapshot->second.contexts.find({layer.id,tile,in.channel.fill_channel});if(saved==snapshot->second.contexts.end())throw std::logic_error("Frozen native material context missing");auto projection=in.native_context->projection;*in.native_context=saved->second;in.native_context->projection=std::move(projection);
    }else if(s.native_context_provider)s.native_context_provider(set,layer,tile,dependencies,*in.native_context);else if(dependencies.object||dependencies.view)throw std::runtime_error("Native material object/view context is not installed");
    const auto projection_rows=in.native_context->projection->encoded();for(std::size_t row=0;row<in.native_context->projection->row_count();++row)append(parameters,projection_rows[row]);
    if(dependencies.object){append(parameters,in.native_context->object_to_world);append(parameters,in.native_context->world_to_object);append(parameters,in.native_context->bounds_min);append(parameters,in.native_context->bounds_max);append(parameters,in.native_context->object_color);append(parameters,in.native_context->object_random);append(parameters,in.native_context->objects?in.native_context->objects->hash():std::uint64_t(0));}
    if(dependencies.view){append(parameters,in.native_context->camera_position);append(parameters,in.native_context->camera_forward);append(parameters,in.native_context->perspective);}
   }else{

   const auto &reference=layer.fill.anchors.at(in.channel.fill_channel);
   if(!reference.source.isNull()){auto input=resolve_anchor(layer.id,reference);if(!input)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;in.kind=9;in.anchor=*input;append(parameters,reference.channel);append(parameters,reference.component);append(parameters,reference.coverage);int slot=0;gpu_anchor_dependencies(node,*input,slot);}
   else {
   append(parameters,layer.fill.alpha_mask.at(in.channel.fill_channel));
   if(fill_channel_texture(layer.fill,in.channel.fill_channel)){const auto *resource=tile_resource(layer.fill,in.channel.fill_channel,tile);in.kind=resource?(fill_surface_projection(layer.fill)?5:2):4;if(resource){const auto key=resource_key(*resource);protected_keys.insert(key);parameters+=key;}else parameters+="missing tile";}
   if(in.kind!=4&&(fill_spatial_projection(layer.fill)||(channel==0&&layer.fill.atom)))in.kind=fill_surface_projection(layer.fill)?5:2;
   if(layer.fill.mode==5){const auto grid=fill_warp_grid(layer.fill);append(parameters,grid->width());append(parameters,grid->height());for(double v:grid->columns)append(parameters,v);for(double v:grid->rows)append(parameters,v);for(const auto &node:grid->nodes){append(parameters,node.position);append(parameters,node.du);append(parameters,node.dv);append(parameters,node.duv);}append(parameters,layer.fill.warp_depth);append(parameters,bool(layer.fill.warp_geometry&&(layer.fill.warp_geometry->enabled||layer.fill.warp_geometry->frozen_reference)));}
   append(parameters,in.channel.fill_channel==4?layer.fill.bump_bias:0.f);append(parameters,layer.fill.atom);append(parameters,layer.fill.border_width);append(parameters,layer.fill.border_dots);append(parameters,layer.fill.middle_dot);append(parameters,layer.fill.invert);append(parameters,layer.fill.wrap);append(parameters,layer.fill.shape_crop);append(parameters,layer.fill.depth_culling);append(parameters,layer.fill.depth_hardness);append(parameters,layer.fill.backface_culling);append(parameters,layer.fill.backface_angle);append(parameters,layer.fill.backface_hardness);append(parameters,layer.fill.cylinder_angle);if(layer.fill.symmetry){const auto &sym=*layer.fill.symmetry;append(parameters,sym.constraint.enabled);append(parameters,sym.constraint.mode);append(parameters,sym.constraint.axis);append(parameters,sym.constraint.offset);append(parameters,sym.constraint.center);append(parameters,sym.constraint.copies);append(parameters,sym.flip_u);append(parameters,sym.flip_v);}
   auto mapping=layer.fill;if(mapping.mode==1){mapping.offset={};mapping.physical=false;}if(fill_spatial_projection(mapping))mapping.physical=false;mapping.native_per_uv=set.native_per_uv;mapping.cm_per_native=set.cm_per_native;const double u=fill_surface_projection(mapping)?0:(tile-1001)%10,v=fill_surface_projection(mapping)?0:(tile-1001)/10;const auto a=mapping.mode==6?std::array<double,2>{0,0}:fill_uv_map(mapping,u,v),b=mapping.mode==6?std::array<double,2>{1,0}:fill_uv_map(mapping,u+1,v),c=mapping.mode==6?std::array<double,2>{0,1}:fill_uv_map(mapping,u,v+1);in.map_u={float(b[0]-a[0]),float(c[0]-a[0]),float(a[0])};in.map_v={float(b[1]-a[1]),float(c[1]-a[1]),float(a[1])};append(parameters,in.map_u);append(parameters,in.map_v);append(parameters,layer.fill.mode);append(parameters,layer.fill.filtering);if(in.kind==5){in.geometry=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;const auto identity=reinterpret_cast<std::uintptr_t>(in.geometry.get());append(parameters,identity);append(parameters,layer.fill.offset3);append(parameters,layer.fill.rotation3);append(parameters,layer.fill.scale3);append(parameters,layer.fill.hardness);append(parameters,layer.fill.physical);append(parameters,fill_physical_size(layer.fill));append(parameters,set.cm_per_native);}
   }
   }
  }
  if(in.kind==5&&(!in.geometry||!in.geometry->tiles.contains(tile)))return GpuPreviewUpdate::Pending;
  const bool staged_content=(querying&&query_source)||stack.queried_sources.contains(node.id)||(layer.kind==LayerKind::Fill&&layer.fill.mode==5)||std::any_of(layer.effects.begin(),layer.effects.end(),[&](const auto &e){return e.enabled&&(e.kind==ContentEffectKind::Paint||e.kind==ContentEffectKind::Fill||(e.kind==ContentEffectKind::Generator&&e.channel==in.channel.fill_channel)||(e.kind==ContentEffectKind::Filter&&e.channel==in.channel.fill_channel&&e.filter.resource>=7));});
  if(staged_content){
   auto source_node=node;source_node.id=node.id+"/source";
   auto source_input=in;source_input.content_pass=1;
   if(in.raw_linear_source){source_input.channel.value={1,1,1};source_node.parameters.replace(source_value_offset,sizeof(source_input.channel.value),reinterpret_cast<const char *>(source_input.channel.value.data()),sizeof(source_input.channel.value));}
   // Authoring source pixels are independent of layer opacity and ordinary
   // blend modes. Keep the expensive Warp source across those final-blend edits.
   source_input.zero_opacity=in.channel.opacity<=0;append(source_node.parameters,source_input.zero_opacity);source_input.channel.opacity=1;source_node.parameters.replace(source_opacity_offset,sizeof(float),reinterpret_cast<const char *>(&source_input.channel.opacity),sizeof(float));
   if(in.channel.blend!=LayerBlend::Passthrough){source_input.channel.blend=LayerBlend::Normal;source_node.parameters.replace(source_blend_offset,sizeof(LayerBlend),reinterpret_cast<const char *>(&source_input.channel.blend),sizeof(LayerBlend));}
   if(in.channel.blend!=LayerBlend::Passthrough){source_input.below.clear();source_node.dependencies.clear();if(!in.group.empty())source_node.dependencies.push_back(in.group);}
   source_input.constant=(in.kind==0||(in.kind==9&&GPU_texture_width(in.anchor.image)==1&&GPU_texture_height(in.anchor.image)==1)||((in.kind==3||in.kind==11)&&(in.group.empty()||constant_nodes.at(in.group))))&&(source_input.below.empty()||constant_nodes.at(source_input.below));
   append(source_node.parameters,source_input.content_pass);append(source_node.parameters,source_input.constant);constant_nodes[source_node.id]=source_input.constant;
   std::string previous=source_node.id;descriptors.push_back(std::move(source_node));inputs.push_back(std::move(source_input));
   for(const auto &effect:layer.effects){if(querying&&query_effect&&effect.id==*query_effect)break;if(effect.kind==ContentEffectKind::Anchor)continue;if((!effect.enabled&&effect.kind!=ContentEffectKind::LinearColorTransform)||(effect.kind!=ContentEffectKind::Paint&&effect.channel!=in.channel.fill_channel))continue;
    GpuStackNode effect_node;effect_node.id=node.id+"/effect/"+effect.id.toString().toStdString();effect_node.dependencies={previous};
    Input effect_input;effect_input.layer=&layer;effect_input.channel=in.channel;effect_input.enabled=true;effect_input.below=previous;
    effect_input.content_pass=effect.kind==ContentEffectKind::Paint?3:2;
    append(effect_node.parameters,effect_input.content_pass);append(effect_node.parameters,gpu_channel_precision(set,channel));
    if(effect.kind==ContentEffectKind::Paint){
     effect_input.kind=7;effect_input.channel.blend=effect.blending;effect_input.channel.opacity=effect.opacity;
     const int c=in.channel.fill_channel;effect_input.effect_pixels=c<5?&effect.paint.pixels:c==5?&effect.paint.ao:c==14?&effect.paint.displacement:&effect.paint.users.at(c-6);
     effect_input.effect_plane=c<5?c:2;effect_input.effect_key=stack_key+":"+effect_node.id;
     protected_keys.insert(effect_input.effect_key);pages(effect_node,effect_input.effect_pixels->tile(tile),effect_input.effect_plane,0);
     append(effect_node.parameters,effect.opacity);append(effect_node.parameters,effect.blending);
    }else if(effect.kind==ContentEffectKind::Fill){
     effect_input.content_pass=effect.initialize_content?5:4;effect_input.channel.blend=effect.blending;effect_input.channel.opacity=effect.opacity;effect_input.effect_rows={gpu_effect_row(effect,in.channel.fill_channel)};effect_input.effect_rows.front()[1][3]=float(effect.source_coverage);effect_input.effect_rows.front()[0][1]=float(LayerBlend::Normal);effect_input.effect_rows.front()[0][2]=1;effect_input.content_effects=1;append(effect_node.parameters,effect_input.content_pass);append(effect_node.parameters,effect.blending);append(effect_node.parameters,effect.opacity);append(effect_node.parameters,effect_input.effect_rows.front());effect_input.constant=constant_nodes.at(previous);
     if(effect.bitmap_fill){
      const auto &mapping=effect.bitmap_fill->projection;effect_input.effect_fill=&mapping;effect_input.kind=2;effect_input.content_pass=effect.initialize_content?7:6;effect_input.constant=false;
      const auto key=resource_key(*mapping.texture);protected_keys.insert(key);effect_node.parameters+=key;
      const double u=(tile-1001)%10,v=(tile-1001)/10;const auto a=fill_uv_map(mapping,u,v),b=fill_uv_map(mapping,u+1,v),c=fill_uv_map(mapping,u,v+1);
      effect_input.map_u={float(b[0]-a[0]),float(c[0]-a[0]),float(a[0])};effect_input.map_v={float(b[1]-a[1]),float(c[1]-a[1]),float(a[1])};
      append(effect_node.parameters,effect_input.content_pass);append(effect_node.parameters,effect_input.map_u);append(effect_node.parameters,effect_input.map_v);
      if(effect.linear_color){effect_input.channel.value={1,1,1};LevelsEffect transform;transform.kind=ContentEffectKind::LinearColorTransform;transform.linear_color=effect.linear_color;effect_input.effect_rows.push_back(gpu_effect_row(transform,0));effect_input.content_effects=2;append(effect_node.parameters,effect_input.effect_rows.back());}
     }
    }else if(effect.kind==ContentEffectKind::Generator){
     effect_input.content_pass=4;effect_input.channel.blend=effect.blending;effect_input.channel.opacity=effect.opacity;effect_input.channel.value=content_generator_color(effect);append(effect_node.parameters,effect_input.channel.value);effect_input.effect_rows={gpu_generator_row(effect,set.mesh_maps)};effect_input.content_effects=1;append(effect_node.parameters,effect_input.content_pass);append(effect_node.parameters,effect.blending);append(effect_node.parameters,effect.opacity);append(effect_node.parameters,effect_input.effect_rows.front());effect_input.constant=!generator_resources_present(set.mesh_maps,effect)&&!generator_has_anchors(effect)&&constant_nodes.at(previous);
     if(effect.triplanar_grunge||effect.surface_fallback){effect_input.geometry=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;effect_input.constant=false;append(effect_node.parameters,reinterpret_cast<std::uintptr_t>(effect_input.geometry.get()));}
     if(generator_has_anchors(effect)){auto values=gpu_generator_inputs(effect,layer.id,resolve_anchor);if(!values)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;effect_input.generator=effect;effect_input.generator_inputs=*values;gpu_generator_options(effect_input.effect_rows.front(),effect,*values);int slot=1;gpu_generator_dependencies(effect_node,effect,*values,slot);append(effect_node.parameters,effect_input.effect_rows.front());}
     std::array<bool,6> needed{};gpu_need_generator_maps(needed,effect);for(int i=0;i<6;++i)if(needed[i])if(const auto &map=set.mesh_maps[gpu_mask_map_roles[i]];map)if(const auto member=map->tiles.find(tile);member!=map->tiles.end()&&member->second.pixels){effect_input.mask_planes[i]=&member->second;const auto &pixels=*member->second.pixels;const auto key="baked:"+pixels.hash+":"+std::to_string(pixels.resolution)+":"+std::to_string(pixels.height);protected_keys.insert(key);effect_node.parameters+=key;effect_input.mask_signature+=key;}
    }else{effect_input.effect_rows={gpu_effect_row(effect,in.channel.fill_channel)};effect_input.content_effects=1;append(effect_node.parameters,effect_input.effect_rows.front());if(effect.kind==ContentEffectKind::LinearColorTransform)append(effect_node.parameters,effect_input.channel.value);effect_input.constant=constant_nodes.at(previous);}
    append(effect_node.parameters,effect_input.constant);constant_nodes[effect_node.id]=effect_input.constant;previous=effect_node.id;
    descriptors.push_back(std::move(effect_node));inputs.push_back(std::move(effect_input));
   }
   if(querying){query_head=previous;break;}
   in.kind=6;in.group=previous;node.dependencies.clear();if(!in.below.empty())node.dependencies.push_back(in.below);node.dependencies.push_back(previous);node.pages.clear();
  }
  if(!staged_content)in.effect_rows=gpu_content_effects(layer,in.channel.fill_channel);in.content_effects=int(in.effect_rows.size());in.mask_stages=mask_builder.build(layer);if(in.mask_stages.empty())gpu_append_mask_effects(in.effect_rows,layer,set,tile);append(parameters,in.content_effects);for(const auto &row:in.effect_rows)append(parameters,row);
  const bool masked=layer.mask&&layer.mask->enabled;append(parameters,masked);if(masked){append(parameters,layer.mask->background);protected_keys.insert(paint_key(set,layer,2,true,tile));pages(node,layer.mask->pixels.tile(tile),2,1);
   bool comparisons_ready=true;int slot=2;for(const auto &stage:in.mask_stages)gpu_visit_mask_stage(stage,[&](const auto &part){parameters+=part.owner_key+part.id+part.anchor_key+part.anchor_id;if(part.bitmap)parameters+=part.bitmap->asset+part.bitmap->hash;append(parameters,part.kind);append(parameters,part.background);append(parameters,part.opacity);append(parameters,part.blend);if(part.kind==GpuMaskStage::AnchorFill){append(parameters,part.reference.channel);append(parameters,part.reference.component);append(parameters,part.reference.coverage);}if(part.generator){auto values=gpu_generator_inputs(*part.generator,part.comparison_layer,resolve_anchor);if(!values)comparisons_ready=false;else gpu_generator_dependencies(node,*part.generator,*values,slot);}if(part.kind==GpuMaskStage::Compare){auto values=resolve(part);if(!values)comparisons_ready=false;else gpu_comparison_dependencies(node,*values,slot);}if(part.kind==GpuMaskStage::AnchorFill&&part.content_anchor){append(parameters,part.reference.channel);append(parameters,part.reference.component);append(parameters,part.reference.coverage);auto value=resolve_anchor(part.comparison_layer,part.reference);if(!value)comparisons_ready=false;else gpu_anchor_dependencies(node,*value,slot);}for(const auto &row:part.rows)append(parameters,row);if(part.spatial){append(parameters,part.spatial->values);append(parameters,reinterpret_cast<std::uintptr_t>(part.geometry.get()));}if(part.pixels)pages(node,part.pixels->tile(tile),2,slot++);});
   if(!comparisons_ready)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
   for(const auto &stage:in.mask_stages)gpu_visit_mask_stage(stage,[&](const auto &part){if(part.pixels)protected_keys.insert(part.owner_key+":"+part.id);},true);
  }
  if(masked){std::array<bool,6> needed{};for(const auto &effect:layer.mask->effects)if(effect.enabled){needed[0]|=effect.kind==MaskEffectKind::ColorSelection&&effect.use_id_map;if(effect.kind==MaskEffectKind::Generator)gpu_need_generator_maps(needed,effect);}
   for(const auto &stage:in.mask_stages)gpu_visit_mask_stage(stage,[&](const auto &part){for(const auto &row:part.rows){needed[0]|=row[0][0]==4&&row[1][0]!=0;if(row[0][0]==3)gpu_need_generator_row_maps(needed,row);}},true);
   for(int i=0;i<6;++i)if(needed[i]){const auto &map=set.mesh_maps[gpu_mask_map_roles[i]];if(map)if(auto found=map->tiles.find(tile);found!=map->tiles.end()&&found->second.pixels){in.mask_planes[i]=&found->second;const auto &pixels=*found->second.pixels;const auto key="baked:"+pixels.hash+":"+std::to_string(pixels.resolution)+":"+std::to_string(pixels.height);protected_keys.insert(key);parameters+=key;in.mask_signature+=key;}}
  }
  if(layer.geometry.type==0&&!layer.geometry.meshes.empty()){
   if(auto found=set.geometry_ownership.find(tile);found!=set.geometry_ownership.end())in.ownership=found->second;
   if(!in.ownership||in.ownership->width!=width||in.ownership->height!=height)return GpuPreviewUpdate::Pending;
   append(parameters,reinterpret_cast<std::uintptr_t>(in.ownership.get()));append(parameters,layer.geometry.default_include);
   for(const auto &[id,name]:set.geometry_names){append(parameters,id);append(parameters,layer.geometry.includes(name));}
  }
  // Uniform prefixes are represented by one GPU texel, never by a CPU blend.
  // An opaque normal/replacement fill has no dependency on hidden lower work.
  const bool unmasked=(!layer.mask||!layer.mask->enabled)&&(layer.geometry.type==1||layer.geometry.meshes.empty());
  const bool uniform_source=in.kind==0||(in.kind==9&&GPU_texture_width(in.anchor.image)==1&&GPU_texture_height(in.anchor.image)==1)||((in.kind==3||in.kind==6||in.kind==11)&&(in.group.empty()||constant_nodes.at(in.group)));
  if(unmasked&&uniform_source){
   // A source query (for example a thumbnail) can stage a uniform fill as
   // kind 6. Its RGB effects preserve full coverage; keep the child dependency
   // while pruning the hidden backdrop. Fractional folders are not opaque fills.
   if(in.enabled&&(in.kind==0||(in.kind==6&&layer.kind==LayerKind::Fill))&&(in.channel.blend==LayerBlend::Replace||(in.channel.blend==LayerBlend::Normal&&in.channel.opacity==1))){in.below.clear();node.dependencies.clear();if(!in.group.empty())node.dependencies.push_back(in.group);}
   in.constant=in.below.empty()||constant_nodes.at(in.below);
  }
  append(parameters,in.constant);constant_nodes[node.id]=in.constant;
  heads[layer.depth]=node.id;descriptors.push_back(std::move(node));inputs.push_back(std::move(in));
 }
 if(query_layer&&query_layer->isNull()){query_head=heads[0];query_enabled=gpu_channel_enabled(set,channel);}
 GpuStackNode final_node;final_node.id=query_layer?query_id:"output";const auto output_head=query_layer?query_head:heads[0];if(!output_head.empty())final_node.dependencies.push_back(output_head);append(final_node.parameters,query_enabled);const auto base=gpu_channel_base(set,channel);append(final_node.parameters,base);append(final_node.parameters,gpu_channel_precision(set,channel));append(final_node.parameters,reinterpret_cast<std::uintptr_t>(margin.get()));descriptors.push_back(std::move(final_node));
 // Inactive folders disconnect their children. Do not allocate/evaluate a
 // prefix that cannot contribute to the visible channel output.
 std::vector<std::string> logical_order;for(const auto &node:descriptors)logical_order.push_back(node.id);
 std::set<std::string> required{query_layer?query_id:"output"};for(auto node=descriptors.rbegin();node!=descriptors.rend();++node)if(required.contains(node->id))required.insert(node->dependencies.begin(),node->dependencies.end());
 for(std::size_t i=inputs.size();i-->0;)if(!required.contains(descriptors[i].id)){inputs.erase(inputs.begin()+i);descriptors.erase(descriptors.begin()+i);}
 for(std::size_t i=0;i<inputs.size();++i)stack.node_owners.try_emplace(descriptors[i].id,inputs[i].layer->id);
 auto transaction=stack.plan.prepare(width,height,descriptors);
 std::set<std::string> live;for(const auto &node:descriptors)live.insert(node.id);
 if(query_layer)stack.queries[query_id]={query_head,query_enabled,*query_layer,live};
 else {std::set<QUuid> owners;for(const auto &layer:evaluation_layers(set))if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.enabled&&effect.kind==MaskEffectKind::Compare&&(effect.left<2||effect.right<2))owners.insert(layer.id);std::erase_if(stack.queries,[&](const auto &item){return item.first.ends_with("/thumbnail")||!owners.contains(item.second.owner);});}
 for(const auto &[id,query]:stack.queries)live.insert(query.nodes.begin(),query.nodes.end());
 // Keep auxiliary Compare/Anchor query closures: they are sampled inputs,
 // not edges in this compositor transaction. The snapshot seed already owns
 // only below-layer nodes, including when an auxiliary query defers early.
 if(query_layer&&!query_source&&!raw_anchor){if(auto frozen=s.adjustment_sources.find(set.id);frozen!=s.adjustment_sources.end()&&frozen->second.layer==*query_layer){
  std::erase_if(stack.outputs,[&](const auto &item){return !live.contains(item.first);});std::erase_if(stack.node_versions,[&](const auto &item){return !live.contains(item.first);});std::erase_if(stack.node_owners,[&](const auto &item){return !live.contains(item.first);});stack.plan.retain(live);stack.head=nullptr;
 }}
 if(!query_layer){
  // Occlusion is not invalidation. Keep already computed lower/folder prefixes
  // so the first opacity/visibility edit can reuse them. Never evaluate hidden
  // work just to populate this cache. Prefer the nearest complete prefix and
  // admit inactive storage against current free VRAM, not a fixed byte cap.
  const auto budget=source_cache_budget(0);std::uint64_t retained_bytes=0,retained_nodes=0;
  for(auto id=logical_order.rbegin();id!=logical_order.rend();++id){if(live.contains(*id))continue;auto found=stack.outputs.find(*id);if(found==stack.outputs.end())continue;const auto bytes=std::uint64_t(GPU_texture_width(found->second.get()))*GPU_texture_height(found->second.get())*16;
   if(bytes<=budget-retained_bytes){live.insert(*id);retained_bytes+=bytes;++retained_nodes;}
  }
  if(performance::enabled()&&retained_nodes)std::printf("BTS_GPU_PREFIX_RETAIN nodes=%llu bytes=%llu budget=%llu\n",(unsigned long long)retained_nodes,(unsigned long long)retained_bytes,(unsigned long long)budget);
  std::erase_if(stack.outputs,[&](const auto &item){return !live.contains(item.first);});
  // Upload can defer below. Retire revision records now, together with their
  // textures, so an intervening source query cannot reuse a freed output.
  stack.plan.retain(live);
 }
 // Resolve/uploads before committing any new revision. A partially uploaded
 // resource leaves the previous full-resolution result untouched.
 for(std::size_t index=0;index<inputs.size();++index){auto &in=inputs[index];const auto &layer=*in.layer;
  if(transaction.work[index].regions.empty())continue;
  in.ownership_image=s.geometry_masks.empty();in.selection_image=s.geometry_masks.empty();
  if(in.ownership){in.ownership_image=s.geometry_masks.atlas(in.ownership,s.remaining,testing_stack?performance::Clock::time_point::max():s.upload_start+std::chrono::milliseconds(4));if(!in.ownership_image)return GpuPreviewUpdate::Pending;in.selection_image=s.geometry_masks.selection(stack_key+":"+descriptors[index].id,set.geometry_names,layer.geometry);}

  for(int i=0;i<6;++i){in.mask_maps[i]=s.baked(in.mask_planes[i],protected_keys);if(!in.mask_maps[i])return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
  if(in.generator)gpu_generator_maps(in.mask_maps,*in.generator,in.generator_inputs);
  if(in.kind!=12)in.source=s.zero.get();in.mask=s.zero.get();in.surface=s.surface({},tile);in.effects=s.effects(stack_key+":"+descriptors[index].id,in.effect_rows);if(!in.effects)return GpuPreviewUpdate::Unsupported;
  if(in.enabled&&in.kind==10){if(in.geometry){in.native_context->surface=s.surface(in.geometry,tile);if(!in.native_context->surface)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;in.native_context->surface_size={in.geometry->resolution,in.geometry->height?in.geometry->height:in.geometry->resolution};in.native_context->surface_stride=sizeof(FillSurfacePoint)/4;}if(in.native_ao){in.native_context->ambient_occlusion=s.baked(in.native_ao,protected_keys);if(!in.native_context->ambient_occlusion)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}}
  else if(in.enabled&&in.kind==9)in.source=in.anchor.image;
  else if(in.enabled&&in.kind==8&&layer.quick_overlay->field)in.source=s.paint(paint_key(set,layer,0,false,tile),layer.quick_overlay->field->pixels.tile(tile),2,protected_keys);
  else if(in.enabled&&in.kind==7)in.source=s.paint(in.effect_key,in.effect_pixels->tile(tile),in.effect_plane,protected_keys);
  else if(in.enabled&&in.kind==1)in.source=s.paint(paint_key(set,layer,channel,false,tile),in.channel.pixels->tile(tile),in.channel.plane,protected_keys);
  else if(in.enabled&&in.effect_fill)in.source=s.resource(*in.effect_fill->texture,protected_keys);
  else if(in.enabled&&(in.kind==2||in.kind==5)&&fill_channel_texture(layer.fill,in.channel.fill_channel))in.source=s.resource(*tile_resource(layer.fill,in.channel.fill_channel,tile),protected_keys);
  if(in.enabled&&(in.kind==5||(in.content_pass==4&&in.geometry))){in.surface=s.surface(in.geometry,tile);if(!in.surface)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
  if(in.kind==12&&!in.source){if(const auto error=s.native_normal_errors.find(set.id);error!=s.native_normal_errors.end()){s.native_image_failure=error->second;s.input_rejected=true;return GpuPreviewUpdate::Unsupported;}return GpuPreviewUpdate::Pending;}
  if(!in.source)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
  if(!in.content_pass&&in.enabled&&layer.mask&&layer.mask->enabled){in.mask=in.mask_stages.empty()?s.paint(paint_key(set,layer,2,true,tile),layer.mask->pixels.tile(tile),2,protected_keys):s.mask_prefix(paint_key(set,layer,2,true,tile),in.mask_stages,in.mask_maps,tile,width,height,protected_keys,in.mask_signature,resolve,resolve_anchor);if(!in.mask)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
 }
 s.last_dispatch_texels=transaction.texels;s.last_cached_nodes=0;s.last_active_nodes=inputs.size();
 const auto region_uniforms=[](gpu::Shader *shader,const GpuStackRegion &r){const int origin[2]={r.x,r.y},size[2]={r.width,r.height};GPU_shader_uniform_2iv(shader,"region_origin",origin);GPU_shader_uniform_2iv(shader,"region_size",size);};
 for(std::size_t index=0;index<inputs.size();++index){const auto &work=transaction.work[index];if(work.regions.empty()){++s.last_cached_nodes;continue;}const auto &in=inputs[index];const auto &layer=*in.layer;
  const int output_width=in.constant?1:width,output_height=in.constant?1:height;
  if(!stack.outputs.contains(work.id)||GPU_texture_width(stack.outputs.at(work.id).get())!=output_width||GPU_texture_height(stack.outputs.at(work.id).get())!=output_height){auto output=texture("BTS GPU cached layer prefix",output_width,output_height,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!output)return GpuPreviewUpdate::Unsupported;stack.outputs.insert_or_assign(work.id,std::move(output));}
  // A stroke snapshot can share an unchanged cached prefix. Detach on the GPU
  // before writing; never let later lower-layer edits mutate its read source.
  if(stack.outputs.at(work.id).use_count()>1){auto replacement=texture("BTS COW frozen prefix",output_width,output_height,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!replacement)return GpuPreviewUpdate::Unsupported;GPU_texture_copy(replacement.get(),stack.outputs.at(work.id).get());++s.frozen_prefix_copies;s.frozen_prefix_copy_texels+=std::uint64_t(output_width)*output_height;GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS|GPU_BARRIER_TEXTURE_UPDATE);stack.outputs.at(work.id)=std::move(replacement);}
  auto *output=stack.outputs.at(work.id).get();auto *backdrop=in.below.empty()?s.zero.get():stack.outputs.at(in.below).get();auto *source=(in.kind==3||in.kind==6||in.kind==11)?(in.group.empty()?s.zero.get():stack.outputs.at(in.group).get()):in.source;
  if(in.kind==10){if(!s.native_source||GPU_texture_width(s.native_source.get())!=width||GPU_texture_height(s.native_source.get())!=height){s.native_source=texture("BTS shared native source scratch",width,height,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!s.native_source)return GpuPreviewUpdate::Unsupported;}performance::Scope timing("gpu_native_material_source");try{for(const auto &region:work.regions)s.native_materials.render(layer.fill.material_resource,in.channel.fill_channel,*layer.fill.native_material,s.native_source.get(),*in.native_context,{region.x,region.y,region.width,region.height});}catch(const NativeFragmentImagePending &){return GpuPreviewUpdate::Pending;}catch(const std::exception &error){s.native_image_failure=error.what();s.input_rejected=true;return GpuPreviewUpdate::Unsupported;}source=s.native_source.get();}
  GpuLayerParameters parameters;auto *shader=s.layer_shader.get();GPU_shader_bind(shader);GPU_texture_bind(source,0);GPU_texture_bind(in.mask,1);GPU_texture_bind(backdrop,2);GPU_texture_bind(in.surface,3);GPU_texture_bind(in.effects,4);for(int i=0;i<6;++i)GPU_texture_bind(in.mask_maps[i],gpu_mask_map_slot(i));GPU_texture_bind(in.ownership_image,9);GPU_texture_bind(in.selection_image,10);GPU_texture_image_bind(output,0);parameters.set("precompose_source",layer.precompose_below);parameters.set("raw_linear_source",in.raw_linear_source);parameters.set("source_zero_opacity",in.zero_opacity);parameters.set("content_pass",in.content_pass);parameters.set("geometry_masked",bool(in.ownership));parameters.set("content_effects",in.content_effects);parameters.set("effect_count",int(in.effect_rows.size()));
  if(in.kind==9){const auto &ref=layer.fill.anchors.at(in.channel.fill_channel);const float options[4]={float(ref.component),float(ref.coverage),float(in.anchor.mask),float(in.anchor.enabled)};parameters.set4("anchor_options",options);}
  parameters.set("quick_editing",layer.quick_overlay&&layer.quick_overlay->editing);parameters.set("quick_inverted",layer.quick_overlay&&layer.quick_overlay->field&&layer.quick_overlay->field->inverted);
  parameters.set("fill_bump_bias",in.channel.fill_channel==4?layer.fill.bump_bias:0);parameters.set("source_ignore_alpha",in.effect_fill||!layer.fill.alpha_mask.at(in.channel.fill_channel));parameters.set("source_wrap",!in.effect_fill&&layer.fill.mode==6);parameters.set("source_filter",in.effect_fill?0:layer.fill.mode>=2?layer.fill.filtering:0);parameters.set("fill_resource_kind",in.effect_fill||fill_channel_texture(layer.fill,in.channel.fill_channel)?1:channel==0&&layer.fill.atom?2:0);parameters.set("projection_mode",layer.fill.mode);parameters.set("projection_wrap",layer.fill.wrap);const float atom_options[4]={float(layer.fill.border_width),float(layer.fill.middle_dot),float(layer.fill.border_dots),float(layer.fill.invert)};parameters.set4("atom_options",atom_options);parameters.set("layer_active",in.enabled);parameters.set("source_kind",in.kind);parameters.set("scalar_channel",channel==6?3:channel==0?0:(channel==1||channel==2||channel==5)?1:2);parameters.set("value_precision",int(layer.precompose_below?ChannelPrecision::RGB32F:gpu_channel_precision(set,channel)));parameters.set("blend_mode",int(in.channel.blend));parameters.set("opacity_value",in.channel.opacity);const auto value=in.channel.value;parameters.set3("source_value",value.data());
  parameters.set("has_mask",!in.content_pass&&(!in.mask_stages.empty()||(layer.mask&&layer.mask->enabled&&!layer.mask->pixels.empty())));parameters.set("mask_background",in.content_pass?1:!in.mask_stages.empty()?0:layer.mask&&layer.mask->enabled?layer.mask->background:1);parameters.set3("map_u",in.map_u.data());parameters.set3("map_v",in.map_v.data());
  gpu_spatial_parameters(parameters,layer.fill,in.kind==5?in.geometry.get():nullptr,set);
  if(in.content_pass==4&&in.geometry)gpu_generator_surface_parameters(parameters,in.geometry.get());
  auto *parameter_image=s.parameters(stack_key+":"+work.id,parameters);if(!parameter_image)return GpuPreviewUpdate::Unsupported;GPU_texture_bind(parameter_image,5);
  if(in.constant){region_uniforms(shader,{0,0,1,1});GPU_compute_dispatch(shader,1,1,1);}
  else for(const auto &r:work.regions){region_uniforms(shader,r);GPU_compute_dispatch(shader,(r.width+15)/16,(r.height+15)/16,1);}GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);GPU_texture_image_unbind(output);
 }
 // The versions belong to GPU output chunks, not to CPU source pixels.
 for(const auto &work:transaction.work)if(!work.regions.empty()){const auto version=++stack.node_clock;for(const auto &r:work.regions){const auto chunk=std::uint64_t(r.y/256)*((width+255)/256)+r.x/256;stack.node_versions[work.id][chunk]={r.x,r.y,r.width,r.height,version};}}
 if(query_layer){GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();stack.plan.commit(std::move(transaction),true);return GpuPreviewUpdate::Updated;}
 auto final_work=transaction.work.back();const int chunk_columns=(width+255)/256;
 if(!final_work.regions.empty()){++stack.revision;stack.last_changes.clear();for(const auto &r:final_work.regions)stack.last_changes.insert((r.y/256)*chunk_columns+r.x/256);}
 for(const auto &r:final_work.regions){const int index=(r.y/256)*chunk_columns+r.x/256;stack.output_dirty.insert(index);stack.height_dirty.insert(index);}
 stack.head=heads[0].empty()?s.zero.get():stack.outputs.at(heads[0]).get();
 if((channel==5||channel==6)&&!final_work.regions.empty()){
  if(!s.coverage_shader)s.coverage_shader=Shader(gpu_derived::coverage_shader(),GPU_shader_free);
  if(!stack.coverage)stack.coverage=texture("BTS cached AO coverage",(width+15)/16,(height+15)/16,gpu::TextureFormat::SFLOAT_32_32_32_32);
  if(!s.coverage_shader||!stack.coverage)return GpuPreviewUpdate::Unsupported;
  auto *shader=s.coverage_shader.get();GPU_shader_bind(shader);GPU_texture_bind(stack.head,0);GPU_texture_image_bind(stack.coverage.get(),0);const int size[2]={width,height};GPU_shader_uniform_2iv(shader,"size",size);
  for(const auto &r:final_work.regions){const int origin[2]={r.x,r.y};GPU_shader_uniform_2iv(shader,"origin",origin);GPU_compute_dispatch(shader,(r.width+15)/16,(r.height+15)/16,1);}GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(stack.coverage.get());
 }
 if(channel==4&&margin){
  const auto float_format=gpu::TextureFormat::SFLOAT_32_32_32_32;
  if(!stack.height_image){stack.height_image=texture("BTS padded bump for normals",width,height,float_format);if(!stack.height_image)return GpuPreviewUpdate::Unsupported;for(int i=0;i<chunk_columns*((height+255)/256);++i)stack.height_dirty.insert(i);}
  if(!stack.height_dirty.empty()){
   if(const auto state=s.initialize(width,height,float_format,false);state!=GpuPreviewUpdate::Updated)return state;
   auto *shader=s.final_shader;GPU_shader_bind(shader);GPU_texture_bind(stack.head,0);GPU_texture_image_bind(stack.height_image.get(),0);
   const int size[2]={width,height},offset[3]={};GPU_shader_uniform_2iv(shader,"tile_size",size);GPU_shader_uniform_3iv(shader,"target_offset",offset);
   GPU_shader_uniform_3fv(shader,"base_value",base.data());GPU_shader_uniform_1i(shader,"value_precision",int(gpu_channel_precision(set,channel)));GPU_shader_uniform_3fv(shader,"output_bias",std::array<float,3>{}.data());GPU_shader_uniform_1i(shader,"output_kind",0);GPU_shader_uniform_1i(shader,"finite_half",0);
   std::vector<std::uint32_t> dirty(chunk_columns*((height+255)/256));
   for(auto chunk:stack.height_dirty){dirty[chunk]=1;const int x=chunk%chunk_columns*256,y=chunk/chunk_columns*256;const GpuStackRegion r{x,y,std::min(256,width-x),std::min(256,height-y)};region_uniforms(shader,r);GPU_compute_dispatch(shader,(r.width+15)/16,(r.height+15)/16,1);}
   GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind_all();GPU_texture_unbind_all();GPU_shader_unbind();
   if(!s.margins.apply(stack_key+":height",margin,stack.height_image.get(),{},dirty))return GpuPreviewUpdate::Unsupported;
   stack.height_dirty.clear();if(const auto state=s.initialize(width,height,format,GPU_texture_is_array(target));state!=GpuPreviewUpdate::Updated)return state;
  }
 }
 if(!prefix_only){final_work.regions.clear();if(target_changed)for(int i=0;i<chunk_columns*((height+255)/256);++i)stack.output_dirty.insert(i);for(auto chunk:stack.output_dirty){const int x=chunk%chunk_columns*256,y=chunk/chunk_columns*256;final_work.regions.push_back({x,y,std::min(256,width-x),std::min(256,height-y)});}}
 if(!prefix_only&&(channel!=5||authored_ao_output)&&!final_work.regions.empty()){
  auto *shader=target_final_shader;GPU_shader_bind(shader);GPU_texture_bind(heads[0].empty()?s.zero.get():stack.outputs.at(heads[0]).get(),0);GPU_texture_image_bind(target,0);const int logical_size[2]={width,height};GPU_shader_uniform_2iv(shader,"tile_size",logical_size);GPU_shader_uniform_3iv(shader,"target_offset",target_offset.data());GPU_shader_uniform_3fv(shader,"base_value",base.data());GPU_shader_uniform_1i(shader,"value_precision",int(gpu_channel_precision(set,channel)));const float bump_bias=channel==4?set.bump_offset():0;GPU_shader_uniform_3fv(shader,"output_bias",std::array<float,3>{bump_bias,bump_bias,bump_bias}.data());GPU_shader_uniform_1i(shader,"output_kind",channel==6?2:gpu_scalar_channel(channel)?1:0);GPU_shader_uniform_1i(shader,"finite_half",format==gpu::TextureFormat::SFLOAT_16_16_16_16);
  for(const auto &r:final_work.regions){region_uniforms(shader,r);GPU_compute_dispatch(shader,(r.width+15)/16,(r.height+15)/16,1);}GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(target);
  if(margin){std::vector<std::uint32_t> dirty(chunk_columns*((height+255)/256));for(auto chunk:stack.output_dirty)dirty[chunk]=1;if(!s.margins.apply(stack_key+":output:"+std::to_string(reinterpret_cast<std::uintptr_t>(target)),margin,target,target_offset,dirty))return GpuPreviewUpdate::Unsupported;}
  stack.output_dirty.clear();
  s.image_changed=true;
 }
 GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();stack.plan.commit(std::move(transaction),true);stack.plan.retain(live);std::erase_if(stack.node_versions,[&](const auto &entry){return !live.contains(entry.first);});if(!prefix_only)s.target_owners[target_key]=stack_key;
 if(!benchmark_stack&&!prefix_only&&channel!=5&&!margin&&std::getenv("BTS_VERIFY_GPU_STACK")&&!std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return bool(layer.fill.native_material);})){
  std::string signature=set.id.toString().toStdString()+std::to_string(channel)+":"+std::to_string(tile);
  for(const auto &layer:evaluation_layers(set))if(active(layer,channel,tile)){signature+=std::to_string(int(layer.kind))+":"+std::to_string(int(gpu_layer_channel(layer,channel).blend))+":"+std::to_string(gpu_layer_channel(layer,channel).opacity<1)+":"+std::to_string(bool(layer.mask))+":"+std::to_string(layer.mask&&!layer.mask->pixels.empty())+":"+std::to_string(layer.fill.scale!=std::array<double,2>{1,1})+":"+std::to_string(layer.fill.rotation!=0)+":"+std::to_string(layer.fill.offset!=std::array<double,2>{});}
  if(std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[](const auto &layer){return layer.fill.mode==5||(layer.mask&&any_mask_effect(layer.mask->effects,[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Fill&&effect.mapping.projection.mode==5;}));}))signature+=":warp:"+std::to_string(stack.revision);
  static std::set<std::string> verified;
  const bool neighborhood=spatial_oracle_unavailable(set)||std::any_of(evaluation_layers(set).begin(),evaluation_layers(set).end(),[&](const auto &layer){const bool content=std::any_of(layer.effects.begin(),layer.effects.end(),[&](const auto &effect){return effect.enabled&&effect.kind==ContentEffectKind::Filter&&effect.filter.resource>=7&&effect.channel==gpu_layer_channel(layer,channel).fill_channel;});const bool mask=layer.mask&&std::any_of(layer.mask->effects.begin(),layer.mask->effects.end(),[](const auto &effect){return effect.enabled&&effect.kind==MaskEffectKind::Filter&&effect.filter.resource>=7;});return content||mask;});
  if(neighborhood)std::printf("BTS_GPU_STACK_NEIGHBORHOOD skipped_cpu_oracle=1\n");
  else if(testing_stack||verified.insert(signature).second){
    auto *pixels=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));double error=0;bool valid=true;std::array<bool,6> wanted{};if(channel<6)wanted[channel]=true;else if(channel==6)wanted[3]=true;
    for(int gy=0;gy<8;++gy)for(int gx=0;gx<8;++gx){const int x=std::min(s.width-1,int((gx+.5)*s.width/8)),y=std::min(s.height-1,int((gy+.5)*s.height/8));std::array<float,4> expected{};if(channel==6){const auto result=TextureDocument::evaluate_channels(set,wanted,x,y,tile);const auto full=precision_channel(result.material,3,set.precision[3]);expected={full[0],full[1],full[2],1};}else if(gpu_scalar_channel(channel)){const auto value=channel==16?TextureDocument::composite_displacement(set,x,y,tile):TextureDocument::composite_user(set,channel-7,x,y,tile);expected={value[0]*value[1],value[0]*value[1],value[0]*value[1],value[1]};}else{const auto result=TextureDocument::evaluate_channels(set,wanted,x,y,tile);const auto full=precision_channel(result.material,channel,gpu_channel_precision(set,channel));const float a=result.coverage[channel];expected={full[0]-base[0]*(1-a),full[1]-base[1]*(1-a),full[2]-base[2]*(1-a),a};}
      for(int k=0;k<4;++k){if(format==gpu::TextureFormat::SFLOAT_16_16_16_16)expected[k]=math::half_to_float(math::float_to_half_make_finite(expected[k]));const float actual=pixels[((std::size_t(target_offset[2])*GPU_texture_height(target)+y+target_offset[1])*GPU_texture_width(target)+x+target_offset[0])*4+k];const double difference=std::abs(double(actual)-expected[k]);const auto precision=gpu_channel_precision(set,channel);double tolerance=(format==gpu::TextureFormat::SFLOAT_32_32_32_32?1e-6:.001)*std::max(1.f,std::abs(expected[k]));if(k<3&&(precision==ChannelPrecision::SRGB8||precision==ChannelPrecision::RGB8||precision==ChannelPrecision::L8))tolerance+=precision==ChannelPrecision::SRGB8?.009:1./255;if(valid&&difference>tolerance&&channel==4){const auto cpu=TextureDocument::evaluate_channels(set,wanted,x,y,tile);auto *input=heads[0].empty()?s.zero.get():stack.outputs.at(heads[0]).get();const std::array<std::array<int,3>,1> point{{{std::min(x,GPU_texture_width(input)-1),std::min(y,GPU_texture_height(input)-1),0}}};const auto raw=probe_gpu_texture(input,point).front();std::fprintf(stderr,"BTS_GPU_BUMP_FAILURE_RAW cpu=%.12g cpu_coverage=%.12g gpu_prefix=%.12g,%.12g,%.12g,%.12g base=%.12g bias=%.12g precision=%d target_format=%d\n",cpu.material.bump,cpu.coverage[4],raw[0],raw[1],raw[2],raw[3],base[0],set.bump_offset(),int(precision),int(format));}if(valid&&difference>tolerance)s.oracle_position={x,y};if(valid&&difference>tolerance)std::fprintf(stderr,"BTS_GPU_STACK_DIFFERENCE channel=%d mode=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",channel,set.layers.back().fill.mode,x,y,k,expected[k],actual);valid&=std::isfinite(actual)&&difference<=tolerance;error=std::max(error,difference);}
    }MEM_delete(pixels);std::printf("BTS_GPU_STACK_VERIFIED channel=%d probes=64 max_error=%.9g valid=%d\n",channel,error,int(valid));if(!valid)throw std::runtime_error("GPU layer preview differs from CPU reference");
  }
 }
 std::printf("BTS_GPU_STACK_CACHE dispatch_texels=%llu cached_nodes=%llu nodes=%zu\n",(unsigned long long)s.last_dispatch_texels,(unsigned long long)s.last_cached_nodes,inputs.size());
 std::printf("BTS_GPU_STACK channel=%d layers=%zu texels=%llu source_upload_bytes=%llu\n",channel,inputs.size(),static_cast<unsigned long long>(std::uint64_t(s.width)*s.height),static_cast<unsigned long long>(upload_before-s.remaining));return GpuPreviewUpdate::Updated;
}
std::optional<GpuComparisonInputs> GpuLayerPreview::comparison_inputs(const TextureSet &set,const GpuMaskStage &stage,gpu::Texture *target,int tile){
 auto &s=*impl_;if(stage.rows.empty()){s.input_rejected=true;return std::nullopt;}GpuComparisonInputs result;for(auto &input:result)input.image=s.zero.get();
 const int channel=stage.comparison_channel==3?6:stage.comparison_channel;
 const auto &row=stage.rows.front();
 for(int operand=0;operand<2;++operand){
  if(int(row[1][0])!=operand&&int(row[1][1])!=operand)continue;
  const auto memo_key=std::to_string(reinterpret_cast<std::uintptr_t>(&set))+":"+stage.comparison_layer.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile)+":"+std::to_string(operand);
  if(const auto cached=s.comparison_memo.find(memo_key);cached!=s.comparison_memo.end()){result[operand]=cached->second;continue;}
  const auto updated=update_tile(set,channel,target,tile,{},true,&stage.comparison_layer,operand==1);
  if(updated!=GpuPreviewUpdate::Updated){s.input_rejected=updated==GpuPreviewUpdate::Unsupported;return std::nullopt;}
  const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);
  auto &stack=s.stacks.at(key);const auto query_id="query/"+stage.comparison_layer.toString().toStdString()+(operand?"/source":"/below");const auto &query=stack.queries.at(query_id);
  auto &input=result[operand];input.key=key+query_id;input.enabled=query.enabled;input.base=gpu_channel_base(set,channel);input.image=query.root.empty()?s.zero.get():stack.outputs.at(query.root).get();input.pages=stack.node_versions.at(query_id);s.comparison_memo.emplace(memo_key,input);
 }
 return result;
}
std::optional<GpuAnchorInput> GpuLayerPreview::anchor_input(const TextureSet &set,const QUuid &consumer,const AnchorReference &ref,int tile){
 auto &s=*impl_;GpuAnchorInput input;input.image=s.zero.get();input.key="broken:"+ref.source.toString().toStdString();
 if(!TextureDocument::anchor_reference_problem(set,consumer,ref).isEmpty())return input;
 const auto point=TextureDocument::anchor_point(set,ref.source);if(!point)return input;
 const auto memo=std::to_string(reinterpret_cast<std::uintptr_t>(&set))+":"+ref.source.toString().toStdString()+":"+std::to_string(ref.channel)+":"+std::to_string(tile);
 if(const auto found=s.anchor_memo.find(memo);found!=s.anchor_memo.end())return found->second;
 if(point->mask){
  auto *image=mask_texture(set,point->layer,tile,&point->id);if(!image)return std::nullopt;
  const auto &layers=evaluation_layers(set);const auto layer=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==point->layer;});
  const auto key=paint_key(set,*layer,2,true,tile);auto &stack=s.mask_stacks.at(key);
  input.image=image;input.key=key+point->id.toString().toStdString();input.pages=stack.node_versions[point->id.toString().toStdString()];input.enabled=true;input.mask=true;
 }else{
  const int channel=ref.channel==3?6:ref.channel==14?16:ref.channel>=6?ref.channel+1:ref.channel;
  const auto updated=update_tile(set,channel,s.zero.get(),tile,{},true,&point->layer,true,&point->id,false,true);
  if(updated!=GpuPreviewUpdate::Updated){s.input_rejected=updated==GpuPreviewUpdate::Unsupported;return std::nullopt;}
  const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile)+":anchor:"+point->layer.toString().toStdString();auto &stack=s.stacks.at(key);
  const auto query_id="query/"+point->layer.toString().toStdString()+"/source/before-selected-effect/anchor/"+point->id.toString().toStdString();const auto &query=stack.queries.at(query_id);
  input.image=query.root.empty()?s.zero.get():stack.outputs.at(query.root).get();input.key=key+query_id;input.pages=stack.node_versions[query_id];input.enabled=query.enabled;
 }
 s.anchor_memo.emplace(memo,input);return input;
}
GpuPreviewUpdate GpuLayerPreview::histogram(const TextureSet &set,const HistogramRequest &request,std::uint64_t ticket){
 PaintMetadataScope metadata;performance::Scope timing("gpu_ui_histogram");auto &s=*impl_;
 if(set.tiles.empty())return GpuPreviewUpdate::Unsupported;
 const int tile=*set.tiles.begin(),w=set.width(tile),h=set.rows(tile);
 if(const auto state=s.initialize(w,h,gpu::TextureFormat::SFLOAT_32_32_32_32,false);state!=GpuPreviewUpdate::Updated)return state;
 const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==request.layer;});if(found==layers.end())return GpuPreviewUpdate::Unsupported;
 GpuHistogram::Input input{s.zero.get(),s.zero.get(),s.zero.get()};input.owners=input.selection=s.geometry_masks.empty();input.width=w;input.height=h;input.channel=request.channel;input.compare=request.compare;input.component=request.component;input.curve=request.curve;
 if(request.mask){
  if(!found->mask)return GpuPreviewUpdate::Unsupported;
  const auto *effect=find_mask_effect(found->mask->effects,request.effect);const auto input_mask=mask_effect_input(*found->mask,request.effect);if(!effect||!input_mask)return GpuPreviewUpdate::Unsupported;
  auto prefix=*found;prefix.mask=*input_mask;const auto selected=*effect;GpuMaskBuilder builder(set,tile);const auto stages=builder.build(prefix,true);
  std::set<std::string> protected_keys;std::array<bool,6> needed{};std::array<gpu::Texture *,6> maps{};std::string signature;
  for(const auto &stage:stages)gpu_visit_mask_stage(stage,[&](const auto &part){if(part.pixels)protected_keys.insert(part.owner_key+":"+part.id);for(const auto &row:part.rows){needed[0]|=row[0][0]==4&&row[1][0]!=0;if(row[0][0]==3)gpu_need_generator_row_maps(needed,row);}},true);
  std::array<const MeshMapPlane *,6> planes{};for(int i=0;i<6;++i)if(needed[i])if(const auto &map=set.mesh_maps[gpu_mask_map_roles[i]];map){const auto member=map->tiles.find(tile);if(member!=map->tiles.end()&&member->second.pixels){planes[i]=&member->second;const auto &p=*planes[i]->pixels;const auto key="baked:"+p.hash+":"+std::to_string(p.resolution)+":"+std::to_string(p.height);protected_keys.insert(key);signature+=key;}}
  for(int i=0;i<6;++i){maps[i]=s.baked(planes[i],protected_keys);if(!maps[i])return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
  const GpuComparisonResolver resolve=[&](const GpuMaskStage &stage){return comparison_inputs(set,stage,s.zero.get(),tile);};
 const GpuAnchorResolver resolve_anchor=[&](const QUuid &consumer,const AnchorReference &ref){return anchor_input(set,consumer,ref,tile);};
  input.mask=s.mask_prefix(paint_key(set,*found,2,true,tile),stages,maps,tile,w,h,protected_keys,signature,resolve,resolve_anchor,true);if(!input.mask)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
  if(request.compare){
   GpuMaskStage stage;stage.comparison_layer=found->id;stage.comparison_channel=selected.compare_channel;TextureLayer description;description.mask=LayerMask{};description.mask->effects={selected};description.mask->effects.back().enabled=true;description.mask->effects.back().blending=LayerBlend::Normal;gpu_append_mask_effects(stage.rows,description,set,tile);if(stage.rows.empty())return GpuPreviewUpdate::Unsupported;
   const auto values=resolve(stage);if(!values)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
   input.below=(*values)[0].image;input.own=(*values)[1].image;input.enabled={(*values)[0].enabled,(*values)[1].enabled};input.base=(*values)[0].base;input.operands={selected.left,selected.right};input.constant=selected.constant;input.channel=selected.compare_channel;
  }else{input.channel=-1;input.component=0;}
 }else{
  const int channel=request.channel==3?6:request.channel==14?16:request.channel>=6?request.channel+1:request.channel;
  if(request.channel>=6){const auto data=gpu_layer_channel(*found,channel);const bool included=found->visible&&data.enabled&&data.blend!=LayerBlend::Disabled&&(found->geometry.type==1?found->geometry.includes_tile(tile):!found->geometry.meshes.empty()||found->geometry.default_include);input.inclusion=included?0:1;
   if(input.inclusion==0&&found->geometry.type==0&&!found->geometry.meshes.empty()){const auto ownership=set.geometry_ownership.find(tile);if(ownership==set.geometry_ownership.end())return GpuPreviewUpdate::Pending;input.owners=s.geometry_masks.atlas(ownership->second,s.remaining,testing_stack?performance::Clock::time_point::max():s.upload_start+std::chrono::milliseconds(4));if(!input.owners)return GpuPreviewUpdate::Pending;input.selection=s.geometry_masks.selection("histogram:"+request.layer.toString().toStdString(),set.geometry_names,found->geometry);input.inclusion=2;}}
  const auto updated=update_tile(set,channel,s.zero.get(),tile,{},true,&request.layer,true,&request.effect);if(updated!=GpuPreviewUpdate::Updated)return updated;
  const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);auto &stack=s.stacks.at(key);const auto query_id="query/"+request.layer.toString().toStdString()+"/source/before-selected-effect";const auto &query=stack.queries.at(query_id);input.own=query.root.empty()?s.zero.get():stack.outputs.at(query.root).get();
 }
 s.histograms.update(request.effect.toString().toStdString(),input,ticket);return GpuPreviewUpdate::Updated;
}
gpu::Texture *GpuLayerPreview::mask_texture(const TextureSet &set,const QUuid &layer,int tile,const QUuid *point){
 auto &s=*impl_;const int w=set.width(tile),h=set.rows(tile);const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==layer;});if(found==layers.end()||!found->mask){s.input_rejected=true;return nullptr;}
 GpuMaskBuilder builder(set,tile);auto stages=builder.build(*found,true);if(point){const auto stop=std::find_if(stages.begin(),stages.end(),[&](const auto &stage){return stage.id==point->toString().toStdString();});if(stop==stages.end()){s.input_rejected=true;return nullptr;}stages.erase(stop+1,stages.end());}if(stages.empty()){s.input_rejected=true;return nullptr;};
 std::set<std::string> protected_keys;std::array<bool,6> needed{};std::array<gpu::Texture *,6> maps{};std::string signature;
 for(const auto &stage:stages)gpu_visit_mask_stage(stage,[&](const auto &part){if(part.pixels)protected_keys.insert(part.owner_key+":"+part.id);for(const auto &row:part.rows){needed[0]|=row[0][0]==4&&row[1][0]!=0;if(row[0][0]==3)gpu_need_generator_row_maps(needed,row);}},true);
 std::array<const MeshMapPlane *,6> planes{};
 for(int i=0;i<6;++i)if(needed[i])if(const auto &map=set.mesh_maps[gpu_mask_map_roles[i]];map){const auto member=map->tiles.find(tile);if(member!=map->tiles.end()&&member->second.pixels){planes[i]=&member->second;const auto &p=*planes[i]->pixels;const auto key="baked:"+p.hash+":"+std::to_string(p.resolution)+":"+std::to_string(p.height);protected_keys.insert(key);signature+=key;}}
 for(int i=0;i<6;++i){maps[i]=s.baked(planes[i],protected_keys);if(!maps[i])return nullptr;}
 const GpuComparisonResolver resolve=[&](const GpuMaskStage &stage){return comparison_inputs(set,stage,s.zero.get(),tile);};
 const GpuAnchorResolver resolve_anchor=[&](const QUuid &consumer,const AnchorReference &ref){return anchor_input(set,consumer,ref,tile);};
 const auto key=paint_key(set,*found,2,true,tile);auto *mask=s.mask_prefix(key,stages,maps,tile,w,h,protected_keys,signature,resolve,resolve_anchor,point!=nullptr);if(!mask)return nullptr;
 return mask;
}
GpuPreviewUpdate GpuLayerPreview::sample_material(const TextureSet &set,const MaterialSampleRequest &request,std::uint64_t ticket){
 PaintMetadataScope metadata;performance::Scope timing("gpu_material_pick");auto &s=*impl_;
 if(s.material_samples.pending())return GpuPreviewUpdate::Pending;
 const int tile=request.tile;if(!set.tiles.contains(tile)||request.x<0||request.y<0||request.x>=set.width(tile)||request.y>=set.rows(tile))return GpuPreviewUpdate::Unsupported;
 const int w=set.width(tile),h=set.rows(tile);s.input_rejected=false;
 if(const auto state=s.initialize(w,h,gpu::TextureFormat::SFLOAT_32_32_32_32,false);state!=GpuPreviewUpdate::Updated)return state;
 std::array<GpuMaterialSample::Input,6> inputs;
 if(!request.mask.isNull()){
  const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==request.mask;});if(found==layers.end()||!found->mask)return GpuPreviewUpdate::Unsupported;
 auto *mask=mask_texture(set,found->id,tile);if(!mask)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
  inputs[0].image=mask;
 }else{
  const QUuid whole_stack;
  for(int c=0;c<6;++c){if(!request.channels[c])continue;const int channel=c==3?6:c;
   const auto state=update_tile(set,channel,s.zero.get(),tile,{},true,&whole_stack);if(state!=GpuPreviewUpdate::Updated)return state;
   auto &stack=s.stacks.at(set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile));
   const auto &query=stack.queries.at("query/"+whole_stack.toString().toStdString()+"/below");
   inputs[c]={query.root.empty()?s.zero.get():stack.outputs.at(query.root).get(),gpu_channel_base(set,channel)};
  }
 }
 s.material_samples.update(request,inputs,ticket);return GpuPreviewUpdate::Updated;
}
std::optional<MaterialSample> GpuLayerPreview::material_sample_result()const{return impl_->material_samples.get();}
GpuPreviewUpdate GpuLayerPreview::capture_author_chunk(const std::string &key,const PaintPixels &pixels,int channel,int tile,int x,int y,int width,int height,GpuProjectReadback &readback){
 PaintMetadataScope metadata;auto &s=*impl_;s.input_rejected=false;
 const auto *source=s.paint(key,pixels,channel,{key});if(!source)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
 readback.submit(0,tile,const_cast<gpu::Texture *>(source),x,y,0,width,height);return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::capture_layer(const TextureSet &set,const LayerCaptureRequest &request,std::uint64_t ticket){
 PaintMetadataScope metadata;performance::Scope timing("gpu_layer_capture");auto &s=*impl_;s.input_rejected=false;if(s.layer_capture.pending())return GpuPreviewUpdate::Pending;
 if(!set.tiles.contains(request.tile)||request.channel<0||request.channel>(request.final_composite?16:14)||request.channel==15)return GpuPreviewUpdate::Unsupported;
 const int tile=request.tile,w=set.width(tile),h=set.rows(tile);if(const auto state=s.initialize(w,h,gpu::TextureFormat::SFLOAT_32_32_32_32,false);state!=GpuPreviewUpdate::Updated)return state;
 if(request.final_composite){
  if(!s.export_final||GPU_texture_width(s.export_final.get())!=w||GPU_texture_height(s.export_final.get())!=h)s.export_final=texture("BTS explicit export final GPU channel",w,h,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!s.export_final)return GpuPreviewUpdate::Unsupported;
  const int channel=request.normal_perturbation?3:request.channel;GpuPreviewUpdate status;
  if(channel==5&&request.authored_ao)status=update_tile(set,channel,s.export_final.get(),tile,{},false,nullptr,false,nullptr,false,false,true);
  else if(channel==3||channel==5){std::map<int,std::array<int,3>> regions;for(int number:set.tiles)regions[number]={};status=request.normal_perturbation?resolve_normal(set,s.export_final.get(),regions,tile,true):update_tiles_impl(set,channel,s.export_final.get(),regions,tile);}
  else status=update(set,channel,s.export_final.get(),tile);
  if(status!=GpuPreviewUpdate::Updated)return status;
  GpuLayerCapture::Input final{s.export_final.get(),s.geometry_masks.empty(),s.geometry_masks.empty(),w,h};
  if(channel==0||channel==1||channel==2||channel==3||channel==4||channel==16||(channel==5&&request.authored_ao)){
   final.completion=1;
   if(channel==3)final.completion_base=precision_channel(set.channels[3]?set.base_values:MaterialValues{},3,set.precision[3]);
   else if(channel==16)final.completion_base.fill(set.displacement.midlevel);
   else {final.completion_base=gpu_channel_base(set,channel);if(channel==4)final.completion_base.fill(precision_bump_base(set));}
  }else if(gpu_scalar_channel(channel)){final.completion=2;final.completion_base=gpu_channel_base(set,channel);}
  s.layer_capture.update(request,final,ticket);return GpuPreviewUpdate::Updated;
 }
 const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==request.layer;});if(found==layers.end())return GpuPreviewUpdate::Unsupported;
 GpuLayerCapture::Input input{s.zero.get(),s.geometry_masks.empty(),s.geometry_masks.empty(),w,h};
 if(request.mask){input.image=mask_texture(set,request.layer,tile);if(!input.image)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
 else {
  const int channel=request.channel==3?6:request.channel==14?16:request.channel>=6?request.channel+1:request.channel;
  TextureSet normalized=set;normalized.instance_layers.reset();normalized.layers=layers;auto &root=normalized.layers.at(found-layers.begin());root.mask.reset();
  QUuid query=request.layer;std::string suffix="/source";
  if(request.contextual){
   TextureLayer sentinel;sentinel.id=QUuid("{318aff28-684e-4183-aeb9-72e1fa400a81}");sentinel.depth=found->depth;sentinel.parent=found->parent;query=sentinel.id;normalized.layers.insert(normalized.layers.begin()+(found-layers.begin())+1,sentinel);suffix="/below";
  }else{
   root.channels=layer_source_channels(root.channels,root.kind==LayerKind::Fill&&bool(root.fill.native_material));root.opacity.fill(1);root.blending.fill(LayerBlend::Normal);root.ao_enabled=true;root.ao_opacity=1;root.ao_blending=LayerBlend::Normal;
   for(auto &user:root.users){user.enabled=true;user.opacity=1;user.blending=LayerBlend::Normal;}root.displacement.enabled=true;root.displacement.opacity=1;root.displacement.blending=LayerBlend::Normal;
  }
  const auto state=update_tile(normalized,channel,s.zero.get(),tile,{},true,&query,!request.contextual);if(state!=GpuPreviewUpdate::Updated)return state;
  auto &stack=s.stacks.at(set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile));const auto &value=stack.queries.at("query/"+query.toString().toStdString()+suffix);input.image=value.root.empty()?s.zero.get():stack.outputs.at(value.root).get();
  if(!request.contextual){const auto &g=found->geometry;input.inclusion=g.type==1?(g.includes_tile(tile)?0:1):g.meshes.empty()?(g.default_include?0:1):2;
   if(input.inclusion==2){auto owner=set.geometry_ownership.find(tile);if(owner==set.geometry_ownership.end())return GpuPreviewUpdate::Pending;input.owners=s.geometry_masks.atlas(owner->second,s.remaining,testing_stack?performance::Clock::time_point::max():s.upload_start+std::chrono::milliseconds(4));if(!input.owners)return GpuPreviewUpdate::Pending;input.selection=s.geometry_masks.selection("capture:"+request.layer.toString().toStdString(),set.geometry_names,g);}
  }
 }
 s.layer_capture.update(request,input,ticket);return GpuPreviewUpdate::Updated;
}
std::optional<LayerCaptureResult> GpuLayerPreview::layer_capture_result()const{return impl_->layer_capture.get();}
GpuPreviewUpdate GpuLayerPreview::thumbnail(const TextureSet &set,const ThumbnailRequest &request,std::uint64_t ticket){
 PaintMetadataScope metadata;performance::Scope timing("gpu_layer_thumbnail");auto &s=*impl_;s.input_rejected=false;if(s.thumbnails.pending())return GpuPreviewUpdate::Pending;if(set.tiles.empty()||request.channel<0||request.channel>14)return GpuPreviewUpdate::Unsupported;
 const int channel=request.channel==3?6:request.channel==14?16:request.channel>=6?request.channel+1:request.channel;
 const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==request.layer;});if(found==layers.end())return GpuPreviewUpdate::Unsupported;
 std::vector<GpuThumbnail::Input> inputs;
 for(int tile:set.tiles){const int w=set.width(tile),h=set.rows(tile);if(const auto state=s.initialize(w,h,gpu::TextureFormat::SFLOAT_32_32_32_32,false);state!=GpuPreviewUpdate::Updated)return state;gpu::Texture *source=s.zero.get();
  if(request.mask){source=mask_texture(set,request.layer,tile);if(!source)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
  else if(gpu_channel_enabled(set,channel)&&gpu_layer_channel(*found,channel).enabled){
   const auto state=update_tile(set,channel,s.zero.get(),tile,{},true,&request.layer,true,nullptr,true);if(state!=GpuPreviewUpdate::Updated)return state;
   auto &stack=s.stacks.at(set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile));const auto &query=stack.queries.at("query/"+request.layer.toString().toStdString()+"/source/thumbnail");source=query.root.empty()?s.zero.get():stack.outputs.at(query.root).get();
  }
  inputs.push_back({source,w,h});
 }
 s.thumbnails.update(request,inputs,ticket);return GpuPreviewUpdate::Updated;
}
std::optional<ThumbnailMetadata> GpuLayerPreview::thumbnail_result()const{return impl_->thumbnails.get();}
void GpuLayerPreview::diagnose_pixel(const TextureSet &set,int channel,int tile,int x,int y){
 const auto key=set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);const auto found=impl_->stacks.find(key);if(found==impl_->stacks.end())return;
 for(const auto &layer:evaluation_layers(set))if(layer.kind==LayerKind::Fill&&layer.fill.mode==5){
  const auto atlas=set.projection_tiles.contains(tile)?set.projection_tiles.at(tile):set.projection_geometry;if(!atlas||!atlas->tiles.contains(tile))continue;
  using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(1,1).sampler(5,ImageType::Float2D,"layer_parameters_img").image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img").push_constant(Type::float3_t,"world_point");
  info.compute_source_generated=GpuLayerParameters::declarations()+gpu_warp::source+R"GLSL(
void main(){vec3 v=world_point-projection_offset,p=vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v))*inverse_denominator;vec2 uv=vec2(-99);float depth=0;vec3 normal,du,dv;bool valid=warp_project(p,uv,depth,normal,du,dv);imageStore(result_img,ivec2(0),vec4(uv,depth,float(valid)));imageStore(result_img,ivec2(1,0),vec4(du,warp_projection_error));imageStore(result_img,ivec2(2,0),vec4(dv,0));}
)GLSL";
  Shader shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);GpuLayerParameters parameters;gpu_spatial_parameters(parameters,layer.fill,atlas.get(),set);auto input=texture("BTS warp projection diagnostic",parameters.values.size(),1,gpu::TextureFormat::SFLOAT_32_32_32_32);GPU_texture_update(input.get(),GPU_DATA_FLOAT,parameters.values.data());auto output=texture("BTS warp projection diagnostic result",3,1,gpu::TextureFormat::SFLOAT_32_32_32_32);
  const auto &point=atlas->tiles.at(tile)[y*atlas->resolution+x];GPU_shader_bind(shader.get());GPU_texture_bind(input.get(),5);GPU_texture_image_bind(output.get(),0);GPU_shader_uniform_3fv(shader.get(),"world_point",point.position.data());GPU_compute_dispatch(shader.get(),1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);auto *actual=static_cast<float *>(GPU_texture_read(output.get(),GPU_DATA_FLOAT,0));const auto frame=fill_planar_frame(layer.fill,point,*atlas);const auto expected=fill_warp_coordinate(layer.fill,frame);
  std::printf("BTS_GPU_WARP_NATIVE_DIAGNOSTIC x=%d y=%d gpu_uv=%.9g,%.9g depth=%.9g valid=%g error=%.9g cpu_uv=%.9g,%.9g depth=%.9g error=%.9g coverage=%g\n",x,y,actual[0],actual[1],actual[2],actual[3],actual[7],expected?expected->u:-99,expected?expected->v:-99,expected?expected->depth:-99,expected?expected->error:-1,expected?fill_warp_coverage(layer.fill,frame,*expected):0);
  QJsonArray rows;for(const auto &row:parameters.values)rows.append(QJsonArray{row[0],row[1],row[2],row[3]});std::printf("BTS_GPU_WARP_NATIVE_INPUT %s\n",QJsonDocument(QJsonObject{{"parameters",rows},{"point",QJsonArray{point.position[0],point.position[1],point.position[2]}}}).toJson(QJsonDocument::Compact).constData());
  MEM_delete(actual);GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
 }
 for(const auto &[id,image]:found->second.outputs){const std::array<std::array<int,3>,1> locations{{{std::min(x,GPU_texture_width(image.get())-1),std::min(y,GPU_texture_height(image.get())-1),0}}};const auto p=probe_gpu_texture(image.get(),locations).front();std::printf("BTS_GPU_ORACLE_PREFIX id=%s rgba=%.9g,%.9g,%.9g,%.9g\n",id.c_str(),p[0],p[1],p[2],p[3]);}
}
void GpuLayerPreview::set_displacement_epoch(std::uint32_t uid,std::uint64_t epoch){impl_->displacement_epochs[uid]=epoch;}
void GpuLayerPreview::retire_layer_channel(const TextureDocument &document,const TextureSet &set,
    const TextureLayer &previous,int channel,std::span<const std::shared_ptr<const FillTexture>> pending)
{
 PaintMetadataScope metadata;performance::Scope timing("gpu_layer_channel_retire");auto &s=*impl_;
 const auto before=blender::gpu::host_texture_memory::live_bytes();
 const int gpu_channel=channel==14?16:channel>=6?channel+1:channel==3?6:channel;
 const int plane=channel<5?channel:2;const auto owner=previous.id.toString().toStdString();
 std::set<std::string> protected_resources,removed_resources;
 std::function<void(const std::shared_ptr<const FillTexture> &,std::set<std::string> &)> keys;
 keys=[&](const auto &texture,auto &result){if(!texture)return;if(texture->tiles){for(const auto &[tile,member]:*texture->tiles)keys(member,result);}else result.insert(resource_key(*texture));};
 keys(fill_channel_texture(previous.fill,channel),removed_resources);
 const auto keep_projection=[&](const FillProjection &projection){for(int c=0;c<15;++c)keys(fill_channel_texture(projection,c),protected_resources);};
 for(const auto &candidate:document.texture_sets())for(const auto &layer:evaluation_layers(candidate)){
  for(int c=0;c<15;++c){const bool enabled=c<5?layer.channels[c]:c==5?layer.ao_enabled:c==14?layer.displacement.enabled:layer.users[c-6].enabled;if(enabled)keys(fill_channel_texture(layer.fill,c),protected_resources);}
  if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))keep_projection(effect.mapping.projection);
 }
 for(const auto &texture:pending)keys(texture,protected_resources);
 std::vector<std::string> input_keys,stack_keys;
 for(int tile:set.tiles){input_keys.push_back(paint_key(set,previous,gpu_channel,false,tile));const auto stack=set.id.toString().toStdString()+":"+std::to_string(gpu_channel)+":"+std::to_string(tile);stack_keys.push_back(stack);input_keys.push_back(stack+":"+owner);}
 const auto owned_node=[&](const std::string &key){return key==owner||key.starts_with(owner+"/");};
 std::uint64_t source_bytes=0;std::size_t source_count=0;
 for(auto it=s.inputs.begin();it!=s.inputs.end();){const auto &key=it->first;bool retire=removed_resources.contains(key)&&!protected_resources.contains(key);
  for(const auto &prefix:input_keys)retire|=key==prefix||key.starts_with(prefix+"/");
  if(!retire||it->second.retained()){++it;continue;}source_bytes+=it->second.bytes();s.used_bytes-=it->second.bytes();++source_count;it=s.inputs.erase(it);
 }
 for(auto &[key,stack]:s.stacks){if(std::none_of(stack_keys.begin(),stack_keys.end(),[&](const auto &prefix){return key==prefix||key.starts_with(prefix+":");}))continue;
  if(owned_node(stack.head_id)){stack.head=nullptr;stack.head_id.clear();}
  std::erase_if(stack.outputs,[&](const auto &item){return owned_node(item.first);});
  std::erase_if(stack.node_versions,[&](const auto &item){return owned_node(item.first);});
  std::erase_if(stack.queries,[&](const auto &item){return item.second.owner==previous.id||owned_node(item.second.root);});stack.queried_sources.erase(owner);
  std::set<std::string> live;for(const auto &[id,image]:stack.outputs)live.insert(id);stack.plan.retain(live);
 }
 std::set<std::tuple<std::uint64_t,std::uint32_t,int>> keep_pages;
 for(const auto &[key,entry]:s.inputs)if(entry.plane>=0)for(const auto &[page,version]:entry.pages)keep_pages.emplace(version,page,entry.plane);
 const auto keep_pixels=[&](const TilePaintPixels &pixels){for(const auto &[tile,data]:pixels.tiles())data.visit_page_versions(plane,[&](auto page,auto version){keep_pages.emplace(version,page,plane);});};
 const auto keep_effect=[&](const EffectPaint &paint){keep_pixels(paint.pixels);if(plane==2){keep_pixels(paint.ao);keep_pixels(paint.displacement);for(const auto &pixels:paint.users)keep_pixels(pixels);}};
 for(const auto &candidate:document.texture_sets()){
  for(const auto &layer:evaluation_layers(candidate)){keep_pixels(layer.pixels);if(plane==2){keep_pixels(layer.ao_pixels);keep_pixels(layer.displacement.pixels);for(const auto &user:layer.users)keep_pixels(user.pixels);if(layer.mask)keep_pixels(layer.mask->pixels);}
   for(const auto &effect:layer.effects)if(effect.kind==ContentEffectKind::Paint)keep_effect(effect.paint);
   if(layer.mask)for(const auto &effect:mask_effects_recursive(layer.mask->effects))if(effect.kind==MaskEffectKind::Paint)keep_effect(effect.paint);
  }
 }
 const auto retired_pages=s.authoring_pages.retire_unused(plane,keep_pages);
 s.comparison_memo.clear();s.anchor_memo.clear();
 const auto after=blender::gpu::host_texture_memory::live_bytes();std::size_t shared=0;for(const auto &key:removed_resources)shared+=protected_resources.contains(key);
 std::printf("BTS_CHANNEL_RELEASE channel=%d source_count=%zu source_bytes=%llu authoring_pages=%zu shared_preserved=%zu storage_before=%llu storage_after=%llu\n",channel,source_count,(unsigned long long)source_bytes,retired_pages,shared,(unsigned long long)before,(unsigned long long)after);
}
bool GpuLayerPreview::texture_resource_ready(const FillTexture &texture)const{
 const auto ready=[&](const FillTexture &source){const auto found=impl_->inputs.find(resource_key(source));return found!=impl_->inputs.end()&&found->second.resource&&found->second.image&&!found->second.rejected&&found->second.width==source.width&&found->second.height==source.height&&found->second.row==source.height;};
 if(texture.tiles){for(const auto &[tile,image]:*texture.tiles)if(!ready(*image))return false;return true;}return ready(texture);
}
GpuPreviewUpdate GpuLayerPreview::prepare_baked_resource(int tile,const MeshMapPlane &plane,NativeBakedAoLease &lease){
 const auto key=native_baked_ao_key(plane,tile);
 if(!GPU_context_active_get())throw std::logic_error("Native baked AO upload requires the GPU owner");
 auto &s=*impl_;s.input_rejected=false;++s.clock;const std::set<std::string> protected_keys{key};
 const bool ready=lease.state.prepare(plane,tile,[&]{
  auto *entry=s.entry(key,plane.resolution,plane.rows(),true,protected_keys);
  if(!entry||entry->rejected||!entry->image)native_baked_ao_reject(tile,"existing upload pool could not admit the baked allocation");
  lease.resource={entry->image.get(),entry->width,entry->height,key,entry->residency};
  return NativeBakedAoPin{key,entry->width,entry->height,entry->residency};
 },[&](const NativeBakedAoPin &pin){
  const auto found=s.inputs.find(pin.key);if(found==s.inputs.end()||found->second.residency!=pin.residency||found->second.image.get()!=lease.resource.texture)native_baked_ao_reject(tile,"pinned baked allocation is no longer owned by the upload pool");
  auto *image=s.baked(&plane,protected_keys);if(s.input_rejected)native_baked_ao_reject(tile,"baked upload was rejected");return image!=nullptr;
 });
 const auto found=s.inputs.find(key);if(found==s.inputs.end()||found->second.residency!=lease.state.pin().residency||found->second.image.get()!=lease.resource.texture)native_baked_ao_reject(tile,"retained baked resource belongs to another upload pool");
 lease.validate(plane,tile,false);return ready?GpuPreviewUpdate::Updated:GpuPreviewUpdate::Pending;
}
std::optional<GpuTextureResource> GpuLayerPreview::retain_texture_resource(const FillTexture &source)const{
 if(source.tiles||!texture_resource_ready(source))return std::nullopt;
 const auto key=resource_key(source);const auto &entry=impl_->inputs.at(key);
 return GpuTextureResource{entry.image.get(),entry.width,entry.height,key,entry.residency};
}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resource(const FillTexture &texture){const std::array<const FillTexture *,1> group{&texture};return prepare_texture_resources(group);}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resources(std::span<const FillTexture *const> textures){
 auto &s=*impl_;s.input_rejected=false;++s.clock;std::set<std::string> protected_keys;
 for(const auto *texture:textures){if(!texture)throw std::invalid_argument("Empty GPU material input");if(texture->tiles)for(const auto &[tile,image]:*texture->tiles)protected_keys.insert(resource_key(*image));else protected_keys.insert(resource_key(*texture));}
 bool ready=true;const auto upload=[&](const FillTexture &image){if(!s.resource(image,protected_keys))ready=false;};for(const auto *texture:textures){if(texture->tiles)for(const auto &[tile,image]:*texture->tiles)upload(*image);else upload(*texture);}
 return s.input_rejected?GpuPreviewUpdate::Unsupported:ready?GpuPreviewUpdate::Updated:GpuPreviewUpdate::Pending;
}

bool GpuLayerPreview::metadata_pending()const{return impl_->ranges.pending()||impl_->histograms.pending()||impl_->material_samples.pending()||impl_->thumbnails.pending()||impl_->layer_capture.pending();}
std::optional<HistogramMetadata> GpuLayerPreview::histogram_result(const QUuid &id)const{return impl_->histograms.get(id.toString().toStdString());}
std::vector<GpuLayerPreview::DisplacementSummary> GpuLayerPreview::displacement_summaries(){impl_->histograms.poll();impl_->material_samples.poll();impl_->thumbnails.poll();impl_->layer_capture.poll();std::vector<DisplacementSummary> result;for(const auto &v:impl_->ranges.poll())result.push_back({v.uid,v.minimum,v.maximum,v.present,v.epoch});return result;}
bool GpuLayerPreview::wrote_image()const{return impl_->image_changed;}
GpuPreviewUpdate GpuLayerPreview::update(const TextureSet &set,int channel,gpu::Texture *target,int tile,std::array<int,3> offset){return update_tiles(set,channel,target,{{tile,offset}});}
GpuPreviewUpdate GpuLayerPreview::resolve_mask(const TextureSet &set,gpu::Texture *target,int tile,std::array<int,3> offset){
 PaintMetadataScope metadata;performance::Scope timing("gpu_mask_inspection");
 if(!supported(set,15)||!set.tiles.contains(tile))return GpuPreviewUpdate::Unsupported;
 auto &s=*impl_;s.input_rejected=false;const auto format=GPU_texture_format(target);const bool array=GPU_texture_is_array(target);const int w=set.width(tile),h=set.rows(tile);
 if(GPU_texture_dimensions(target)!=2||offset[0]<0||offset[1]<0||offset[2]<0||GPU_texture_width(target)<offset[0]+w||GPU_texture_height(target)<offset[1]+h||offset[2]>=std::max(1,GPU_texture_layer_count(target)))return GpuPreviewUpdate::Unsupported;
 if(format!=gpu::TextureFormat::SFLOAT_16_16_16_16&&format!=gpu::TextureFormat::SFLOAT_32_32_32_32)return GpuPreviewUpdate::Unsupported;
 if(const auto state=s.initialize(w,h,format,array);state!=GpuPreviewUpdate::Updated)return state;
 const auto &layers=evaluation_layers(set);const auto found=std::find_if(layers.begin(),layers.end(),[&](const auto &l){return l.id==set.selected_layer;});if(found==layers.end()||!found->mask)return GpuPreviewUpdate::Unsupported;
 const auto key=paint_key(set,*found,2,true,tile);auto *mask=mask_texture(set,found->id,tile);if(!mask)return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;
 auto &cache=s.mask_stacks.at(key);const auto target_key=std::pair{reinterpret_cast<std::uintptr_t>(target),offset};const auto identity="mask:"+key;
 const int columns=(w+255)/256;if(s.target_owners[target_key]!=identity)for(int y=0;y<h;y+=256)for(int x=0;x<w;x+=256)cache.output_dirty.insert((y/256)*columns+x/256);
 if(!cache.output_dirty.empty()){
  const auto shader_key=std::pair{format,array};if(!s.mask_output_shaders.contains(shader_key))s.mask_output_shaders.emplace(shader_key,Shader(compile_mask_output(format,array),GPU_shader_free));auto *shader=s.mask_output_shaders.at(shader_key).get();if(!shader)return GpuPreviewUpdate::Unsupported;
  GPU_shader_bind(shader);GPU_texture_bind(mask,0);GPU_texture_image_bind(target,0);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());
  for(int chunk:cache.output_dirty){const int x=chunk%columns*256,y=chunk/columns*256,origin[2]={x,y},size[2]={std::min(256,w-x),std::min(256,h-y)};GPU_shader_uniform_2iv(shader,"region_origin",origin);GPU_shader_uniform_2iv(shader,"region_size",size);GPU_compute_dispatch(shader,(size[0]+15)/16,(size[1]+15)/16,1);}
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(target);GPU_shader_unbind();s.image_changed=true;
  std::printf("BTS_GPU_MASK_INSPECTION tile=%d width=%d height=%d chunks=%zu cpu_composite=0\n",tile,w,h,cache.output_dirty.size());cache.output_dirty.clear();
 }
 s.target_owners[target_key]=identity;
 if(std::getenv("BTS_VERIFY_GPU_STACK")){
  std::vector<std::array<int,3>> positions;for(int y=0;y<8;++y)for(int x=0;x<8;++x)positions.push_back({offset[0]+(2*x+1)*w/16,offset[1]+(2*y+1)*h/16,offset[2]});
  const auto pixels=probe_gpu_texture(target,positions);double maximum=0;
  for(std::size_t i=0;i<pixels.size();++i){const float value=TextureDocument::mask_value(set,set.selected_layer,positions[i][0]-offset[0],positions[i][1]-offset[1],tile);for(int c=0;c<4;++c)maximum=std::max(maximum,double(std::abs(pixels[i][c]-(c==3?1:value))));}
  const bool valid=maximum<.001;std::printf("BTS_GPU_STACK_VERIFIED channel=15 probes=64 max_error=%.9g valid=%d\n",maximum,int(valid));if(!valid)throw std::runtime_error("GPU mask inspection differs from CPU oracle");
 }
 return GpuPreviewUpdate::Updated;
}

GpuPreviewUpdate GpuLayerPreview::update_tiles(const TextureSet &set,int channel,gpu::Texture *target,const std::map<int,std::array<int,3>> &regions){return update_tiles_impl(set,channel,target,regions,0);}
GpuPreviewUpdate GpuLayerPreview::update_tiles_impl(const TextureSet &set,int channel,gpu::Texture *target,const std::map<int,std::array<int,3>> &regions,int output_tile){
 impl_->image_changed=false;
 if(channel==15){bool pending=false;for(const auto &[tile,offset]:regions){const auto result=resolve_mask(set,target,tile,offset);if(result==GpuPreviewUpdate::Unsupported)return result;pending|=result==GpuPreviewUpdate::Pending;}return pending?GpuPreviewUpdate::Pending:GpuPreviewUpdate::Updated;}
 if((channel==3||channel==5)&&(regions.size()!=set.tiles.size()||std::any_of(set.tiles.begin(),set.tiles.end(),[&](int tile){return !regions.contains(tile);})))return GpuPreviewUpdate::Unsupported;
 if(channel==3)return resolve_normal(set,target,regions,output_tile);
 auto &s=*impl_;bool pending=false;
 for(const auto &[tile,offset]:regions){const auto result=update_tile(set,channel,target,tile,offset,channel==5);if(result==GpuPreviewUpdate::Unsupported)return result;pending|=result==GpuPreviewUpdate::Pending;}
 if(pending)return GpuPreviewUpdate::Pending;
 // GpuRangeSummary consumes a complete tile set and removes omitted sources.
 // A single-tile pixel update cannot claim new whole-material bounds, even if
 // older stacks happen to exist for the other UDIMs. The normal all-tile owner
 // submits one coherent complete range after all requested prefixes are ready.
 const bool complete_regions=regions.size()==set.tiles.size()&&std::all_of(set.tiles.begin(),set.tiles.end(),[&](int tile){return regions.contains(tile);});
 if(channel==16&&!testing_stack&&complete_regions){std::vector<GpuRangeSummary::Source> sources;for(int tile:set.tiles){const auto key=set.id.toString().toStdString()+":16:"+std::to_string(tile);const auto &stack=s.stacks.at(key);sources.push_back({tile,set.width(tile),set.rows(tile),stack.head,stack.revision,stack.last_changes});}s.ranges.update(set.material_session_uid,sources,s.displacement_epochs[set.material_session_uid]);}
 if(channel!=5)return GpuPreviewUpdate::Updated;
 performance::Scope timing("gpu_ao_resolve");std::map<int,gpu::Texture *> bakes;std::set<std::string> protected_keys;
 if(set.mesh_maps[3])for(const auto &[tile,plane]:set.mesh_maps[3]->tiles)if(plane.pixels)protected_keys.insert("baked:"+plane.pixels->hash+":"+std::to_string(plane.pixels->resolution)+":"+std::to_string(plane.pixels->height));
 for(const auto &[tile,offset]:regions){const MeshMapPlane *plane=nullptr;if(set.mesh_maps[3])if(auto i=set.mesh_maps[3]->tiles.find(tile);i!=set.mesh_maps[3]->tiles.end())plane=&i->second;bakes[tile]=s.baked(plane,protected_keys);if(!bakes[tile])return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
 auto dirty_update=s.derived_changes(set,5,target,regions,output_tile);if(!dirty_update.needed)return GpuPreviewUpdate::Updated;
 auto &derived=s.derived[s.derived_key(set,5)];if(!s.prepare_derived_masks(derived,dirty_update,set))return GpuPreviewUpdate::Unsupported;
 if(!s.presence_shader)s.presence_shader=Shader(gpu_derived::presence_shader(),GPU_shader_free);
 if(!derived.presence)derived.presence=texture("BTS cross-tile AO presence",1,1,gpu::TextureFormat::SFLOAT_32_32_32_32);
 if(!s.presence_shader||!derived.presence)return GpuPreviewUpdate::Unsupported;
 auto *reduce=s.presence_shader.get();GPU_shader_bind(reduce);GPU_texture_image_bind(derived.presence.get(),0);bool first=true;
 const auto stack_key=[&](int tile){return set.id.toString().toStdString()+":5:"+std::to_string(tile);};
 for(const auto &[tile,offset]:regions){const auto &stack=s.stacks.at(stack_key(tile));GPU_texture_bind(stack.coverage.get(),0);GPU_shader_uniform_1i(reduce,"first_tile",first);GPU_compute_dispatch(reduce,1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);first=false;}GPU_texture_image_unbind(derived.presence.get());
 const auto format=GPU_texture_format(target);const auto key=std::pair{format,GPU_texture_is_array(target)};if(!s.ao_shaders.contains(key))s.ao_shaders.emplace(key,Shader(gpu_derived::ao_shader(format,key.second,quant_source),GPU_shader_free));auto *shader=s.ao_shaders.at(key).get();if(!shader)return GpuPreviewUpdate::Unsupported;
 GPU_shader_bind(shader);GPU_texture_bind(derived.presence.get(),2);GPU_texture_image_bind(target,0);GPU_shader_uniform_1f(shader,"base_value",gpu_channel_base(set,5)[0]);GPU_shader_uniform_1i(shader,"authored_enabled",set.ao_channel);GPU_shader_uniform_1i(shader,"replace_mode",set.ao_mixing==AoMixing::Replace);GPU_shader_uniform_1i(shader,"value_precision",int(set.precision[5]));
 for(const auto &[tile,offset]:regions){if(output_tile&&tile!=output_tile)continue;auto &stack=s.stacks.at(stack_key(tile));GPU_texture_bind(stack.head,0);GPU_texture_bind(bakes.at(tile),1);GPU_texture_bind(derived.dirty_images.at(tile).get(),3);const int size[2]={set.width(tile),set.rows(tile)};GPU_shader_uniform_2iv(shader,"size",size);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());GPU_compute_dispatch(shader,(size[0]+15)/16,(size[1]+15)/16,1);}GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
 if(const auto found=s.margin_plans.find(set.material_session_uid);found!=s.margin_plans.end())for(const auto &[tile,offset]:regions){if(output_tile&&tile!=output_tile)continue;const auto &plan=(set.padding_3d?found->second.adjacent:found->second.extend).at(tile);if(!s.margins.apply(stack_key(tile)+":ao",plan,target,offset,dirty_update.flags.at(tile),false,derived.presence.get()))return GpuPreviewUpdate::Unsupported;}
 s.commit_derived(set,5,target,regions,std::move(dirty_update));
 if(!output_tile&&std::getenv("BTS_VERIFY_GPU_STACK")&&!s.margin_plans.contains(set.material_session_uid)){
  std::array<bool,6> wanted{};wanted[5]=true;bool authored=false;for(int tile:set.tiles){for(int y=0;y<set.rows(tile)&&!authored;++y)for(int x=0;x<set.width(tile)&&!authored;++x)authored=TextureDocument::evaluate_channels(set,wanted,x,y,tile).ao_coverage>0;}
  auto *pixels=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));double maximum=0;bool valid=true;
  for(const auto &[tile,offset]:regions)for(int y=0;y<8;++y)for(int x=0;x<8;++x){const int px=int((x+.5)*set.width(tile)/8),py=int((y+.5)*set.rows(tile)/8);const auto composite=TextureDocument::evaluate_channels(set,wanted,px,py,tile);const auto baked=sample_baked_ao(set,(px+.5)/set.width(tile),(py+.5)/set.rows(tile),tile);const float mixed=mix_ambient_occlusion(set,composite.material.ambient_occlusion,authored,baked);float expected=precision_rgb({mixed,mixed,mixed},set.precision[5])[0];if(format==gpu::TextureFormat::SFLOAT_16_16_16_16)expected=math::half_to_float(math::float_to_half_make_finite(expected));const auto index=((std::size_t(offset[2])*GPU_texture_height(target)+py+offset[1])*GPU_texture_width(target)+px+offset[0])*4;for(int c=0;c<4;++c){const double difference=std::abs(pixels[index+c]-(c==3?1:expected));maximum=std::max(maximum,difference);const double tolerance=set.precision[5]==ChannelPrecision::L8?1./255+.001:set.precision[5]==ChannelPrecision::L16?1./65535+.001:format==gpu::TextureFormat::SFLOAT_16_16_16_16?.001 : 1e-6;if(valid&&difference>tolerance)std::fprintf(stderr,"BTS_GPU_AO_DIFFERENCE tile=%d x=%d y=%d precision=%d mode=%d authored=%d coverage=%.9g material=%.9g bake=%.9g expected=%.9g actual=%.9g\n",tile,px,py,int(set.precision[5]),int(set.ao_mixing),int(authored),composite.ao_coverage,composite.material.ambient_occlusion,baked.value,expected,pixels[index+c]);valid&=std::isfinite(pixels[index+c])&&difference<=tolerance;}}
  MEM_delete(pixels);std::printf("BTS_GPU_STACK_VERIFIED channel=5 probes=64 max_error=%.9g valid=%d\n",maximum,int(valid));if(!valid)throw std::runtime_error("GPU AO differs from CPU cross-tile reference");
 }
 return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::resolve_normal(const TextureSet &source_set,gpu::Texture *target,const std::map<int,std::array<int,3>> &regions,int output_tile,bool without_height){
 auto set=native_normal_stack(source_set);
 if(!supported(set,3))return GpuPreviewUpdate::Unsupported;auto &s=*impl_;bool pending=false;
 for(const auto &[tile,offset]:regions){for(int c:{6,4}){if(c==4&&(!set.channels[4]||without_height))continue;const auto updated=update_tile(set,c,target,tile,offset,true);if(updated==GpuPreviewUpdate::Unsupported)return updated;pending|=updated==GpuPreviewUpdate::Pending;}}
 if(pending)return GpuPreviewUpdate::Pending;if(without_height)set.channels[4]=false;performance::Scope timing("gpu_normal_resolve");
 const auto stack_key=[&](int channel,int tile){return set.id.toString().toStdString()+":"+std::to_string(channel)+":"+std::to_string(tile);};
 std::map<int,gpu::Texture *> bakes;std::set<std::string> protected_keys;
 if(set.mesh_maps[0])for(const auto &[tile,plane]:set.mesh_maps[0]->tiles)if(plane.pixels)protected_keys.insert("baked:"+plane.pixels->hash+":"+std::to_string(plane.pixels->resolution)+":"+std::to_string(plane.pixels->height));
 for(const auto &[tile,offset]:regions){const MeshMapPlane *plane=nullptr;if(set.mesh_maps[0])if(auto i=set.mesh_maps[0]->tiles.find(tile);i!=set.mesh_maps[0]->tiles.end())plane=&i->second;bakes[tile]=s.baked(plane,protected_keys);if(!bakes[tile])return s.input_rejected?GpuPreviewUpdate::Unsupported:GpuPreviewUpdate::Pending;}
 auto dirty_update=s.derived_changes(set,3,target,regions,output_tile);if(!dirty_update.needed)return GpuPreviewUpdate::Updated;
 auto &derived=s.derived[s.derived_key(set,3)];if(!s.prepare_derived_masks(derived,dirty_update,set))return GpuPreviewUpdate::Unsupported;
 if(!s.presence_shader)s.presence_shader=Shader(gpu_derived::presence_shader(),GPU_shader_free);
 if(!derived.presence)derived.presence=texture("BTS cross-tile authored presence",1,1,gpu::TextureFormat::SFLOAT_32_32_32_32);
 if(!s.presence_shader||!derived.presence)return GpuPreviewUpdate::Unsupported;
 auto *reduce=s.presence_shader.get();GPU_shader_bind(reduce);GPU_texture_image_bind(derived.presence.get(),0);bool first=true;
 for(const auto &[tile,offset]:regions){GPU_texture_bind(s.stacks.at(stack_key(6,tile)).coverage.get(),0);GPU_shader_uniform_1i(reduce,"first_tile",first);GPU_compute_dispatch(reduce,1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);first=false;}GPU_texture_image_unbind(derived.presence.get());
 const auto format=GPU_texture_format(target);const auto key=std::pair{format,GPU_texture_is_array(target)};if(!s.normal_shaders.contains(key))s.normal_shaders.emplace(key,Shader(gpu_derived::normal_shader(format,key.second,quant_source),GPU_shader_free));auto *shader=s.normal_shaders.at(key).get();if(!shader)return GpuPreviewUpdate::Unsupported;
 GPU_shader_bind(shader);GPU_texture_bind(derived.presence.get(),2);GPU_texture_image_bind(target,0);
 const auto basis=set.channels[3]?set.base_values:MaterialValues{};const std::array<float,3> base{basis.normal[0]*.5f+.5f,basis.normal[1]*.5f+.5f,basis.normal[2]*.5f+.5f};const auto quantized=precision_channel(basis,3,set.precision[3]);
 GPU_shader_uniform_3fv(shader,"normal_base",base.data());GPU_shader_uniform_3fv(shader,"quantized_base",quantized.data());GPU_shader_uniform_1f(shader,"height_base",gpu_channel_base(set,4)[0]);GPU_shader_uniform_1i(shader,"height_precision",int(set.precision[4]));GPU_shader_uniform_1i(shader,"height_enabled",set.channels[4]);GPU_shader_uniform_1i(shader,"authored_enabled",set.channels[3]);GPU_shader_uniform_1i(shader,"directx",set.normal_format==NormalMapFormat::DirectX);GPU_shader_uniform_1i(shader,"replace_mode",set.normal_mixing==NormalMixing::Replace);GPU_shader_uniform_1i(shader,"sobel",set.bump_method==BumpMethod::SmoothSobel);GPU_shader_uniform_1i(shader,"value_precision",int(set.precision[3]));
 for(const auto &[tile,offset]:regions){
  if(output_tile&&tile!=output_tile)continue;
  std::array<std::array<float,4>,9> meta{};const int col=(tile-1001)%10,row=(tile-1001)/10;
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int i=(dy+1)*3+dx+1,neighbor=1001+col+dx+10*(row+dy);const bool exists=col+dx>=0&&col+dx<10&&row+dy>=0&&row+dy<100&&set.tiles.contains(neighbor);const auto *height_stack=exists&&set.channels[4]?&s.stacks.at(stack_key(4,neighbor)):nullptr;const bool padded=height_stack&&bool(height_stack->height_image);meta[i]={float(exists?set.width(neighbor):1),float(exists?set.rows(neighbor):1),float(exists),float(padded)};GPU_texture_bind(height_stack?(padded?height_stack->height_image.get():height_stack->head):s.zero.get(),4+i);}
  auto &entry=s.normal_neighbors[stack_key(3,tile)];if(!entry.image)entry.image=texture("BTS normal UDIM neighbors",9,1,gpu::TextureFormat::SFLOAT_32_32_32_32);if(!entry.image)return GpuPreviewUpdate::Unsupported;if(entry.values!=meta){GPU_texture_update(entry.image.get(),GPU_DATA_FLOAT,meta.data());entry.values=meta;}
  GPU_texture_bind(entry.image.get(),3);GPU_texture_bind(s.stacks.at(stack_key(6,tile)).head,0);GPU_texture_bind(bakes.at(tile),1);GPU_texture_bind(derived.dirty_images.at(tile).get(),13);const int size[2]={set.width(tile),set.rows(tile)};GPU_shader_uniform_2iv(shader,"size",size);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());GPU_compute_dispatch(shader,(size[0]+15)/16,(size[1]+15)/16,1);
 }
 GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
 if(const auto found=s.margin_plans.find(set.material_session_uid);found!=s.margin_plans.end())for(const auto &[tile,offset]:regions){if(output_tile&&tile!=output_tile)continue;const auto &plan=found->second.extend.at(tile);if(!s.margins.apply(stack_key(3,tile)+":normal",plan,target,offset,dirty_update.flags.at(tile),false,derived.presence.get()))return GpuPreviewUpdate::Unsupported;}
 s.commit_derived(set,3,target,regions,std::move(dirty_update));
 if(!output_tile&&std::getenv("BTS_VERIFY_GPU_STACK")&&testing_stack&&!s.margin_plans.contains(set.material_session_uid)){
  CompositeChannels channels{};channels[3]=true;std::atomic_bool cancel=false;const auto reference=native_composite_job(set,false,nullptr,1,cancel,false,channels);if(!reference)throw std::runtime_error("Normal CPU oracle cancelled");auto *pixels=static_cast<float *>(GPU_texture_read(target,GPU_DATA_FLOAT,0));double maximum=0;bool valid=true;
  for(const auto &[tile,offset]:regions)for(int y=0;y<set.rows(tile);++y)for(int x=0;x<set.width(tile);++x){const auto index=((std::size_t(offset[2])*GPU_texture_height(target)+y+offset[1])*GPU_texture_width(target)+x+offset[0])*4;const auto local=(std::size_t(y)*set.width(tile)+x)*4;for(int c=0;c<4;++c){float expected=reference->tiles.at(tile)[3][local+c];if(format==gpu::TextureFormat::SFLOAT_16_16_16_16)expected=math::half_to_float(math::float_to_half_make_finite(expected));const double difference=std::abs(pixels[index+c]-expected),tolerance=(format==gpu::TextureFormat::SFLOAT_16_16_16_16||set.precision[3]==ChannelPrecision::RGB16F)?.001:2e-5;if(valid&&difference>tolerance)std::fprintf(stderr,"BTS_GPU_NORMAL_DIFFERENCE tile=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",tile,x,y,c,expected,pixels[index+c]);maximum=std::max(maximum,difference);valid&=std::isfinite(pixels[index+c])&&difference<=tolerance;}}
  MEM_delete(pixels);std::printf("BTS_GPU_STACK_VERIFIED channel=3 probes=64 max_error=%.9g valid=%d\n",maximum,int(valid));if(!valid)throw std::runtime_error("GPU normal differs from CPU derived reference");
 }
 return GpuPreviewUpdate::Updated;
}
void GpuLayerPreview::self_test(){
 // Owner-context integration oracle: actual compute output, no mock GPU. No readback in production.
 testing_stack=true;
#include "GpuFillCoverageTests.inc"
 if(qEnvironmentVariableIsSet("BTS_GPU_FILL_COVERAGE_TEST")){testing_stack=false;return;}
#include "GpuFolderCompositionTests.inc"
#include "GpuSmartMaskTests.inc"
 GpuLayerPreview preview;auto target=texture("BTS GPU test",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 TextureSet set;set.texture_resolution=128;set.layers.front().kind=LayerKind::Fill;set.layers.front().values.base_color={.13f,.42f,.71f,1};set.layers.front().values.roughness=.34;set.layers.front().values.metallic=.27;
 TextureDocument::add_fill(set);auto &top=set.layers.back();top.values.base_color={.67f,.21f,.39f,1};top.opacity.fill(.63f);top.mask=LayerMask{};top.mask->background=.37;
 PaintPixels::Stamp ink{{.53,.47},{.43,0},{0,.34}};ink.values.base_color={.79f,.31f,.52f,1};ink.values.metallic=.8;ink.values.roughness=.61;ink.flow=.73;ink.hardness=.34;
 auto mask=ink;mask.channels={false,false,true,false,false};mask.values.roughness=.86;top.mask->pixels.stamp(128,mask);
 auto resource=std::make_shared<FillTexture>();resource->width=4;resource->height=4;resource->hash="self-test";auto texels=std::make_shared<std::vector<FillTexture::Pixel>>();for(int y=0;y<4;++y)for(int x=0;x<4;++x)texels->push_back({.1f+x*.24f,.07f+y*.27f,.4f,(x+y)%2?.31f:.89f});resource->pixels=texels;
 int cases=verify_gpu_texture_probe()+verify_gpu_brush_raster();
#include "GpuPolygonRasterTests.inc"
 auto check=[&](int channel){preview.begin_frame();if(preview.update(set,channel,target.get())!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU stack oracle did not execute");++cases;};
 for(int channel=0;channel<3;++channel)for(int mode=0;mode<=28;++mode){if(mode==4)continue;top.blending[channel]=LayerBlend(mode);check(channel);}
 for(int channel=0;channel<3;++channel){top.blending[channel]=LayerBlend::Normal;top.fill.texture.reset();top.fill.channel_textures={};fill_channel_texture(top.fill,channel)=resource;top.fill.scale={2.3,.7};top.fill.offset={.17,-.11};top.fill.rotation=37;check(channel);}
 // Same source bytes, different per-usage OCIO interpretations must occupy distinct GPU slots.
 auto alternate=std::make_shared<FillTexture>(*resource);alternate->source_space="ACEScg";auto converted=std::make_shared<std::vector<FillTexture::Pixel>>(*texels);for(auto &pixel:*converted)for(int c=0;c<3;++c)pixel[c]=pixel[c]*.3f+.1f;alternate->pixels=converted;top.fill.texture=resource;top.fill.channel_textures={};check(0);top.fill.texture=alternate;check(0);top.fill.texture=resource;check(0);
 // Distinct asset identities and sequence wrappers share one physical GPU source.
 const auto allocations=preview.impl_->resource_allocations;
 for(int use=0;use<3;++use){auto shared=std::make_shared<FillTexture>(*resource);shared->asset=QUuid::createUuid().toString().toStdString();shared->name="shared "+std::to_string(use);auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();members->emplace(1001,resource);shared->tiles=members;shared->hash="sequence wrapper "+std::to_string(use);top.fill.texture=shared;check(0);}
 if(preview.impl_->resource_allocations!=allocations)throw std::runtime_error("Shared texture allocated duplicate GPU storage");
 std::printf("BTS_GPU_RESOURCE_SHARED usages=3 extra_allocations=0\n");
 // Optional real encoded 4K resource test; ordinary GPU regression stays small.
 if(const char *path=std::getenv("BTS_VERIFY_SHARED_TEXTURE")){
  const auto source=reference_image_resource(QString::fromUtf8(path));StudioAsset asset;asset.id=QUuid::createUuid();asset.name=source.name;asset.filename=source.filename;asset.path=path;asset.scope=AssetScope::Project;asset.usages={"texture"};asset.texture_files=source.files;
  const auto decoded=decode_fill_texture(asset);if(!decoded->tiles||decoded->tiles->size()!=2)throw std::runtime_error("Shared 4K fixture needs two tiles");
  TextureSet shared_set;shared_set.texture_resolution=128;shared_set.tiles={1001,1002};shared_set.layers.front().kind=LayerKind::Fill;shared_set.layers.front().fill.mode=6;shared_set.layers.front().fill.texture=decoded;
  GpuLayerPreview shared_preview;const auto render=[&]{for(int tile:shared_set.tiles){const auto started=performance::Clock::now();for(;;){shared_preview.begin_frame();const auto result=shared_preview.update(shared_set,0,target.get(),tile);if(result==GpuPreviewUpdate::Updated)break;if(result==GpuPreviewUpdate::Unsupported||performance::ms(started)>10000)throw std::runtime_error("Shared 4K source failed to upload");}++cases;}};
  render();const auto once=shared_preview.impl_->resource_allocations;if(once!=2)throw std::runtime_error("Expected one GPU source allocation per 4K UDIM tile");
  for(int use=1;use<3;++use){auto alias=asset;alias.id=QUuid::createUuid();alias.name="Alias "+QString::number(use);const auto reused=decode_fill_texture(alias);for(int tile:shared_set.tiles)if(reused->tiles->at(tile)->pixels.get()!=decoded->tiles->at(tile)->pixels.get())throw std::runtime_error("4K alias copied RAM storage");TextureDocument::add_fill(shared_set);auto &layer=shared_set.layers.back();layer.fill.mode=6;layer.fill.texture=reused;layer.opacity[0]=.37;render();}
  if(shared_preview.impl_->resource_allocations!=once)throw std::runtime_error("4K aliases duplicated GPU storage");
  std::uint64_t resource_bytes=0;int resources=0;for(const auto &[key,entry]:shared_preview.impl_->inputs)if(entry.resource){if(entry.width!=4096||entry.height!=4096)throw std::runtime_error("4K source was downscaled");resource_bytes+=entry.bytes();++resources;}if(resources!=2)throw std::runtime_error("4K shared storage lost a tile");
  std::printf("BTS_GPU_RESOURCE_SHARED_4K usages=3 tiles=2 width=4096 height=4096 allocations=%llu bytes=%llu ram_shared=1\n",(unsigned long long)once,(unsigned long long)resource_bytes);
 }

 // AB: alpha is a per-use sampling policy, never a separate RAM/GPU image.
 {TextureSet data_set;data_set.texture_resolution=128;data_set.tiles={1001,1002,1011};data_set.precision[4]=ChannelPrecision::L32F;TextureDocument::add_displacement_channel(data_set);TextureDocument::add_fill(data_set);auto &layer=data_set.layers.back();layer.fill.mode=6;layer.displacement.enabled=true;
  auto sequence=std::make_shared<FillTexture>();sequence->raw=true;sequence->width=sequence->height=4;sequence->hash="AB scalar sequence";auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(int tile:data_set.tiles){auto image=std::make_shared<FillTexture>(*sequence);image->hash="AB raw tile "+std::to_string(tile);const float height=tile==1001?-.001f:tile==1002?.002f:2.25f;image->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(16,FillTexture::Pixel{height,height,height,0});members->emplace(tile,image);}sequence->pixels=members->begin()->second->pixels;sequence->tiles=members;fill_channel_texture(layer.fill,14)=sequence;fill_channel_texture(layer.fill,4)=sequence;
  GpuLayerPreview data_preview;auto output=texture("AB signed data",128,128,gpu::TextureFormat::SFLOAT_32_32_32_32);const int start=cases;
  for(bool alpha:{false,true,false})for(int filtering:{0,1,2})for(int channel:{4,16})for(int tile:data_set.tiles){const int source=channel==16?14:4;layer.fill.alpha_mask[source]=alpha;layer.fill.filtering=filtering;data_preview.begin_frame();if(data_preview.update(data_set,channel,output.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("AB scalar GPU source deferred");auto *pixels=static_cast<float *>(GPU_texture_read(output.get(),GPU_DATA_FLOAT,0));const auto index=(32*128+32)*4;const float expected=tile==1001?-.001f:tile==1002?.002f:2.25f;const bool valid=std::abs(pixels[index]-(alpha?0:expected))<1e-6&&pixels[index+3]==(alpha?0:1);MEM_delete(pixels);if(!valid)throw std::runtime_error("AB GPU signed zero-alpha data differs");++cases;}
  if(data_preview.impl_->resource_allocations!=3)throw std::runtime_error("Alpha/channel policy duplicated GPU resources");std::printf("BTS_GPU_AB_ALPHA_VERIFIED cases=%d tiles=3 source_allocations=3 alpha_opt_in=1 signed_hdr=1 float32=1\n",cases-start);
 }
 // Zero free VRAM is a valid pressure report, not an unavailable query.
 {const auto old_valid=texture_vram_query_valid.exchange(true);const auto old_free=texture_free_vram.exchange(0);const auto limit=source_cache_budget(0);texture_free_vram=old_free;texture_vram_query_valid=old_valid;if(limit!=0)throw std::runtime_error("Known exhausted VRAM fell back to a RAM-derived cache budget");++cases;std::puts("BTS_GPU_ZERO_FREE_VRAM_VERIFIED inactive_cache_budget=0 fallback=0");}
 // Preparing a resource spans bounded owner frames and never publishes a
 // partially uploaded image. The subsequent graph reuses those exact objects.
 {auto source=std::make_shared<FillTexture>();source->raw=true;source->width=source->height=1024;source->hash="AB staged sequence";auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(int tile:{1001,1002,1011}){auto member=std::make_shared<FillTexture>(*source);member->hash="AB staged tile "+std::to_string(tile);member->pixels=std::make_shared<const std::vector<FillTexture::Pixel>>(1024*1024,FillTexture::Pixel{.001f,.001f,.001f,0});members->emplace(tile,member);}source->tiles=members;source->pixels=members->begin()->second->pixels;GpuLayerPreview staged;
  const auto free_before=texture_free_vram.exchange(2ULL*1024*1024);++texture_vram_query_generation;texture_free_vram=free_before;++texture_vram_query_generation;
  if(staged.texture_resource_ready(*source))throw std::runtime_error("Unallocated texture reported ready");staged.begin_frame();if(staged.prepare_texture_resource(*source)!=GpuPreviewUpdate::Pending||staged.texture_resource_ready(*source))throw std::runtime_error("Partial resource reported ready");++cases;
  staged.begin_frame();if(staged.prepare_texture_resource(*source)!=GpuPreviewUpdate::Updated||!staged.texture_resource_ready(*source))throw std::runtime_error("Prepared resource did not become ready");++cases;const auto allocations=staged.impl_->resource_allocations;
  TextureSet model;model.texture_resolution=128;model.tiles={1001,1002,1011};TextureDocument::add_displacement_channel(model);TextureDocument::add_fill(model);auto &fill=model.layers.back();fill.fill.mode=6;fill.displacement.enabled=true;fill_channel_texture(fill.fill,14)=source;auto image=texture("AB prepared output",128,128,gpu::TextureFormat::SFLOAT_32_32_32_32);for(int tile:model.tiles){staged.begin_frame();if(staged.update(model,16,image.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Prepared graph needed another source upload");++cases;}if(allocations!=3||staged.impl_->resource_allocations!=allocations)throw std::runtime_error("Prepared graph duplicated source storage");std::puts("BTS_GPU_TEXTURE_PREPARE_VERIFIED tiles=3 frames=2 bytes=50331648 no_partial_publish=1 reused_allocations=3");
 }
 top.fill=FillProjection{};top.kind=LayerKind::Paint;top.pixels.stamp(128,ink);for(int channel=0;channel<3;++channel){top.blending[channel]=LayerBlend::Multiply;check(channel);}auto before=top.pixels;ink.values.base_color={.15f,.91f,.23f,1};top.pixels.stamp(128,ink);check(0);top.pixels=before;check(0);
 top.pixels={};top.opacity[0]=0;top.blending[0]=LayerBlend::Replace;check(0);
 top.opacity[0]=.63;top.blending[0]=LayerBlend::Normal;top.pixels.stamp(128,ink);for(int precision=0;precision<9;++precision){set.precision[0]=ChannelPrecision(precision);set.id=QUuid::createUuid();check(0);}
 // Persistent prefix and isolated folder caches, measured with real GPU work.
 set=TextureSet{};set.texture_resolution=512;set.layers.clear();
 TextureLayer under;under.kind=LayerKind::Fill;under.values.base_color={.2f,.5f,.7f,1};set.layers.push_back(under);
 TextureLayer folder;folder.kind=LayerKind::Folder;folder.opacity[0]=.7f;folder.blending[0]=LayerBlend::Passthrough;
 TextureLayer child;child.parent=folder.id;child.depth=1;auto dab=ink;dab.center={.2,.2};dab.axis_x={.015,0};dab.axis_y={0,.015};child.pixels.stamp(512,dab);set.layers.push_back(child);set.layers.push_back(folder);
 TextureLayer above;above.kind=LayerKind::Fill;above.opacity[0]=.3f;above.values.base_color={.9f,.2f,.1f,1};set.layers.push_back(above);
 target=texture("BTS GPU cached test",512,512,gpu::TextureFormat::SFLOAT_16_16_16_16);check(0);const auto full_work=preview.impl_->last_dispatch_texels;
 auto saved=set.layers[1].pixels;dab.center={.3,.2};set.layers[1].pixels.stamp(512,dab);check(0);
 const auto partial_work=preview.impl_->last_dispatch_texels;if(preview.impl_->last_cached_nodes<1||partial_work>=full_work)throw std::runtime_error("GPU dirty edit did not reuse lower prefix");
 set.layers[1].pixels=saved;check(0);set.layers[2].opacity[0]=.4f;check(0);if(preview.impl_->last_cached_nodes<2)throw std::runtime_error("Folder opacity recomputed cached children");
 set.layers[2].visible=false;check(0);set.layers[2].visible=true;check(0);
 std::printf("BTS_GPU_PREFIX_VERIFIED full_texels=%llu dirty_texels=%llu cached_under_and_folder=1\n",(unsigned long long)full_work,(unsigned long long)partial_work);
 // Opaque uniform fills disconnect hidden paint. Partial opacity reconnects it;
 // uniform-only stacks and folder results remain single GPU texels.
 set=TextureSet{};set.texture_resolution=512;set.layers.front().pixels.stamp(512,dab);
 for(int i=0;i<12;++i){TextureDocument::add_fill(set);set.layers.back().values.base_color={.03f*i,.2f,.7f,1};}
 check(0);
 const auto prefix_shape=[&](){std::size_t count=0,texels=0;for(const auto &[key,entry]:preview.impl_->stacks)if(key.starts_with(set.id.toString().toStdString()))for(const auto &[id,image]:entry.outputs){++count;texels+=GPU_texture_width(image.get())*GPU_texture_height(image.get());}return std::pair{count,texels};};
 auto shape=prefix_shape();if(shape.first!=1||shape.second!=1)throw std::runtime_error("Opaque constant prefix retained hidden full-resolution layers");
 set.layers.back().opacity[0]=.4f;check(0);shape=prefix_shape();if(shape.first!=2||shape.second!=2)throw std::runtime_error("Constant prefix used full-resolution storage");
 for(std::size_t i=1;i<set.layers.size();++i)set.layers[i].opacity[0]=.3f;check(0);
 shape=prefix_shape();if(shape.second<=shape.first)throw std::runtime_error("Transparent stack did not reconnect spatial paint");
 set.layers.back().blending[0]=LayerBlend::Replace;check(0);shape=prefix_shape();if(preview.impl_->last_active_nodes!=1)throw std::runtime_error("Replace did not disconnect hidden spatial paint");
 set.layers.back().mask=LayerMask{};set.layers.back().mask->background=.3;check(0);
 set.layers.back().mask->enabled=false;check(0);shape=prefix_shape();if(preview.impl_->last_active_nodes!=1)throw std::runtime_error("Disabled mask prevented uniform prefix");
 std::printf("BTS_GPU_CONSTANT_PREFIX_VERIFIED opaque_pruned=1 translucent_reconnect=1 replace=1 mask=1 uniform_texels=1 layers=13\n");
 // Cached prefixes survive opaque occlusion, but hidden edits and memory
 // eviction still invalidate them before they can contribute again.
 set=TextureSet{};set.texture_resolution=512;set.layers.front().pixels.stamp(512,dab);TextureDocument::add_fill(set);set.layers.back().opacity[0]=.4f;check(0);const auto warm_work=preview.impl_->last_dispatch_texels;
 set.layers.back().opacity[0]=1;check(0);if(preview.impl_->last_active_nodes!=1)throw std::runtime_error("Opaque cache warming evaluated hidden paint");
 set.layers.back().opacity[0]=.4f;check(0);const auto reconnect_work=preview.impl_->last_dispatch_texels;if(preview.impl_->last_cached_nodes<1||reconnect_work>=warm_work)throw std::runtime_error("Opacity reconnect discarded a valid lower prefix");
 set.layers.back().opacity[0]=1;check(0);dab.center={.12,.12};set.layers.front().pixels.stamp(512,dab);check(0);if(preview.impl_->last_active_nodes!=1)throw std::runtime_error("Hidden source edit evaluated occluded work");set.layers.back().opacity[0]=.4f;check(0);if(preview.impl_->last_dispatch_texels<=reconnect_work)throw std::runtime_error("Hidden source edit reused stale lower pixels");
 const auto prior_free=texture_free_vram.exchange(2ULL*1024*1024);set.layers.back().opacity[0]=1;check(0);texture_free_vram=prior_free;set.layers.back().opacity[0]=.4f;check(0);if(preview.impl_->last_dispatch_texels< warm_work)throw std::runtime_error("Evicted lower cache kept valid plan metadata");
 std::printf("BTS_GPU_OCCLUDED_PREFIX_VERIFIED warm_texels=%llu reconnect_texels=%llu hidden_dirty=1 pressure_evict=1 no_hidden_dispatch=1\n",(unsigned long long)warm_work,(unsigned long long)reconnect_work);
 // Thumbnail/source staging must not turn a uniform opaque fill into a
 // full-resolution prefix that depends on hidden paint.
 set=TextureSet{};set.texture_resolution=512;set.layers.front().pixels.stamp(512,dab);TextureDocument::add_fill(set);check(0);
 for(auto &[key,entry]:preview.impl_->stacks)if(key.starts_with(set.id.toString().toStdString()))entry.queried_sources.insert(set.layers.back().id.toString().toStdString());
 check(0);if(preview.impl_->last_active_nodes!=2)throw std::runtime_error("Uniform source query reconnected hidden paint");
 for(const auto &[key,entry]:preview.impl_->stacks)if(key.starts_with(set.id.toString().toStdString()))for(const auto &id:{set.layers.back().id.toString().toStdString(),set.layers.back().id.toString().toStdString()+"/source"})if(GPU_texture_width(entry.outputs.at(id).get())!=1||GPU_texture_height(entry.outputs.at(id).get())!=1)throw std::runtime_error("Uniform queried fill lost constant storage");
 set.layers.back().opacity[0]=.4f;check(0);set.layers.back().opacity[0]=1;check(0);if(preview.impl_->last_active_nodes!=2)throw std::runtime_error("Restored queried fill lost opaque pruning");
 std::puts("BTS_GPU_QUERIED_CONSTANT_VERIFIED active_nodes=2 uniform_storage=1 opaque_pruned=1 reconnect_exact=1");
 // Geometry masks evaluate raw ownership IDs on GPU, sharing the atlas across
 // layers/channels. Unknown IDs and uncovered UV texels always exclude.
 set=TextureSet{};set.texture_resolution=128;set.geometry_names={{11,"left"},{42,"right"}};set.layers.front().kind=LayerKind::Fill;set.layers.front().values.base_color={.3f,.2f,.7f,1};TextureDocument::add_fill(set);set.layers.back().values.base_color={.8f,.4f,.1f,1};
 auto ownership=std::make_shared<GeometryOwnershipTile>();ownership->width=ownership->height=128;ownership->owners.resize(128*128);
 for(int y=0;y<128;++y)for(int x=0;x<128;++x)ownership->owners[y*128+x]=x<40?11:x<80?42:x<96?0:99;
 set.geometry_ownership[1001]=ownership;target=texture("BTS geometry mask oracle",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 const auto geometry_allocations=preview.impl_->geometry_masks.atlas_count();
 for(bool default_include:{false,true})for(bool left:{false,true})for(bool right:{false,true}){
  auto &mask=set.layers.back().geometry;mask={};mask.default_include=default_include;mask.set("left",left);mask.set("right",right);for(int c:{0,1,2,4})check(c);
 }
 if(preview.impl_->geometry_masks.atlas_count()!=geometry_allocations+1)throw std::runtime_error("Geometry ownership was duplicated per channel");
 auto &selection=set.layers.back().geometry;selection={};selection.set("left",false);check(0);
 auto changed=std::make_shared<GeometryOwnershipTile>(*ownership);std::swap_ranges(changed->owners.begin(),changed->owners.begin()+4096,changed->owners.begin()+8192);std::fill(changed->owners.begin(),changed->owners.begin()+4096,42);set.geometry_ownership[1001]=changed;check(0);
 selection.type=1;selection.set_tile(1001,false);check(0);selection.set_tile(1001,true);check(0);
 std::printf("BTS_GPU_GEOMETRY_MASK_VERIFIED channels=4 shared_atlas=1 known_unknown_uncovered=1 selection_rebind=1 udim_selection=1\n");
 // Actual array image with two tiles sharing an atlas layer and a third row.
 set=TextureSet{};set.texture_resolution=128;set.tiles={1001,1002,1011};set.layers.front().kind=LayerKind::Fill;set.layers.front().values.base_color={1,1,1,1};set.layers.front().fill.mode=6;
 auto sequence=std::make_shared<FillTexture>();auto members=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();int member_index=0;
 for(int tile:set.tiles){auto member=std::make_shared<FillTexture>();member->width=member->height=4;member->hash="array-oracle-"+std::to_string(tile);auto values=std::make_shared<std::vector<FillTexture::Pixel>>(16);for(int i=0;i<16;++i)(*values)[i]={.12f+member_index*.2f+(i%4)*.03f,.3f+(i/4)*.1f,.8f-member_index*.1f,.4f+.1f*(i%4)};member->pixels=values;members->emplace(tile,member);++member_index;}
 *sequence=*members->begin()->second;sequence->hash="array-oracle-sequence";sequence->tiles=members;set.layers.front().fill.texture=sequence;
 Texture array{GPU_texture_create_2d_array("BTS native UDIM packing test",256,128,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};const float sentinel[4]={-.25f,-.25f,-.25f,-.25f};GPU_texture_clear(array.get(),GPU_DATA_FLOAT,sentinel);
 const std::map<int,std::array<int,3>> regions{{1001,{0,0,0}},{1002,{128,0,0}},{1011,{0,0,1}}};
 for(int filtering:{0,2}){set.layers.front().fill.filtering=filtering;for(const auto &[tile,offset]:regions){preview.begin_frame();if(preview.update(set,0,array.get(),tile,offset)!=GpuPreviewUpdate::Updated)throw std::runtime_error("UDIM GPU dispatch failed");++cases;}}
 auto *array_pixels=static_cast<float *>(GPU_texture_read(array.get(),GPU_DATA_FLOAT,0));for(int y=0;y<128;++y)for(int x=128;x<256;++x)for(int k=0;k<4;++k)if(array_pixels[((std::size_t(128)+y)*256+x)*4+k]!=sentinel[k])throw std::runtime_error("UDIM dispatch wrote outside its atlas region");MEM_delete(array_pixels);
 std::printf("BTS_GPU_UDIM_VERIFIED tiles=1001,1002,1011 shared_array_layer=1 untouched_region=1 filtering=bilinear,nearest\n");
 // Non-square repeating textures sample -1 at the wrap seam. Cover both
 // axes and a real-world triplanar position near an integer UV boundary.
 {
  TextureSet wrapped;wrapped.texture_resolution=128;wrapped.precision.fill(ChannelPrecision::RGB32F);wrapped.layers.front().kind=LayerKind::Fill;
  auto edge=std::make_shared<FillTexture>();edge->width=200;edge->height=203;edge->raw=true;edge->hash="non-square-wrap-seam";auto data=std::make_shared<std::vector<FillTexture::Pixel>>(200*203);
  for(int y=0;y<203;++y)for(int x=0;x<200;++x){const float v=(x==199||y==202)?1:.1f;(*data)[y*200+x]={v,v,v,1};}edge->pixels=data;
  auto &layer=wrapped.layers.front();layer.fill.texture=edge;fill_channel_texture(layer.fill,2)=edge;layer.fill.offset={.0625,.0625};
  auto geometry=std::make_shared<FillSurfaceAtlas>();geometry->resolution=geometry->height=128;geometry->extent=.90712344646453857;FillSurfacePoint point;point.position={-.0683562309f,-.183462828f,.589798927f};point.normal={-.386040688f,-.871991932f,.3010028f};point.footprint=.000332799507f;point.valid=true;geometry->tiles[1001].assign(128*128,point);wrapped.projection_geometry=geometry;
  GpuLayerPreview seam_preview;auto image=texture("BTS wrap seam regression",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
  for(int mode:{0,1}){layer.fill.mode=mode;layer.fill.scale=mode==1?std::array<double,2>{10,10}:std::array<double,2>{1,1};for(int channel:{0,2}){seam_preview.begin_frame();if(seam_preview.update(wrapped,channel,image.get())!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU wrap seam did not execute");++cases;}}
  std::printf("BTS_GPU_WRAP_SEAM_VERIFIED non_square=200x203 uv_triplanar=1 negative_neighbor=1\n");
 }
 // Signed bump/displacement and every custom scalar use float32 without a 0..1 clamp.
 set=TextureSet{};set.texture_resolution=128;set.precision[4]=ChannelPrecision::L32F;set.displacement.allocated=set.displacement.channel=true;set.displacement.precision=ChannelPrecision::L32F;for(auto &user:set.users){user.present=user.allocated=true;user.precision=ChannelPrecision::L32F;}
 set.layers.front().kind=LayerKind::Fill;set.layers.front().values.bump=-.75f;set.layers.front().displacement.enabled=true;set.layers.front().displacement.value=-.75f;for(auto &user:set.layers.front().users)user.value=-.75f;
 TextureLayer signed_paint;signed_paint.opacity.fill(.7f);signed_paint.mask=LayerMask{};signed_paint.mask->background=.6f;signed_paint.displacement.enabled=true;signed_paint.displacement.opacity=.7f;for(auto &user:signed_paint.users)user.opacity=.7f;
 auto signed_dab=ink;signed_dab.channels={false,false,true,false,true};signed_dab.values.roughness=2.5f;signed_dab.values.bump=2.5f;signed_paint.pixels.stamp(128,signed_dab);signed_paint.displacement.pixels.stamp(128,signed_dab);for(auto &user:signed_paint.users)user.pixels.stamp(128,signed_dab);set.layers.push_back(signed_paint);
 target=texture("BTS signed scalar test",128,128,gpu::TextureFormat::SFLOAT_32_32_32_32);
 for(int c:{4,7,8,9,10,11,12,13,14,16})check(c);
 auto signed_texture=std::make_shared<FillTexture>();signed_texture->raw=true;signed_texture->input_space="Non-Color/raw";signed_texture->width=signed_texture->height=4;signed_texture->hash="signed-scalar-test";auto signed_values=std::make_shared<std::vector<FillTexture::Pixel>>(16);for(int i=0;i<16;++i){const float value=i%2?2.5f:-.75f;(*signed_values)[i]={value,value,value,1};}signed_texture->pixels=signed_values;
 for(int c:{4,7,8,9,10,11,12,13,14,16}){const auto data=gpu_layer_channel(set.layers.front(),c);fill_channel_texture(set.layers.front().fill,data.fill_channel)=signed_texture;check(c);}
 std::printf("BTS_GPU_SCALARS_VERIFIED channels=bump,user0..7,displacement signed_range=-0.75..2.5 float32=1 paint_fill_mask=1\n");
 // Immutable geometry is shared by every channel; projection math stays on GPU.
 set=TextureSet{};set.texture_resolution=128;set.layers.front().kind=LayerKind::Fill;
 auto atlas=std::make_shared<FillSurfaceAtlas>();atlas->resolution=128;atlas->height=128;atlas->extent=2.3;auto &surface_points=atlas->tiles[1001];surface_points.resize(128*128);
 for(int y=0;y<128;++y)for(int x=0;x<128;++x){auto &point=surface_points[y*128+x];point.position={float(x)/128-0.5f,float(y)/128-.5f,float(x+y)/256-.5f};point.normal={.26726124f,.53452248f,.80178373f};point.footprint=.019;point.valid=x>12&&y>9;}
 set.projection_geometry=atlas;auto &projected=set.layers.front();projected.fill.mode=1;projected.values.base_color={.4,.7,.9,1};projected.values.bump=.01;projected.fill.texture=resource;fill_channel_texture(projected.fill,1)=resource;fill_channel_texture(projected.fill,2)=resource;target=texture("BTS triplanar reference",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(int variant=0;variant<4;++variant){projected.fill.hardness=variant/3.;projected.fill.rotation3={17.*variant,-11.*variant,23.*variant};projected.fill.offset3={.1*variant,-.2*variant,.07};projected.fill.scale3={1.3,-.8,2.1};projected.fill.scale={1.7,.9};projected.fill.rotation=37;projected.fill.physical=variant==3;for(int c=0;c<3;++c)check(c);}
 std::printf("BTS_GPU_TRIPLANAR_VERIFIED channels=3 rotation_physical_hardness=1 shared_geometry=1\n");
 for(int mode:{0,1,2,3,4,6})for(int variant=0;variant<4;++variant){
  projected.fill.mode=mode;projected.fill.physical=variant==3;projected.fill.offset3={.08,-.07,.03};projected.fill.rotation3={17,-11,23};projected.fill.scale3={.65,-.8,.75};projected.fill.scale={1.7,.9};projected.fill.offset={.13,-.17};projected.fill.filtering=variant%3;projected.fill.wrap=variant;projected.fill.shape_crop=variant==1;projected.fill.depth_culling=true;projected.fill.depth_hardness=.35;projected.fill.backface_culling=variant==2;projected.fill.backface_angle=113;projected.fill.backface_hardness=.3;projected.fill.cylinder_angle=variant==2?180:360;
  projected.fill.atom=variant==0;projected.fill.texture=variant>=2?resource:nullptr;projected.fill.border_width=.73;projected.fill.invert=variant==0;projected.fill.middle_dot=true;projected.fill.border_dots=true;
  for(int c:{1,2})fill_channel_texture(projected.fill,c)=variant>=2?resource:nullptr;
  for(int c:{0,1,2,4})check(c);
 }
 const auto warp_diagnostic=[&]{const auto [diagnostic_x,diagnostic_y]=preview.impl_->oracle_position;
  using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(1,1).sampler(5,ImageType::Float2D,"layer_parameters_img").image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"result_img").push_constant(Type::float3_t,"world_point");
  info.compute_source_generated=GpuLayerParameters::declarations()+gpu_warp::source+R"GLSL(
void main(){vec3 v=world_point-projection_offset,p=vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v))*inverse_denominator;vec2 uv=vec2(-99);float depth=0;vec3 normal,du,dv;bool valid=warp_project(p,uv,depth,normal,du,dv);imageStore(result_img,ivec2(0),vec4(uv,depth,float(valid)));imageStore(result_img,ivec2(1,0),vec4(du,warp_projection_error));imageStore(result_img,ivec2(2,0),vec4(dv,0));}
)GLSL";
  Shader shader(GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free);if(!shader)throw std::runtime_error("Warp diagnostic shader failed");GpuLayerParameters parameters;gpu_spatial_parameters(parameters,projected.fill,atlas.get(),set);auto input=texture("BTS warp diagnostic metadata",parameters.values.size(),1,gpu::TextureFormat::SFLOAT_32_32_32_32);GPU_texture_update(input.get(),GPU_DATA_FLOAT,parameters.values.data());auto output=texture("BTS warp diagnostic",3,1,gpu::TextureFormat::SFLOAT_32_32_32_32);
  auto *layer_pixels=static_cast<float *>(GPU_texture_read(target.get(),GPU_DATA_FLOAT,0));const auto pixel=layer_pixels+(diagnostic_y*128+diagnostic_x)*4;std::fprintf(stderr,"BTS_GPU_WARP_LAYER rgba=%g,%g,%g,%g\n",pixel[0],pixel[1],pixel[2],pixel[3]);MEM_delete(layer_pixels);
  const auto &point=atlas->tiles.at(1001)[diagnostic_y*128+diagnostic_x];GPU_shader_bind(shader.get());GPU_texture_bind(input.get(),5);GPU_texture_image_bind(output.get(),0);GPU_shader_uniform_3fv(shader.get(),"world_point",point.position.data());GPU_compute_dispatch(shader.get(),1,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);auto *actual=static_cast<float *>(GPU_texture_read(output.get(),GPU_DATA_FLOAT,0));const auto frame=fill_planar_frame(projected.fill,point,*atlas);const auto expected=fill_warp_coordinate(projected.fill,frame);std::fprintf(stderr,"BTS_GPU_WARP_DIAGNOSTIC gpu_uv=%g,%g depth=%g valid=%g error=%g du=%g,%g,%g dv=%g,%g,%g cpu_uv=%g,%g depth=%g error=%g du=%g,%g,%g dv=%g,%g,%g coverage=%g sample=%g,%g\n",actual[0],actual[1],actual[2],actual[3],actual[7],actual[4],actual[5],actual[6],actual[8],actual[9],actual[10],expected?expected->u:-99,expected?expected->v:-99,expected?expected->depth:-99,expected?expected->error:-1,expected?expected->du[0]:0,expected?expected->du[1]:0,expected?expected->du[2]:0,expected?expected->dv[0]:0,expected?expected->dv[1]:0,expected?expected->dv[2]:0,expected?fill_warp_coverage(projected.fill,frame,*expected):0,TextureDocument::fill_sample(set,projected,0,diagnostic_x,diagnostic_y)[0],TextureDocument::fill_sample(set,projected,0,diagnostic_x,diagnostic_y)[3]);QJsonArray rows;for(const auto &row:parameters.values)rows.append(QJsonArray{row[0],row[1],row[2],row[3]});std::printf("BTS_GPU_WARP_NATIVE_INPUT %s\n",QJsonDocument(QJsonObject{{"parameters",rows},{"point",QJsonArray{point.position[0],point.position[1],point.position[2]}}}).toJson(QJsonDocument::Compact).constData());MEM_delete(actual);GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
 };
 const int warp_case_begin=cases;
 for(int deformation=0;deformation<4;++deformation)for(int variant=0;variant<4;++variant){
  projected.fill.mode=5;projected.fill.physical=variant==3;projected.fill.offset3={.08,-.07,.03};projected.fill.rotation3={17,-11,23};projected.fill.scale3={.65,-.8,.75};projected.fill.scale={1.7,.9};projected.fill.offset={.13,-.17};projected.fill.filtering=variant%3;projected.fill.wrap=variant;projected.fill.shape_crop=variant==1;projected.fill.depth_culling=true;projected.fill.depth_hardness=.35;projected.fill.backface_culling=variant==2;projected.fill.backface_angle=113;projected.fill.backface_hardness=.3;projected.fill.warp_depth=1.3;
  auto grid=std::make_shared<WarpGrid>();if(deformation){grid->move(4,{.1,-.5-.06*deformation,.04});grid->move(8,{.55,-.48,.43});}if(deformation>=2){grid->automatic_tangents=false;grid->at(1,1).du={1.2,.11,.03};grid->at(1,1).dv={.02,-.13,.87};grid->split(true,.27);}projected.fill.warp=grid;
  projected.fill.warp_geometry.reset();if(deformation==3){auto settings=std::make_shared<WarpGeometrySettings>();settings->authored_reference=grid;settings->frozen_reference=grid;settings->validate();projected.fill.warp_geometry=settings;}
  projected.fill.atom=variant==0;projected.fill.texture=variant>=2?resource:nullptr;for(int c:{1,2})fill_channel_texture(projected.fill,c)=variant>=2?resource:nullptr;
  for(int c:{0,1,2,4})try{check(c);}catch(const std::exception &error){warp_diagnostic();throw std::runtime_error("Warp deformation="+std::to_string(deformation)+" variant="+std::to_string(variant)+" channel="+std::to_string(c)+": "+error.what());}
 }
 // Timing-only owner-context run. Blocking query reads are confined to this
 // test; interactive production never waits for timer or texture readbacks.
 if(std::getenv("BTS_GPU_WARP_TIMING")){
  auto sample=set;sample.texture_resolution=1024;auto &layer=sample.layers.front();layer.fill.physical=false;layer.fill.offset3={};layer.fill.rotation3={};layer.fill.scale3={1,1,1};layer.fill.scale={1,1};layer.fill.offset={};layer.fill.rotation=0;layer.fill.texture=resource;layer.fill.atom=false;layer.fill.filtering=1;layer.fill.backface_culling=false;layer.fill.warp_geometry.reset();auto grid=std::make_shared<WarpGrid>();grid->move(4,{.1,-.56,.04});layer.fill.warp=grid;
  GpuLayerPreview measured;auto image=texture("BTS Warp timing",1024,1024,gpu::TextureFormat::SFLOAT_16_16_16_16);GLuint queries[2];glGenQueries(2,queries);benchmark_stack=true;
  for(int operation=0;operation<4;++operation){if(operation==2)layer.opacity[0]=.5;if(operation==3){auto changed=std::make_shared<WarpGrid>(*grid);changed->move(4,{.13,-.54,.06});layer.fill.warp=changed;}measured.begin_frame();const auto start=performance::Clock::now();glQueryCounter(queries[0],GL_TIMESTAMP);if(measured.update(sample,0,image.get())!=GpuPreviewUpdate::Updated)throw std::runtime_error("Warp timing unexpectedly deferred");const double cpu_ms=performance::ms(start);glQueryCounter(queries[1],GL_TIMESTAMP);GLuint64 begin,end;glGetQueryObjectui64v(queries[1],GL_QUERY_RESULT,&end);glGetQueryObjectui64v(queries[0],GL_QUERY_RESULT,&begin);std::printf("BTS_GPU_WARP_TIMING size=1024 operation=%s cpu_submit_ms=%.3f gpu_ms=%.3f test_query_wait_ms=%.3f\n",std::array{"initial","unchanged","opacity","control-point"}[operation],cpu_ms,double(end-begin)/1e6,performance::ms(start)-cpu_ms);}
  benchmark_stack=false;glDeleteQueries(2,queries);
 }
 const int warp_layer_cases=cases-warp_case_begin;
 // Warp mask sources use the same GPU solver and one shared prefix per tile.
 projected.fill=FillProjection{};projected.values.base_color={.3f,.7f,.9f,1};projected.mask=LayerMask{};projected.mask->background=.6;
 MaskEffect warp_mask;warp_mask.kind=MaskEffectKind::Fill;warp_mask.fill=.83;warp_mask.opacity=.71;warp_mask.blending=LayerBlend::Multiply;warp_mask.mapping.projection.mode=5;warp_mask.mapping.projection.warp_depth=1.7;warp_mask.mapping.projection.depth_culling=true;warp_mask.mapping.projection.depth_hardness=.24;
 auto mask_grid=std::make_shared<WarpGrid>();mask_grid->move(4,{.06,-.56,-.03});warp_mask.mapping.projection.warp=mask_grid;auto udim_atlas=std::make_shared<FillSurfaceAtlas>(*atlas);udim_atlas->tiles[1002]=udim_atlas->tiles.at(1001);for(auto &point:udim_atlas->tiles[1002])point.position[0]+=.14;set.projection_geometry=udim_atlas;set.tiles={1001,1002};
 for(bool atom:{false,true})for(int filter=0;filter<3;++filter){warp_mask.mapping.projection.atom=atom;warp_mask.mapping.projection.filtering=filter;projected.mask->effects={warp_mask};for(int tile:set.tiles){for(int channel:{0,2}){const auto work=preview.impl_->mask_dispatch_texels;preview.begin_frame();if(preview.update(set,channel,target.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU Warp mask UDIM deferred");if(channel==2&&preview.impl_->mask_dispatch_texels!=work)throw std::runtime_error("Warp mask duplicated between channels");++cases;}}}
 set.selected_layer=projected.id;set.selected_mask=true;for(int tile:set.tiles){preview.begin_frame();if(preview.update(set,15,target.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU Warp mask isolation deferred");++cases;}
 set.selected_mask=false;set.tiles={1001};set.projection_geometry=atlas;projected.mask.reset();
 std::printf("BTS_GPU_WARP_MASK_VERIFIED cases=26 tiles=2 uniform_atom=1 filters=3 shared_channels=2 isolation=1\n");
 projected.fill.warp.reset();projected.fill.warp_geometry.reset();
 std::printf("BTS_GPU_WARP_VERIFIED cases=%d identity_deformed_tangents_split=1 uniform_atom_texture=1 filter_wrap_physical=1 crop_depth_backface=1 frozen_envelope=1\n",warp_layer_cases);
 std::printf("BTS_GPU_PROJECTION_VERIFIED modes=uv,triplanar,planar,spherical,cylindrical,udim atom_uniform_texture=1 crop_cull_wrap_filter_physical=1\n");
 projected.fill.texture=resource;projected.fill.atom=false;projected.fill.physical=false;projected.fill.shape_crop=false;projected.fill.backface_culling=false;projected.fill.filtering=0;
 auto symmetry=std::make_shared<FillSymmetry>();symmetry->constraint.enabled=true;symmetry->constraint.copies=5;symmetry->constraint.center={.03,-.11,.05};symmetry->constraint.offset=.1;projected.fill.symmetry=symmetry;
 for(int mode:{1,2,3,4})for(int axis=0;axis<3;++axis)for(int kind=0;kind<2;++kind){symmetry->constraint.axis=axis;symmetry->constraint.mode=SymmetryMode(kind);symmetry->flip_u=true;symmetry->flip_v=axis==1;projected.fill.mode=mode;check(0);}
 std::printf("BTS_GPU_PROJECTION_SYMMETRY_VERIFIED modes=triplanar,planar,spherical,cylindrical axes=xyz mirror_radial_flip=1\n");


 set=TextureSet{};set.texture_resolution=128;set.channels[3]=true;set.layers.front().kind=LayerKind::Fill;set.layers.front().values.normal=normalized_normal({.3f,.1f,.9f});TextureDocument::add_fill(set);auto &normal_layer=set.layers.back();normal_layer.values.normal=normalized_normal({-.2f,.4f,.8f});normal_layer.opacity[3]=.67;normal_layer.mask=LayerMask{};normal_layer.mask->background=.59f;
 target=texture("BTS authored normal reference",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(int mode=0;mode<=28;++mode){normal_layer.blending[3]=LayerBlend(mode);check(6);}
 normal_layer.blending[3]=LayerBlend::NormalDetail;fill_channel_texture(normal_layer.fill,3)=resource;check(6);normal_layer.kind=LayerKind::Paint;auto normal_ink=ink;normal_ink.channels={false,false,false,true,false};normal_ink.values.normal=normalized_normal({-.4f,.3f,.8f});normal_layer.pixels.stamp(128,normal_ink);check(6);
 std::printf("BTS_GPU_AUTHORED_NORMAL_VERIFIED modes=29 fill_texture_paint_mask=1\n");
 set=TextureSet{};set.texture_resolution=128;set.channels[3]=true;auto &resampled=set.layers.front();resampled.mask=LayerMask{};resampled.mask->background=.25;auto resize_ink=ink;resize_ink.channels={true,true,true,true,true};resize_ink.values.normal=normalized_normal({.2f,-.3f,.9f});resize_ink.values.bump=-.1f;resampled.pixels.stamp(512,resize_ink);resampled.mask->pixels.stamp(256,mask);
 for(int height:{128,256}){set.texture_height=height;target=texture("BTS mixed-axis authored resampling",128,height,gpu::TextureFormat::SFLOAT_16_16_16_16);for(int c:{0,1,2,4,6})check(c);}
 std::printf("BTS_GPU_RESAMPLING_VERIFIED source=512 target=128x128,128x256 paint_mask_normal=1\n");
 set=TextureSet{};set.texture_resolution=128;set.channels[3]=true;set.displacement.channel=true;set.users[0].present=true;set.layers.front().kind=LayerKind::Fill;TextureDocument::add_fill(set);
 auto &effect_layer=set.layers.back();effect_layer.values.base_color={.2,.4,.7,1};effect_layer.values.metallic=.34;effect_layer.values.roughness=.67;effect_layer.values.bump=-.2;effect_layer.values.normal=normalized_normal({.2f,-.3f,.9f});effect_layer.opacity.fill(.68);effect_layer.displacement.enabled=true;effect_layer.displacement.value=-.2;effect_layer.users[0].enabled=true;effect_layer.users[0].value=.7;
 effect_layer.mask=LayerMask{};effect_layer.mask->background=.43;MaskEffect mask_levels;mask_levels.kind=MaskEffectKind::Levels;mask_levels.levels.gamma=1.6;mask_levels.levels.output_high=.9;effect_layer.mask->effects.push_back(mask_levels);MaskEffect mask_filter;mask_filter.kind=MaskEffectKind::Filter;mask_filter.filter.resource=1;mask_filter.opacity=.6;effect_layer.mask->effects.push_back(mask_filter);MaskEffect mask_fill;mask_fill.kind=MaskEffectKind::Fill;mask_fill.fill=.7;mask_fill.opacity=.4;mask_fill.blending=LayerBlend::Multiply;effect_layer.mask->effects.push_back(mask_fill);
 target=texture("BTS content and mask effects reference",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(int c:{0,1,2,4,6,7,16}){const int ec=gpu_layer_channel(effect_layer,c).fill_channel;LevelsEffect levels;levels.channel=ec;levels.input_low=-.1;levels.input_high=.9;levels.gamma=1.7;levels.output_low=-.2;levels.output_high=1.3;levels.rgb_curves[1].gamma=.8;LevelsEffect filter;filter.kind=ContentEffectKind::Filter;filter.channel=ec;filter.filter.resource=1;filter.opacity=.37;filter.blending=LayerBlend::Overlay;effect_layer.effects={levels,filter};for(int variant=0;variant<4;++variant){effect_layer.effects[0].clamp=variant==1;effect_layer.effects[1].filter.resource=variant;const auto blend=variant==3?LayerBlend::Passthrough:variant==2?LayerBlend::Replace:LayerBlend::Normal;effect_layer.blending.fill(blend);effect_layer.displacement.blending=blend;effect_layer.users[0].blending=blend;check(c);}}
 const int levels_mix_first=cases;
 set.selected_mask=true;
 for(int channel:{0,1,2,4,7,16})for(auto mode:{LayerBlend::Normal,LayerBlend::Multiply,LayerBlend::Overlay,LayerBlend::LinearDodge,LayerBlend::Disabled})for(float opacity:{0.f,.37f,1.f}){
  LevelsEffect levels;levels.channel=gpu_layer_channel(effect_layer,channel).fill_channel;levels.gamma=1.7;levels.output_low=.1;levels.output_high=.9;levels.opacity=opacity;levels.blending=mode;effect_layer.effects={levels};
  auto &mask_mix=effect_layer.mask->effects.front();mask_mix.opacity=opacity;mask_mix.blending=mode;
  check(channel);if(channel==0)check(15);
 }
 set.selected_mask=false;
 std::printf("BTS_GPU_LEVELS_MIX_VERIFIED cases=%d channels=color,metallic,roughness,bump,user,displacement mask_isolation=1 modes=normal,multiply,overlay,add,disabled opacity=0,0.37,1 cpu_oracle=1\n",cases-levels_mix_first);
 effect_layer.effects.clear();effect_layer.mask->effects.clear();check(0);
 std::printf("BTS_GPU_POINT_EFFECTS_VERIFIED channels=color,metallic,roughness,bump,normal,user,displacement content_levels_filters_mask_levels_fill=1 passthrough_replace=1\n");
 // Baked mask inputs stay shared and invalidate only their consuming prefixes.
 for(int role:{2,4,5}){MeshMap map;map.name="Mask source";std::vector<float> values(32*32*4);for(int y=0;y<32;++y)for(int x=0;x<32;++x){const int i=(y*32+x)*4;const float alpha=x<6?0:x<16?.4f:1;values[i]=alpha*(x+.5f)/32;values[i+1]=alpha*(y+.5f)/32;values[i+2]=alpha*.4f;values[i+3]=alpha;}auto pixels=std::make_shared<MeshMapPixels>(32,std::move(values));map.tiles[1001]={32,pixels->hash,pixels};set.mesh_maps[role]=map;}
 MaskEffect id_selection;id_selection.kind=MaskEffectKind::ColorSelection;id_selection.output=.83;id_selection.hardness=.4;id_selection.tolerance=.2;
 for(int i=0;i<17;++i)id_selection.colors.push_back({(i+.5f)/17,.43f,.4f});effect_layer.mask->effects={id_selection,mask_levels,mask_filter};
 for(int variant=0;variant<4;++variant){effect_layer.mask->effects[0].use_id_map=variant!=1;if(variant==2)effect_layer.mask->effects[0].colors.clear();if(variant==3)effect_layer.mask->effects[0]=id_selection;for(int c:{0,2,4,6,7,16})check(c);}
 const auto original_generator_ao=set.mesh_maps[3],original_generator_thickness=set.mesh_maps[6],original_generator_normal=set.mesh_maps[1];set.mesh_maps[3]=set.mesh_maps[4];set.mesh_maps[6]=set.mesh_maps[4];set.mesh_maps[1]=set.mesh_maps[5];
 for(int generator=0;generator<=6;++generator)for(int axis=0;axis<3;++axis)for(int curve=0;curve<3;++curve)for(int filtering=0;filtering<2;++filtering){MaskEffect effect;effect.kind=MaskEffectKind::Generator;effect.generator=generator;effect.axis=axis;effect.curve=curve;effect.map_filter=filtering;effect.contrast=2.3;effect.bias=-.1;effect.invert=axis==1;effect.opacity=.73;effect.blending=LayerBlend::Multiply;effect_layer.mask->effects={effect,mask_filter};for(int c:{0,2,4})check(c);}
 for(const auto direction:std::array<std::array<float,3>,5>{{{1,0,0},{0,-1,0},{1,.3f,-.7f},{0,0,0},{0,0,1e-8f}}}){MaskEffect light;light.kind=MaskEffectKind::Generator;light.generator=6;light.light_direction=direction;light.map_filter=1;effect_layer.mask->effects={light};check(0);check(2);}
 for(float influence:{0.f,.3f,1.f}){MaskEffect dirt;dirt.kind=MaskEffectKind::Generator;dirt.generator=5;dirt.dirt_curvature=influence;effect_layer.mask->effects={dirt};check(0);check(4);}
 for(int generator:{4,5,6}){const auto roles=mask_generator_inputs(generator);for(int role=0;role<7;++role)if(roles&(1u<<role)){const auto original=set.mesh_maps[role];MaskEffect live;live.kind=MaskEffectKind::Generator;live.generator=generator;effect_layer.mask->effects={live};set.mesh_maps[role].reset();check(0);set.mesh_maps[role]=set.mesh_maps[5];check(0);set.mesh_maps[role]=original;check(0);}}
 // Isolation must bind every input itself, never inherit a previous layer draw.
 for(int generator:{4,6}){MaskEffect resource;resource.kind=MaskEffectKind::Generator;resource.generator=generator;effect_layer.mask->effects={resource};GPU_texture_unbind_all();auto *isolated=preview.mask_texture(set,effect_layer.id,1001);if(!isolated)throw std::runtime_error("Generator isolation did not finish");auto *values=static_cast<float *>(GPU_texture_read(isolated,GPU_DATA_FLOAT,0));if(!values)throw std::runtime_error("Generator isolation readback failed");double error=0;for(int y=4;y<128;y+=16)for(int x=4;x<128;x+=16)error=std::max(error,double(std::abs(values[y*128+x]-effect_layer.mask->value(x,y,1001,128,128,nullptr,&set.mesh_maps))));MEM_delete(values);if(error>2e-6)throw std::runtime_error("Generator isolation inherited stale sampler bindings");GPU_texture_unbind_all();}
 std::printf("BTS_GPU_MESH_GENERATORS_VERIFIED thickness=1 dirt_ao_curvature=1 world_normal_light=1 directions=5 missing_rebake=1 clean_bindings=1\n");
 set.mesh_maps[3]=original_generator_ao;set.mesh_maps[6]=original_generator_thickness;set.mesh_maps[1]=original_generator_normal;
 for(int generator:{2,3}){const int role=mask_generator_role(generator);const auto original=set.mesh_maps[role];MaskEffect live;live.kind=MaskEffectKind::Generator;live.generator=generator;live.curve=generator==3?2:1;live.map_filter=1;live.invert=true;effect_layer.mask->effects={live};set.mesh_maps[role].reset();check(0);set.mesh_maps[role]=set.mesh_maps[5];check(0);set.mesh_maps[role]=original;check(0);}
 std::printf("BTS_GPU_SMART_MATERIAL_GENERATORS_VERIFIED ao=1 curves=3 filters=2 axes=3\n");
 // A map disappearing and returning must not leave a stale cached result.
 const auto saved_position=set.mesh_maps[5];MaskEffect height_mask;height_mask.kind=MaskEffectKind::Generator;height_mask.generator=1;height_mask.axis=0;effect_layer.mask->effects={height_mask};set.mesh_maps[5].reset();check(0);set.mesh_maps[5]=saved_position;check(0);set.mesh_maps[5]->tiles.clear();check(0);set.mesh_maps[5]=saved_position;check(0);
 for(int operation=0;operation<3;++operation)for(int left:{2,3})for(int right:{2,3}){MaskEffect compare;compare.kind=MaskEffectKind::Compare;compare.operation=operation;compare.left=left;compare.right=right;compare.constant=.47;compare.hardness=.4;compare.tolerance=.23;compare.opacity=.8;effect_layer.mask->effects={height_mask,compare};check(0);check(2);}
 effect_layer.mask->effects.clear();check(0);
 std::printf("BTS_GPU_MASK_MAPS_VERIFIED id_colors=17 generators=height,curvature axes=xyz constant_compare=1 missing_rebake=1 shared_maps=1\n");
 // Material-channel Compare reads the GPU source/below prefix at this exact
 // layer depth. Never substitute the final channel, or let a mask read itself.
 set=TextureSet{};set.texture_resolution=128;set.channels.fill(true);set.ao_channel=true;
 set.layers.front().kind=LayerKind::Fill;set.layers.front().values.base_color={.15f,.3f,.55f,1};set.layers.front().values.roughness=.23f;set.layers.front().values.metallic=.65f;set.layers.front().values.bump=-.3f;set.layers.front().values.normal=normalized_normal({-.2f,.3f,.9f});set.layers.front().ao_enabled=true;set.layers.front().values.ambient_occlusion=.31f;
 TextureDocument::add_fill(set);auto &compared=set.layers.back();compared.channels.fill(true);compared.ao_enabled=true;compared.values.base_color={.7f,.4f,.2f,1};compared.values.roughness=.68f;compared.values.metallic=.35f;compared.values.bump=.2f;compared.values.normal=normalized_normal({.3f,-.2f,.9f});compared.values.ambient_occlusion=.74f;compared.opacity.fill(.71f);compared.mask=LayerMask{};compared.mask->background=.37f;
 MaskEffect material_compare;material_compare.kind=MaskEffectKind::Compare;material_compare.hardness=.43f;material_compare.tolerance=.28f;material_compare.constant=.42f;material_compare.opacity=.81f;
 target=texture("BTS GPU material comparison",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);const auto compare_case_start=cases;
 for(int c=0;c<6;++c)for(int operation=0;operation<3;++operation)for(int left=0;left<4;++left)for(int right=0;right<4;++right){if(left>=2&&right>=2)continue;material_compare.compare_channel=c;material_compare.left=left;material_compare.right=right;material_compare.operation=operation;compared.mask->effects={material_compare};check(0);check(2);}
 for(float offset:{-.3f,0.f,.4f})for(float width:{0.f,.2f,1.f})for(bool invert:{false,true}){
  auto ramp=material_compare;ramp.compare_channel=4;ramp.left=1;ramp.right=0;ramp.compare_offset=offset;ramp.compare_width=width;ramp.compare_invert=invert;ramp.compare_ramp={0,.1f,.8f,.6f,1};compared.mask->effects={ramp};check(0);check(15);
 }
 material_compare.compare_channel=2;material_compare.left=1;material_compare.right=0;material_compare.operation=2;compared.mask->effects={material_compare};set.selected_layer=compared.id;
 // Isolation still evaluates hidden/disabled masks and their source metadata.
 for(int variant=0;variant<6;++variant){compared.visible=variant!=1;compared.mask->enabled=variant!=2;compared.channels[2]=variant!=3;compared.opacity[2]=variant==4?0:.71f;compared.blending[2]=variant==5?LayerBlend::Passthrough:LayerBlend::Normal;check(15);check(0);}
 compared.visible=true;compared.mask->enabled=true;compared.channels[2]=true;compared.opacity[2]=.71f;compared.blending[2]=LayerBlend::Normal;
 LevelsEffect comparison_paint;comparison_paint.kind=ContentEffectKind::Paint;comparison_paint.paint.pixels.stamp(128,ink);LevelsEffect comparison_levels;comparison_levels.channel=2;comparison_levels.gamma=1.8f;compared.effects={comparison_paint,comparison_levels};check(0);check(15);
 compared.effects.clear();const auto comparison_saved=set;std::swap(set.layers[0],set.layers[1]);check(0);set=comparison_saved;check(0);
 // Nested folders use their own depth accumulator for the Below operand.
 set=comparison_saved;TextureLayer comparison_child=set.layers.back();comparison_child.id=QUuid::createUuid();comparison_child.depth=1;TextureLayer group;group.kind=LayerKind::Folder;group.channels.fill(true);group.mask=LayerMask{};group.mask->background=.8f;group.mask->effects={material_compare};
 set.layers={set.layers.front(),comparison_child,group};set.selected_layer=comparison_child.id;check(0);check(15);set.selected_layer=group.id;check(15);check(2);
 // An anchor consumes an earlier mask prefix, including its material queries.
 MaskEffect point_anchor;point_anchor.kind=MaskEffectKind::Anchor;set.layers[1].mask->effects.push_back(point_anchor);
 MaskEffect from_anchor;from_anchor.kind=MaskEffectKind::Fill;from_anchor.anchor_source=point_anchor.id;set.layers[2].mask->effects={from_anchor};check(0);check(15);
 set.layers[1].visible=false;set.layers[1].mask->enabled=false;check(0);check(15);
 set=comparison_saved;set.texture_resolution=512;set.layers.front().kind=LayerKind::Paint;auto comparison_ink=ink;comparison_ink.channels={true,true,true,true,true};comparison_ink.values.bump=-.3f;comparison_ink.center={.15,.15};comparison_ink.axis_x={.06,0};comparison_ink.axis_y={0,.06};set.layers.front().pixels.stamp(512,comparison_ink);set.layers.back().mask->effects[0].compare_channel=4;
 target=texture("BTS material comparison dirty chunks",512,512,gpu::TextureFormat::SFLOAT_16_16_16_16);check(0);const auto comparison_dispatch_before=preview.impl_->mask_dispatch_texels;comparison_ink.center={.1,.1};comparison_ink.values.bump=.8f;set.layers.front().pixels.stamp(512,comparison_ink);check(0);const auto comparison_dirty=preview.impl_->mask_dispatch_texels-comparison_dispatch_before;if(!comparison_dirty||comparison_dirty>2ULL*256*256)throw std::runtime_error("Material Compare recomputed clean mask chunks");
 const auto comparison_unchanged=preview.impl_->mask_dispatch_texels;check(0);if(preview.impl_->mask_dispatch_texels!=comparison_unchanged||preview.impl_->last_dispatch_texels)throw std::runtime_error("Unchanged material Compare recomputed prefixes");
 std::printf("BTS_GPU_MATERIAL_COMPARE_VERIFIED cases=%d channels=6 operands=4 operations=3 hidden_disabled_opacity_zero=1 content_paint_levels=1 passthrough=1 reorder_undo=1 isolation=1 production_readback=0\n",cases-compare_case_start);
 // UI histograms reduce GPU prefixes asynchronously. The CPU oracle below is
 // test-only and matches the former panel sampling positions and Auto range.
 set=TextureSet{};set.texture_resolution=128;set.channels.fill(true);set.ao_channel=true;set.layers.front().kind=LayerKind::Fill;set.layers.front().values.roughness=.19f;set.layers.front().values.base_color={.17f,.33f,.58f,1};TextureDocument::add_fill(set);auto &hist_layer=set.layers.back();hist_layer.values.roughness=.71f;hist_layer.values.base_color={.69f,.41f,.23f,1};hist_layer.channels.fill(true);hist_layer.ao_enabled=true;hist_layer.mask=LayerMask{};hist_layer.mask->background=.37f;set.selected_layer=hist_layer.id;
 MaskEffect hist_compare;hist_compare.kind=MaskEffectKind::Compare;hist_compare.constant=.43f;hist_layer.mask->effects={hist_compare};
 const auto histogram_start=cases;std::uint64_t histogram_ticket=0;
 const auto histogram_reference=[&](const HistogramRequest &request){HistogramMetadata expected;expected.minimum=65504;expected.maximum=-65504;expected.ticket=histogram_ticket;const auto &layer=TextureDocument::layer(set,request.layer);LayerMask prefix;if(request.mask){const auto input=mask_effect_input(*layer.mask,request.effect);if(!input)throw std::runtime_error("Histogram mask effect missing");prefix=*input;}
  for(int y=0;y<16;++y)for(int x=0;x<16;++x){const int px=(2*x+1)*128/32,py=(2*y+1)*128/32;const auto record=[&](int series,float value){++expected.bins[series][std::clamp(int(std::clamp(value,0.f,1.f)*63),0,63)];expected.minimum=std::min(expected.minimum,value);expected.maximum=std::max(expected.maximum,value);};
   if(request.compare){const auto &effect=layer.mask->effects.back();const auto context=TextureDocument::comparison_inputs(set,layer.id,px,py,1001);const float previous=prefix.value(px,py,1001,128,128,nullptr,&set.mesh_maps,&context);record(0,mask_compare_operand(effect,effect.left,previous,&context));record(1,mask_compare_operand(effect,effect.right,previous,&context));}
   else {std::array<float,3> rgb;if(request.mask){const auto context=TextureDocument::comparison_inputs(set,layer.id,px,py,1001);rgb.fill(prefix.value(px,py,1001,128,128,nullptr,&set.mesh_maps,&context));}else rgb=TextureDocument::effect_input(set,request.effect,px,py,1001);for(int c=0;c<3;++c)if(!request.component||request.component==c+1)record(0,request.component?levels_curve_value(request.curve,rgb[c]):rgb[c]);}
  }return expected;
 };
 const auto check_histogram=[&](HistogramRequest request){++histogram_ticket;preview.begin_frame();if(preview.histogram(set,request,histogram_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU histogram request deferred");const auto start=performance::Clock::now();while(preview.impl_->histograms.pending()){preview.impl_->histograms.poll();if(performance::ms(start)>3000)throw std::runtime_error("Histogram metadata timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}const auto actual=preview.histogram_result(request.effect);const auto expected=histogram_reference(request);if(!actual||actual->ticket!=histogram_ticket||actual->bins!=expected.bins||std::abs(actual->minimum-expected.minimum)>2e-5||std::abs(actual->maximum-expected.maximum)>2e-5){std::fprintf(stderr,"BTS_GPU_HISTOGRAM_ORACLE compare=%d mask=%d channel=%d component=%d ticket=%llu min=%g/%g max=%g/%g bins_equal=%d\n",request.compare,request.mask,request.channel,request.component,(unsigned long long)histogram_ticket,actual?actual->minimum:-999,expected.minimum,actual?actual->maximum:-999,expected.maximum,actual&&actual->bins==expected.bins);throw std::runtime_error("GPU UI histogram differs from CPU oracle");}++cases;};
 HistogramRequest histogram_request;histogram_request.layer=hist_layer.id;histogram_request.effect=hist_compare.id;histogram_request.mask=histogram_request.compare=true;
 for(int c=0;c<6;++c)for(int left=0;left<4;++left)for(int right=0;right<4;++right){hist_layer.mask->effects.back().compare_channel=c;hist_layer.mask->effects.back().left=left;hist_layer.mask->effects.back().right=right;histogram_request.channel=c;check_histogram(histogram_request);}
 LevelsEffect hist_levels;hist_levels.channel=0;hist_layer.effects={hist_levels};histogram_request.mask=histogram_request.compare=false;histogram_request.effect=hist_levels.id;histogram_request.channel=0;
 for(int component=0;component<4;++component){histogram_request.component=component;histogram_request.curve.gamma=1.8f;check_histogram(histogram_request);}
 // Prefix before Levels includes earlier content operations, but excludes the
 // selected effect even when it is disabled. Signed Auto endpoints survive.
 hist_layer.effects.front().channel=4;hist_layer.values.bump=-.17f;histogram_request.channel=4;histogram_request.component=0;check_histogram(histogram_request);hist_layer.values.bump=1.7f;check_histogram(histogram_request);hist_layer.effects.front().enabled=false;check_histogram(histogram_request);
 TextureDocument::add_user_channel(set,0);TextureDocument::add_displacement_channel(set);hist_layer.users[0].enabled=hist_layer.displacement.enabled=true;hist_layer.users[0].value=-.37f;hist_layer.displacement.value=1.7f;
 for(int channel:{6,14}){hist_layer.effects.front().channel=channel;histogram_request.channel=channel;check_histogram(histogram_request);hist_layer.visible=false;check_histogram(histogram_request);hist_layer.visible=true;auto &scalar=channel==14?hist_layer.displacement:hist_layer.users[0];scalar.opacity=0;check_histogram(histogram_request);scalar.opacity=1;
  auto owners=std::make_shared<GeometryOwnershipTile>();owners->width=owners->height=128;owners->owners.resize(128*128);for(int y=0;y<128;++y)for(int x=0;x<128;++x)owners->owners[y*128+x]=x<64?11:42;set.geometry_ownership[1001]=owners;set.geometry_names={{11,"left"},{42,"right"}};hist_layer.geometry.meshes={{"right",false}};check_histogram(histogram_request);hist_layer.geometry={};}
 hist_layer.effects.front().channel=4;histogram_request.channel=4;
 MaskEffect hist_mask_levels;hist_mask_levels.kind=MaskEffectKind::Levels;hist_layer.mask->effects.push_back(hist_mask_levels);histogram_request.effect=hist_mask_levels.id;histogram_request.mask=true;check_histogram(histogram_request);
 // A Levels child sees its isolated sibling prefix, independent of both
 // ancestor mixes/backgrounds and later siblings at either nesting depth.
 {MaskEffect first;first.kind=MaskEffectKind::Fill;first.fill=.27f;MaskEffect later=first;later.id=QUuid::createUuid();later.fill=.93f;
  MaskEffect inner;inner.kind=MaskEffectKind::Group;inner.opacity=.23f;inner.blending=LayerBlend::Multiply;inner.group_effects=std::make_shared<const std::vector<MaskEffect>>(std::vector{first,hist_mask_levels,later});MaskEffect outer=inner;outer.id=QUuid::createUuid();MaskEffect before= later;before.id=QUuid::createUuid();MaskEffect after=later;after.id=QUuid::createUuid();outer.group_effects=std::make_shared<const std::vector<MaskEffect>>(std::vector{before,inner});hist_layer.mask->background=.81f;hist_layer.mask->effects={outer,after};
  check_histogram(histogram_request);const auto nested=preview.histogram_result(hist_mask_levels.id);if(!nested||std::abs(nested->minimum-.27f)>2e-5||std::abs(nested->maximum-.27f)>2e-5||nested->bins[0][17]!=768)throw std::runtime_error("Nested Levels histogram includes ancestor/later effects");std::printf("BTS_GPU_NESTED_LEVELS_INPUT_VERIFIED samples=256 values=%u minimum=%.9g maximum=%.9g ancestor_isolation=1\n",nested->bins[0][17],nested->minimum,nested->maximum);}
 // Replacing an in-flight histogram may publish only the newest ticket.
 histogram_request.effect=hist_compare.id;histogram_request.compare=true;hist_layer.mask->effects={hist_compare};hist_layer.mask->effects.back().left=3;hist_layer.mask->effects.back().right=3;
 preview.begin_frame();hist_layer.mask->effects.back().constant=.13f;if(preview.histogram(set,histogram_request,++histogram_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("First async histogram rejected");hist_layer.mask->effects.back().constant=.83f;if(preview.histogram(set,histogram_request,++histogram_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Latest async histogram rejected");const auto hist_start=performance::Clock::now();while(preview.impl_->histograms.pending()){preview.impl_->histograms.poll();if(performance::ms(hist_start)>3000)throw std::runtime_error("Latest histogram timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}const auto latest=preview.histogram_result(hist_compare.id);if(!latest||latest->ticket!=histogram_ticket||latest->bins[0][52]!=256||!preview.impl_->histograms.discarded)throw std::runtime_error("Stale histogram published");++cases;
 std::printf("BTS_GPU_HISTOGRAM_VERIFIED cases=%d samples=256 packet_bytes=520 compare=1 levels=1 signed_auto=1 latest_wins=1 cpu_composition=0\n",cases-histogram_start);
 // Material picker reads the cached unlit authoring prefixes, never the
 // framebuffer or a lazily materialized CPU paint recipe.
 const int picker_start=cases;std::uint64_t picker_ticket=0;
 set=TextureSet{};set.texture_resolution=128;set.tiles={1001,1002};set.channels.fill(true);set.ao_channel=true;set.precision.fill(ChannelPrecision::RGB32F);
 set.base_values.base_color={.23f,.41f,1.7f,1};set.base_values.normal={.2f,.1f,.97f};
 auto &picker_fill=set.layers.front();picker_fill.kind=LayerKind::Fill;picker_fill.channels.fill(true);picker_fill.ao_enabled=true;picker_fill.values.base_color={.71f,.11f,2.4f,1};picker_fill.values.normal={.6f,.2f,.77f};picker_fill.values.bump=-.27f;picker_fill.values.ambient_occlusion=.31f;picker_fill.opacity.fill(.43f);picker_fill.ao_opacity=.61f;
 TextureDocument::add_paint(set);auto &picker_paint=set.layers.back();picker_paint.channels.fill(true);picker_paint.ao_enabled=true;picker_paint.opacity.fill(.71f);picker_paint.ao_opacity=.81f;picker_paint.mask=LayerMask{};picker_paint.mask->background=.37f;
 PaintPixels::Stamp picker_stamp;picker_stamp.center={.3,.4};picker_stamp.axis_x={.17,0};picker_stamp.axis_y={0,.13};picker_stamp.values.base_color={.17f,1.3f,.08f,1};picker_stamp.values.metallic=.83f;picker_stamp.values.roughness=.21f;picker_stamp.values.bump=1.7f;picker_stamp.channels.fill(true);picker_stamp.flow=.71f;
 {PaintRecordingScope recording(true);picker_paint.pixels.stamp(128,picker_stamp,set.tiles);picker_stamp.center={1.7,.6};picker_paint.pixels.stamp(128,picker_stamp,set.tiles);}
 const auto picker_values=[](const MaterialValues &v){return std::array<float,11>{v.base_color[0],v.base_color[1],v.base_color[2],v.base_color[3],v.metallic,v.roughness,v.normal[0],v.normal[1],v.normal[2],v.bump,v.ambient_occlusion};};
 double picker_maximum=0;
 const auto check_picker=[&](MaterialSampleRequest request){preview.begin_frame();const auto reads=PaintPixels::recipe_materializations();if(preview.sample_material(set,request,++picker_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU material pick deferred");if(PaintPixels::recipe_materializations()!=reads)throw std::runtime_error("Material pick materialized CPU paint");
  if(preview.sample_material(set,request,picker_ticket+1)!=GpuPreviewUpdate::Pending)throw std::runtime_error("Material sample overwrote in-flight packet");
  const auto started=performance::Clock::now();while(preview.impl_->material_samples.pending()){preview.impl_->material_samples.poll();if(performance::ms(started)>3000)throw std::runtime_error("Material pick timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  const auto actual=preview.material_sample_result();if(!actual||!actual->valid||actual->ticket!=picker_ticket)throw std::runtime_error("Wrong material pick ticket");
  double error=0;if(!request.mask.isNull())error=std::abs(actual->mask-TextureDocument::mask_value(set,request.mask,request.x,request.y,request.tile));else{const auto a=picker_values(actual->material),b=picker_values(TextureDocument::evaluate(set,request.x,request.y,request.tile).material);for(int c=0;c<11;++c)error=std::max(error,double(std::abs(a[c]-b[c])));}
  picker_maximum=std::max(picker_maximum,error);if(error>3e-5){std::fprintf(stderr,"BTS_GPU_MATERIAL_PICK_ORACLE tile=%d x=%d y=%d mask=%d error=%.9g\n",request.tile,request.x,request.y,!request.mask.isNull(),error);throw std::runtime_error("GPU material pick differs from channel-precision CPU oracle");}++cases;
 };
 for(int precision:{8,6,4,2,0}){set.precision.fill(ChannelPrecision(precision));for(int tile:set.tiles)for(auto point:std::array<std::array<int,2>,4>{{{7,11},{38,51},{89,76},{123,119}}}){MaterialSampleRequest request;request.tile=tile;request.x=point[0];request.y=point[1];check_picker(request);request.mask=picker_paint.id;check_picker(request);}}
 MaskEffect picker_compare;picker_compare.kind=MaskEffectKind::Compare;picker_compare.compare_channel=2;picker_compare.left=0;picker_compare.right=3;picker_compare.constant=.2;picker_paint.mask->effects={picker_compare};MaterialSampleRequest picker_request;picker_request.x=38;picker_request.y=51;picker_request.mask=picker_paint.id;set.precision.fill(ChannelPrecision::RGB32F);check_picker(picker_request);picker_paint.mask->enabled=false;check_picker(picker_request);
 picker_request.tile=1003;if(preview.sample_material(set,picker_request,++picker_ticket)!=GpuPreviewUpdate::Unsupported)throw std::runtime_error("Missing pick tile accepted");++cases;
 std::printf("BTS_GPU_MATERIAL_PICK_VERIFIED cases=%d packet_bytes=96 udim=2 precision_modes=5 signed_hdr=1 mask_compare=1 in_flight_guard=1 cpu_composition=0 max_error=%.9g\n",cases-picker_start,picker_maximum);
 const int thumbnail_start=cases;std::uint64_t thumbnail_ticket=0;double thumbnail_maximum=0;
 set=TextureSet{};set.texture_resolution=128;set.tiles={1001,1002,1011};set.channels.fill(true);set.ao_channel=true;set.precision.fill(ChannelPrecision::RGB32F);set.users[0].present=true;set.displacement.channel=set.displacement.allocated=true;
 auto &thumbnail_layer=set.layers.front();thumbnail_layer.kind=LayerKind::Paint;thumbnail_layer.channels.fill(true);thumbnail_layer.ao_enabled=true;thumbnail_layer.users[0].enabled=true;thumbnail_layer.displacement.enabled=true;
 picker_stamp.center={.25,.5};picker_stamp.axis_x={.21,0};picker_stamp.axis_y={0,.31};picker_stamp.values.normal={.2f,.6f,.77f};
 {PaintRecordingScope recording(true);for(const auto center:std::array<std::array<double,2>,3>{{{.25,.5},{1.7,.6},{.7,1.3}}}){picker_stamp.center=center;thumbnail_layer.pixels.stamp(128,picker_stamp,set.tiles);auto scalar_stamp=picker_stamp;scalar_stamp.channels={false,false,true,false,false};scalar_stamp.values.roughness=-.23f;thumbnail_layer.ao_pixels.stamp(128,scalar_stamp,set.tiles);thumbnail_layer.users[0].pixels.stamp(128,scalar_stamp,set.tiles);thumbnail_layer.displacement.pixels.stamp(128,scalar_stamp,set.tiles);}}
 thumbnail_layer.mask=LayerMask{};thumbnail_layer.mask->background=.27f;MaskEffect thumbnail_levels;thumbnail_levels.kind=MaskEffectKind::Levels;thumbnail_levels.levels.gamma=1.7;thumbnail_layer.mask->effects={thumbnail_levels};
 const auto check_thumbnail=[&](ThumbnailRequest request){preview.begin_frame();const auto before=PaintPixels::recipe_materializations();if(preview.thumbnail(set,request,++thumbnail_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU thumbnail deferred");if(PaintPixels::recipe_materializations()!=before)throw std::runtime_error("Thumbnail materialized CPU paint");
  const auto started=performance::Clock::now();while(preview.impl_->thumbnails.pending()){preview.impl_->thumbnails.poll();if(performance::ms(started)>3000)throw std::runtime_error("Thumbnail fence timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  const auto actual=preview.thumbnail_result();if(!actual||actual->ticket!=thumbnail_ticket||!actual->valid)throw std::runtime_error("Wrong thumbnail ticket");
  auto oracle=set;auto &own=oracle.layers.front();own.visible=true;own.opacity.fill(1);own.ao_opacity=1;own.displacement.opacity=1;own.users[0].opacity=1;own.blending.fill(LayerBlend::Normal);own.ao_blending=own.displacement.blending=own.users[0].blending=LayerBlend::Normal;own.mask.reset();LevelsEffect sentinel;sentinel.channel=request.channel;own.effects.push_back(sentinel);
  const std::vector<int> tiles(set.tiles.begin(),set.tiles.end());double maximum=0;
  for(int y=0;y<48;++y)for(int x=0;x<48;++x){const int index=(y*2/48)*2+x*2/48;std::array<float,4> expected{};
   if(index<int(tiles.size())){const int tile=tiles[index],px=int(std::fmod((x+.5)*2/48,1.)*set.width(tile)),py=int((1-std::fmod((y+.5)*2/48,1.))*set.rows(tile));
    if(request.mask){const float v=TextureDocument::mask_value(set,request.layer,px,py,tile);expected={v,v,v,1};}
    else {const auto rgb=TextureDocument::effect_input(oracle,sentinel.id,px,py,tile);float alpha;
     if(request.channel==14)alpha=TextureDocument::composite_displacement(oracle,px,py,tile)[1];else if(request.channel>=6)alpha=TextureDocument::composite_user(oracle,request.channel-6,px,py,tile)[1];else {const auto c=TextureDocument::evaluate(oracle,px,py,tile);alpha=request.channel==5?c.ao_coverage:c.coverage[request.channel];}
     expected={rgb[0],rgb[1],rgb[2],alpha};
    }
   }
   const auto &pixel=actual->pixels[y*48+x];for(int c=0;c<4;++c)maximum=std::max(maximum,double(std::abs(pixel[c]-expected[c])));
  }
  thumbnail_maximum=std::max(thumbnail_maximum,maximum);if(maximum>3e-5){std::fprintf(stderr,"BTS_GPU_THUMBNAIL_ORACLE channel=%d mask=%d error=%.9g\n",request.channel,request.mask,maximum);throw std::runtime_error("GPU thumbnail differs from source CPU oracle");}++cases;
 };
 for(int channel:{0,1,2,3,4,5,6,14}){thumbnail_layer.visible=true;thumbnail_layer.opacity.fill(.61f);thumbnail_layer.blending.fill(LayerBlend::Normal);check_thumbnail({thumbnail_layer.id,channel,false});thumbnail_layer.visible=false;thumbnail_layer.opacity.fill(0);thumbnail_layer.ao_opacity=thumbnail_layer.users[0].opacity=thumbnail_layer.displacement.opacity=0;thumbnail_layer.blending.fill(LayerBlend::Disabled);thumbnail_layer.ao_blending=thumbnail_layer.users[0].blending=thumbnail_layer.displacement.blending=LayerBlend::Disabled;check_thumbnail({thumbnail_layer.id,channel,false});}
 check_thumbnail({thumbnail_layer.id,0,true});thumbnail_layer.mask->enabled=false;check_thumbnail({thumbnail_layer.id,0,true});
 // A thumbnail must not overwrite an already-returned Compare source in
 // the same GPU frame (especially Passthrough or zero-opacity content effects).
 for(bool zero_opacity:{false,true}){
  set=TextureSet{};set.texture_resolution=128;set.channels.fill(true);set.precision.fill(ChannelPrecision::RGB32F);set.layers.front().kind=LayerKind::Fill;set.layers.front().values.base_color={.2f,.4f,.6f,1};TextureDocument::add_paint(set);auto &own=set.layers.back();own.blending[0]=zero_opacity?LayerBlend::Normal:LayerBlend::Passthrough;own.opacity[0]=zero_opacity?0:.6f;
  picker_stamp.center={.3,.4};picker_stamp.channels={true,false,false,false,false};picker_stamp.flow=.5f;own.pixels.stamp(128,picker_stamp,set.tiles);
  if(zero_opacity){LevelsEffect paint;paint.kind=ContentEffectKind::Paint;paint.opacity=.4f;auto effect_stamp=picker_stamp;effect_stamp.values.base_color={.8f,.2f,.1f,1};paint.paint.pixels.stamp(128,effect_stamp,set.tiles);own.effects={paint};}
  preview.begin_frame();GpuMaskStage stage;stage.comparison_layer=own.id;stage.comparison_channel=0;TextureLayer compare_description;compare_description.mask=LayerMask{};MaskEffect compare_effect;compare_effect.kind=MaskEffectKind::Compare;compare_effect.left=0;compare_effect.right=1;compare_effect.compare_channel=0;compare_description.mask->effects={compare_effect};gpu_append_mask_effects(stage.rows,compare_description,set,1001);auto inputs=preview.comparison_inputs(set,stage,preview.impl_->zero.get(),1001);if(!inputs)throw std::runtime_error("Thumbnail isolation comparison missing");const std::array<std::array<int,3>,1> point{{{38,51,0}}};const auto before=probe_gpu_texture((*inputs)[1].image,point).front();
  if(preview.thumbnail(set,{own.id,0,false},++thumbnail_ticket)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Thumbnail isolation deferred");const auto after=probe_gpu_texture((*inputs)[1].image,point).front();if(before!=after)throw std::runtime_error("Thumbnail overwrote live Compare input");
  const auto started=performance::Clock::now();while(preview.impl_->thumbnails.pending()){preview.impl_->thumbnails.poll();if(performance::ms(started)>3000)throw std::runtime_error("Thumbnail isolation timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}++cases;
 }
#include "GpuDeferredThumbnailTest.inc"
 std::printf("BTS_GPU_THUMBNAIL_VERIFIED cases=%d pixels_per_case=2304 udim=3 channels=8 hidden_zero_opacity_disabled=1 raw_mask=1 same_frame_compare_isolation=1 cpu_composition=0 max_error=%.9g\n",cases-thumbnail_start,thumbnail_maximum);
 // Independent scalar mask prefixes: interleaved paint/point effects, exact
 // ordering, shared across channels, dirty chunks and cached Undo sources.
 set=TextureSet{};set.texture_resolution=512;set.layers.front().kind=LayerKind::Fill;TextureDocument::add_fill(set);
 auto &paint_mask_layer=set.layers.back();paint_mask_layer.values.base_color={.7f,.2f,.4f,1};paint_mask_layer.opacity.fill(.71f);paint_mask_layer.mask=LayerMask{};paint_mask_layer.mask->background=.31f;
 MaskEffect first_paint;first_paint.kind=MaskEffectKind::Paint;first_paint.opacity=.73f;
 auto effect_ink=mask;effect_ink.center={.2,.2};effect_ink.axis_x={.08,0};effect_ink.axis_y={0,.08};first_paint.paint.pixels.stamp(512,effect_ink);
 MaskEffect paint_levels;paint_levels.kind=MaskEffectKind::Levels;paint_levels.levels.gamma=1.3;paint_levels.levels.output_low=-.2;paint_levels.levels.output_high=1.4;paint_levels.levels.clamp=false;
 MaskEffect second_paint;second_paint.kind=MaskEffectKind::Paint;second_paint.opacity=.61f;effect_ink.center={.3,.3};effect_ink.values.roughness=.18;second_paint.paint.pixels.stamp(256,effect_ink);
 paint_mask_layer.mask->effects={first_paint,paint_levels,second_paint};target=texture("BTS GPU mask paint effects",512,512,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(int blend=0;blend<=28;++blend){paint_mask_layer.mask->effects[2].blending=LayerBlend(blend);check(0);check(2);}
 paint_mask_layer.mask->effects[2].blending=LayerBlend::Normal;check(0);const auto shared_work=preview.impl_->mask_dispatch_texels;check(1);check(2);check(4);
 if(preview.impl_->mask_dispatch_texels!=shared_work)throw std::runtime_error("Mask prefixes duplicated per channel");
 const auto saved_mask_effects=paint_mask_layer.mask->effects;auto &last_paint=paint_mask_layer.mask->effects[2];effect_ink.center={.05,.05};effect_ink.axis_x={.01,0};effect_ink.axis_y={0,.01};last_paint.paint.pixels.stamp(256,effect_ink);
 const auto dirty_before=preview.impl_->mask_dispatch_texels;check(0);const auto dirty_mask=preview.impl_->mask_dispatch_texels-dirty_before;
 if(dirty_mask>256*256||dirty_mask==0)throw std::runtime_error("Mask paint recomputed unchanged lower prefixes/chunks");
 paint_mask_layer.mask->effects=saved_mask_effects;check(0);
 paint_mask_layer.mask->effects[0].enabled=false;check(0);paint_mask_layer.mask->effects[0].enabled=true;check(0);
 std::swap(paint_mask_layer.mask->effects[0],paint_mask_layer.mask->effects[2]);check(0);
 paint_mask_layer.mask->effects[0].paint.pixels={};check(0);paint_mask_layer.mask->effects.erase(paint_mask_layer.mask->effects.begin());check(0);
 paint_mask_layer.mask.reset();check(0);
 for(const auto &[key,entry]:preview.impl_->mask_stacks)if(key.starts_with(set.id.toString().toStdString()))throw std::runtime_error("Deleted mask retained GPU prefixes");
 std::printf("BTS_GPU_MASK_PAINT_VERIFIED modes=29 interleaved_levels=1 resampled=1 shared_channels=4 dirty_texels=%llu undo_reorder_clear=1 production_readback=0\n",(unsigned long long)dirty_mask);

 // Atom masks use their own wrap/filter contract (different from fill maps),
 // both inline and between cached paint-effect prefixes, including UDIM offsets.
 set=TextureSet{};set.texture_resolution=128;set.layers.front().kind=LayerKind::Fill;TextureDocument::add_fill(set);
 auto &atom_layer=set.layers.back();atom_layer.values.base_color={.17f,.48f,.73f,1};atom_layer.opacity.fill(.71f);atom_layer.mask=LayerMask{};atom_layer.mask->background=.43;
 MaskEffect atom_mask;atom_mask.kind=MaskEffectKind::Fill;atom_mask.fill=.77f;atom_mask.opacity=.83f;atom_mask.blending=LayerBlend::Multiply;
 atom_mask.mapping.projection.atom=true;atom_mask.mapping.projection.scale={2.3,.7};atom_mask.mapping.projection.offset={-.2,.3};atom_mask.mapping.projection.rotation=33;
 target=texture("BTS GPU Atom mask oracle",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(bool staged:{false,true})for(int wrap=0;wrap<3;++wrap)for(int filter=0;filter<2;++filter)for(bool invert:{false,true}){
  atom_mask.mapping.wrap=wrap;atom_mask.mapping.filtering=filter;atom_mask.mapping.projection.invert=invert;atom_layer.mask->effects={atom_mask};
  if(staged)atom_layer.mask->effects.insert(atom_layer.mask->effects.begin(),first_paint);
  for(int channel:{0,2})check(channel);
 }
 set.tiles={1001,1002,1011};atom_layer.mask->effects={first_paint,atom_mask,paint_levels};
 for(int tile:set.tiles){preview.begin_frame();if(preview.update(set,0,target.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU UDIM mask did not execute");++cases;}
 atom_layer.mask->effects[1].mapping.projection.physical=true;set.cm_per_native=100;set.native_per_uv={1.4,.6};check(0);
 std::printf("BTS_GPU_MASK_ATOM_VERIFIED wrap=repeat,clamp,none filters=bilinear,nearest inline_and_paint_prefix=1 tiles=1001,1002,1011 physical=1\n");
 // Reuse the same shader projection contract for mask Fill sources.
 set=TextureSet{};set.texture_resolution=128;set.layers.front().kind=LayerKind::Fill;TextureDocument::add_fill(set);set.projection_geometry=atlas;
 auto &spatial_mask_layer=set.layers.back();spatial_mask_layer.values.base_color={.18f,.5f,.76f,1};spatial_mask_layer.values.roughness=.2;spatial_mask_layer.mask=LayerMask{};spatial_mask_layer.mask->background=.8;
 MaskEffect spatial_mask;spatial_mask.kind=MaskEffectKind::Fill;spatial_mask.fill=.7;spatial_mask.opacity=.8;spatial_mask.blending=LayerBlend::Multiply;
 spatial_mask.mapping.projection.rotation3={13,21,-9};spatial_mask.mapping.projection.scale3={1.3,.8,.7};spatial_mask.mapping.projection.scale={1.4,.9};spatial_mask.mapping.projection.offset={.17,-.21};spatial_mask.mapping.projection.backface_culling=true;
 for(int mode=1;mode<=4;++mode)for(bool atom:{false,true})for(int filter:{0,1,2}){
  spatial_mask.mapping.projection.mode=mode;spatial_mask.mapping.projection.atom=atom;spatial_mask.mapping.projection.filtering=filter;
  spatial_mask_layer.mask->effects={first_paint,spatial_mask,paint_levels};for(int c:{0,2})check(c);
 }
 spatial_mask_layer.mask->effects={spatial_mask};check(0);spatial_mask_layer.mask->effects[0].mapping.projection.physical=true;set.cm_per_native=100;check(0);
 auto mask_atlas=std::make_shared<FillSurfaceAtlas>(*atlas);mask_atlas->tiles[1001][0].normal={0,0,0};mask_atlas->tiles[1002]=mask_atlas->tiles.at(1001);set.projection_geometry=mask_atlas;set.tiles={1001,1002};spatial_mask_layer.mask->effects[0].mapping.projection.mode=1;spatial_mask_layer.mask->effects[0].mapping.projection.atom=false;
 for(int tile:set.tiles){preview.begin_frame();if(preview.update(set,0,target.get(),tile)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU spatial mask UDIM did not execute");++cases;}
 spatial_mask_layer.mask->effects={first_paint,spatial_mask};spatial_mask_layer.mask->effects[1].blending=LayerBlend::Disabled;set.projection_geometry.reset();check(0);
 std::printf("BTS_GPU_MASK_SPATIAL_VERIFIED modes=triplanar,planar,spherical,cylindrical atom_uniform=1 filters=3 paint_prefix=1 physical=1 udim=1 disabled_no_geometry=1\n");
 // Live anchors refer to earlier mask prefixes, including hidden/disabled
 // producer masks. They clamp the exposed value without clamping its stream.
 set=TextureSet{};set.texture_resolution=512;set.layers.clear();TextureLayer anchor_producer;anchor_producer.kind=LayerKind::Fill;anchor_producer.visible=false;anchor_producer.mask=LayerMask{};anchor_producer.mask->background=.2;anchor_producer.mask->enabled=false;
 MaskEffect anchor_a;anchor_a.kind=MaskEffectKind::Anchor;MaskEffect anchor_b=anchor_a;anchor_b.id=QUuid::createUuid();
 MaskEffect after_anchor;after_anchor.kind=MaskEffectKind::Filter;after_anchor.filter.resource=1;
 anchor_producer.mask->effects={first_paint,anchor_a,after_anchor};set.layers.push_back(anchor_producer);
 TextureLayer anchor_middle;anchor_middle.kind=LayerKind::Fill;anchor_middle.values.base_color={.7f,.2f,.1f,1};anchor_middle.opacity.fill(.65);anchor_middle.mask=LayerMask{};
 MaskEffect reference_a;reference_a.kind=MaskEffectKind::Fill;reference_a.anchor_source=anchor_a.id;reference_a.opacity=.73;
 anchor_middle.mask->effects={reference_a,anchor_b,paint_levels};set.layers.push_back(anchor_middle);
 TextureLayer anchor_top;anchor_top.kind=LayerKind::Fill;anchor_top.values.base_color={.1f,.3f,.8f,1};anchor_top.opacity.fill(.7);anchor_top.mask=LayerMask{};
 MaskEffect reference_b=reference_a;reference_b.id=QUuid::createUuid();reference_b.anchor_source=anchor_b.id;reference_b.blending=LayerBlend::Multiply;
 anchor_top.mask->effects={reference_a,reference_b};set.layers.push_back(anchor_top);
 target=texture("BTS GPU live mask anchors",512,512,gpu::TextureFormat::SFLOAT_16_16_16_16);check(0);
 const auto anchored_work=preview.impl_->mask_dispatch_texels;check(2);check(4);if(preview.impl_->mask_dispatch_texels!=anchored_work)throw std::runtime_error("Anchor producers were recomputed per channel");
 set.layers[0].mask->effects[2].filter.resource=2;check(0);if(preview.impl_->mask_dispatch_texels!=anchored_work)throw std::runtime_error("Post-anchor change invalidated its consumers");
 auto anchor_before=set.layers[0].mask->effects[0].paint.pixels;auto anchor_ink=mask;anchor_ink.center={.05,.05};anchor_ink.axis_x={.01,0};anchor_ink.axis_y={0,.01};set.layers[0].mask->effects[0].paint.pixels.stamp(512,anchor_ink);
 const auto anchor_dirty_start=preview.impl_->mask_dispatch_texels;check(0);const auto anchor_dirty=preview.impl_->mask_dispatch_texels-anchor_dirty_start;
 if(anchor_dirty==0||anchor_dirty>=512*512*8)throw std::runtime_error("Anchor edit did not propagate sparse dirty chunks");
 set.layers[0].mask->effects[0].paint.pixels=anchor_before;check(0);
 set.layers[0].mask->effects[1].enabled=false;check(0);set.layers[0].mask->effects[1].enabled=true;check(0);
 set.layers[0].mask->background=1;set.layers[0].mask->effects.insert(set.layers[0].mask->effects.begin(),paint_levels);check(0);set.layers[0].mask->enabled=true;check(0);set.layers[0].visible=true;check(0);
 const auto producer_saved=set.layers[0];set.layers[0].mask.reset();check(0);set.layers[0]=producer_saved;check(0);
 set.layers[2].mask->effects[0].anchor_source=QUuid::createUuid();check(0);set.layers[2].mask->effects[0].anchor_source=anchor_a.id;check(0);
 std::swap(set.layers[0],set.layers[2]);check(0);std::swap(set.layers[0],set.layers[2]);check(0);
 std::printf("BTS_GPU_MASK_ANCHORS_VERIFIED nested=1 shared_channels=3 hidden_disabled_producer=1 post_anchor_unchanged=1 dirty_texels=%llu undo_missing_forward_reorder=1 clamped_reference=1\n",(unsigned long long)anchor_dirty);
 // Isolation reuses the exact cached mask stream, even on disabled/hidden layers.
 for(int i:{0,1,2,0}){set.selected_layer=set.layers[i].id;set.layers[i].mask->enabled=false;check(15);}
 check(15);if(preview.wrote_image())throw std::runtime_error("Unchanged mask inspection uploaded its image");
 set.layers[0].mask->effects.back().enabled=false;check(15);set.layers[0].mask->effects.back().enabled=true;check(15);
 set.selected_layer=set.layers[0].id;check(15);const auto isolation_before=preview.impl_->mask_dispatch_texels;
 set.layers[0].mask->effects[1].paint.pixels.stamp(512,anchor_ink);check(15);
 const auto isolation_dirty=preview.impl_->mask_dispatch_texels-isolation_before;
 if(!isolation_dirty||isolation_dirty>256*256*4)throw std::runtime_error("Mask inspection lost sparse prefix reuse");
 set.layers[0].mask->effects.clear();set.layers[0].mask->background=.37;check(15);
 set.layers[0].mask->effects={first_paint,atom_mask,paint_levels};set.tiles={1001,1002,1011};set.texture_resolution=128;
 target=Texture(GPU_texture_create_2d_array("BTS isolated masks atlas",384,128,1,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
 const std::map<int,std::array<int,3>> isolated_regions{{1001,{0,0,0}},{1002,{128,0,0}},{1011,{256,0,0}}};
 preview.begin_frame();if(preview.update_tiles(set,15,target.get(),isolated_regions)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Isolated mask UDIM atlas did not execute");++cases;
 set.projection_geometry=mask_atlas;set.layers[0].mask->effects={spatial_mask};set.tiles={1001,1002};
 preview.begin_frame();if(preview.update_tiles(set,15,target.get(),{{1001,{0,0,0}},{1002,{128,0,0}}})!=GpuPreviewUpdate::Updated)throw std::runtime_error("Spatial mask isolation did not execute");++cases;
 std::printf("BTS_GPU_MASK_ISOLATION_VERIFIED disabled_hidden=1 selection=1 unchanged_upload=0 dirty_texels=%llu uniform_paint_levels_atom_spatial_anchors=1 udim_atlas=1 production_readback=0\n",(unsigned long long)isolation_dirty);
#include "GpuContentAnchorTests.inc"
#include "GpuFillAnchorTests.inc"
#include "GpuGeneratorAnchorTests.inc"
 // Temporary Quick Mask overlay consumes the original scalar pages on GPU.
 {TextureSet original;original.texture_resolution=512;original.tiles={1001,1002,1011};original.layers[0].kind=LayerKind::Fill;original.layers[0].values.base_color={.7f,.3f,.1f,1};
  QuickMaskField field;field.resolution=256;auto stripe=ink;stripe.channels={false,false,true,false,false};stripe.values.roughness=0;stripe.center={.45,.5};stripe.axis_x={.04,0};stripe.axis_y={0,.3};field.pixels.stamp(256,stripe,original.tiles);stripe.center={1.6,.5};field.pixels.stamp(256,stripe,original.tiles);
  for(int width:{128,200,512})for(bool editing:{false,true})for(bool inverted:{false,true}){original.texture_resolution=width;original.texture_height=width==200?203:0;field.inverted=inverted;set=quick_mask_preview(original,&field,editing);target=texture("BTS quick mask virtual source",width,set.rows(),gpu::TextureFormat::SFLOAT_32_32_32_32);for(int tile:original.tiles){preview.begin_frame();if(preview.update_tile(set,0,target.get(),tile,{})!=GpuPreviewUpdate::Updated)throw std::runtime_error("Quick mask GPU tile deferred");++cases;}}
  original.texture_resolution=1024;original.texture_height=0;field.inverted=false;set=quick_mask_preview(original,&field,true);target=texture("BTS quick mask dirty prefix",1024,1024,gpu::TextureFormat::SFLOAT_32_32_32_32);check(0);check(0);if(preview.impl_->last_dispatch_texels||preview.impl_->remaining!=upload_budget)throw std::runtime_error("Quick mask unchanged work");
  const auto saved=field;stripe.center={.249,.249};stripe.axis_x={.006,0};stripe.axis_y={0,.006};{PaintRecordingScope record;field.pixels.stamp(256,stripe,original.tiles);}set=quick_mask_preview(original,&field,true);check(0);const auto dirty=preview.impl_->last_dispatch_texels;if(!dirty||dirty>=1024*1024*2)throw std::runtime_error("Quick mask did not retain lower prefix");set=quick_mask_preview(original,&saved,true);check(0);
  set=original;check(0);set=quick_mask_preview(original,nullptr,true);check(0);set=quick_mask_preview(original,nullptr,false);check(0);
  std::printf("BTS_GPU_QUICK_MASK_VERIFIED tiles=3 virtual_256_source=1 full_resolution_target=1 inversion=1 dirty_texels=%llu unchanged_upload=0 undo=1 production_readback=0\n",(unsigned long long)dirty);
 }
 // GPU bounds/presence use dirty chunks and a fenced 16-byte result, never pixels.
 {
  GpuRangeSummary ranges;GpuHistogram histograms;std::map<std::uint32_t,std::uint64_t> displacement_epochs;std::vector<float> a(512*512*4),b(512*256*4);
  for(std::size_t i=0;i<a.size();i+=4){a[i]=.25f;a[i+3]=1;}a[40*4]=-.001f;
  b[10*4]=2;b[10*4+3]=1;b[300*4]=.7;b[300*4+3]=1;b[11*4]=200; // Zero coverage must not affect bounds.
  auto first=texture("BTS range source A",512,512,gpu::TextureFormat::SFLOAT_32_32_32_32);auto second=texture("BTS range source B",512,256,gpu::TextureFormat::SFLOAT_32_32_32_32);
  GPU_texture_update(first.get(),GPU_DATA_FLOAT,a.data());GPU_texture_update(second.get(),GPU_DATA_FLOAT,b.data());
  std::vector<GpuRangeSummary::Source> sources{{23,512,512,first.get(),1,{}},{1002,512,256,second.get(),1,{}}};sources[0].tile=1001;
  const auto wait_range=[&](){const auto start=performance::Clock::now();std::vector<GpuRangeSummary::Result> result;while(ranges.pending()){auto ready=ranges.poll();result.insert(result.end(),ready.begin(),ready.end());if(performance::ms(start)>3000)throw std::runtime_error("GPU range metadata timed out");std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(result.empty())throw std::runtime_error("Missing async GPU range");return result.back();};
  ranges.update(23,sources);auto range=wait_range();if(!range.present||std::abs(range.minimum+.001f)>1e-7||range.maximum!=2)throw std::runtime_error("Signed GPU range/coverage incorrect");
  const auto full=ranges.reduced_texels;ranges.update(23,sources);if(ranges.pending()||ranges.reduced_texels!=full)throw std::runtime_error("Unchanged GPU range was recomputed");
  std::vector<float> dirty(256*256*4);GPU_texture_update_sub(second.get(),GPU_DATA_FLOAT,dirty.data(),0,0,0,256,256,1);sources[1].version=2;sources[1].dirty={0};ranges.update(23,sources);
  for(std::size_t i=0;i<dirty.size();i+=4){dirty[i]=-.5;dirty[i+3]=1;}GPU_texture_update_sub(second.get(),GPU_DATA_FLOAT,dirty.data(),0,0,0,256,256,1);sources[1].version=3;ranges.update(23,sources);range=wait_range();
  if(range.minimum!=-.5f||std::abs(range.maximum-.7f)>1e-7||ranges.discarded!=1||ranges.reduced_texels-full!=2*256*256)throw std::runtime_error("GPU dirty range/coalescing mismatch");
  sources.resize(1);std::fill(a.begin(),a.end(),0);GPU_texture_update(first.get(),GPU_DATA_FLOAT,a.data());sources[0].version=2;sources[0].dirty={0,1,2,3};ranges.update(23,sources);range=wait_range();if(range.present)throw std::runtime_error("GPU range clear retained coverage");
  const auto before_epoch=ranges.reduced_texels;ranges.update(23,sources,7);range=wait_range();if(range.epoch!=7||ranges.reduced_texels!=before_epoch)throw std::runtime_error("GPU range CPU-save epoch transition lost metadata");
  std::printf("BTS_GPU_RANGE_VERIFIED tiles=2 signed_unbounded=1 zero_alpha_ignored=1 dirty_texels=%llu discarded_stale=%llu metadata_bytes=%llu unchanged_dispatch=0 pixel_readback=0\n",(unsigned long long)(2*256*256),(unsigned long long)ranges.discarded,(unsigned long long)ranges.readback_bytes);cases+=5;
 }
 // Per-effect GPU source prefixes preserve coverage, encoded normals and ordering.
 for(auto parent_kind:{LayerKind::Fill,LayerKind::Paint,LayerKind::Folder}){
  set=TextureSet{};set.texture_resolution=128;set.channels.fill(true);set.ao_channel=true;set.displacement.channel=true;set.users[0].present=true;
  set.layers.clear();TextureLayer lower;lower.kind=LayerKind::Fill;lower.values.base_color={.2f,.3f,.1f,1};lower.opacity.fill(.43);set.layers.push_back(lower);
  if(parent_kind==LayerKind::Folder){auto child=lower;child.id=QUuid::createUuid();child.depth=1;child.values.base_color={.6f,.1f,.3f,1};set.layers.push_back(child);}
  TextureLayer painted;painted.kind=parent_kind;painted.values.base_color={.3f,.7f,.4f,1};painted.values.bump=-.2;painted.values.roughness=.61;painted.opacity.fill(.71);painted.ao_enabled=true;painted.ao_opacity=.6;painted.values.ambient_occlusion=.7;painted.displacement.enabled=true;painted.displacement.value=-.3;painted.displacement.opacity=.7;painted.users[0].enabled=true;painted.users[0].value=.2;
  auto content_ink=effect_ink;content_ink.center={.5,.5};content_ink.axis_x={.35,0};content_ink.axis_y={0,.35};content_ink.channels.fill(true);content_ink.values.base_color={.7f,.1f,.4f,1};content_ink.values.bump=-.13;content_ink.values.normal={.2f,.3f,.93f};content_ink.values.roughness=.28;
  painted.pixels.stamp(128,content_ink);painted.ao_pixels.stamp(128,content_ink);painted.users[0].pixels.stamp(128,content_ink);painted.displacement.pixels.stamp(128,content_ink);set.layers.push_back(painted);
  target=texture("BTS GPU content paint effects",128,128,gpu::TextureFormat::SFLOAT_32_32_32_32);
  for(int c:{0,1,2,4,5,6,7,16}){
   auto &layer=set.layers.back();const int effect_channel=gpu_layer_channel(layer,c).fill_channel;
   LevelsEffect pre;pre.channel=effect_channel;pre.gamma=1.2;pre.opacity=.83;
   LevelsEffect ink;ink.kind=ContentEffectKind::Paint;ink.channel=0;ink.opacity=.67;ink.paint.pixels=painted.pixels;ink.paint.ao=painted.ao_pixels;ink.paint.users[0]=painted.users[0].pixels;ink.paint.displacement=painted.displacement.pixels;
   LevelsEffect post=pre;post.id=QUuid::createUuid();post.output_low=-.1;post.output_high=1.2;post.clamp=false;
   layer.effects={pre,ink,post};
   for(int mode=0;mode<=28;++mode){layer.effects[1].blending=LayerBlend(mode);try{check(c);}catch(...){std::fprintf(stderr,"BTS_GPU_CONTENT_PAINT_FAILURE parent=%d channel=%d paint_blend=%d pre_opacity=%.9g post_opacity=%.9g\n",int(parent_kind),c,mode,pre.opacity,post.opacity);throw;}}
   layer.effects[1].paint={};layer.effects[1].blending=LayerBlend::Replace;check(c);
   layer.effects[1]=ink;std::swap(layer.effects[0],layer.effects[1]);check(c);layer.effects[0].enabled=false;check(c);
   layer.effects={pre,ink,post};for(auto mode:{LayerBlend::Passthrough,LayerBlend::Replace}){if(c<5)layer.blending[c]=mode;else if(c==6)layer.blending[3]=mode;else if(c==5)layer.ao_blending=mode;else if(c==16)layer.displacement.blending=mode;else layer.users[0].blending=mode;check(c);}
  }
 }
 set.texture_resolution=512;set.layers.back().kind=LayerKind::Fill;set.layers.back().effects.clear();LevelsEffect sparse_content;sparse_content.kind=ContentEffectKind::Paint;sparse_content.channel=0;set.layers.back().effects={sparse_content};target=texture("BTS sparse content effect",512,512,gpu::TextureFormat::SFLOAT_32_32_32_32);check(0);
 auto content_saved=set.layers.back().effects;auto content_dab=effect_ink;content_dab.center={.05,.05};content_dab.axis_x={.01,0};content_dab.axis_y={0,.01};content_dab.channels.fill(true);set.layers.back().effects[0].paint.pixels.stamp(512,content_dab);check(0);const auto content_dirty=preview.impl_->last_dispatch_texels;
 if(content_dirty==0||content_dirty>=512*512*3)throw std::runtime_error("Paint effect invalidated unchanged source prefixes");
 set.layers.back().effects=content_saved;check(0);check(0);if(preview.wrote_image())throw std::runtime_error("Unchanged Paint effect wrote image");
 std::printf("BTS_GPU_CONTENT_PAINT_VERIFIED parents=fill,paint,folder channels=8 blend_modes=29 levels_order=1 empty_replace=1 passthrough=1 undo=1 dirty_texels=%llu unchanged_upload=0 production_readback=0\n",(unsigned long long)content_dirty);
 set=TextureSet{};set.texture_resolution=128;set.ao_channel=true;set.base_values.ambient_occlusion=.8;set.layers.front().kind=LayerKind::Fill;set.layers.front().values.ambient_occlusion=.37;set.layers.front().ao_opacity=.6;set.layers.front().mask=LayerMask{};set.layers.front().mask->background=.7;TextureDocument::add_fill(set);auto &ao_top=set.layers.back();ao_top.values.ambient_occlusion=.68;ao_top.ao_opacity=.43;
 target=texture("BTS authored and baked AO reference",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);
 for(int mode=0;mode<=28;++mode){ao_top.ao_blending=LayerBlend(mode);check(5);}
 ao_top.ao_blending=LayerBlend::Normal;auto baked_values=std::vector<float>(128*128*4);for(int y=0;y<128;++y)for(int x=0;x<128;++x){const auto i=(y*128+x)*4;const float alpha=x<64?.3:1;for(int c=0;c<3;++c)baked_values[i+c]=(.2f+.6f*y/128)*alpha;baked_values[i+3]=alpha;}
 auto baked=std::make_shared<MeshMapPixels>(128,std::move(baked_values));MeshMap map;map.name="AO";map.tiles[1001]={128,baked->hash,baked};set.mesh_maps[3]=map;
 for(int mode=0;mode<2;++mode){set.ao_mixing=AoMixing(mode);for(bool enabled:{false,true}){set.ao_channel=enabled;check(5);}}
 set.ao_channel=true;set.layers.clear();TextureLayer ao_paint;ao_paint.ao_enabled=true;ao_paint.kind=LayerKind::Paint;set.layers.push_back(ao_paint);set.tiles={1001,1002};set.mesh_maps[3]->tiles[1002]={128,baked->hash,baked};auto ao_ink=mask;ao_ink.values.roughness=.25;ao_ink.center={1.5,.5};set.layers.front().ao_pixels.stamp(128,ao_ink,{1002});
 target=Texture(GPU_texture_create_2d_array("BTS cross-tile AO atlas",256,128,1,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);const std::map<int,std::array<int,3>> ao_regions{{1001,{0,0,0}},{1002,{128,0,0}}};
 for(auto precision:{ChannelPrecision::L8,ChannelPrecision::L16F,ChannelPrecision::L32F}){set.precision[5]=precision;for(int variant=0;variant<3;++variant){set.ao_mixing=variant==0?AoMixing::Multiply:AoMixing::Replace;set.layers.front().visible=variant!=2;preview.begin_frame();if(preview.update_tiles(set,5,target.get(),ao_regions)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU cross-tile AO did not execute");++cases;}}
 std::printf("BTS_GPU_AO_VERIFIED baked_authored=1 modes=29 tiles=1001,1002 global_presence=1 float32=1\n");
 set=TextureSet{};set.texture_resolution=128;set.channels[3]=true;set.channels[4]=true;set.layers.front().kind=LayerKind::Fill;set.layers.front().values.normal=normalized_normal({.3f,-.2f,.9f});set.layers.front().values.bump=.002;set.layers.front().opacity[3]=.6;
 target=texture("BTS derived normal reference",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);for(bool bump:{false,true}){set.channels[4]=bump;check(3);}
 set.channels[4]=true;set.layers.front().kind=LayerKind::Paint;set.tiles={1001,1002,1011};set.tile_resolutions[1002]=256;
 for(int tile:set.tiles){const int w=set.width(tile),h=set.rows(tile);std::vector<float> heights(std::size_t(w)*h*4),normals(heights.size()),bake_values(heights.size());for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto i=(std::size_t(y)*w+x)*4;const float alpha=x>w/4?.73f:0.f;const float height_value=.0002f*((x+.5f)/w+(tile-1001)%10)-.0001f*((y+.5f)/h+(tile-1001)/10);for(int c=0;c<3;++c){heights[i+c]=height_value;normals[i+c]=set.layers.front().values.normal[c]*alpha;bake_values[i+c]=(c==0?.63f:c==1?.42f:.98f)*(y>h/4?.4f:0.f);}heights[i+3]=1;normals[i+3]=alpha;bake_values[i+3]=y>h/4?.4f:0.f;}set.layers.front().pixels.assign_channel(w,4,heights,tile);set.layers.front().pixels.assign_channel(w,3,normals,tile);auto pixels=std::make_shared<MeshMapPixels>(w,std::move(bake_values),h);if(!set.mesh_maps[0]){MeshMap map;map.name="Normal";set.mesh_maps[0]=map;}set.mesh_maps[0]->tiles[tile]={w,pixels->hash,pixels,h};}
 target=Texture(GPU_texture_create_2d_array("BTS derived normal UDIM atlas",512,256,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);const std::map<int,std::array<int,3>> normal_regions{{1001,{0,0,0}},{1002,{128,0,0}},{1011,{0,0,1}}};set.precision[3]=ChannelPrecision::RGB32F;set.precision[4]=ChannelPrecision::L32F;
 for(int method=0;method<2;++method)for(int mixing=0;mixing<2;++mixing)for(bool dx:{false,true}){set.bump_method=BumpMethod(method);set.normal_mixing=NormalMixing(mixing);set.normal_format=dx?NormalMapFormat::DirectX:NormalMapFormat::OpenGL;preview.begin_frame();if(preview.update_tiles(set,3,target.get(),normal_regions)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU derived normal did not execute");++cases;}
 set.channels[3]=false;preview.begin_frame();if(preview.update_tiles(set,3,target.get(),normal_regions)!=GpuPreviewUpdate::Updated)throw std::runtime_error("GPU baked-only normal did not execute");++cases;
 std::printf("BTS_GPU_DERIVED_NORMAL_VERIFIED baked_authored_bump=1 tiles=1001,1002,1011 resolutions=128,256 global_presence=1 sharp_sobel_directx=1 float32=1\n");
 {
  TextureSet dirty_set;dirty_set.texture_resolution=512;dirty_set.tiles={1001,1002,1011};dirty_set.channels[3]=dirty_set.channels[4]=true;dirty_set.ao_channel=true;
  dirty_set.precision[3]=ChannelPrecision::RGB32F;dirty_set.precision[4]=dirty_set.precision[5]=ChannelPrecision::L32F;
  dirty_set.base_values.normal=normalized_normal({.2f,.3f,.9f});dirty_set.base_values.ambient_occlusion=.7f;dirty_set.ao_mixing=AoMixing::Replace;dirty_set.normal_mixing=NormalMixing::Replace;
  auto &paint=dirty_set.layers.front();paint.kind=LayerKind::Paint;paint.channels[3]=paint.channels[4]=paint.ao_enabled=true;
  for(int role:{0,3}){MeshMap map;map.name=role==0?"Normal":"AO";std::vector<float> values(32*32*4);for(int i=0;i<32*32;++i){values[i*4]=role==0?.65f:.4f;values[i*4+1]=role==0?.4f:.4f;values[i*4+2]=role==0?.98f:.4f;values[i*4+3]=1;}auto pixels=std::make_shared<MeshMapPixels>(32,std::move(values));for(int tile:dirty_set.tiles)map.tiles[tile]={32,pixels->hash,pixels};dirty_set.mesh_maps[role]=std::move(map);}
  GpuLayerPreview dirty_preview;
  Texture normal{GPU_texture_create_2d_array("BTS dirty normal tiles",512,512,3,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};
  Texture ao{GPU_texture_create_2d_array("BTS dirty AO tiles",512,512,3,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};
  const std::map<int,std::array<int,3>> regions{{1001,{0,0,0}},{1002,{0,0,1}},{1011,{0,0,2}}};bool partial_normal=false,partial_ao=false;
  for(int step=0;step<4;++step){
   if(step==3){paint.pixels={};paint.ao_pixels={};}
   else if(step){auto dab=ink;dab.center=step==1?std::array<double,2>{.99,.5}:std::array<double,2>{.25,.25};dab.axis_x={.025,0};dab.axis_y={0,.025};dab.channels={false,false,false,true,true};dab.values.normal=normalized_normal({-.3f,.4f,.8f});dab.values.bump=.002f;paint.pixels.stamp(512,dab,dirty_set.tiles);dab.channels={false,false,true,false,false};dab.values.roughness=.2f;paint.ao_pixels.stamp(512,dab,dirty_set.tiles);}
   for(int c:{3,5}){
    auto *destination=c==3?normal.get():ao.get();dirty_preview.begin_frame();
    if(dirty_preview.update_tiles(dirty_set,c,destination,regions)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Dirty derived tiles did not execute");
    const auto changed=dirty_preview.impl_->last_derived_chunks,total=dirty_preview.impl_->last_derived_total;
    if(step==2){if(c==3)partial_normal=changed>0&&changed<total;else partial_ao=changed>0&&changed<total;}
    if(c==5){CompositeChannels wanted{};wanted[5]=true;std::atomic_bool cancel=false;const auto reference=native_composite_job(dirty_set,true,nullptr,1,cancel,false,wanted);auto *actual=static_cast<float *>(GPU_texture_read(destination,GPU_DATA_FLOAT,0));for(const auto &[tile,offset]:regions){const auto &expected=reference->tiles.at(tile)[5];for(std::size_t i=0;i<expected.size();++i)if(std::abs(actual[std::size_t(offset[2])*512*512*4+i]-expected[i])>1e-6)throw std::runtime_error("Dirty AO/global coverage differs from CPU");}MEM_delete(actual);}
    dirty_preview.begin_frame();if(dirty_preview.update_tiles(dirty_set,c,destination,regions)!=GpuPreviewUpdate::Updated||dirty_preview.wrote_image())throw std::runtime_error("Unchanged derived channel was recomputed");++cases;
   }
  }
  if(!partial_normal||!partial_ao)throw std::runtime_error("Derived channels ignored dirty chunk boundaries");
  std::printf("BTS_GPU_DERIVED_DIRTY_VERIFIED tiles=1001,1002,1011 size=512 partial_normal=1 partial_ao=1 cross_tile_halo=1 global_coverage_transition=1 unchanged_dispatch=0\n");
 }
 {
  SceneSnapshot scene;MaterialSummary material;material.session_id=31;scene.materials.push_back(material);scene.minimum={0,-1,0};scene.maximum={1,1,0};
  UvFaceTriangle first;first.material=0;first.vertex_ids={0,1,2};first.positions={{{0,0,0},{1,0,0},{0,1,0}}};first.corners={{{.1f,.1f},{.45f,.1f},{.1f,.8f}}};first.triangle_id=1;
  UvFaceTriangle second;second.material=0;second.vertex_ids={1,0,3};second.positions={{{1,0,0},{0,0,0},{0,-1,0}}};second.corners={{{.6f,.6f},{.9f,.6f},{.9f,.9f}}};second.triangle_id=2;scene.uv_triangles={first,second};
  TexturePadding padding(scene,31);std::size_t sparse_operations=0;double max_error=0;
  for(int rows:{64,128})for(bool adjacent:{false,true}){
   const auto plan=padding.gpu_margin_plan(128,rows,1001,adjacent);if(plan!=padding.gpu_margin_plan(128,rows,1001,adjacent))throw std::runtime_error("Geometry-only margin plan was not cached");sparse_operations+=plan->operations.size();
   std::vector<float> expected(std::size_t(128)*rows*4);for(std::size_t i=0;i<expected.size();++i)expected[i]=float((i*13)%971)/400-.7f;auto actual=expected;
   auto *buffer=IMB_allocImBuf(128,rows,ImBufFlags::FloatData);std::copy(expected.begin(),expected.end(),buffer->float_data_for_write());padding.apply(*buffer,1001,adjacent);std::copy_n(buffer->float_data(),expected.size(),expected.begin());IMB_freeImBuf(buffer);plan->evaluate_reference(actual);
   for(std::size_t i=0;i<actual.size();++i){max_error=std::max(max_error,double(std::abs(actual[i]-expected[i])));if(std::abs(actual[i]-expected[i])>1e-6)throw std::runtime_error("GPU margin instructions differ from native Blender padding");}++cases;
  }
  if(!sparse_operations)throw std::runtime_error("Margin geometry oracle produced no margin");
  std::printf("BTS_MARGIN_PLAN_VERIFIED extend_adjacent=1 dimensions=128x64,128x128 operations=%zu max_error=%.9g pixels_stored=0\n",sparse_operations,max_error);
  GpuMarginExecutor margins;
 GpuGeometryMask geometry_masks;std::uint64_t initial_samples=0,partial_samples=0;double gpu_error=0;bool remote_update=false;
  for(bool adjacent:{false,true}){
   constexpr int width=512,rows=512;
   const auto plan=padding.gpu_margin_plan(width,rows,1001,adjacent);
   Texture image{GPU_texture_create_2d_array("BTS margin atlas oracle",width+32,rows+16,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};
   const float sentinel[4]={-9,-8,-7,-6};GPU_texture_clear(image.get(),GPU_DATA_FLOAT,sentinel);
   std::vector<float> original(std::size_t(width)*rows*4);for(std::size_t i=0;i<original.size();++i)original[i]=float((i*13)%971)/400-.7f;
   std::vector<float> previous;const std::array<int,3> offset{16,8,1};
   for(int step=0;step<=20;++step){
    const int chunk=step?((step-1)%4):0,left=step?(chunk%2)*256:0,bottom=step?(chunk/2)*256:0,w=step?256:width,h=step?256:rows;
    if(step)for(int y=bottom;y<bottom+h;++y)for(int x=left;x<left+w;++x)for(int c=0;c<4;++c)original[(std::size_t(y)*width+x)*4+c]+=.031f*(c+1);
    std::vector<float> rectangle(std::size_t(w)*h*4);
    for(int y=0;y<h;++y)std::copy_n(original.data()+(std::size_t(y+bottom)*width+left)*4,w*4,rectangle.data()+std::size_t(y)*w*4);
    GPU_texture_update_sub(image.get(),GPU_DATA_FLOAT,rectangle.data(),left+offset[0],bottom+offset[1],1,w,h,1);
    std::vector<std::uint32_t> dirty(4,step?0:1);dirty[chunk]=1;
    if(!margins.apply(adjacent?"adjacent":"extend",plan,image.get(),offset,dirty,true))throw std::runtime_error("Sparse GPU margin dispatch failed");
    if(step)partial_samples+=margins.original_samples_updated();else initial_samples+=margins.original_samples_updated();
    auto *buffer=IMB_allocImBuf(width,rows,ImBufFlags::FloatData);std::copy(original.begin(),original.end(),buffer->float_data_for_write());padding.apply(*buffer,1001,adjacent);
    const auto *expected=buffer->float_data();auto *actual=static_cast<float *>(GPU_texture_read(image.get(),GPU_DATA_FLOAT,0));
    for(int y=0;y<rows;++y)for(int x=0;x<width;++x)for(int c=0;c<4;++c){
     const auto i=(std::size_t(y)*width+x)*4+c,atlas=((std::size_t(rows+16)+y+offset[1])*(width+32)+x+offset[0])*4+c;
     const double difference=std::abs(actual[atlas]-expected[i]);gpu_error=std::max(gpu_error,difference);
     if(difference>1e-6){std::fprintf(stderr,"BTS_GPU_MARGIN_DIFFERENCE adjacent=%d step=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",adjacent,step,x,y,c,expected[i],actual[atlas]);throw std::runtime_error("Sparse GPU margin differs from Blender");}
     if(step&&adjacent&&(x<left||x>=left+w||y<bottom||y>=bottom+h)&&std::abs(expected[i]-previous[i])>1e-6)remote_update=true;
    }
    for(int c=0;c<4;++c)if(actual[c]!=sentinel[c])throw std::runtime_error("GPU margin overwrote another atlas region");
    previous.assign(expected,expected+original.size());MEM_delete(actual);IMB_freeImBuf(buffer);++cases;
   }
  }
  if(!remote_update||partial_samples>=initial_samples*20)throw std::runtime_error("GPU margin dirty donors were not reused across UV seams");
  std::printf("BTS_GPU_MARGIN_VERIFIED extend_adjacent=1 dirty_edits=40 remote_seam=1 original_samples=%llu partial_samples=%llu max_error=%.9g atlas_untouched=1 production_readback=0\n",(unsigned long long)initial_samples,(unsigned long long)partial_samples,gpu_error);
  GpuLayerPreview padded_preview;TextureSet padded_set;padded_set.material_session_uid=31;padded_set.texture_resolution=128;padded_set.channels.fill(true);padded_set.ao_channel=true;padded_set.precision.fill(ChannelPrecision::RGB32F);padded_set.displacement.allocated=padded_set.displacement.channel=true;
  auto &fill=padded_set.layers.front();fill.kind=LayerKind::Fill;fill.channels.fill(true);fill.ao_enabled=true;fill.displacement.enabled=true;fill.values.bump=.013f;fill.values.ambient_occlusion=.37f;fill.fill.texture=resource;fill_channel_texture(fill.fill,4)=signed_texture;fill_channel_texture(fill.fill,14)=signed_texture;fill.opacity.fill(.71f);
  padded_preview.set_padding_geometry(31,{{1001,padding.gpu_margin_plan(128,128,1001,false)}},{{1001,padding.gpu_margin_plan(128,128,1001,true)}});
  auto padded_target=texture("BTS complete padded stack oracle",128,128,gpu::TextureFormat::SFLOAT_32_32_32_32);double stack_error=0;
  for(bool adjacent:{false,true})for(int c:{0,1,2,3,4,5,6,16}){
   padded_set.padding_3d=adjacent;padded_preview.begin_frame();
   if(padded_preview.update(padded_set,c,padded_target.get())!=GpuPreviewUpdate::Updated)throw std::runtime_error("Padded GPU stack was not ready");
   CompositeChannels channels{};channels[c]=true;std::atomic_bool cancelled=false;const auto reference=native_composite_job(padded_set,true,padding.clone(),1,cancelled,false,channels);
   auto *actual=static_cast<float *>(GPU_texture_read(padded_target.get(),GPU_DATA_FLOAT,0));const auto &expected=reference->tiles.at(1001)[c];
   for(std::size_t i=0;i<expected.size();++i){const double error=std::abs(actual[i]-expected[i]);stack_error=std::max(stack_error,error);if(error>(c==3?3e-5:2e-6)){std::fprintf(stderr,"BTS_GPU_PADDED_STACK_DIFFERENCE channel=%d adjacent=%d index=%zu expected=%.9g actual=%.9g\n",c,adjacent,i,expected[i],actual[i]);throw std::runtime_error("Padded GPU stack differs from CPU reference");}}
   MEM_delete(actual);++cases;
  }
  std::printf("BTS_GPU_PADDED_STACK_VERIFIED channels=8 extend_adjacent=1 padded_bump_normal=1 max_error=%.9g\n",stack_error);
 }
 // Content resources own coverage and are channel-local, including empty
 // Paint/Folder sources. Every operation starts with clean sampler bindings.
 {set=TextureSet{};set.texture_resolution=128;set.tiles={1001};set.channels.fill(true);set.ao_channel=true;set.displacement.channel=set.displacement.allocated=true;set.users[0].present=set.users[0].allocated=true;set.precision.fill(ChannelPrecision::RGB32F);set.displacement.precision=ChannelPrecision::L32F;
  for(int role:{1,3,4,5,6}){MeshMap map;map.name="Content generator input";for(int tile:{1001,1002}){std::vector<float> rgba(32*32*4);for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto at=(y*32+x)*4;rgba[at]=(x+.5f)/32;rgba[at+1]=(y+.5f)/32;rgba[at+2]=tile==1001?.8f:.2f;rgba[at+3]=1;}auto pixels=std::make_shared<MeshMapPixels>(32,std::move(rgba));map.tiles[tile]={32,pixels->hash,pixels};}set.mesh_maps[role]=std::move(map);}
  auto &layer=set.layers.front();layer.channels.fill(true);layer.ao_enabled=true;layer.users[0].enabled=true;layer.displacement.enabled=true;target=texture("BTS content generators",128,128,gpu::TextureFormat::SFLOAT_16_16_16_16);const int first=cases;
  for(auto kind:{LayerKind::Paint,LayerKind::Fill,LayerKind::Folder})for(int channel:{0,1,2,4,5,7,16})for(int resource=1;resource<=6;++resource)for(auto mode:{LayerBlend::Normal,LayerBlend::Multiply,LayerBlend::Replace})for(float opacity:{.37f,1.f}){layer.kind=kind;LevelsEffect effect;effect.kind=ContentEffectKind::Generator;effect.channel=gpu_layer_channel(layer,channel).fill_channel;effect.generator=resource;effect.opacity=opacity;effect.blending=mode;effect.light_direction={.3f,.4f,1};effect.map_filter=resource%2;layer.effects={effect};GPU_texture_unbind_all();check(channel);}
  set.tiles.insert(1002);layer.kind=LayerKind::Paint;layer.effects.front().channel=14;layer.effects.front().generator=6;GPU_texture_unbind_all();preview.begin_frame();if(preview.update(set,16,target.get(),1002)!=GpuPreviewUpdate::Updated)throw std::runtime_error("Content generator UDIM oracle deferred");++cases;
  const auto source=set.mesh_maps[1];set.mesh_maps[1].reset();check(16);set.mesh_maps[1]=set.mesh_maps[5];check(16);set.mesh_maps[1]=source;check(16);layer.effects.front().enabled=false;check(16);layer.effects.front().enabled=true;check(16);
  LevelsEffect levels;levels.channel=14;levels.gamma=1.7;layer.effects.push_back(levels);check(16);std::swap(layer.effects[0],layer.effects[1]);check(16);
  std::printf("BTS_GPU_CONTENT_GENERATORS_VERIFIED cases=%d parents=paint,fill,folder channels=7 resources=6 blends=3 opacity=2 missing_rebake=1 udim=1 levels_order=1 clean_bindings=1\n",cases-first);
 }
 cases+=verify_gpu_flatten();
 testing_stack=false;std::printf("BTS_GPU_STACK_SELF_TEST cases=%d blend_mask_texture_paint_versions_precision=pass\n",cases);
}

}
