#include "NativeMaterialPreparationStatus.hh"
#include "NativeMaterialPreview.hh"
#include "BakeSource.hh"
#pragma once
#include "ProjectFileSafety.hh"
#include "NativeMaterialMappingValidation.hh"

#include "LensProjection.hh"
#include "SceneSnapshot.hh"
#include "SurfaceBrush.hh"
#include "Symmetry.hh"
#include "AssetCatalog.hh"
#include "BlendMaterial.hh"
#include "BlendAssetLibrary.hh"
#include <future>
#include "MeshImport.hh"
#include "MeshExport.hh"
#include "MeshReplacement.hh"
#include "ProjectSettings.hh"
#include "MaterialInspection.hh"
#include "TextureExport.hh"
#include <QJsonObject>
#include "UvPreview.hh"
#include "ViewLayout.hh"
#include "EeveeCompletionSnapshot.hh"
#include "SurfaceGraphSnapshot.hh"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

class QOpenGLWidget;
class QOpenGLContext;
class QSurface;

namespace bts {
struct AdjustmentPaintSnapshot;

struct ViewFrame {
  ViewPane pane;
  std::array<float, 16> view;
  std::array<float, 16> projection;
  std::array<double,2> clipping{};
  bool wireframe=false,wireframe_triangles=false;
  const SceneSnapshot *wire_source=nullptr;
  float overlay_line_width=1;
  std::array<double,2> uv_center{.5,.5};
  double uv_extent=1;
  std::set<int> hidden_uv_tiles;
};

struct PendingProject;
class PendingProjectGpu;class NativeMaterialHeightGpu;
class BlenderHost {
 public:
  BlenderHost(const std::string &executable, const std::string &project);
  ~BlenderHost();
  BlenderHost(const BlenderHost &) = delete;
  BlenderHost &operator=(const BlenderHost &) = delete;

  // One-time initialization of an empty factory workspace, before GPU/workers.
  void initialize_empty_workspace_working_space();
  void with_gpu(const std::function<void()> &);
  void initialize_gpu(QOpenGLWidget &surface);
  void initialize_gpu(QOpenGLContext &,QSurface &,unsigned framebuffer);
  void initialize_gpu(QOpenGLContext &,QSurface &,std::function<unsigned()> framebuffer);
  void set_subsurface_preview(const QJsonObject &);
  void edit_subsurface_preview(const QString &,double);
  void set_render_preview(bool enabled,bool force_cpu=false);
  void set_render_quality(int samples,bool denoise);
  QJsonObject render_state() const;
  void diagnostic_cycles_cuda_render(const QString &directory);
  // Owner-only copy of the most recent draw, without querying live engine data.
  EeveeCompletionSnapshot eevee_completion_snapshot() const;
  // Test-only copied graph values; capture opt-in must precede Host creation.
  SurfaceGraphSnapshot surface_graph_snapshot() const;
  // Test-only symmetric cold-view isolation. Enters its own owner GPU scope;
  // call idle, never from inside with_gpu/draw. Native scene and offscreen stay.
  std::uint64_t diagnostic_recreate_scene_viewport();
  double draw(std::span<const ViewFrame> frames, int width, int height, bool defer_material_render=false);
  void set_selected_material(std::optional<std::uint32_t> session_uid);
  std::optional<std::uint32_t> selected_material() const;
  UvPreviewSummary uv_preview_summary() const;
  void set_roughness(float roughness);
  SceneSnapshot scene_snapshot();
  QJsonObject overlay_profile() const;
  bool mesh_export_displacement_available(const TextureDocument &)const;
  std::shared_ptr<NativeMeshExport> prepare_mesh_export(const TextureDocument &,bool displaced=false,bool recompute_normals=true);
  SceneSnapshot load_bake_source(const std::string &path,const BakeSourceOptions &options={});
  std::optional<SurfaceBrushSample> surface_stamp(std::uint32_t selected,
      const std::array<float,3> &origin,const std::array<float,3> &direction,float diameter_percent,bool coverage=false,bool square=false,const BrushProjection &projection={});
  std::vector<unsigned char> texture_coverage(std::uint32_t,int tile=1001) const;
  std::vector<ExportTile> export_source(const TextureDocument &,const std::vector<QUuid> &);
  bool has_texture_pixels(std::uint32_t) const;
  std::vector<float> texture_pixels(std::uint32_t,int channel,int tile=1001) const;
  std::uint64_t texture_hash(std::uint32_t,int channel,int tile=1001) const;
  double verify_gpu_texture_stack(std::uint32_t) const;
  // Verification only: synchronous GPU probes; never an interactive read path.
  std::vector<std::array<float,4>> diagnostic_gpu_pixels(std::uint32_t,int channel,std::span<const std::array<int,2>>,int tile=1001) const;
  std::optional<std::array<float,4>> diagnostic_gpu_pixel(std::uint32_t,int channel,int x,int y,int tile=1001) const;
  std::array<float,4> sample_texture(std::uint32_t,int channel,int x,int y,int tile=1001) const;
  void prepare_warp_projections(const TextureSet &);
  void apply_texture_set(const TextureSet &,int resolution,PaintPixels::Bounds bounds={},bool interactive=false);
  void preview_displacement(const TextureSet &);
  QJsonObject displacement_preview_state();
  bool preview_layer_edit(const TextureSet &,CompositeChannels,bool finalize=true);
  NativeSourceEditBatch prepare_native_source_edits(std::span<const NativeSourceEdit>);
  QJsonObject diagnostic_native_source_queues()const;
  QJsonObject native_world_normal_status(std::uint32_t)const;
  QJsonArray diagnostic_native_world_normal_samples(std::uint32_t,bool edge=false);
  void preview_material_roughness(const TextureSet &);
  void set_shader_parameters(const TextureSet &);
  QJsonObject shader_parameter_state() const;
  void preview_material_color(const TextureSet &);
  void prepare_paint_texture(const TextureSet &);
  void finish_texture_set(const TextureSet &);
  void flush_texture_updates();
  // Owner-thread capability snapshot; reads prepared metadata, never GPU pixels.
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
  bool prepare_texture_resource(const std::shared_ptr<const FillTexture> &);
  bool prepare_texture_resources(const std::vector<std::shared_ptr<const FillTexture>> &);
  std::map<std::uint32_t,QJsonObject> deferred_imported_materials()const;
  void adopt_imported_material(const TextureSet &,float bump_distance,const SceneSnapshot &);
  void cancel_texture_preparation();
  // Owner bookkeeping only: no GPU context entry. Completed legacy uploads may
  // yield this slot; active uploads and another token always retain ownership.
  bool claim_texture_preparation(const QUuid &);
  bool prepare_texture_resources(const QUuid &,const std::vector<std::shared_ptr<const FillTexture>> &);
  bool cancel_texture_preparation(const QUuid &);
  bool texture_updates_pending() const;
  void set_texture_interaction(bool);
  bool viewport_redraw_pending() const;
  QString viewport_texture_status() const;
  void set_material_values(std::uint32_t session_uid, const MaterialValues &values);
  void set_material_roughness(std::uint32_t session_uid, float roughness);
  MeshReplacement prepare_reimport(const TextureDocument &,const std::string &path,bool preserve_world=true,std::optional<bool> import_cameras=std::nullopt,std::optional<AutoUnwrapOptions> auto_unwrap=std::nullopt,std::optional<UsdImportOptions> usd=std::nullopt,std::optional<UnitSettings> units=std::nullopt,std::optional<BlendMeshSelection> blend=std::nullopt);
  void rebind_texture_sets(const TextureDocument &);
  void commit_reimport(TextureDocument &,MeshReplacement &);
  std::vector<std::string> apply_imported_shader_profiles(TextureDocument &);
  void retire_layer_channel(const TextureDocument &,const TextureSet &,const TextureLayer &,int);
  std::string rename_material(std::uint32_t uid,const std::string &name);
  std::shared_future<NativeMaterialPreviewResult> preview_native_material(const QString &,const QString &,std::shared_ptr<std::atomic_bool>);
  void cancel_native_material_preview();
  bool native_material_preview_pending()const;
  void publish_native_material_preview(const QUuid &,const QString &);
  void prepare_native_material_gpu(const QUuid &,const QString &,std::shared_ptr<NativeMaterialGraph>);
  void cancel_native_material_preparation(const QUuid &);
  // Only a fresh unpublished staging UUID owned by the caller may be discarded.
  void discard_native_material_candidate(const QUuid &);
  void alias_native_material(const QUuid &,const QUuid &);
  bool has_native_material(const QUuid &)const;
  void validate_native_mapping_edit(const TextureSet &before,const TextureSet &after,NativeMappingEditOrigin=NativeMappingEditOrigin::Authoring)const;
  // Explicit authoring only. The caller must also apply the uniform/model
  // eligibility gate; this checks the live binding's restorable native graph.
  void validate_material_layering(const TextureSet &)const;
  void validate_material_parameter_import(const TextureSet &)const;
  // Explicit parameter export only: three exact mask images, no CPU compositor.
  void begin_material_parameter_export(const TextureSet &,const std::vector<StudioAsset> &,const QString &directory);
  bool material_parameter_export_pending()const;
  bool poll_material_parameter_export(bool current);
  void cancel_material_parameter_export();
  void validate_native_material_target(const QUuid &,const TextureSet &)const;
  NativeMaterialPreparationStatus native_material_status(const QUuid &)const;
  void install_native_material(const QUuid &,const QString &,std::shared_ptr<NativeMaterialGraph>);
  std::shared_future<std::shared_ptr<NativeMaterialGraph>> prepare_material_graph(const QString &,const QString &,std::shared_ptr<std::atomic_bool>);
  std::shared_future<NativeMaterialDescription> inspect_material_graph(const QString &,const QString &,std::shared_ptr<std::atomic_bool>);
  std::shared_future<std::shared_ptr<NativeMaterialGraph>> prepare_material_resource(const StudioAsset &,std::shared_ptr<std::atomic_bool>,std::vector<StudioAsset> textures={});
  std::shared_future<PreparedBlendMaterial> import_native_material(const QString &,const QString &,std::shared_ptr<std::atomic_bool>);
  std::shared_future<PreparedBlendMaterial> import_blend_material(const QString &path,const QString &name,std::shared_ptr<std::atomic_bool> cancellation,bool preview_only=false);
  std::shared_future<BlendAssetLibrary> inspect_asset_library(const QString &,std::shared_ptr<std::stop_source>,std::function<void(const QString &)> progress);
  ImportSummary new_project(const std::string &mesh_path, const std::string &hdr_path,const NewProjectOptions *options=nullptr);
  void begin_mask_preset(const LayerMask &);
  std::optional<QByteArray> poll_mask_preset();
  void begin_material_preset(const TextureSet &,const QUuid &folder);
  std::optional<QByteArray> poll_material_preset();
  void begin_save_project(const TextureDocument &,const std::string &path,const std::vector<StudioAsset> &resources={},std::function<void()> persist_metadata={},std::optional<ProjectFileIdentity> expected={});
  ProjectFileIdentity saved_project_identity() const;
  bool poll_save_project(); // Owner context, never waits; throws a completed writer error.
  bool save_project_pending() const;
  void save_project(const TextureDocument &,const std::string &path,const std::vector<StudioAsset> &resources={},bool recovery_copy=false,std::optional<ProjectFileIdentity> expected={});
  std::vector<StudioAsset> project_resources() const;
  TextureDocument adopt_project(std::unique_ptr<PendingProject>,const std::string &path);
  std::unique_ptr<PendingProjectGpu> begin_project_gpu(PendingProject &);
  void validate_project_adoption(const PendingProject &,const PendingProjectGpu *)const;
  void prepare_native_height(const QUuid &,NativeMaterialHeightResource);
  std::unique_ptr<NativeMaterialHeightGpu> stage_native_height_gpu(const QUuid &,NativeMaterialHeightResource);
  bool step_native_height_gpu(NativeMaterialHeightGpu &);
  void install_native_height_gpu(NativeMaterialHeightGpu &);
  void probe_native_height(const TextureSet &,const QUuid &);
  void discard_native_height_gpu(NativeMaterialHeightGpu &);
  bool set_native_height_policy(TextureSet &,const QUuid &,NativeHeightPolicy)const;
  bool step_project_gpu(PendingProject &,PendingProjectGpu &);
  bool project_gpu_available()const;
  void discard_project_gpu(PendingProjectGpu &);
  TextureDocument adopt_project(std::unique_ptr<PendingProject>,const std::string &path,std::unique_ptr<PendingProjectGpu>);
  // Batch-only synchronous bridge; rejects an active owner event loop.
  TextureDocument open_project(const std::string &path,std::function<void(const std::vector<StudioAsset> &)> validate_resources={});
  QJsonObject project_profile() const;
  SymmetrySettings symmetry_settings() const;
  void set_symmetry(const SymmetrySettings &);
  std::vector<std::string> display_transforms() const;
  QJsonObject color_state() const;
  void display_rgba(std::span<float> rgba) const;
  void set_inspection(InspectionMode, std::span<const InspectionMaterial>);
  InspectionMode inspection_mode() const;
  // UV-private view state only; never alters the live 3D material shader.
  void set_material_layering_view(std::uint32_t material_uid,bool enabled,int active_mask);
  void set_geometry_visibility(const TextureSet *);
  bool geometry_visibility_enabled(std::uint32_t) const;
  std::size_t geometry_visibility_image_count() const;
  QJsonObject environment_state() const;
  LensProjection lens_projection() const;
  void set_display_post_effects(const QJsonObject &);
  void edit_display_post_effect(const QString &,double);
  QJsonObject tangent_shader_state() const;
  std::string shader_failures() const;
  void set_environment_rotation(double degrees);
  void set_environment_alignment(bool world);
  void set_environment_resource(const std::string &path);
  void set_environment_appearance(const std::string &field,double value);
  void set_display_transform(const std::string &name);
  void shutdown();
  std::string renderer() const;

 private:
  void initialize_gpu_surface();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace bts
