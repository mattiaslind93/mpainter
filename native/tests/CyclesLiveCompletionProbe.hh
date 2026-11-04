#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace bts {
// Test-only boundary. No Cycles layout is exposed to a host-flags TU.
struct CyclesCompletion {
 std::uint64_t generation=0,epoch=0,terminal_epoch=0,sequence=0;
 int sample=0;double progress=0;std::string status,error;
};
class CyclesLiveCompletionProbe {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 CyclesLiveCompletionProbe();~CyclesLiveCompletionProbe();
 void install_controlled_profile();
 void arm_entry();
 void restore();
 CyclesCompletion snapshot()const;
 std::string profile_json()const;
 std::string trace_json()const;
};
}
