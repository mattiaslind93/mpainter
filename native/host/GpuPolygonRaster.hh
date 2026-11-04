#pragma once
#include "PaintPixels.hh"
#include "PaintRecording.hh"
#include "BrushFootprintBounds.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "GPU_batch.hh"
#include "GPU_capabilities.hh"
#include "GPU_compute.hh"
#include "GPU_framebuffer.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "GPU_texture.hh"
#include "GPU_vertex_buffer.hh"
#include "gpu_shader_create_info.hh"
#include "GpuDerivedShaders.hh"
#include "GpuAuthoringProgramTrace.hh"
#include <cmath>
#include <map>
#include <memory>
namespace bts {
// Rasterize selection metadata once, then edit only authored pages. No pixel
// data crosses back to the CPU. A padded primitive box plus the original
// barycentric test preserves compute-brush coverage at triangle boundaries.
class GpuPolygonRaster {
 friend class GpuPaintPages;
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 using Buffer=std::unique_ptr<blender::gpu::StorageBuf,decltype(&blender::GPU_storagebuf_free)>;
 struct Coverage {Texture image{nullptr,blender::GPU_texture_free};Buffer triangles{nullptr,blender::GPU_storagebuf_free};std::shared_ptr<const PaintPixels::SurfaceFootprint> owner;PaintPixels::Bounds bounds;std::uint64_t used=0,bytes=0;};
 std::map<std::pair<const PaintPixels::SurfaceFootprint *,int>,Coverage> coverage_;
 Shader coverage_shader_{nullptr,blender::GPU_shader_free},pages_shader_{nullptr,blender::GPU_shader_free};
 std::unique_ptr<blender::gpu::Batch,decltype(&blender::GPU_batch_discard)> quad_{nullptr,blender::GPU_batch_discard};
 std::uint64_t clock_=0,bytes_=0;bool warmed_=false;
 static blender::gpu::shader::StageInterfaceInfo coverage_interface(){using namespace blender::gpu::shader;
  StageInterfaceInfo varying("bts_polygon_interface");varying.flat(Type::float2_t,"triangle_a").flat(Type::float2_t,"triangle_b").flat(Type::float2_t,"triangle_c").no_perspective(Type::float2_t,"sample_uv");return varying;
 }
 static blender::gpu::shader::ShaderCreateInfo coverage_info(blender::gpu::shader::StageInterfaceInfo &varying){using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");info.vertex_in(0,Type::float2_t,"pos").vertex_out(varying).fragment_out(0,Type::float_t,"coverage_value").storage_buf(0,Qualifier::read,"vec4","uv_rows[]").push_constant(Type::float_t,"paint_size").push_constant(Type::float2_t,"rect_origin").push_constant(Type::float2_t,"rect_size");
  info.vertex_source_generated=R"GLSL(
void main(){vec4 ab=uv_rows[gl_InstanceID*2];triangle_a=ab.xy;triangle_b=ab.zw;triangle_c=uv_rows[gl_InstanceID*2+1].xy;
 vec2 lo=min(min(triangle_a,triangle_b),triangle_c)*paint_size-1,hi=max(max(triangle_a,triangle_b),triangle_c)*paint_size+1;
 vec2 p=mix(lo,hi,pos);sample_uv=p/paint_size;gl_Position=vec4((p-rect_origin)/rect_size*2-1,0,1);}
)GLSL";
  info.fragment_source_generated=R"GLSL(
void main(){vec2 uv=(floor(sample_uv*paint_size)+.5)/paint_size,d=triangle_b-triangle_a,e=triangle_c-triangle_a,p=uv-triangle_a;
 float determinant=d.x*e.y-d.y*e.x;if(abs(determinant)<1e-20)discard;float a=(p.x*e.y-p.y*e.x)/determinant,b=(d.x*p.y-d.y*p.x)/determinant;
 if(a< -1e-8||b< -1e-8||a+b>1+1e-8)discard;coverage_value=1;}
)GLSL";return info;
 }
 static blender::gpu::shader::ShaderCreateInfo pages_info(){using namespace blender;using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16,16).storage_buf(0,Qualifier::read,"uint","page_keys[]").sampler(0,ImageType::Float2D,"source_img").sampler(1,ImageType::Float2D,"coverage_img").image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::read_write,ImageReadWriteType::image2D,"target_img").push_constant(Type::int_t,"operation").push_constant(Type::int_t,"page_count").push_constant(Type::int_t,"page_offset").push_constant(Type::int_t,"atlas_columns").push_constant(Type::int2_t,"coverage_origin").push_constant(Type::float4_t,"paint_value").push_constant(Type::float_t,"opacity");
  info.compute_source_generated=R"GLSL(
void main(){int i=page_offset+int(gl_WorkGroupID.z);if(i>=page_count)return;ivec2 local=ivec2(gl_GlobalInvocationID.xy);uint key=page_keys[i];ivec2 p=ivec2(int(key%65536u),int(key/65536u))*64+local;
 ivec2 atlas_point=ivec2(i%atlas_columns,i/atlas_columns)*64+local;
 if(operation==0){vec4 v=all(lessThan(p,textureSize(source_img,0)))?texelFetch(source_img,p,0):vec4(0);imageStore(target_img,atlas_point,v);return;}
 if(any(greaterThanEqual(p,imageSize(target_img))))return;
 if(operation==1){imageStore(target_img,p,texelFetch(source_img,atlas_point,0));return;}
 if(operation==2){imageStore(target_img,p,vec4(0));return;}
 ivec2 q=p-coverage_origin;if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,textureSize(coverage_img,0)))||texelFetch(coverage_img,q,0).r<=0)return;
 precise vec4 before=imageLoad(target_img,p);precise vec4 result_value=paint_value*opacity+before*(1-opacity);if(result_value.a==0)result_value=vec4(0);imageStore(target_img,p,result_value);}
)GLSL";return info;
 }
 static Shader compile_coverage(){trace_authoring_compilation();auto varying=coverage_interface();auto info=coverage_info(varying);gpu_derived::stabilize_hosted_resource_owners(info);return {blender::GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),blender::GPU_shader_free};}
 static Shader compile_pages(){trace_authoring_compilation();auto info=pages_info();gpu_derived::stabilize_hosted_resource_owners(info);return {blender::GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),blender::GPU_shader_free};}
 Coverage &coverage(const PaintPixels::Stamp &stamp,int resolution){using namespace blender;
  const auto key=std::pair{stamp.surface.get(),resolution};if(auto it=coverage_.find(key);it!=coverage_.end()){it->second.used=++clock_;return it->second;}
  auto bounds=brush_footprint_bounds(resolution,stamp);bounds={std::max(0,bounds.x0),std::max(0,bounds.y0),std::min(resolution,bounds.x1),std::min(resolution,bounds.y1)};if(bounds.empty())throw std::logic_error("Empty polygon coverage");
  std::vector<std::array<float,4>> triangles;triangles.reserve(stamp.surface->triangles.size()*2);for(const auto &t:stamp.surface->triangles){triangles.push_back({float(t.uv[0][0]),float(t.uv[0][1]),float(t.uv[1][0]),float(t.uv[1][1])});triangles.push_back({float(t.uv[2][0]),float(t.uv[2][1]),0,0});}
  const auto bytes=std::uint64_t(bounds.x1-bounds.x0)*(bounds.y1-bounds.y0)+triangles.size()*sizeof(triangles[0]);const auto free=texture_free_vram.load();const auto budget=(bytes_+free)/8;
  while(bytes_+bytes>budget&&!coverage_.empty()){auto old=std::min_element(coverage_.begin(),coverage_.end(),[](const auto &a,const auto &b){return a.second.used<b.second.used;});bytes_-=old->second.bytes;coverage_.erase(old);}
  Coverage value;value.owner=stamp.surface;value.bounds=bounds;value.used=++clock_;value.bytes=bytes;value.image={GPU_texture_create_2d("BTS polygon UV coverage",bounds.x1-bounds.x0,bounds.y1-bounds.y0,1,gpu::TextureFormat::UNORM_8,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};value.triangles={GPU_storagebuf_create_ex(triangles.size()*sizeof(triangles[0]),triangles.data(),GPU_USAGE_STATIC,"BTS polygon UV triangles"),GPU_storagebuf_free};if(!value.image||!value.triangles)throw std::bad_alloc();const float zero=0;GPU_texture_clear(value.image.get(),GPU_DATA_FLOAT,&zero);
  struct State {gpu::FrameBuffer *fb=GPU_framebuffer_active_get();GPUBlend blend=GPU_blend_get();GPUDepthTest depth=GPU_depth_test_get();GPUFaceCullTest cull=GPU_face_culling_get();GPUStencilTest stencil=GPU_stencil_test_get();GPUWriteMask write=GPU_write_mask_get();int viewport[4],scissor[4];State(){GPU_viewport_size_get_i(viewport);GPU_scissor_get(scissor);}~State(){if(fb)GPU_framebuffer_bind(fb);else GPU_framebuffer_restore();GPU_viewport(viewport[0],viewport[1],viewport[2],viewport[3]);GPU_scissor(scissor[0],scissor[1],scissor[2],scissor[3]);GPU_blend(blend);GPU_depth_test(depth);GPU_face_culling(cull);GPU_stencil_test(stencil);GPU_write_mask(write);}} state;
  gpu::FrameBuffer *raw=nullptr;GPU_framebuffer_ensure_config(&raw,{GPU_ATTACHMENT_NONE,GPU_ATTACHMENT_TEXTURE(value.image.get())});std::unique_ptr<gpu::FrameBuffer,decltype(&GPU_framebuffer_free)> framebuffer(raw,GPU_framebuffer_free);GPU_framebuffer_bind(raw);GPU_viewport(0,0,bounds.x1-bounds.x0,bounds.y1-bounds.y0);GPU_scissor(0,0,bounds.x1-bounds.x0,bounds.y1-bounds.y0);GPU_blend(GPU_BLEND_NONE);GPU_depth_test(GPU_DEPTH_NONE);GPU_face_culling(GPU_CULL_NONE);GPU_stencil_test(GPU_STENCIL_NONE);GPU_color_mask(true,true,true,true);
  GPU_batch_set_shader(quad_.get(),coverage_shader_.get());GPU_shader_uniform_1f(coverage_shader_.get(),"paint_size",float(resolution));const float origin[2]={float(bounds.x0),float(bounds.y0)},size[2]={float(bounds.x1-bounds.x0),float(bounds.y1-bounds.y0)};GPU_shader_uniform_2fv(coverage_shader_.get(),"rect_origin",origin);GPU_shader_uniform_2fv(coverage_shader_.get(),"rect_size",size);GPU_storagebuf_bind(value.triangles.get(),0);GPU_batch_draw_instance_range(quad_.get(),0,int(stamp.surface->triangles.size()));trace_authoring_dispatch(coverage_shader_.get());GPU_storagebuf_unbind(value.triangles.get());GPU_shader_unbind();if(state.fb)GPU_framebuffer_bind(state.fb);else GPU_framebuffer_restore();GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);
  bytes_+=bytes;return coverage_.emplace(key,std::move(value)).first->second;
 }
 void dispatch(blender::gpu::Texture *target,blender::gpu::Texture *source,blender::gpu::StorageBuf *keys,int count,int columns,int operation,blender::gpu::Texture *mask=nullptr,std::array<int,2> origin={},std::array<float,4> value={},float opacity=1){using namespace blender;
  GPU_shader_bind(pages_shader_.get());GPU_shader_uniform_1i(pages_shader_.get(),"operation",operation);GPU_shader_uniform_1i(pages_shader_.get(),"page_count",count);GPU_shader_uniform_1i(pages_shader_.get(),"atlas_columns",columns);GPU_shader_uniform_2iv(pages_shader_.get(),"coverage_origin",origin.data());GPU_shader_uniform_4fv(pages_shader_.get(),"paint_value",value.data());GPU_shader_uniform_1f(pages_shader_.get(),"opacity",opacity);GPU_storagebuf_bind(keys,0);if(source)GPU_texture_bind(source,0);if(mask)GPU_texture_bind(mask,1);GPU_texture_image_bind(target,0);for(int offset=0;offset<count;){const int batch=std::min(count-offset,GPU_max_work_group_count(2));GPU_shader_uniform_1i(pages_shader_.get(),"page_offset",offset);GPU_compute_dispatch(pages_shader_.get(),4,4,batch);trace_authoring_dispatch(pages_shader_.get());offset+=batch;}GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS|GPU_BARRIER_TEXTURE_UPDATE);GPU_texture_image_unbind(target);if(source)GPU_texture_unbind(source);if(mask)GPU_texture_unbind(mask);GPU_storagebuf_unbind(keys);GPU_shader_unbind();
 }
 public:
 struct Snapshot {Texture image{nullptr,blender::GPU_texture_free};Buffer keys{nullptr,blender::GPU_storagebuf_free};int count=0,columns=1;std::uint64_t bytes=0;};
 bool prepare(){using namespace blender;if(!coverage_shader_)coverage_shader_=compile_coverage();if(!pages_shader_)pages_shader_=compile_pages();if(!quad_){GPUVertFormat format={};const auto attr=GPU_vertformat_attr_add(&format,"pos",gpu::VertAttrType::SFLOAT_32_32);auto *vertices=GPU_vertbuf_create_with_format(format);GPU_vertbuf_data_alloc(*vertices,6);const float points[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};GPU_vertbuf_attr_fill(vertices,attr,points);quad_.reset(GPU_batch_create_ex(GPU_PRIM_TRIS,vertices,nullptr,GPU_BATCH_OWNS_VBO));}if(!coverage_shader_||!pages_shader_||!quad_)return false;
  // Execute every pipeline once while the workspace is loading: driver JIT
  // compilation must not be charged to the artist's first bucket click.
  if(!warmed_){Texture target(GPU_texture_create_2d("BTS polygon warmup",8,8,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!target)throw std::bad_alloc();const float zero[4]={};GPU_texture_clear(target.get(),GPU_DATA_FLOAT,zero);PaintPixels::Stamp stamp{};auto surface=std::make_shared<PaintPixels::SurfaceFootprint>();surface->center={0,0,0};surface->radius=1;PaintPixels::SurfaceTriangle triangle{};triangle.uv={{{0,0},{1,0},{0,1}}};surface->triangles.push_back(triangle);stamp.surface=surface;stamp.alpha.kind=AlphaKind::Uniform;stamp.alpha.uniform=1;const std::array<std::uint32_t,1> pages{0};auto saved=capture(target.get(),pages);paint(target.get(),stamp,0,8,pages,1);restore(target.get(),saved);clear(target.get(),pages);coverage_.clear();bytes_=0;warmed_=true;}return true;}
 static bool supported(const PaintPixels::Stamp &s){if(!s.surface||s.surface->center!=std::array<double,3>{}||s.surface->radius!=1||s.alpha.kind!=AlphaKind::Uniform||s.alpha.uniform!=1||s.alpha.inverted||s.flow!=1||s.stencil||s.projected||s.smudge||s.cloned||s.coverage_mask)return false;for(const auto &t:s.surface->triangles)for(const auto &p:t.position)if(p!=std::array<double,3>{})return false;return true;}
 static std::vector<std::uint32_t> changed_pages(const PaintPixels::PageVersions::value_type &before,const PaintPixels::PageVersions::value_type &after){std::vector<std::uint32_t> result;for(const auto &[page,version]:after){const auto old=before.find(page);if(old==before.end()||old->second!=version)result.push_back(page);}return result;}
 Snapshot capture(blender::gpu::Texture *source,std::span<const std::uint32_t> pages){using namespace blender;Snapshot result;if(pages.empty())return result;result.count=int(pages.size());result.columns=int(std::ceil(std::sqrt(double(result.count))));const int width=result.columns*64,height=((result.count+result.columns-1)/result.columns)*64;result.bytes=std::uint64_t(width)*height*16+pages.size_bytes();result.image={GPU_texture_create_2d("BTS polygon Undo dirty pages",width,height,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free};result.keys={GPU_storagebuf_create_ex(pages.size_bytes(),pages.data(),GPU_USAGE_STATIC,"BTS polygon dirty page keys"),GPU_storagebuf_free};if(!result.image||!result.keys)throw std::bad_alloc();dispatch(result.image.get(),source,result.keys.get(),result.count,result.columns,0);return result;}
 void restore(blender::gpu::Texture *target,const Snapshot &saved){dispatch(target,saved.image.get(),saved.keys.get(),saved.count,saved.columns,1);}
 void clear(blender::gpu::Texture *target,std::span<const std::uint32_t> pages){using namespace blender;if(pages.empty())return;Buffer keys(GPU_storagebuf_create_ex(pages.size_bytes(),pages.data(),GPU_USAGE_STATIC,"BTS cleared polygon page keys"),GPU_storagebuf_free);dispatch(target,nullptr,keys.get(),int(pages.size()),1,2);}
 void paint(blender::gpu::Texture *target,const PaintPixels::Stamp &stamp,int channel,int resolution,std::span<const std::uint32_t> pages,float opacity){using namespace blender;if(pages.empty())return;performance::Scope timing("gpu_polygon_raster");auto &mask=coverage(stamp,resolution);Buffer keys(GPU_storagebuf_create_ex(pages.size_bytes(),pages.data(),GPU_USAGE_STATIC,"BTS polygon paint page keys"),GPU_storagebuf_free);std::array<float,4> value{0,0,0,1};if(channel==0)std::copy_n(stamp.values.base_color.begin(),3,value.begin());else if(channel==3)std::copy_n(stamp.values.normal.begin(),3,value.begin());else value[0]=channel==1?stamp.values.metallic:channel==2?stamp.values.roughness:stamp.values.bump;dispatch(target,nullptr,keys.get(),int(pages.size()),1,3,mask.image.get(),{mask.bounds.x0,mask.bounds.y0},value,opacity);}
};
}
