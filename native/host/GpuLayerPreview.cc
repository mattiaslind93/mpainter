// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetResidency.hh"
#include "LayerSourceChannels.hh"
#include "NativeMaterialPreparationQueue.hh"
#include "GPU_host_texture_memory.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialProjectionParameters.hh"
#include "NativeMaterialHeightGpuPrivate.hh"
#include "GpuLayerPreview.hh"
#include "NativeMaterialBakedAoContext.hh"
#include "SpatialOracleScope.hh"
#include "MaskEffectContext.hh"
#include "GpuProgramWarmup.hh"
#include "GpuProjectReadback.hh"
#include "FillTextureIO.hh"
#include "ImageSequence.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "GpuStackPlan.hh"
#include "FolderEntryPlan.hh"
#include "AdjustmentPaintPlan.hh"
#include "BumpPrecision.hh"
#include "GpuAdjustmentPages.hh"
#include "../tests/FolderCompositionFixture.hh"
#include "../tests/SmartMaskProbe.hh"
#include "GpuLayerChannel.hh"
#include "GpuLayerEffects.hh"
#include "GpuMaskStages.hh"
#include "GpuGeneratorAnchors.hh"
#include "GpuMaskResolve.hh"
#include "GpuRangeSummary.hh"
#include "GpuHistogram.hh"
#include "GpuMaterialSample.hh"
#include "GpuThumbnail.hh"
#include "SmartMaskThumbnailPacket.hh"
#include "GpuLayerCapture.hh"
#include "GpuFlattenTest.hh"
#include "QuickMaskPreview.hh"
#include <thread>
#include <QJsonDocument>
#include "GpuSpatialParameters.hh"
#include "GpuDerivedShaders.hh"
#include "GpuProjectionSource.hh"
#include "GpuWarpSource.hh"
#include <epoxy/gl.h>
#include "GpuLayerParameters.hh"
#include "GpuGeometryMask.hh"
#include "GpuMarginPlan.hh"
#include "GpuMarginExecutor.hh"
#include "GpuDerivedDirty.hh"
#include "GpuBrushRasterTest.hh"
#include "GpuAuthoringWorkerTest.hh"
#include "GpuTextureProbe.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "AmbientOcclusion.hh"
#include "NativeCompositeJob.hh"
#include "GPU_compute.hh"
#include "GPU_context.hh"
#include "BLI_math_half.hh"
#include "MEM_guardedalloc.h"
#include "GPU_shader.hh"
#include "GPU_shader_hosted.hh"
#include "GPU_capabilities.hh"
#include "GPU_state.hh"
#include "gpu_shader_create_info.hh"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstring>
namespace bts {
using namespace blender;
namespace {
#include "GpuLayerPreviewInternals.inc"

}  // namespace
#include "GpuLayerPreviewImpl.inc"
gpu::Texture *GpuLayerPreview::Impl::mask_prefix(const std::string &key,
                                                 const std::vector<GpuMaskStage> &stages,
                                                 const std::array<gpu::Texture *, 6> &maps,
                                                 int tile,
                                                 int w,
                                                 int h,
                                                 const std::set<std::string> &protected_keys,
                                                 const std::string &map_signature,
                                                 const GpuComparisonResolver &resolve,
                                                 const GpuAnchorResolver &resolve_anchor,
                                                 bool partial)
{
  performance::Scope timing("gpu_mask_prefix");
  for (const auto &stage : stages)
    if (stage.kind == GpuMaskStage::SpatialFill && stage.surface_required &&
        (!stage.geometry || !stage.geometry->tiles.contains(tile)))
      return nullptr;
  auto &cache = mask_stacks[key];
  if (cache.width != w || cache.height != h) {
    cache.output_dirty.clear();
    cache.node_versions.clear();
    cache.width = w;
    cache.height = h;
  }
  std::vector<GpuStackNode> nodes;
  const auto append = [](std::string &bytes, const auto &value) {
    bytes.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  for (const auto &stage : stages) {
    bool comparison_ready = true;
    GpuStackNode node;
    node.id = stage.id;
    if (!nodes.empty())
      node.dependencies = {nodes.back().id};
    int slot = 0;
    gpu_visit_mask_stage(stage, [&](const auto &part) {
      node.parameters += part.owner_key + part.id + part.anchor_key + part.anchor_id;
      if (part.bitmap)
        node.parameters += part.bitmap->asset + part.bitmap->hash;
      append(node.parameters, part.kind);
      append(node.parameters, part.background);
      append(node.parameters, part.opacity);
      append(node.parameters, part.blend);
      for (const auto &row : part.rows)
        append(node.parameters, row);
      if (part.spatial) {
        append(node.parameters, part.spatial->values);
        append(node.parameters, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
      }
      if (part.kind == GpuMaskStage::Points)
        node.parameters += map_signature;
      if (part.generator) {
        auto values = gpu_generator_inputs(*part.generator, part.comparison_layer, resolve_anchor);
        if (!values)
          comparison_ready = false;
        else
          gpu_generator_dependencies(node, *part.generator, *values, slot);
      }
      if (part.kind == GpuMaskStage::Compare) {
        auto values = resolve(part);
        if (!values)
          comparison_ready = false;
        else
          gpu_comparison_dependencies(node, *values, slot);
      }
      if (part.kind == GpuMaskStage::AnchorFill) {
        append(node.parameters, part.reference.channel);
        append(node.parameters, part.reference.component);
        append(node.parameters, part.reference.coverage);
        append(node.parameters, part.content_anchor);
        if (part.content_anchor) {
          auto value = resolve_anchor(part.comparison_layer, part.reference);
          if (!value)
            comparison_ready = false;
          else
            gpu_anchor_dependencies(node, *value, slot);
        }
      }
      if (part.pixels) {
        const int index = slot++;
        const auto &pixels = part.pixels->tile(tile);
        const int resolution = pixels.resolution();
        append(node.parameters, resolution);
        if (resolution) {
          const double sx = double(w) / resolution, sy = double(h) / resolution;
          const auto versions = pixels.page_versions();
          for (const auto &[page, revision] : versions[2]) {
            const int px = int(page % 65536) * 64, py = int(page / 65536) * 64;
            const int x = int(std::floor(px * sx)) - 1, y = int(std::floor(py * sy)) - 1,
                      right = int(std::ceil((px + 64) * sx)) + 1,
                      bottom = int(std::ceil((py + 64) * sy)) + 1;
            node.pages[(std::uint64_t(index) << 32) | page] = {
                x, y, right - x, bottom - y, revision};
          }
        }
      }
    });
    if (!comparison_ready)
      return nullptr;
    nodes.push_back(std::move(node));
  }
  auto transaction = cache.plan.prepare(w, h, nodes);
  std::set<std::string> live;
  for (const auto &node : nodes)
    live.insert(node.id);
  if (!partial)
    std::erase_if(cache.outputs, [&](const auto &entry) { return !live.contains(entry.first); });
  struct Input {
    gpu::Texture *source = nullptr, *comparison = nullptr, *effects = nullptr,
                 *parameters = nullptr, *surface = nullptr;
    std::array<gpu::Texture *, 6> maps{};
    bool anchor_valid = false, anchor_mask = true;
  };
  std::vector<Input> resolved(stages.size());
  for (std::size_t i = 0; i < stages.size(); ++i) {
    if (transaction.work[i].regions.empty())
      continue;
    const auto &stage = stages[i];
    const auto identity = key + ":" + stage.id;
    auto &in = resolved[i];
    in.source = stage.bitmap ? resource(*stage.bitmap, protected_keys) :
                stage.pixels ? paint(identity, stage.pixels->tile(tile), 2, protected_keys) :
                               zero.get();
    if (!in.source)
      return nullptr;
    if (stage.anchor_stages) {
      if (!mask_prefix(stage.anchor_key,
                       *stage.anchor_stages,
                       maps,
                       tile,
                       w,
                       h,
                       protected_keys,
                       map_signature,
                       resolve,
                       resolve_anchor,
                       true))
        return nullptr;
      in.source = mask_stacks.at(stage.anchor_key).outputs.at(stage.anchor_id).get();
      in.anchor_valid = true;
    }
    if (stage.content_anchor) {
      auto value = resolve_anchor(stage.comparison_layer, stage.reference);
      if (!value)
        return nullptr;
      in.source = value->image;
      in.anchor_valid = value->enabled;
      in.anchor_mask = value->mask;
    }
    in.maps = maps;
    auto rows = stage.rows;
    if (stage.generator) {
      auto values = gpu_generator_inputs(*stage.generator, stage.comparison_layer, resolve_anchor);
      if (!values)
        return nullptr;
      gpu_generator_options(rows.front(), *stage.generator, *values);
      gpu_generator_maps(in.maps, *stage.generator, *values);
    }
    in.effects = effects(identity, rows);
    if (!in.effects)
      return nullptr;
    in.surface = surface(stage.geometry, tile);
    if (!in.surface)
      return nullptr;
    GpuLayerParameters params = stage.spatial.value_or(GpuLayerParameters{});
    const float anchor_options[4] = {float(stage.reference.component),
                                     float(stage.reference.coverage),
                                     float(in.anchor_mask),
                                     float(in.anchor_valid)};
    params.set4("anchor_options", anchor_options);
    params.set("mask_pass", stage.kind);
    params.set("mask_background", stage.background);
    params.set("opacity_value", stage.opacity);
    params.set("blend_mode", int(stage.blend));
    params.set("effect_count", stage.rows.size());
    in.comparison = zero.get();
    if (stage.kind == GpuMaskStage::Compare) {
      auto values = resolve(stage);
      if (!values)
        return nullptr;
      in.source = (*values)[0].image;
      in.comparison = (*values)[1].image;
      params.set("compare_channel", stage.comparison_channel);
      const int enabled[2] = {(*values)[0].enabled, (*values)[1].enabled};
      params.set2("compare_enabled", enabled);
      params.set3("compare_base", (*values)[0].base.data());
    }
    in.parameters = parameters(identity, params);
    if (!in.parameters) {
      input_rejected = true;
      return nullptr;
    }
  }
  auto *shader = mask_shader.get();
  GPU_shader_bind(shader);
  for (std::size_t i = 0; i < stages.size(); ++i) {
    const auto &work = transaction.work[i];
    if (work.regions.empty())
      continue;
    auto found = cache.outputs.find(work.id);
    if (found == cache.outputs.end() || GPU_texture_width(found->second.get()) != w ||
        GPU_texture_height(found->second.get()) != h)
    {
      auto output = texture("BTS shared GPU mask prefix", w, h, gpu::TextureFormat::SFLOAT_32);
      if (!output) {
        input_rejected = true;
        GPU_shader_unbind();
        return nullptr;
      }
      cache.outputs.insert_or_assign(work.id, std::move(output));
    }
    auto *output = cache.outputs.at(work.id).get();
    const auto &in = resolved[i];
    GPU_texture_bind(in.source, 0);
    GPU_texture_bind(in.comparison, 1);
    GPU_texture_bind(i ? cache.outputs.at(stages[i - 1].id).get() : zero.get(), 2);
    GPU_texture_bind(in.surface, 3);
    GPU_texture_bind(in.effects, 4);
    GPU_texture_bind(in.parameters, 5);
    for (int m = 0; m < 6; ++m)
      GPU_texture_bind(in.maps[m], gpu_mask_map_slot(m));
    GPU_texture_bind(geometry_masks.empty(), 9);
    GPU_texture_bind(geometry_masks.empty(), 10);
    GPU_texture_image_bind(output, 0);
    for (const auto &r : work.regions) {
      const int origin[2] = {r.x, r.y}, size[2] = {r.width, r.height};
      GPU_shader_uniform_2iv(shader, "region_origin", origin);
      GPU_shader_uniform_2iv(shader, "region_size", size);
      GPU_compute_dispatch(shader, (r.width + 15) / 16, (r.height + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH);
    GPU_texture_image_unbind(output);
  }
  GPU_shader_unbind();
  for (const auto &work : transaction.work)
    if (!work.regions.empty()) {
      const auto version = ++cache.node_clock;
      for (const auto &r : work.regions) {
        const auto chunk = std::uint64_t(r.y / 256) * ((w + 255) / 256) + r.x / 256;
        cache.node_versions[work.id][chunk] = {r.x, r.y, r.width, r.height, version};
      }
    }
  if (partial) {
    mask_dispatch_texels += transaction.texels;
    cache.plan.commit(std::move(transaction), true);
    return cache.outputs.at(stages.back().id).get();
  }
  if (cache.head_id != stages.back().id) {
    for (int y = 0; y < h; y += 256)
      for (int x = 0; x < w; x += 256)
        cache.output_dirty.insert(y / 256 * ((w + 255) / 256) + x / 256);
    cache.head_id = stages.back().id;
  }
  for (const auto &region : transaction.work.back().regions)
    cache.output_dirty.insert(region.y / 256 * ((w + 255) / 256) + region.x / 256);
  mask_dispatch_texels += transaction.texels;
  cache.plan.commit(std::move(transaction));
  cache.head = cache.outputs.at(stages.back().id).get();
  return cache.head;
}

// Main-thread retirement; disposal happens only on the active GPU owner.
std::vector<std::unique_ptr<GpuLayerPreview::Impl>> &GpuLayerPreview::retired()
{
  static std::vector<std::unique_ptr<Impl>> values;
  return values;
}
GpuLayerPreview::GpuLayerPreview() : impl_(std::make_unique<Impl>())
{
  bind_adjustment_resolver();
}
void GpuLayerPreview::bind_adjustment_resolver()
{
  impl_->authoring_pages.adjustment_resolver([this](const SmudgeCapture &capture,
                                                    int plane,
                                                    int tile,
                                                    std::uint32_t page,
                                                    gpu::Texture *own,
                                                    auto deadline) {
    return adjustment_page(capture, plane, tile, page, own, deadline);
  });
}
GpuLayerPreview::~GpuLayerPreview()
{
  if (impl_ && impl_->has_gpu())
    retired().push_back(std::move(impl_));
}
std::uint64_t GpuLayerPreview::release_bake_scratch()
{
  auto &s = *impl_;
  std::uint64_t bytes = 0;
  for (const auto *cache : {&s.stacks, &s.mask_stacks})
    for (const auto &[key, stack] : *cache)
      for (const auto &[id, image] : stack.outputs)
        if (image.use_count() == 1)
          bytes = resource_sum(
              bytes,
              resource_product(resource_product(GPU_texture_width(image.get()),
                                                GPU_texture_height(image.get())),
                               4 * GPU_texture_component_len(GPU_texture_format(image.get()))));
  s.comparison_memo.clear();
  s.anchor_memo.clear();
  s.stacks.clear();
  s.mask_stacks.clear();
  for (auto i = s.inputs.begin(); i != s.inputs.end();)
    if (!i->second.retained() && !s.adjustment_input_pins.contains(i->first)) {
      bytes = resource_sum(bytes, i->second.bytes());
      s.used_bytes -= i->second.bytes();
      i = s.inputs.erase(i);
    }
    else
      ++i;
  texture_memory_refresh_needed = true;
  std::printf("BTS_BAKE_RELEASE reusable_bytes=%llu\n", (unsigned long long)bytes);
  return bytes;
}
void GpuLayerPreview::evict_residency(const std::vector<TextureSet> &resident)
{
  release_bake_scratch();
  auto &s = *impl_;
  s.authoring_pages.evict();
  s.adjustment_pages.evict();
  std::set<std::string> resources;
  std::set<QUuid> live_sets;
  for (const auto &set : resident)
    if (!set.loaded_tiles().empty()) {
      live_sets.insert(set.id);
      for (const auto &layer : evaluation_layers(set))
        if (layer.fill.native_material)
          resources.insert(layer.fill.material_resource);
    }
  for (const auto &[id, entry] : s.adjustment_sources)
    if (auto token = entry.source.lock(); token && live_sets.contains(token->set))
      resources.insert(entry.native_aliases.begin(), entry.native_aliases.end());
  for (const auto &[id, entry] : s.adjustment_sources)
    if (auto token = entry.source.lock(); token && !live_sets.contains(token->set))
      visit_set_textures(entry.set, [](const FillTexture &texture) {
        if (texture.tiles) {
          for (const auto &[tile, image] : *texture.tiles)
            image->pixels.spill();
        }
        else
          texture.pixels.spill();
      });
  s.native_materials.evict_except(resources);
  s.geometry.clear();
  s.effect_parameters.clear();
  s.layer_parameters.clear();
  texture_memory_refresh_needed = true;
}
void GpuLayerPreview::reset(bool terminal)
{
  auto previous = std::move(impl_);
  impl_ = std::make_unique<Impl>();
  if (!terminal) {
    impl_->adjustment_sources = std::move(previous->adjustment_sources);
    impl_->adjustment_pages = std::move(previous->adjustment_pages);
    impl_->native_context_provider = previous->native_context_provider;
    for (const auto &[id, entry] : impl_->adjustment_sources)
      for (const auto &key : entry.native_aliases)
        impl_->native_materials.retain_alias_from(previous->native_materials, key);
  }
  bind_adjustment_resolver();
  if (previous->has_gpu())
    retired().push_back(std::move(previous));
}
void GpuLayerPreview::collect_retired(bool terminal)
{
  std::erase_if(retired(), [&](const auto &state) {
    // Ordinary retirement only polls: freeing a running prepared-info owner would
    // wait inside the compiler cancellation API. GUI shutdown may explicitly wait.
    if (terminal)
      state->cancel_programs();
    else
      state->poll_programs();
    // Terminal owner shutdown has joined/detached all image users. Metadata
    // residency leases cannot keep GPU allocations alive past GPU_exit.
    return !state->programs_pending() &&
           (terminal || std::none_of(state->inputs.begin(),
                                     state->inputs.end(),
                                     [](const auto &entry) { return entry.second.retained(); }));
  });
}
void GpuLayerPreview::poll_programs()
{
  collect_retired();
  impl_->poll_programs();
}
bool GpuLayerPreview::programs_pending() const
{
  return impl_->programs_pending() ||
         std::any_of(retired().begin(), retired().end(), [](const auto &state) {
           return state->programs_pending();
         });
}
std::string GpuLayerPreview::take_program_error()
{
  if (impl_->programs_pending())
    return {};
  auto error = impl_->program_state.take_error();
  if (error.empty())
    if (auto derived = impl_->derived_program_state.take_error(); !derived.empty())
      error = "Derived warmup: " + derived;
  if (error.empty())
    if (auto authoring = impl_->authoring_pages.take_program_error(); !authoring.empty())
      error = "Authoring warmup: " + authoring;
  return error;
}
void GpuLayerPreview::begin_frame()
{
  impl_->authoring_pages.retire_payloads();
  impl_->native_materials.step_image_uploads();
  for (auto i = impl_->adjustment_sources.begin(); i != impl_->adjustment_sources.end();) {
    if (!i->second.source.expired()) {
      ++i;
      continue;
    }
    const auto id = i->first;
    const auto prefix = i->second.set.id.toString().toStdString();
    impl_->adjustment_pages.retire(id);
    for (const auto &key : i->second.native_aliases)
      impl_->native_materials.erase(key);
    std::erase_if(impl_->stacks, [&](const auto &p) { return p.first.starts_with(prefix + ":"); });
    std::erase_if(impl_->mask_stacks, [&](const auto &p) { return p.first.starts_with(prefix); });
    i = impl_->adjustment_sources.erase(i);
  }
  impl_->comparison_memo.clear();
  impl_->anchor_memo.clear();
  if (std::getenv("BTS_RESOLUTION_LOG") &&
      performance::Clock::now() - impl_->cache_logged >= std::chrono::seconds(2))
  {
    auto &s = *impl_;
    s.cache_logged = performance::Clock::now();
    std::uint64_t prefix = 0, mask = 0;
    for (const auto &[key, stack] : s.stacks)
      for (const auto &[id, image] : stack.outputs)
        prefix += std::uint64_t(GPU_texture_width(image.get())) * GPU_texture_height(image.get()) *
                  16;
    for (const auto &[key, stack] : s.mask_stacks)
      for (const auto &[id, image] : stack.outputs)
        mask += std::uint64_t(GPU_texture_width(image.get())) * GPU_texture_height(image.get()) *
                4;
    std::printf(
        "BTS_GPU_CACHE authoring_pages=%zu authoring_bytes=%llu computed=%llu evicted=%llu page_allocations=%llu page_reuses=%llu source_bytes=%llu prefix_bytes=%llu mask_bytes=%llu all_texture_bytes=%llu driver_free_bytes=%llu\n",
        s.authoring_pages.resident_pages(),
        (unsigned long long)(s.authoring_pages.resident_pages() * 64ULL * 64 * 16),
        (unsigned long long)s.authoring_pages.computed_pages(),
        (unsigned long long)s.authoring_pages.evicted_pages(),
        (unsigned long long)s.authoring_pages.image_allocations(),
        (unsigned long long)s.authoring_pages.image_reuses(),
        (unsigned long long)s.used_bytes,
        (unsigned long long)prefix,
        (unsigned long long)mask,
        (unsigned long long)blender::gpu::host_texture_memory::live_bytes(),
        (unsigned long long)texture_free_vram.load());
    auto groups = blender::gpu::host_texture_memory::snapshot();
    std::vector<std::pair<std::string, blender::gpu::host_texture_memory::Group>> sorted(
        groups.begin(), groups.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
      return a.second.bytes > b.second.bytes;
    });
    QJsonArray top;
    for (std::size_t i = 0; i < std::min<std::size_t>(12, sorted.size()); ++i)
      top.append(QJsonObject{{"label", QString::fromStdString(sorted[i].first)},
                             {"bytes", qint64(sorted[i].second.bytes)},
                             {"count", qint64(sorted[i].second.count)}});
    std::printf("BTS_GPU_TEXTURE_MEMORY %s\n",
                QJsonDocument(top).toJson(QJsonDocument::Compact).constData());
  }
  if (performance::enabled()) {
    auto &s = *impl_;
    const auto count = s.authoring_pages.computed_pages();
    if (count != s.reported_authoring_pages) {
      std::printf("BTS_GPU_AUTHORING pages=%llu total=%llu evicted=%llu resident=%zu\n",
                  (unsigned long long)(count - s.reported_authoring_pages),
                  (unsigned long long)count,
                  (unsigned long long)s.authoring_pages.evicted_pages(),
                  s.authoring_pages.resident_pages());
      s.reported_authoring_pages = count;
    }
  }
  static bool tested = false;
  if (!tested && std::getenv("BTS_VERIFY_GPU_STACK")) {
    tested = true;
    self_test();
  }
  collect_retired();
  impl_->poll_programs();
  if (!impl_->startup_prepared && !impl_->failed &&
      (!(impl_->programs_pending() || impl_->use_program_workers()) ||
       impl_->prepare_programs() == GpuPreviewUpdate::Updated))
  {
    performance::Scope timing("gpu_stack_program_warmup");
    {
      performance::Scope part("gpu_stack_warmup_authoring_pages");
      impl_->authoring_pages.prepare();
    }
    {
      performance::Scope part("gpu_adjustment_source_warmup");
      impl_->adjustment_pages.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_ranges");
      impl_->ranges.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_histograms");
      impl_->histograms.prepare();
    }
    {
      performance::Scope part("gpu_stack_warmup_margins");
      if (!impl_->margins.prepare_shaders())
        throw std::runtime_error("GPU margin shader preparation failed");
    }
    for (auto format :
         {gpu::TextureFormat::SFLOAT_16_16_16_16, gpu::TextureFormat::SFLOAT_32_32_32_32})
    {
      {
        performance::Scope part("gpu_stack_warmup_finals");
        for (bool array : {false, true})
          impl_->initialize(1, 1, format, array);
      }
      if (!impl_->uniform_shaders.contains(format)) {
        performance::Scope part("gpu_stack_warmup_uniform");
        impl_->uniform_shaders.emplace(format, compile_uniform(format));
      }
    }
    if (!impl_->clear_shader) {
      performance::Scope part("gpu_stack_warmup_uniform");
      impl_->clear_shader = compile_uniform(gpu::TextureFormat::SFLOAT_32_32_32_32);
    }
    if (!impl_->derived_programs_started) {
      performance::Scope part("gpu_stack_warmup_derived");
      impl_->coverage_shader = Shader(gpu_derived::coverage_shader(), GPU_shader_free);
      impl_->presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
      for (auto format :
           {gpu::TextureFormat::SFLOAT_16_16_16_16, gpu::TextureFormat::SFLOAT_32_32_32_32})
        for (bool array : {false, true}) {
          const auto key = std::pair{format, array};
          impl_->ao_shaders.emplace(
              key, Shader(gpu_derived::ao_shader(format, array, quant_source), GPU_shader_free));
          impl_->normal_shaders.emplace(
              key,
              Shader(gpu_derived::normal_shader(format, array, quant_source), GPU_shader_free));
        }
    }
    impl_->startup_prepared = true;
  }
  // Explicit functional readback suite, after the separately measured owner
  // warmup block. Never startup/performance evidence, never enabled by default.
  static bool authoring_verified = false;
  if (!authoring_verified && std::getenv("BTS_GPU_AUTHORING_PROGRAM_TEST") &&
      impl_->startup_prepared)
  {
    if (!impl_->authoring_pages.programs_started())
      throw std::runtime_error("Authoring oracle requires the hosted worker route");
    performance::Scope verification("gpu_authoring_worker_verification");
    verify_gpu_authoring_workers(impl_->authoring_pages);
    authoring_verified = true;
  }
  impl_->remaining = upload_budget;
  impl_->upload_start = performance::Clock::now();
  impl_->remaining_work = 32ULL * 1024 * 1024;
}
NativeMaterialGpuLibrary &GpuLayerPreview::native_world_sources()
{
  return impl_->native_materials;
}
std::string GpuLayerPreview::world_normal_mask_signature(const TextureSet &set,
                                                         int tile,
                                                         std::span<const QUuid> masks,
                                                         std::uint64_t dependent_epoch) const
{
  PaintMetadataScope metadata;
  std::string result;
  GpuMaskBuilder builder(set, tile);
  const auto &layers = evaluation_layers(set);
  bool dependent = false;
  const auto append = [](std::string &key, const auto &value) {
    key.append(reinterpret_cast<const char *>(&value), sizeof(value));
  };
  for (const auto &id : masks) {
    const auto layer = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == id; });
    if (layer == layers.end() || !layer->mask)
      throw std::invalid_argument("Native world Normal mask owner missing");
    const auto stages = builder.build(*layer, true);
    for (const auto &stage : stages)
      gpu_visit_mask_stage(
          stage,
          [&](const auto &part) {
            result += part.owner_key + part.id + part.anchor_key + part.anchor_id;
            append(result, part.kind);
            append(result, part.background);
            append(result, part.opacity);
            append(result, part.blend);
            for (const auto &row : part.rows)
              append(result, row);
            if (part.spatial) {
              append(result, part.spatial->values);
              append(result, reinterpret_cast<std::uintptr_t>(part.geometry.get()));
            }
            if (part.bitmap)
              result += part.bitmap->asset + part.bitmap->hash;
            if (part.pixels) {
              const auto &pixels = part.pixels->tile(tile);
              append(result, pixels.resolution());
              pixels.visit_page_versions(2, [&](auto key, auto revision) {
                append(result, key);
                append(result, revision);
              });
            }
            dependent |= part.kind == GpuMaskStage::Compare ||
                         part.kind == GpuMaskStage::AnchorFill || bool(part.generator) ||
                         bool(part.anchor_stages) || part.content_anchor;
          },
          true);
  }
  // External anchor/comparison prefixes may depend on a different channel. Keep
  // their existing conservative document epoch; ordinary masks remain tile/page
  // specific and unrelated source Color/Roughness edits do not rerender W.
  if (dependent)
    append(result, dependent_epoch);
  for (const auto &map : set.mesh_maps)
    if (map)
      if (const auto member = map->tiles.find(tile);
          member != map->tiles.end() && member->second.pixels)
        result += member->second.pixels->hash;
  return result;
}
GpuPreviewUpdate GpuLayerPreview::prepare_world_normal_weight(
    const TextureSet &set, int tile, float opacity, std::span<const QUuid> masks, bool array)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  if (!std::isfinite(opacity))
    return GpuPreviewUpdate::Unsupported;
  if (!s.world_weight || GPU_texture_width(s.world_weight.get()) != w ||
      GPU_texture_height(s.world_weight.get()) != h)
  {
    s.world_weight = texture(
        "Native world Normal mask weight", w, h, gpu::TextureFormat::SFLOAT_32);
  }
  std::vector<gpu::Texture *> resolved;
  for (const auto &id : masks) {
    auto *mask = mask_texture(set, id, tile);
    if (!mask)
      return GpuPreviewUpdate::Pending;
    resolved.push_back(mask);
  }
  const float weight[4]{std::clamp(opacity, 0.0f, 1.0f), 0, 0, 0};
  GPU_texture_clear(s.world_weight.get(), GPU_DATA_FLOAT, weight);
  if (!resolved.empty()) {
    if (!s.world_weight_shader)
      s.world_weight_shader = compile_world_weight();
    if (!s.world_weight_shader)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.world_weight_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_image_bind(s.world_weight.get(), 0);
    for (auto *mask : resolved) {
      GPU_texture_bind(mask, 0);
      GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    }
    GPU_texture_image_unbind(s.world_weight.get());
    GPU_texture_unbind_all();
    GPU_shader_unbind();
  }
  const auto plans = s.margin_plans.find(set.material_session_uid);
  if (plans == s.margin_plans.end() || !plans->second.extend.contains(tile))
    return GpuPreviewUpdate::Pending;
  if (!s.margins.prepare(
          plans->second.extend.at(tile), s.remaining, s.upload_start, testing_stack))
    return GpuPreviewUpdate::Pending;
  if (!s.world_apply_shader)
    s.world_apply_shader = compile_world_apply();
  if (!s.world_apply_shader)
    return GpuPreviewUpdate::Unsupported;
  for (bool kind : {false, array}) {
    auto &program = s.world_publish_shaders[kind];
    if (!program)
      program = compile_world_publish(kind);
    if (!program)
      return GpuPreviewUpdate::Unsupported;
  }
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::publish_world_normal(const TextureSet &set,
                                                       int tile,
                                                       gpu::Texture *source,
                                                       gpu::Texture *destination,
                                                       std::array<int, 3> offset,
                                                       bool apply_weight)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  if (!source || !destination || !s.world_weight || GPU_texture_width(s.world_weight.get()) != w ||
      GPU_texture_height(s.world_weight.get()) != h)
    return GpuPreviewUpdate::Unsupported;
  if (source == destination) {
    if (!apply_weight || GPU_texture_is_array(destination) || !s.world_apply_shader)
      return GpuPreviewUpdate::Unsupported;
    auto *shader = s.world_apply_shader.get();
    GPU_shader_bind(shader);
    GPU_texture_bind(s.world_weight.get(), 0);
    GPU_texture_image_bind(destination, 0);
    GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                       GPU_BARRIER_TEXTURE_UPDATE);
    GPU_texture_image_unbind(destination);
    GPU_texture_unbind_all();
    GPU_shader_unbind();
    return GpuPreviewUpdate::Updated;
  }
  const bool array = GPU_texture_is_array(destination);
  auto &program = s.world_publish_shaders[array];
  if (!program)
    return GpuPreviewUpdate::Unsupported;
  auto *shader = program.get();
  GPU_shader_bind(shader);
  GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
  GPU_shader_uniform_1i(shader, "apply_weight", apply_weight);
  GPU_texture_bind(source, 0);
  GPU_texture_bind(s.world_weight.get(), 1);
  GPU_texture_image_bind(destination, 0);
  GPU_compute_dispatch(shader, (w + 15) / 16, (h + 15) / 16, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_TEXTURE_UPDATE);
  GPU_texture_image_unbind(destination);
  GPU_texture_unbind_all();
  GPU_shader_unbind();
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::pad_world_normal(const TextureSet &set,
                                                   int tile,
                                                   gpu::Texture *image,
                                                   int side)
{
  auto &s = *impl_;
  const auto plans = s.margin_plans.find(set.material_session_uid);
  if (plans == s.margin_plans.end() || !plans->second.extend.contains(tile))
    return GpuPreviewUpdate::Pending;
  // Geometry-only extend donors touch outside UV islands, never an interior
  // mask hole. Signed vector and coverage use the same generic RGBA operation.
  const int w = set.width(tile), h = set.rows(tile);
  std::vector<std::uint32_t> dirty(((w + 255) / 256) * ((h + 255) / 256), 1);
  const auto key = set.id.toString().toStdString() + ":world:" + std::to_string(side) + ":" +
                   std::to_string(tile);
  return s.margins.apply(key, plans->second.extend.at(tile), image, {}, std::move(dirty)) ?
             GpuPreviewUpdate::Updated :
             GpuPreviewUpdate::Unsupported;
}
void GpuLayerPreview::publish_native_material_preview(const std::string &resource,
                                                      const std::string &hash)
{
  impl_->native_preparations.publish_preview(resource, hash);
}
void GpuLayerPreview::prepare_native_material(std::string resource,
                                              std::string content,
                                              std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->native_preparations.request(std::move(resource), std::move(content), std::move(graph));
}
void GpuLayerPreview::cancel_native_preparation(const std::string &resource)
{
  impl_->native_preparations.cancel(resource);
}
void GpuLayerPreview::discard_native_candidate(const std::string &resource)
{
  impl_->native_preparations.cancel(resource);
  impl_->native_materials.erase(resource);
}
void GpuLayerPreview::alias_native_material(const std::string &source,
                                            const std::string &destination)
{
  impl_->native_materials.alias(source, destination);
}
void GpuLayerPreview::validate_native_mapping_instances(const TextureSet &set,
                                                        const TextureSet *before) const
{
  for (const auto &layer : evaluation_layers(set))
    if (layer.kind == LayerKind::Fill && layer.fill.native_material) {
      const auto &after = *layer.fill.native_material;
      require_native_height_fill(
          after.height_policy, set.bump_origin, layer.fill.mode, layer.channels[3]);
      if (after.height_policy != NativeHeightPolicy::None)
        impl_->native_materials.validate_instance(layer.fill.material_resource, after);
      if (std::none_of(after.parameters.begin(), after.parameters.end(), [](const auto &p) {
            return !p.mapping_transform.identity();
          }))
        continue;
      const NativeMaterialDescription *old = nullptr;
      bool same_resource = false;
      if (before) {
        const auto &layers = evaluation_layers(*before);
        const auto found = std::find_if(
            layers.begin(), layers.end(), [&](const auto &value) { return value.id == layer.id; });
        if (found != layers.end() && found->fill.native_material) {
          const auto &candidate = *found->fill.native_material;
          bool same_interface = candidate.group == after.group &&
                                candidate.surface_type == after.surface_type &&
                                candidate.parameters.size() == after.parameters.size();
          if (same_interface)
            for (std::size_t i = 0; i < after.parameters.size(); ++i) {
              const auto &a = candidate.parameters[i], &b = after.parameters[i];
              same_interface = same_interface && a.identifier == b.identifier &&
                               a.type == b.type && a.source_node == b.source_node &&
                               a.source_socket == b.source_socket && a.mapping == b.mapping;
            }
          if (same_interface) {
            old = &candidate;
            same_resource = found->fill.material_resource == layer.fill.material_resource;
          }
        }
        // This preserves prior authored row state, not source lineage/equivalence.
        // A different prepared source still passes its actual layout/edge checks.
        if (old && same_resource) {
          bool unchanged = true;
          for (std::size_t i = 0; i < after.parameters.size(); ++i)
            unchanged = unchanged && old->parameters[i].mapping_transform ==
                                         after.parameters[i].mapping_transform;
          if (unchanged)
            continue;
        }
      }
      // Packed Open may queue a saved instance while its source is still preparing.
      // Once ready, signature/render enforce the same original-edge safety again.
      if (!impl_->native_materials.contains(layer.fill.material_resource)) {
        if (before)
          throw std::invalid_argument(
              "Native material is still preparing; retry the Mapping edit when ready");
        continue;
      }
      if (before)
        impl_->native_materials.validate_mapping_edit(layer.fill.material_resource, old, after);
      else
        impl_->native_materials.validate_instance(layer.fill.material_resource, after);
    }
}
bool GpuLayerPreview::has_native_material(const std::string &resource) const
{
  return impl_->native_materials.contains(resource);
}
bool GpuLayerPreview::native_material_uses_object(const std::string &resource) const
{
  return impl_->native_materials.uses_object(resource);
}
bool GpuLayerPreview::native_material_uses_ao(const std::string &resource) const
{
  return impl_->native_materials.uses_ao(resource);
}
NativeMaterialDependencies GpuLayerPreview::native_requirements(
    const std::string &resource, const NativeMaterialDescription &instance) const
{
  return impl_->native_materials.requirements(resource, instance);
}
void GpuLayerPreview::prepare_native_height(const std::string &resource,
                                            NativeMaterialHeightResource value)
{
  impl_->native_materials.prepare_height(resource, std::move(value));
}
void GpuLayerPreview::stage_native_height(NativeMaterialHeightGpu &work,
                                          NativeMaterialHeightResource resource)
{
  work.impl_->candidate = impl_->native_materials.stage_height(work.impl_->key,
                                                               std::move(resource));
}
bool GpuLayerPreview::step_native_height(NativeMaterialHeightGpu &work)
{
  if (work.ready_)
    return true;
  if (!work.impl_->candidate)
    throw std::logic_error("Native Height candidate is missing");
  return work.ready_ = impl_->native_materials.step_height_candidate(work.impl_->key,
                                                                     *work.impl_->candidate);
}
void GpuLayerPreview::install_native_height(NativeMaterialHeightGpu &work)
{
  if (!work.ready_)
    throw std::logic_error("Native Height candidate is not ready");
  if (work.impl_->change)
    throw std::logic_error("Raw authored Height publication already began");
  work.impl_->change = impl_->native_materials.install_height_candidate(
      work.impl_->key, std::move(work.impl_->candidate));
}
bool GpuLayerPreview::set_native_height_policy(TextureSet &set,
                                               const QUuid &id,
                                               NativeHeightPolicy policy) const
{
  auto &layer = TextureDocument::layer(set, id);
  require_native_height_fill(
      policy, set.bump_origin, layer.fill.mode, layer.channels[3]);  // Before any candidate edit.
  if (layer.kind != LayerKind::Fill || layer.instance || !layer.fill.native_material)
    throw std::invalid_argument("Raw authored Height requires an editable native Fill instance");
  if (layer.fill.native_material->height_policy == policy)
    return false;
  if (policy != NativeHeightPolicy::None && !set.channels[4])
    throw std::invalid_argument(
        "Enable the Texture Set Bump channel before selecting raw authored Height");
  auto instance = std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);
  instance->height_policy = policy;
  // Actual prepared source/sidecar state is the only capability authority.
  // Saved descriptor channels and source names never grant raw Height support.
  instance->channels = impl_->native_materials.effective_channels(layer.fill.material_resource,
                                                                  *instance);
  const bool bump = std::find(instance->channels.begin(), instance->channels.end(), 4) !=
                    instance->channels.end();
  layer.fill.native_material = std::move(instance);
  layer.channels[4] = bump;
  return true;
}
void GpuLayerPreview::probe_native_height(const std::string &key,
                                          const NativeMaterialDescription &instance,
                                          NativeMaterialGpuContext context)
{
  impl_->native_materials.probe_height(key, instance, std::move(context));
}
CompositeChannels GpuLayerPreview::native_view_channels(const TextureSet &set) const
{
  CompositeChannels result{};
  for (const auto &layer : evaluation_layers(set))
    if (layer.visible && layer.fill.native_material &&
        impl_->native_materials.contains(layer.fill.material_resource))
      for (int channel : impl_->native_materials.view_channels(layer.fill.material_resource,
                                                               *layer.fill.native_material))
        result[channel] = true;
  return result;
}
void GpuLayerPreview::step_native_preparations()
{
  impl_->native_preparations.step();
}
bool GpuLayerPreview::native_preparations_pending() const
{
  return impl_->native_preparations.pending() || impl_->native_materials.image_uploads_pending();
}
NativeMaterialPreparationStatus GpuLayerPreview::native_preparation_status(
    const std::string &resource) const
{
  auto state = impl_->native_preparations.status(resource);
  if (const auto error = impl_->native_materials.image_error(resource); !error.isEmpty()) {
    state.pending = false;
    state.ready = false;
    state.error = error;
    state.message = "Material source unavailable";
  }
  return state;
}
std::string GpuLayerPreview::native_image_error() const
{
  return impl_->native_image_failure;
}
void GpuLayerPreview::install_native_material(std::string resource,
                                              std::string content,
                                              std::shared_ptr<NativeMaterialGraph> graph)
{
  impl_->native_materials.install(resource, std::move(content), std::move(graph));
  impl_->native_materials.prepare_world_normal(resource);
}
void GpuLayerPreview::set_native_context_provider(NativeContextProvider provider)
{
  impl_->native_context_provider = std::move(provider);
}
NativeMaterialViewContext GpuLayerPreview::native_world_view(const TextureSet &set,
                                                             const TextureLayer &layer,
                                                             int tile) const
{
  if (!impl_->native_context_provider)
    throw NativeFragmentContextError(
        NativeFragmentContextCode::View,
        "Native fragment requires a real validated view: provider is unavailable");
  NativeMaterialGpuContext context;
  NativeMaterialDependencies required;
  required.view = true;
  impl_->native_context_provider(set, layer, tile, required, context);
  context.view.validate();
  return context.view;
}
TextureSet GpuLayerPreview::prepare_layer_capture(const TextureSet &source) const
{
  auto result = source;
  if (source.instance_layers) {
    result.layers = evaluation_layers(source);
    result.instance_layers.reset();
  }
  for (auto &layer : result.layers)
    if (layer.kind == LayerKind::Fill && layer.fill.native_material) {
      layer.fill.native_material
          ->validate();  // Admit the detached parameter copy before allocation.
      auto description = std::make_shared<NativeMaterialDescription>(*layer.fill.native_material);
      description->channels = impl_->native_materials.effective_channels(
          layer.fill.material_resource, *description);
      if (description->channels.empty())
        throw std::runtime_error("Native source has no prepared channel capabilities");
      layer.fill.native_material = std::move(description);
    }
  return result;
}
std::shared_ptr<const AdjustmentPaintSnapshot> GpuLayerPreview::begin_adjustment_source(
    const TextureSet &source, const QUuid &layer_id, const QUuid &document)
{
  PaintMetadataScope metadata;
  const auto &layers = evaluation_layers(source);
  const auto selected = std::find_if(
      layers.begin(), layers.end(), [&](const auto &item) { return item.id == layer_id; });
  if (selected == layers.end())
    throw std::invalid_argument("Adjustment layer is missing");
  const auto &layer = *selected;
  if (!adjustment_layer(layer))
    throw std::invalid_argument("Adjustment source requires Passthrough Paint channels");
  auto token = std::make_shared<AdjustmentPaintSnapshot>();
  token->id = QUuid::createUuid();
  token->set = source.id;
  token->layer = layer_id;
  token->document = document;
  token->tiles = source.tiles;
  auto &s = *impl_;
  Impl::AdjustmentEntry entry;
  entry.source = token;
  entry.set = prepare_layer_capture(source);
  entry.set.id = token->id;
  entry.layer = layer_id;
  try {
    for (auto &item : entry.set.layers)
      if (item.kind == LayerKind::Fill && item.fill.native_material) {
        const auto original = item.fill.material_resource;
        const auto alias = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        s.native_materials.alias(original, alias);
        entry.native_aliases.push_back(alias);
        item.fill.material_resource = alias;
        for (int tile : source.tiles)
          for (int channel :
               s.native_materials.effective_channels(alias, *item.fill.native_material))
          {
            auto context = s.native_materials.default_context(alias);
            context.tile = tile;
            const auto &dependencies = s.native_materials.dependencies(
                alias, channel, *item.fill.native_material);
            if (s.native_context_provider)
              s.native_context_provider(source, item, tile, dependencies, context);
            else if (dependencies.object || dependencies.view)
              throw std::runtime_error("Native material object/view context is not installed");
            entry.contexts[{item.id, tile, channel}] = std::move(context);
          }
      }
    s.adjustment_sources.emplace(token->id, std::move(entry));
    return token;
  }
  catch (...) {
    for (const auto &alias : entry.native_aliases)
      s.native_materials.erase(alias);
    throw;
  }
}
gpu::Texture *GpuLayerPreview::adjustment_page(const SmudgeCapture &capture,
                                               int plane,
                                               int tile,
                                               std::uint32_t page,
                                               gpu::Texture *own,
                                               performance::Clock::time_point deadline)
{
  if (!capture.adjustment.enabled(plane))
    return own;
  if (performance::Clock::now() >= deadline)
    return nullptr;
  auto &s = *impl_;
  const auto &token = *capture.adjustment.snapshot;
  try {
    const int channel = capture.adjustment.channels[plane];
    const auto found = s.adjustment_sources.find(token.id);
    if (found == s.adjustment_sources.end())
      throw std::runtime_error("Adjustment source snapshot is no longer resident");
    auto &entry = found->second;
    if (!entry.failure.empty()) {
      s.input_rejected = true;
      return nullptr;
    }
    gpu::Texture *prefix = nullptr;
    if (!s.adjustment_pages.contains(token, channel, tile, capture.resolution, page)) {
      const auto snapshot_key = entry.set.id.toString().toStdString() + ":" +
                                std::to_string(channel) + ":" + std::to_string(tile);
      if (!s.stacks.contains(snapshot_key)) {
        const auto live_key = token.set.toString().toStdString() + ":" + std::to_string(channel) +
                              ":" + std::to_string(tile);
        if (auto live = s.stacks.find(live_key); live != s.stacks.end()) {
          const auto &before = live->second;
          auto &snapshot = s.stacks[snapshot_key];
          snapshot.plan = before.plan;
          snapshot.queried_sources = before.queried_sources;
          std::set<QUuid> below;
          for (const auto &layer : evaluation_layers(entry.set)) {
            if (layer.id == entry.layer)
              break;
            below.insert(layer.id);
          }
          std::set<std::string> retained;
          for (const auto &[id, owner] : before.node_owners)
            if (below.contains(owner) && before.outputs.contains(id)) {
              snapshot.outputs.emplace(id, before.outputs.at(id));
              snapshot.node_owners.emplace(id, owner);
              retained.insert(id);
            }
          snapshot.plan.retain(retained);
          snapshot.node_versions = before.node_versions;
          std::erase_if(snapshot.node_versions,
                        [&](const auto &v) { return !retained.contains(v.first); });
          snapshot.node_clock = before.node_clock;
          snapshot.width = before.width;
          snapshot.height = before.height;
        }
      }
      auto saved_pins = s.adjustment_input_pins;
      struct RestorePins {
        std::set<std::string> &pins, saved;
        ~RestorePins()
        {
          pins = std::move(saved);
        }
      } restore{s.adjustment_input_pins, std::move(saved_pins)};
      for (const auto &[key, input] : s.inputs)
        s.adjustment_input_pins.insert(key);
      const auto result = update_tile(
          entry.set, channel, s.zero.get(), tile, {}, true, &entry.layer, false);
      if (result == GpuPreviewUpdate::Pending)
        return nullptr;
      if (result != GpuPreviewUpdate::Updated)
        throw std::runtime_error("Adjustment lower stack is unavailable on GPU");
      auto &stack = s.stacks.at(entry.set.id.toString().toStdString() + ":" +
                                std::to_string(channel) + ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + entry.layer.toString().toStdString() +
                                           "/below");
      prefix = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    }
    auto *result = s.adjustment_pages.compose(
        token, channel, tile, capture.resolution, page, prefix, own);
    for (const auto &id : s.adjustment_pages.take_discarded()) {
      const auto key = id.toString().toStdString();
      std::erase_if(s.stacks, [&](const auto &p) { return p.first.starts_with(key + ":"); });
      std::erase_if(s.mask_stacks, [&](const auto &p) { return p.first.starts_with(key); });
    }
    return result;
  }
  catch (const AdjustmentPaintMemoryError &error) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id, token.set, error.what(), error.needed, error.available};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = error.what();
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
  catch (const std::bad_alloc &) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id,
        token.set,
        "Adjustment GPU source allocation failed (active pages need 0.125 MiB); available " +
            std::to_string(double(texture_free_vram.load()) / (1024 * 1024)) +
            " MiB. Stroke cancelled; prior image retained."};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = pending_adjustment_failure->message;
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
  catch (const std::exception &error) {
    s.input_rejected = true;
    pending_adjustment_failure = AdjustmentPaintFailure{
        token.id,
        token.set,
        std::string("Adjustment GPU source failed: ") + error.what() +
            ". Stroke cancelled; prior image retained."};
    if (auto found = s.adjustment_sources.find(token.id); found != s.adjustment_sources.end())
      found->second.failure = pending_adjustment_failure->message;
    pending_adjustment_failure->document = token.document;
    pending_adjustment_failure->layer = token.layer;
    return nullptr;
  }
}
void GpuLayerPreview::diagnostic_adjustment_budget(std::optional<std::uint64_t> limit)
{
  if (!qEnvironmentVariableIsSet("BTS_BM_ADJUSTMENT_DEMO") &&
      !qEnvironmentVariableIsSet("BTS_BM_GPU_ADJUSTMENT_TEST"))
    throw std::logic_error(
        "Adjustment budget injection is restricted to the explicit diagnostic actor");
  impl_->adjustment_pages.diagnostic_budget(limit);
}
std::array<std::uint64_t, 2> GpuLayerPreview::adjustment_copy_statistics() const
{
  return {impl_->frozen_prefix_copies, impl_->frozen_prefix_copy_texels};
}
std::array<std::uint64_t, 3> GpuLayerPreview::native_statistics() const
{
  const auto s = impl_->native_materials.statistics();
  return {s.sources, s.programs, s.dispatches};
}
bool GpuLayerPreview::prefetch_geometry(
    const std::map<int, std::shared_ptr<const FillSurfaceAtlas>> &atlases)
{
  for (const auto &[tile, atlas] : atlases)
    if (!impl_->surface(atlas, tile))
      return false;
  return true;
}
void GpuLayerPreview::set_padding_geometry(
    std::uint32_t uid,
    const std::map<int, std::shared_ptr<const GpuMarginPlan>> &extend,
    const std::map<int, std::shared_ptr<const GpuMarginPlan>> &adjacent)
{
  impl_->margin_plans[uid] = {extend, adjacent};
}
void GpuLayerPreview::set_native_normal_error(const QUuid &set, std::string error)
{
  impl_->native_normal_errors[set] = std::move(error);
}
void GpuLayerPreview::clear_native_normals(const QUuid &set)
{
  impl_->native_normal_errors.erase(set);
  std::erase_if(impl_->native_normals, [&](const auto &v) { return std::get<0>(v.first) == set; });
}
void GpuLayerPreview::set_native_normal(const QUuid &set,
                                        const QUuid &layer,
                                        int tile,
                                        std::shared_ptr<gpu::Texture> image)
{
  impl_->native_normals[{set, layer, tile}] = std::move(image);
  ++impl_->native_normal_revision;
}
TextureSet GpuLayerPreview::native_normal_stack(const TextureSet &source) const
{
  TextureSet set = source;
  set.layers = evaluation_layers(source);
  set.instance_layers.reset();
  bool any = false;
  for (auto &layer : set.layers) {
    if (!source.channels[3])
      layer.channels[3] = false;
    if (layer.fill.native_material) {
      const bool normal = layer.fill.native_material->height_policy == NativeHeightPolicy::None &&
                          impl_->native_materials.contains(layer.fill.material_resource) &&
                          impl_->native_materials.world_normal_perturbed(
                              layer.fill.material_resource);
      layer.channels[3] = normal && set.channels[4] && layer.channels[4];
      if (normal) {
        any = true;
        layer.opacity[3] = layer.opacity[4];
        layer.blending[3] = layer.blending[4];
      }
    }
  }
  if (any) {
    set.channels[3] = true;
    for (auto &layer : set.layers)
      if (layer.kind == LayerKind::Folder) {
        layer.channels[3] = layer.channels[4];
        layer.opacity[3] = layer.opacity[4];
        layer.blending[3] = layer.blending[4];
      }
  }
  return set;
}
bool GpuLayerPreview::uniform(gpu::Texture *target, const std::array<float, 4> &value)
{
  auto &s = *impl_;
  const auto format = GPU_texture_format(target);
  if (s.uniform_failed || GPU_texture_dimensions(target) != 2 || GPU_texture_is_array(target) ||
      (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
       format != gpu::TextureFormat::SFLOAT_32_32_32_32))
    return false;
  if (!s.uniform_shaders.contains(format))
    s.uniform_shaders.emplace(format, compile_uniform(format));
  auto *shader = s.uniform_shaders.at(format).get();
  if (!shader) {
    s.uniform_failed = true;
    return false;
  }
  // Update the canonical image directly: no full-size broadcast temporary/copy.
  performance::Scope timing("gpu_uniform_broadcast");
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_shader_bind(shader);
  GPU_shader_uniform_4fv(shader, "fill_value", value.data());
  GPU_texture_image_bind(target, 0);
  GPU_compute_dispatch(
      shader, (GPU_texture_width(target) + 15) / 16, (GPU_texture_height(target) + 15) / 16, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_image_unbind(target);
  GPU_shader_unbind();
  return true;
}
bool GpuLayerPreview::supported(const TextureSet &set, int channel)
{
  if (channel == 15) {
    if (std::getenv("BTS_GPU_STACK_DISABLE"))
      return false;
    const auto &layers = evaluation_layers(set);
    const auto selected = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == set.selected_layer; });
    if (selected == layers.end() || !selected->mask)
      return false;
    for (const auto &layer : layers)
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (!gpu_mask_effect_supported(effect))
            return false;
    return true;
  }
  if (channel == 3)
    return supported(set, 6) && (!set.channels[4] || supported(set, 4));
  if (!gpu_authored_channel(set, channel) || std::getenv("BTS_GPU_STACK_DISABLE"))
    return false;
  const auto unsupported = [&](const TextureLayer &layer, const char *reason) {
    static std::set<std::string> logged;
    const auto key = set.id.toString().toStdString() + ":" + layer.id.toString().toStdString() +
                     ":" + std::to_string(channel) + ":" + reason;
    if (logged.insert(key).second)
      std::fprintf(stderr,
                   "BTS_GPU_STACK_FALLBACK uid=%u layer=%s channel=%d reason=%s background=1\n",
                   set.material_session_uid,
                   layer.name.c_str(),
                   channel,
                   reason);
    return false;
  };
  const bool anchor_dependencies = std::any_of(
      evaluation_layers(set).begin(), evaluation_layers(set).end(), [](const auto &layer) {
        return TextureDocument::anchor_dependent(layer);
      });
  if (anchor_dependencies)
    for (const auto &layer : evaluation_layers(set))
      if (layer.mask)
        for (const auto &effect : layer.mask->effects)
          if (!gpu_mask_effect_supported(effect))
            return unsupported(layer, "anchor-producer-mask-effects");
  for (int tile : set.tiles)
    for (const auto &layer : evaluation_layers(set)) {
      const auto info = gpu_layer_channel(layer, channel);
      if (!gpu_channel_enabled(set, channel) || !active(layer, channel, tile))
        continue;

      if (int(info.blend) > 28)
        return unsupported(layer, "blend-mode");
      if (layer.mask && (layer.mask->enabled || anchor_dependencies)) {
        if (std::any_of(layer.mask->effects.begin(), layer.mask->effects.end(), [](const auto &e) {
              return !gpu_mask_effect_supported(e);
            }))
          return unsupported(layer, "mask-effects");
      }
      if (layer.kind == LayerKind::Fill) {
        if (const auto &source = fill_channel_texture(layer.fill, info.fill_channel);
            source && !source->pixels)
          return unsupported(layer, "source-not-ready");
      }
    }
  return true;
}
std::uint64_t GpuLayerPreview::texture_resource_allocations() const
{
  return impl_->resource_allocations;
}
blender::gpu::Texture *GpuLayerPreview::diagnostic_composite_texture(const TextureSet &set,
                                                                     int channel,
                                                                     int tile) const
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto found = impl_->stacks.find(key);
  return found == impl_->stacks.end() ? nullptr : found->second.head;
}
std::uint64_t GpuLayerPreview::content_node_revision(const TextureSet &set,
                                                     int channel,
                                                     int tile,
                                                     const std::string &node) const
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto stack = impl_->stacks.find(key);
  if (stack == impl_->stacks.end())
    return 0;
  const auto found = stack->second.node_versions.find(node);
  if (found == stack->second.node_versions.end())
    return 0;
  std::uint64_t revision = 0;
  for (const auto &[chunk, page] : found->second)
    revision = std::max(revision, page.revision);
  return revision;
}
#include "GpuLayerPreviewUpdateTile.inc"
std::optional<GpuComparisonInputs> GpuLayerPreview::comparison_inputs(const TextureSet &set,
                                                                      const GpuMaskStage &stage,
                                                                      gpu::Texture *target,
                                                                      int tile)
{
  auto &s = *impl_;
  if (stage.rows.empty()) {
    s.input_rejected = true;
    return std::nullopt;
  }
  GpuComparisonInputs result;
  for (auto &input : result)
    input.image = s.zero.get();
  const int channel = stage.comparison_channel == 3 ? 6 : stage.comparison_channel;
  const auto &row = stage.rows.front();
  for (int operand = 0; operand < 2; ++operand) {
    if (int(row[1][0]) != operand && int(row[1][1]) != operand)
      continue;
    const auto memo_key = std::to_string(reinterpret_cast<std::uintptr_t>(&set)) + ":" +
                          stage.comparison_layer.toString().toStdString() + ":" +
                          std::to_string(channel) + ":" + std::to_string(tile) + ":" +
                          std::to_string(operand);
    if (const auto cached = s.comparison_memo.find(memo_key); cached != s.comparison_memo.end()) {
      result[operand] = cached->second;
      continue;
    }
    const auto updated = update_tile(
        set, channel, target, tile, {}, true, &stage.comparison_layer, operand == 1);
    if (updated != GpuPreviewUpdate::Updated) {
      s.input_rejected = updated == GpuPreviewUpdate::Unsupported;
      return std::nullopt;
    }
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile);
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + stage.comparison_layer.toString().toStdString() +
                          (operand ? "/source" : "/below");
    const auto &query = stack.queries.at(query_id);
    auto &input = result[operand];
    input.key = key + query_id;
    input.enabled = query.enabled;
    input.base = gpu_channel_base(set, channel);
    input.image = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    input.pages = stack.node_versions.at(query_id);
    s.comparison_memo.emplace(memo_key, input);
  }
  return result;
}
std::optional<GpuAnchorInput> GpuLayerPreview::anchor_input(const TextureSet &set,
                                                            const QUuid &consumer,
                                                            const AnchorReference &ref,
                                                            int tile)
{
  auto &s = *impl_;
  GpuAnchorInput input;
  input.image = s.zero.get();
  input.key = "broken:" + ref.source.toString().toStdString();
  if (!TextureDocument::anchor_reference_problem(set, consumer, ref).isEmpty())
    return input;
  const auto point = TextureDocument::anchor_point(set, ref.source);
  if (!point)
    return input;
  const auto memo = std::to_string(reinterpret_cast<std::uintptr_t>(&set)) + ":" +
                    ref.source.toString().toStdString() + ":" + std::to_string(ref.channel) + ":" +
                    std::to_string(tile);
  if (const auto found = s.anchor_memo.find(memo); found != s.anchor_memo.end())
    return found->second;
  if (point->mask) {
    auto *image = mask_texture(set, point->layer, tile, &point->id);
    if (!image)
      return std::nullopt;
    const auto &layers = evaluation_layers(set);
    const auto layer = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == point->layer; });
    const auto key = paint_key(set, *layer, 2, true, tile);
    auto &stack = s.mask_stacks.at(key);
    input.image = image;
    input.key = key + point->id.toString().toStdString();
    input.pages = stack.node_versions[point->id.toString().toStdString()];
    input.enabled = true;
    input.mask = true;
  }
  else {
    const int channel = ref.channel == 3  ? 6 :
                        ref.channel == 14 ? 16 :
                        ref.channel >= 6  ? ref.channel + 1 :
                                            ref.channel;
    const auto updated = update_tile(
        set, channel, s.zero.get(), tile, {}, true, &point->layer, true, &point->id, false, true);
    if (updated != GpuPreviewUpdate::Updated) {
      s.input_rejected = updated == GpuPreviewUpdate::Unsupported;
      return std::nullopt;
    }
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile) + ":anchor:" + point->layer.toString().toStdString();
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + point->layer.toString().toStdString() +
                          "/source/before-selected-effect/anchor/" +
                          point->id.toString().toStdString();
    const auto &query = stack.queries.at(query_id);
    input.image = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    input.key = key + query_id;
    input.pages = stack.node_versions[query_id];
    input.enabled = query.enabled;
  }
  s.anchor_memo.emplace(memo, input);
  return input;
}
GpuPreviewUpdate GpuLayerPreview::histogram(const TextureSet &set,
                                            const HistogramRequest &request,
                                            std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_ui_histogram");
  auto &s = *impl_;
  if (set.tiles.empty())
    return GpuPreviewUpdate::Unsupported;
  const int tile = *set.tiles.begin(), w = set.width(tile), h = set.rows(tile);
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  GpuHistogram::Input input{s.zero.get(), s.zero.get(), s.zero.get()};
  input.owners = input.selection = s.geometry_masks.empty();
  input.width = w;
  input.height = h;
  input.channel = request.channel;
  input.compare = request.compare;
  input.component = request.component;
  input.curve = request.curve;
  if (request.mask) {
    if (!found->mask)
      return GpuPreviewUpdate::Unsupported;
    const auto *effect = find_mask_effect(found->mask->effects, request.effect);
    const auto input_mask = mask_effect_input(*found->mask, request.effect);
    if (!effect || !input_mask)
      return GpuPreviewUpdate::Unsupported;
    auto prefix = *found;
    prefix.mask = *input_mask;
    const auto selected = *effect;
    GpuMaskBuilder builder(set, tile);
    const auto stages = builder.build(prefix, true);
    std::set<std::string> protected_keys;
    std::array<bool, 6> needed{};
    std::array<gpu::Texture *, 6> maps{};
    std::string signature;
    for (const auto &stage : stages)
      gpu_visit_mask_stage(
          stage,
          [&](const auto &part) {
            if (part.pixels)
              protected_keys.insert(part.owner_key + ":" + part.id);
            for (const auto &row : part.rows) {
              needed[0] |= row[0][0] == 4 && row[1][0] != 0;
              if (row[0][0] == 3)
                gpu_need_generator_row_maps(needed, row);
            }
          },
          true);
    std::array<const MeshMapPlane *, 6> planes{};
    for (int i = 0; i < 6; ++i)
      if (needed[i])
        if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map) {
          const auto member = map->tiles.find(tile);
          if (member != map->tiles.end() && member->second.pixels) {
            planes[i] = &member->second;
            const auto &p = *planes[i]->pixels;
            const auto key = "baked:" + p.hash + ":" + std::to_string(p.resolution) + ":" +
                             std::to_string(p.height);
            protected_keys.insert(key);
            signature += key;
          }
        }
    for (int i = 0; i < 6; ++i) {
      maps[i] = s.baked(planes[i], protected_keys);
      if (!maps[i])
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
      return comparison_inputs(set, stage, s.zero.get(), tile);
    };
    const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer,
                                                 const AnchorReference &ref) {
      return anchor_input(set, consumer, ref, tile);
    };
    input.mask = s.mask_prefix(paint_key(set, *found, 2, true, tile),
                               stages,
                               maps,
                               tile,
                               w,
                               h,
                               protected_keys,
                               signature,
                               resolve,
                               resolve_anchor,
                               true);
    if (!input.mask)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    if (request.compare) {
      GpuMaskStage stage;
      stage.comparison_layer = found->id;
      stage.comparison_channel = selected.compare_channel;
      TextureLayer description;
      description.mask = LayerMask{};
      description.mask->effects = {selected};
      description.mask->effects.back().enabled = true;
      description.mask->effects.back().blending = LayerBlend::Normal;
      gpu_append_mask_effects(stage.rows, description, set, tile);
      if (stage.rows.empty())
        return GpuPreviewUpdate::Unsupported;
      const auto values = resolve(stage);
      if (!values)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
      input.below = (*values)[0].image;
      input.own = (*values)[1].image;
      input.enabled = {(*values)[0].enabled, (*values)[1].enabled};
      input.base = (*values)[0].base;
      input.operands = {selected.left, selected.right};
      input.constant = selected.constant;
      input.channel = selected.compare_channel;
    }
    else {
      input.channel = -1;
      input.component = 0;
    }
  }
  else {
    const int channel = request.channel == 3  ? 6 :
                        request.channel == 14 ? 16 :
                        request.channel >= 6  ? request.channel + 1 :
                                                request.channel;
    if (request.channel >= 6) {
      const auto data = gpu_layer_channel(*found, channel);
      const bool included = found->visible && data.enabled && data.blend != LayerBlend::Disabled &&
                            (found->geometry.type == 1 ? found->geometry.includes_tile(tile) :
                                                         !found->geometry.meshes.empty() ||
                                                             found->geometry.default_include);
      input.inclusion = included ? 0 : 1;
      if (input.inclusion == 0 && found->geometry.type == 0 && !found->geometry.meshes.empty()) {
        const auto ownership = set.geometry_ownership.find(tile);
        if (ownership == set.geometry_ownership.end())
          return GpuPreviewUpdate::Pending;
        input.owners = s.geometry_masks.atlas(ownership->second,
                                              s.remaining,
                                              testing_stack ?
                                                  performance::Clock::time_point::max() :
                                                  s.upload_start + std::chrono::milliseconds(4));
        if (!input.owners)
          return GpuPreviewUpdate::Pending;
        input.selection = s.geometry_masks.selection("histogram:" +
                                                         request.layer.toString().toStdString(),
                                                     set.geometry_names,
                                                     found->geometry);
        input.inclusion = 2;
      }
    }
    const auto updated = update_tile(
        set, channel, s.zero.get(), tile, {}, true, &request.layer, true, &request.effect);
    if (updated != GpuPreviewUpdate::Updated)
      return updated;
    const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                     std::to_string(tile);
    auto &stack = s.stacks.at(key);
    const auto query_id = "query/" + request.layer.toString().toStdString() +
                          "/source/before-selected-effect";
    const auto &query = stack.queries.at(query_id);
    input.own = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
  }
  s.histograms.update(request.effect.toString().toStdString(), input, ticket);
  return GpuPreviewUpdate::Updated;
}
gpu::Texture *GpuLayerPreview::mask_texture(const TextureSet &set,
                                            const QUuid &layer,
                                            int tile,
                                            const QUuid *point)
{
  auto &s = *impl_;
  const int w = set.width(tile), h = set.rows(tile);
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == layer; });
  if (found == layers.end() || !found->mask) {
    s.input_rejected = true;
    return nullptr;
  }
  GpuMaskBuilder builder(set, tile);
  auto stages = builder.build(*found, true);
  if (point) {
    const auto stop = std::find_if(stages.begin(), stages.end(), [&](const auto &stage) {
      return stage.id == point->toString().toStdString();
    });
    if (stop == stages.end()) {
      s.input_rejected = true;
      return nullptr;
    }
    stages.erase(stop + 1, stages.end());
  }
  if (stages.empty()) {
    s.input_rejected = true;
    return nullptr;
  };
  std::set<std::string> protected_keys;
  std::array<bool, 6> needed{};
  std::array<gpu::Texture *, 6> maps{};
  std::string signature;
  for (const auto &stage : stages)
    gpu_visit_mask_stage(
        stage,
        [&](const auto &part) {
          if (part.pixels)
            protected_keys.insert(part.owner_key + ":" + part.id);
          for (const auto &row : part.rows) {
            needed[0] |= row[0][0] == 4 && row[1][0] != 0;
            if (row[0][0] == 3)
              gpu_need_generator_row_maps(needed, row);
          }
        },
        true);
  std::array<const MeshMapPlane *, 6> planes{};
  for (int i = 0; i < 6; ++i)
    if (needed[i])
      if (const auto &map = set.mesh_maps[gpu_mask_map_roles[i]]; map) {
        const auto member = map->tiles.find(tile);
        if (member != map->tiles.end() && member->second.pixels) {
          planes[i] = &member->second;
          const auto &p = *planes[i]->pixels;
          const auto key = "baked:" + p.hash + ":" + std::to_string(p.resolution) + ":" +
                           std::to_string(p.height);
          protected_keys.insert(key);
          signature += key;
        }
      }
  for (int i = 0; i < 6; ++i) {
    maps[i] = s.baked(planes[i], protected_keys);
    if (!maps[i])
      return nullptr;
  }
  const GpuComparisonResolver resolve = [&](const GpuMaskStage &stage) {
    return comparison_inputs(set, stage, s.zero.get(), tile);
  };
  const GpuAnchorResolver resolve_anchor = [&](const QUuid &consumer, const AnchorReference &ref) {
    return anchor_input(set, consumer, ref, tile);
  };
  const auto key = paint_key(set, *found, 2, true, tile);
  auto *mask = s.mask_prefix(key,
                             stages,
                             maps,
                             tile,
                             w,
                             h,
                             protected_keys,
                             signature,
                             resolve,
                             resolve_anchor,
                             point != nullptr);
  if (!mask)
    return nullptr;
  return mask;
}
GpuPreviewUpdate GpuLayerPreview::sample_material(const TextureSet &set,
                                                  const MaterialSampleRequest &request,
                                                  std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_material_pick");
  auto &s = *impl_;
  if (s.material_samples.pending())
    return GpuPreviewUpdate::Pending;
  const int tile = request.tile;
  if (!set.tiles.contains(tile) || request.x < 0 || request.y < 0 ||
      request.x >= set.width(tile) || request.y >= set.rows(tile))
    return GpuPreviewUpdate::Unsupported;
  const int w = set.width(tile), h = set.rows(tile);
  s.input_rejected = false;
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  std::array<GpuMaterialSample::Input, 6> inputs;
  if (!request.mask.isNull()) {
    const auto &layers = evaluation_layers(set);
    const auto found = std::find_if(
        layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.mask; });
    if (found == layers.end() || !found->mask)
      return GpuPreviewUpdate::Unsupported;
    auto *mask = mask_texture(set, found->id, tile);
    if (!mask)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    inputs[0].image = mask;
  }
  else {
    const QUuid whole_stack;
    for (int c = 0; c < 6; ++c) {
      if (!request.channels[c])
        continue;
      const int channel = c == 3 ? 6 : c;
      const auto state = update_tile(set, channel, s.zero.get(), tile, {}, true, &whole_stack);
      if (state != GpuPreviewUpdate::Updated)
        return state;
      auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                                ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + whole_stack.toString().toStdString() +
                                           "/below");
      inputs[c] = {query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get(),
                   gpu_channel_base(set, channel)};
    }
  }
  s.material_samples.update(request, inputs, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<MaterialSample> GpuLayerPreview::material_sample_result() const
{
  return impl_->material_samples.get();
}
GpuPreviewUpdate GpuLayerPreview::capture_author_chunk(const std::string &key,
                                                       const PaintPixels &pixels,
                                                       int channel,
                                                       int tile,
                                                       int x,
                                                       int y,
                                                       int width,
                                                       int height,
                                                       GpuProjectReadback &readback)
{
  PaintMetadataScope metadata;
  auto &s = *impl_;
  s.input_rejected = false;
  const auto *source = s.paint(key, pixels, channel, {key});
  if (!source)
    return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  readback.submit(0, tile, const_cast<gpu::Texture *>(source), x, y, 0, width, height);
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::capture_layer(const TextureSet &set,
                                                const LayerCaptureRequest &request,
                                                std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_capture");
  auto &s = *impl_;
  s.input_rejected = false;
  if (s.layer_capture.pending())
    return GpuPreviewUpdate::Pending;
  if (!set.tiles.contains(request.tile) || request.channel < 0 ||
      request.channel > (request.final_composite ? 16 : 14) || request.channel == 15)
    return GpuPreviewUpdate::Unsupported;
  const int tile = request.tile, w = set.width(tile), h = set.rows(tile);
  if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
      state != GpuPreviewUpdate::Updated)
    return state;
  if (request.final_composite) {
    if (!s.export_final || GPU_texture_width(s.export_final.get()) != w ||
        GPU_texture_height(s.export_final.get()) != h)
      s.export_final = texture(
          "BTS explicit export final GPU channel", w, h, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!s.export_final)
      return GpuPreviewUpdate::Unsupported;
    const int channel = request.normal_perturbation ? 3 : request.channel;
    GpuPreviewUpdate status;
    if (channel == 5 && request.authored_ao)
      status = update_tile(set,
                           channel,
                           s.export_final.get(),
                           tile,
                           {},
                           false,
                           nullptr,
                           false,
                           nullptr,
                           false,
                           false,
                           true);
    else if (channel == 3 || channel == 5) {
      std::map<int, std::array<int, 3>> regions;
      for (int number : set.tiles)
        regions[number] = {};
      status = request.normal_perturbation ?
                   resolve_normal(set, s.export_final.get(), regions, tile, true) :
                   update_tiles_impl(set, channel, s.export_final.get(), regions, tile);
    }
    else
      status = update(set, channel, s.export_final.get(), tile);
    if (status != GpuPreviewUpdate::Updated)
      return status;
    GpuLayerCapture::Input final{
        s.export_final.get(), s.geometry_masks.empty(), s.geometry_masks.empty(), w, h};
    if (channel == 0 || channel == 1 || channel == 2 || channel == 3 || channel == 4 ||
        channel == 16 || (channel == 5 && request.authored_ao))
    {
      final.completion = 1;
      if (channel == 3)
        final.completion_base = precision_channel(
            set.channels[3] ? set.base_values : MaterialValues{}, 3, set.precision[3]);
      else if (channel == 16)
        final.completion_base.fill(set.displacement.midlevel);
      else {
        final.completion_base = gpu_channel_base(set, channel);
        if (channel == 4)
          final.completion_base.fill(precision_bump_base(set));
      }
    }
    else if (gpu_scalar_channel(channel)) {
      final.completion = 2;
      final.completion_base = gpu_channel_base(set, channel);
    }
    s.layer_capture.update(request, final, ticket);
    return GpuPreviewUpdate::Updated;
  }
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  GpuLayerCapture::Input input{
      s.zero.get(), s.geometry_masks.empty(), s.geometry_masks.empty(), w, h};
  if (request.mask) {
    input.image = mask_texture(set, request.layer, tile);
    if (!input.image)
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  else {
    const int channel = request.channel == 3  ? 6 :
                        request.channel == 14 ? 16 :
                        request.channel >= 6  ? request.channel + 1 :
                                                request.channel;
    TextureSet normalized = set;
    normalized.instance_layers.reset();
    normalized.layers = layers;
    auto &root = normalized.layers.at(found - layers.begin());
    root.mask.reset();
    QUuid query = request.layer;
    std::string suffix = "/source";
    if (request.contextual) {
      TextureLayer sentinel;
      sentinel.id = QUuid("{318aff28-684e-4183-aeb9-72e1fa400a81}");
      sentinel.depth = found->depth;
      sentinel.parent = found->parent;
      query = sentinel.id;
      normalized.layers.insert(normalized.layers.begin() + (found - layers.begin()) + 1, sentinel);
      suffix = "/below";
    }
    else {
      root.channels = layer_source_channels(
          root.channels, root.kind == LayerKind::Fill && bool(root.fill.native_material));
      root.opacity.fill(1);
      root.blending.fill(LayerBlend::Normal);
      root.ao_enabled = true;
      root.ao_opacity = 1;
      root.ao_blending = LayerBlend::Normal;
      for (auto &user : root.users) {
        user.enabled = true;
        user.opacity = 1;
        user.blending = LayerBlend::Normal;
      }
      root.displacement.enabled = true;
      root.displacement.opacity = 1;
      root.displacement.blending = LayerBlend::Normal;
    }
    const auto state = update_tile(
        normalized, channel, s.zero.get(), tile, {}, true, &query, !request.contextual);
    if (state != GpuPreviewUpdate::Updated)
      return state;
    auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                              ":" + std::to_string(tile));
    const auto &value = stack.queries.at("query/" + query.toString().toStdString() + suffix);
    input.image = value.root.empty() ? s.zero.get() : stack.outputs.at(value.root).get();
    if (!request.contextual) {
      const auto &g = found->geometry;
      input.inclusion = g.type == 1      ? (g.includes_tile(tile) ? 0 : 1) :
                        g.meshes.empty() ? (g.default_include ? 0 : 1) :
                                           2;
      if (input.inclusion == 2) {
        auto owner = set.geometry_ownership.find(tile);
        if (owner == set.geometry_ownership.end())
          return GpuPreviewUpdate::Pending;
        input.owners = s.geometry_masks.atlas(owner->second,
                                              s.remaining,
                                              testing_stack ?
                                                  performance::Clock::time_point::max() :
                                                  s.upload_start + std::chrono::milliseconds(4));
        if (!input.owners)
          return GpuPreviewUpdate::Pending;
        input.selection = s.geometry_masks.selection(
            "capture:" + request.layer.toString().toStdString(), set.geometry_names, g);
      }
    }
  }
  s.layer_capture.update(request, input, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<LayerCaptureResult> GpuLayerPreview::layer_capture_result() const
{
  return impl_->layer_capture.get();
}
GpuPreviewUpdate GpuLayerPreview::thumbnail(const TextureSet &set,
                                            const ThumbnailRequest &request,
                                            std::uint64_t ticket)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_thumbnail");
  auto &s = *impl_;
  s.input_rejected = false;
  if (s.thumbnails.pending())
    return GpuPreviewUpdate::Pending;
  if (set.tiles.empty() || request.channel < 0 || request.channel > 14)
    return GpuPreviewUpdate::Unsupported;
  const int channel = request.channel == 3  ? 6 :
                      request.channel == 14 ? 16 :
                      request.channel >= 6  ? request.channel + 1 :
                                              request.channel;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == request.layer; });
  if (found == layers.end())
    return GpuPreviewUpdate::Unsupported;
  std::vector<GpuThumbnail::Input> inputs;
  for (int tile : set.tiles) {
    const int w = set.width(tile), h = set.rows(tile);
    if (const auto state = s.initialize(w, h, gpu::TextureFormat::SFLOAT_32_32_32_32, false);
        state != GpuPreviewUpdate::Updated)
      return state;
    gpu::Texture *source = s.zero.get();
    if (request.mask) {
      source = mask_texture(set, request.layer, tile);
      if (!source)
        return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
    }
    else if (gpu_channel_enabled(set, channel) && gpu_layer_channel(*found, channel).enabled) {
      const auto state = update_tile(
          set, channel, s.zero.get(), tile, {}, true, &request.layer, true, nullptr, true);
      if (state != GpuPreviewUpdate::Updated)
        return state;
      auto &stack = s.stacks.at(set.id.toString().toStdString() + ":" + std::to_string(channel) +
                                ":" + std::to_string(tile));
      const auto &query = stack.queries.at("query/" + request.layer.toString().toStdString() +
                                           "/source/thumbnail");
      source = query.root.empty() ? s.zero.get() : stack.outputs.at(query.root).get();
    }
    inputs.push_back({source, w, h});
  }
  s.thumbnails.update(request, inputs, ticket);
  return GpuPreviewUpdate::Updated;
}
std::optional<ThumbnailMetadata> GpuLayerPreview::thumbnail_result() const
{
  return impl_->thumbnails.get();
}
void GpuLayerPreview::diagnose_pixel(const TextureSet &set, int channel, int tile, int x, int y)
{
  const auto key = set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
                   std::to_string(tile);
  const auto found = impl_->stacks.find(key);
  if (found == impl_->stacks.end())
    return;
  for (const auto &layer : evaluation_layers(set))
    if (layer.kind == LayerKind::Fill && layer.fill.mode == 5) {
      const auto atlas = set.projection_tiles.contains(tile) ? set.projection_tiles.at(tile) :
                                                               set.projection_geometry;
      if (!atlas || !atlas->tiles.contains(tile))
        continue;
      using namespace gpu::shader;
      ShaderCreateInfo info("pyGPU_Shader");
      info.local_group_size(1, 1)
          .sampler(5, ImageType::Float2D, "layer_parameters_img")
          .image(0,
                 gpu::TextureFormat::SFLOAT_32_32_32_32,
                 Qualifier::write,
                 ImageReadWriteType::image2D,
                 "result_img")
          .push_constant(Type::float3_t, "world_point");
      info.compute_source_generated = GpuLayerParameters::declarations() + gpu_warp::source +
                                      R"GLSL(
void main(){vec3 v=world_point-projection_offset,p=vec3(dot(rotation_x,v),dot(rotation_y,v),dot(rotation_z,v))*inverse_denominator;vec2 uv=vec2(-99);float depth=0;vec3 normal,du,dv;bool valid=warp_project(p,uv,depth,normal,du,dv);imageStore(result_img,ivec2(0),vec4(uv,depth,float(valid)));imageStore(result_img,ivec2(1,0),vec4(du,warp_projection_error));imageStore(result_img,ivec2(2,0),vec4(dv,0));}
)GLSL";
      Shader shader(
          GPU_shader_create_from_info_python(reinterpret_cast<GPUShaderCreateInfo *>(&info)),
          GPU_shader_free);
      GpuLayerParameters parameters;
      gpu_spatial_parameters(parameters, layer.fill, atlas.get(), set);
      auto input = texture("BTS warp projection diagnostic",
                           parameters.values.size(),
                           1,
                           gpu::TextureFormat::SFLOAT_32_32_32_32);
      GPU_texture_update(input.get(), GPU_DATA_FLOAT, parameters.values.data());
      auto output = texture(
          "BTS warp projection diagnostic result", 3, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
      const auto &point = atlas->tiles.at(tile)[y * atlas->resolution + x];
      GPU_shader_bind(shader.get());
      GPU_texture_bind(input.get(), 5);
      GPU_texture_image_bind(output.get(), 0);
      GPU_shader_uniform_3fv(shader.get(), "world_point", point.position.data());
      GPU_compute_dispatch(shader.get(), 1, 1, 1);
      GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
      auto *actual = static_cast<float *>(GPU_texture_read(output.get(), GPU_DATA_FLOAT, 0));
      const auto frame = fill_planar_frame(layer.fill, point, *atlas);
      const auto expected = fill_warp_coordinate(layer.fill, frame);
      std::printf(
          "BTS_GPU_WARP_NATIVE_DIAGNOSTIC x=%d y=%d gpu_uv=%.9g,%.9g depth=%.9g valid=%g error=%.9g cpu_uv=%.9g,%.9g depth=%.9g error=%.9g coverage=%g\n",
          x,
          y,
          actual[0],
          actual[1],
          actual[2],
          actual[3],
          actual[7],
          expected ? expected->u : -99,
          expected ? expected->v : -99,
          expected ? expected->depth : -99,
          expected ? expected->error : -1,
          expected ? fill_warp_coverage(layer.fill, frame, *expected) : 0);
      QJsonArray rows;
      for (const auto &row : parameters.values)
        rows.append(QJsonArray{row[0], row[1], row[2], row[3]});
      std::printf(
          "BTS_GPU_WARP_NATIVE_INPUT %s\n",
          QJsonDocument(
              QJsonObject{
                  {"parameters", rows},
                  {"point", QJsonArray{point.position[0], point.position[1], point.position[2]}}})
              .toJson(QJsonDocument::Compact)
              .constData());
      MEM_delete(actual);
      GPU_texture_unbind_all();
      GPU_texture_image_unbind_all();
      GPU_shader_unbind();
    }
  for (const auto &[id, image] : found->second.outputs) {
    const std::array<std::array<int, 3>, 1> locations{
        {{std::min(x, GPU_texture_width(image.get()) - 1),
          std::min(y, GPU_texture_height(image.get()) - 1),
          0}}};
    const auto p = probe_gpu_texture(image.get(), locations).front();
    std::printf("BTS_GPU_ORACLE_PREFIX id=%s rgba=%.9g,%.9g,%.9g,%.9g\n",
                id.c_str(),
                p[0],
                p[1],
                p[2],
                p[3]);
  }
}
void GpuLayerPreview::set_displacement_epoch(std::uint32_t uid, std::uint64_t epoch)
{
  impl_->displacement_epochs[uid] = epoch;
}
void GpuLayerPreview::retire_layer_channel(
    const TextureDocument &document,
    const TextureSet &set,
    const TextureLayer &previous,
    int channel,
    std::span<const std::shared_ptr<const FillTexture>> pending)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_layer_channel_retire");
  auto &s = *impl_;
  const auto before = blender::gpu::host_texture_memory::live_bytes();
  const int gpu_channel = channel == 14 ? 16 :
                          channel >= 6  ? channel + 1 :
                          channel == 3  ? 6 :
                                          channel;
  const int plane = channel < 5 ? channel : 2;
  const auto owner = previous.id.toString().toStdString();
  std::set<std::string> protected_resources, removed_resources;
  std::function<void(const std::shared_ptr<const FillTexture> &, std::set<std::string> &)> keys;
  keys = [&](const auto &texture, auto &result) {
    if (!texture)
      return;
    if (texture->tiles) {
      for (const auto &[tile, member] : *texture->tiles)
        keys(member, result);
    }
    else
      result.insert(resource_key(*texture));
  };
  keys(fill_channel_texture(previous.fill, channel), removed_resources);
  const auto keep_projection = [&](const FillProjection &projection) {
    for (int c = 0; c < 15; ++c)
      keys(fill_channel_texture(projection, c), protected_resources);
  };
  for (const auto &candidate : document.texture_sets())
    for (const auto &layer : evaluation_layers(candidate)) {
      for (int c = 0; c < 15; ++c) {
        const bool enabled = c < 5   ? layer.channels[c] :
                             c == 5  ? layer.ao_enabled :
                             c == 14 ? layer.displacement.enabled :
                                       layer.users[c - 6].enabled;
        if (enabled)
          keys(fill_channel_texture(layer.fill, c), protected_resources);
      }
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          keep_projection(effect.mapping.projection);
    }
  for (const auto &texture : pending)
    keys(texture, protected_resources);
  std::vector<std::string> input_keys, stack_keys;
  for (int tile : set.tiles) {
    input_keys.push_back(paint_key(set, previous, gpu_channel, false, tile));
    const auto stack = set.id.toString().toStdString() + ":" + std::to_string(gpu_channel) + ":" +
                       std::to_string(tile);
    stack_keys.push_back(stack);
    input_keys.push_back(stack + ":" + owner);
  }
  const auto owned_node = [&](const std::string &key) {
    return key == owner || key.starts_with(owner + "/");
  };
  std::uint64_t source_bytes = 0;
  std::size_t source_count = 0;
  for (auto it = s.inputs.begin(); it != s.inputs.end();) {
    const auto &key = it->first;
    bool retire = removed_resources.contains(key) && !protected_resources.contains(key);
    for (const auto &prefix : input_keys)
      retire |= key == prefix || key.starts_with(prefix + "/");
    if (!retire || it->second.retained()) {
      ++it;
      continue;
    }
    source_bytes += it->second.bytes();
    s.used_bytes -= it->second.bytes();
    ++source_count;
    it = s.inputs.erase(it);
  }
  for (auto &[key, stack] : s.stacks) {
    if (std::none_of(stack_keys.begin(), stack_keys.end(), [&](const auto &prefix) {
          return key == prefix || key.starts_with(prefix + ":");
        }))
      continue;
    if (owned_node(stack.head_id)) {
      stack.head = nullptr;
      stack.head_id.clear();
    }
    std::erase_if(stack.outputs, [&](const auto &item) { return owned_node(item.first); });
    std::erase_if(stack.node_versions, [&](const auto &item) { return owned_node(item.first); });
    std::erase_if(stack.queries, [&](const auto &item) {
      return item.second.owner == previous.id || owned_node(item.second.root);
    });
    stack.queried_sources.erase(owner);
    std::set<std::string> live;
    for (const auto &[id, image] : stack.outputs)
      live.insert(id);
    stack.plan.retain(live);
  }
  std::set<std::tuple<std::uint64_t, std::uint32_t, int>> keep_pages;
  for (const auto &[key, entry] : s.inputs)
    if (entry.plane >= 0)
      for (const auto &[page, version] : entry.pages)
        keep_pages.emplace(version, page, entry.plane);
  const auto keep_pixels = [&](const TilePaintPixels &pixels) {
    for (const auto &[tile, data] : pixels.tiles())
      data.visit_page_versions(
          plane, [&](auto page, auto version) { keep_pages.emplace(version, page, plane); });
  };
  const auto keep_effect = [&](const EffectPaint &paint) {
    keep_pixels(paint.pixels);
    if (plane == 2) {
      keep_pixels(paint.ao);
      keep_pixels(paint.displacement);
      for (const auto &pixels : paint.users)
        keep_pixels(pixels);
    }
  };
  for (const auto &candidate : document.texture_sets()) {
    for (const auto &layer : evaluation_layers(candidate)) {
      keep_pixels(layer.pixels);
      if (plane == 2) {
        keep_pixels(layer.ao_pixels);
        keep_pixels(layer.displacement.pixels);
        for (const auto &user : layer.users)
          keep_pixels(user.pixels);
        if (layer.mask)
          keep_pixels(layer.mask->pixels);
      }
      for (const auto &effect : layer.effects)
        if (effect.kind == ContentEffectKind::Paint)
          keep_effect(effect.paint);
      if (layer.mask)
        for (const auto &effect : mask_effects_recursive(layer.mask->effects))
          if (effect.kind == MaskEffectKind::Paint)
            keep_effect(effect.paint);
    }
  }
  const auto retired_pages = s.authoring_pages.retire_unused(plane, keep_pages);
  s.comparison_memo.clear();
  s.anchor_memo.clear();
  const auto after = blender::gpu::host_texture_memory::live_bytes();
  std::size_t shared = 0;
  for (const auto &key : removed_resources)
    shared += protected_resources.contains(key);
  std::printf(
      "BTS_CHANNEL_RELEASE channel=%d source_count=%zu source_bytes=%llu authoring_pages=%zu shared_preserved=%zu storage_before=%llu storage_after=%llu\n",
      channel,
      source_count,
      (unsigned long long)source_bytes,
      retired_pages,
      shared,
      (unsigned long long)before,
      (unsigned long long)after);
}
bool GpuLayerPreview::texture_resource_ready(const FillTexture &texture) const
{
  const auto ready = [&](const FillTexture &source) {
    const auto found = impl_->inputs.find(resource_key(source));
    return found != impl_->inputs.end() && found->second.resource && found->second.image &&
           !found->second.rejected && found->second.width == source.width &&
           found->second.height == source.height && found->second.row == source.height;
  };
  if (texture.tiles) {
    for (const auto &[tile, image] : *texture.tiles)
      if (!ready(*image))
        return false;
    return true;
  }
  return ready(texture);
}
GpuPreviewUpdate GpuLayerPreview::prepare_baked_resource(int tile,
                                                         const MeshMapPlane &plane,
                                                         NativeBakedAoLease &lease)
{
  const auto key = native_baked_ao_key(plane, tile);
  if (!GPU_context_active_get())
    throw std::logic_error("Native baked AO upload requires the GPU owner");
  auto &s = *impl_;
  s.input_rejected = false;
  ++s.clock;
  const std::set<std::string> protected_keys{key};
  const bool ready = lease.state.prepare(
      plane,
      tile,
      [&] {
        auto *entry = s.entry(key, plane.resolution, plane.rows(), true, protected_keys);
        if (!entry || entry->rejected || !entry->image)
          native_baked_ao_reject(tile,
                                 "existing upload pool could not admit the baked allocation");
        lease.resource = {entry->image.get(), entry->width, entry->height, key, entry->residency};
        return NativeBakedAoPin{key, entry->width, entry->height, entry->residency};
      },
      [&](const NativeBakedAoPin &pin) {
        const auto found = s.inputs.find(pin.key);
        if (found == s.inputs.end() || found->second.residency != pin.residency ||
            found->second.image.get() != lease.resource.texture)
          native_baked_ao_reject(tile,
                                 "pinned baked allocation is no longer owned by the upload pool");
        auto *image = s.baked(&plane, protected_keys);
        if (s.input_rejected)
          native_baked_ao_reject(tile, "baked upload was rejected");
        return image != nullptr;
      });
  const auto found = s.inputs.find(key);
  if (found == s.inputs.end() || found->second.residency != lease.state.pin().residency ||
      found->second.image.get() != lease.resource.texture)
    native_baked_ao_reject(tile, "retained baked resource belongs to another upload pool");
  lease.validate(plane, tile, false);
  return ready ? GpuPreviewUpdate::Updated : GpuPreviewUpdate::Pending;
}
std::optional<GpuTextureResource> GpuLayerPreview::retain_texture_resource(
    const FillTexture &source) const
{
  if (source.tiles || !texture_resource_ready(source))
    return std::nullopt;
  const auto key = resource_key(source);
  const auto &entry = impl_->inputs.at(key);
  return GpuTextureResource{entry.image.get(), entry.width, entry.height, key, entry.residency};
}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resource(const FillTexture &texture)
{
  const std::array<const FillTexture *, 1> group{&texture};
  return prepare_texture_resources(group);
}
GpuPreviewUpdate GpuLayerPreview::prepare_texture_resources(
    std::span<const FillTexture *const> textures)
{
  auto &s = *impl_;
  s.input_rejected = false;
  ++s.clock;
  std::set<std::string> protected_keys;
  for (const auto *texture : textures) {
    if (!texture)
      throw std::invalid_argument("Empty GPU material input");
    if (texture->tiles)
      for (const auto &[tile, image] : *texture->tiles)
        protected_keys.insert(resource_key(*image));
    else
      protected_keys.insert(resource_key(*texture));
  }
  bool ready = true;
  const auto upload = [&](const FillTexture &image) {
    if (!s.resource(image, protected_keys))
      ready = false;
  };
  for (const auto *texture : textures) {
    if (texture->tiles)
      for (const auto &[tile, image] : *texture->tiles)
        upload(*image);
    else
      upload(*texture);
  }
  return s.input_rejected ? GpuPreviewUpdate::Unsupported :
         ready            ? GpuPreviewUpdate::Updated :
                            GpuPreviewUpdate::Pending;
}

bool GpuLayerPreview::metadata_pending() const
{
  return impl_->ranges.pending() || impl_->histograms.pending() ||
         impl_->material_samples.pending() || impl_->thumbnails.pending() ||
         impl_->layer_capture.pending();
}
std::optional<HistogramMetadata> GpuLayerPreview::histogram_result(const QUuid &id) const
{
  return impl_->histograms.get(id.toString().toStdString());
}
std::vector<GpuLayerPreview::DisplacementSummary> GpuLayerPreview::displacement_summaries()
{
  impl_->histograms.poll();
  impl_->material_samples.poll();
  impl_->thumbnails.poll();
  impl_->layer_capture.poll();
  std::vector<DisplacementSummary> result;
  for (const auto &v : impl_->ranges.poll())
    result.push_back({v.uid, v.minimum, v.maximum, v.present, v.epoch});
  return result;
}
bool GpuLayerPreview::wrote_image() const
{
  return impl_->image_changed;
}
GpuPreviewUpdate GpuLayerPreview::update(
    const TextureSet &set, int channel, gpu::Texture *target, int tile, std::array<int, 3> offset)
{
  return update_tiles(set, channel, target, {{tile, offset}});
}
GpuPreviewUpdate GpuLayerPreview::resolve_mask(const TextureSet &set,
                                               gpu::Texture *target,
                                               int tile,
                                               std::array<int, 3> offset)
{
  PaintMetadataScope metadata;
  performance::Scope timing("gpu_mask_inspection");
  if (!supported(set, 15) || !set.tiles.contains(tile))
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  s.input_rejected = false;
  const auto format = GPU_texture_format(target);
  const bool array = GPU_texture_is_array(target);
  const int w = set.width(tile), h = set.rows(tile);
  if (GPU_texture_dimensions(target) != 2 || offset[0] < 0 || offset[1] < 0 || offset[2] < 0 ||
      GPU_texture_width(target) < offset[0] + w || GPU_texture_height(target) < offset[1] + h ||
      offset[2] >= std::max(1, GPU_texture_layer_count(target)))
    return GpuPreviewUpdate::Unsupported;
  if (format != gpu::TextureFormat::SFLOAT_16_16_16_16 &&
      format != gpu::TextureFormat::SFLOAT_32_32_32_32)
    return GpuPreviewUpdate::Unsupported;
  if (const auto state = s.initialize(w, h, format, array); state != GpuPreviewUpdate::Updated)
    return state;
  const auto &layers = evaluation_layers(set);
  const auto found = std::find_if(
      layers.begin(), layers.end(), [&](const auto &l) { return l.id == set.selected_layer; });
  if (found == layers.end() || !found->mask)
    return GpuPreviewUpdate::Unsupported;
  const auto key = paint_key(set, *found, 2, true, tile);
  auto *mask = mask_texture(set, found->id, tile);
  if (!mask)
    return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  auto &cache = s.mask_stacks.at(key);
  const auto target_key = std::pair{reinterpret_cast<std::uintptr_t>(target), offset};
  const auto identity = "mask:" + key;
  const int columns = (w + 255) / 256;
  if (s.target_owners[target_key] != identity)
    for (int y = 0; y < h; y += 256)
      for (int x = 0; x < w; x += 256)
        cache.output_dirty.insert((y / 256) * columns + x / 256);
  if (!cache.output_dirty.empty()) {
    const auto shader_key = std::pair{format, array};
    if (!s.mask_output_shaders.contains(shader_key))
      s.mask_output_shaders.emplace(shader_key,
                                    Shader(compile_mask_output(format, array), GPU_shader_free));
    auto *shader = s.mask_output_shaders.at(shader_key).get();
    if (!shader)
      return GpuPreviewUpdate::Unsupported;
    GPU_shader_bind(shader);
    GPU_texture_bind(mask, 0);
    GPU_texture_image_bind(target, 0);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    for (int chunk : cache.output_dirty) {
      const int x = chunk % columns * 256, y = chunk / columns * 256, origin[2] = {x, y},
                size[2] = {std::min(256, w - x), std::min(256, h - y)};
      GPU_shader_uniform_2iv(shader, "region_origin", origin);
      GPU_shader_uniform_2iv(shader, "region_size", size);
      GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
    }
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    GPU_texture_image_unbind(target);
    GPU_shader_unbind();
    s.image_changed = true;
    std::printf("BTS_GPU_MASK_INSPECTION tile=%d width=%d height=%d chunks=%zu cpu_composite=0\n",
                tile,
                w,
                h,
                cache.output_dirty.size());
    cache.output_dirty.clear();
  }
  s.target_owners[target_key] = identity;
  if (std::getenv("BTS_VERIFY_GPU_STACK")) {
    std::vector<std::array<int, 3>> positions;
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        positions.push_back(
            {offset[0] + (2 * x + 1) * w / 16, offset[1] + (2 * y + 1) * h / 16, offset[2]});
    const auto pixels = probe_gpu_texture(target, positions);
    double maximum = 0;
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      const float value = TextureDocument::mask_value(
          set, set.selected_layer, positions[i][0] - offset[0], positions[i][1] - offset[1], tile);
      for (int c = 0; c < 4; ++c)
        maximum = std::max(maximum, double(std::abs(pixels[i][c] - (c == 3 ? 1 : value))));
    }
    const bool valid = maximum < .001;
    std::printf("BTS_GPU_STACK_VERIFIED channel=15 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU mask inspection differs from CPU oracle");
  }
  return GpuPreviewUpdate::Updated;
}

GpuPreviewUpdate GpuLayerPreview::update_tiles(const TextureSet &set,
                                               int channel,
                                               gpu::Texture *target,
                                               const std::map<int, std::array<int, 3>> &regions)
{
  return update_tiles_impl(set, channel, target, regions, 0);
}
GpuPreviewUpdate GpuLayerPreview::update_tiles_impl(
    const TextureSet &set,
    int channel,
    gpu::Texture *target,
    const std::map<int, std::array<int, 3>> &regions,
    int output_tile)
{
  impl_->image_changed = false;
  if (channel == 15) {
    bool pending = false;
    for (const auto &[tile, offset] : regions) {
      const auto result = resolve_mask(set, target, tile, offset);
      if (result == GpuPreviewUpdate::Unsupported)
        return result;
      pending |= result == GpuPreviewUpdate::Pending;
    }
    return pending ? GpuPreviewUpdate::Pending : GpuPreviewUpdate::Updated;
  }
  if ((channel == 3 || channel == 5) &&
      (regions.size() != set.tiles.size() ||
       std::any_of(
           set.tiles.begin(), set.tiles.end(), [&](int tile) { return !regions.contains(tile); })))
    return GpuPreviewUpdate::Unsupported;
  if (channel == 3)
    return resolve_normal(set, target, regions, output_tile);
  auto &s = *impl_;
  bool pending = false;
  for (const auto &[tile, offset] : regions) {
    const auto result = update_tile(set, channel, target, tile, offset, channel == 5);
    if (result == GpuPreviewUpdate::Unsupported)
      return result;
    pending |= result == GpuPreviewUpdate::Pending;
  }
  if (pending)
    return GpuPreviewUpdate::Pending;
  // GpuRangeSummary consumes a complete tile set and removes omitted sources.
  // A single-tile pixel update cannot claim new whole-material bounds, even if
  // older stacks happen to exist for the other UDIMs. The normal all-tile owner
  // submits one coherent complete range after all requested prefixes are ready.
  const bool complete_regions = regions.size() == set.tiles.size() &&
                                std::all_of(set.tiles.begin(), set.tiles.end(), [&](int tile) {
                                  return regions.contains(tile);
                                });
  if (channel == 16 && !testing_stack && complete_regions) {
    std::vector<GpuRangeSummary::Source> sources;
    for (int tile : set.tiles) {
      const auto key = set.id.toString().toStdString() + ":16:" + std::to_string(tile);
      const auto &stack = s.stacks.at(key);
      sources.push_back(
          {tile, set.width(tile), set.rows(tile), stack.head, stack.revision, stack.last_changes});
    }
    s.ranges.update(
        set.material_session_uid, sources, s.displacement_epochs[set.material_session_uid]);
  }
  if (channel != 5)
    return GpuPreviewUpdate::Updated;
  performance::Scope timing("gpu_ao_resolve");
  std::map<int, gpu::Texture *> bakes;
  std::set<std::string> protected_keys;
  if (set.mesh_maps[3])
    for (const auto &[tile, plane] : set.mesh_maps[3]->tiles)
      if (plane.pixels)
        protected_keys.insert("baked:" + plane.pixels->hash + ":" +
                              std::to_string(plane.pixels->resolution) + ":" +
                              std::to_string(plane.pixels->height));
  for (const auto &[tile, offset] : regions) {
    const MeshMapPlane *plane = nullptr;
    if (set.mesh_maps[3])
      if (auto i = set.mesh_maps[3]->tiles.find(tile); i != set.mesh_maps[3]->tiles.end())
        plane = &i->second;
    bakes[tile] = s.baked(plane, protected_keys);
    if (!bakes[tile])
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  auto dirty_update = s.derived_changes(set, 5, target, regions, output_tile);
  if (!dirty_update.needed)
    return GpuPreviewUpdate::Updated;
  auto &derived = s.derived[s.derived_key(set, 5)];
  if (!s.prepare_derived_masks(derived, dirty_update, set))
    return GpuPreviewUpdate::Unsupported;
  if (!s.presence_shader)
    s.presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
  if (!derived.presence)
    derived.presence = texture(
        "BTS cross-tile AO presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
  if (!s.presence_shader || !derived.presence)
    return GpuPreviewUpdate::Unsupported;
  auto *reduce = s.presence_shader.get();
  GPU_shader_bind(reduce);
  GPU_texture_image_bind(derived.presence.get(), 0);
  bool first = true;
  const auto stack_key = [&](int tile) {
    return set.id.toString().toStdString() + ":5:" + std::to_string(tile);
  };
  for (const auto &[tile, offset] : regions) {
    const auto &stack = s.stacks.at(stack_key(tile));
    GPU_texture_bind(stack.coverage.get(), 0);
    GPU_shader_uniform_1i(reduce, "first_tile", first);
    GPU_compute_dispatch(reduce, 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    first = false;
  }
  GPU_texture_image_unbind(derived.presence.get());
  const auto format = GPU_texture_format(target);
  const auto key = std::pair{format, GPU_texture_is_array(target)};
  if (!s.ao_shaders.contains(key))
    s.ao_shaders.emplace(
        key, Shader(gpu_derived::ao_shader(format, key.second, quant_source), GPU_shader_free));
  auto *shader = s.ao_shaders.at(key).get();
  if (!shader)
    return GpuPreviewUpdate::Unsupported;
  GPU_shader_bind(shader);
  GPU_texture_bind(derived.presence.get(), 2);
  GPU_texture_image_bind(target, 0);
  GPU_shader_uniform_1f(shader, "base_value", gpu_channel_base(set, 5)[0]);
  GPU_shader_uniform_1i(shader, "authored_enabled", set.ao_channel);
  GPU_shader_uniform_1i(shader, "replace_mode", set.ao_mixing == AoMixing::Replace);
  GPU_shader_uniform_1i(shader, "value_precision", int(set.precision[5]));
  for (const auto &[tile, offset] : regions) {
    if (output_tile && tile != output_tile)
      continue;
    auto &stack = s.stacks.at(stack_key(tile));
    GPU_texture_bind(stack.head, 0);
    GPU_texture_bind(bakes.at(tile), 1);
    GPU_texture_bind(derived.dirty_images.at(tile).get(), 3);
    const int size[2] = {set.width(tile), set.rows(tile)};
    GPU_shader_uniform_2iv(shader, "size", size);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  if (const auto found = s.margin_plans.find(set.material_session_uid);
      found != s.margin_plans.end())
    for (const auto &[tile, offset] : regions) {
      if (output_tile && tile != output_tile)
        continue;
      const auto &plan = (set.padding_3d ? found->second.adjacent : found->second.extend).at(tile);
      if (!s.margins.apply(stack_key(tile) + ":ao",
                           plan,
                           target,
                           offset,
                           dirty_update.flags.at(tile),
                           false,
                           derived.presence.get()))
        return GpuPreviewUpdate::Unsupported;
    }
  s.commit_derived(set, 5, target, regions, std::move(dirty_update));
  if (!output_tile && std::getenv("BTS_VERIFY_GPU_STACK") &&
      !s.margin_plans.contains(set.material_session_uid))
  {
    std::array<bool, 6> wanted{};
    wanted[5] = true;
    bool authored = false;
    for (int tile : set.tiles) {
      for (int y = 0; y < set.rows(tile) && !authored; ++y)
        for (int x = 0; x < set.width(tile) && !authored; ++x)
          authored = TextureDocument::evaluate_channels(set, wanted, x, y, tile).ao_coverage > 0;
    }
    auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
    double maximum = 0;
    bool valid = true;
    for (const auto &[tile, offset] : regions)
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
          const int px = int((x + .5) * set.width(tile) / 8),
                    py = int((y + .5) * set.rows(tile) / 8);
          const auto composite = TextureDocument::evaluate_channels(set, wanted, px, py, tile);
          const auto baked = sample_baked_ao(
              set, (px + .5) / set.width(tile), (py + .5) / set.rows(tile), tile);
          const float mixed = mix_ambient_occlusion(
              set, composite.material.ambient_occlusion, authored, baked);
          float expected = precision_rgb({mixed, mixed, mixed}, set.precision[5])[0];
          if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
            expected = math::half_to_float(math::float_to_half_make_finite(expected));
          const auto index = ((std::size_t(offset[2]) * GPU_texture_height(target) + py +
                               offset[1]) *
                                  GPU_texture_width(target) +
                              px + offset[0]) *
                             4;
          for (int c = 0; c < 4; ++c) {
            const double difference = std::abs(pixels[index + c] - (c == 3 ? 1 : expected));
            maximum = std::max(maximum, difference);
            const double tolerance = set.precision[5] == ChannelPrecision::L8 ? 1. / 255 + .001 :
                                     set.precision[5] == ChannelPrecision::L16 ?
                                                                                1. / 65535 + .001 :
                                     format == gpu::TextureFormat::SFLOAT_16_16_16_16 ? .001 :
                                                                                        1e-6;
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_AO_DIFFERENCE tile=%d x=%d y=%d precision=%d mode=%d authored=%d coverage=%.9g material=%.9g bake=%.9g expected=%.9g actual=%.9g\n",
                  tile,
                  px,
                  py,
                  int(set.precision[5]),
                  int(set.ao_mixing),
                  int(authored),
                  composite.ao_coverage,
                  composite.material.ambient_occlusion,
                  baked.value,
                  expected,
                  pixels[index + c]);
            valid &= std::isfinite(pixels[index + c]) && difference <= tolerance;
          }
        }
    MEM_delete(pixels);
    std::printf("BTS_GPU_STACK_VERIFIED channel=5 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU AO differs from CPU cross-tile reference");
  }
  return GpuPreviewUpdate::Updated;
}
GpuPreviewUpdate GpuLayerPreview::resolve_normal(const TextureSet &source_set,
                                                 gpu::Texture *target,
                                                 const std::map<int, std::array<int, 3>> &regions,
                                                 int output_tile,
                                                 bool without_height)
{
  auto set = native_normal_stack(source_set);
  if (!supported(set, 3))
    return GpuPreviewUpdate::Unsupported;
  auto &s = *impl_;
  bool pending = false;
  for (const auto &[tile, offset] : regions) {
    for (int c : {6, 4}) {
      if (c == 4 && (!set.channels[4] || without_height))
        continue;
      const auto updated = update_tile(set, c, target, tile, offset, true);
      if (updated == GpuPreviewUpdate::Unsupported)
        return updated;
      pending |= updated == GpuPreviewUpdate::Pending;
    }
  }
  if (pending)
    return GpuPreviewUpdate::Pending;
  if (without_height)
    set.channels[4] = false;
  performance::Scope timing("gpu_normal_resolve");
  const auto stack_key = [&](int channel, int tile) {
    return set.id.toString().toStdString() + ":" + std::to_string(channel) + ":" +
           std::to_string(tile);
  };
  std::map<int, gpu::Texture *> bakes;
  std::set<std::string> protected_keys;
  if (set.mesh_maps[0])
    for (const auto &[tile, plane] : set.mesh_maps[0]->tiles)
      if (plane.pixels)
        protected_keys.insert("baked:" + plane.pixels->hash + ":" +
                              std::to_string(plane.pixels->resolution) + ":" +
                              std::to_string(plane.pixels->height));
  for (const auto &[tile, offset] : regions) {
    const MeshMapPlane *plane = nullptr;
    if (set.mesh_maps[0])
      if (auto i = set.mesh_maps[0]->tiles.find(tile); i != set.mesh_maps[0]->tiles.end())
        plane = &i->second;
    bakes[tile] = s.baked(plane, protected_keys);
    if (!bakes[tile])
      return s.input_rejected ? GpuPreviewUpdate::Unsupported : GpuPreviewUpdate::Pending;
  }
  auto dirty_update = s.derived_changes(set, 3, target, regions, output_tile);
  if (!dirty_update.needed)
    return GpuPreviewUpdate::Updated;
  auto &derived = s.derived[s.derived_key(set, 3)];
  if (!s.prepare_derived_masks(derived, dirty_update, set))
    return GpuPreviewUpdate::Unsupported;
  if (!s.presence_shader)
    s.presence_shader = Shader(gpu_derived::presence_shader(), GPU_shader_free);
  if (!derived.presence)
    derived.presence = texture(
        "BTS cross-tile authored presence", 1, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
  if (!s.presence_shader || !derived.presence)
    return GpuPreviewUpdate::Unsupported;
  auto *reduce = s.presence_shader.get();
  GPU_shader_bind(reduce);
  GPU_texture_image_bind(derived.presence.get(), 0);
  bool first = true;
  for (const auto &[tile, offset] : regions) {
    GPU_texture_bind(s.stacks.at(stack_key(6, tile)).coverage.get(), 0);
    GPU_shader_uniform_1i(reduce, "first_tile", first);
    GPU_compute_dispatch(reduce, 1, 1, 1);
    GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
    first = false;
  }
  GPU_texture_image_unbind(derived.presence.get());
  const auto format = GPU_texture_format(target);
  const auto key = std::pair{format, GPU_texture_is_array(target)};
  if (!s.normal_shaders.contains(key))
    s.normal_shaders.emplace(
        key,
        Shader(gpu_derived::normal_shader(format, key.second, quant_source), GPU_shader_free));
  auto *shader = s.normal_shaders.at(key).get();
  if (!shader)
    return GpuPreviewUpdate::Unsupported;
  GPU_shader_bind(shader);
  GPU_texture_bind(derived.presence.get(), 2);
  GPU_texture_image_bind(target, 0);
  const auto basis = set.channels[3] ? set.base_values : MaterialValues{};
  const std::array<float, 3> base{
      basis.normal[0] * .5f + .5f, basis.normal[1] * .5f + .5f, basis.normal[2] * .5f + .5f};
  const auto quantized = precision_channel(basis, 3, set.precision[3]);
  GPU_shader_uniform_3fv(shader, "normal_base", base.data());
  GPU_shader_uniform_3fv(shader, "quantized_base", quantized.data());
  GPU_shader_uniform_1f(shader, "height_base", gpu_channel_base(set, 4)[0]);
  GPU_shader_uniform_1i(shader, "height_precision", int(set.precision[4]));
  GPU_shader_uniform_1i(shader, "height_enabled", set.channels[4]);
  GPU_shader_uniform_1i(shader, "authored_enabled", set.channels[3]);
  GPU_shader_uniform_1i(shader, "directx", set.normal_format == NormalMapFormat::DirectX);
  GPU_shader_uniform_1i(shader, "replace_mode", set.normal_mixing == NormalMixing::Replace);
  GPU_shader_uniform_1i(shader, "sobel", set.bump_method == BumpMethod::SmoothSobel);
  GPU_shader_uniform_1i(shader, "value_precision", int(set.precision[3]));
  for (const auto &[tile, offset] : regions) {
    if (output_tile && tile != output_tile)
      continue;
    std::array<std::array<float, 4>, 9> meta{};
    const int col = (tile - 1001) % 10, row = (tile - 1001) / 10;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int i = (dy + 1) * 3 + dx + 1, neighbor = 1001 + col + dx + 10 * (row + dy);
        const bool exists = col + dx >= 0 && col + dx < 10 && row + dy >= 0 && row + dy < 100 &&
                            set.tiles.contains(neighbor);
        const auto *height_stack = exists && set.channels[4] ?
                                       &s.stacks.at(stack_key(4, neighbor)) :
                                       nullptr;
        const bool padded = height_stack && bool(height_stack->height_image);
        meta[i] = {float(exists ? set.width(neighbor) : 1),
                   float(exists ? set.rows(neighbor) : 1),
                   float(exists),
                   float(padded)};
        GPU_texture_bind(height_stack ?
                             (padded ? height_stack->height_image.get() : height_stack->head) :
                             s.zero.get(),
                         4 + i);
      }
    auto &entry = s.normal_neighbors[stack_key(3, tile)];
    if (!entry.image)
      entry.image = texture(
          "BTS normal UDIM neighbors", 9, 1, gpu::TextureFormat::SFLOAT_32_32_32_32);
    if (!entry.image)
      return GpuPreviewUpdate::Unsupported;
    if (entry.values != meta) {
      GPU_texture_update(entry.image.get(), GPU_DATA_FLOAT, meta.data());
      entry.values = meta;
    }
    GPU_texture_bind(entry.image.get(), 3);
    GPU_texture_bind(s.stacks.at(stack_key(6, tile)).head, 0);
    GPU_texture_bind(bakes.at(tile), 1);
    GPU_texture_bind(derived.dirty_images.at(tile).get(), 13);
    const int size[2] = {set.width(tile), set.rows(tile)};
    GPU_shader_uniform_2iv(shader, "size", size);
    GPU_shader_uniform_3iv(shader, "target_offset", offset.data());
    GPU_compute_dispatch(shader, (size[0] + 15) / 16, (size[1] + 15) / 16, 1);
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  GPU_texture_unbind_all();
  GPU_texture_image_unbind_all();
  GPU_shader_unbind();
  if (const auto found = s.margin_plans.find(set.material_session_uid);
      found != s.margin_plans.end())
    for (const auto &[tile, offset] : regions) {
      if (output_tile && tile != output_tile)
        continue;
      const auto &plan = found->second.extend.at(tile);
      if (!s.margins.apply(stack_key(3, tile) + ":normal",
                           plan,
                           target,
                           offset,
                           dirty_update.flags.at(tile),
                           false,
                           derived.presence.get()))
        return GpuPreviewUpdate::Unsupported;
    }
  s.commit_derived(set, 3, target, regions, std::move(dirty_update));
  if (!output_tile && std::getenv("BTS_VERIFY_GPU_STACK") && testing_stack &&
      !s.margin_plans.contains(set.material_session_uid))
  {
    CompositeChannels channels{};
    channels[3] = true;
    std::atomic_bool cancel = false;
    const auto reference = native_composite_job(set, false, nullptr, 1, cancel, false, channels);
    if (!reference)
      throw std::runtime_error("Normal CPU oracle cancelled");
    auto *pixels = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
    double maximum = 0;
    bool valid = true;
    for (const auto &[tile, offset] : regions)
      for (int y = 0; y < set.rows(tile); ++y)
        for (int x = 0; x < set.width(tile); ++x) {
          const auto index = ((std::size_t(offset[2]) * GPU_texture_height(target) + y +
                               offset[1]) *
                                  GPU_texture_width(target) +
                              x + offset[0]) *
                             4;
          const auto local = (std::size_t(y) * set.width(tile) + x) * 4;
          for (int c = 0; c < 4; ++c) {
            float expected = reference->tiles.at(tile)[3][local + c];
            if (format == gpu::TextureFormat::SFLOAT_16_16_16_16)
              expected = math::half_to_float(math::float_to_half_make_finite(expected));
            const double difference = std::abs(pixels[index + c] - expected),
                         tolerance = (format == gpu::TextureFormat::SFLOAT_16_16_16_16 ||
                                      set.precision[3] == ChannelPrecision::RGB16F) ?
                                         .001 :
                                         2e-5;
            if (valid && difference > tolerance)
              std::fprintf(
                  stderr,
                  "BTS_GPU_NORMAL_DIFFERENCE tile=%d x=%d y=%d c=%d expected=%.9g actual=%.9g\n",
                  tile,
                  x,
                  y,
                  c,
                  expected,
                  pixels[index + c]);
            maximum = std::max(maximum, difference);
            valid &= std::isfinite(pixels[index + c]) && difference <= tolerance;
          }
        }
    MEM_delete(pixels);
    std::printf("BTS_GPU_STACK_VERIFIED channel=3 probes=64 max_error=%.9g valid=%d\n",
                maximum,
                int(valid));
    if (!valid)
      throw std::runtime_error("GPU normal differs from CPU derived reference");
  }
  return GpuPreviewUpdate::Updated;
}
#include "GpuLayerPreviewSelfTest.inc"

}  // namespace bts
