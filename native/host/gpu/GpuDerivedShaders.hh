#pragma once
#include "GPU_shader.hh"
#include "gpu_shader_create_info.hh"
#include <string>
namespace bts::gpu_derived {
using namespace blender;
inline constexpr char hosted_resource_owner[] = "pyGPU_Shader";
// These hosted builders declare every resource locally. Blender Resource::info_name
// and SharedVariable::info_name are non-owning views of ShaderCreateInfo::name_.
// Copying the CreateInfo into a queued compiler job does not rebase those views;
// pin this known local namespace to literal storage before the source goes away.
// Resource/socket names are already literals and generated source strings are owned.
inline void stabilize_hosted_resource_owners(gpu::shader::ShaderCreateInfo &info)
{
  for (auto *resources :
       {&info.pass_resources_, &info.batch_resources_, &info.geometry_resources_})
    for (auto &resource : *resources)
      resource.info_name = hosted_resource_owner;
  for (auto &variable : info.shared_variables_)
    variable.info_name = hosted_resource_owner;
}
inline gpu::shader::ShaderCreateInfo coverage_info()
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.shared_variable(Type::uint_t, "present[256]")
      .local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "prefix_img")
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::write,
             ImageReadWriteType::image2D,
             "flags_img")
      .push_constant(Type::int2_t, "origin")
      .push_constant(Type::int2_t, "size");
  info.compute_source_generated = R"GLSL(
CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
void main(){
 ivec2 p=origin+ivec2(gl_GlobalInvocationID.xy);uint lane=gl_LocalInvocationIndex;
 present[lane]=all(lessThan(p,size))&&texelFetch(prefix_img,min(p,textureSize(prefix_img,0)-1),0).a>0?1u:0u;
 barrier();for(uint stride=128u;stride>0u;stride>>=1u){if(lane<stride)present[lane]|=present[lane+stride];barrier();}
 if(lane==0u)imageStore(flags_img,origin/16+ivec2(gl_WorkGroupID.xy),vec4(float(present[0])));
})GLSL";
  stabilize_hosted_resource_owners(info);
  return info;
}
inline gpu::Shader *coverage_shader()
{
  auto info = coverage_info();
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
}
inline gpu::shader::ShaderCreateInfo presence_info()
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.shared_variable(Type::uint_t, "present[256]")
      .local_group_size(256)
      .sampler(0, ImageType::Float2D, "flags_img")
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::read_write,
             ImageReadWriteType::image2D,
             "presence_img")
      .push_constant(Type::int_t, "first_tile");
  info.compute_source_generated = R"GLSL(
CREATE_INFO_RES_SHARED_VARS_pyGPU_Shader
void main(){
 uint lane=gl_LocalInvocationIndex;ivec2 size=textureSize(flags_img,0);uint total=uint(size.x*size.y);uint found=0u;
 for(uint i=lane;i<total;i+=256u)found|=texelFetch(flags_img,ivec2(int(i)%size.x,int(i)/size.x),0).r>0?1u:0u;
 present[lane]=found;barrier();for(uint stride=128u;stride>0u;stride>>=1u){if(lane<stride)present[lane]|=present[lane+stride];barrier();}
 if(lane==0u){vec4 previous=imageLoad(presence_img,ivec2(0));imageStore(presence_img,ivec2(0),vec4(max(float(present[0]),first_tile!=0?0.0:previous.r),first_tile!=0?previous.r:previous.g,0,0));}
})GLSL";
  stabilize_hosted_resource_owners(info);
  return info;
}
inline gpu::Shader *presence_shader()
{
  auto info = presence_info();
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
}
inline gpu::shader::ShaderCreateInfo ao_info(gpu::TextureFormat format,
                                             bool array,
                                             const char *quant_source)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "prefix_img")
      .sampler(1, ImageType::Float2D, "baked_img")
      .sampler(2, ImageType::Float2D, "presence_img")
      .sampler(3, ImageType::Uint2D, "dirty_img")
      .image(0,
             format,
             Qualifier::write,
             array ? ImageReadWriteType::image2DArray : ImageReadWriteType::image2D,
             "result_img")
      .push_constant(Type::int2_t, "size")
      .push_constant(Type::int3_t, "target_offset")
      .push_constant(Type::float_t, "base_value")
      .push_constant(Type::int_t, "authored_enabled")
      .push_constant(Type::int_t, "replace_mode")
      .push_constant(Type::int_t, "value_precision");
  std::string body = R"GLSL(
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;
 vec2 presence=texelFetch(presence_img,ivec2(0),0).rg;
 if(presence.x==presence.y&&texelFetch(dirty_img,p/256,0).r==0u)return;
 vec4 prefix=texelFetch(prefix_img,min(p,textureSize(prefix_img,0)-1),0);
 vec4 baked=textureLod(baked_img,(vec2(p)+.5)/vec2(size),0);
 float value=baked.a>1e-8?clamp(baked.r/baked.a,0.0,1.0):1.0;
 if(authored_enabled!=0&&texelFetch(presence_img,ivec2(0),0).r>0){
  float authored=clamp(prefix.r+base_value*(1-prefix.a),0.0,1.0);
  value=replace_mode!=0?authored:authored*value;
 }
 vec4 result=vec4(quantize_value(vec3(value),value_precision),1);
 STORE_RESULT
})GLSL";
  body.replace(body.find("STORE_RESULT"),
               12,
               array ? "imageStore(result_img,ivec3(p+target_offset.xy,target_offset.z),result);" :
                       "imageStore(result_img,p+target_offset.xy,result);");
  info.compute_source_generated = std::string(quant_source) + body;
  stabilize_hosted_resource_owners(info);
  return info;
}
inline gpu::Shader *ao_shader(gpu::TextureFormat format, bool array, const char *quant_source)
{
  auto info = ao_info(format, array, quant_source);
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
}
inline gpu::shader::ShaderCreateInfo normal_info(gpu::TextureFormat format,
                                                 bool array,
                                                 const char *quant_source)
{
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "prefix_img")
      .sampler(1, ImageType::Float2D, "baked_img")
      .sampler(2, ImageType::Float2D, "presence_img")
      .sampler(3, ImageType::Float2D, "neighbors_img");
  static constexpr const char *height_names[] = {"height0",
                                                 "height1",
                                                 "height2",
                                                 "height3",
                                                 "height4",
                                                 "height5",
                                                 "height6",
                                                 "height7",
                                                 "height8"};
  for (int i = 0; i < 9; ++i)
    info.sampler(4 + i, ImageType::Float2D, height_names[i]);
  info.sampler(13, ImageType::Uint2D, "dirty_img");
  info.image(0,
             format,
             Qualifier::write,
             array ? ImageReadWriteType::image2DArray : ImageReadWriteType::image2D,
             "result_img")
      .push_constant(Type::int2_t, "size")
      .push_constant(Type::int3_t, "target_offset")
      .push_constant(Type::float3_t, "normal_base")
      .push_constant(Type::float3_t, "quantized_base")
      .push_constant(Type::float_t, "height_base")
      .push_constant(Type::int_t, "height_precision")
      .push_constant(Type::int_t, "authored_enabled")
      .push_constant(Type::int_t, "height_enabled")
      .push_constant(Type::int_t, "sobel")
      .push_constant(Type::int_t, "directx")
      .push_constant(Type::int_t, "replace_mode")
      .push_constant(Type::int_t, "value_precision");
  std::string body = R"GLSL(
vec3 unit_normal(vec3 v){float len=length(v);return len<1e-12?vec3(0,0,1):v/len;}
vec3 rnm(vec3 base,vec3 detail){
 base=unit_normal(base);detail=unit_normal(detail);
 if(base.z< -1+1e-6)return unit_normal(vec3(detail.x,-detail.y,-detail.z));
 vec3 t=base+vec3(0,0,1),u=vec3(-detail.xy,detail.z);return unit_normal(t*dot(t,u)-u*t.z);
}
vec4 height_prefix(int index,ivec2 p){
 FETCH_HEIGHT
}
float height_texel(int index,ivec2 p){
 vec4 prefix=height_prefix(index,p);
 if(texelFetch(neighbors_img,ivec2(index,0),0).w!=0)return height_base*(1-prefix.a)+prefix.r;
 vec3 full=prefix.rgb+height_base*(1-prefix.a);
 float q=quantize_value(full,height_precision).r;
 // Match the premultiplied-image reconstruction used by the CPU reference.
 precise float rgba=q-height_base*(1-prefix.a);return height_base*(1-prefix.a)+rgba;
}
float height_at(ivec2 p){
 if(height_enabled==0)return 0;
 vec2 uv=(vec2(p)+.5)/vec2(size);ivec2 direction=ivec2(0);
 if(uv.x<0){direction.x=-1;uv.x+=1;}else if(uv.x>=1){direction.x=1;uv.x-=1;}
 if(uv.y<0){direction.y=-1;uv.y+=1;}else if(uv.y>=1){direction.y=1;uv.y-=1;}
 int index=(direction.y+1)*3+direction.x+1;
 vec4 meta=texelFetch(neighbors_img,ivec2(index,0),0);
 if(meta.z==0){index=4;meta=texelFetch(neighbors_img,ivec2(4,0),0);uv=(vec2(clamp(p,ivec2(0),size-1))+.5)/vec2(size);}
 vec2 position=uv*meta.xy-.5,f=fract(position);ivec2 lo=ivec2(floor(position));
 return mix(mix(height_texel(index,lo),height_texel(index,lo+ivec2(1,0)),f.x),
            mix(height_texel(index,lo+ivec2(0,1)),height_texel(index,lo+ivec2(1)),f.x),f.y);
}
void main(){
 ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,size)))return;
 vec2 presence=texelFetch(presence_img,ivec2(0),0).rg;
 if(presence.x==presence.y&&texelFetch(dirty_img,p/256,0).r==0u)return;
 vec4 prefix=texelFetch(prefix_img,min(p,textureSize(prefix_img,0)-1),0);
 vec3 authored=prefix.a>0?prefix.rgb/prefix.a:vec3(0,0,1);float authored_length=length(authored);authored=authored_length>1e-8?authored/authored_length:vec3(0,0,1);
 vec4 result=vec4((authored*.5+.5)*prefix.a,prefix.a);
 vec3 mixed=unit_normal((normal_base*(1-result.a)+result.rgb)*2-1);
 vec4 baked=textureLod(baked_img,(vec2(p)+.5)/vec2(size),0);
 if(baked.a>1e-8){vec3 detail=baked.rgb/baked.a*2-1;if(directx!=0)detail.y=-detail.y;detail=unit_normal(detail);
  if(authored_enabled==0||texelFetch(presence_img,ivec2(0),0).r<=0)mixed=detail;
  else if(replace_mode==0)mixed=rnm(detail,mixed);
 }
 float du=0,dv=0;
 if(height_enabled!=0){if(sobel==0){du=height_at(p+ivec2(1,0))-height_at(p-ivec2(1,0));dv=height_at(p+ivec2(0,1))-height_at(p-ivec2(0,1));}
  else for(int o=-1;o<=1;o++){float w=o==0?.5:.25;du+=w*(height_at(p+ivec2(1,o))-height_at(p+ivec2(-1,o)));dv+=w*(height_at(p+ivec2(o,1))-height_at(p+ivec2(o,-1)));}
 }
 vec3 detail=unit_normal(vec3(-du*size.x*.5*.005,-dv*size.y*.5*.005,1));
 bool height_effect=height_enabled!=0&&(abs(detail.x)+abs(detail.y)>1e-8||height_prefix(4,p).a>=1.175494351e-38);
 if(height_effect)mixed=rnm(mixed,detail);
 if(baked.a>1e-8||height_effect)result=vec4(mixed*.5+.5,1);
 result.rgb=quantize_value(result.rgb+normal_base*(1-result.a),value_precision)-quantized_base*(1-result.a);
 STORE_RESULT
})GLSL";
  std::string fetch;
  for (int i = 0; i < 9; ++i)
    fetch += "if(index==" + std::to_string(i) + ")return texelFetch(height" + std::to_string(i) +
             ",clamp(p,ivec2(0),textureSize(height" + std::to_string(i) + ",0)-1),0);\n";
  fetch += "return vec4(0);";
  body.replace(body.find("FETCH_HEIGHT"), 12, fetch);
  body.replace(body.find("STORE_RESULT"),
               12,
               array ? "imageStore(result_img,ivec3(p+target_offset.xy,target_offset.z),result);" :
                       "imageStore(result_img,p+target_offset.xy,result);");
  info.compute_source_generated = std::string(quant_source) + body;
  stabilize_hosted_resource_owners(info);
  return info;
}
inline gpu::Shader *normal_shader(gpu::TextureFormat format, bool array, const char *quant_source)
{
  auto info = normal_info(format, array, quant_source);
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
}
}  // namespace bts::gpu_derived
