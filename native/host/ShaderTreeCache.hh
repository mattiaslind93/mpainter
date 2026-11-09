#pragma once
#include <memory>
#include <vector>
namespace blender {
struct Main;
struct bNodeTree;
struct Image;
}  // namespace blender
namespace bts {
// Lossless native shader payloads on disk; stable ID shells stay in the Main.
// The GPU owner retires consumers before spill and restores before reuse/save.
class ShaderTreeCache {
 public:
  ShaderTreeCache();
  ~ShaderTreeCache();
  ShaderTreeCache(ShaderTreeCache &&) noexcept;
  ShaderTreeCache &operator=(ShaderTreeCache &&) noexcept;
  bool cold() const;
  void spill(blender::Main &,
             const std::vector<blender::bNodeTree *> &,
             const std::vector<blender::Image *> &images = {});
  void restore();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace bts
