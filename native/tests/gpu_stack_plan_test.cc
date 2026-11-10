// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuStackPlan.hh"
#include "GpuDerivedDirty.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  GpuStackPlan plan;
  std::vector<GpuStackNode> nodes{{"under", "fill", {}, {}},
                                  {"child", "paint", {}, {}},
                                  {"folder", "mask", {"child"}, {}},
                                  {"upper", "blend", {"under", "folder"}, {}}};
  auto initial = plan.prepare(1024, 768, nodes);
  assert(initial.texels == 4ULL * 1024 * 768);
  plan.commit(std::move(initial));
  assert(plan.prepare(1024, 768, nodes).texels == 0);
  nodes[1].pages[7] = {510, 10, 66, 66, 1};
  auto changed = plan.prepare(1024, 768, nodes);
  assert(changed.work[0].regions.empty());
  for (int i = 1; i < 4; ++i)
    assert(changed.work[i].regions.size() == 2);
  assert(changed.texels == 6ULL * 256 * 256);
  // Preparing, then cancelling, must not consume dirty revisions.
  assert(plan.prepare(1024, 768, nodes).texels == changed.texels);
  plan.commit(std::move(changed));
  nodes[1].pages.clear();
  auto undo = plan.prepare(1024, 768, nodes);
  assert(undo.work[1].regions.size() == 2);
  plan.commit(std::move(undo));
  nodes[2].parameters = "different folder opacity";
  auto opacity = plan.prepare(1024, 768, nodes);
  assert(opacity.work[0].regions.empty() && opacity.work[1].regions.empty());
  assert(opacity.work[2].regions.size() == 12 && opacity.work[3].regions.size() == 12);
  plan.commit(std::move(opacity));
  nodes[3].dependencies = {"folder", "under"};
  auto reorder = plan.prepare(1024, 768, nodes);
  assert(reorder.texels == 1024 * 768);
  plan.commit(std::move(reorder));
  auto resized = plan.prepare(1000, 777, nodes);
  assert(resized.texels == 4ULL * 1000 * 777);
  for (const auto &work : resized.work)
    for (const auto &r : work.regions)
      assert(r.x + r.width <= 1000 && r.y + r.height <= 777);
  bool rejected = false;
  try {
    auto invalid = nodes;
    invalid[0].dependencies = {"upper"};
    plan.prepare(1024, 768, invalid);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  // A mask query evaluates a source prefix before the material output. The
  // later update must reuse that prefix and still invalidate its consumers.
  GpuStackPlan queried;
  std::vector<GpuStackNode> graph{{"source", "paint", {}, {}},
                                  {"group", "folder", {"source"}, {}},
                                  {"top", "blend", {"group"}, {}}};
  queried.commit(queried.prepare(1024, 768, graph));
  graph[0].pages[1] = {12, 13, 64, 64, 1};
  std::vector<GpuStackNode> prefix{graph[0]};
  auto query = queried.prepare(1024, 768, prefix);
  assert(query.texels == 256 * 256);
  queried.commit(std::move(query), true);
  assert(queried.prepare(1024, 768, prefix).texels == 0);
  auto rest = queried.prepare(1024, 768, graph);
  assert(rest.work[0].regions.empty());
  assert(rest.work[1].regions.size() == 1 && rest.work[2].regions.size() == 1);
  // Cancellation/retry preserves pending downstream dirtiness.
  assert(queried.prepare(1024, 768, graph).texels == rest.texels);
  std::vector<GpuStackNode> folder{graph[0], graph[1]};
  queried.commit(queried.prepare(1024, 768, folder), true);
  rest = queried.prepare(1024, 768, graph);
  assert(rest.work[0].regions.empty() && rest.work[1].regions.empty());
  assert(rest.work[2].regions.size() == 1);
  queried.commit(std::move(rest));
  assert(queried.prepare(1024, 768, graph).texels == 0);
  graph[0].pages.clear();
  queried.commit(queried.prepare(1024, 768, {graph[0]}), true);
  rest = queried.prepare(1024, 768, graph);
  assert(rest.work[1].regions.size() == 1 && rest.work[2].regions.size() == 1);
  queried.commit(std::move(rest));
  // Resizing a queried prefix must invalidate every retained output.
  queried.commit(queried.prepare(512, 512, {graph[0]}), true);
  rest = queried.prepare(512, 512, graph);
  assert(rest.work[0].regions.empty() && rest.texels == 2ULL * 512 * 512);
  queried.commit(std::move(rest));
  assert(queried.prepare(512, 512, graph).texels == 0);
  // Released GPU outputs must also forget their plan metadata. Undo needs
  // a fresh dispatch even if the restored node has the same source versions.
  queried.retain({"source"});
  const auto restored = queried.prepare(512, 512, graph);
  assert(restored.work[0].regions.empty());
  assert(restored.work[1].regions.size() == 4 && restored.work[2].regions.size() == 4);
  queried.commit(restored);
  assert(queried.prepare(512, 512, graph).texels == 0);
  // Occlusion retains a previously computed prefix without evaluating it.
  // A hidden source edit or GPU eviction must still be noticed on reconnect.
  GpuStackPlan occluded;
  std::vector<GpuStackNode> visible{{"lower", "paint", {}, {}},
                                    {"top", "translucent", {"lower"}, {}}};
  occluded.commit(occluded.prepare(512, 512, visible));
  const std::vector<GpuStackNode> opaque{{"top", "opaque", {}, {}}};
  auto hide = occluded.prepare(512, 512, opaque);
  assert(hide.work.size() == 1);
  occluded.commit(std::move(hide), true);
  auto reconnect = occluded.prepare(512, 512, visible);
  assert(reconnect.work[0].regions.empty() && reconnect.work[1].regions.size() == 4);
  occluded.commit(std::move(reconnect), true);
  occluded.commit(occluded.prepare(512, 512, opaque), true);
  visible[0].pages[0] = {0, 0, 16, 16, 9};
  auto edited_hidden = occluded.prepare(512, 512, visible);
  assert(edited_hidden.work[0].regions.size() == 1 && edited_hidden.work[1].regions.size() == 4);
  occluded.commit(std::move(edited_hidden), true);
  occluded.commit(occluded.prepare(512, 512, opaque), true);
  occluded.retain({"top"});
  assert(occluded.prepare(512, 512, visible).work[0].regions.size() == 4);
  GpuDerivedDirty derived;
  assert(derived.changed(4, 1001, 1, {1}, 512, 512, false).size() == 4);
  assert(derived.changed(4, 1001, 1, {1}, 512, 512, false).empty());
  assert(derived.changed(4, 1001, 2, {1}, 512, 512, false) == std::set<int>{1});
  assert(derived.changed(4, 1001, 4, {1}, 512, 512, false).size() == 4);
  GpuDerivedDirty::Chunks halo;
  GpuDerivedDirty::normal_halo(
      halo, 1001, {1}, {{1001, {512, 512}}, {1002, {1024, 1024}}, {1011, {512, 512}}});
  assert((halo[1002] == std::set<int>{0, 4, 8}));  // Two-texel halo, rescaled across UDIM edge.
  assert(halo[1011].empty());  // This source chunk never reaches the upper tile.
  std::vector<unsigned char> coverage(1024 * 256);
  coverage[100 * 1024 + 10] = 1;
  auto assigned = std::vector<char>(coverage.begin(), coverage.end());
  assigned[100 * 1024 + 900] = 1;
  const std::array<GpuMarginPlan::Lookup, 1> lookups{{{100 * 1024 + 900, 10, 100}}};
  const auto seam = GpuMarginPlan::compile(1024, 256, coverage, lookups, assigned);
  assert(GpuDerivedDirty::padded({0}, &seam).contains(3));  // Remote UV island shares this donor.
  assert(!GpuDerivedDirty::padded({1}, &seam).contains(3));
  std::cout << "GPU stack plan: dirty chunks, prefix/folder cache, undo, reorder, cancellation "
               "resize, derived UV halos and remote seam dependencies passed\n";
}
