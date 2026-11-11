// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GpuContextOwner.hh"
#include <memory>
namespace bts {
void install_metal_shared_contexts();
class MetalContextOwner final : public GpuContextOwner {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 MetalContextOwner();
 ~MetalContextOwner() override;
 GHOST_IContext *ghost_context() override;
 blender::GPUBackendType backend()const override;
 void restore_external_state() override;
};
}
