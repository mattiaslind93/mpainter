#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace bts {
// Geometry-only sparse program. Original image refs carry the high bit;
// intermediate refs name an earlier instruction. No channel pixels are stored.
struct GpuMarginPlan {
  static constexpr std::uint32_t original = 0x80000000u, zero = 0xffffffffu;
  struct Lookup {
    int destination;
    float x, y;
  };
  struct Operation {
    std::array<std::uint32_t, 4> meta{};  // destination, operation, divisor bits, dependency level
    std::array<std::uint32_t, 8> refs{};
    std::array<float, 8> weights{};
  };
  static_assert(sizeof(Operation) == 80);
  int width = 0, height = 0;
  std::vector<Operation> operations;
  std::vector<std::vector<std::uint32_t>> levels;
  std::vector<std::uint32_t> schedule, level_offsets;
  std::vector<std::array<std::uint32_t, 2>> outputs;  // destination, last instruction
  std::vector<std::uint32_t> original_pixels;         // compact immutable source slots
  // Geometry-only dependency closure: a donor chunk may update a remote UV
  // island's margin. Derived normals must invalidate those destination chunks.
  std::vector<std::vector<std::uint32_t>> affected_chunks;
  static GpuMarginPlan compile(int width,
                               int height,
                               std::span<const unsigned char> coverage,
                               std::span<const Lookup> lookups = {},
                               std::span<const char> adjacent_mask = {})
  {
    if (width <= 0 || height <= 0 || std::uint64_t(width) * height >= original ||
        coverage.size() != std::size_t(width) * height)
      throw std::invalid_argument("Invalid GPU margin dimensions");
    if (!adjacent_mask.empty() && adjacent_mask.size() != coverage.size())
      throw std::invalid_argument("Invalid adjacent-face margin mask");
    GpuMarginPlan plan;
    plan.width = width;
    plan.height = height;
    std::vector<unsigned char> assigned(coverage.begin(), coverage.end());
    std::vector<std::uint32_t> current(coverage.size());
    for (std::uint32_t i = 0; i < current.size(); ++i)
      current[i] = original | i;
    const auto index = [&](int x, int y) {
      return x < 0 || y < 0 || x >= width || y >= height ? -1 : y * width + x;
    };
    const auto reference = [&](int x, int y) {
      const int i = index(x, y);
      return i < 0 ? zero : current[i];
    };
    const auto emit = [&](Operation op) {
      std::uint32_t level = 0;
      for (auto ref : op.refs)
        if ((ref & original) == 0)
          level = std::max(level, plan.operations.at(ref).meta[3] + 1);
      op.meta[3] = level;
      const auto id = std::uint32_t(plan.operations.size());
      if (id >= original)
        throw std::length_error("GPU margin program too large");
      plan.operations.push_back(op);
      if (plan.levels.size() <= level)
        plan.levels.resize(level + 1);
      plan.levels[level].push_back(id);
      return id;
    };
    const auto extend = [&] {
      const auto used = [&](int x, int y) {
        const int i = index(x, y);
        return i >= 0 && assigned[i] != 0;
      };
      for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::array<std::uint32_t, 2>> changed;
        for (int y = 0; y < height; ++y)
          for (int x = 0; x < width; ++x) {
            const auto dst = std::uint32_t(y * width + x);
            if (assigned[dst] ||
                !(used(x - 1, y) || used(x + 1, y) || used(x, y - 1) || used(x, y + 1)))
              continue;
            Operation op;
            op.meta[0] = dst;
            op.refs.fill(zero);
            int slot = 0;
            float total = 0;
            // Match IMB_filter_extend's x-major accumulation, including missing taps.
            for (int dx = -1; dx <= 1; ++dx)
              for (int dy = -1; dy <= 1; ++dy) {
                if (dx == 0 && dy == 0)
                  continue;
                if (used(x + dx, y + dy)) {
                  const float weight = dx == 0 || dy == 0 ? 2 : 1;
                  op.refs[slot] = reference(x + dx, y + dy);
                  op.weights[slot] = weight;
                  total += weight;
                }
                ++slot;
              }
            op.meta[2] = std::bit_cast<std::uint32_t>(total);
            changed.push_back({dst, emit(op)});
          }
        for (auto change : changed) {
          current[change[0]] = change[1];
          assigned[change[0]] = 1;
        }
      }
    };
    extend();
    if (!adjacent_mask.empty()) {
      for (const auto &lookup : lookups) {
        if (lookup.destination < 0 || std::size_t(lookup.destination) >= current.size() ||
            !std::isfinite(lookup.x) || !std::isfinite(lookup.y))
          throw std::invalid_argument("Invalid adjacent-face margin lookup");
        Operation op;
        op.refs.fill(zero);
        op.meta[0] = lookup.destination;
        op.meta[1] = 1;
        op.meta[2] = std::bit_cast<std::uint32_t>(1.f);
        const int x = int(std::floor(lookup.x)), y = int(std::floor(lookup.y));
        const float a = lookup.x - x, b = lookup.y - y;
        op.refs[0] = reference(x, y);
        op.refs[1] = reference(x + 1, y);
        op.refs[2] = reference(x, y + 1);
        op.refs[3] = reference(x + 1, y + 1);
        op.weights[0] = (1 - a) * (1 - b);
        op.weights[1] = a * (1 - b);
        op.weights[2] = (1 - a) * b;
        op.weights[3] = a * b;
        current[lookup.destination] = emit(op);
      }
      for (std::size_t i = 0; i < assigned.size(); ++i)
        assigned[i] = adjacent_mask[i] != 0;
      extend();
    }
    for (std::uint32_t i = 0; i < current.size(); ++i)
      if ((current[i] & original) == 0)
        plan.outputs.push_back({i, current[i]});
    std::unordered_map<std::uint32_t, std::uint32_t> slots;
    for (auto &op : plan.operations)
      for (auto &ref : op.refs)
        if (ref != zero && (ref & original)) {
          const auto pixel = ref & ~original;
          const auto [it, inserted] = slots.try_emplace(
              pixel, std::uint32_t(plan.original_pixels.size()));
          if (inserted)
            plan.original_pixels.push_back(pixel);
          ref = original | it->second;
        }
    for (const auto &level : plan.levels) {
      plan.level_offsets.push_back(std::uint32_t(plan.schedule.size()));
      plan.schedule.insert(plan.schedule.end(), level.begin(), level.end());
    }
    const auto columns = std::uint32_t((width + 255) / 256);
    const auto chunk = [&](std::uint32_t pixel) {
      return (pixel / std::uint32_t(width) / 256) * columns + (pixel % std::uint32_t(width) / 256);
    };
    std::vector<std::vector<std::uint32_t>> donors(plan.operations.size());
    for (std::size_t i = 0; i < plan.operations.size(); ++i) {
      auto &sources = donors[i];
      for (auto ref : plan.operations[i].refs) {
        if (ref == zero)
          continue;
        if (ref & original)
          sources.push_back(chunk(plan.original_pixels.at(ref & ~original)));
        else
          sources.insert(sources.end(), donors.at(ref).begin(), donors.at(ref).end());
      }
      std::sort(sources.begin(), sources.end());
      sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    }
    plan.affected_chunks.resize(columns * ((height + 255) / 256));
    for (auto out : plan.outputs)
      for (auto source : donors.at(out[1]))
        plan.affected_chunks[source].push_back(chunk(out[0]));
    for (auto &destinations : plan.affected_chunks) {
      std::sort(destinations.begin(), destinations.end());
      destinations.erase(std::unique(destinations.begin(), destinations.end()),
                         destinations.end());
    }
    return plan;
  }
  // Verification oracle for the instruction format, not an interactive path.
  void evaluate_reference(std::span<float> pixels) const
  {
    if (pixels.size() != std::size_t(width) * height * 4)
      throw std::invalid_argument("GPU margin reference dimensions differ");
    std::vector<std::array<float, 4>> results(operations.size());
    const auto sample = [&](std::uint32_t ref, int c) {
      return ref == zero      ? 0.f :
             (ref & original) ? pixels[std::size_t(original_pixels.at(ref & ~original)) * 4 + c] :
                                results.at(ref)[c];
    };
    for (std::size_t i = 0; i < operations.size(); ++i) {
      const auto &op = operations[i];
      for (int c = 0; c < 4; ++c) {
        float value = 0;
        if (op.meta[1])
          value = (op.weights[0] * sample(op.refs[0], c) + op.weights[1] * sample(op.refs[1], c)) +
                  (op.weights[2] * sample(op.refs[2], c) + op.weights[3] * sample(op.refs[3], c));
        else {
          for (int j = 0; j < 8; ++j)
            value += op.weights[j] * sample(op.refs[j], c);
          value /= std::bit_cast<float>(op.meta[2]);
        }
        results[i][c] = value;
      }
    }
    for (auto out : outputs)
      std::copy(results[out[1]].begin(),
                results[out[1]].end(),
                pixels.begin() + std::size_t(out[0]) * 4);
  }
};
}  // namespace bts
