#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace bts {
struct HistorySpillResult {
 std::uint64_t allocated=0,resident_before=0,resident_after=0,written=0,budget=0;
 std::string error;
};
std::uint64_t allocated_history_bytes();
bool request_history_spill(std::uint64_t budget);
bool history_spill_pending();
std::optional<HistorySpillResult> poll_history_spill();
// Explicit tests/shutdown only. Interactive callers use the nonblocking poll.
std::optional<HistorySpillResult> wait_history_spill();
}
