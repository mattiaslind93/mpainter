// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeHeightImageReference.hh"
#include <cassert>
#include <iostream>
int main(){
 using namespace bts::height_image_reference;
 const std::array<Pixel,4> image{{{-2,7,8,1},{4,7,8,1},{8,7,8,1},{14,7,8,1}}};
 for(bool linear:{false,true})for(bool repeat:{false,true}){
  assert(sample(image,2,2,.25,.25,linear,repeat)==-2);
  assert(sample(image,2,2,.75,.75,linear,repeat)==14);
 }
 assert(sample(image,2,2,.5,.5,true,false)==6);
 assert(sample(image,2,2,.5,.25,true,false)==1);
 assert(sample(image,2,2,.25,.5,true,false)==3);
 assert(sample(image,2,2,0,.25,true,true)==1);
 assert(sample(image,2,2,0,0,true,true)==6);
 assert(sample(image,2,2,-1.75,2.25,false,true)==-2);
 assert(sample(image,2,2,-10,5,true,false)==8);
 assert(sample(image,2,2,1,1,false,false)==14);
 assert(sample(image,2,2,1,1,false,true)==-2);
 const auto owned=fixture_pixels();float low=0,high=0;
 for(int y=0;y<fixture_height;++y)for(int x=0;x<fixture_width;++x){
  const auto value=owned[y*fixture_width+x][0];low=std::min(low,value);high=std::max(high,value);
  assert(sample(owned,fixture_width,fixture_height,(x+.5)/fixture_width,(y+.5)/fixture_height,true,false)==value);
 }
 assert(low<-1&&high>4);
 std::cout<<"Independent raw Height image sampling reference PASS\n";
}
