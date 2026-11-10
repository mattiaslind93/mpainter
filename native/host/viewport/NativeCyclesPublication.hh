// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
namespace blender {
struct ImBuf;
}
namespace bts {
inline std::string_view native_cycles_failure(std::uint64_t failed,
                                              std::uint64_t current,
                                              std::string_view detail) noexcept
{
  if (!failed || failed != current)
    return {};
  return detail.empty() ? std::string_view("Live material image preparation failed") : detail;
}
// Only a coordinator-recorded failure may defer Rendering. Unknown exceptions
// retain their original propagation; do not reset/join a capture at this boundary.
template<class Update, class Error, class Report>
bool native_cycles_recorded_error_boundary(bool &reported,
                                           Update &&update,
                                           Error &&error,
                                           Report &&report)
{
  static_assert(std::is_nothrow_invocable_v<Report, std::string_view>);
  if (error().empty())
    reported = false;
  try {
    const bool ready = update();
    if (error().empty())
      reported = false;
    return ready;
  }
  catch (...) {
    const std::string_view detail = error();
    if (detail.empty())
      throw;
    if (!reported) {
      reported = true;
      report(detail);
    }
    return false;
  }
}
// Storage and keeper slots are prepared before entering this owner operation.
// The enclosing batch owns previous even if a publisher fails after mutation.
struct NativeCyclesCacheEntry {
  std::uint32_t image = 0;
  int tile = 1001;
  blender::ImBuf *buffer = nullptr, *previous = nullptr;
};
template<class Publish, class Notify>
void publish_native_cycles_cache(std::span<NativeCyclesCacheEntry> entries,
                                 std::span<const std::uint32_t> images,
                                 bool &attempted,
                                 Publish &&publish,
                                 Notify &&notify)
{
  for (auto &entry : entries) {
    attempted = true;
    if (!publish(entry.image, entry.tile, entry.buffer, &entry.previous))
      throw std::runtime_error("Cycles CPU mirror publication rejected validated native tile");
  }
  for (auto image : images)
    notify(image);
}
}  // namespace bts
