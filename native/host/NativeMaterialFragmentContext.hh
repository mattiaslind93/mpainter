#pragma once
#include "NativeMaterialFragmentGpuCompiler.hh"
namespace bts {
// Pure CPU preflight for the bounded production layout. Reuses the compiler's
// exact source/context checks; creates no material, program or GPU object.
NativeFragmentDiagnostics inspect_native_fragment_source_context(
    const PreparedNativeFragmentNormal &, const NativeFragmentLayout &);
// Static request planning never asserts that the required payload exists.
NativeFragmentLayout native_world_normal_source_request(const PreparedNativeFragmentNormal &);
NativeFragmentLayout native_world_normal_source_layout(
    const PreparedNativeFragmentNormal &,
    const NativeFragmentGeometry *supplied = nullptr,
    const NativeMaterialViewContext *view = nullptr,
    const NativeMaterialBakedAoContext *ao = nullptr);
}  // namespace bts
