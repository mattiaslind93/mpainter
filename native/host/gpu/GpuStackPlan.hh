#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace bts {
// Dependency planning only: no pixel reads, blends or content hashes on the CPU.
// Every node is a cached GPU result, including folder contents and stack prefixes.
struct GpuStackPage {
  int x = 0, y = 0, width = 0, height = 0;
  std::uint64_t revision = 0;
  bool operator==(const GpuStackPage &) const = default;
};
struct GpuStackNode {
  std::string id, parameters;
  std::vector<std::string> dependencies;
  std::map<std::uint64_t, GpuStackPage> pages;
};
struct GpuStackRegion {
  int x = 0, y = 0, width = 0, height = 0;
};
struct GpuStackWork {
  std::string id;
  std::vector<GpuStackRegion> regions;
};
class GpuStackPlan {
 public:
  static constexpr int chunk_size = 256;
  struct Transaction {
    int width = 0, height = 0;
    std::map<std::string, GpuStackNode> nodes;
    std::vector<GpuStackWork> work;
    std::uint64_t texels = 0;
  };
  Transaction prepare(int width, int height, const std::vector<GpuStackNode> &nodes) const
  {
    if (width <= 0 || height <= 0)
      throw std::invalid_argument("Invalid GPU stack dimensions");
    Transaction result;
    result.width = width;
    result.height = height;
    const int columns = (width + chunk_size - 1) / chunk_size,
              rows = (height + chunk_size - 1) / chunk_size;
    std::map<std::string, std::set<std::uint64_t>> dirty;
    for (const auto &node : nodes) {
      if (node.id.empty() || result.nodes.contains(node.id))
        throw std::invalid_argument("Duplicate GPU stack node");
      auto &changed = dirty[node.id];
      if (width == width_ && height == height_)
        if (auto pending = pending_.find(node.id); pending != pending_.end())
          changed = pending->second;
      const auto old = nodes_.find(node.id);
      const bool full = width != width_ || height != height_ || old == nodes_.end() ||
                        old->second.parameters != node.parameters ||
                        old->second.dependencies != node.dependencies;
      if (full) {
        for (int y = 0; y < rows; ++y)
          for (int x = 0; x < columns; ++x)
            changed.insert(std::uint64_t(y) * columns + x);
      }
      else {
        const auto mark = [&](const GpuStackPage &page) {
          if (page.width <= 0 || page.height <= 0)
            return;
          const int left = std::clamp(page.x, 0, width), top = std::clamp(page.y, 0, height),
                    right = std::clamp(page.x + page.width, 0, width),
                    bottom = std::clamp(page.y + page.height, 0, height);
          if (left >= right || top >= bottom)
            return;
          for (int y = top / chunk_size; y <= (bottom - 1) / chunk_size; ++y)
            for (int x = left / chunk_size; x <= (right - 1) / chunk_size; ++x)
              changed.insert(std::uint64_t(y) * columns + x);
        };
        for (const auto &[id, page] : node.pages) {
          const auto previous = old->second.pages.find(id);
          if (previous == old->second.pages.end() || previous->second != page) {
            mark(page);
            if (previous != old->second.pages.end())
              mark(previous->second);
          }
        }
        // Undo/clear removes pages: their old coverage must be erased on the GPU.
        for (const auto &[id, page] : old->second.pages)
          if (!node.pages.contains(id))
            mark(page);
      }
      for (const auto &dependency : node.dependencies) {
        if (!result.nodes.contains(dependency))
          throw std::invalid_argument("GPU stack dependency must precede its consumer");
        const auto &source = dirty.at(dependency);
        changed.insert(source.begin(), source.end());
      }
      GpuStackWork work;
      work.id = node.id;
      for (auto chunk : changed) {
        const int x = int(chunk % columns) * chunk_size, y = int(chunk / columns) * chunk_size;
        const GpuStackRegion region{
            x, y, std::min(chunk_size, width - x), std::min(chunk_size, height - y)};
        work.regions.push_back(region);
        result.texels += std::uint64_t(region.width) * region.height;
      }
      result.work.push_back(std::move(work));
      result.nodes.emplace(node.id, node);
    }
    return result;
  }
  // Publish only after every GPU dispatch succeeds. Cancelled/incomplete frames
  // retain the previous revisions so a later request cannot lose a dirty page.
  void commit(Transaction transaction, bool partial = false)
  {
    if (width_ != transaction.width || height_ != transaction.height) {
      nodes_.clear();
      pending_.clear();
    }
    width_ = transaction.width;
    height_ = transaction.height;
    if (partial) {
      // Queries publish a prefix without publishing its downstream consumers.
      // Remember their dirty chunks; a later full update must not treat the
      // already-published prefix as proof that those consumers are current.
      const int columns = (width_ + chunk_size - 1) / chunk_size;
      std::map<std::string, std::set<std::uint64_t>> changed;
      for (const auto &work : transaction.work)
        for (const auto &r : work.regions)
          changed[work.id].insert(std::uint64_t(r.y / chunk_size) * columns + r.x / chunk_size);
      for (const auto &[id, node] : nodes_)
        if (!transaction.nodes.contains(id))
          for (const auto &dependency : node.dependencies) {
            if (auto source = changed.find(dependency); source != changed.end())
              pending_[id].insert(source->second.begin(), source->second.end());
          }
      for (auto &[id, node] : transaction.nodes) {
        pending_.erase(id);
        nodes_.insert_or_assign(id, std::move(node));
      }
    }
    else {
      nodes_ = std::move(transaction.nodes);
      pending_.clear();
    }
  }
  void retain(const std::set<std::string> &live)
  {
    std::erase_if(nodes_, [&](const auto &item) { return !live.contains(item.first); });
    std::erase_if(pending_, [&](const auto &item) { return !live.contains(item.first); });
  }
  void reset()
  {
    width_ = height_ = 0;
    nodes_.clear();
    pending_.clear();
  }

 private:
  int width_ = 0, height_ = 0;
  std::map<std::string, GpuStackNode> nodes_;
  std::map<std::string, std::set<std::uint64_t>> pending_;
};
}  // namespace bts
