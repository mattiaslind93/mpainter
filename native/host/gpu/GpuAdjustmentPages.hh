// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuBrushRaster.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "AdjustmentPaintSource.hh"
#include <map>
#include <tuple>
#include <utility>
namespace bts {
// Owner-context immutable prefix pages plus one disposable own-over-prefix page.
// No CPU pixels or GPU objects escape in the metadata token.
class GpuAdjustmentPages {
  using Texture = std::unique_ptr<blender::gpu::Texture, decltype(&blender::GPU_texture_free)>;
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  using Key = std::tuple<QUuid, int, int, int, std::uint32_t>;
  struct Page {
    Texture image{nullptr, blender::GPU_texture_free};
    std::uint64_t used = 0;
  };
  std::uint64_t clock_ = 0;
  AuthoringCacheBudget budget_;
  std::optional<std::uint64_t> diagnostic_budget_;
  std::vector<QUuid> discarded_;
  std::map<Key, Page> pages_;
  Texture composed_{nullptr, blender::GPU_texture_free};
  Shader shader_{nullptr, blender::GPU_shader_free};
  static constexpr std::uint64_t page_bytes = 64ULL * 64 * 16;
  Texture allocate(const char *label)
  {
    using namespace blender;
    const auto free = texture_free_vram.load();
    const auto owned = (pages_.size() + bool(composed_)) * page_bytes;
    const auto limit = diagnostic_budget_.value_or(
        budget_.update(texture_vram_query_generation.load(),
                       owned,
                       free,
                       texture_vram_query_valid.load() || free != 0,
                       texture_vram_query_valid.load() ? 0 : texture_available_ram_bytes()));
    while ((pages_.size() + 2) * page_bytes > limit && !pages_.empty()) {
      auto victim = std::min_element(
          pages_.begin(), pages_.end(), [](const auto &a, const auto &b) {
            return a.second.used < b.second.used;
          });
      discarded_.push_back(std::get<0>(victim->first));
      pages_.erase(victim);
    }
    if (diagnostic_budget_ && 2 * page_bytes > limit)
      require_adjustment_page_pair(limit);
    Texture t(GPU_texture_create_2d(label,
                                    64,
                                    64,
                                    1,
                                    gpu::TextureFormat::SFLOAT_32_32_32_32,
                                    GPU_TEXTURE_USAGE_GENERAL,
                                    nullptr),
              GPU_texture_free);
    if (!t)
      throw std::bad_alloc();
    return t;
  }

 public:
  bool has_gpu() const
  {
    return bool(shader_) || bool(composed_) || !pages_.empty();
  }
  void diagnostic_budget(std::optional<std::uint64_t> limit)
  {
    diagnostic_budget_ = limit;
  }
  void prepare()
  {
    if (shader_)
      return;
    using namespace blender;
    using namespace gpu::shader;
    ShaderCreateInfo info("pyGPU_Shader");
    info.local_group_size(16, 16)
        .sampler(0, ImageType::Float2D, "prefix_img")
        .sampler(1, ImageType::Float2D, "own_img")
        .image(0,
               gpu::TextureFormat::SFLOAT_32_32_32_32,
               Qualifier::write,
               ImageReadWriteType::image2D,
               "result_img")
        .push_constant(Type::int2_t, "page_origin")
        .push_constant(Type::int_t, "author_size")
        .push_constant(Type::int_t, "capture_mode")
        .push_constant(Type::int_t, "encoded_normal");
    info.compute_source_generated = R"GLSL(
vec4 prefix_value(ivec2 p){ivec2 size=textureSize(prefix_img,0);vec2 at=(vec2(p+page_origin)+.5)/float(author_size)*vec2(size)-.5,f=fract(at);ivec2 q=ivec2(floor(at));vec4 a=texelFetch(prefix_img,clamp(q,ivec2(0),size-1),0),b=texelFetch(prefix_img,clamp(q+ivec2(1,0),ivec2(0),size-1),0),c=texelFetch(prefix_img,clamp(q+ivec2(0,1),ivec2(0),size-1),0),d=texelFetch(prefix_img,clamp(q+ivec2(1),ivec2(0),size-1),0);vec4 value=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);if(encoded_normal!=0)value.rgb=value.rgb*.5+vec3(value.a*.5);return value;}
void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);if(any(greaterThanEqual(p,ivec2(64))))return;vec4 lower=capture_mode==0?prefix_value(p):texelFetch(prefix_img,p,0);if(capture_mode!=0){vec4 own=texelFetch(own_img,p,0);lower=own+lower*(1-own.a);}lower.a=clamp(lower.a,0.0,1.0);imageStore(result_img,p,lower);}
)GLSL";
    trace_authoring_compilation();
    gpu_derived::stabilize_hosted_resource_owners(info);
    shader_.reset(
        GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)));
    if (!shader_)
      throw std::runtime_error("Adjustment source page shader failed");
    // Force first dispatch during the existing real GPU startup phase.
    auto dummy = allocate("BTS adjustment startup zero");
    composed_ = allocate("BTS adjustment own over prefix scratch");
    const float zero[4] = {};
    GPU_texture_clear(dummy.get(), GPU_DATA_FLOAT, zero);
    GPU_shader_bind(shader_.get());
    GPU_texture_bind(dummy.get(), 0);
    GPU_texture_bind(dummy.get(), 1);
    GPU_texture_image_bind(composed_.get(), 0);
    const int origin[2] = {0, 0};
    GPU_shader_uniform_2iv(shader_.get(), "page_origin", origin);
    GPU_shader_uniform_1i(shader_.get(), "author_size", 64);
    GPU_shader_uniform_1i(shader_.get(), "capture_mode", 1);
    GPU_shader_uniform_1i(shader_.get(), "encoded_normal", 0);
    GPU_compute_dispatch(shader_.get(), 4, 4, 1);
    trace_authoring_dispatch(shader_.get());
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                       GPU_BARRIER_TEXTURE_UPDATE);
    GPU_texture_image_unbind(composed_.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
  }
  bool contains(
      const AdjustmentPaintSnapshot &s, int channel, int tile, int size, std::uint32_t page) const
  {
    return pages_.contains(Key{s.id, channel, tile, size, page});
  }
  blender::gpu::Texture *compose(const AdjustmentPaintSnapshot &s,
                                 int channel,
                                 int tile,
                                 int size,
                                 std::uint32_t page,
                                 blender::gpu::Texture *prefix,
                                 blender::gpu::Texture *own)
  {
    using namespace blender;
    prepare();
    const Key key{s.id, channel, tile, size, page};
    auto found = pages_.find(key);
    if (found == pages_.end()) {
      if (!prefix)
        return nullptr;
      auto image = allocate("BTS immutable adjustment prefix page");
      GPU_shader_bind(shader_.get());
      GPU_texture_bind(prefix, 0);
      GPU_texture_bind(own, 1);
      GPU_texture_image_bind(image.get(), 0);
      const int origin[2] = {int(page % 65536) * 64, int(page / 65536) * 64};
      GPU_shader_uniform_2iv(shader_.get(), "page_origin", origin);
      GPU_shader_uniform_1i(shader_.get(), "author_size", size);
      GPU_shader_uniform_1i(shader_.get(), "capture_mode", 0);
      GPU_shader_uniform_1i(shader_.get(), "encoded_normal", channel == 6);
      GPU_compute_dispatch(shader_.get(), 4, 4, 1);
      trace_authoring_dispatch(shader_.get());
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
      GPU_texture_image_unbind(image.get());
      found = pages_.emplace(key, Page{std::move(image), ++clock_}).first;
    }
    if (!composed_)
      composed_ = allocate("BTS adjustment own over prefix scratch");
    GPU_shader_bind(shader_.get());
    found->second.used = ++clock_;
    GPU_texture_bind(found->second.image.get(), 0);
    GPU_texture_bind(own, 1);
    GPU_texture_image_bind(composed_.get(), 0);
    GPU_shader_uniform_1i(shader_.get(), "capture_mode", 1);
    GPU_compute_dispatch(shader_.get(), 4, 4, 1);
    trace_authoring_dispatch(shader_.get());
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(composed_.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    return composed_.get();
  }
  std::vector<QUuid> take_discarded()
  {
    return std::exchange(discarded_, {});
  }
  void evict()
  {
    pages_.clear();
    composed_.reset();
    discarded_.clear();
  }
  void retire(const QUuid &id)
  {
    std::erase_if(pages_, [&](const auto &p) { return std::get<0>(p.first) == id; });
  }
};
}  // namespace bts
