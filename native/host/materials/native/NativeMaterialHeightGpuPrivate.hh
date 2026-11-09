// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialHeightGpu.hh"
#include "NativeMaterialGpuLibrary.hh"
namespace bts {
struct NativeMaterialHeightGpu::Impl {
  std::string key;
  std::unique_ptr<NativeMaterialGpuLibrary::HeightCandidate> candidate;
  std::unique_ptr<NativeMaterialGpuLibrary::HeightChange> change;
};
}  // namespace bts
