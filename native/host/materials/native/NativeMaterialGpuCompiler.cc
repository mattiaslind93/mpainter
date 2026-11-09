#include "NativeMaterialDependencies.hh"
#include "BLI_listbase.h"
#include "NativeMaterialGpuCompiler.hh"
#include "NativeMaterialMapping.hh"
#include "NativeMaterialProjectionSource.hh"
#include "NativeMaterialSourcePolicy.hh"
#include "BKE_node.hh"
#include "NOD_shader.h"
#include "DNA_node_types.h"
#include "DNA_customdata_types.h"
#include "GPU_material.hh"
#include "GPU_material_hosted.hh"
#include "gpu_shader_create_info.hh"
#include <charconv>
#include <stdexcept>
#include <QtGlobal>
namespace bts {
namespace {
using namespace blender;
std::string attribute_value(const GPUMaterialAttribute &attribute,
                            const NativeMaterialCompilation &compilation)
{
  const std::string_view name(attribute.name);
  const bool vec3 = attribute.type == CD_ORCO;
  if (name == "BTS_Dynamic_Zero")
    return vec3 ? "vec3(0.0)" : "vec4(0.0)";
  if (const auto mapping = read_native_mapping_attribute(name,
                                                         compilation.logical_parameter_count()))
  {
    const auto index = compilation.parameter_slots.empty() ?
                           mapping->parameter :
                           compilation.parameter_slots[mapping->parameter];
    if (index >= compilation.parameter_count)
      throw std::runtime_error("Invalid native Mapping physical binding");
    const auto slot = resource_sum(compilation.parameter_count,
                                   resource_sum(resource_product(index, 3), mapping->row));
    return "native_parameters[" + std::to_string(slot) + "]";
  }
  if (name.starts_with("BTS_Param")) {
    std::size_t index = 0;
    const auto suffix = name.substr(9);
    const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size())
      throw std::runtime_error("Invalid native parameter binding");
    if (!compilation.parameter_slots.empty()) {
      if (index >= compilation.parameter_slots.size())
        throw std::runtime_error("Invalid native logical parameter binding");
      index = compilation.parameter_slots[index];
    }
    if (index >= compilation.parameter_count)
      throw std::runtime_error("Invalid native parameter binding");
    return "native_parameters[" + std::to_string(index) + "]";
  }
  if (compilation.surface_context) {
    return native_material_compute_context_value(name, vec3);
  }
  if (compilation.fixture_context && name == "BTS_Mesh_AO")
    return "vec4(.65,.65,.65,1)";
  if (compilation.fixture_context && name == "BTS_Object_Random")
    return "vec4(0.42)";
  if (compilation.fixture_context && name == "BTS_Object_DisplacementScale")
    return "vec4(fixture_displacement_scale)";
  if (compilation.fixture_context &&
      (name.starts_with("BTS_Coord_") || name.starts_with("BTS_Geometry_") || name.empty()))
    return vec3 ? "vec3(0.25,0.5,0.75)" : "vec4(0.25,0.5,0.75,1.0)";
  throw std::runtime_error("Unsupported native compute attribute: " + std::string(name));
}
void construct(void *data, GPUMaterial *material)
{
  auto &compilation = *static_cast<NativeMaterialCompilation *>(data);
  try {
    compilation.surface_required = native_material_dependencies(
                                       *compilation.tree, compilation.logical_parameter_count())
                                       .surface;
  }
  catch (const std::exception &error) {
    compilation.error = error.what();
  }
  GPU_material_hosted_ramps_enable(material);
  ntreeGPUMaterialNodes(compilation.tree, material);
  std::uint32_t key = 2166136261u;
  const auto feed = [&](std::string_view value) {
    for (unsigned char c : value)
      key = (key ^ c) * 16777619u;
    key = (key ^ 255u) * 16777619u;
  };
  feed("mPainter native channels v12 reachable-eevee-source-context");
  feed(compilation.fixture_context ? "fixture" :
       compilation.surface_context ? "surface-v2-objects" :
                                     "parameters-only");
  for (const auto &attribute : GPU_material_attributes(material)) {
    if (std::string_view(attribute.name).starts_with(native_mapping_attribute_prefix))
      compilation.mapping_required = true;
    feed(attribute.name);
    feed(std::to_string(attribute.type));
    try {
      feed(attribute_value(attribute, compilation));
    }
    catch (const std::exception &error) {
      compilation.error = error.what();
    }
  }
  for (int i = 0; i < GPU_material_hosted_ramp_count(material); ++i) {
    int parameter, row;
    GPU_material_hosted_ramp_binding(material, i, parameter, row);
    feed("ramp");
    feed(std::to_string(parameter));
    feed(std::to_string(row));
  }
  feed(compilation.error.empty() ? "valid" : "invalid");
  if (!qEnvironmentVariableIsSet("BTS_NATIVE_MATERIAL_CACHE_LEGACY"))
    GPU_material_set_hosted_cache_key(material, key);
}
void codegen_impl(void *data, GPUMaterial *material, GPUCodegenOutput *output)
{
  using namespace gpu::shader;
  const auto &compilation = *static_cast<NativeMaterialCompilation *>(data);
  if (!compilation.error.empty())
    throw std::runtime_error(compilation.error);
  auto &info = *reinterpret_cast<ShaderCreateInfo *>(output->create_info);
  info.auto_resource_location(true)
      .local_group_size(8, 8)
      .image(0,
             gpu::TextureFormat::SFLOAT_32_32_32_32,
             Qualifier::write,
             ImageReadWriteType::Float2D,
             "target")
      .push_constant(Type::int_t, "selected_aov")
      .push_constant(Type::int_t, "scalar_aov")
      .push_constant(Type::float_t, "fixture_displacement_scale")
      .storage_buf(0, Qualifier::read, "float4", "native_parameters[]");
  if (compilation.surface_context) {
    info.storage_buf(2, Qualifier::read, "float4", "native_projection[]");
    info.storage_buf(1, Qualifier::read, "float4", "native_objects[]")
        .push_constant(Type::int_t, "native_object_count");
    info.sampler(0, ImageType::Uint2DArray, "native_surface")
        .push_constant(Type::int2_t, "native_surface_size")
        .push_constant(Type::int_t, "native_surface_stride")
        .push_constant(Type::float2_t, "native_tile_offset")
        .push_constant(Type::float4x4_t, "native_object_to_world")
        .push_constant(Type::float4x4_t, "native_world_to_object")
        .push_constant(Type::float3_t, "native_bounds_min")
        .push_constant(Type::float3_t, "native_bounds_max")
        .push_constant(Type::float4_t, "native_object_color")
        .push_constant(Type::float_t, "native_object_random")
        .push_constant(Type::int2_t, "native_region_origin")
        .push_constant(Type::int2_t, "native_region_size")
        .push_constant(Type::float3_t, "native_camera_position")
        .push_constant(Type::float3_t, "native_camera_forward")
        .push_constant(Type::int_t, "native_perspective")
        .push_constant(Type::float3x3_t, "native_color_to_aces")
        .sampler(1, ImageType::Float2D, "native_mesh_ao")
        .push_constant(Type::int_t, "native_has_ao")
        .push_constant(Type::float3_t, "native_map_u")
        .push_constant(Type::float3_t, "native_map_v");
  }
  info.vertex_inputs_.clear();
  info.vertex_out_interfaces_.clear();
  std::string attributes, initialize;
  int projection_domains = 0;
  for (const auto &attribute : GPU_material_attributes(material)) {
    const std::string_view name(attribute.name);
    if (name == "BTS_Coord_UV" || (name.empty() && attribute.type != CD_ORCO))
      projection_domains |= 1;
    else if (name == "BTS_Geometry_Position")
      projection_domains |= 2;
    else if (name == "BTS_Coord_Object")
      projection_domains |= 4;
    else if (name == "BTS_Coord_Generated" || (name.empty() && attribute.type == CD_ORCO))
      projection_domains |= 8;
    attributes += (attribute.type == CD_ORCO ? "vec3 v" : "vec4 v") +
                  std::to_string(attribute.id) + ";\n";
    initialize += "var_attrs.v" + std::to_string(attribute.id) + "=" +
                  attribute_value(attribute, compilation) + ";\n";
  }
  for (auto *resources :
       {&info.batch_resources_, &info.pass_resources_, &info.geometry_resources_})
    for (auto &resource : *resources)
      resource.info_name = "bts_native_nodetree";
  std::string types = "struct Closure {float unused;};\n#define CLOSURE_DEFAULT Closure(0.0)\n" +
                      info.typedef_source_generated;
  if (!attributes.empty())
    types += "struct BtsAttributes {\n" + attributes + "} var_attrs;\n";
  // Only explicit parameter attributes and diagnostic fixture coordinates are accepted.
  // Other attribute names are rejected above; these adapters do not add volume support.
  types +=
      "vec4 attr_load_color_post(vec4 x){return x;}\nfloat attr_load_temperature_post(float x){return x;}\nvec4 attr_load_uniform(vec4 x,uint hash){return x;}\n";
  for (const char *frequency : {"PASS", "BATCH", "GEOMETRY"}) {
    const std::string macro = std::string("CREATE_INFO_RES_") + frequency + "_bts_native_nodetree";
    types += "\n#ifdef " + macro + "\n" + macro + "\n#endif\n";
  }
  if (compilation.surface_context)
    types += R"GLSL(
vec3 bts_uv,bts_position,bts_normal,bts_object,bts_generated,bts_object_normal,bts_object_location;
vec3 bts_real_uv,bts_real_position,bts_real_normal,bts_real_object,bts_real_generated,bts_real_object_normal;
float bts_real_footprint;
float bts_displacement_scale,bts_mesh_ao,bts_object_random;vec4 bts_object_color;
uint bts_surface_word(int index){ivec3 n=textureSize(native_surface,0);int t=index/4;uvec4 v=texelFetch(native_surface,ivec3(t%n.x,(t/n.x)%n.y,t/(n.x*n.y)),0);return v[index%4];}
vec3 bts_surface_vec(int index){return vec3(uintBitsToFloat(bts_surface_word(index)),uintBitsToFloat(bts_surface_word(index+1)),uintBitsToFloat(bts_surface_word(index+2)));}
bool bts_context(ivec2 p){
 vec3 native_uv_point=vec3((vec2(p)+.5)/vec2(imageSize(target))+native_tile_offset,1.0);bts_uv=native_projection[0].x!=0?vec3(native_uv_point.xy,0):vec3(dot(native_map_u,native_uv_point),dot(native_map_v,native_uv_point),0.0);bts_real_footprint=0;
 bts_position=vec3(0);bts_normal=vec3(0,0,1);bool valid=true;uint owner=0u;
 mat4 object_to_world=native_object_to_world,world_to_object=native_world_to_object;vec3 bounds_min=native_bounds_min,bounds_max=native_bounds_max;bts_object_color=native_object_color;bts_object_random=native_object_random;
 if(native_surface_stride>0){ivec2 q=clamp(ivec2((vec2(p)+.5)*vec2(native_surface_size)/vec2(imageSize(target))),ivec2(0),native_surface_size-1);int index=(q.y*native_surface_size.x+q.x)*native_surface_stride;bts_position=bts_surface_vec(index);bts_normal=normalize(bts_surface_vec(index+3));bts_real_footprint=uintBitsToFloat(bts_surface_word(index+6));valid=(bts_surface_word(index+7)&255u)!=0u;if(native_object_count>0&&valid)owner=bts_surface_word(index+14);}
 if(native_object_count>0&&valid){
  int low=0,high=native_object_count;while(low<high){int mid=low+(high-low)/2;uint key=floatBitsToUint(native_objects[mid*12].x);if(key<owner)low=mid+1;else high=mid;}
  if(low>=native_object_count||floatBitsToUint(native_objects[low*12].x)!=owner)return false;int base=low*12;
  object_to_world=mat4(native_objects[base+1],native_objects[base+2],native_objects[base+3],native_objects[base+4]);world_to_object=mat4(native_objects[base+5],native_objects[base+6],native_objects[base+7],native_objects[base+8]);bounds_min=native_objects[base+9].xyz;bounds_max=native_objects[base+10].xyz;bts_object_color=native_objects[base+11];bts_object_random=native_objects[base].y;
 }
 bts_object_location=object_to_world[3].xyz;
 bts_object=(world_to_object*vec4(bts_position,1)).xyz;
 bts_generated=(bts_object-bounds_min)/max(bounds_max-bounds_min,vec3(1e-20));
 bts_object_normal=normalize(transpose(mat3(object_to_world))*bts_normal);
 bts_mesh_ao=native_has_ao!=0?textureLod(native_mesh_ao,(vec2(p)+.5)/vec2(imageSize(target)),0.0).r:1.0;
 bts_displacement_scale=1.0/max(length(mat3(world_to_object)*bts_normal),1e-20);
 bts_real_uv=bts_uv;bts_real_position=bts_position;bts_real_normal=bts_normal;bts_real_object=bts_object;bts_real_generated=bts_generated;bts_real_object_normal=bts_object_normal;
 return valid;
}
)GLSL";
  if (compilation.surface_context)
    types +=
        "const int bts_fill_domains=" + std::to_string(projection_domains) + ";\n" +
        native_material_projection_source() +
        "vec3 coordinate_incoming(vec3 P){return native_perspective!=0?normalize(native_camera_position-bts_real_position):normalize(native_camera_forward);}\n";
  else if (compilation.fixture_context)
    types += "vec3 coordinate_incoming(vec3 P){return vec3(0,0,1);}\n";
  info.generated_sources.append({"gpu_shader_compositor_nodetree_type.glsl", {}, types});
  auto body = output->surface.serialized;
  const std::string from = "node_output_aov(", to = "bts_output_aov(";
  for (std::size_t pos = 0; (pos = body.find(from, pos)) != std::string::npos; pos += to.size())
    body.replace(pos, from.size(), to);
  Vector<StringRefNull> dependencies;
  dependencies.append("gpu_shader_compositor_nodetree_type.glsl");
  dependencies.append("gpu_shader_codegen_lib.glsl");
  for (const auto &dependency : output->surface.dependencies)
    if (dependency != "gpu_shader_material_output_aov.glsl")
      dependencies.append(dependency);
  const std::string generated = R"GLSL(
vec4 bts_sample;
void bts_output_aov(vec4 color,float value,float hash,out Closure dummy){
 dummy=CLOSURE_DEFAULT;
 if(int(floatBitsToUint(hash))==selected_aov)bts_sample=scalar_aov!=0?vec4(value,value,value,1.0):vec4(color.rgb,1.0);
}
Closure bts_evaluate(){
)GLSL" + body + "\n}\n";
  info.generated_sources.append({"gpu_shader_compositor_store.glsl", dependencies, generated});
  std::string evaluate = R"GLSL(
void evaluate(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,imageSize(target))))return;bts_sample=vec4(0.0);BTS_INITIALIZE_ATTRIBUTES
bts_evaluate();imageStore(target,p,bts_sample);}
)GLSL";
  if (compilation.surface_context) {
    const auto begin = evaluate.find("ivec2 p=ivec2(gl_GlobalInvocationID.xy);");
    evaluate.replace(
        begin,
        std::string("ivec2 p=ivec2(gl_GlobalInvocationID.xy);").size(),
        "ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,native_region_size)))return;ivec2 p=local+native_region_origin;");
    // Graph attributes may be projected; built-in incoming/normal context stays
    // on the actual surface for every plane and footprint tap.
    initialize =
        "g_data.P=bts_real_position;g_data.N=bts_real_normal;g_data.Ng=bts_real_normal;\n" +
        initialize;
  }
  if (compilation.fixture_context)
    initialize = "g_data.P=vec3(.25,.5,.75);g_data.N=vec3(0,0,1);g_data.Ng=g_data.N;\n" +
                 initialize;
  if (compilation.surface_context)
    evaluate = R"GLSL(
void evaluate(){
 ivec2 local=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(local,native_region_size)))return;ivec2 p=local+native_region_origin;if(any(greaterThanEqual(p,imageSize(target))))return;
 if(!bts_context(p)||!bts_fill_prepare()){imageStore(target,p,vec4(0));return;}
 int mode=bts_fill_enabled()?bts_fill_projection_mode:0;
 int axes=mode==1?3:1,taps=mode>0&&mode<6&&bts_fill_source_filter==0?4:1;vec4 accumulated=vec4(0);
 for(int axis=0;axis<axes;axis++)for(int tap=0;tap<taps;tap++){
  float weight;if(!bts_fill_select(axis,tap,weight))continue;bts_sample=vec4(0);BTS_INITIALIZE_ATTRIBUTES
  bts_evaluate();accumulated+=vec4(bts_sample.rgb*bts_sample.a,bts_sample.a)*weight;
 }
 if(accumulated.a>0)accumulated.rgb/=accumulated.a;
 // Normalized plane/tap weights may sum one Float32 ULP above one.
 // Keep straight signed/HDR source values unchanged; only coverage is bounded.
 accumulated.a=clamp(accumulated.a,0.0,1.0);
 if(scalar_aov==0)accumulated.rgb=native_color_to_aces*accumulated.rgb;imageStore(target,p,accumulated);
}
)GLSL";
  const auto token = evaluate.find("BTS_INITIALIZE_ATTRIBUTES");
  evaluate.replace(token, std::string("BTS_INITIALIZE_ATTRIBUTES").size(), initialize);
  info.compute_source("gpu_shader_compositor_main.glsl");
  info.generated_sources.append(
      {"gpu_shader_compositor_eval.glsl", {"gpu_shader_compositor_store.glsl"}, evaluate});
  static_cast<NativeMaterialCompilation *>(data)->generated = types + generated + evaluate;
}
void codegen(void *data, GPUMaterial *material, GPUCodegenOutput *output)
{
  try {
    codegen_impl(data, material, output);
  }
  catch (const std::exception &error) {
    // Do not unwind through Blender's C-style callback: its GPUMaterial has not
    // reached our RAII owner yet. Return a valid no-op pass, then reject it below.
    static_cast<NativeMaterialCompilation *>(data)->error = error.what();
    using namespace gpu::shader;
    auto &info = *reinterpret_cast<ShaderCreateInfo *>(output->create_info);
    ShaderCreateInfo failed("mPainter unsupported native source");
    failed.local_group_size(1, 1).compute_source("gpu_shader_compositor_main.glsl");
    failed.generated_sources.append({"gpu_shader_compositor_eval.glsl", {}, "void evaluate(){}"});
    info = std::move(failed);
  }
}
}  // namespace
blender::GPUMaterial *compile_native_material(NativeMaterialCompilation &compilation)
{
  return blender::GPU_material_from_callbacks(GPU_MAT_EEVEE, construct, codegen, &compilation);
}
}  // namespace bts
