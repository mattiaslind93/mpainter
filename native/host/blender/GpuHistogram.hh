// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HistogramMetadata.hh"
#include "GPU_shader.hh"
#include <bit>
#include "GPU_texture.hh"
#include "GPU_compute.hh"
#include "GPU_state.hh"
#include "GPU_storage_buffer.hh"
#include "gpu_shader_create_info.hh"
#include "GpuReadbackFence.hh"
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
namespace bts {
// UI metadata only: the same 16x16 locations as the original histogram.
// Shader evaluation stays on the GPU; a zero-timeout fence gates 520 bytes of bins.
class GpuHistogram {
  using Shader = std::unique_ptr<blender::gpu::Shader, decltype(&blender::GPU_shader_free)>;
  using Buffer =
      std::unique_ptr<blender::gpu::StorageBuf, decltype(&blender::GPU_storagebuf_free)>;

 public:
  using Bins = std::array<std::array<unsigned, 64>, 2>;
  struct Packet {
    Bins bins{};
    unsigned minimum = 0xffffffffu, maximum = 0;
  };
  struct Input {
    blender::gpu::Texture *below, *own, *mask;
    std::array<float, 3> base{};
    std::array<int, 2> enabled{}, operands{};
    int channel = 0, width = 0, height = 0;
    float constant = 0;
    bool compare = true;
    int component = 0;
    LevelCurve curve;
    blender::gpu::Texture *owners = nullptr, *selection = nullptr;
    int inclusion = 0;
  };

 private:
  struct Entry {
    Buffer buffer{nullptr, blender::GPU_storagebuf_free};
    GpuReadbackFence fence;
    std::uint64_t revision = 0, submitted = 0;
    std::optional<HistogramMetadata> result;
    std::uint64_t ticket = 0, submitted_ticket = 0;
  };
  Shader shader_{nullptr, blender::GPU_shader_free};
  std::map<std::string, Entry> entries_;
  static blender::gpu::Shader *compile()
  {
    using namespace blender;
    using namespace gpu::shader;
    ShaderCreateInfo info("pyGPU_Shader");
    info.local_group_size(16, 16)
        .sampler(0, ImageType::Float2D, "below_img")
        .sampler(1, ImageType::Float2D, "own_img")
        .sampler(2, ImageType::Float2D, "mask_img")
        .sampler(3, ImageType::Uint2D, "owners_img")
        .sampler(4, ImageType::Uint2D, "selection_img")
        .storage_buf(0, Qualifier::read_write, "uint", "bins[]")
        .push_constant(Type::float3_t, "base_value")
        .push_constant(Type::int2_t, "enabled")
        .push_constant(Type::int2_t, "operands")
        .push_constant(Type::int2_t, "size")
        .push_constant(Type::int_t, "channel")
        .push_constant(Type::float_t, "constant_value")
        .push_constant(Type::int_t, "comparison_mode")
        .push_constant(Type::int_t, "component")
        .push_constant(Type::float4_t, "curve")
        .push_constant(Type::float_t, "output_high")
        .push_constant(Type::int_t, "inclusion");
    info.compute_source_generated = R"GLSL(
vec3 normal_unit(vec3 n){float d=dot(n,n);return d>1e-20?n*inversesqrt(d):vec3(0,0,1);}
uint ordered(float v){uint u=floatBitsToUint(v);return (u&0x80000000u)!=0u?~u:u^0x80000000u;}
void record_value(int series,float v){atomicAdd(bins[series*64+clamp(int(clamp(v,0.0,1.0)*63),0,63)],1u);atomicMin(bins[128],ordered(v));atomicMax(bins[129],ordered(v));}
float levels(float v){float t=(v-curve.x)/(curve.z-curve.x);if(t>0)t=pow(t,1/curve.y);return clamp(curve.w+(output_high-curve.w)*t,-65504.0,65504.0);}
bool included(ivec2 p){if(inclusion==0)return true;if(inclusion==1)return false;uint owner=texelFetch(owners_img,p,0).r;if(owner==0u)return false;int low=0,high=textureSize(selection_img,0).x;while(low<high){int middle=(low+high)/2;uvec2 row=texelFetch(selection_img,ivec2(middle,0),0).rg;if(row.x<owner)low=middle+1;else high=middle;}if(low>=textureSize(selection_img,0).x)return false;uvec2 row=texelFetch(selection_img,ivec2(low,0),0).rg;return row.x==owner&&row.y!=0u;}
void main(){ivec2 p=(2*ivec2(gl_LocalInvocationID.xy)+1)*size/32;
 vec4 a=texelFetch(below_img,min(p,textureSize(below_img,0)-1),0),b=texelFetch(own_img,min(p,textureSize(own_img,0)-1),0);
 vec3 below=a.rgb+base_value*(1-a.a),own=b.rgb;
 if(channel==3){below=normal_unit(below)*.5+.5;own=normal_unit(own*2-1)*.5+.5;}
 vec2 material=channel==0||channel==3?vec2(dot(below,vec3(.2126,.7152,.0722)),dot(own,vec3(.2126,.7152,.0722))):vec2(below.r,own.r);
 if(enabled.x==0)material.x=0;if(enabled.y==0)material.y=0;
 float mask=texelFetch(mask_img,min(p,textureSize(mask_img,0)-1),0).r;
 if(comparison_mode!=0){for(int i=0;i<2;i++)record_value(i,operands[i]<2?material[operands[i]]:operands[i]==2?mask:constant_value);}
 else {vec3 value=channel==-1?vec3(mask):included(p)?b.rgb:vec3(0);for(int i=0;i<3;i++){if(component>0&&i!=component-1)continue;record_value(0,component>0?levels(value[i]):value[i]);}}
})GLSL";
    return GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info));
  }
  void submit(Entry &entry)
  {
    using namespace blender;
    if (entry.fence || entry.submitted == entry.revision)
      return;
    entry.fence.submit(entry.buffer.get());
    entry.submitted = entry.revision;
    entry.submitted_ticket = entry.ticket;
  }

 public:
  std::uint64_t dispatches = 0, readback_bytes = 0, discarded = 0;
  void prepare()
  {
    using namespace blender;
    if (!shader_)
      shader_ = Shader(compile(), GPU_shader_free);
    if (!shader_)
      throw std::runtime_error("GPU histogram shader failed");
  }
  void update(const std::string &id, const Input &in, std::uint64_t ticket)
  {
    using namespace blender;
    prepare();
    if (!entries_.contains(id) && entries_.size() >= 64) {
      auto victim = entries_.end();
      for (auto i = entries_.begin(); i != entries_.end(); ++i)
        if (!i->second.fence &&
            (victim == entries_.end() || i->second.ticket < victim->second.ticket))
          victim = i;
      if (victim != entries_.end())
        entries_.erase(victim);
    }
    auto &entry = entries_[id];
    if (!entry.buffer)
      entry.buffer = Buffer(GPU_storagebuf_create_ex(
                                sizeof(Packet), nullptr, GPU_USAGE_DYNAMIC, "BTS async histogram"),
                            GPU_storagebuf_free);
    if (!entry.buffer)
      throw std::bad_alloc();
    Packet initial;
    GPU_storagebuf_update(entry.buffer.get(), &initial);
    entry.ticket = ticket;
    GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
    GPU_shader_bind(shader_.get());
    GPU_texture_bind(in.below, 0);
    GPU_texture_bind(in.own, 1);
    GPU_texture_bind(in.mask, 2);
    GPU_texture_bind(in.owners, 3);
    GPU_texture_bind(in.selection, 4);
    GPU_storagebuf_bind(entry.buffer.get(), 0);
    GPU_shader_uniform_3fv(shader_.get(), "base_value", in.base.data());
    GPU_shader_uniform_2iv(shader_.get(), "enabled", in.enabled.data());
    GPU_shader_uniform_2iv(shader_.get(), "operands", in.operands.data());
    const int size[2] = {in.width, in.height};
    GPU_shader_uniform_2iv(shader_.get(), "size", size);
    GPU_shader_uniform_1i(shader_.get(), "channel", in.channel);
    GPU_shader_uniform_1f(shader_.get(), "constant_value", in.constant);
    GPU_shader_uniform_1i(shader_.get(), "comparison_mode", in.compare);
    GPU_shader_uniform_1i(shader_.get(), "component", in.component);
    GPU_shader_uniform_1i(shader_.get(), "inclusion", in.inclusion);
    const float curve[4] = {
        in.curve.input_low, in.curve.gamma, in.curve.input_high, in.curve.output_low};
    GPU_shader_uniform_4fv(shader_.get(), "curve", curve);
    GPU_shader_uniform_1f(shader_.get(), "output_high", in.curve.output_high);
    GPU_compute_dispatch(shader_.get(), 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
    GPU_storagebuf_unbind(entry.buffer.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    ++entry.revision;
    ++dispatches;
    submit(entry);
  }
  void poll()
  {
    using namespace blender;
    for (auto &[id, entry] : entries_) {
      if (!entry.fence)
        continue;
      Packet packet;
      if (!entry.fence.read(entry.buffer.get(), &packet, entry.submitted != entry.revision))
        continue;
      if (entry.submitted == entry.revision) {
        const auto decode = [](unsigned u) {
          return std::bit_cast<float>((u & 0x80000000u) != 0u ? u ^ 0x80000000u : ~u);
        };
        entry.result = HistogramMetadata{
            packet.bins, decode(packet.minimum), decode(packet.maximum), entry.submitted_ticket};
        readback_bytes += sizeof(Packet);
      }
      else
        ++discarded;
      submit(entry);
    }
  }
  bool pending() const
  {
    for (const auto &[id, e] : entries_)
      if (e.fence)
        return true;
    return false;
  }
  std::optional<HistogramMetadata> get(const std::string &id) const
  {
    auto i = entries_.find(id);
    return i == entries_.end() ? std::nullopt : i->second.result;
  }
};
}  // namespace bts
