#pragma once
#include "PreviewSourceMeshes.hh"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace blender { struct bContext; struct View3D; struct ObjectInfos; }
namespace bts {
// Defined by the separately prepared phase2 compiler; do not duplicate its ABI.
struct NativeFragmentVertex;
struct NativeFragmentPhysicalContext;
enum class NativeFragmentUvKind { Named, DefaultRender, DisplayActive };
struct NativeFragmentUvRequest {
 NativeFragmentUvKind kind=NativeFragmentUvKind::DefaultRender;
 std::string name; // Named requires nonempty. Other kinds require empty.
 bool operator==(const NativeFragmentUvRequest &)const=default;
};
struct NativeFragmentGeometryRequest {
 std::uint64_t revision=0; // Explicit authored geometry revision, never an edit counter.
 std::uint32_t material_uid=0;
 std::vector<NativeFragmentUvRequest> uv_requests;
 NativeFragmentUvRequest target_uv{NativeFragmentUvKind::DisplayActive,{}}; // painter raster authority, independently resolved
 std::vector<int> tiles;
 bool generated_stream=false; // Actual Original coordinates, opt-in only.
 bool allow_original_orco=false; // Stock ensure on a private clone after ordered original-mesh proof.
};
enum class NativeFragmentGeneratedProvenance {None,CapturedOrco,StockOriginalOrco};
struct NativeFragmentGeometryObject {
 std::uint32_t object_uid=0,source_mesh_uid=0;
 NativeFragmentGeneratedProvenance generated_provenance=NativeFragmentGeneratedProvenance::None;
 std::uint32_t original_mesh_uid=0;
 bool source_override=false; // actual sourceMeshes selection, not displaced-context proof
 std::array<float,16> object_to_world{},world_to_object{}; // actual column-major matrices
};
// Pure CPU validation shared by capture and resident upload, before allocation.
void validate_native_fragment_object_context(const NativeFragmentGeometryObject &,const blender::ObjectInfos &);
struct NativeFragmentGeometryStream {
 std::size_t object=0;
 std::string resolved_uv_name;
 NativeFragmentUvRequest target_request;
 std::string resolved_target_uv_name;
 // Keep logical empty/render and named requests distinct even if they coalesce.
 std::vector<NativeFragmentUvRequest> requests;
};
struct NativeFragmentGeometryRange {
 std::size_t stream=0;
 int tile=1001;
 std::array<float,2> raster_origin{}; // subtract ONLY from raster UV on GPU
 std::uint32_t first_index=0,index_count=0;
 bool invert_front_face=false; // canonical physical FrontFacing=true
};
class NativeFragmentGeometry;
// O(owners/streams) structural admission. Finite values are checked during
// capture and worker preparation; the resulting payload is private/immutable.
void validate_native_fragment_generated_context(const NativeFragmentGeometry &);
class NativeFragmentGeometrySource;
std::shared_ptr<const NativeFragmentGeometrySource> capture_native_fragment_geometry(
 blender::bContext *,blender::View3D *,const SourceMeshMap &,
 NativeFragmentGeometryRequest,const std::atomic_bool &);
std::shared_ptr<const NativeFragmentGeometry> prepare_native_fragment_geometry(
 std::shared_ptr<const NativeFragmentGeometrySource>,const std::atomic_bool &);
// Immutable copied scene data only; no borrowed Mesh/Main/Object pointers escape.
class NativeFragmentGeometrySource {
 public:
 ~NativeFragmentGeometrySource();
 std::uint64_t revision()const;
 std::uint32_t material_uid()const;
 std::size_t object_count()const;
 std::uint64_t storage_bytes()const;
 private:
 friend class NativeFragmentGeometry;
 struct Impl;std::unique_ptr<Impl> impl_;
 explicit NativeFragmentGeometrySource(std::unique_ptr<Impl>);
 friend std::shared_ptr<const NativeFragmentGeometrySource> capture_native_fragment_geometry(
  blender::bContext *,blender::View3D *,const SourceMeshMap &,NativeFragmentGeometryRequest,const std::atomic_bool &);
 friend std::shared_ptr<const NativeFragmentGeometry> prepare_native_fragment_geometry(
  std::shared_ptr<const NativeFragmentGeometrySource>,const std::atomic_bool &);
};
// Capture is evaluated/source-override geometry only; it does not prove
// MAT_DISPLACEMENT_BUMP or a displaced NormalMap base is available.
// CPU-prepared immutable resident-upload input. No GPU allocation/readback or
// ScalarProjectionAtlas. A single vertex stream serves all indexed UDIM ranges.
// Degenerate physical/target-UV triangles and nonfinite coordinate triangles
// are omitted using one selection shared by every UV/Generated stream. Indices
// address those compacted stream corners, never original Mesh triangle IDs.
// Invalid normals/tangents use a finite local basis; valid Mikk signs survive.
class NativeFragmentGeometry {
 public:
 ~NativeFragmentGeometry();
 std::uint64_t revision()const;
 std::uint32_t material_uid()const;
 std::uint64_t storage_bytes()const;
 std::span<const NativeFragmentGeometryObject> objects()const;
 const blender::ObjectInfos &object_infos(std::size_t object)const;
 std::span<const NativeFragmentGeometryStream> streams()const;
 std::span<const NativeFragmentVertex> vertices(std::size_t stream)const;
 bool generated_requested()const;
 std::span<const std::array<float,3>> generated(std::size_t stream)const;
 std::span<const std::uint32_t> indices()const;
 std::span<const NativeFragmentGeometryRange> ranges()const;
 // PhysicalContext creation / indexed resident draw belongs to the GPU owner;
 // an old diagnostic draw(span) must not interpret these indexed ranges.
 private:
 struct Impl;std::unique_ptr<Impl> impl_;
 explicit NativeFragmentGeometry(std::unique_ptr<Impl>);
 friend std::shared_ptr<const NativeFragmentGeometry> prepare_native_fragment_geometry(
  std::shared_ptr<const NativeFragmentGeometrySource>,const std::atomic_bool &);
};
}
