#pragma once
#include <algorithm>
#include <memory>
#include <functional>
#include <array>
#include <cmath>
#include <vector>
#include <stdexcept>
namespace bts {
class PaintPixels;
struct SmudgePayload;
struct SmudgePayloadRecipe {
 enum class Kind {Capture,Carry};
 const Kind kind;
 explicit SmudgePayloadRecipe(Kind type):kind(type){}
 virtual ~SmudgePayloadRecipe()=default;
 virtual const SmudgePayload &evaluated()const=0;
 virtual bool has_ink(int channel)const=0;
 virtual void cpu_sources(const std::function<void(const PaintPixels &)> &)const{}
};
// Avoid recursive destruction of a long capture/carry authoring graph.
inline void retire_smudge_recipe(std::shared_ptr<const SmudgePayloadRecipe> recipe){
 static thread_local bool draining=false;
 static thread_local std::vector<std::shared_ptr<const SmudgePayloadRecipe>> retired;
 retired.push_back(std::move(recipe));if(draining)return;draining=true;
 while(!retired.empty()){auto item=std::move(retired.back());retired.pop_back();item.reset();}
 draining=false;
}
struct SmudgePayload {
  using Pixel=std::array<float,4>;
  int side=0;
  std::shared_ptr<const SmudgePayloadRecipe> recipe;
  ~SmudgePayload(){if(recipe)retire_smudge_recipe(std::move(recipe));}
  bool has_ink(int channel)const{return recipe?recipe->has_ink(channel):std::any_of(channels[channel].begin(),channels[channel].end(),[](const Pixel &p){return p[3]>0;});}
  const SmudgePayload &evaluated()const{return recipe?recipe->evaluated():*this;}
  std::array<std::vector<Pixel>,5> channels;
  std::vector<float> pickup;
  void validate() const {
    if(side<1||side>128)throw std::invalid_argument("Invalid smudge payload size");
    if(recipe)return;
    for(const auto &channel:channels){if(!channel.empty()&&channel.size()!=std::size_t(side)*side)throw std::invalid_argument("Incomplete smudge payload");for(const auto &pixel:channel){for(float value:pixel)if(!std::isfinite(value)||std::abs(value)>65504)throw std::invalid_argument("Invalid smudge source pixel");if(pixel[3]<0||pixel[3]>1)throw std::invalid_argument("Invalid smudge source coverage");}}
  }
  Pixel sample(int channel,double u,double v) const {
    if(recipe)return recipe->evaluated().sample(channel,u,v);
    const auto &pixels=channels[channel];if(side<1||pixels.empty()||std::abs(u)>1||std::abs(v)>1)return {};
    const double x=(u+1)*.5*side-.5,y=(v+1)*.5*side-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const auto at=[&](int x,int y)->const Pixel &{return pixels[std::size_t(std::clamp(y,0,side-1))*side+std::clamp(x,0,side-1)];};
    Pixel result;for(int c=0;c<4;++c)result[c]=std::lerp(std::lerp(at(ix,iy)[c],at(ix+1,iy)[c],float(x-ix)),std::lerp(at(ix,iy+1)[c],at(ix+1,iy+1)[c],float(x-ix)),float(y-iy));return result;
  }
};
}
