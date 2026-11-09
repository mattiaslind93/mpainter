#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace bts {
struct BrushJitter {
  float size = 0, flow = 0, angle = 0, position = 0;
  std::array<float, 4> values() const
  {
    return {size, flow, angle, position};
  }
  void validate() const
  {
    const auto fields = values();
    for (int i = 0; i < 4; ++i)
      if (!std::isfinite(fields[i]) || fields[i] < 0 || fields[i] > (i == 2 ? 180 : 100))
        throw std::invalid_argument("Invalid brush jitter");
  }
};
struct JitterDab {
  double size = 1, flow = 1, angle = 0;
  std::array<double, 2> position{};
};
class JitterSampler {
 public:
  explicit JitterSampler(std::uint64_t seed = 0) : state_(seed) {}
  void reset(std::uint64_t seed)
  {
    state_ = seed;
  }
  JitterDab sample(const BrushJitter &settings)
  {
    settings.validate();
    if (settings.size == 0 && settings.flow == 0 && settings.angle == 0 && settings.position == 0)
      return {};
    // Every active dab consumes the same draws, keeping the four controls independent.
    const double size = unit(), flow = unit(), angle = unit(), radius = std::sqrt(unit()),
                 direction = unit() * tau;
    return {1 - settings.size / 100 * (1 - size),
            1 - settings.flow / 100 * (1 - flow),
            (2 * angle - 1) * settings.angle * tau / 360,
            {radius * std::cos(direction) * settings.position / 100,
             radius * std::sin(direction) * settings.position / 100}};
  }

 private:
  static constexpr double tau = 6.2831853071795864769;
  std::uint64_t state_;
  double unit()
  {
    auto value = (state_ += 0x9e3779b97f4a7c15ULL);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return double((value ^ (value >> 31)) >> 11) * 0x1.0p-53;
  }
};
}  // namespace bts
