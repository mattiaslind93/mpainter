// SPDX-License-Identifier: GPL-3.0-or-later
#include "PendingProjectCreate.hh"
#include "GpuProgramWarmup.hh"
#include "GpuPreparedProgramOwner.hh"
#include <type_traits>
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using State=bts::GpuProgramWarmup::State;
int premature_waits=0;
struct Job {
 bool done=false,success=true;
 std::shared_ptr<int> sources=std::make_shared<int>(42);
 ~Job(){if(!done)++premature_waits;}
};
struct Pair {
 bts::GpuProgramWarmup state;
 std::array<std::unique_ptr<Job>,2> jobs;
 std::array<bool,2> shaders{};
 std::array<int,2> polls{};
 int submissions=0;bool reject_second=false;
 bool pending()const{return bool(jobs[0])||bool(jobs[1]);}
 bool poll(int index){
  ++polls[index];auto &job=jobs[index];if(!job||!job->done)return true;
  shaders[index]=job->success;job.reset();return shaders[index];
 }
 void drain(){state.drain([&](int index){return poll(index);});}
 State prepare(){
  return state.prepare([&](int index){
   ++submissions;
   if(index==1&&reject_second)throw std::runtime_error("second submission failed");
   jobs[index]=std::make_unique<Job>();
  },[&](int index){return poll(index);},[&](int index){return shaders[index];},[&](int index){return bool(jobs[index]);});
 }
};
void partial_submission(){
 Pair pair;pair.reject_second=true;
 assert(pair.prepare()==State::Pending&&pair.submissions==2&&pair.jobs[0]&&!pair.jobs[1]);
 std::weak_ptr<int> source=pair.jobs[0]->sources;assert(!source.expired());
 pair.jobs[0]->done=true;
 assert(pair.prepare()==State::Failed&&pair.shaders[0]&&!pair.shaders[1]);
 assert(pair.prepare()==State::Failed&&pair.submissions==2); // No retry or false Ready.
 assert(source.expired()&&pair.state.take_error().find("second submission failed")!=std::string::npos);
 assert(pair.state.take_error().empty()&&premature_waits==0);
}
void failed_sibling_is_drained(){
 Pair pair;assert(pair.prepare()==State::Pending);
 pair.jobs[0]->done=true;pair.jobs[0]->success=false;
 const int before=pair.polls[1];
 assert(pair.prepare()==State::Pending&&pair.polls[1]>before&&!pair.jobs[0]&&pair.jobs[1]);
 const int after_failure=pair.polls[1];pair.jobs[1]->done=true;
 assert(pair.prepare()==State::Failed&&pair.polls[1]>after_failure&&!pair.pending());
 assert(!pair.shaders[0]&&pair.shaders[1]&&pair.submissions==2);
 assert(pair.state.take_error()=="GPU stack RGBA shader compilation failed");
 assert(pair.state.take_error().empty()&&premature_waits==0);
}
void reset_retains_sources_without_waiting(){
 auto active=std::make_unique<Pair>();assert(active->prepare()==State::Pending);
 auto *first=active->jobs[0].get(),*second=active->jobs[1].get();
 std::weak_ptr<int> a=first->sources,b=second->sources;
 std::vector<std::unique_ptr<Pair>> retired;retired.push_back(std::move(active));active=std::make_unique<Pair>();
 const auto pending=[&]{return active->pending()||std::any_of(retired.begin(),retired.end(),[](const auto &pair){return pair->pending();});};
 const auto collect=[&]{std::erase_if(retired,[](const auto &pair){pair->drain();return !pair->pending();});};
 assert(pending()&&!active->pending()&&!a.expired()&&!b.expired());
 collect();assert(pending()&&retired.size()==1&&premature_waits==0);
 first->done=true;collect();assert(pending()&&a.expired()&&!b.expired()&&retired.size()==1);
 second->done=true;collect();assert(!pending()&&retired.empty()&&b.expired()&&premature_waits==0);
}
void both_outputs_required(){
 Pair pair;assert(pair.prepare()==State::Pending);
 pair.jobs[0]->done=true;assert(pair.prepare()==State::Pending);
 pair.jobs[1]->done=true;assert(pair.prepare()==State::Ready);
 assert(pair.shaders[0]&&pair.shaders[1]&&pair.submissions==2&&pair.state.take_error().empty());
 assert(pair.prepare()==State::Ready&&pair.submissions==2&&premature_waits==0);
}
#include "GpuDerivedWarmupStateTests.inc"
}
namespace {
void pending_new_is_one_copied_intent(){
 struct Options {std::string mesh;};bts::PendingProjectCreate<Options> intent;
 Options options{"first.obj"};intent.replace(options,"project",7);options.mesh="mutated.obj";
 assert(!intent.take_ready(intent.generation(),true,"project",7)&&intent.pending());
 auto copied=intent.take_ready(intent.generation(),false,"project",7);assert(copied&&copied->options.mesh=="first.obj");
 assert(intent.restore(std::move(*copied))); // Compilation resumed after Save.
 intent.replace({"latest.obj"},"project",7);auto latest=intent.take_ready(intent.generation(),false,"project",7);
 assert(latest&&latest->options.mesh=="latest.obj"&&!intent.pending());
 assert(!intent.take_ready(intent.generation(),false,"project",7)); // At most one submission.
}
void pending_new_cancel_stale_and_shutdown(){
 bts::PendingProjectCreate<int> intent;intent.replace(1,"project",7);intent.cancel();
 assert(!intent.take_ready(intent.generation(),false,"project",7));
 intent.replace(2,"project",7);assert(!intent.take_ready(intent.generation(),true,"other",7)&&!intent.pending());
 intent.replace(3,"project",7);assert(!intent.take_ready(intent.generation(),false,"project",8)&&!intent.pending());
 intent.replace(4,"project",7);intent.retire();intent.replace(5,"project",7);
 assert(!intent.pending()&&!intent.take_ready(intent.generation(),false,"project",7));
}
void pending_new_taken_failure_owns_only_its_generation(){
 bts::PendingProjectCreate<int> intent;intent.replace(1,"project",7);const auto generation=intent.generation();
 auto taken=intent.take_ready(intent.generation(),false,"project",7);assert(taken&&!intent.pending());
 // Save may edit then return false: pending/current are both false, but the
 // taken generation still owns cancellation/cleanup and must be retired.
 assert(!intent.current(*taken,"project",8));assert(intent.cancel_generation(generation));
 assert(!intent.pending()&&!intent.restore(*taken));
 intent.replace(2,"project",7);auto old=intent.take_ready(intent.generation(),false,"project",7);
 intent.replace(3,"project",7);assert(old&&!intent.cancel_generation(old->generation)&&intent.pending());
 assert(!intent.take_ready(old->generation,false,"project",7)&&intent.pending()); // Old timeout cannot consume newer work.
 auto newer=intent.take_ready(intent.generation(),false,"project",7);assert(newer&&newer->options==3);
 // A failure before take_ready must also cancel the waiting owned generation.
 intent.replace(4,"project",7);assert(intent.cancel_generation(intent.generation())&&!intent.pending());
}
void pending_new_rechecks_after_event_pump(){
 bts::PendingProjectCreate<int> intent;intent.replace(1,"project",7);auto request=intent.take_ready(intent.generation(),false,"project",7);
 assert(request&&intent.current(*request,"project",7));
 assert(!intent.current(*request,"project",8)); // An edit during Save is not overwritten.
 intent.replace(2,"project",7);assert(!intent.current(*request,"project",7)&&!intent.restore(*request));
 auto replacement=intent.take_ready(intent.generation(),false,"project",7);assert(replacement&&replacement->options==2);
 intent.cancel();assert(!intent.current(*replacement,"project",7)&&!intent.restore(*replacement));
}
}
#include "GpuAuthoringProgramStateTests.inc"
int main(){fourteen_authoring_outputs_and_failure_drain();prepared_interface_copy_and_destruction_lifetime();partial_submission();failed_sibling_is_drained();reset_retains_sources_without_waiting();both_outputs_required();ten_outputs_required();derived_partial_submission_drains_every_slot();pending_new_is_one_copied_intent();pending_new_cancel_stale_and_shutdown();pending_new_rechecks_after_event_pump();pending_new_taken_failure_owns_only_its_generation();}
