#pragma once
#include "NativeMaterialFragmentNormal.hh"
#include "NativeMaterialMapping.hh"
#include "NativeMaterialViewContext.hh"
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace blender {struct ObjectInfos;}
namespace blender::gpu {class Texture;}
namespace bts {
struct NativeMaterialProjection;
class NativeFragmentGeometry;class NativeMaterialBakedAoContext;struct NativeBakedAoInput;
// Explicit UV-pixel raster contract. These are mesh vertices, never texture
// readbacks. Ni is original shading Normal; N and Ng are independent inputs.
struct NativeFragmentVertex {
 std::array<float,2> raster_uv{},uv{};
 std::array<float,3> position{},normal{0,0,1},initial_normal{0,0,1},geometric_normal{0,0,1};
 // Real per-corner object-space CD_TANGENT, including handedness/sentinel.
 std::array<float,4> tangent{0,0,0,1};
};
struct NativeFragmentLayout {
 // The supplied source UV has this exact name. Indexed resident draws resolve
 // empty to the logical DefaultRender request, independently of target raster UV.
 // Another requested UV name is unsupported, never silently aliased.
 std::string uv_name;
 bool tangent_output=false; // Encoded perturbation in the target UV frame, for layer composition.
 bool fill_projection=false;
 bool tangent_stream=false; // CD_TANGENT has the exact same UV name.
 bool paired_physical_sides=false; // Production front/back pair; dynamic side, one program.
 bool generated_stream=false; // Separately supplied actual ORCO, resident only.
 bool resident_images=false; // Shared worker-decoded/uploaded single-file owners, indexed resident only.
 bool default_image_uv=false; // Same-stream proof checked against actual captured geometry.
 bool resident_baked_ao_context=false; // Actual retained per-tile project AO, indexed resident only.
 bool resident_view_context=false; // Actual validated view required, indexed resident only.
 bool resident_object_context=false; // Actual indexed owner ObjectInfos/full matrices only.
 bool resident_raster_origin=false; // Opt-in indexed draw_tile variant; no product admission.
 std::size_t physical_parameters=0; // zero selects the source logical count.
 std::vector<std::size_t> parameter_slots; // optional logical -> physical rows.
};
struct NativeFragmentDiagnostics {
 std::string generated;
 int material_functions=0,active_bumps=0,normal_maps=0;
 bool mapping_rows=false,codegen_invoked=false,object_context=false,generated_context=false,view_context=false,baked_ao_context=false,image_context=false,object_matrices_context=false;
};
// Prepared once when mesh/physical facing changes, never inside draw or per edit.
// Triangles are partitioned into at most four raster-winding/physical-facing
// groups, so stock gl_FrontFacing agrees with the supplied physical context.
struct NativeFragmentFacingBatch {unsigned int first=0,count=0;bool invert_front_face=false;};
struct NativeFragmentFacingGeometry {
 std::vector<NativeFragmentVertex> vertices;
 std::vector<NativeFragmentFacingBatch> batches;
};
NativeFragmentFacingGeometry prepare_native_fragment_facing(
 std::span<const NativeFragmentVertex>,std::span<const unsigned char> physical_front);
struct NativeFragmentPhysicalContext {
 // Actual Blender ObjectInfos copy; only its stock negative-scale flag is used.
 // Never infer that flag from UV winding or a guessed determinant.
 const blender::ObjectInfos *object_infos=nullptr;
 // Column-major real world-to-object linear matrix, as in the mesh draw owner.
 std::array<float,9> world_to_object{1,0,0,0,1,0,0,0,1};
 std::span<const NativeFragmentFacingBatch> facing_batches;
};
struct NativeFragmentResidentDiagnostics {
 std::uint64_t revision=0,storage_bytes=0;
 std::size_t objects=0,streams=0,vertices=0,indices=0,ranges=0;
 std::size_t vertex_uploads=0,index_uploads=0,object_uploads=0;
};
// Cached immutable GPU upload only. Caller captures/prepares geometry once per
// authored mesh revision. No Main/Mesh pointers, draw-time CPU mesh work or readback.
class NativeMaterialFragmentGpuGeometry {
 public:
 explicit NativeMaterialFragmentGpuGeometry(std::shared_ptr<const NativeFragmentGeometry>);
 ~NativeMaterialFragmentGpuGeometry();
 NativeMaterialFragmentGpuGeometry(const NativeMaterialFragmentGpuGeometry &)=delete;
 NativeMaterialFragmentGpuGeometry &operator=(const NativeMaterialFragmentGpuGeometry &)=delete;
 const NativeFragmentGeometry &source()const; // Same immutable CPU payload retained by the resident upload.
 const NativeFragmentResidentDiagnostics &diagnostics()const;
 private:
 friend class NativeMaterialFragmentGpuProgram;
 struct Impl;std::unique_ptr<Impl> impl_;
};
// Explicit GPU-owner wrapper only. No interactive caller/registry/policy/export
// admission. Keeps prepared.source/Main alive through material/buffer retirement.
// Construction, methods and destruction require the same current GPU owner.
// Image-free tangent NormalMap needs all explicit physical bindings. Other
// spaces, derivative-basis custom1, images/view dependencies reject. Object
// attributes require the explicit indexed resident owner payload.
class NativeMaterialFragmentGpuProgram {
 public:
 NativeMaterialFragmentGpuProgram(PreparedNativeFragmentNormal,NativeFragmentLayout={});
 ~NativeMaterialFragmentGpuProgram();
 NativeMaterialFragmentGpuProgram(const NativeMaterialFragmentGpuProgram &)=delete;
 NativeMaterialFragmentGpuProgram &operator=(const NativeMaterialFragmentGpuProgram &)=delete;
 void validate_images()const; // Readiness/handles only, before target mutation or allocation.
 void update_ramps(const NativeMaterialDescription &);
 // Clears a private RGBA32F target to coverage0, then draws triangles. Output
 // is raw world-vector RGB/coverage, with no normalize/encoding/color transform.
 // No readback, image decode, scalar Height or CPU texture evaluation occurs.
 void draw(blender::gpu::Texture *,std::span<const NativeFragmentVertex>,
           std::span<const std::array<float,4>>,std::span<const NativeMaterialMapping> mappings={},
           const NativeFragmentPhysicalContext *physical=nullptr);
 // Draw pre-indexed ranges for one target UDIM. Clears that private target;
 // subtracts tile origin from raster only; authored UV/P and source T stay intact.
 // Uses each stream's actual object matrix/flags. No CPU mesh rebuild/upload.
 void draw_tile(blender::gpu::Texture *,const NativeMaterialFragmentGpuGeometry &,int tile,
                std::span<const std::array<float,4>>,
                std::span<const NativeMaterialMapping> mappings={},bool back_face=false,
                const NativeMaterialViewContext *view=nullptr,const NativeBakedAoInput *ao=nullptr,
                const NativeMaterialProjection *projection=nullptr);
 const NativeFragmentDiagnostics &diagnostics()const;
 private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
