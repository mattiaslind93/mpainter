#pragma once
#include <string>
#include "GPU_shader.hh"
#include "gpu_shader_create_info.hh"
namespace bts {
inline blender::gpu::Shader *compile_mask_output(blender::gpu::TextureFormat format, bool array)
{
  using namespace blender;
  using namespace gpu::shader;
  ShaderCreateInfo info("pyGPU_Shader");
  info.local_group_size(16, 16)
      .sampler(0, ImageType::Float2D, "mask_img")
      .image(0,
             format,
             Qualifier::write,
             array ? ImageReadWriteType::image2DArray : ImageReadWriteType::image2D,
             "result_img")
      .push_constant(Type::int2_t, "region_origin")
      .push_constant(Type::int2_t, "region_size")
      .push_constant(Type::int3_t, "target_offset");
  std::string source = R"GLSL(
void main(){ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,region_size)))return;
 ivec2 p=region_origin+local;float mask=clamp(texelFetch(mask_img,min(p,textureSize(mask_img,0)-1),0).r,0.0,1.0);
 STORE_RESULT
})GLSL";
  source.replace(
      source.find("STORE_RESULT"),
      12,
      array ?
          "imageStore(result_img,ivec3(p+target_offset.xy,target_offset.z),vec4(vec3(mask),1));" :
          "imageStore(result_img,p+target_offset.xy,vec4(vec3(mask),1));");
  info.compute_source_generated = source;
  return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
}
}  // namespace bts
