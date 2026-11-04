#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace bts {
struct DynamicStrokeParameters {
  int cycle=32;
  bool random_start=false;
  float roundness=.5f,width=.1f;
  double phase=0; // Resolved per dab; source controls remain unchanged.
  void validate() const {if(cycle<1||cycle>1024||!std::isfinite(roundness)||roundness<0||roundness>1||!std::isfinite(width)||width<.01f||width>1||!std::isfinite(phase)||phase<0||phase>=1)throw std::invalid_argument("Invalid dynamic outline parameters");}
  float sample(double u,double v) const {
    constexpr double tau=6.2831853071795864769;
    const double angle=std::sin(phase*tau)*.35,ca=std::cos(angle),sa=std::sin(angle),x=u*ca+v*sa,y=v*ca-u*sa;
    const double morph=std::clamp(double(roundness)+.5*std::cos(phase*tau),0.,1.);
    const double exponent=2+10*morph,radius=std::pow(std::pow(std::abs(x),exponent)+std::pow(std::abs(y),exponent),1/exponent);
    const double distance=std::abs(radius-.75),edge=.008;
    const double t=std::clamp((double(width)*.5+edge-distance)/(2*edge),0.,1.);return float(t*t*(3-2*t));
  }
};
class DynamicStrokeSampler {
 public:
  explicit DynamicStrokeSampler(std::uint64_t seed=0):seed_(seed){}
  void reset(std::uint64_t seed){seed_=seed;index_=0;}
  double phase(const DynamicStrokeParameters &settings){
    settings.validate();std::uint64_t value=seed_+0x9e3779b97f4a7c15ULL;value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;value=(value^(value>>27))*0x94d049bb133111ebULL;value^=value>>31;
    const auto start=settings.random_start?value%settings.cycle:0;return double((start+index_++)%settings.cycle)/settings.cycle;
  }
 private:
  std::uint64_t seed_=0,index_=0;
};
}
