#include <QtCore>
#include "PendingProjectGpu.hh"
#include "BlenderHost.hh"
#include "MaterialInspection.hh"
#include "NativeMaterialHeightGpu.hh"
#include <cstdio>
#include <exception>

namespace bts {
PendingProjectGpu::PendingProjectGpu(BlenderHost &host) : host_(&host) {}
PendingProjectGpu::~PendingProjectGpu()
{
  if (!host_)
    return;
  // No late context-less retirement is permitted: the enclosing Open job is
  // explicitly drained while Host and viewport are alive, before BFD teardown.
  try {
    host_->discard_project_gpu(*this);
  }
  catch (...) {
    std::fputs("BTS_PENDING_PROJECT_GPU_OWNER_CLEANUP_FAILED\n", stderr);
    std::terminate();
  }
}
}  // namespace bts
