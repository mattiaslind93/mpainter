// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "PaintPixels.hh"
#include "PaintRecording.hh"
#include "PaintRasterSource.hh"
#include <mutex>
#include <atomic>
namespace bts {
// Persistent authoring graph. Copies/Undo share commands and old COW pages.
// Pixel materialization is an explicit CPU reference/export boundary; GPU
// consumers traverse these same commands without asking for CPU pixel data.
std::vector<PaintPixels> evaluate_paint_batch(std::span<const PaintPixels> inputs);
struct PaintRecipe {
  ~PaintRecipe();
  enum class Kind { Stamp, Over, Mix, Difference, Source, ClearChannel };
  Kind kind = Kind::Stamp;
  PaintPixels base, before, ink;
  PaintPixels::Stamp stamp;
  std::shared_ptr<const PaintRasterSource> source;
  PaintPixels::Bounds bounds;
  PaintPixels::PageVersions versions;
  int resolution = 0;
  std::uint64_t revision = 0;
  float opacity = 1;
  bool erase = false, replace = false;
  int cleared_channel = -1;
  // The caller supplies the entire current stroke and includes both old/new
  // spline tails in dirty bounds. A changed page can be rebuilt directly from
  // before + ink, without replaying intermediate preview opacity results.
  bool complete_stroke = false;
  std::vector<const PaintPixels *> cpu_inputs() const
  {
    std::vector<const PaintPixels *> inputs{&base, &before, &ink};
    if (kind == Kind::Stamp)
      for (const auto &payload : {stamp.smudge, stamp.cloned})
        if (payload && payload->recipe)
          payload->recipe->cpu_sources([&](const PaintPixels &p) { inputs.push_back(&p); });
    if (source) {
      auto dependencies = source->cpu_inputs();
      inputs.insert(inputs.end(), dependencies.begin(), dependencies.end());
    }
    return inputs;
  }
  const PaintPixels &evaluated() const;

 private:
  friend std::vector<PaintPixels> evaluate_paint_batch(std::span<const PaintPixels>);
  mutable std::once_flag once_;
  mutable std::atomic_bool ready_ = false;
  mutable std::unique_ptr<PaintPixels> pixels_;
};
}  // namespace bts
