#pragma once
#include <array>
#include <cmath>
#include <stdexcept>
namespace bts {
// Taut-string follower. Release uses the same filter, never finishes the remaining gap.
class LazyMouse {
 public:
  using Point=std::array<double,2>;
  void reset(Point point){validate(point);position_=point;}
  Point follow(Point input,double radius) {
    validate(input);if(!std::isfinite(radius)||radius<0)throw std::invalid_argument("Invalid lazy mouse radius");
    const double dx=input[0]-position_[0],dy=input[1]-position_[1],length=std::hypot(dx,dy);
    if(length>radius){const double t=(length-radius)/length;position_[0]+=dx*t;position_[1]+=dy*t;}
    return position_;
  }
 private:
  Point position_{};
  static void validate(Point p){for(double v:p)if(!std::isfinite(v))throw std::invalid_argument("Invalid lazy mouse position");}
};
}
