#pragma once
#include "NativeMaterialRawHeight.hh"
#include "NativeMaterialDependencies.hh"
#include "NativeMaterialHeightPolicy.hh"
#include <QString>
#include <vector>
#include <utility>

namespace bts {
struct StudioAsset;
// Read-only actual raw-AOV closure inventory. Used by worker admission and
// permanent native graph regressions; it does not create an activatable owner.
NativeHeightRequirements native_height_requirements(const PreparedNativeRawHeight &);
// Optional derived owner. Never registered as a catalog/source asset and never
// substituted for the ordinary source graph. Its raw AOV may own an admitted single-file raw image.
class NativeMaterialHeightResource {
 public:
 NativeMaterialHeightResource(NativeMaterialHeightResource &&)=default;
 NativeMaterialHeightResource &operator=(NativeMaterialHeightResource &&)=default;
 const NativeRawHeightProvenance &provenance()const{return prepared.provenance;}
 private:
 NativeMaterialHeightResource(QString hash,PreparedNativeRawHeight owner,NativeMaterialDependencies deps)
  :source_hash(std::move(hash)),prepared(std::move(owner)),dependencies(std::move(deps)){}
 friend class NativeMaterialGpuLibrary;
 friend NativeMaterialHeightResource prepare_native_material_height_resource(const StudioAsset &,const std::vector<StudioAsset> &,const std::atomic_bool &,NativeWorldNormalSurfaceContract);
 QString source_hash;
 PreparedNativeRawHeight prepared;
 NativeMaterialDependencies dependencies;
};
// Known Painter resource consumer by default. Explicit diagnostic consumers
// must use the same immutable contract as their installed source graph.
NativeMaterialHeightResource prepare_native_material_height_resource(
 const StudioAsset &,const std::vector<StudioAsset> &,const std::atomic_bool &,
 NativeWorldNormalSurfaceContract=NativeWorldNormalSurfaceContract::PainterUndisplaced);
}
