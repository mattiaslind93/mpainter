#pragma once
#include "PaintPixels.hh"
#include "BrushFootprintBounds.hh"
#include "QuickMask.hh"
#include "GpuBrushMapping.hh"
#include "GPU_capabilities.hh"
#include <map>
#include "Performance.hh"
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "gpu_shader_create_info.hh"
#include "GpuDerivedShaders.hh"
#include "GpuAuthoringProgramTrace.hh"
#include <memory>
#include <span>
#include <bit>
namespace bts {
// Owner-context brush primitives. Only footprint/parameter metadata crosses the
// CPU boundary; coverage, paint accumulation and whole-stroke opacity run on GPU.
class GpuBrushRaster {
  friend class GpuPaintPages;
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  Shader stamp_shader_{nullptr, blender::GPU_shader_free},
      over_shader_{nullptr, blender::GPU_shader_free},
      contact_shader_{nullptr, blender::GPU_shader_free};
  Texture parameters_{nullptr, blender::GPU_texture_free},
      triangles_{nullptr, blender::GPU_texture_free},
      empty_alpha_{nullptr, blender::GPU_texture_free};
  struct AlphaEntry {
    std::shared_ptr<const AlphaBitmap> source;
    Texture image{nullptr, blender::GPU_texture_free};
  };
  std::map<const AlphaBitmap *, AlphaEntry> alphas_;
  std::uint64_t alpha_allocations_ = 0;
  static std::shared_ptr<const AlphaBitmap> stencil_bitmap(const PaintPixels::Stamp &stamp)
  {
    return stamp.stencil && stamp.stencil->stencil.resource &&
                   stamp.stencil->stencil.resource->kind == AlphaKind::Bitmap ?
               stamp.stencil->stencil.resource->bitmap :
               nullptr;
  }
  blender::gpu::Texture *alpha_image(const std::shared_ptr<const AlphaBitmap> &source)
  {
    using namespace blender;
    const bool valid = source && source->width > 0 && source->height > 0 &&
                       source->pixels.size() == std::size_t(source->width) * source->height;
    if (!valid) {
      if (!empty_alpha_) {
        const float zero = 0;
        empty_alpha_ = Texture(GPU_texture_create_2d("BTS empty brush alpha",
                                                     1,
                                                     1,
                                                     1,
                                                     gpu::TextureFormat::SFLOAT_32,
                                                     GPU_TEXTURE_USAGE_GENERAL,
                                                     &zero),
                               GPU_texture_free);
      }
      if (!empty_alpha_)
        throw std::bad_alloc();
      return empty_alpha_.get();
    }
    auto &entry = alphas_[source.get()];
    if (!entry.image) {
      entry.source = source;
      entry.image = Texture(GPU_texture_create_2d("BTS shared brush/stencil alpha",
                                                  source->width,
                                                  source->height,
                                                  1,
                                                  gpu::TextureFormat::SFLOAT_32,
                                                  GPU_TEXTURE_USAGE_GENERAL,
                                                  source->pixels.data()),
                            GPU_texture_free);
      if (!entry.image)
        throw std::bad_alloc();
      ++alpha_allocations_;
    }
    return entry.image.get();
  }
  std::uint64_t metadata_allocations_ = 0;
  static blender::gpu::shader::ShaderCreateInfo program_info(int mode)
  {
    const bool over = mode == 1;
    using namespace blender;
    using namespace gpu::shader;
    ShaderCreateInfo info("pyGPU_Shader");
    info.local_group_size(16, 16);
    info.sampler(0, ImageType::Float2D, "parameters_img")
        .image(0,
               gpu::TextureFormat::SFLOAT_32_32_32_32,
               over ? Qualifier::write : Qualifier::read_write,
               ImageReadWriteType::image2D,
               "result_img")
        .push_constant(Type::int2_t, "region_origin")
        .push_constant(Type::int2_t, "region_size");
    if (!over)
      info.push_constant(Type::int2_t, "page_origin")
          .push_constant(Type::int_t, "paint_size")
          .push_constant(Type::int_t, "command_count");
    if (over)
      info.sampler(1, ImageType::Float2D, "before_img")
          .sampler(2, ImageType::Float2D, "stroke_img");
    else
      info.sampler(1, ImageType::Float2D, "triangles_img")
          .sampler(2, ImageType::Float2D, "alpha_img")
          .sampler(3, ImageType::Float2D, "coverage_img")
          .sampler(4, ImageType::Float2D, "stencil_img")
          .sampler(5, ImageType::Float2D, "payload_img");
    const std::string header = R"GLSL(
int current_stamp=0;
vec4 parameter(int i){return texelFetch(parameters_img,ivec2(i,current_stamp),0);}
)GLSL";
    if (over)
      info.compute_source_generated = header + R"GLSL(
void main(){
 ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,region_size)))return;
 ivec2 p=region_origin+local;vec4 options=parameter(0);
 vec4 ink=texelFetch(stroke_img,min(p,textureSize(stroke_img,0)-1),0);
 if(options.w==0&&options.z==0&&ink.a<=0)return;
 vec4 before=texelFetch(before_img,min(p,textureSize(before_img,0)-1),0);
 if(options.w==2){imageStore(result_img,p,all(equal(before,ink))?vec4(0):options.y!=0?vec4(0,0,0,1):ink);return;}
 if(options.w==1){precise vec4 value=mix(before,ink,options.x);if(value.a<=1e-8)value=vec4(0);imageStore(result_img,p,value);return;}
 precise float alpha=ink.a*options.x;precise vec4 combined=(options.y!=0?vec4(0):ink*options.x)+before*(1-alpha);
 if(combined.a==0)combined=vec4(0);imageStore(result_img,p,combined);
}
)GLSL";
    else {
      const std::string common = header + R"GLSL(
vec4 brush_triangle(int i,int row){ivec2 size=textureSize(triangles_img,0);int columns=size.x/8;return texelFetch(triangles_img,ivec2((row%columns)*8+i,row/columns),0);}
)GLSL" + gpu_brush_mapping_source +
                                 R"GLSL(
float bitmap_alpha(vec2 uv){
 ivec2 size=textureSize(alpha_img,0);vec2 p=(uv+1)*.5*vec2(size)-.5;ivec2 at=ivec2(floor(p));vec2 f=fract(p);
 float a=texelFetch(alpha_img,clamp(at,ivec2(0),size-1),0).r,b=texelFetch(alpha_img,clamp(at+ivec2(1,0),ivec2(0),size-1),0).r;
 float c=texelFetch(alpha_img,clamp(at+ivec2(0,1),ivec2(0),size-1),0).r,d=texelFetch(alpha_img,clamp(at+ivec2(1,1),ivec2(0),size-1),0).r;
 return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}
float brush_alpha(vec2 uv){
 if(any(greaterThan(abs(uv),vec2(1))))return 0;
 vec4 options=parameter(2);float result_value;
 if(options.x==0){float r=length(uv);if(r>=1)return 0;float t=options.z==1?1:clamp((1-r)/(1-options.z),0.0,1.0);result_value=t*t*(3-2*t);}
 else if(options.x==1)result_value=abs(uv.x)<=.22||abs(uv.y)<=.22?1:0;
 else if(options.x==3)result_value=bitmap_alpha(uv);
 else if(options.x==4){vec4 d=parameter(8);vec2 q=vec2(dot(uv,d.xy),uv.y*d.x-uv.x*d.y);float radius=pow(pow(abs(q.x),d.z)+pow(abs(q.y),d.z),1/d.z);float t=clamp((d.w*.5+.008-abs(radius-.75))/.016,0.0,1.0);result_value=t*t*(3-2*t);}
 else result_value=options.w;
 return options.y!=0?1-result_value:result_value;
}
vec4 tri(int i,int row){return brush_triangle(i,row+int(parameter(0).w));}
)GLSL";
      if (mode == 2)
        info.compute_source_generated = common + R"GLSL(
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy),size=imageSize(result_img);if(any(greaterThanEqual(p,size)))return;
 vec2 local=2*(vec2(p)+.5)/vec2(size)-1,uv=parameter(0).xy+local.x*parameter(1).xy+local.y*parameter(1).zw;
 float weight=brush_alpha(local);bool valid=parameter(4).x<0;vec3 world=vec3(0);float nearest=1e30;
 if(!valid){
  for(int n=0;n<int(parameter(4).x);n++){
   vec4 a=tri(0,n),b=tri(2,n),c=tri(4,n);vec3 pa=tri(1,n).xyz,pb=tri(3,n).xyz,pc=tri(5,n).xyz;
   vec2 aa=parameter(4).z!=0?a.zw:vec2(dot(pa,parameter(5).xyz),dot(pa,parameter(6).xyz));
   vec2 bb=parameter(4).z!=0?b.zw:vec2(dot(pb,parameter(5).xyz),dot(pb,parameter(6).xyz));
   vec2 cc=parameter(4).z!=0?c.zw:vec2(dot(pc,parameter(5).xyz),dot(pc,parameter(6).xyz));
   // Evaluate each oriented edge identically on both adjacent triangles.
   // Fused subtraction plus 1-w1-w2 can reject both sides of a shared edge.
   precise vec2 x=bb-aa,y=cc-aa;precise float determinant=x.x*y.y-x.y*y.x;if(abs(determinant)<1e-20)continue;
   precise vec2 ap=aa-local,bp=bb-local,cp=cc-local;
   precise vec3 edges=vec3(bp.x*cp.y-bp.y*cp.x,cp.x*ap.y-cp.y*ap.x,ap.x*bp.y-ap.y*bp.x);
   vec3 w=edges/determinant;
   if(min(min(w.x,w.y),w.z)<-1e-8)continue;
   vec3 point=w.x*pa+w.y*pb+w.z*pc;float depth=dot(point,parameter(7).xyz);
   if(abs(depth)>1&&(parameter(4).z==0||tri(6,n).x!=0))continue;
   if(abs(depth)<nearest){nearest=abs(depth);valid=true;uv=w.x*a.xy+w.y*b.xy+w.z*c.xy;vec4 origin=parameter(21);world=point*origin.w+origin.xyz;}
  }
  if(parameter(4).w!=0)weight*=clamp(1-nearest,0.0,1.0);
 }
 if(valid&&parameter(10).x!=0){vec2 screen;bool mapped=parameter(4).x>=0?mapping_world_screen(world,10,screen):mapping_uv_screen(uv,10,screen);weight*=mapped?mapping_alpha(screen,10):0;}
 imageStore(result_img,p,valid?vec4(uv,weight,1):vec4(0));
}
)GLSL";
      else
        info.compute_source_generated = common + R"GLSL(
float quick_coverage(ivec2 p){
 vec4 options=parameter(9);if(options.x==0)return 1;if(options.z==0)return 0;
 ivec2 output_size=ivec2(paint_size),size=textureSize(coverage_img,0);bvec2 area=bvec2(size.x>=output_size.x&&size.x%output_size.x==0,size.y>=output_size.y&&size.y%output_size.y==0);
 vec2 location=(vec2(p)+.5)*vec2(size)/vec2(output_size)-.5,fraction=fract(location);ivec2 first=ivec2(floor(location)),count=ivec2(2);
 if(area.x){count.x=size.x/output_size.x;first.x=p.x*count.x;}if(area.y){count.y=size.y/output_size.y;first.y=p.y*count.y;}
 precise vec4 sum=vec4(0),correction=vec4(0);
 for(int y=0;y<count.y;y++)for(int x=0;x<count.x;x++){float weight=(area.x?1.0/count.x:(x==0?1-fraction.x:fraction.x))*(area.y?1.0/count.y:(y==0?1-fraction.y:fraction.y));precise vec4 term=texelFetch(coverage_img,clamp(first+ivec2(x,y),ivec2(0),size-1),0)*weight-correction;precise vec4 next=sum+term;correction=(next-sum)-term;sum=next;}
 float allowed=clamp(sum.r+1-sum.a,0.0,1.0);return options.y!=0?1-allowed:allowed;
}

float surface_coverage(vec2 uv,vec2 image_size,out vec2 source_screen){
 source_screen=vec2(0);
 float coverage=0;vec4 options=parameter(4);
 for(int n=0;n<int(options.x);n++){
  vec4 av=tri(0,n),bv=tri(2,n),cv=tri(4,n);vec2 a=av.xy,b=bv.xy,c=cv.xy;
  vec2 lo=min(min(a,b),c)-2/image_size,hi=max(max(a,b),c)+2/image_size;if(any(lessThan(uv,lo))||any(greaterThan(uv,hi)))continue;
  vec2 u=b-a,v=c-a;float determinant=u.x*v.y-u.y*v.x;if(abs(determinant)<1e-20)continue;
  vec3 weights;weights.y=((uv.x-a.x)*v.y-(uv.y-a.y)*v.x)/determinant;weights.z=(u.x*(uv.y-a.y)-u.y*(uv.x-a.x))/determinant;weights.x=1-weights.y-weights.z;
  if(min(min(weights.x,weights.y),weights.z)<-1e-9){
   float nearest=4/(image_size.x*image_size.x);bool found=false;vec3 edges=tri(7,n).xyz;
   for(int edge=0;edge<3;edge++)if(edges[edge]!=0){int next=(edge+1)%3;vec2 start=edge==0?a:edge==1?b:c,end=next==0?a:next==1?b:c,delta=end-start;float squared=dot(delta,delta);if(squared<1e-20)continue;float t=clamp(dot(uv-start,delta)/squared,0.0,1.0);float distance_value=dot(uv-start-t*delta,uv-start-t*delta);if(distance_value<=nearest){nearest=distance_value;weights=vec3(0);weights[edge]=1-t;weights[next]=t;found=true;}}
   if(!found)continue;
  }
  vec3 delta=weights.x*tri(1,n).xyz+weights.y*tri(3,n).xyz+weights.z*tri(5,n).xyz;
  float depth=dot(delta,parameter(7).xyz);if(abs(depth)>1&&(options.z==0||tri(6,n).x!=0))continue;
  vec2 projected=vec2(dot(delta,parameter(5).xyz),dot(delta,parameter(6).xyz));
  if(options.z!=0)projected=weights.x*av.zw+weights.y*bv.zw+weights.z*cv.zw;
  vec2 source_brush=projected;
  if(options.y==0&&parameter(2).x==0)projected=vec2(length(delta),0);
  float alpha=brush_alpha(projected);if(parameter(10).x!=0){vec4 origin=parameter(21);vec2 screen;if(!mapping_world_screen(delta*origin.w+origin.xyz,10,screen))alpha=0;else alpha*=mapping_alpha(screen,10);}
  if(options.w!=0)alpha*=clamp(1-abs(depth),0.0,1.0);
  if(parameter(23).x!=0){vec4 origin=parameter(21);vec2 screen,local_uv;if(!mapping_world_screen(delta*origin.w+origin.xyz,23,screen)||!mapping_coordinates(screen,23,local_uv))continue;if(alpha>=coverage)source_screen=screen;}
  if(parameter(9).w!=0&&alpha>=coverage)source_screen=source_brush;
  coverage=max(coverage,alpha);
 }return coverage;
}
void main(){
 ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,region_size)))return;ivec2 p=region_origin+local;
 vec2 size=vec2(paint_size),uv=(vec2(p+page_origin)+.5)/size;precise vec4 result_value=imageLoad(result_img,p);
 for(current_stamp=0;current_stamp<command_count;current_stamp++){
  vec4 axes=parameter(1);float alpha;vec2 source_screen=vec2(0);
  if(parameter(4).x>=0)alpha=surface_coverage(uv,size,source_screen);
  else {vec2 delta=uv-parameter(0).xy;float determinant=axes.x*axes.w-axes.y*axes.z;if(abs(determinant)<1e-16)continue;source_screen=vec2(delta.x*axes.w-delta.y*axes.z,delta.y*axes.x-delta.x*axes.y)/determinant;alpha=brush_alpha(source_screen);}
  if(parameter(4).x<0&&parameter(10).x!=0){vec2 screen;if(!mapping_uv_screen(uv,10,screen))alpha=0;else alpha*=mapping_alpha(screen,10);}
  alpha*=parameter(0).z*quick_coverage(p+page_origin);if(alpha<=1e-8)continue;
  if(parameter(9).w!=0){
   vec4 sampled=vec4(0);if(all(lessThanEqual(abs(source_screen),vec2(1)))){
    ivec2 payload_size=textureSize(payload_img,0);vec2 point=(source_screen+1)*.5*vec2(payload_size)-.5,f=fract(point);ivec2 at=ivec2(floor(point));
    vec4 a=texelFetch(payload_img,clamp(at,ivec2(0),payload_size-1),0),b=texelFetch(payload_img,clamp(at+ivec2(1,0),ivec2(0),payload_size-1),0),c=texelFetch(payload_img,clamp(at+ivec2(0,1),ivec2(0),payload_size-1),0),d=texelFetch(payload_img,clamp(at+ivec2(1),ivec2(0),payload_size-1),0);sampled=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
   }
   if(parameter(9).w==1){result_value=mix(result_value,sampled,alpha);if(result_value.a<=1e-8)result_value=vec4(0);}
   else if(sampled.a>1e-8)result_value=sampled*alpha+result_value*(1-sampled.a*alpha);
   continue;
  }
  vec3 source_value=parameter(3).rgb;
  if(parameter(23).x!=0){
   if(parameter(4).x<0&&!mapping_uv_screen(uv,23,source_screen))continue;vec2 local_uv;if(!mapping_coordinates(source_screen,23,local_uv))continue;
   vec4 options=parameter(36);
   if(options.x!=0){vec2 thread_uv=(local_uv+1)*.5*options.y;float relief=.65*(.5+.5*sin(6.283185307179586*(thread_uv.x+.15*thread_uv.y)))+.35*(.5+.5*cos(6.283185307179586*thread_uv.y));
    if(options.z==0)source_value=clamp(source_value*(.55+.7*relief),vec3(-65504),vec3(65504));
    else if(options.z==2&&options.w==0)source_value.x=clamp(source_value.x+.1*(relief-.5),0.0,1.0);
    else if(options.z==4)source_value.x=clamp(source_value.x+.004*(relief-.5),-65504.0,65504.0);
   }
  }
  result_value=vec4(source_value,1)*alpha+result_value*(1-alpha);
 }
 imageStore(result_img,p,result_value);
}
)GLSL";
    }
    return info;
  }
  static Shader compile(int mode)
  {
    trace_authoring_compilation();
    auto info = program_info(mode);
    gpu_derived::stabilize_hosted_resource_owners(info);
    return {blender::GPU_shader_create_from_info_python(
                reinterpret_cast<GPUShaderCreateInfo *>(&info)),
            blender::GPU_shader_free};
  }
  void upload_parameters(std::span<const std::array<std::array<float, 4>, 37>> data)
  {
    using namespace blender;
    if (!parameters_ || GPU_texture_height(parameters_.get()) < int(data.size())) {
      parameters_ = Texture(GPU_texture_create_2d("BTS brush parameters",
                                                  37,
                                                  int(std::bit_ceil(unsigned(data.size()))),
                                                  1,
                                                  gpu::TextureFormat::SFLOAT_32_32_32_32,
                                                  GPU_TEXTURE_USAGE_GENERAL,
                                                  nullptr),
                            GPU_texture_free);
      ++metadata_allocations_;
    }
    if (!parameters_)
      throw std::bad_alloc();
    GPU_texture_update_sub(
        parameters_.get(), GPU_DATA_FLOAT, data.data(), 0, 0, 0, 37, int(data.size()), 1);
  }
  static PaintPixels::Bounds clipped(PaintPixels::Bounds b, int size)
  {
    b.x0 = std::clamp(b.x0, 0, size);
    b.y0 = std::clamp(b.y0, 0, size);
    b.x1 = std::clamp(b.x1, 0, size);
    b.y1 = std::clamp(b.y1, 0, size);
    return b;
  }
  static void dispatch(blender::gpu::Shader *shader,
                       blender::gpu::Texture *target,
                       PaintPixels::Bounds b)
  {
    using namespace blender;
    const int origin[2] = {b.x0, b.y0}, size[2] = {b.x1 - b.x0, b.y1 - b.y0};
    GPU_shader_uniform_2iv(shader, "region_origin", origin);
    GPU_shader_uniform_2iv(shader, "region_size", size);
    GPU_texture_image_bind(target, 0);
    GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
    trace_authoring_dispatch(shader);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                       GPU_BARRIER_TEXTURE_UPDATE);
    GPU_texture_image_unbind(target);
    GPU_shader_unbind();
  }

 public:
  void evict()
  {
    parameters_.reset();
    triangles_.reset();
    empty_alpha_.reset();
    alphas_.clear();
  }
  std::uint64_t alpha_allocations() const
  {
    return alpha_allocations_;
  }
  std::uint64_t metadata_allocations() const
  {
    return metadata_allocations_;
  }
  bool prepare()
  {
    if (!stamp_shader_)
      stamp_shader_ = compile(false);
    if (!over_shader_)
      over_shader_ = compile(true);
    if (!contact_shader_)
      contact_shader_ = compile(2);
    return bool(stamp_shader_) && bool(over_shader_) && bool(contact_shader_);
  }
  static bool supported(const PaintPixels::Stamp &s)
  {
    return gpu_basic_brush_supported(s);
  }
  static PaintPixels::Bounds bounds(int size, const PaintPixels::Stamp &s)
  {
    return brush_footprint_bounds(size, s);
  }
  bool stamp(blender::gpu::Texture *target,
             int channel,
             const PaintPixels::Stamp &s,
             int resolution = 0,
             std::array<int, 2> origin = {})
  {
    const PaintPixels::Stamp *one = &s;
    return stamps(target, channel, std::span(&one, 1), resolution, origin);
  }
  bool stamps(blender::gpu::Texture *target,
              int channel,
              std::span<const PaintPixels::Stamp *const> commands,
              int resolution = 0,
              std::array<int, 2> origin = {},
              blender::gpu::Texture *coverage_image = nullptr,
              blender::gpu::Texture *payload_image = nullptr,
              bool capture = false)
  {
    using namespace blender;
    if (channel < 0 || channel >= 5)
      return false;
    if (commands.empty())
      return true;
    if (!resolution)
      resolution = GPU_texture_width(target);
    PaintPixels::Bounds b{resolution, resolution, 0, 0};
    const auto bitmap = commands.front()->alpha.kind == AlphaKind::Bitmap ?
                            commands.front()->alpha.bitmap :
                            nullptr;
    const auto stencil_source = stencil_bitmap(*commands.front());
    for (const auto *command : commands) {
      if (stencil_bitmap(*command) != stencil_source ||
          (command->coverage_mask && !coverage_image) ||
          command->coverage_mask != commands.front()->coverage_mask ||
          command->stencil_uv_offset != commands.front()->stencil_uv_offset ||
          (!capture && ((bool(command->smudge) || bool(command->cloned)) && !payload_image)) ||
          command->smudge != commands.front()->smudge ||
          command->cloned != commands.front()->cloned || (!capture && !supported(*command)) ||
          (command->alpha.kind == AlphaKind::Bitmap ? command->alpha.bitmap : nullptr) != bitmap)
        return false;
      if (!command->channels[channel])
        continue;
      const auto bounds_value = bounds(resolution, *command);
      if (bounds_value.empty())
        continue;
      b = {std::min(b.x0, bounds_value.x0),
           std::min(b.y0, bounds_value.y0),
           std::max(b.x1, bounds_value.x1),
           std::max(b.y1, bounds_value.y1)};
    }
    b = {b.x0 - origin[0], b.y0 - origin[1], b.x1 - origin[0], b.y1 - origin[1]};
    b = clipped(b, GPU_texture_width(target));
    if (capture)
      b = {0, 0, GPU_texture_width(target), GPU_texture_height(target)};
    if (b.empty())
      return true;
    performance::Scope timing("gpu_brush_raster");
    auto &selected_shader = capture ? contact_shader_ : stamp_shader_;
    if (!selected_shader)
      selected_shader = compile(capture ? 2 : 0);
    if (!selected_shader)
      return false;
    std::vector<std::array<std::array<float, 4>, 37>> parameters;
    std::vector<std::array<float, 4>> triangles;
    for (const auto *command : commands) {
      const auto &s = *command;
      if (!s.channels[channel])
        continue;
      std::array<std::array<float, 4>, 37> data{};
      data[0] = {float(s.center[0]), float(s.center[1]), s.flow, float(triangles.size() / 8)};
      data[1] = {float(s.axis_x[0]), float(s.axis_x[1]), float(s.axis_y[0]), float(s.axis_y[1])};
      data[2] = {float(s.alpha.kind),
                 float(s.alpha.inverted),
                 s.hardness,
                 std::clamp(s.alpha.uniform, 0.f, 1.f)};
      const auto &values = s.projected ? s.projected->source.values : s.values;
      if (channel == 0)
        data[3] = values.base_color;
      else if (channel == 3)
        data[3] = {values.normal[0], values.normal[1], values.normal[2], 0};
      else
        data[3] = {channel == 1 ? values.metallic :
                   channel == 2 ? values.roughness :
                                  values.bump,
                   0,
                   0,
                   0};
      if (s.alpha.kind == AlphaKind::DynamicOutline) {
        s.alpha.dynamic.validate();
        const auto &d = s.alpha.dynamic;
        const double phase = d.phase * 6.2831853071795864769, angle = std::sin(phase) * .35;
        data[8] = {float(std::cos(angle)),
                   float(std::sin(angle)),
                   float(2 + 10 * std::clamp(double(d.roundness) + .5 * std::cos(phase), 0., 1.)),
                   d.width};
      }
      if (s.coverage_mask) {
        const auto &offset = s.stencil_uv_offset;
        data[9] = {1,
                   float(s.coverage_mask->inverted),
                   float(offset[0] >= 0 && offset[0] < 10 && offset[1] >= 0 && offset[1] < 100),
                   0};
      }
      data[9][3] = s.smudge ? 1 : s.cloned ? 2 : 0;
      data[4][0] = -1;
      if (s.surface) {
        const auto &surface = *s.surface;
        data[21] = {float(surface.center[0]),
                    float(surface.center[1]),
                    float(surface.center[2]),
                    float(surface.radius)};
        data[4] = {
            0, float(surface.projected), float(surface.unwrapped), float(surface.planar_fade)};
        for (int c = 0; c < 3; ++c) {
          data[5][c] = surface.tangent[c];
          data[6][c] = surface.bitangent[c];
          data[7][c] = surface.normal[c];
        }
        for (const auto &triangle : surface.triangles) {
          if (surface.cull &&
              !accepts_brush_normal(triangle.normal, surface.view_direction, surface.cull_angle))
            continue;
          const auto first = triangles.size();
          triangles.resize(first + 8);
          data[4][0]++;
          for (int v = 0; v < 3; ++v) {
            triangles[first + v * 2] = {float(triangle.uv[v][0]),
                                        float(triangle.uv[v][1]),
                                        float(triangle.paint_coordinates[v][0] / surface.radius),
                                        float(triangle.paint_coordinates[v][1] / surface.radius)};
            for (int c = 0; c < 3; ++c)
              triangles[first + v * 2 + 1][c] = (triangle.position[v][c] - surface.center[c]) /
                                                surface.radius;
          }
          triangles[first + 6][0] = triangle.bounded_depth;
          for (int c = 0; c < 3; ++c)
            triangles[first + 7][c] = triangle.seam_edges[c];
        }
      }
      if (s.stencil) {
        const int first = int(triangles.size() / 8);
        for (const auto &triangle : s.stencil->uv_world) {
          const auto offset = triangles.size();
          triangles.resize(offset + 8);
          for (int v = 0; v < 3; ++v) {
            triangles[offset + v * 2] = {float(triangle.uv[v][0]), float(triangle.uv[v][1]), 0, 0};
            for (int c = 0; c < 3; ++c)
              triangles[offset + v * 2 + 1][c] = float(triangle.position[v][c]);
          }
        }
        gpu_brush_mapping(std::span(data).subspan(10, 13),
                          *s.stencil,
                          s.stencil->stencil,
                          s.stencil_uv_offset,
                          first,
                          int(s.stencil->uv_world.size()));
      }
      if (s.projected) {
        const auto &projected = *s.projected;
        const int first = int(triangles.size() / 8);
        for (const auto &triangle : projected.mapping.uv_world) {
          const auto offset = triangles.size();
          triangles.resize(offset + 8);
          for (int v = 0; v < 3; ++v) {
            triangles[offset + v * 2] = {float(triangle.uv[v][0]), float(triangle.uv[v][1]), 0, 0};
            for (int c = 0; c < 3; ++c)
              triangles[offset + v * 2 + 1][c] = float(triangle.position[v][c]);
          }
        }
        gpu_brush_mapping(std::span(data).subspan(23, 13),
                          projected.mapping,
                          projected.source.transform,
                          s.stencil_uv_offset,
                          first,
                          int(projected.mapping.uv_world.size()));
        data[36] = {float(projected.source.weave),
                    projected.source.density,
                    float(channel),
                    float(bool(projected.scalar_override))};
        if (channel == 2 && projected.scalar_override)
          data[3][0] = *projected.scalar_override;
      }
      parameters.push_back(data);
    }
    if (parameters.empty())
      return true;
    std::erase_if(alphas_, [&](const auto &entry) {
      return entry.first != bitmap.get() && entry.first != stencil_source.get();
    });
    auto *alpha_image_value = alpha_image(bitmap), *stencil_image = alpha_image(stencil_source);
    const int rows = std::max(1, int(triangles.size() / 8)), maximum = GPU_max_texture_size(),
              columns = (rows + maximum - 1) / maximum, width = 8 * columns,
              height = (rows + columns - 1) / columns;
    triangles.resize(std::size_t(width) * height);
    if (width > maximum)
      throw std::length_error("Brush projection metadata exceeds GPU texture dimensions");
    if (!triangles_ || GPU_texture_width(triangles_.get()) != width ||
        GPU_texture_height(triangles_.get()) < height)
    {
      triangles_ = Texture(
          GPU_texture_create_2d("BTS brush footprint triangles",
                                width,
                                std::min(maximum, int(std::bit_ceil(unsigned(height)))),
                                1,
                                gpu::TextureFormat::SFLOAT_32_32_32_32,
                                GPU_TEXTURE_USAGE_GENERAL,
                                nullptr),
          GPU_texture_free);
      ++metadata_allocations_;
    }
    if (!triangles_)
      throw std::bad_alloc();
    GPU_texture_update_sub(
        triangles_.get(), GPU_DATA_FLOAT, triangles.data(), 0, 0, 0, width, height, 1);
    upload_parameters(parameters);
    GPU_shader_bind(selected_shader.get());
    GPU_shader_uniform_1i(selected_shader.get(), "command_count", int(parameters.size()));
    GPU_shader_uniform_2iv(selected_shader.get(), "page_origin", origin.data());
    GPU_shader_uniform_1i(selected_shader.get(), "paint_size", resolution);
    GPU_texture_bind(parameters_.get(), 0);
    GPU_texture_bind(triangles_.get(), 1);
    GPU_texture_bind(alpha_image_value, 2);
    GPU_texture_bind(coverage_image ? coverage_image : alpha_image_value, 3);
    GPU_texture_bind(stencil_image, 4);
    GPU_texture_bind(payload_image ? payload_image : alpha_image_value, 5);
    dispatch(selected_shader.get(), target, b);
    return true;
  }
  bool contact(blender::gpu::Texture *target, const PaintPixels::Stamp &footprint, int resolution)
  {
    auto stamp = footprint;
    stamp.channels = {true, false, false, false, false};
    stamp.coverage_mask.reset();
    const PaintPixels::Stamp *one = &stamp;
    return stamps(target, 0, std::span(&one, 1), resolution, {}, nullptr, nullptr, true);
  }
  bool over(blender::gpu::Texture *target,
            blender::gpu::Texture *before,
            blender::gpu::Texture *ink,
            float opacity,
            PaintPixels::Bounds b,
            bool erase = false,
            bool replace = true,
            int operation = 0)
  {
    using namespace blender;
    b = clipped(b, GPU_texture_width(target));
    if (b.empty() || (opacity == 0 && operation == 0))
      return true;
    performance::Scope timing("gpu_brush_stroke_opacity");
    if (!over_shader_)
      over_shader_ = compile(true);
    if (!over_shader_)
      return false;
    std::array<std::array<float, 4>, 37> data{};
    data[0] = {opacity, float(erase), float(replace), float(operation)};
    upload_parameters(std::span(&data, 1));
    GPU_shader_bind(over_shader_.get());
    GPU_texture_bind(parameters_.get(), 0);
    GPU_texture_bind(before, 1);
    GPU_texture_bind(ink, 2);
    dispatch(over_shader_.get(), target, b);
    return true;
  }
};
}  // namespace bts
