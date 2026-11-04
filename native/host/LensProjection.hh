#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
namespace bts {
// Coordinates use either top-left or bottom-left normalized image space; the
// radial and border equations are symmetric in Y. Matches Blender's radial
// compositor with dispersion=0 and Fit=false (pin fbe6228777e7).
struct LensProjection {
 using Point=std::array<double,2>;
 bool enabled=false;double distortion=0,roundness=0,smoothness=0;
 static double coefficient(double power,double fov){const auto angle=std::tan(fov*std::acos(-1.)/360)/std::tan(std::acos(-1.)/8);return std::clamp((power-1)*.25*angle*angle,-.35,.35);}
 double alpha(Point p)const{
  if(!enabled)return 1;
  const double r=roundness,qx=std::abs(p[0]-.5)-(.5-r),qy=std::abs(p[1]-.5)-(.5-r),distance=std::hypot(std::max(qx,0.),std::max(qy,0.))+std::min(std::max(qx,qy),0.)-r;
  if(smoothness<=0)return distance<0?1:0;
  const auto t=std::clamp((distance+.005*smoothness)/(.005*smoothness),0.,1.);return 1-t*t*(3-2*t);
 }
 std::optional<Point> to_source(Point p)const{
  if(!std::isfinite(p[0])||!std::isfinite(p[1]))return {};
  if(!enabled)return p;
  if(p[0]<0||p[0]>1||p[1]<0||p[1]>1||alpha(p)<=0)return {};
  const double scale=1/(1+distortion),x=(p[0]*2-1)*scale,y=(p[1]*2-1)*scale,r2=x*x+y*y,radicand=1-4*distortion*r2;if(radicand<0)return {};
  const double factor=1/(1+std::sqrt(radicand));Point source{x*factor+.5,y*factor+.5};if(source[0]<0||source[0]>1||source[1]<0||source[1]>1)return {};return source;
 }
 std::optional<Point> to_display(Point p)const{
  if(!std::isfinite(p[0])||!std::isfinite(p[1]))return {};
  if(!enabled)return p;
  const double x=p[0]-.5,y=p[1]-.5,r2=x*x+y*y,denominator=1+4*distortion*r2;if(denominator<=0||4*distortion*r2>1)return {};
  const double scale=1/(1+distortion);return Point{x/(denominator*scale)+.5,y/(denominator*scale)+.5};
 }
};
}
