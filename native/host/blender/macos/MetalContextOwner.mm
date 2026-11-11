// SPDX-License-Identifier: GPL-3.0-or-later
#include "MetalContextOwner.hh"
#include "GHOST_ContextMTL.hh"
#include <stdexcept>
namespace bts {
struct MetalContextOwner::Impl {std::unique_ptr<GHOST_ContextMTL> ghost;};
MetalContextOwner::MetalContextOwner():impl_(std::make_unique<Impl>()){
 GHOST_GPUSettings settings{};settings.context_type=GHOST_kDrawingContextTypeMetal;
 const GHOST_ContextParams params=GHOST_CONTEXT_PARAMS_FROM_GPU_SETTINGS_OFFSCREEN(settings);
 impl_->ghost=std::make_unique<GHOST_ContextMTL>(params,nil,nil);
 if(impl_->ghost->initializeDrawingContext()!=GHOST_kSuccess)
  throw std::runtime_error("Cannot initialize windowless Metal context");
}
MetalContextOwner::~MetalContextOwner()=default;
GHOST_IContext *MetalContextOwner::ghost_context(){return impl_->ghost.get();}
blender::GPUBackendType MetalContextOwner::backend()const{return blender::GPU_BACKEND_METAL;}
void MetalContextOwner::restore_external_state(){/* No borrowed Qt GL state on Metal. */}
}
