#include "CyclesDefaultCompletionEpoch.hh"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

#ifdef NDEBUG
#  error "The default completion epoch test requires active assertions"
#endif

namespace {
using namespace bts;
int checks=0;
void check(bool okay){++checks;assert(okay);}
bool terminal(const DefaultCompletionEpoch &state,int sample,double progress,bool done){
 return default_completion_terminal(state.epoch,state.terminal_epoch,sample,progress,done);
}
DefaultCompletionEpoch completed(){
 DefaultCompletionEpoch state;
 check(state.observe(0,0.1,false));
 check(state.active_seen&&state.terminal_epoch==0);
 check(state.observe(96,1,true));
 check(terminal(state,96,1,true));
 return state;
}
void initial_active_required(){
 DefaultCompletionEpoch state;
 check(state.observe(96,1,true));
 check(state.epoch==1&&!state.active_seen&&!terminal(state,96,1,true));
 check(state.observe(96,1,true));
 check(!terminal(state,96,1,true));
 auto adaptive=completed();
 check(adaptive.epoch==1&&adaptive.previous==96);
}
void prior_done_and_no_drop(){
 auto state=completed();
 for(int i=0;i<3;++i){
  check(state.observe(96,1,true));
  check(state.epoch==1);
  check(!default_completion_matches(3,state.epoch,state.terminal_epoch,96,1,true,3,2));
 }
 // Even observed work without a sample decrease must not invent a new epoch.
 check(state.observe(97,0.5,false));
 check(state.observe(100,1,true));
 check(state.epoch==1);
 check(!default_completion_matches(3,state.epoch,state.terminal_epoch,100,1,true,3,2));
}
void stale_done_on_drop_then_adaptive(){
 auto state=completed();
 check(state.observe(0,1,true));
 check(state.epoch==2&&!state.active_seen&&state.terminal_epoch==0);
 check(!terminal(state,0,1,true));
 check(state.observe(12,1,true));
 check(!terminal(state,12,1,true));
 check(!default_completion_matches(3,state.epoch,state.terminal_epoch,12,1,true,3,2));
 check(state.observe(24,0.25,false));
 check(state.active_seen&&state.terminal_epoch==0);
 check(state.observe(64,1,true));
 check(terminal(state,64,1,true));
 check(default_completion_matches(3,state.epoch,state.terminal_epoch,64,1,true,3,2));
 check(state.previous==64); // Actual adaptive sample count; no literal128 gate.
}
void active_drop_itself_is_insufficient(){
 auto state=completed();
 check(state.observe(0,0,false));
 check(state.epoch==2&&!state.active_seen);
 check(state.observe(64,1,true));
 check(!terminal(state,64,1,true));
 check(state.observe(65,0.75,false));
 check(state.observe(96,1,true));
 check(terminal(state,96,1,true));
}
void nonterminal_and_zero_sample(){
 DefaultCompletionEpoch state;
 check(state.observe(0,0.1,false));
 check(state.observe(0,1,true));
 check(state.terminal_epoch==0&&!terminal(state,0,1,true));
 check(state.observe(12,0.9,true));
 check(!terminal(state,12,0.9,true));
 check(state.observe(13,1,false));
 check(!terminal(state,13,1,false));
 check(!default_completion_terminal(0,0,13,1,true));
 check(!default_completion_terminal(1,0,13,1,true));
}
void generation_and_draw_mismatch(){
 const auto state=completed();
 check(default_completion_matches(4,state.epoch,state.terminal_epoch,96,1,true,4,1));
 check(!default_completion_matches(3,state.epoch,state.terminal_epoch,96,1,true,4,1));
 check(!default_completion_matches(5,state.epoch,state.terminal_epoch,96,1,true,4,1));
 check(!default_completion_matches(0,state.epoch,state.terminal_epoch,96,1,true,0,1));
 check(!default_completion_matches(4,state.epoch,state.terminal_epoch,96,1,true,4,2));
 check(default_draw_matches(4,2,4,2));
 check(!default_draw_matches(4,2,3,2));
 check(!default_draw_matches(4,2,4,1));
 check(!default_draw_matches(4,2,4,3));
 check(!default_draw_matches(0,2,0,2));
 check(!default_draw_matches(4,0,4,0));
}
void invalid_observations_do_not_mutate(){
 const double bad[]={std::numeric_limits<double>::quiet_NaN(),
  std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),-0.01,1.01};
 auto state=completed();
 const auto before=state;
 auto unchanged=[&]{check(state.previous==before.previous&&state.epoch==before.epoch&&
  state.terminal_epoch==before.terminal_epoch&&state.active_seen==before.active_seen);};
 check(!state.observe(-1,0.5,false));unchanged();
 for(double value:bad){check(!state.observe(0,value,false));unchanged();}
}
}
int main(){
 initial_active_required();prior_done_and_no_drop();stale_done_on_drop_then_adaptive();
 active_drop_itself_is_insufficient();nonterminal_and_zero_sample();
 generation_and_draw_mismatch();invalid_observations_do_not_mutate();
 std::cout<<"DEFAULT_COMPLETION_EPOCH_V2_PASS cases=7 assertions="<<checks<<'\n';
}
