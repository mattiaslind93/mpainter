#pragma once
#include "GpuMarginPlan.hh"
#include "Performance.hh"
#include "GPU_capabilities.hh"
#include "GPU_compute.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "gpu_shader_create_info.hh"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <map>
#include <memory>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace bts {
// Sparse boundary state only. Geometry is shared by all channels; original
// donor samples survive in-place padding so later edits never sample a padded
// value in place of the original. No production readback or CPU pixel math.
//
// Tables are packed as 1024-wide 2D arrays. A single 2D texture of height
// ceil(count/1024) is invalid on Metal once that height exceeds
// GPU_max_texture_size() (16384 on Apple3+/Mac1). Blender's Metal backend
// does not bake on GPU_texture_create_2d(nullptr); MTLTexture::ensure_baked()
// runs on the first GPU_texture_update_sub and validateWithDevice aborts.
class GpuMarginExecutor {
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 using Shader=std::unique_ptr<blender::gpu::Shader,decltype(&blender::GPU_shader_free)>;
 static constexpr int row_width=1024;
 struct Geometry {
  std::shared_ptr<const GpuMarginPlan> plan;
  Texture operations{nullptr,blender::GPU_texture_free},schedule{nullptr,blender::GPU_texture_free},
          originals{nullptr,blender::GPU_texture_free},outputs{nullptr,blender::GPU_texture_free};
  std::array<int,4> uploaded_rows{};
  bool unusable=false;
 };
 struct State {
  std::shared_ptr<const GpuMarginPlan> plan;
  Texture original_values{nullptr,blender::GPU_texture_free},results{nullptr,blender::GPU_texture_free},
          generations{nullptr,blender::GPU_texture_free},dirty{nullptr,blender::GPU_texture_free};
  std::uint32_t generation=0;
 };
 std::map<const GpuMarginPlan *,Geometry> geometry_;
 std::map<std::string,State> states_;
 std::map<bool,Shader> gather_;
 Shader evaluate_{nullptr,blender::GPU_shader_free};
 std::map<std::pair<blender::gpu::TextureFormat,bool>,Shader> commit_;
 Texture no_force_{nullptr,blender::GPU_texture_free};
 std::uint64_t last_operations_=0,last_originals_=0;
 static int layer_rows(){return std::max(1,blender::GPU_max_texture_size());}
public:
 struct TableShape {int width=row_width,height=1,layers=1;bool valid=false;};
 static TableShape packed_table_shape(std::size_t count,int max_size,int max_layers){
  TableShape shape;
  if(max_size<1||max_layers<1||row_width>max_size||row_width<1)return shape;
  const std::size_t texels=std::max<std::size_t>(1,count);
  const std::size_t rows=(texels+std::size_t(row_width)-1)/std::size_t(row_width);
  shape.width=row_width;
  if(rows<=std::size_t(max_size)){shape.height=int(rows);shape.layers=1;shape.valid=true;return shape;}
  shape.height=max_size;
  const std::size_t needed=(rows+std::size_t(max_size)-1)/std::size_t(max_size);
  if(needed>std::size_t(max_layers)||needed>std::size_t(std::numeric_limits<int>::max()))return shape;
  shape.layers=int(needed);shape.valid=true;return shape;
 }
 static bool debug_upload_uint_table(std::size_t count,std::uint32_t *first,std::uint32_t *last,int *width,int *height,int *layers){
  using namespace blender;
  std::vector<std::uint32_t> data(std::max<std::size_t>(1,count));
  for(std::size_t i=0;i<data.size();++i)data[i]=std::uint32_t(i*2654435761u+1u);
  Texture image{nullptr,GPU_texture_free};int row=0;std::uint64_t budget=std::numeric_limits<std::uint64_t>::max();
  if(!upload_table(image,row,"BTS margin table diagnostic",data.data(),data.size()*4,4,gpu::TextureFormat::UINT_32,budget,performance::Clock::now(),true)||!image)return false;
  const int w=GPU_texture_width(image.get()),h=GPU_texture_height(image.get()),z=std::max(1,GPU_texture_layer_count(image.get()));
  if(w>GPU_max_texture_size()||h>GPU_max_texture_size()||z>GPU_max_texture_layers())return false;
  if(width)*width=w;if(height)*height=h;if(layers)*layers=z;
  auto *pixels=static_cast<std::uint32_t *>(GPU_texture_read(image.get(),GPU_DATA_UINT,0));if(!pixels)return false;
  const std::size_t i=data.size()-1,x=i%std::size_t(w),y=(i/std::size_t(w))%std::size_t(h),layer=(i/std::size_t(w))/std::size_t(h);
  const std::size_t at=(layer*std::size_t(h)+y)*std::size_t(w)+x;
  if(first)*first=pixels[0];if(last)*last=pixels[at];
  const bool ok=pixels[0]==data[0]&&pixels[at]==data.back();MEM_delete(pixels);return ok;
 }
private:
 static Texture table(const char *name,std::size_t count,blender::gpu::TextureFormat format){
  using namespace blender;
  const auto shape=packed_table_shape(count,GPU_max_texture_size(),GPU_max_texture_layers());
  if(!shape.valid){
   std::fprintf(stderr,"BTS_GPU_MARGIN_TABLE_SKIPPED name=%s count=%zu max_2d=%d max_layers=%d\n",
                name,count,GPU_max_texture_size(),GPU_max_texture_layers());
   return {nullptr,GPU_texture_free};
  }
  auto *texture=GPU_texture_create_2d_array(name,shape.width,shape.height,shape.layers,1,format,GPU_TEXTURE_USAGE_GENERAL,nullptr);
  if(!texture)std::fprintf(stderr,"BTS_GPU_MARGIN_TABLE_SKIPPED name=%s create_failed width=%d height=%d layers=%d\n",
                           name,shape.width,shape.height,shape.layers);
  return {texture,GPU_texture_free};
 }
 static bool update_table_rows(blender::gpu::Texture *image,int row,int rows,int width,const void *data){
  using namespace blender;
  if(!image||rows<=0||width<=0)return false;
  const int tw=GPU_texture_width(image),th=GPU_texture_height(image),layers=std::max(1,GPU_texture_layer_count(image));
  if(tw<1||th<1||width>tw){std::fprintf(stderr,"BTS_GPU_MARGIN_UPDATE_SKIPPED invalid_texture width=%d height=%d request_width=%d\n",tw,th,width);return false;}
  const int y=row%th,z=row/th;
  if(z<0||z>=layers||y<0||y>=th||y+rows>th){
   std::fprintf(stderr,"BTS_GPU_MARGIN_UPDATE_SKIPPED out_of_bounds row=%d rows=%d height=%d layers=%d\n",row,rows,th,layers);
   return false;
  }
  GPU_texture_update_sub(image,GPU_DATA_UINT,data,0,y,z,width,rows,1);return true;
 }
 static bool upload_table(Texture &image,int &row,const char *name,const void *data,std::size_t bytes,
                          int texel_bytes,blender::gpu::TextureFormat format,std::uint64_t &budget,
                          performance::Clock::time_point start,bool blocking){
  using namespace blender;
  if(texel_bytes<=0)return false;
  if(!image)image=table(name,(bytes+std::size_t(texel_bytes)-1)/std::size_t(texel_bytes),format);
  if(!image)return false;
  const std::size_t stride=std::size_t(row_width)*std::size_t(texel_bytes);const auto *source=static_cast<const unsigned char *>(data);
  const int height=std::max(1,GPU_texture_height(image.get()));
  while(std::size_t(row)*stride<bytes){
   if(budget<stride||(!blocking&&performance::ms(start)>=4))return false;
   const int y=row%height;
   const std::size_t remaining=bytes-std::size_t(row)*stride;
   const int room=height-y;
   if(room<=0){std::fprintf(stderr,"BTS_GPU_MARGIN_UPDATE_SKIPPED name=%s row=%d height=%d\n",name,row,height);return false;}
   if(remaining>=stride){
    const int rows=int(std::min<std::size_t>({64,budget/stride,remaining/stride,std::size_t(room)}));
    if(rows<=0)return false;
    if(!update_table_rows(image.get(),row,rows,row_width,source+std::size_t(row)*stride))return false;
    budget-=std::size_t(rows)*stride;row+=rows;
   }else{
    std::array<unsigned char,row_width*16> tail{};std::memcpy(tail.data(),source+std::size_t(row)*stride,remaining);
    if(!update_table_rows(image.get(),row,1,row_width,tail.data()))return false;
    budget-=stride;++row;
   }
  }return true;
 }
 static const char *common() {return R"GLSL(
ivec3 slot(uint i){uint h=uint(table_layer_rows);if(h==0u)h=1u;uint row=i/1024u;return ivec3(int(i%1024u),int(row%h),int(row/h));}
bool changed(uint pixel){vec2 presence=texelFetch(force_image,ivec2(0),0).rg;if(presence.x!=presence.y)return true;ivec2 p=ivec2(pixel%uint(tile_size.x),pixel/uint(tile_size.x));return texelFetch(dirty_chunks,p/256,0).x!=0u;}
)GLSL";}
 static Shader gather_shader(bool array) {
  using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(128);
  info.sampler(7,ImageType::Float2D,"force_image");
  info.sampler(0,ImageType::Uint2DArray,"original_pixels").sampler(1,ImageType::Uint2D,"dirty_chunks")
      .sampler(2,array?ImageType::Float2DArray:ImageType::Float2D,"source_image")
      .image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::write,ImageReadWriteType::image2DArray,"original_values")
      .push_constant(Type::int_t,"count").push_constant(Type::int2_t,"tile_size").push_constant(Type::int3_t,"target_offset")
      .push_constant(Type::int_t,"table_layer_rows");
  info.compute_source_generated=std::string(common())+R"GLSL(
void main(){uint i=gl_GlobalInvocationID.x;if(i>=uint(count))return;uint pixel=texelFetch(original_pixels,slot(i),0).x;
 if(!changed(pixel))return;ivec2 p=ivec2(pixel%uint(tile_size.x),pixel/uint(tile_size.x));
)GLSL"+(array?std::string("vec4 value=texelFetch(source_image,ivec3(p+target_offset.xy,target_offset.z),0);"):
                   std::string("vec4 value=texelFetch(source_image,p+target_offset.xy,0);"))+
       "imageStore(original_values,slot(i),value);}";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};
 }
 static Shader evaluate_shader() {
  using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(128);
  info.sampler(7,ImageType::Float2D,"force_image");
  info.sampler(0,ImageType::Uint2DArray,"instructions").sampler(1,ImageType::Uint2D,"dirty_chunks")
      .sampler(2,ImageType::Uint2DArray,"original_pixels").sampler(3,ImageType::Float2DArray,"original_values")
      .sampler(4,ImageType::Uint2DArray,"schedule")
      .image(0,gpu::TextureFormat::SFLOAT_32_32_32_32,Qualifier::read_write,ImageReadWriteType::image2DArray,"results")
      .image(1,gpu::TextureFormat::UINT_32,Qualifier::read_write,ImageReadWriteType::uimage2DArray,"generations")
      .push_constant(Type::int_t,"count").push_constant(Type::int_t,"start")
      .push_constant(Type::int_t,"epoch").push_constant(Type::int2_t,"tile_size")
      .push_constant(Type::int_t,"table_layer_rows");
  info.compute_source_generated=std::string(common())+R"GLSL(
vec4 donor(uint ref){if(ref==0xffffffffu)return vec4(0);if((ref&0x80000000u)!=0u)return texelFetch(original_values,slot(ref&0x7fffffffu),0);return imageLoad(results,slot(ref));}
bool donor_changed(uint ref){if(ref==0xffffffffu)return false;if((ref&0x80000000u)!=0u)return changed(texelFetch(original_pixels,slot(ref&0x7fffffffu),0).x);return imageLoad(generations,slot(ref)).x==uint(epoch);}
void main(){
 uint invocation=gl_GlobalInvocationID.x;if(invocation>=uint(count))return;
 uint id=texelFetch(schedule,slot(invocation+uint(start)),0).x;
 uvec4 meta=texelFetch(instructions,slot(id*5u),0),r0=texelFetch(instructions,slot(id*5u+1u),0),r1=texelFetch(instructions,slot(id*5u+2u),0);
 bool dirty=epoch==1;for(int j=0;j<4;j++)dirty=dirty||donor_changed(r0[j])||donor_changed(r1[j]);if(!dirty)return;
 vec4 w0=uintBitsToFloat(texelFetch(instructions,slot(id*5u+3u),0)),w1=uintBitsToFloat(texelFetch(instructions,slot(id*5u+4u),0));
 precise vec4 value=vec4(0);
 if(meta.y!=0u)value=(donor(r0.x)*w0.x+donor(r0.y)*w0.y)+(donor(r0.z)*w0.z+donor(r0.w)*w0.w);
 else {for(int j=0;j<4;j++)value+=donor(r0[j])*w0[j];for(int j=0;j<4;j++)value+=donor(r1[j])*w1[j];value/=uintBitsToFloat(meta.z);}
 imageStore(results,slot(id),value);imageStore(generations,slot(id),uvec4(uint(epoch)));
}
)GLSL";
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};
 }
 static Shader commit_shader(blender::gpu::TextureFormat format,bool array) {
  using namespace blender;using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");info.local_group_size(128);
  info.sampler(7,ImageType::Float2D,"force_image");
  info.sampler(0,ImageType::Uint2DArray,"outputs").sampler(1,ImageType::Uint2D,"dirty_chunks")
      .sampler(2,ImageType::Float2DArray,"results").sampler(3,ImageType::Uint2DArray,"generations")
      .image(0,format,Qualifier::write,array?ImageReadWriteType::image2DArray:ImageReadWriteType::image2D,"target")
      .push_constant(Type::int_t,"count").push_constant(Type::int_t,"epoch")
      .push_constant(Type::int2_t,"tile_size").push_constant(Type::int3_t,"target_offset")
      .push_constant(Type::int_t,"table_layer_rows");
  info.compute_source_generated=std::string(common())+R"GLSL(
void main(){uint i=gl_GlobalInvocationID.x;if(i>=uint(count))return;uvec2 outref=texelFetch(outputs,slot(i),0).xy;
 if(!changed(outref.x)&&texelFetch(generations,slot(outref.y),0).x!=uint(epoch))return;
 ivec2 p=ivec2(outref.x%uint(tile_size.x),outref.x/uint(tile_size.x));vec4 value=texelFetch(results,slot(outref.y),0);
)GLSL"+(array?std::string("imageStore(target,ivec3(p+target_offset.xy,target_offset.z),value);}"):
                  std::string("imageStore(target,p+target_offset.xy,value);}"));
  return {GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),GPU_shader_free};
 }
 static void bind_table_rows(blender::gpu::Shader *shader){blender::GPU_shader_uniform_1i(shader,"table_layer_rows",layer_rows());}
public:
 bool prepare_shaders(){
  using namespace blender;
  for(bool array:{false,true})if(!gather_.contains(array))gather_.emplace(array,gather_shader(array));
  if(!evaluate_)evaluate_=evaluate_shader();
  for(auto format:{gpu::TextureFormat::SFLOAT_16_16_16_16,gpu::TextureFormat::SFLOAT_32_32_32_32})for(bool array:{false,true}){const auto key=std::pair{format,array};if(!commit_.contains(key))commit_.emplace(key,commit_shader(format,array));}
  return bool(evaluate_)&&std::all_of(gather_.begin(),gather_.end(),[](const auto &s){return bool(s.second);})&&std::all_of(commit_.begin(),commit_.end(),[](const auto &s){return bool(s.second);});
 }
 bool prepare(const std::shared_ptr<const GpuMarginPlan> &plan,std::uint64_t &budget,
              performance::Clock::time_point start,bool blocking=false){
  using namespace blender;if(!plan||plan->operations.empty())return true;
  auto &geometry=geometry_[plan.get()];geometry.plan=plan;
  if(geometry.unusable)return true;
  const auto finish_table=[&](Texture &image,int &uploaded,const char *name,const void *data,std::size_t bytes,int texel_bytes,gpu::TextureFormat format){
   if(!upload_table(image,uploaded,name,data,bytes,texel_bytes,format,budget,start,blocking)){
    if(!image){geometry.unusable=true;std::fprintf(stderr,"BTS_GPU_MARGIN_SKIPPED name=%s allocation_failed\n",name);return true;}
    return false;
   }return true;
  };
  if(!finish_table(geometry.operations,geometry.uploaded_rows[0],"BTS shared margin instructions",plan->operations.data(),plan->operations.size()*sizeof(GpuMarginPlan::Operation),16,gpu::TextureFormat::UINT_32_32_32_32))return false;
  if(geometry.unusable)return true;
  if(!finish_table(geometry.originals,geometry.uploaded_rows[1],"BTS shared margin donors",plan->original_pixels.data(),plan->original_pixels.size()*4,4,gpu::TextureFormat::UINT_32))return false;
  if(geometry.unusable)return true;
  if(!finish_table(geometry.schedule,geometry.uploaded_rows[2],"BTS shared margin schedule",plan->schedule.data(),plan->schedule.size()*4,4,gpu::TextureFormat::UINT_32))return false;
  if(geometry.unusable)return true;
  if(!finish_table(geometry.outputs,geometry.uploaded_rows[3],"BTS shared margin destinations",plan->outputs.data(),plan->outputs.size()*8,8,gpu::TextureFormat::UINT_32_32))return false;
  return true;
 }
 bool apply(const std::string &key,const std::shared_ptr<const GpuMarginPlan> &plan,
            blender::gpu::Texture *target,std::array<int,3> offset,std::vector<std::uint32_t> dirty,bool measure=false,
            blender::gpu::Texture *force=nullptr) {
  using namespace blender;performance::Scope timing("gpu_sparse_margin");
  last_operations_=last_originals_=0;
  if(!plan||plan->operations.empty())return true;
  const int cols=(plan->width+255)/256,rows=(plan->height+255)/256;
  if(dirty.size()!=std::size_t(cols)*rows)throw std::invalid_argument("Invalid margin dirty chunks");
  std::uint64_t unlimited=std::numeric_limits<std::uint64_t>::max();prepare(plan,unlimited,performance::Clock::now(),true);
  auto &geometry=geometry_.at(plan.get());
  if(geometry.unusable){std::fprintf(stderr,"BTS_GPU_MARGIN_SKIPPED reason=unusable_tables\n");return true;}
  if(!geometry.operations||!geometry.originals||!geometry.schedule||!geometry.outputs)return false;
  auto &state=states_[key];if(state.plan!=plan){state=State{};state.plan=plan;}
  if(!state.original_values)state.original_values=table("BTS margin original samples",plan->original_pixels.size(),gpu::TextureFormat::SFLOAT_32_32_32_32);
  if(!state.results)state.results=table("BTS margin results",plan->operations.size(),gpu::TextureFormat::SFLOAT_32_32_32_32);
  if(!state.generations)state.generations=table("BTS margin revisions",plan->operations.size(),gpu::TextureFormat::UINT_32);
  if(!state.dirty)state.dirty=Texture(GPU_texture_create_2d("BTS margin dirty chunks",cols,rows,1,gpu::TextureFormat::UINT_32,GPU_TEXTURE_USAGE_GENERAL,nullptr),GPU_texture_free);
  if(!state.original_values||!state.results||!state.generations||!state.dirty){
   std::fprintf(stderr,"BTS_GPU_MARGIN_SKIPPED reason=state_allocation\n");return true;}
  if(state.generation==0||state.generation==0x7ffffffeu){state.generation=0;std::fill(dirty.begin(),dirty.end(),1);}
  if(!force&&std::none_of(dirty.begin(),dirty.end(),[](auto value){return value!=0;}))return true;
  if(!force){if(!no_force_){const float zero[4]={};no_force_=Texture(GPU_texture_create_2d("BTS margin no global change",1,1,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,zero),GPU_texture_free);}force=no_force_.get();if(!force)return false;}
  ++state.generation;GPU_texture_update(state.dirty.get(),GPU_DATA_UINT,dirty.data());
  const bool array=GPU_texture_is_array(target);const auto format=GPU_texture_format(target);const auto shader_key=std::pair{format,array};
  if(!gather_.contains(array))gather_.emplace(array,gather_shader(array));if(!evaluate_)evaluate_=evaluate_shader();
  if(!commit_.contains(shader_key))commit_.emplace(shader_key,commit_shader(format,array));
  if(!gather_.at(array)||!evaluate_||!commit_.at(shader_key))return false;
  const int size[2]={plan->width,plan->height};
  auto *shader=gather_.at(array).get();GPU_shader_bind(shader);
  GPU_texture_bind(force,7);
  GPU_texture_bind(geometry.originals.get(),0);GPU_texture_bind(state.dirty.get(),1);GPU_texture_bind(target,2);GPU_texture_image_bind(state.original_values.get(),0);
  GPU_shader_uniform_1i(shader,"count",int(plan->original_pixels.size()));GPU_shader_uniform_2iv(shader,"tile_size",size);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());
  bind_table_rows(shader);
  GPU_compute_dispatch(shader,(plan->original_pixels.size()+127)/128,1,1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_unbind_all();GPU_texture_image_unbind_all();
  shader=evaluate_.get();GPU_shader_bind(shader);GPU_texture_bind(geometry.operations.get(),0);GPU_texture_bind(state.dirty.get(),1);
  GPU_texture_bind(force,7);
  GPU_texture_bind(geometry.originals.get(),2);GPU_texture_bind(state.original_values.get(),3);GPU_texture_bind(geometry.schedule.get(),4);
  GPU_texture_image_bind(state.results.get(),0);GPU_texture_image_bind(state.generations.get(),1);
  GPU_shader_uniform_1i(shader,"epoch",int(state.generation));GPU_shader_uniform_2iv(shader,"tile_size",size);
  bind_table_rows(shader);
  for(std::size_t i=0;i<plan->levels.size();++i){GPU_shader_uniform_1i(shader,"count",int(plan->levels[i].size()));GPU_shader_uniform_1i(shader,"start",int(plan->level_offsets[i]));GPU_compute_dispatch(shader,(plan->levels[i].size()+127)/128,1,1);GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);}
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);GPU_texture_unbind_all();GPU_texture_image_unbind_all();
  shader=commit_.at(shader_key).get();GPU_shader_bind(shader);GPU_texture_bind(geometry.outputs.get(),0);GPU_texture_bind(state.dirty.get(),1);GPU_texture_bind(state.results.get(),2);GPU_texture_bind(state.generations.get(),3);GPU_texture_image_bind(target,0);
  GPU_texture_bind(force,7);
  GPU_shader_uniform_1i(shader,"count",int(plan->outputs.size()));GPU_shader_uniform_1i(shader,"epoch",int(state.generation));GPU_shader_uniform_2iv(shader,"tile_size",size);GPU_shader_uniform_3iv(shader,"target_offset",offset.data());
  bind_table_rows(shader);
  GPU_compute_dispatch(shader,(plan->outputs.size()+127)/128,1,1);GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH|GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();GPU_texture_image_unbind_all();GPU_shader_unbind();
  if(measure)for(auto pixel:plan->original_pixels)last_originals_+=dirty[(pixel/plan->width/256)*cols+(pixel%plan->width/256)]!=0;
  // This is scheduled boundary work; unchanged instructions return immediately.
  last_operations_=plan->operations.size();return true;
 }
 std::uint64_t original_samples_updated()const{return last_originals_;}
};
}
