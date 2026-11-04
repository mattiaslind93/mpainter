#include "ResourceAvailability.hh"
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
namespace bts {
struct StrokeSample {
  std::array<double,2> position{};
  double pressure=1;
  std::array<double,2> tilt{};
};
// Non-uniform Hermite form of Catmull-Rom, with centripetal (sqrt-distance)
// knots. Blender's curve_catmull_rom.cc uses the uniform basis; keeping explicit
// knots here prevents loops/overshoot at unevenly spaced pointer samples.
class StrokeCurve {
  std::array<StrokeSample,4> points_;
  std::array<double,3> intervals_;
  double component(double a,double b,double c,double d,double u)const {
    const auto [h0,h1,h2]=intervals_;
    const double m1=h1*((b-a)/h0-(c-a)/(h0+h1)+(c-b)/h1);
    const double m2=h1*((c-b)/h1-(d-b)/(h1+h2)+(d-c)/h2);
    const double u2=u*u,u3=u2*u;
    return (2*u3-3*u2+1)*b+(u3-2*u2+u)*m1+(-2*u3+3*u2)*c+(u3-u2)*m2;
  }
 public:
  explicit StrokeCurve(std::array<StrokeSample,4> points):points_(points) {
    for(int i=0;i<3;++i)intervals_[i]=std::sqrt(std::max(1e-8,std::hypot(points[i+1].position[0]-points[i].position[0],points[i+1].position[1]-points[i].position[1])));
  }
  StrokeSample at(double u)const {
    u=std::clamp(u,0.,1.);StrokeSample value;
    for(int c=0;c<2;++c){value.position[c]=component(points_[0].position[c],points_[1].position[c],points_[2].position[c],points_[3].position[c],u);value.tilt[c]=std::clamp(component(points_[0].tilt[c],points_[1].tilt[c],points_[2].tilt[c],points_[3].tilt[c],u),-90.,90.);}
    value.pressure=std::clamp(component(points_[0].pressure,points_[1].pressure,points_[2].pressure,points_[3].pressure,u),0.,1.);return value;
  }
  // Adaptive polyline approximates arc length to subpixel precision. Pressure
  // and tilt also constrain subdivision, including on a straight spatial curve.
  template<class Emit> void flatten(Emit output)const {

    const auto visit=[&](auto &&self,double u0,const StrokeSample &a,double u1,const StrokeSample &b,int depth)->void {
      const double middle=(u0+u1)*.5;const auto m=at(middle);bool split=std::hypot(b.position[0]-a.position[0],b.position[1]-a.position[1])>4;
      for(double fraction:{.25,.5,.75}){const auto p=at(std::lerp(u0,u1,fraction));split|=std::hypot(p.position[0]-std::lerp(a.position[0],b.position[0],fraction),p.position[1]-std::lerp(a.position[1],b.position[1],fraction))>.05;split|=std::abs(p.pressure-std::lerp(a.pressure,b.pressure,fraction))>.001;for(int c=0;c<2;++c)split|=std::abs(p.tilt[c]-std::lerp(a.tilt[c],b.tilt[c],fraction))>.1;}
      if(split&&middle>u0&&middle<u1){self(self,u0,a,middle,m,depth+1);self(self,middle,m,u1,b,depth+1);}
      else {output(b);}
    };
    visit(visit,0,at(0),1,at(1),0);
  }
};
class StrokeSpline {
  StrokeSample previous_,from_,to_;bool previous_valid_=false,to_valid_=false;
  static StrokeSample reflect(const StrokeSample &point,const StrokeSample &other){auto result=point;for(int c=0;c<2;++c){result.position[c]=2*point.position[c]-other.position[c];result.tilt[c]=2*point.tilt[c]-other.tilt[c];}result.pressure=2*point.pressure-other.pressure;return result;}
  static void validate(const StrokeSample &sample){for(double p:sample.position)if(!std::isfinite(p))throw std::invalid_argument("Invalid stroke spline position");if(!std::isfinite(sample.pressure)||sample.pressure<0||sample.pressure>1)throw std::invalid_argument("Invalid stroke spline pressure");for(double tilt:sample.tilt)if(!std::isfinite(tilt)||std::abs(tilt)>90)throw std::invalid_argument("Invalid stroke spline tilt");}
 public:
  void reset(StrokeSample point){validate(point);from_=point;previous_valid_=to_valid_=false;}
  std::optional<StrokeCurve> append(StrokeSample point){
    validate(point);auto &last=to_valid_?to_:from_;
    if(std::hypot(point.position[0]-last.position[0],point.position[1]-last.position[1])<1e-6){last=point;return {};}
    if(!to_valid_){to_=point;to_valid_=true;return {};}
    StrokeCurve result({previous_valid_?previous_:reflect(from_,to_),from_,to_,point});previous_=from_;from_=to_;to_=point;previous_valid_=true;return result;
  }
  // Present the unfinished segment immediately. Its raster is replaced (never
  // overpainted) when a future sample supplies the exact outgoing tangent.
  std::optional<StrokeCurve> tail()const {if(!to_valid_)return {};return StrokeCurve({previous_valid_?previous_:reflect(from_,to_),from_,to_,reflect(to_,from_)});}
};
}
