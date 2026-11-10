// SPDX-License-Identifier: GPL-2.0-or-later
// A UV-bakepixel stencil, not a camera/screen-footprint implementation.
#include <epoxy/gl.h>
#include "NativeMaterialFragmentBumpTest.hh"
#include "BKE_main.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_hash.h"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_node_types.h"
#include "GPU_batch.hh"
#include "GPU_framebuffer.hh"
#include "GPU_material.hh"
#include "GPU_material_hosted.hh"
#include "GPU_shader.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "GPU_uniform_buffer.hh"
#include "GPU_vertex_buffer.hh"
#include "MEM_guardedalloc.h"
#include "NOD_shader.h"
#include "gpu_shader_create_info.hh"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QScopeGuard>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bts {
namespace {
using namespace blender;
constexpr double tolerance = 3e-5;
void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
struct Variant {
  const char *name;
  float strength, distance, width;
  bool invert;
};
struct Compilation {
  bNodeTree *tree = nullptr;
  float width = 0;
  std::string variant;
  gpu::shader::StageInterfaceInfo varying{"bts_bump_stencil_interface"};
  std::string generated, error;
  int functions = 0;
  Compilation(bNodeTree *tree, const Variant &v) : tree(tree), width(v.width), variant(v.name)
  {
    using namespace gpu::shader;
    varying.smooth(Type::float2_t, "stencil_uv")
        .smooth(Type::float3_t, "stencil_P")
        .smooth(Type::float3_t, "stencil_N")
        .smooth(Type::float3_t, "stencil_Ng")
        .smooth(Type::float3_t, "stencil_T")
        .smooth(Type::float3_t, "stencil_B");
  }
};
bNodeSocket &socket(bNode &node, const char *id, bool input)
{
  for (auto &s : input ? node.inputs : node.outputs)
    if (STREQ(s.identifier, id))
      return s;
  throw std::runtime_error(std::string("Fragment Bump fixture socket missing: ") + id);
}
bNodeTree *make_tree(Main &main, const Variant &v)
{
  auto *tree = bke::node_tree_add_tree(&main, "UV fragment Bump stencil", "ShaderNodeTree");
  tree->flag |= NTREE_IS_GPU_SHADER_INTERNAL;
  const auto add = [&](const char *type) {
    return bke::node_add_node(nullptr, *tree, UString(type));
  };
  const auto link = [&](bNode &from, const char *out, bNode &to, const char *in) {
    bke::node_add_link(*tree, from, socket(from, out, false), to, socket(to, in, true));
  };
  auto *uv = add("ShaderNodeAttribute");
  STRNCPY(static_cast<NodeShaderAttribute *>(uv->storage)->name, "BTS_StencilUV");
  auto *separate = add("ShaderNodeSeparateXYZ");
  link(*uv, "Vector", *separate, "Vector");
  auto *square = add("ShaderNodeMath");
  square->custom1 = NODE_MATH_MULTIPLY;
  auto *first = static_cast<bNodeSocket *>(square->inputs.first);
  bke::node_add_link(*tree, *separate, socket(*separate, "X", false), *square, *first);
  bke::node_add_link(*tree, *separate, socket(*separate, "X", false), *square, *first->next);
  auto *bump = add("ShaderNodeBump");
  bump->custom1 = v.invert;
  for (const auto &[name, value] : std::array<std::pair<const char *, float>, 3>{
           {{"Strength", v.strength}, {"Distance", v.distance}, {"Filter Width", v.width}}})
    static_cast<bNodeSocketValueFloat *>(socket(*bump, name, true).default_value)->value = value;
  link(*square, "Value", *bump, "Height");  // Normal deliberately unlinked.
  auto *normal = add("ShaderNodeOutputAOV"), *height = add("ShaderNodeOutputAOV");
  STRNCPY(static_cast<NodeShaderOutputAOV *>(normal->storage)->name, "BTS_StencilNormal");
  STRNCPY(static_cast<NodeShaderOutputAOV *>(height->storage)->name, "BTS_StencilHeight");
  link(*bump, "Normal", *normal, "Color");
  link(*square, "Value", *height, "Value");
  add("ShaderNodeOutputMaterial");
  BKE_ntree_update_after_single_tree_change(main, *tree);
  return tree;
}
void construct(void *data, GPUMaterial *material)
{
  auto &c = *static_cast<Compilation *>(data);
  ntreeGPUMaterialNodes(c.tree, material);
  // Filter Width is a literal inside dF_branch_incomplete, not just a UBO value.
  // Separate this fragment program from production compute programs and from
  // every different finite-offset stencil even on a hosted pass-cache hit.
  const auto key = BLI_hash_string(
      ("AW fragment Bump stencil v1 " + c.variant + " width=" + std::to_string(c.width)).c_str());
  GPU_material_set_hosted_cache_key(material, key);
}
void codegen_impl(Compilation &c, GPUMaterial *material, GPUCodegenOutput &output)
{
  using namespace gpu::shader;
  auto &info = *reinterpret_cast<ShaderCreateInfo *>(output.create_info);
  info.vertex_inputs_.clear();
  info.vertex_out_interfaces_.clear();
  info.auto_resource_location(true)
      .vertex_in(0, Type::float2_t, "raster_uv")
      .vertex_in(1, Type::float2_t, "authored_uv")
      .vertex_in(2, Type::float3_t, "world_P")
      .vertex_in(3, Type::float3_t, "world_N")
      .vertex_in(4, Type::float3_t, "world_Ng")
      .vertex_in(5, Type::float3_t, "world_T")
      .vertex_in(6, Type::float3_t, "world_B")
      .vertex_out(c.varying)
      .fragment_out(0, Type::float4_t, "normal3_out")
      .fragment_out(1, Type::float4_t, "height_out")
      .fragment_out(2, Type::float4_t, "world_normal_out");
  info.vertex_source_generated = R"GLSL(
void main(){stencil_uv=authored_uv;stencil_P=world_P;stencil_N=world_N;
 stencil_Ng=world_Ng;stencil_T=world_T;stencil_B=world_B;
 gl_Position=vec4(raster_uv*2.0-1.0,0.0,1.0);}
)GLSL";
  info.vertex_source("gpu_shader_python_vert.glsl");
  info.generated_sources.append({"gpu_shader_python_vert.glsl", {}, info.vertex_source_generated});
  std::string fields, initialize;
  for (const auto &attribute : GPU_material_attributes(material)) {
    require(std::string_view(attribute.name) == "BTS_StencilUV",
            "Unexpected fragment fixture attribute");
    const bool vec3 = attribute.type == CD_ORCO;
    fields += (vec3 ? "vec3 v" : "vec4 v") + std::to_string(attribute.id) + ";\n";
    initialize += "var_attrs.v" + std::to_string(attribute.id) + "=" +
                  (vec3 ? "vec3(stencil_uv,0.0)" : "vec4(stencil_uv,0.0,1.0)") + ";\n";
  }
  require(!fields.empty(), "Fragment fixture lost its varying Attribute node");
  for (auto *resources :
       {&info.batch_resources_, &info.pass_resources_, &info.geometry_resources_})
    for (auto &resource : *resources)
      resource.info_name = "bts_fragment_bump";
  std::string types = "struct Closure {float unused;};\n#define CLOSURE_DEFAULT Closure(0.0)\n" +
                      info.typedef_source_generated;
  types += "struct BtsAttributes {" + fields +
           "} var_attrs;\n"
           "float derivative_scale_get(){return 1.0;}\n"
           "vec4 attr_load_color_post(vec4 v){return v;}\n"
           "float attr_load_temperature_post(float v){return v;}\n"
           "vec4 attr_load_uniform(vec4 v,uint hash){return v;}\n";
  for (const char *frequency : {"PASS", "BATCH", "GEOMETRY"}) {
    const std::string macro = std::string("CREATE_INFO_RES_") + frequency + "_bts_fragment_bump";
    types += "\n#ifdef " + macro + "\n" + macro + "\n#endif\n";
  }
  info.generated_sources.append({"gpu_shader_python_typedef_lib.glsl", {}, types});
  Vector<StringRefNull> dependencies = {"gpu_shader_python_typedef_lib.glsl",
                                        "gpu_shader_codegen_lib.glsl"};
  std::string functions;
  // Same ordering as pinned EEVEE shader_codegen: ALL split functions and their
  // dependencies precede the surface graph. This is what compute-only stubs miss.
  c.functions = int(output.material_functions.size());
  require(c.functions > 0, "Bump Height split function missing");
  for (const auto &function : output.material_functions) {
    functions += function.serialized;
    dependencies.extend(function.dependencies);
  }
  auto body = output.surface.serialized;
  const std::string from = "node_output_aov(", to = "stencil_aov(";
  for (std::size_t p = 0; (p = body.find(from, p)) != std::string::npos; p += to.size())
    body.replace(p, from.size(), to);
  for (const auto &dependency : output.surface.dependencies)
    if (dependency != "gpu_shader_material_output_aov.glsl")
      dependencies.append(dependency);
  require(body.find("node_bump(") != std::string::npos &&
              body.find("dF_branch_incomplete(") != std::string::npos,
          "Fixture did not generate the real Blender Bump derivative path");
  const std::string evaluate = functions + R"GLSL(
vec3 stencil_world_normal;float stencil_height;bool stencil_has_normal,stencil_has_height;
void stencil_aov(vec4 color,float value,float hash,out Closure dummy){
 dummy=CLOSURE_DEFAULT;
 if(floatBitsToUint(hash)==)GLSL" +
                               std::to_string(BLI_hash_string("BTS_StencilNormal")) +
                               R"GLSL(u){stencil_world_normal=color.rgb;stencil_has_normal=true;}
 if(floatBitsToUint(hash)==)GLSL" +
                               std::to_string(BLI_hash_string("BTS_StencilHeight")) +
                               R"GLSL(u){stencil_height=value;stencil_has_height=true;}
}
Closure stencil_evaluate(){
)GLSL" + body + R"GLSL(
}
void main(){
 g_data.P=stencil_P;g_data.N=normalize(stencil_N);g_data.Ni=stencil_N;g_data.Ng=normalize(stencil_Ng);
)GLSL" + initialize + R"GLSL(
 stencil_has_normal=false;stencil_has_height=false;stencil_world_normal=vec3(0);stencil_height=0;
 // All derivative-dependent code executes before any coverage decision.
 stencil_evaluate();
 vec3 N=normalize(stencil_N),T=normalize(stencil_T-N*dot(stencil_T,N));
 vec3 B=normalize(cross(N,T));if(dot(B,stencil_B)<0.0)B=-B;
 vec3 W=normalize(stencil_world_normal),Nt=normalize(vec3(dot(W,T),dot(W,B),dot(W,N)));
 float coverage=stencil_has_normal&&stencil_has_height&&gl_FrontFacing?1.0:0.0;
 normal3_out=vec4(0.5+0.5*Nt,coverage);height_out=vec4(vec3(stencil_height),coverage);
 world_normal_out=vec4(W,coverage);
}
)GLSL";
  info.fragment_source("gpu_shader_python_frag.glsl");
  info.generated_sources.append({"gpu_shader_python_frag.glsl", dependencies, evaluate});
  c.generated = types + info.vertex_source_generated + evaluate;
}
void codegen(void *data, GPUMaterial *material, GPUCodegenOutput *output)
{
  auto &c = *static_cast<Compilation *>(data);
  try {
    codegen_impl(c, material, *output);
  }
  catch (const std::exception &error) {
    // Never unwind through GPU_material_from_callbacks before its owner exists.
    c.error = error.what();
    using namespace gpu::shader;
    auto &info = *reinterpret_cast<ShaderCreateInfo *>(output->create_info);
    ShaderCreateInfo failed("AW rejected fragment Bump stencil");
    failed.vertex_source("gpu_shader_python_vert.glsl");
    failed.generated_sources.append(
        {"gpu_shader_python_vert.glsl", {}, "void main(){gl_Position=vec4(0);}"});
    failed.fragment_out(0, Type::float4_t, "rejected");
    failed.fragment_source("gpu_shader_python_frag.glsl");
    failed.generated_sources.append(
        {"gpu_shader_python_frag.glsl", {}, "void main(){rejected=vec4(0);}"});
    info = std::move(failed);
  }
}

// Owns/ restores even the enable bits absent from the public GPU getters.
struct State {
  gpu::FrameBuffer *fb = GPU_framebuffer_active_get();
  GPUBlend blend = GPU_blend_get();
  GPUDepthTest depth = GPU_depth_test_get();
  GPUFaceCullTest cull = GPU_face_culling_get();
  GPUStencilTest stencil = GPU_stencil_test_get();
  GPUWriteMask write = GPU_write_mask_get();
  int viewport[4], scissor[4];
  GLint front = 0;
  bool scissor_enabled = glIsEnabled(GL_SCISSOR_TEST), srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
  State()
  {
    GPU_viewport_size_get_i(viewport);
    GPU_scissor_get(scissor);
    glGetIntegerv(GL_FRONT_FACE, &front);
  }
  ~State()
  {
    if (fb)
      GPU_framebuffer_bind(fb);
    else
      GPU_framebuffer_restore();
    GPU_viewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    GPU_scissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    GPU_scissor_test(scissor_enabled);
    GPU_front_facing(front == GL_CW);
    GPU_blend(blend);
    GPU_depth_test(depth);
    GPU_face_culling(cull);
    GPU_stencil_test(stencil);
    GPU_write_mask(write);
    if (srgb)
      glEnable(GL_FRAMEBUFFER_SRGB);
    else
      glDisable(GL_FRAMEBUFFER_SRGB);
  }
};
struct Frame {
  const char *name;
  std::array<float, 3> T, B;
};
using Texture = std::unique_ptr<gpu::Texture, decltype(&GPU_texture_free)>;
struct Samples {
  std::array<std::vector<float>, 3> pixels;
};
Samples draw(GPUMaterial *material, int size, const Frame &frame)
{
  State state;
  Texture normal{nullptr, GPU_texture_free}, height{nullptr, GPU_texture_free},
      world{nullptr, GPU_texture_free};
  for (auto *image : {&normal, &height, &world}) {
    image->reset(GPU_texture_create_2d(
        "AW fragment stencil RGBA32F",
        size,
        size,
        1,
        gpu::TextureFormat::SFLOAT_32_32_32_32,
        GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_HOST_READ,
        nullptr));
    require(bool(*image), "Fragment Bump target allocation failed");
    const float sentinel[4]{-9, -9, -9, 0};
    GPU_texture_clear(image->get(), GPU_DATA_FLOAT, sentinel);
  }
  gpu::FrameBuffer *raw = nullptr;
  GPU_framebuffer_ensure_config(&raw,
                                {GPU_ATTACHMENT_NONE,
                                 GPU_ATTACHMENT_TEXTURE(normal.get()),
                                 GPU_ATTACHMENT_TEXTURE(height.get()),
                                 GPU_ATTACHMENT_TEXTURE(world.get())});
  std::unique_ptr<gpu::FrameBuffer, decltype(&GPU_framebuffer_free)> fbo(raw,
                                                                         GPU_framebuffer_free);
  char error[256]{};
  require(GPU_framebuffer_check_valid(raw, error), error);
  GPU_framebuffer_bind_no_srgb(raw);
  GPU_viewport(0, 0, size, size);
  GPU_scissor(0, 0, size, size);
  GPU_scissor_test(true);
  GPU_front_facing(false);
  GPU_blend(GPU_BLEND_NONE);
  GPU_depth_test(GPU_DEPTH_NONE);
  GPU_face_culling(GPU_CULL_NONE);
  GPU_stencil_test(GPU_STENCIL_NONE);
  GPU_color_mask(true, true, true, true);
  GPUVertFormat format{};
  const auto raster = GPU_vertformat_attr_add(
      &format, "raster_uv", gpu::VertAttrType::SFLOAT_32_32);
  const auto uv = GPU_vertformat_attr_add(&format, "authored_uv", gpu::VertAttrType::SFLOAT_32_32);
  std::array<unsigned, 5> ids{};
  int index = 0;
  for (const char *name : {"world_P", "world_N", "world_Ng", "world_T", "world_B"})
    ids[index++] = GPU_vertformat_attr_add(&format, name, gpu::VertAttrType::SFLOAT_32_32_32);
  std::unique_ptr<gpu::VertBuf, decltype(&GPU_vertbuf_discard)> vertices(
      GPU_vertbuf_create_with_format(format), GPU_vertbuf_discard);
  require(bool(vertices), "Fragment Bump vertices missing");
  GPU_vertbuf_data_alloc(*vertices, 6);
  const float corners[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
  // At (31,15) (64) and (63,31) (128) this gives the SAME authored
  // UV=(63/128,31/128) and world point.
  // The finite pixel footprint nevertheless changes from 1/64 to 1/128.
  const float offset = size == 128 ? -1.f / 256 : 0;
  float authored[6][2], position[6][3], N[6][3], T[6][3], B[6][3];
  for (int i = 0; i < 6; ++i) {
    authored[i][0] = corners[i][0] + offset;
    authored[i][1] = corners[i][1] + offset;
    for (int c = 0; c < 3; ++c) {
      position[i][c] = frame.T[c] * authored[i][0] + frame.B[c] * authored[i][1];
      N[i][c] = c == 2 ? 1 : 0;
      T[i][c] = frame.T[c];
      B[i][c] = frame.B[c];
    }
  }
  GPU_vertbuf_attr_fill(vertices.get(), raster, corners);
  GPU_vertbuf_attr_fill(vertices.get(), uv, authored);
  GPU_vertbuf_attr_fill(vertices.get(), ids[0], position);
  GPU_vertbuf_attr_fill(vertices.get(), ids[1], N);
  GPU_vertbuf_attr_fill(vertices.get(), ids[2], N);
  GPU_vertbuf_attr_fill(vertices.get(), ids[3], T);
  GPU_vertbuf_attr_fill(vertices.get(), ids[4], B);
  std::unique_ptr<gpu::Batch, decltype(&GPU_batch_discard)> batch(
      GPU_batch_create_ex(GPU_PRIM_TRIS, vertices.get(), nullptr, GPU_BATCH_OWNS_VBO),
      GPU_batch_discard);
  require(bool(batch), "Fragment Bump batch allocation failed");
  vertices.release();
  auto *shader = GPU_material_get_shader(material);
  require(shader, "Fragment Bump shader missing");
  GPU_batch_set_shader(batch.get(), shader);
  GPU_shader_bind(shader);
  auto *uniform = GPU_material_uniform_buffer_get(material);
  if (uniform)
    GPU_uniformbuf_bind(uniform, GPU_shader_get_ubo_binding(shader, "node_tree"));
  const auto unbind = qScopeGuard([&] {
    if (uniform)
      GPU_uniformbuf_unbind(uniform);
    GPU_shader_unbind();
  });
  GPU_batch_draw(batch.get());
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
  Samples result;
  int attachment = 0;
  for (auto *image : {normal.get(), height.get(), world.get()}) {
    auto *data = static_cast<float *>(GPU_texture_read(image, GPU_DATA_FLOAT, 0));
    require(data, "Fragment Bump diagnostic readback failed");
    const auto release = qScopeGuard([&] { MEM_delete(data); });
    result.pixels[attachment++].assign(data, data + size * size * 4);
  }
  return result;
}
QString height_hash(const Samples &samples)
{
  const auto &values = samples.pixels[1];
  return QString::fromLatin1(
      QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(values.data()),
                                              values.size() * sizeof(float)),
                               QCryptographicHash::Sha256)
          .toHex());
}
}  // namespace

QJsonObject verify_native_fragment_bump(const QString &directory)
{
  using namespace blender;
  require(QDir().mkpath(directory), "Cannot create fragment Bump proof directory");
  std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
  const std::array variants = {Variant{"strength0", 0, .3f, .5f, false},
                               Variant{"strength1", 1, .3f, .5f, false},
                               Variant{"strength_partial", .35f, .3f, .5f, false},
                               Variant{"distance0", 1, 0, .5f, false},
                               Variant{"invert", 1, .3f, .5f, true},
                               Variant{"wide", 1, .3f, 4, false}};
  const std::array frames = {Frame{"ordinary", {1, 0, 0}, {0, 1, 0}},
                             Frame{"mirrored", {-1, 0, 0}, {0, 1, 0}},
                             Frame{"rotated", {0, 1, 0}, {-1, 0, 0}}};
  QJsonArray cases;
  double maximum = 0;
  int reads = 0;
  std::uint64_t checks = 0;
  std::array<double, 2> resolution_normal{};
  std::array<float, 2> resolution_height{};
  // Compile six explicit graph/cache variants once, and reuse them for all
  // frame/resolution draws. Keep interface owners alive with their materials.
  using Material = std::unique_ptr<GPUMaterial, decltype(&GPU_material_free_single)>;
  std::vector<std::unique_ptr<Compilation>> compilations;
  std::vector<Material> materials;
  for (const auto &variant : variants) {
    auto *tree = make_tree(*main, variant);
    auto compilation = std::make_unique<Compilation>(tree, variant);
    Material material(
        GPU_material_from_callbacks(GPU_MAT_EEVEE, construct, codegen, compilation.get()),
        GPU_material_free_single);
    QFile generated(directory + "/" + variant.name + ".glsl");
    require(generated.open(QIODevice::WriteOnly),
            "Cannot preserve fragment Bump generated source");
    generated.write(QByteArray::fromStdString(compilation->generated));
    generated.close();
    require(compilation->error.empty(), compilation->error.c_str());
    require(compilation->functions > 0,
            "Fragment fixture unexpectedly reused an unrecorded cached program");
    require(material && GPU_material_status(material.get()) == GPU_MAT_SUCCESS,
            "Fragment Bump compilation failed");
    compilations.push_back(std::move(compilation));
    materials.push_back(std::move(material));
  }
  for (const int size : {64, 128})
    for (const auto &frame : frames) {
      QString raw_hash;
      std::vector<float> baseline_normal;
      for (std::size_t variant_index = 0; variant_index < variants.size(); ++variant_index) {
        const auto &variant = variants[variant_index];
        const auto &compilation = *compilations[variant_index];
        const auto name = QString("%1-%2-%3").arg(frame.name).arg(size).arg(variant.name);
        auto sample = draw(materials[variant_index].get(), size, frame);
        reads += 3;
        const auto hash = height_hash(sample);
        if (raw_hash.isEmpty())
          raw_hash = hash;
        require(hash == raw_hash, "Bump controls changed raw Height pixels");
        if (baseline_normal.empty())
          baseline_normal = sample.pixels[0];
        double case_error = 0, changed = 0;
        const double offset = size == 128 ? -1. / 256 : 0;
        // Exclude a six-pixel border and the shared triangle edge. No cross-island,
        // padding or triangle-edge derivative claim is hidden in this analytic test.
        for (int y = 6; y < size - 6; ++y)
          for (int x = 6; x < size - 6; ++x) {
            if (std::abs(x - y) < 6)
              continue;
            const double u = (x + .5) / size + offset;
            const double slope = (variant.invert ? -1 : 1) * variant.distance *
                                 (2 * u + variant.width / size);
            const double norm = std::hypot(slope, 1.0), nx = -slope / norm, nz = 1 / norm;
            const double mx = variant.strength * nx,
                         mz = (1 - variant.strength) + variant.strength * nz;
            const double mixed_norm = std::hypot(mx, mz);
            const std::array<double, 3> nt{mx / mixed_norm, 0, mz / mixed_norm};
            const auto i = (y * size + x) * 4;
            for (int c = 0; c < 4; ++c) {
              const double normal = c == 3 ? 1 : .5 + .5 * nt[c];
              const double raw = c == 3 ? 1 : u * u;
              const double world = c == 3 ? 1 :
                                            frame.T[c] * nt[0] + frame.B[c] * nt[1] +
                                                (c == 2 ? nt[2] : 0);
              for (const auto &[actual, expected] :
                   std::array<std::pair<double, double>, 3>{{{sample.pixels[0][i + c], normal},
                                                             {sample.pixels[1][i + c], raw},
                                                             {sample.pixels[2][i + c], world}}})
              {
                require(std::isfinite(actual), "Nonfinite fragment Bump output");
                case_error = std::max(case_error, std::abs(actual - expected));
                ++checks;
              }
              changed = std::max(
                  changed, std::abs(double(sample.pixels[0][i + c] - baseline_normal[i + c])));
            }
          }
        std::printf("BTS_FRAGMENT_BUMP case=%s max_error=%.12g raw_sha256=%s changed=%.9g\n",
                    qPrintable(name),
                    case_error,
                    qPrintable(hash),
                    changed);
        std::fflush(stdout);
        require(case_error <= tolerance,
                "Fragment Bump differs from independent finite-offset oracle");
        if (variant.strength > 0 && variant.distance > 0)
          require(changed > .01, "Active fragment Bump returned the unchanged normal");
        maximum = std::max(maximum, case_error);
        if (std::string_view(frame.name) == "ordinary" && std::string_view(variant.name) == "wide")
        {
          const int i = ((size == 64 ? 15 : 31) * size + (size == 64 ? 31 : 63)) * 4;
          resolution_normal[size == 64 ? 0 : 1] = sample.pixels[0][i];
          resolution_height[size == 64 ? 0 : 1] = sample.pixels[1][i];
        }
        cases.append(QJsonObject{{"name", name},
                                 {"size", size},
                                 {"strength", variant.strength},
                                 {"distance", variant.distance},
                                 {"invert", variant.invert},
                                 {"filter_width", variant.width},
                                 {"material_functions", compilation.functions},
                                 {"raw_height_sha256", hash},
                                 {"max_error", case_error},
                                 {"normal_changed_from_strength0", changed}});
      }
    }
  require(resolution_height[0] == resolution_height[1],
          "Resolution witness did not sample the same raw Height");
  require(std::abs(resolution_normal[0] - resolution_normal[1]) > .001,
          "UV pixel footprint witness did not change Normal3");
  return {{"contract", "UV-bakepixel only; no camera/screen derivative or texture LOD parity"},
          {"production_active_bump_enabled", false},
          {"internal_channel", 3},
          {"normal_encoding", "0.5 + 0.5 * tangent normal; coverage alpha"},
          {"format", "RGBA32F"},
          {"height", "u*u"},
          {"derivative_scale", 1},
          {"tolerance", tolerance},
          {"max_error", maximum},
          {"scalar_checks", qint64(checks)},
          {"diagnostic_readbacks", reads},
          {"same_uv_height_64", resolution_height[0]},
          {"same_uv_height_128", resolution_height[1]},
          {"same_uv_normal3_x_64", resolution_normal[0]},
          {"same_uv_normal3_x_128", resolution_normal[1]},
          {"cases", cases}};
}
}  // namespace bts
