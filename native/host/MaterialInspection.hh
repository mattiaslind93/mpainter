#pragma once
#include "NativeMaterialMappingValidation.hh"
#include "MaterialValues.hh"
#include "NativeSourceEditBatch.hh"
#include "HistogramMetadata.hh"
#include "MaterialSample.hh"
#include "ThumbnailMetadata.hh"
#include "LayerCapture.hh"
#include "NativeMaterialPreparationStatus.hh"
#include "CompositeChannels.hh"
#include "PaintPixels.hh"
#include <cstdint>
#include <atomic>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <span>
namespace blender { struct Main; }
namespace bts {
struct AdjustmentPaintSnapshot;
class TexturePadding;
class NativeMaterialObjectTable;
class GpuProjectReadback;
struct TextureSet;struct NativeMaterialGraph;struct NativeMaterialDependencies;struct NativeMaterialGpuContext;
struct TextureLayer;
struct MaterialParameterMaskCapture;
class NativeMaterialHeightResource;class NativeMaterialHeightGpu;
enum class NativeHeightPolicy;
struct FillTexture;
struct MeshMap;
class TextureDocument;
struct SceneSnapshot;class NativeFragmentGeometrySource;struct NativeFragmentGeometryRequest;
enum class InspectionMode : int { Material=-1, BaseColor=0, Metallic=1, Roughness=2, Normal=3, Bump=4, Mask=5, AmbientOcclusion=6, Displacement=15 };
struct InspectionMaterial {
  std::uint32_t material_uid;
  MaterialValues authored;
  std::array<float,5> coverage;
  const TextureSet *mask_set=nullptr;
  int resolution=0;
  const MeshMap *mesh_map=nullptr;
  bool mesh_inspection=false;
};
// Derived shader presentation only. Original Principled inputs and authored layer data stay intact.
// Main owns the generated nodes; reset bindings before Main IDs are retired.
class MaterialInspection {
 public:
  MaterialInspection();
  void publish_native_material_preview(const std::string &,const std::string &);
  void prepare_native_material(std::string,std::string,std::shared_ptr<NativeMaterialGraph>);
  void cancel_native_preparation(const std::string &);
  void discard_native_candidate(const std::string &);
  void alias_native_material(const std::string &,const std::string &);
  void validate_native_mapping_edit(const TextureSet &before,const TextureSet &after,NativeMappingEditOrigin=NativeMappingEditOrigin::Authoring)const;
  // Read-only authoring admission on the live owner Main, never Open/draw.
  // Model/numeric eligibility is a separate caller preflight, including Enable.
  void validate_material_layering(const TextureSet &)const;
  bool has_native_material(const std::string &)const;
  bool native_material_uses_object(const std::string &)const;
  bool native_material_uses_ao(const std::string &)const;
  NativeMaterialDependencies native_requirements(const std::string &,const NativeMaterialDescription &)const;
  void prepare_native_height(const std::string &,NativeMaterialHeightResource);
  void stage_native_height(NativeMaterialHeightGpu &,NativeMaterialHeightResource);
  bool step_native_height(NativeMaterialHeightGpu &);
  void install_native_height(NativeMaterialHeightGpu &);
  bool set_native_height_policy(TextureSet &,const QUuid &,NativeHeightPolicy)const;
  void probe_native_height(const std::string &,const NativeMaterialDescription &,NativeMaterialGpuContext);
  void step_native_preparations();
  // Owner-stage source upload/binding only, no composite/readback/whole-frame work.
  bool prepare_material_layer_sources(bool allow_upload=true);
  // Pending-Open only: every declared source must have an exact prepared owner binding.
  void validate_material_layer_sources(const TextureDocument &,bool require_ready=false)const;
  void refresh_native_view();
  NativeMaterialPreparationStatus native_preparation_status(const std::string &)const;
  void install_native_material(std::string,std::string,std::shared_ptr<NativeMaterialGraph>);
  void set_native_context_provider(std::function<void(const TextureSet &,const TextureLayer &,int,const NativeMaterialDependencies &,NativeMaterialGpuContext &)>);
  using NativeFragmentGeometryProvider=std::function<std::shared_ptr<const NativeFragmentGeometrySource>(NativeFragmentGeometryRequest,const std::atomic_bool &)>;
  void set_native_fragment_geometry_provider(NativeFragmentGeometryProvider);
  using NativeFragmentObjectProvider=std::function<std::shared_ptr<const NativeMaterialObjectTable>(std::uint32_t)>;
  void set_native_fragment_object_provider(NativeFragmentObjectProvider);
  // GPU owner calls after latched evaluated scene updates. Reads small owner
  // properties only; a changed stamp or geometry tag schedules the existing
  // capture path. Geometry tags invalidate even when owner properties match.
  void refresh_native_world_objects(bool owner_revision,bool geometry_revision=false);
  void initialize_gpu_programs(); // GPU owner; opt-in large programs can remain pending.
  void poll_gpu_programs(); // Nonblocking pair/retired drain; never full begin_frame.
  bool gpu_programs_pending()const;
  ~MaterialInspection();
  bool update(blender::Main &, InspectionMode, std::span<const InspectionMaterial>);
  void reset(bool terminal=false);
  bool set_geometry_visibility(blender::Main &,const TextureSet *);
  bool geometry_visibility_enabled(std::uint32_t) const;
  void collect_retired_visibility_images(blender::Main &);
  std::size_t geometry_visibility_image_count() const;
  void pack_images();
  void capture_project_images(GpuProjectReadback &);
  std::vector<MaterialParameterMaskCapture> capture_material_layer_masks(const TextureSet &,GpuProjectReadback &);
  void begin_project_source_capture();
  bool capture_project_source(const std::string &,const PaintPixels &,int channel,int tile,int x,int y,int width,int height,GpuProjectReadback &);
  void restore(blender::Main &,const TextureDocument &);
  // Accepted project commit only: both pimpl addresses remain stable and their
  // reference-bound native preparation queues move with them, never by value.
  // Caller drains old state before its Main is retired and prebuilds new state.
  void swap_prepared(MaterialInspection &)noexcept;
  bool has_pixels(std::uint32_t material_uid) const;
  bool has_padding(std::uint32_t material_uid) const;
  std::shared_ptr<TexturePadding> export_padding(std::uint32_t material_uid) const;
  void set_padding_geometry(blender::Main &,std::uint32_t,const SceneSnapshot &);
  std::vector<unsigned char> uv_coverage(std::uint32_t,int tile) const;
  // 0..4 complete material channels; 5 AO; 6 authored normals; 7..14 user slots; 15 selected mask; 16 displacement.
  std::vector<float> copy_pixels(std::uint32_t,int channel,int tile=1001) const;
  std::uint64_t image_hash(std::uint32_t,int channel,int tile=1001) const;
  // Verification only: synchronous GPU probes; never an interactive read path.
  std::vector<std::array<float,4>> diagnostic_gpu_pixels(std::uint32_t,int channel,std::span<const std::array<int,2>>,int tile=1001) const;
  std::optional<std::array<float,4>> diagnostic_gpu_pixel(std::uint32_t,int channel,int x,int y,int tile=1001) const;
  std::array<float,4> sample_pixel(std::uint32_t,int channel,int x,int y,int tile=1001) const;
  double verify_gpu_pixels(std::uint32_t material_uid) const; // Explicit diagnostic readback + CPU oracle.
  // Returns true when native shader topology was first installed. Bounds empty means full update.
  bool update_pixels(blender::Main &,const TextureSet &,int resolution,PaintPixels::Bounds bounds={},bool interactive=false,bool allocate_only=false);
  bool prepare_texture_resource(const std::shared_ptr<const FillTexture> &);
  bool prepare_texture_resources(const std::vector<std::shared_ptr<const FillTexture>> &);
  void cancel_texture_preparation();
  // Owner bookkeeping only: no GPU context entry. Completed legacy uploads may
  // yield this slot; active uploads and another token always retain ownership.
  bool claim_texture_preparation(const QUuid &);
  bool prepare_texture_resources(const QUuid &,const std::vector<std::shared_ptr<const FillTexture>> &);
  bool cancel_texture_preparation(const QUuid &);
  void retire_layer_channel(const TextureDocument &,const TextureSet &,const TextureLayer &,int);
  void discard_binding(std::uint32_t);
  void queue_composite(const TextureSet &);
  bool queue_layer_edit(const TextureSet &,CompositeChannels,bool finalize=true);
  NativeSourceEditBatch prepare_native_source_edits(std::span<const NativeSourceEdit>);
  QJsonObject diagnostic_native_source_queues()const;
  QJsonObject native_world_normal_status(std::uint32_t)const;
  QJsonArray diagnostic_native_world_normal_samples(std::uint32_t,bool edge=false)const;
  void upload_staging_images(bool allow_gpu_compute=true,bool input_active=false);
  bool sync_cycles_images(blender::Main &,bool active);
  bool cycles_images_pending()const;
  std::string_view cycles_images_error()const;
  void finalize_gpu_images(blender::Main &,bool allow_gpu_compute=true); // GPU owner context only.
  void diagnostic_adjustment_budget(std::optional<std::uint64_t>);
  std::shared_ptr<const AdjustmentPaintSnapshot> begin_adjustment_source(const TextureSet &,const QUuid &layer,const QUuid &document={});
  TextureSet prepare_layer_capture(const TextureSet &)const;
  std::uint64_t request_layer_capture(const TextureSet &,const LayerCaptureRequest &);
  std::optional<LayerCaptureResult> layer_capture_result() const;
  std::uint64_t request_thumbnail(const TextureSet &,const ThumbnailRequest &);
  std::optional<ThumbnailMetadata> thumbnail_result() const;
  std::uint64_t request_material_sample(const TextureSet &,const MaterialSampleRequest &);
  std::optional<MaterialSample> material_sample_result() const;
  std::uint64_t request_histogram(const TextureSet &,const HistogramRequest &);
  std::optional<HistogramMetadata> histogram_result(const QUuid &) const;
  bool updates_pending(bool include_preparation=true) const;
  bool take_displacement_updates();
  bool poll_composites(blender::Main &,bool wait=false);
  void resume_gpu_channels(); // GPU owner, after EEVEE has bound visible images.
  bool displacement_content(std::uint32_t) const;
  bool shader_parameters(blender::Main &,const TextureSet &);
  // Explicit diagnostic oracle: applying identical native parameters must be a no-op.
  bool openpbr_reapply_is_stable(blender::Main &,std::uint32_t);
  bool set_subsurface_enabled(blender::Main &,bool);
  bool preview_displacement(blender::Main &,const TextureSet &);
  bool preview_uniform(blender::Main &,const TextureSet &,int channel);
  bool preview_roughness(blender::Main &main,const TextureSet &set){return preview_uniform(main,set,2);}
  bool preview_color(blender::Main &main,const TextureSet &set){return preview_uniform(main,set,0);}
  void prepare_pixels(blender::Main &,const TextureSet &);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
