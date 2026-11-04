#include "ExportNormalToBump.hh"
#include <fstream>
#include <iostream>
#include <limits>
using namespace bts;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv){try{
 if(argc==5){const int w=std::stoi(argv[1]),h=std::stoi(argv[2]);std::vector<float> input(std::size_t(w)*h*4);std::ifstream file(argv[3],std::ios::binary);file.read(reinterpret_cast<char *>(input.data()),input.size()*sizeof(float));require(bool(file),"Input truncated");auto out=hybrid_normal_to_bump(input,w,h);std::ofstream output(argv[4],std::ios::binary);output.write(reinterpret_cast<char *>(out.data()),out.size()*sizeof(float));return 0;}
 for(auto [w,h]:{std::pair{1,1},std::pair{32,16},std::pair{37,23}}){
  auto pixels=std::make_shared<std::vector<float>>(w*h*4);for(int i=0;i<w*h;++i){(*pixels)[4*i]=(*pixels)[4*i+1]=.5f;(*pixels)[4*i+2]=(*pixels)[4*i+3]=1;}
  auto flat=hybrid_normal_to_bump(*pixels,w,h);for(float v:flat)require(v==.5f,"Flat map is not neutral");
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){const auto i=(y*w+x)*4;const double p=.3*std::sin(2*std::numbers::pi*x/w),q=.2*std::cos(2*std::numbers::pi*y/h),n=std::sqrt(1+p*p+q*q);(*pixels)[i]=float(-p/n*.5+.5);(*pixels)[i+1]=float(-q/n*.5+.5);(*pixels)[i+2]=float(1/n*.5+.5);}
  auto height=hybrid_normal_to_bump(*pixels,w,h);for(float v:height)require(std::isfinite(v)&&v>=0&&v<=1,"Invalid height");
  ExportNormalContribution source{pixels};auto single=export_normal_bump(std::span(&source,1),w,h);source.strength=.25f;auto weights=std::make_shared<std::vector<float>>(w*h,.4f);source.weights=weights;std::array pair{source,source};auto combined=export_normal_bump(pair,w,h);for(int i=0;i<w*h;++i)require(std::abs(combined[i]-.2f*single[i])<1e-6,"Post-integration normal masks/strength changed");
  auto dx=*pixels;for(int i=0;i<w*h;++i)dx[i*4+1]=1-dx[i*4+1];auto flipped=hybrid_normal_to_bump(dx,w,h,4,true);for(int i=0;i<w*h;++i)require(std::abs(flipped[i]-height[i])<1e-6,"DirectX orientation changed");
 }
 bool rejected=false;try{hybrid_normal_to_bump({},0,0);}catch(const std::invalid_argument &){rejected=true;}require(rejected,"Invalid extent accepted");
 std::atomic_bool cancel=true;rejected=false;try{const std::array<float,4> flat{.5,.5,1,1};hybrid_normal_to_bump(flat,1,1,4,false,&cancel);}catch(const std::runtime_error &){rejected=true;}require(rejected,"Cancellation ignored");
 std::cout<<"Hybrid normal-to-bump: neutral, rectangular/prime FFT, composition, masks, strength, DirectX, validation and cancellation passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
