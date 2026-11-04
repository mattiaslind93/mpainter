#pragma once
#include "CyclesLiveCompletionProbe.hh" // Reuse only the opaque value record.
#include <memory>
namespace bts {
// Observation-only v2; no settings-write path.
class CyclesDefaultCompletionProbe {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 CyclesDefaultCompletionProbe();~CyclesDefaultCompletionProbe();
 void install_observation_only();
 void arm_entry();void restore();CyclesCompletion snapshot()const;
 std::string profile_json()const;std::string trace_json()const;
};
}
