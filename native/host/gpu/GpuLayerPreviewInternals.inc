// SPDX-License-Identifier: GPL-3.0-or-later
// GpuLayerPreview's internal helpers: transfer scheduling, the GLSL sources of the layer stack and
// their small C++ helpers. GpuLayerPreview.cc includes this inside its anonymous namespace.
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
// MSL wraps these functions as class members, where redeclaration is illegal.
#ifndef GPU_METAL
vec4 generator_map_sample(sampler2D image,vec2 uv,bool linear_filter);
#endif
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
