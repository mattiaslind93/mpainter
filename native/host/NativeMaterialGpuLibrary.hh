#pragma once
#include "NativeMaterialGpuProgram.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialDependencies.hh"
#include "NativeMaterialHeightResource.hh"
#include "NativeMaterialFragmentGpuCompiler.hh"
#include "NativeFragmentImageReadiness.hh"
#include "NativeMaterialTiledStorage.hh"
#include <map>
#include <set>
#include "ShaderTreeCache.hh"
#include "ColdImageCache.hh"
#include <string>
namespace bts {
class NativeMaterialImageUpload;
enum class NativeWorldNormalContextState {Unsupported,PendingGenerated,PendingView,PendingBakedAo,PendingImages,Ready};
// GPU-owner registry. Instances share one source Main, native image IDs and
// compiled channel programs; only their small authored parameter values differ.
class NativeMaterialGpuLibrary {
 private:
 struct Entry;struct HeightState;
 public:
 class HeightCandidate {
  public:~HeightCandidate();
  private:friend class NativeMaterialGpuLibrary;HeightCandidate()=default;std::shared_ptr<Entry> source_;std::unique_ptr<HeightState> value_;std::unique_ptr<NativeMaterialImageUpload> upload_;
 };
 class HeightChange {
  public:~HeightChange();void commit()noexcept{committed_=true;}
  private:friend class NativeMaterialGpuLibrary;HeightChange()=default;std::shared_ptr<Entry> source_;std::unique_ptr<HeightState> previous_;bool changed_=false,committed_=false;
 };
 std::unique_ptr<HeightCandidate> stage_height(const std::string &,NativeMaterialHeightResource);
 bool step_height_candidate(const std::string &,HeightCandidate &,std::size_t packet_bytes=4ULL*1024*1024);
 std::unique_ptr<HeightChange> install_height_candidate(const std::string &,std::unique_ptr<HeightCandidate>);
 void install(std::string resource,std::string content_hash,std::shared_ptr<NativeMaterialGraph>);
 void alias(const std::string &source,const std::string &destination);
 void retain_alias_from(const NativeMaterialGpuLibrary &,const std::string &key);
 // Preview metadata changes encoded identity without changing shader programs.
 void register_content_alias(const std::string &resource,const std::string &hash);
 void erase(const std::string &resource);
 void clear();
 void evict_except(const std::set<std::string> &resident);
 bool contains(const std::string &)const;
 bool uses_object(const std::string &)const;
 bool uses_ao(const std::string &)const;
 std::vector<int> view_channels(const std::string &)const;
 std::vector<int> view_channels(const std::string &,const NativeMaterialDescription &)const;
 std::uint64_t revision(const std::string &)const;
 const NativeMaterialDescription &description(const std::string &)const;
 const NativeMaterialGraph &source_graph(const std::string &)const;
 std::vector<int> effective_channels(const std::string &,const NativeMaterialDescription &)const;
 NativeMaterialGpuContext default_context(const std::string &)const;
 void validate_instance(const std::string &,const NativeMaterialDescription &)const;
 void validate_mapping_edit(const std::string &,const NativeMaterialDescription *before,const NativeMaterialDescription &after)const;
 std::string signature(const std::string &,int,const NativeMaterialDescription &)const;
 const NativeMaterialDependencies &dependencies(const std::string &,int)const;
 const NativeMaterialDependencies &dependencies(const std::string &,int,const NativeMaterialDescription &)const;
 NativeMaterialDependencies requirements(const std::string &,const NativeMaterialDescription &)const;
 // Legacy immediate bridge rejects image-backed owners. Workspace and Open step an unpublished
 // candidate through bounded image upload and program compilation, then atomically
 // caches it on the existing source Entry. No source alias/default is replaced.
 // A caller must stage this before publishing the instance interpretation.
 void prepare_height(const std::string &,NativeMaterialHeightResource);
 bool height_ready(const std::string &)const;
 // Admission dispatch, not a content oracle: actual authored parameters and
 // complete candidate object table, finite diagnostic surface, no readback.
 // Forces first-use policy resources before a pending project is adopted.
 void probe_height(const std::string &,const NativeMaterialDescription &,NativeMaterialGpuContext);
 // Preserved native Normal/Bump graph. It can render raw WORLD pairs or
 // encoded target-tangent perturbations for the layer stack and export capture.
 std::uint64_t source_generation()const{return clock_;}
 bool world_normal_uses_object(const std::string &)const;
 bool world_normal_perturbed(const std::string &)const;
 QString world_normal_error(const std::string &)const;
 NativeWorldNormalContextState world_normal_context_state(const std::string &)const;
 bool world_normal_requires_generated(const std::string &)const;
 bool world_normal_requires_view(const std::string &)const;
 bool world_normal_requires_baked_ao(const std::string &)const;
 bool world_normal_requires_images(const std::string &)const;
 void validate_world_normal_ao(const std::string &,const NativeMaterialBakedAoContext *)const;
 std::string world_normal_ao_signature(const std::string &,int tile,const NativeMaterialBakedAoContext *)const;
 void validate_world_normal_view(const std::string &,const NativeMaterialViewContext *)const;
 // CPU-only exact physical-payload check; no program or geometry upload.
 void validate_world_normal_geometry(const std::string &,const NativeFragmentGeometry &,const NativeMaterialViewContext *view=nullptr,const NativeMaterialBakedAoContext *ao=nullptr)const;
 std::string world_normal_uv(const std::string &)const;
 bool prepare_world_normal(const std::string &,const NativeFragmentGeometry *supplied=nullptr,const NativeMaterialViewContext *view=nullptr,const NativeMaterialBakedAoContext *ao=nullptr,bool tangent_output=false);
 std::string world_normal_signature(const std::string &,const NativeMaterialDescription &,const NativeMaterialViewContext *view=nullptr)const;
 void render_world_normal(const std::string &,const NativeMaterialDescription &,
   const NativeMaterialFragmentGpuGeometry &,int tile,blender::gpu::Texture *front,
   blender::gpu::Texture *back,const std::string &source_uv,const NativeMaterialViewContext *view=nullptr,const NativeBakedAoInput *ao=nullptr,bool tangent_output=false,const NativeMaterialProjection *projection=nullptr);
 void prepare(const std::string &,int channel);
 // Cached TILED handles may be evicted after import. Queue recovery before any
 // output target is changed; the existing preview frame pumps one action.
 bool prepare_images(const std::string &);
 void step_image_uploads(std::size_t packet_bytes=4ULL*1024*1024);
 bool image_uploads_pending()const;
 QString image_error(const std::string &)const;
 void render(const std::string &,int channel,const NativeMaterialDescription &,
             blender::gpu::Texture *,const NativeMaterialGpuContext &,std::array<int,4>);
 struct Statistics{std::size_t resources=0,sources=0,programs=0,world_programs=0;std::uint64_t dispatches=0,texels=0,world_dispatches=0;};
 Statistics statistics()const;
 private:
 struct HeightState{mutable ShaderTreeCache cold;mutable ColdImageCache pixels;explicit HeightState(NativeMaterialHeightResource value):resource(std::move(value)){std::array<blender::bNodeTree *,1> roots{resource.prepared.tree};image_requests=native_material_image_upload_requests(*resource.prepared.source,roots,false);}NativeMaterialHeightResource resource;std::vector<NativeImageUploadRequest> image_requests;std::unique_ptr<NativeMaterialGpuProgram> program;};
 struct Entry{mutable ShaderTreeCache cold;mutable ColdImageCache pixels;std::shared_ptr<NativeMaterialGraph> graph;std::string content_hash,cache_identity;std::map<int,std::unique_ptr<NativeMaterialGpuProgram>> programs;std::map<int,NativeMaterialDependencies> dependencies;std::unique_ptr<HeightState> height;std::vector<NativeImageUploadRequest> image_requests;NativeFragmentImageReadiness source_image_readiness;std::unique_ptr<NativeMaterialImageUpload> source_upload,height_upload;QString source_image_error;std::map<std::string,std::unique_ptr<NativeMaterialFragmentGpuProgram>> world_programs;NativeMaterialDependencies world_dependencies;NativeFragmentLayout world_layout;std::string world_image_signature;bool world_image_required=false;NativeFragmentImageReadiness world_image_readiness;std::unique_ptr<NativeMaterialImageUpload> world_upload;QString world_error;bool world_prepared=false;std::uint64_t revision=0,dispatches=0,texels=0,world_dispatches=0;};
 std::map<std::string,std::shared_ptr<Entry>> resources_;
 std::map<std::string,std::weak_ptr<Entry>> content_;
 std::uint64_t clock_=0;
 const Entry &entry(const std::string &)const;
};
}
