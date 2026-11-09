#pragma once
#include "NativeMaterialFragmentNormal.hh"
#include <QJsonObject>
#include <string>
#include <vector>
namespace bts {
struct NativeFragmentResidentFixtures;
struct NativeFragmentGpuFixtures {
  std::vector<PreparedNativeFragmentNormal> bumps;
  std::shared_ptr<const NativeFragmentResidentFixtures> resident;
  PreparedNativeFragmentNormal geometry, named, ramp;
  std::vector<PreparedNativeFragmentNormal> image_normals;
  std::vector<std::pair<std::string, PreparedNativeFragmentNormal>> unsupported;
};
// Called before entering the native GPU owner. Internally prepares on a worker.
NativeFragmentGpuFixtures prepare_native_fragment_gpu_fixtures();
// Caller owns current native GPU context. Diagnostic reads only; no product API.
QJsonObject verify_native_fragment_normal_gpu(const NativeFragmentGpuFixtures &);
}  // namespace bts
