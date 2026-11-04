#pragma once
#include "Performance.hh"
#include <chrono>
#include <cstdio>
#include <ctime>
namespace bts {
struct PreviewScope {bool &value;bool before;explicit PreviewScope(bool &v):value(v),before(v){value=true;}~PreviewScope(){value=before;}};
struct EditTiming {
  performance::Scope profile;
  const char *name;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();std::clock_t cpu=std::clock();
  explicit EditTiming(const char *label):profile(label),name(label){}
  ~EditTiming(){const double wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),used=1000.*double(std::clock()-cpu)/CLOCKS_PER_SEC;std::printf("%s wall_ms=%.3f cpu_ms=%.3f\n",name,wall,used);std::fflush(stdout);}
};
}
