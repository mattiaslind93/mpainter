#include "HeightNormal.hh"
#ifdef NDEBUG
# undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
  assert(!bts::bump_normal_coverage(0)&&!bts::bump_normal_coverage(std::numeric_limits<float>::denorm_min())&&bts::bump_normal_coverage(std::numeric_limits<float>::min())&&bts::bump_normal_coverage(1));
  const auto plane=[](int x,int y,int){return .002f*x-.001f*y;};
  const auto sharp=bts::height_normal(bts::BumpMethod::Sharp,128,20,20,1001,plane),smooth=bts::height_normal(bts::BumpMethod::SmoothSobel,128,20,20,1001,plane);
  assert(sharp[0]<0&&sharp[1]>0);for(int c=0;c<3;++c)assert(std::abs(sharp[c]-smooth[c])<1e-5);
  const auto constant=[](int,int,int){return .2f;};assert((bts::height_normal(bts::BumpMethod::Sharp,128,20,20,1001,constant)==bts::NormalVector{0,0,1}));
  const auto spike=[](int x,int y,int){return x==0&&y==0?.01f:0.f;};const auto a=bts::height_normal(bts::BumpMethod::Sharp,128,1,0,1001,spike),b=bts::height_normal(bts::BumpMethod::SmoothSobel,128,1,0,1001,spike);assert(a[0]>b[0]&&b[0]>0);
  const auto diagonal_a=bts::height_normal(bts::BumpMethod::Sharp,128,1,1,1001,spike),diagonal_b=bts::height_normal(bts::BumpMethod::SmoothSobel,128,1,1,1001,spike);assert(diagonal_a[0]==0&&diagonal_b[0]>0&&diagonal_b[1]>0);
  std::puts("height normals: planar/constant invariants, tangent slope sign, sharp impulse strength and Sobel diagonal smoothing PASS");
}
