#pragma once
// Test-only opaque ABI boundary: its TU uses the exact frozen Cycles image.cpp
// command, never host/Qt flags. Image must remain alive until the probe retires.
#include <array>
#include <memory>
#include <vector>
namespace blender {struct Image;}
namespace bts {
struct MaterialLayerSourceSamples {int width=0,height=0;bool data=false;std::vector<std::array<float,4>> pixels;};
class MaterialLayeringCyclesSourceProbe {
 public:
  explicit MaterialLayeringCyclesSourceProbe(blender::Image &,int tile=1001);
  ~MaterialLayeringCyclesSourceProbe();
  MaterialLayeringCyclesSourceProbe(const MaterialLayeringCyclesSourceProbe &)=delete;
  MaterialLayeringCyclesSourceProbe &operator=(const MaterialLayeringCyclesSourceProbe &)=delete;
  MaterialLayerSourceSamples read();
  bool immutable_snapshot()const;
  void release_on_worker();
 private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
