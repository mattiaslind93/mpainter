#pragma once
#include "NativeMaterialGpuProgram.hh"
#include <atomic>
#include <map>
#include <set>
namespace blender {struct Main;struct Scene;struct ViewLayer;}
namespace bts {
void require_native_height_no_drivers(blender::Main &);
// Worker-only private evaluation. No active/global/editor depsgraph, callbacks,
// output mesh consumption or synchronous writeback. Returned tables own values.
std::map<std::uint32_t,NativeMaterialGpuContext> prepare_native_height_context(
 blender::Main &,blender::Scene &,blender::ViewLayer &,const std::set<std::uint32_t> &,const std::atomic_bool &);
}
