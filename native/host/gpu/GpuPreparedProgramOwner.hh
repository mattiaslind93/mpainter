// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <stdexcept>
#include <utility>
namespace bts {
// A prepared compiler handle may retain raw views of caller-owned metadata.
// Destroy/reset the handle first; ordinary callers only reset after readiness.
// Explicit terminal cleanup may join through the handle's existing deleter.
template<class Metadata, class Handle> class GpuPreparedProgramOwner {
  std::unique_ptr<Metadata> metadata_;
  Handle handle_;

 public:
  explicit GpuPreparedProgramOwner(Handle handle) : handle_(std::move(handle)) {}
  GpuPreparedProgramOwner(const GpuPreparedProgramOwner &) = delete;
  GpuPreparedProgramOwner &operator=(const GpuPreparedProgramOwner &) = delete;
  GpuPreparedProgramOwner(GpuPreparedProgramOwner &&other) noexcept
      : metadata_(std::move(other.metadata_)), handle_(std::move(other.handle_))
  {
  }
  GpuPreparedProgramOwner &operator=(GpuPreparedProgramOwner &&other) noexcept
  {
    if (this != &other) {
      reset();
      metadata_ = std::move(other.metadata_);
      handle_ = std::move(other.handle_);
    }
    return *this;
  }
  ~GpuPreparedProgramOwner() = default;  // Reverse member order: handle, then metadata.
  explicit operator bool() const
  {
    return bool(handle_);
  }
  auto get() const
  {
    return handle_.get();
  }
  void retain(std::unique_ptr<Metadata> value)
  {
    if (handle_)
      throw std::logic_error("Cannot replace pending shader metadata");
    metadata_ = std::move(value);
  }
  Metadata *metadata() const
  {
    return metadata_.get();
  }
  void submit(Handle value)
  {
    if (handle_)
      throw std::logic_error("Shader program already submitted");
    handle_ = std::move(value);
  }
  void reset()
  {
    handle_.reset();
    metadata_.reset();
  }
};
}  // namespace bts
