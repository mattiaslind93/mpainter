// SPDX-License-Identifier: GPL-3.0-or-later
// Same registered state test, now exercising every derived slot, not just two.
void ten_outputs_required(){
 bts::GpuProgramWarmup state{10};std::array<std::unique_ptr<Job>,10> jobs;
 std::array<bool,10> ready{};int submissions=0;
 const auto poll=[&](int index){auto &job=jobs[index];if(!job||!job->done)return true;
  ready[index]=job->success;job.reset();return ready[index];};
 const auto prepare=[&]{return state.prepare([&](int index){++submissions;jobs[index]=std::make_unique<Job>();},poll,
  [&](int index){return ready[index];},[&](int index){return bool(jobs[index]);});};
 assert(prepare()==State::Pending&&submissions==10);
 for(int index=0;index<9;++index)jobs[index]->done=true;
 assert(prepare()==State::Pending&&submissions==10&&jobs[9]);
 jobs[9]->done=true;assert(prepare()==State::Ready&&submissions==10);
 assert(std::all_of(ready.begin(),ready.end(),[](bool value){return value;}));
 assert(prepare()==State::Ready&&submissions==10&&state.take_error().empty());
}
void derived_partial_submission_drains_every_slot(){
 bts::GpuProgramWarmup state{10};std::array<std::unique_ptr<Job>,10> jobs;
 std::array<bool,10> ready{};std::array<int,10> polls{};int submissions=0;
 const auto poll=[&](int index){++polls[index];auto &job=jobs[index];if(!job||!job->done)return true;
  ready[index]=job->success;job.reset();return ready[index];};
 const auto prepare=[&]{return state.prepare([&](int index){++submissions;
  if(index==7)throw std::runtime_error("derived submission7 failed");jobs[index]=std::make_unique<Job>();},poll,
  [&](int index){return ready[index];},[&](int index){return bool(jobs[index]);});};
 assert(prepare()==State::Pending&&submissions==8&&jobs[6]&&!jobs[7]);
 std::weak_ptr<int> last_source=jobs[6]->sources;
 jobs[0]->done=true;jobs[0]->success=false;
 const auto before=polls;assert(prepare()==State::Pending&&!last_source.expired());
 for(int index=0;index<10;++index)assert(polls[index]>before[index]);
 for(auto &job:jobs)if(job)job->done=true;
 assert(prepare()==State::Failed&&last_source.expired()&&submissions==8);
 assert(state.take_error().find("derived submission7 failed")!=std::string::npos);
 assert(prepare()==State::Failed&&state.take_error().empty()&&premature_waits==0);
}
