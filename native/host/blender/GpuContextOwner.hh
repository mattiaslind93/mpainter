// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "GHOST_IContext.hh"
#include "GPU_platform.hh"
namespace bts {
// Ownership belongs to the host, while Blender borrows the GHOST context until
// WM_exit_ex. Presentation and native context APIs stay in the backend adapter.
class GpuContextOwner {
 public:
 virtual ~GpuContextOwner()=default;
 virtual GHOST_IContext *ghost_context()=0;
 virtual blender::GPUBackendType backend()const=0;
 virtual void restore_external_state()=0;
};
}
