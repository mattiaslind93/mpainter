namespace {
void fourteen_authoring_outputs_and_failure_drain(){
 bts::GpuProgramWarmup state(14);std::array<bool,14> pending{},complete{},good{};int submitted=0,polled=0;
 const auto poll=[&](int index){++polled;if(pending[index]&&complete[index]){pending[index]=false;return good[index];}return true;};
 const auto prepare=[&]{return state.prepare([&](int index){++submitted;pending[index]=true;},poll,[&](int index){return complete[index]&&good[index];},[&](int index){return pending[index];});};
 assert(prepare()==State::Pending&&submitted==14);good.fill(true);complete.fill(true);complete[13]=false;
 assert(prepare()==State::Pending&&pending[13]);complete[13]=true;
 assert(prepare()==State::Ready&&submitted==14);assert(state.take_error().empty());
 bts::GpuProgramWarmup failed(14);pending.fill(false);complete.fill(false);good.fill(true);submitted=0;
 const auto fail_prepare=[&]{return failed.prepare([&](int index){if(index==8)throw std::runtime_error("authoring submission8");++submitted;pending[index]=true;},poll,[&](int index){return complete[index]&&good[index];},[&](int index){return pending[index];});};
 assert(fail_prepare()==State::Pending&&submitted==8);complete[2]=true;good[2]=false;
 assert(fail_prepare()==State::Pending&&!pending[2]&&pending[7]);const int before=polled;
 complete.fill(true);assert(fail_prepare()==State::Failed&&polled>=before+14&&submitted==8);
 assert(std::none_of(pending.begin(),pending.end(),[](bool value){return value;}));assert(!failed.take_error().empty());assert(failed.take_error().empty());
}
void prepared_interface_copy_and_destruction_lifetime(){
 struct Metadata {bool *live;std::vector<int> *events;int value=42;Metadata(bool &l,std::vector<int> &e):live(&l),events(&e){l=true;}~Metadata(){*live=false;events->push_back(2);}};
 struct Info {Metadata *borrowed;std::string source;};
 struct Handle {Info copy;explicit Handle(const Info &info):copy(info){}~Handle(){assert(*copy.borrowed->live&&copy.borrowed->value==42&&copy.source=="original");copy.borrowed->events->push_back(1);}};
 using Owner=bts::GpuPreparedProgramOwner<Metadata,std::unique_ptr<Handle>>;
 static_assert(!std::is_copy_constructible_v<Owner> && !std::is_copy_assignable_v<Owner>);
 bool live=false;std::vector<int> events;
 {
  Owner owner{std::unique_ptr<Handle>{}};owner.retain(std::make_unique<Metadata>(live,events));
  {Info source{owner.metadata(),"original"};owner.submit(std::make_unique<Handle>(source));source.source="destroyed caller";}
  assert(live&&owner&&owner.get()->copy.source=="original");
  Owner moved(std::move(owner));assert(!owner&&moved&&live);moved.reset();assert(!live&&(events==std::vector<int>{1,2}));
 }
 events.clear();
 {Owner owner{std::unique_ptr<Handle>{}};owner.retain(std::make_unique<Metadata>(live,events));Info source{owner.metadata(),"original"};owner.submit(std::make_unique<Handle>(source));}
 assert(!live&&(events==std::vector<int>{1,2})); // Terminal destruction, not only ready reset.
}
}
