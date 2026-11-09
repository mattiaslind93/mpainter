#pragma once
#include "CompositeChannels.hh"
#include <functional>
#include <utility>
namespace bts {
struct TextureSet;
struct NativeSourceEdit {
  const TextureSet *before = nullptr, *after = nullptr;
  CompositeChannels channels{};
};
// Owner-only, immediate-use queue transaction. Preparation touches no live
// queues/images. Do not draw, process events or change bindings before commit.
class NativeSourceEditBatch {
 public:
  NativeSourceEditBatch() = default;
  NativeSourceEditBatch(NativeSourceEditBatch &&) = default;
  NativeSourceEditBatch &operator=(NativeSourceEditBatch &&) = default;
  NativeSourceEditBatch(const NativeSourceEditBatch &) = delete;
  void commit() noexcept
  {
    if (action_) {
      auto execute = std::move(action_);
      execute();
    }
  }

 private:
  friend class MaterialInspection;
  friend class BlenderHost;
  explicit NativeSourceEditBatch(std::function<void()> action) : action_(std::move(action)) {}
  std::function<void()> action_;
};
}  // namespace bts
