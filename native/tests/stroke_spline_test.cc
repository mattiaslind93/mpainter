// SPDX-License-Identifier: GPL-3.0-or-later
#include "StrokeSpline.hh"
#include "StrokeSpacing.hh"
#include <cassert>
#include <iostream>
#include <vector>
using namespace bts;
int main(){
 const StrokeSample a{{0,0},.1,{-30,0}},b{{20,0},.3,{-10,10}},c{{28,24},.8,{25,40}},d{{60,28},1,{40,60}};
 StrokeCurve curve({a,b,c,d});assert(curve.at(0).position==b.position&&curve.at(1).position==c.position);
 const auto middle=curve.at(.5);assert(std::hypot(middle.position[0]-24,middle.position[1]-12)>.25);
 double length=0;auto last=curve.at(0);int count=0;curve.flatten([&](auto sample){assert(sample.pressure>=0&&sample.pressure<=1);assert(std::hypot(sample.position[0]-last.position[0],sample.position[1]-last.position[1])<=4.01);assert(std::abs(sample.pressure-last.pressure)<.12);length+=std::hypot(sample.position[0]-last.position[0],sample.position[1]-last.position[1]);last=sample;++count;});assert(count>8&&length>std::hypot(8,24)&&last.position==c.position);
 StrokeSpacing spacing;spacing.reset(b.position,2);std::vector<StrokeSpacing::Point> dabs;curve.flatten([&](auto sample){spacing.advance(sample.position,[&](auto point){dabs.push_back(point);return 2.;});});assert(dabs.size()==std::size_t(length/2));
 StrokeSpline online;online.reset(a);assert(!online.tail());assert(!online.append(b));assert(online.tail()->at(1).position==b.position);const auto first=online.append(c);assert(first&&first->at(0).position==a.position&&first->at(1).position==b.position);assert(online.tail()->at(1).position==c.position);const auto second=online.append(d);assert(second&&second->at(.5).position==curve.at(.5).position);
 assert(!online.append(d));assert(online.tail()->at(1).position==d.position);
 // Incoming and outgoing derivatives at a shared knot agree in knot time.
 StrokeCurve next({b,c,d,{{80,10},.5,{0,0}}});const double eps=1e-5;const auto left=curve.at(1-eps),right=next.at(eps);const double h1=std::sqrt(std::hypot(c.position[0]-b.position[0],c.position[1]-b.position[1])),h2=std::sqrt(std::hypot(d.position[0]-c.position[0],d.position[1]-c.position[1]));for(int i=0;i<2;++i)assert(std::abs((c.position[i]-left.position[i])/(eps*h1)-(right.position[i]-c.position[i])/(eps*h2))<.001);
 // Replacing an immediate tail must not change final spacing or pressure when
 // identical inputs arrive singly, in groups, or in one event-loop burst.
 const auto sampled=[&](int batch){StrokeSpline stream;stream.reset(a);StrokeSpacing saved;auto cursor=a;saved.reset(a.position,1);std::vector<StrokeSample> prefix{a},visible;const std::array<StrokeSample,6> inputs{b,c,d,{{72,5},.5,{10,20}},{{90,35},.7,{30,40}},{{110,42},1,{45,60}}};for(int begin=0;begin<6;begin+=batch){auto spacing=saved;auto last_sample=cursor;visible=prefix;const auto render=[&](const StrokeCurve &part){part.flatten([&](auto sample){const auto delta=std::array{sample.position[0]-last_sample.position[0],sample.position[1]-last_sample.position[1]};const double length=delta[0]*delta[0]+delta[1]*delta[1];spacing.advance(sample.position,[&](auto point){const double t=length>0?std::clamp(((point[0]-last_sample.position[0])*delta[0]+(point[1]-last_sample.position[1])*delta[1])/length,0.,1.):1.;StrokeSample dab;dab.position=point;dab.pressure=std::lerp(last_sample.pressure,sample.pressure,t);for(int i=0;i<2;++i)dab.tilt[i]=std::lerp(last_sample.tilt[i],sample.tilt[i],t);visible.push_back(dab);return .5+2*dab.pressure;});last_sample=sample;});};for(int i=begin;i<std::min(6,begin+batch);++i)if(auto stable=stream.append(inputs[i]))render(*stable);saved=spacing;cursor=last_sample;prefix=visible;if(auto tail=stream.tail())render(*tail);}return visible;};
 const auto one=sampled(1),three=sampled(3),all=sampled(6);assert(one.size()==three.size()&&one.size()==all.size());for(std::size_t i=0;i<one.size();++i){assert(one[i].position==three[i].position&&one[i].position==all[i].position);assert(one[i].pressure==three[i].pressure&&one[i].pressure==all[i].pressure);assert(one[i].tilt==three[i].tilt&&one[i].tilt==all[i].tilt);}
 bool rejected=false;try{online.append({{NAN,0},1,{}});}catch(const std::invalid_argument &){rejected=true;}assert(rejected);
 std::cout<<"Centripetal spline endpoints, curved midpoint, bounded pressure/tilt, arc spacing, immediate prediction, exact future segment, continuity and invalid input passed\n";
}
