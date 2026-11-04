#include "NativeCyclesRevisions.hh"
#include "NativeCyclesPublication.hh"
#include "ResourceBudget.hh"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <type_traits>
namespace {bool deny_allocations=false;}
void *operator new(std::size_t n){if(deny_allocations)throw std::bad_alloc();if(auto *p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}
namespace {
using namespace bts;
void require(bool value,const char *message){if(!value)throw message;}
NativeCyclesImageIdentity identity(){
 NativeCyclesImageIdentity id;id.main=1;id.allocation=2;id.image=3;id.material=4;id.role=0;id.format=16;id.width=id.height=8;id.layers=2;id.source=1;id.type=2;id.alpha_mode=1;
 id.declared_space="Linear Rec.709";id.working_space=id.buffer_space="ACEScg";id.working_matrix={1,0,0,0,1,0,0,0,1};
 id.tiles={{1001,0,0,0,8,8,8,8},{1002,0,0,1,8,8,8,8}};return id;
}
bool accepts(const NativeCyclesRevision &state,const NativeCyclesImageIdentity &required,const NativeCyclesImageIdentity &captured,std::uint64_t revision=6,std::uint64_t epoch=9,std::uint64_t captured_epoch=9,bool active=true){return native_cycles_publishable(state,epoch,captured_epoch,revision,required,captured,active);}
void monotonic_and_floor(){
 const auto id=identity();NativeCyclesRevision state;native_cycles_require(state,9,5,true);state.wanted=10;
 require(accepts(state,id,id),"A complete captured revision older than wanted was rejected");
 native_cycles_commit_revision(state,6);
 require(state.published==6&&state.wanted==10&&native_cycles_revision_ready(state,9),"Publication lied about wanted or moved fixed floor");
 require(!accepts(state,id,id,6)&&!accepts(state,id,id,5)&&!accepts(state,id,id,11),"Duplicate/regressing/future capture admitted");
 require(!accepts(state,id,id,7,9,8)&&!accepts(state,id,id,7,10,9)&&!accepts(state,id,id,7,9,9,false),"Entry/reset epoch or inactive completion admitted");
 native_cycles_require(state,10,11,false);state.wanted=20;
 require(!native_cycles_revision_ready(state,10)&&!accepts(state,id,id,10,10,10),"Old publication satisfied re-entry");
 require(accepts(state,id,id,11,10,10),"Fixed entry floor chased newer wanted");native_cycles_commit_revision(state,11);
 require(native_cycles_revision_ready(state,10)&&state.wanted==20,"Fixed floor cannot admit captured entry generation");
 native_cycles_require(state,10,21,true);
 require(state.published==0&&!native_cycles_revision_ready(state,10),"New required identity inherited old nonzero readiness");
 require(accepts(state,id,id,21,10,10),"New image/identity floor cannot become ready");
 bool overflow=false;try{native_cycles_next_serial(std::numeric_limits<std::uint64_t>::max());}catch(const std::overflow_error&){overflow=true;}
 require(overflow,"Revision counter wrapped across epoch/ABA guard");
}
void complete_identity(){
 const auto id=identity();NativeCyclesRevision state;native_cycles_require(state,9,5,true);state.wanted=10;
 require(native_cycles_valid_identity(id),"Valid complete two-tile fixture rejected");
 auto rejects=[&](auto change){auto other=id;change(other);require(!accepts(state,other,id),"Changed complete identity admitted");};
 rejects([](auto &x){++x.main;});rejects([](auto &x){++x.allocation;});rejects([](auto &x){++x.image;});rejects([](auto &x){++x.material;});rejects([](auto &x){++x.role;});
 rejects([](auto &x){++x.format;});rejects([](auto &x){++x.width;});rejects([](auto &x){++x.height;});rejects([](auto &x){++x.layers;});rejects([](auto &x){++x.mips;});
 rejects([](auto &x){++x.source;});rejects([](auto &x){++x.type;});rejects([](auto &x){++x.alpha_mode;});rejects([](auto &x){x.data=!x.data;});
 rejects([](auto &x){x.declared_space="Non-Color";});rejects([](auto &x){x.working_space="Linear Rec.709";});rejects([](auto &x){x.buffer_space="Linear Rec.709";});rejects([](auto &x){++x.working_matrix[4];});
 rejects([](auto &x){x.tiles.pop_back();});rejects([](auto &x){x.tiles.push_back({1003,0,0,1,8,8,8,8});});
 rejects([](auto &x){x.tiles[1].number=1004;});rejects([](auto &x){x.tiles[0].x=1;});rejects([](auto &x){x.tiles[0].y=1;});rejects([](auto &x){x.tiles[0].layer=1;});
 rejects([](auto &x){--x.tiles[0].width;--x.tiles[0].generated_width;});rejects([](auto &x){--x.tiles[0].height;--x.tiles[0].generated_height;});
 rejects([](auto &x){--x.tiles[0].generated_width;});rejects([](auto &x){--x.tiles[0].generated_height;});
 auto malformed=id;std::swap(malformed.tiles[0],malformed.tiles[1]);require(!native_cycles_valid_identity(malformed),"Unsorted identity admitted");
 malformed=id;malformed.tiles[1].number=1001;require(!native_cycles_valid_identity(malformed),"Duplicate UDIM identity admitted");
 malformed=id;malformed.tiles.clear();require(!native_cycles_valid_identity(malformed),"Empty/partial image admitted");
}
void pause_and_drain(){
 using A=NativeCyclesCompletionAction;
 require(native_cycles_completion_action(true,true,false,true)==A::Wait,"Paused generation dropped in-flight capture");
 require(native_cycles_completion_action(true,true,true,false)==A::Wait,"CPU retirement backpressure ignored");
 require(native_cycles_completion_action(true,true,true,true)==A::Consume,"Paused early-return skips completed future");
 require(native_cycles_completion_action(true,false,false,true)==A::Paused,"Same-generation failure can spin new work");
 require(native_cycles_completion_action(false,false,false,true)==A::Continue,"New generation did not resume");
 deny_allocations=true;const bool paused=!native_cycles_failure(8,8,{}).empty(),retry=native_cycles_failure(8,9,{}).empty();deny_allocations=false;
 require(paused&&retry,"Failure warning/retry needs allocating message or lost generation");
}
void recorded_error_boundary(){
 bool reported=false;int warnings=0;std::string_view detail;
 auto report=[&](std::string_view value)noexcept{if(!value.empty())++warnings;};
 auto error=[&]{return detail;};
 require(native_cycles_recorded_error_boundary(reported,[]{return true;},error,report)&&!reported,"Healthy update was deferred");
 require(!native_cycles_recorded_error_boundary(reported,[&]()->bool{detail="Recorded std failure";throw std::runtime_error("original");},error,report)&&reported&&warnings==1,"Recorded std failure escaped Rendering boundary");
 require(!native_cycles_recorded_error_boundary(reported,[]()->bool{throw 17;},error,report)&&warnings==1,"Recorded nonstd failure escaped or warned repeatedly");
 detail={};bool std_propagated=false,nonstd_propagated=false;
 try{native_cycles_recorded_error_boundary(reported,[]()->bool{throw std::runtime_error("unknown");},error,report);}catch(const std::runtime_error&){std_propagated=true;}
 try{native_cycles_recorded_error_boundary(reported,[]()->bool{throw 23;},error,report);}catch(int value){nonstd_propagated=value==23;}
 require(std_propagated&&nonstd_propagated&&!reported&&warnings==1,"Unknown/unrecorded failure was swallowed");
 // Failure detail need not allocate: production exposes this exact standing
 // warning when metadata admission or post-mutation detail allocation fails.
 std::uint64_t failed=0;deny_allocations=true;
 const bool deferred=!native_cycles_recorded_error_boundary(reported,[&]()->bool{failed=8;throw std::bad_alloc();},[&]{return native_cycles_failure(failed,8,{});},report);
 deny_allocations=false;
 require(deferred&&reported&&warnings==2,"Allocation denial escaped standing mirror failure boundary");
 failed=0;require(native_cycles_recorded_error_boundary(reported,[]{return true;},[&]{return native_cycles_failure(failed,9,{});},report)&&!reported,"A clean/new generation retained stale log state");
 require(!native_cycles_recorded_error_boundary(reported,[&]()->bool{failed=9;throw 29;},[&]{return native_cycles_failure(failed,9,{});},report)&&warnings==3,"New recorded generation did not warn once");
}
struct FakeTexture {int refs=1;};
std::array<int,32> events{};std::size_t count=0;bool owner=true;
void retain(FakeTexture *texture){require(owner,"GPU retain ran off owner");++texture->refs;events[count++]=1;}
void release(FakeTexture *texture){require(owner,"GPU release ran on CPU retirement");--texture->refs;events[count++]=2;}
void owner_lifetime(){
 using Ref=NativeCyclesOwnerReference<FakeTexture>;static_assert(!std::is_copy_constructible_v<Ref>);
 for(int path=0;path<4;++path){ // Success, discard, exception and reset ordering.
  FakeTexture old,next;count=0;Ref required(&old,retain,release);Ref capture(&old,retain,release);
  required=Ref(&next,retain,release);release(&old); // Live slot replaced; captured allocation still alive.
  require(old.refs==1,"Rebinding released captured allocation before owner validation");
  auto *metadata=&old;owner=false;const auto inert=metadata;require(inert==metadata,"CPU immutable metadata changed");owner=true;
  if(path==3)required.reset(); // Explicit reset releases current entry first.
  const auto last_use=count;events[count++]=3;
  if(path==0){Ref moved(std::move(capture));require(!capture.get()&&moved.get()==&old,"Keeper move duplicated owner ref");moved.reset();}
  else if(path==1)capture.reset(); // Invalid identity/epoch discard.
  else if(path==2){try{Ref scope(std::move(capture));throw 7;}catch(int){}} // Owner exception unwind.
  else capture=Ref{}; // Reset/close clears capture owner scope.
  require(old.refs==0,"Old allocation leaked after final owner use");
  events[count++]=4; // owner_done then CPU-only retirement, no live GPU refs there.
  owner=false;require(old.refs==0,"CPU retirement owns captured GPU last-ref");owner=true;
  require(events[last_use]==3&&events[last_use+1]==2&&events[last_use+2]==4,"GPU release not between last use and owner_done");
  required.reset();require(next.refs==1,"Reset lost live slot's independent reference");
 }
}
void prepared_publication(){
 const auto id=identity();NativeCyclesRevision a,b;native_cycles_require(a,9,5,true);native_cycles_require(b,9,5,true);a.wanted=b.wanted=10;
 require(accepts(a,id,id)&&accepts(b,id,id),"Commit fixture preflight failed");
 std::array<int,4> storage{};auto ptr=[&](int n){return reinterpret_cast<blender::ImBuf *>(&storage[n]);};
 std::array<blender::ImBuf *,2> cache{ptr(0),ptr(1)};std::array<NativeCyclesCacheEntry,2> rows{{{1,1001,ptr(2),nullptr},{2,1002,ptr(3),nullptr}}};const std::array<std::uint32_t,2> ids{1,2};
 bool attempted=false;int calls=0,notifications=0;bool failed=false;
 try{publish_native_cycles_cache(rows,ids,attempted,[&](auto image,int,auto *value,auto **old){if(++calls==2)return false;*old=cache[image-1];cache[image-1]=value;return true;},[&](auto){++notifications;});native_cycles_commit_revision(a,6);native_cycles_commit_revision(b,6);}catch(const std::runtime_error&){failed=true;}
 require(failed&&attempted&&a.published==0&&b.published==0&&notifications==0&&rows[0].previous==ptr(0),"Partial BKE failure advertised complete revision or lost keeper");
 rows[0].previous=nullptr;attempted=false;
 publish_native_cycles_cache(rows,ids,attempted,[&](auto image,int,auto *value,auto **old){*old=cache[image-1];cache[image-1]=value;deny_allocations=true;return true;},[&](auto){++notifications;});
 native_cycles_commit_revision(a,6);native_cycles_commit_revision(b,6);deny_allocations=false;
 require(a.published==6&&b.published==6&&a.wanted==10&&b.wanted==10&&notifications==2,"Prepared commit lied about latest GPU revision");
 std::uint64_t (*previous)()=texture_ram_query;texture_ram_query=+[]()->std::uint64_t{return 32;};bool rejected=false;
 try{ResourceBudget{}.require(17,0,"Synthetic metadata admission");}catch(const std::length_error&){rejected=true;}texture_ram_query=previous;
 require(rejected,"Dynamic RAM admission ignored actual available memory");
}
}
int main(){try{monotonic_and_floor();complete_identity();pause_and_drain();recorded_error_boundary();owner_lifetime();prepared_publication();std::cout<<"Native Cycles captured revisions: monotonic identity/epochs/floors, pause drain, recorded-error boundary, owner keepers and preallocated partial-failure commit PASS\n";return 0;}catch(const char *error){deny_allocations=false;std::cerr<<error<<'\n';return 1;}catch(const std::exception &error){deny_allocations=false;std::cerr<<error.what()<<'\n';return 1;}}
