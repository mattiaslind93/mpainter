#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <span>
#include <stdexcept>
#include <vector>

namespace bts {
namespace hybrid_bump_detail {
using Complex=std::complex<double>;
inline void check_cancel(const std::atomic_bool *cancel){if(cancel&&cancel->load())throw std::runtime_error("Normal to bump conversion cancelled");}
// Radix-2 FFT, with Bluestein convolution for arbitrary image dimensions.
class FFT {
 std::size_t n_,m_=1;
 std::vector<Complex> roots_,chirp_,kernel_;
 void radix(std::vector<Complex> &a,bool inverse)const{
  const auto n=a.size();for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
  for(std::size_t len=2;len<=n;len*=2)for(std::size_t i=0;i<n;i+=len)for(std::size_t j=0;j<len/2;++j){const auto w=roots_[j*(m_/len)];const auto u=a[i+j],v=a[i+j+len/2]*(inverse?std::conj(w):w);a[i+j]=u+v;a[i+j+len/2]=u-v;}
  if(inverse)for(auto &v:a)v/=double(n);
 }
 public:
 explicit FFT(std::size_t n):n_(n){
  if(!n)throw std::invalid_argument("Empty FFT");
  const bool power=(n&(n-1))==0;while(m_<(power?n:2*n-1))m_*=2;
  roots_.resize(m_/2);for(std::size_t i=0;i<roots_.size();++i)roots_[i]=std::polar(1.,-2*std::numbers::pi*i/m_);
  if(!power){chirp_.resize(n);kernel_.resize(m_);for(std::size_t i=0;i<n;++i){chirp_[i]=std::polar(1.,-std::numbers::pi*double((i*i)%(2*n))/n);kernel_[i]=std::conj(chirp_[i]);if(i)kernel_[m_-i]=kernel_[i];}radix(kernel_,false);}
 }
 void forward(std::vector<Complex> &a)const{
  if(chirp_.empty()){radix(a,false);return;}
  std::vector<Complex> work(m_);for(std::size_t i=0;i<n_;++i)work[i]=a[i]*chirp_[i];radix(work,false);for(std::size_t i=0;i<m_;++i)work[i]*=kernel_[i];radix(work,true);for(std::size_t i=0;i<n_;++i)a[i]=work[i]*chirp_[i];
 }
 void transform(std::vector<Complex> &a,bool inverse)const{if(inverse)for(auto &v:a)v=std::conj(v);forward(a);if(inverse)for(auto &v:a)v=std::conj(v)/double(n_);}
};
inline void fft2(std::vector<Complex> &a,int width,int height,bool inverse,const std::atomic_bool *cancel){
 FFT rows(width),cols(height);std::vector<Complex> line(width);
 for(int y=0;y<height;++y){check_cancel(cancel);std::copy_n(a.begin()+std::size_t(y)*width,width,line.begin());rows.transform(line,inverse);std::copy(line.begin(),line.end(),a.begin()+std::size_t(y)*width);}
 line.resize(height);for(int x=0;x<width;++x){check_cancel(cancel);for(int y=0;y<height;++y)line[y]=a[std::size_t(y)*width+x];cols.transform(line,inverse);for(int y=0;y<height;++y)a[std::size_t(y)*width+x]=line[y];}
}
inline double percentile98(std::vector<double> values){
 const double index=.98*(values.size()-1);const auto lo=std::size_t(index),hi=std::size_t(std::ceil(index));std::nth_element(values.begin(),values.begin()+lo,values.end());const double a=values[lo];if(lo==hi)return a;const double b=*std::min_element(values.begin()+hi,values.end());return a+(b-a)*(index-lo);
}
inline void normalize(std::vector<double> &values,bool center){
 const auto [lo,hi]=std::minmax_element(values.begin(),values.end());const double low=*lo,range=*hi-low;
 if(range<=1e-6){if(center)std::fill(values.begin(),values.end(),.5);return;}
 for(auto &v:values)v=(v-low)/range;
 if(center){const double shift=.5-std::accumulate(values.begin(),values.end(),0.)/values.size();for(auto &v:values)v=std::clamp(v+shift,0.,1.);}
}
// scipy.ndimage.gaussian_filter default: sigma=5, truncate=4, half-sample reflect.
inline std::vector<double> gaussian(const std::vector<double> &a,int w,int h,const std::atomic_bool *cancel){
 constexpr int radius=20;double weights[41],total=0;for(int k=-radius;k<=radius;++k)total+=(weights[k+radius]=std::exp(-.5*k*k/25.));for(auto &v:weights)v/=total;
 const auto reflect=[](int x,int size){x%=2*size;if(x<0)x+=2*size;return x<size?x:2*size-1-x;};
 std::vector<double> temp(a.size()),out(a.size());
 for(int y=0;y<h;++y){check_cancel(cancel);for(int x=0;x<w;++x)for(int k=-radius;k<=radius;++k)temp[std::size_t(y)*w+x]+=a[std::size_t(reflect(y+k,h))*w+x]*weights[k+radius];}
 for(int y=0;y<h;++y){check_cancel(cancel);for(int x=0;x<w;++x)for(int k=-radius;k<=radius;++k)out[std::size_t(y)*w+x]+=temp[std::size_t(y)*w+reflect(x+k,w)]*weights[k+radius];}
 return out;
}
}
// Mattias Lind's hybrid_normal_to_bump defaults. Input is encoded OpenGL tangent
// RGB(A), in Blender bottom-up row order. Output is scalar, neutral at 0.5.
// Integration is periodic (as in the reference), independently per UDIM tile.
inline std::vector<float> hybrid_normal_to_bump(std::span<const float> pixels,int width,int height,int stride=4,bool directx=false,const std::atomic_bool *cancel=nullptr){
 using namespace hybrid_bump_detail;
 if(width<=0||height<=0||stride<3||pixels.size()!=std::size_t(width)*height*stride)throw std::invalid_argument("Invalid normal image extent");
 const auto count=std::size_t(width)*height;std::vector<Complex> p(count),q(count);std::vector<double> dot(count),magnitude(count);
 for(std::size_t i=0;i<count;++i){if((i&65535)==0)check_cancel(cancel);double x=pixels[i*stride]*2.-1,y=(pixels[i*stride+1]*2.-1)*(directx?-1 : 1),z=pixels[i*stride+2]*2.-1;if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))throw std::invalid_argument("Non-finite normal map");
  // Four uniformly spaced azimuths: exact mean and population std of the
  // reference's dot products at 90, 45 and 20 degrees (no normal renormalizing).
  dot[i]=2*std::cos(20*std::numbers::pi/180)*std::sqrt((x*x+y*y)/2)+.5*(std::sin(std::numbers::pi/4)-1)*z;
  if(std::abs(z)<1e-6)z=1e-6;p[i]=-x/z;q[i]=-y/z;magnitude[i]=std::abs(p[i].real());
 }
 const double pmax=percentile98(std::move(magnitude));magnitude.resize(count);for(std::size_t i=0;i<count;++i)magnitude[i]=std::abs(q[i].real());const double qmax=percentile98(std::move(magnitude));
 for(std::size_t i=0;i<count;++i){p[i]=std::clamp(p[i].real(),-pmax,pmax);q[i]=std::clamp(q[i].real(),-qmax,qmax);}
 fft2(p,width,height,false,cancel);fft2(q,width,height,false,cancel);
 for(int y=0;y<height;++y){const double wy=2*std::numbers::pi*(y<(height+1)/2?y:y-height)/height;for(int x=0;x<width;++x){const double wx=2*std::numbers::pi*(x<(width+1)/2?x:x-width)/width;const auto i=std::size_t(y)*width+x;p[i]=i?Complex(0,-1)*(wx*p[i]+wy*q[i])/(wx*wx+wy*wy):Complex{};}}
 std::vector<Complex>().swap(q);fft2(p,width,height,true,cancel);std::vector<double> fc(count);for(std::size_t i=0;i<count;++i)fc[i]=p[i].real();std::vector<Complex>().swap(p);
 // Unit frequency-band scales telescope to the original FC result.
 normalize(fc,true);for(auto &v:fc)v=(v-.5)*.75+.5;
 normalize(dot,false);for(auto &v:dot)v=1-v;auto low=gaussian(dot,width,height,cancel);double mean=0;for(std::size_t i=0;i<count;++i){dot[i]-=low[i];mean+=dot[i];}mean/=count;
 for(std::size_t i=0;i<count;++i)fc[i]+=.12*(dot[i]-mean);
 normalize(fc,true);const double final_mean=std::accumulate(fc.begin(),fc.end(),0.)/count;if(std::abs(final_mean-.5)>.01)for(auto &v:fc)v=std::clamp(v-final_mean+.5,0.,1.);
 return std::vector<float>(fc.begin(),fc.end());
}
}
