#pragma once
#include <cstdint>
#include <string>
namespace bts {
// Owner-thread delivery. Worker exceptions are read only after future.get().
inline std::string pending_composite_error;
inline std::uint64_t composite_failed_jobs = 0, composite_split_jobs = 0;
}  // namespace bts
