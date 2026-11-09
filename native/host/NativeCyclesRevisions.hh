// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bts {
// Immutable CPU-only identity. Allocation addresses are meaningful only while
// owner-side plain references keep those allocations alive (never save pins).
struct NativeCyclesTileIdentity {
  int number = 0, x = 0, y = 0, layer = 0, width = 0, height = 0, generated_width = 0,
      generated_height = 0;
  bool operator==(const NativeCyclesTileIdentity &) const = default;
};
struct NativeCyclesImageIdentity {
  std::uintptr_t main = 0, allocation = 0;
  std::uint32_t image = 0, material = 0;
  int role = -1, format = 0, width = 0, height = 0, layers = 0, mips = 1, source = 0, type = 0,
      alpha_mode = 0;
  bool data = false;
  std::string declared_space, working_space, buffer_space;
  std::array<std::uint32_t, 9> working_matrix{};
  std::vector<NativeCyclesTileIdentity> tiles;  // Strictly sorted, complete set.
  bool operator==(const NativeCyclesImageIdentity &) const = default;
};
inline bool native_cycles_valid_identity(const NativeCyclesImageIdentity &id) noexcept
{
  if (!id.main || !id.allocation || !id.image || !id.material || id.width < 1 || id.height < 1 ||
      id.layers < 1 || id.mips < 1 || id.tiles.empty() || id.buffer_space.empty())
    return false;
  int previous = 0;
  for (const auto &tile : id.tiles) {
    if (tile.number <= previous || tile.x < 0 || tile.y < 0 || tile.layer < 0 ||
        tile.layer >= id.layers || tile.width < 1 || tile.height < 1 ||
        tile.generated_width != tile.width || tile.generated_height != tile.height ||
        std::int64_t(tile.x) + tile.width > id.width ||
        std::int64_t(tile.y) + tile.height > id.height)
      return false;
    previous = tile.number;
  }
  return true;
}
struct NativeCyclesRevision {
  std::uint64_t wanted = 0, published = 0, floor = 0, entry_epoch = 0;
  bool operator==(const NativeCyclesRevision &) const = default;
};
enum class NativeCyclesCompletionAction { Continue, Wait, Consume, Paused };
inline NativeCyclesCompletionAction native_cycles_completion_action(bool paused,
                                                                    bool worker,
                                                                    bool ready,
                                                                    bool retirement_ready) noexcept
{
  // A same-generation failure may stop new work, never skip a pending drain.
  if (worker)
    return ready && retirement_ready ? NativeCyclesCompletionAction::Consume :
                                       NativeCyclesCompletionAction::Wait;
  return paused ? NativeCyclesCompletionAction::Paused : NativeCyclesCompletionAction::Continue;
}
inline std::uint64_t native_cycles_next_serial(std::uint64_t value)
{
  if (value == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("Cycles image revision counter exhausted");
  return value + 1;
}
// Called only after producer readiness and a complete required identity have
// been established. No moving floor on ordinary later pixel writes.
inline void native_cycles_require(NativeCyclesRevision &state,
                                  std::uint64_t epoch,
                                  std::uint64_t revision,
                                  bool identity_changed) noexcept
{
  if (identity_changed)
    state.published = 0;
  state.wanted = revision;
  state.floor = revision;
  state.entry_epoch = epoch;
}
inline bool native_cycles_revision_ready(const NativeCyclesRevision &state,
                                         std::uint64_t epoch) noexcept
{
  return epoch && state.entry_epoch == epoch && state.floor && state.published >= state.floor &&
         state.published <= state.wanted;
}
inline bool native_cycles_publishable(const NativeCyclesRevision &state,
                                      std::uint64_t epoch,
                                      std::uint64_t captured_epoch,
                                      std::uint64_t captured_revision,
                                      const NativeCyclesImageIdentity &required,
                                      const NativeCyclesImageIdentity &captured,
                                      bool active) noexcept
{
  return active && native_cycles_valid_identity(required) &&
         native_cycles_valid_identity(captured) && required == captured && epoch &&
         captured_epoch == epoch && state.entry_epoch == epoch && state.floor &&
         captured_revision >= state.floor && captured_revision > state.published &&
         captured_revision <= state.wanted;
}
// The caller has already validated every image and prepared all commit slots.
// This must remain allocation-free/noexcept after the first cache mutation.
inline void native_cycles_commit_revision(NativeCyclesRevision &state,
                                          std::uint64_t captured) noexcept
{
  state.published = captured;  // wanted may already refer to a newer GPU write.
}

// A GPU-owner-only guard. It is not a pin, does not trigger COW, and must never
// enter a worker capture/result or CPU retirement batch. Tests use fake refs;
// the production specialization calls plain GPU_texture_ref/free on the owner.
template<class T> class NativeCyclesOwnerReference {
  T *value_ = nullptr;
  void (*release_)(T *) = nullptr;

 public:
  NativeCyclesOwnerReference() = default;
  NativeCyclesOwnerReference(T *value, void (*retain)(T *), void (*release)(T *)) noexcept
      : value_(value), release_(release)
  {
    if (value_)
      retain(value_);
  }
  NativeCyclesOwnerReference(const NativeCyclesOwnerReference &) = delete;
  NativeCyclesOwnerReference &operator=(const NativeCyclesOwnerReference &) = delete;
  NativeCyclesOwnerReference(NativeCyclesOwnerReference &&other) noexcept
      : value_(std::exchange(other.value_, nullptr)), release_(other.release_)
  {
  }
  NativeCyclesOwnerReference &operator=(NativeCyclesOwnerReference &&other) noexcept
  {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
      release_ = other.release_;
    }
    return *this;
  }
  ~NativeCyclesOwnerReference()
  {
    reset();
  }
  void reset() noexcept
  {
    if (auto *value = std::exchange(value_, nullptr))
      release_(value);
  }
  T *get() const noexcept
  {
    return value_;
  }
};
}  // namespace bts
