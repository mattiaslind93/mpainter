#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace bts {
// Test-only boundary. No Cycles layout is exposed to a host-flags TU.
struct CyclesDisplacementCompletion {
 std::uint64_t generation=0,epoch=0,terminal_epoch=0,sequence=0;
 int sample=0;double progress=0;std::string status,error;
};
class CyclesLiveDisplacementProbe {
 struct Impl;std::unique_ptr<Impl> impl_;
 public:
 CyclesLiveDisplacementProbe();~CyclesLiveDisplacementProbe();
 void install_controlled_profile();
 void arm_entry();
 void restore();
 CyclesDisplacementCompletion snapshot()const;
 std::string profile_json()const;
 std::string trace_json()const;
 // Optional test-only witness. Arm BEFORE the new entry/pixel edit. The copy
 // happens in that live Session's first matching terminal callback; no cached
 // Cycles pointer is dereferenced later by the artist/Qt thread.
 void arm_geometry_capture(std::uint64_t generation,std::uint64_t epoch);
 std::string geometry_json()const;
};
}
