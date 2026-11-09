#pragma once
#include "GpuStackPlan.hh"
#include "Performance.hh"
#include "GPU_shader.hh"
#include "GPU_texture.hh"
#include "GPU_compute.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#include "GpuReadbackFence.hh"
#include <map>
#include <memory>
#include <span>
namespace bts {
// Only 16 bytes of culling metadata cross back to the CPU, after a zero-timeout
// fence poll. Authored pixels and the composition remain on the GPU.
class GpuRangeSummary {
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 using Buffer=std::unique_ptr<blender::gpu::StorageBuf,decltype(&blender::GPU_storagebuf_free)>;
 struct Tile {Texture image{nullptr,blender::GPU_texture_free};int width=0,height=0;std::uint64_t version=0;blender::gpu::Texture *source=nullptr;};
 struct Entry {std::map<int,Tile> tiles;Buffer buffer{nullptr,blender::GPU_storagebuf_free};GpuReadbackFence fence;std::uint64_t revision=0,submitted=0,epoch=0,submitted_epoch=0;};
 Shader chunks_{nullptr,blender::GPU_shader_free},reduce_{nullptr,blender::GPU_shader_free};std::map<std::uint32_t,Entry> entries_;
 static blender::gpu::Shader *compile_chunks(){using namespace blender;using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16,16).shared_variable(Type::float4_t,"bounds[256]").sampler(0,ImageType::Float2D,"prefix_img")
  .image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2D,"range_img")
  .push_constant(Type::int2_t,"chunk").push_constant(Type::int2_t,"size");
  info.compute_source_generated=R"GLSL(
#ifdef CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
#endif
void main(){uint lane=gl_LocalInvocationIndex;vec4 range=vec4(3.402823e38,-3.402823e38,0,0);
 for(int y=0;y<16;y++)for(int x=0;x<16;x++){ivec2 p=chunk*256+ivec2(gl_LocalInvocationID.xy)+ivec2(x,y)*16;if(any(greaterThanEqual(p,size)))continue;
  vec4 ink=texelFetch(prefix_img,min(p,textureSize(prefix_img,0)-1),0);if(ink.a>0){float v=ink.r/ink.a;range.x=min(range.x,v);range.y=max(range.y,v);range.z=1;}}
 bounds[lane]=range;barrier();for(uint stride=128u;stride>0u;stride>>=1u){if(lane<stride){bounds[lane].x=min(bounds[lane].x,bounds[lane+stride].x);bounds[lane].y=max(bounds[lane].y,bounds[lane+stride].y);bounds[lane].z=max(bounds[lane].z,bounds[lane+stride].z);}barrier();}
 if(lane==0u)imageStore(range_img,chunk,bounds[0]);
})GLSL";
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
 }
 static blender::gpu::Shader *compile_reduce(){using namespace blender;using namespace gpu::shader;ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(256).shared_variable(Type::float4_t,"bounds[256]").sampler(0,ImageType::Float2D,"range_img")
  .storage_buf(0,Qualifier::read_write,"vec4","summary[]").push_constant(Type::int_t,"first_tile");
  info.compute_source_generated=R"GLSL(
#ifdef CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
#endif
void main(){uint lane=gl_LocalInvocationIndex;ivec2 size=textureSize(range_img,0);vec4 range=vec4(3.402823e38,-3.402823e38,0,0);
 for(uint i=lane;i<uint(size.x*size.y);i+=256u){vec4 item=texelFetch(range_img,ivec2(int(i)%size.x,int(i)/size.x),0);range.x=min(range.x,item.x);range.y=max(range.y,item.y);range.z=max(range.z,item.z);}
 bounds[lane]=range;barrier();for(uint stride=128u;stride>0u;stride>>=1u){if(lane<stride){bounds[lane].x=min(bounds[lane].x,bounds[lane+stride].x);bounds[lane].y=max(bounds[lane].y,bounds[lane+stride].y);bounds[lane].z=max(bounds[lane].z,bounds[lane+stride].z);}barrier();}
 if(lane==0u){vec4 old=first_tile!=0?vec4(3.402823e38,-3.402823e38,0,0):summary[0];summary[0]=vec4(min(old.x,bounds[0].x),max(old.y,bounds[0].y),max(old.z,bounds[0].z),0);}
})GLSL";
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
 }
 void submit(Entry &entry){using namespace blender;if(entry.fence||entry.submitted==entry.revision)return;
  if(!entry.buffer)entry.buffer=Buffer(GPU_storagebuf_create_ex(16,nullptr,GPU_USAGE_DYNAMIC,"BTS async displacement bounds"),GPU_storagebuf_free);
  if(!entry.buffer)throw std::runtime_error("GPU range metadata allocation failed");
  GPU_shader_bind(reduce_.get());GPU_storagebuf_bind(entry.buffer.get(),0);int first=1;
  for(const auto &[id,tile]:entry.tiles){GPU_texture_bind(tile.image.get(),0);GPU_shader_uniform_1i(reduce_.get(),"first_tile",first);first=0;GPU_compute_dispatch(reduce_.get(),1,1,1);GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);}
  GPU_storagebuf_unbind(entry.buffer.get());GPU_texture_unbind_all();GPU_shader_unbind();entry.fence.submit(entry.buffer.get());entry.submitted=entry.revision;entry.submitted_epoch=entry.epoch;
 }
 public:
 struct Source {int tile,width,height;blender::gpu::Texture *prefix;std::uint64_t version;std::set<int> dirty;};
 struct Result {std::uint32_t uid;float minimum,maximum;bool present;std::uint64_t revision,epoch;};
 std::uint64_t reduced_texels=0,readback_bytes=0,discarded=0;
 void prepare(){using namespace blender;if(!chunks_)chunks_=Shader(compile_chunks(),GPU_shader_free);if(!reduce_)reduce_=Shader(compile_reduce(),GPU_shader_free);if(!chunks_||!reduce_)throw std::runtime_error("GPU range shader compilation failed");}
 void update(std::uint32_t uid,std::span<const Source> sources,std::uint64_t epoch=0){using namespace blender;performance::Scope timing("gpu_displacement_range");prepare();
  auto &entry=entries_[uid];bool changed=entry.epoch!=epoch;entry.epoch=epoch;std::set<int> live;
  for(const auto &source:sources){live.insert(source.tile);auto &tile=entry.tiles[source.tile];const bool full=!tile.image||tile.width!=source.width||tile.height!=source.height||tile.source!=source.prefix||source.version>tile.version+1;
   if(!full&&tile.version==source.version)continue;changed=true;std::set<int> dirty=source.dirty;const int columns=(source.width+255)/256,rows=(source.height+255)/256;
   if(full){tile.image=Texture(GPU_texture_create_2d("BTS cached displacement bounds",columns,rows,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);if(!tile.image)throw std::runtime_error("GPU range tile allocation failed");for(int i=0;i<columns*rows;++i)dirty.insert(i);}
   GPU_shader_bind(chunks_.get());GPU_texture_bind(source.prefix,0);GPU_texture_image_bind(tile.image.get(),0);const int size[2]={source.width,source.height};GPU_shader_uniform_2iv(chunks_.get(),"size",size);
   for(int index:dirty){const int chunk[2]={index%columns,index/columns};GPU_shader_uniform_2iv(chunks_.get(),"chunk",chunk);GPU_compute_dispatch(chunks_.get(),1,1,1);reduced_texels+=std::uint64_t(std::min(256,source.width-chunk[0]*256))*std::min(256,source.height-chunk[1]*256);}
   GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_image_unbind(tile.image.get());GPU_texture_unbind_all();GPU_shader_unbind();tile.width=source.width;tile.height=source.height;tile.version=source.version;tile.source=source.prefix;
  }
  for(auto i=entry.tiles.begin();i!=entry.tiles.end();)if(!live.contains(i->first)){changed=true;i=entry.tiles.erase(i);}else ++i;
  if(changed)++entry.revision;submit(entry);
 }
 std::vector<Result> poll(){using namespace blender;performance::Scope timing("gpu_displacement_range_poll");std::vector<Result> results;
  for(auto &[uid,entry]:entries_){if(!entry.fence)continue;std::array<float,4> value{};if(!entry.fence.read(entry.buffer.get(),value.data(),entry.submitted!=entry.revision))continue;if(entry.submitted==entry.revision){readback_bytes+=16;results.push_back({uid,value[2]>0?value[0]:0,value[2]>0?value[1]:0,value[2]>0,entry.revision,entry.submitted_epoch});}else ++discarded;submit(entry);
  }return results;
 }
 bool pending()const{return std::any_of(entries_.begin(),entries_.end(),[](const auto &e){return bool(e.second.fence);});}
};
}
