#pragma once
namespace bts {
enum class NativeMappingEditOrigin {Authoring,HistoryRestore};
struct NativeMaterialGraph;
struct NativeMaterialDescription;
struct NativeMaterialParameter;
// Immutable original-edge admission only; no traversal, allocation or GPU work.
void validate_native_mapping_override(const NativeMaterialGraph &,const NativeMaterialParameter &,bool require_active);
void validate_native_mapping_instance(const NativeMaterialGraph &,const NativeMaterialDescription &);
}
