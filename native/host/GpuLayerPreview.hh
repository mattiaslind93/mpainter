#pragma once
#include "TextureDocument.hh"
#include "NativeMaterialHeightPolicy.hh"
#include "CompositeChannels.hh"
#include "GpuMaskComparison.hh"
#include "HistogramMetadata.hh"
#include "MaterialSample.hh"
#include "ThumbnailMetadata.hh"
#include "LayerCapture.hh"
#include "AdjustmentPaintSource.hh"
#include "Performance.hh"
#include "GpuTextureResource.hh"
#include "NativeMaterialPreparationStatus.hh"
#include "GPU_texture.hh"
#include <memory>
#include <functional>
#include <span>
#include <vector>
namespace bts {
struct NativeBakedAoLease;struct NativeMaterialViewContext;struct SmudgeCapture;struct GpuMarginPlan;struct NativeMaterialGraph;struct NativeMaterialGpuContext;struct NativeMaterialDependencies;
class NativeMaterialHeightResource;class NativeMaterialHeightGpu;
class GpuProjectReadback;
enum class GpuPreviewUpdate {Unsupported,Pending,Updated};
// Interactive GPU evaluation and padding over immutable authored document pages.
// CPU evaluation is reserved for explicit diagnostics/export or logged fallback.
// GPU methods run exclusively on Blender's owner context; uploads are bounded.
class NativeMaterialGpuLibrary;
class GpuLayerPreview {
 public:
  GpuLayerPreview();
  ~GpuLayerPreview();
  // Ordinary calls only poll. Terminal owner shutdown can cancel/wait before GPU_exit.
  static void collect_retired(bool terminal=false);
  void poll_programs(); // Pair and retired handles only; no other startup warmup.
  bool programs_pending()const; // Includes retired handles that still require owner polling.
  std::string take_program_error(); // One owner-thread status delivery.
  static bool supported(const TextureSet &,int channel);
  void begin_frame();
  void publish_native_material_preview(const std::string &,const std::string &);
  void prepare_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph>);
  void cancel_native_preparation(const std::string &);
  void discard_native_candidate(const std::string &);
  void alias_native_material(const std::string &,const std::string &);
  void validate_native_mapping_instances(const TextureSet &,const TextureSet *before=nullptr)const;
  bool has_native_material(const std::string &)const;
  bool native_material_uses_object(const std::string &)const;
  bool native_material_uses_ao(const std::string &)const;
  NativeMaterialGpuLibrary &native_world_sources();
  // GPU-only associated WORLD publication, separate from all artist channels.
  std::string world_normal_mask_signature(const TextureSet &,int,std::span<const QUuid>,std::uint64_t dependent_epoch)const;
  GpuPreviewUpdate prepare_world_normal_weight(const TextureSet &,int tile,float opacity,std::span<const QUuid> masks,bool array_target);
  GpuPreviewUpdate pad_world_normal(const TextureSet &,int,blender::gpu::Texture *,int side);
  GpuPreviewUpdate publish_world_normal(const TextureSet &,int tile,
    blender::gpu::Texture *source,blender::gpu::Texture *destination,
    std::array<int,3> offset,bool apply_weight=true);

  NativeMaterialDependencies native_requirements(const std::string &,const NativeMaterialDescription &)const;
  void prepare_native_height(const std::string &,NativeMaterialHeightResource);
  void stage_native_height(NativeMaterialHeightGpu &,NativeMaterialHeightResource);
  bool step_native_height(NativeMaterialHeightGpu &);
  void install_native_height(NativeMaterialHeightGpu &);
  bool set_native_height_policy(TextureSet &,const QUuid &,NativeHeightPolicy)const;
  void probe_native_height(const std::string &,const NativeMaterialDescription &,NativeMaterialGpuContext);
  CompositeChannels native_view_channels(const TextureSet &)const;
  void step_native_preparations();
  bool native_preparations_pending()const;
  NativeMaterialPreparationStatus native_preparation_status(const std::string &)const;
  std::string native_image_error()const;
  void install_native_material(std::string resource,std::string content,std::shared_ptr<NativeMaterialGraph>);
  using NativeContextProvider=std::function<void(const TextureSet &,const TextureLayer &,int,const NativeMaterialDependencies &,NativeMaterialGpuContext &)>;
  void set_native_context_provider(NativeContextProvider);
  // Uses the existing baked entry, pinning its allocation before bounded upload.
  GpuPreviewUpdate prepare_baked_resource(int tile,const MeshMapPlane &,NativeBakedAoLease &);
  NativeMaterialViewContext native_world_view(const TextureSet &,const TextureLayer &,int tile)const;
  TextureSet prepare_layer_capture(const TextureSet &)const;
  std::shared_ptr<const AdjustmentPaintSnapshot> begin_adjustment_source(const TextureSet &,const QUuid &layer,const QUuid &document={});
  std::array<std::uint64_t,3> native_statistics()const; // Sources/programs/dirty-region evaluations.
  void diagnostic_adjustment_budget(std::optional<std::uint64_t>);
  std::array<std::uint64_t,2> adjustment_copy_statistics()const; // Full-image COW copies/texels.
  // Metadata-only diagnostics; never read GPU pixels or evaluate content.
  std::uint64_t texture_resource_allocations()const;
  std::uint64_t content_node_revision(const TextureSet &,int channel,int tile,const std::string &node)const;
  // Borrowed float stack before output quantization; explicit diagnostic callers
  // may read it. Invalidated by any cache update/reset; never performs readback.
  blender::gpu::Texture *diagnostic_composite_texture(const TextureSet &,int channel,int tile)const;

  GpuPreviewUpdate capture_author_chunk(const std::string &,const PaintPixels &,int channel,int tile,int x,int y,int width,int height,GpuProjectReadback &);
  bool prefetch_geometry(const std::map<int,std::shared_ptr<const FillSurfaceAtlas>> &);
  void set_padding_geometry(std::uint32_t uid,const std::map<int,std::shared_ptr<const GpuMarginPlan>> &extend,
                            const std::map<int,std::shared_ptr<const GpuMarginPlan>> &adjacent);
  void clear_native_normals(const QUuid &set);
  void set_native_normal_error(const QUuid &set,std::string);
  void set_native_normal(const QUuid &set,const QUuid &layer,int tile,std::shared_ptr<blender::gpu::Texture>);
  TextureSet native_normal_stack(const TextureSet &)const;
  bool uniform(blender::gpu::Texture *,const std::array<float,4> &);
  GpuPreviewUpdate update(const TextureSet &,int channel,blender::gpu::Texture *target,int tile=1001,std::array<int,3> target_offset={});
  // Evaluate all prefixes before resolving derived channels with cross-tile
  // dependencies. Target regions can share a native UDIM atlas array layer.
  GpuPreviewUpdate update_tiles(const TextureSet &,int channel,blender::gpu::Texture *target,const std::map<int,std::array<int,3>> &regions);
  struct DisplacementSummary {std::uint32_t uid;float minimum,maximum;bool present;std::uint64_t epoch;};
  void set_displacement_epoch(std::uint32_t uid,std::uint64_t epoch);
  GpuPreviewUpdate histogram(const TextureSet &,const HistogramRequest &,std::uint64_t ticket);
  std::optional<HistogramMetadata> histogram_result(const QUuid &) const;
  GpuPreviewUpdate sample_material(const TextureSet &,const MaterialSampleRequest &,std::uint64_t ticket);
  std::optional<MaterialSample> material_sample_result() const;
  GpuPreviewUpdate capture_layer(const TextureSet &,const LayerCaptureRequest &,std::uint64_t ticket);
  std::optional<LayerCaptureResult> layer_capture_result() const;
  GpuPreviewUpdate thumbnail(const TextureSet &,const ThumbnailRequest &,std::uint64_t ticket);
  std::optional<ThumbnailMetadata> thumbnail_result() const;
  bool texture_resource_ready(const FillTexture &) const;
  // No upload/allocation. A successful lease prevents source eviction until
  // its owner has detached all native Image aliases on the GPU context.
  std::optional<GpuTextureResource> retain_texture_resource(const FillTexture &) const;
  GpuPreviewUpdate prepare_texture_resource(const FillTexture &);
  GpuPreviewUpdate prepare_texture_resources(std::span<const FillTexture *const>);
  bool metadata_pending() const;
  void retire_layer_channel(const TextureDocument &,const TextureSet &,const TextureLayer &,int,
                            std::span<const std::shared_ptr<const FillTexture>> pending={});
  std::vector<DisplacementSummary> displacement_summaries();
  bool wrote_image() const;
  void diagnose_pixel(const TextureSet &,int channel,int tile,int x,int y);
  // Ordinary reset preserves immutable adjustment histories; shutdown retires all GPU owners.
  void reset(bool terminal=false);
  void evict_residency();
  std::uint64_t release_bake_scratch();
 private:
  static void self_test();
  GpuPreviewUpdate update_tile(const TextureSet &,int,blender::gpu::Texture *,int,std::array<int,3>,bool prefix_only=false,const QUuid *query_layer=nullptr,bool query_source=false,const QUuid *query_effect=nullptr,bool thumbnail=false,bool raw_anchor=false,bool authored_ao_output=false);
  std::optional<GpuComparisonInputs> comparison_inputs(const TextureSet &,const GpuMaskStage &,blender::gpu::Texture *,int);
  blender::gpu::Texture *mask_texture(const TextureSet &,const QUuid &,int,const QUuid *point=nullptr);
  std::optional<GpuAnchorInput> anchor_input(const TextureSet &,const QUuid &,const AnchorReference &,int);
  GpuPreviewUpdate resolve_mask(const TextureSet &,blender::gpu::Texture *,int,std::array<int,3>);
  GpuPreviewUpdate resolve_normal(const TextureSet &,blender::gpu::Texture *,const std::map<int,std::array<int,3>> &,int output_tile=0,bool without_height=false);
  GpuPreviewUpdate update_tiles_impl(const TextureSet &,int,blender::gpu::Texture *,const std::map<int,std::array<int,3>> &,int output_tile);
  blender::gpu::Texture *adjustment_page(const SmudgeCapture &,int,int,std::uint32_t,blender::gpu::Texture *,performance::Clock::time_point);
  void bind_adjustment_resolver();
  struct Impl;
  static std::vector<std::unique_ptr<Impl>> &retired();
  std::unique_ptr<Impl> impl_;
};
}
