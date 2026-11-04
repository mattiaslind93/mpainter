#include "NativeCyclesPublication.hh"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
namespace {bool deny_allocations=false;}
void *operator new(std::size_t n){if(deny_allocations)throw std::bad_alloc();if(auto *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
namespace {
void require(bool value,const char *message){if(!value)throw message;}
void reserve_entries(std::vector<bts::NativeCyclesCacheEntry> &entries){entries.reserve(2);}
void run(){
 deny_allocations=true;const auto fallback=bts::native_cycles_failure(9,9,{});const bool paused=!fallback.empty();const bool retried=bts::native_cycles_failure(9,10,{}).empty();deny_allocations=false;
 require(paused&&retried,"OOM fallback lost paused warning or new-generation retry");
 int storage[4]{};
 auto pointer=[&](int i){return reinterpret_cast<blender::ImBuf *>(&storage[i]);};
 std::array<blender::ImBuf *,2> cache{pointer(0),pointer(1)};
 std::array<bts::NativeCyclesCacheEntry,2> entries{{{1,1001,pointer(2),nullptr},{2,1002,pointer(3),nullptr}}};
 const std::array<std::uint32_t,2> images{1,2};
 bool attempted=false,completed=false;int notifications=0,calls=0;
 // Allocation denial before the prepared operation leaves both old entries.
 deny_allocations=true;bool denied=false;
 void (*volatile prepare)(std::vector<bts::NativeCyclesCacheEntry>&)=reserve_entries;
 try{std::vector<bts::NativeCyclesCacheEntry> unprepared;prepare(unprepared);}catch(const std::bad_alloc&){denied=true;}
 deny_allocations=false;
 require(denied&&!attempted&&cache[0]==pointer(0)&&cache[1]==pointer(1),"Prepublication failure changed cache");
 // The second publisher rejects. No generation notification may precede a
 // complete tile batch, and the already replaced entry retains its old ref.
 try{bts::publish_native_cycles_cache(entries,images,attempted,[&](auto id,int,auto *buffer,auto **previous){if(++calls==2)return false;*previous=cache[id-1];cache[id-1]=buffer;return true;},[&](auto){++notifications;});completed=true;}catch(const std::runtime_error&){}
 require(attempted&&!completed&&notifications==0&&entries[0].previous==pointer(0)&&!entries[1].previous,"Partial generation was advertised or keeper lost");
 require(cache[0]==pointer(2)&&cache[1]==pointer(1),"Failure fixture did not exercise partial mutation");
 // Retry the same source generation. After the first mutation, forbid all
 // C++ allocation: keeper storage, iteration and notification bookkeeping
 // must already exist. Backend cache allocation is a separate BKE contract.
 attempted=false;entries[0].previous=nullptr;
 bts::publish_native_cycles_cache(entries,images,attempted,[&](auto id,int,auto *buffer,auto **previous){*previous=cache[id-1];cache[id-1]=buffer;deny_allocations=true;return true;},[&](auto){require(cache[0]==pointer(2)&&cache[1]==pointer(3),"Notification preceded complete batch");++notifications;});
 completed=true;deny_allocations=false;
 require(completed&&attempted&&notifications==2&&entries[0].previous==pointer(2)&&entries[1].previous==pointer(1),"Same-generation retry lost a cache or keeper");
}
}
int main(){try{run();std::cout<<"Native Cycles publication: preallocation denial, partial rejection, retained keeper and allocation-free idempotent retry PASS\n";return 0;}catch(const char *e){deny_allocations=false;std::cerr<<e<<'\n';return 1;}catch(const std::exception&e){deny_allocations=false;std::cerr<<e.what()<<'\n';return 1;}}
