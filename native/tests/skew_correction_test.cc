// SPDX-License-Identifier: GPL-3.0-or-later
#include "SkewCorrection.hh"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){using namespace bts;const SkewNormal average{.6f,0,.8f},face{0,0,1};
  assert(skew_projection_normal(average,face,0)==average);assert(skew_projection_normal(average,face,1)==face);
  const auto middle=skew_projection_normal(average,face,.5);assert(middle[0]>0&&middle[0]<average[0]&&middle[2]>.8);assert(std::abs(middle[0]*middle[0]+middle[2]*middle[2]-1)<1e-6);
  assert(skew_projection_normal(average,face,-1)==average);assert(skew_projection_normal(average,face,2)==face);assert(skew_projection_normal(average,face,NAN)==average);
  assert(skew_edge_weight(1,0,.1,0)==0);assert(skew_edge_weight(.7,.1,.1,0)==.7f);assert(skew_edge_weight(1,.05,.1,1)<skew_edge_weight(1,.05,.1,0));assert(skew_edge_weight(1,.05,.1,-1)>skew_edge_weight(1,.05,.1,0));assert(skew_edge_weight(.5,0,0,0)==.5f);assert(skew_edge_weight(1,NAN,.1,0)==0);assert(skew_edge_weight(.7,INFINITY,.1,0)==.7f);
  std::cout<<"Skew projection endpoints/unit interpolation/clamping/nonfinite guard and geometry-edge protection falloff passed\n";
}
