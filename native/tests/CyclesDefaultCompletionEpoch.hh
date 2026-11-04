#pragma once
#include <cmath>
#include <cstdint>
namespace bts {
// Pure test state, used by the opaque default observer. A requested edit
// never advances an epoch: only an observed actual sample decrease does.
// v2: a reset sample can coexist with a stale status because the Cycles
// Progress getters lock independently. Require later active evidence, then
// a positive-sample terminal event; requests never manufacture epochs.
struct DefaultCompletionEpoch {
 int previous=-1;std::uint64_t epoch=1,terminal_epoch=0;bool active_seen=false;
 bool observe(int sample,double progress,bool done)noexcept {
  if(sample<0||!std::isfinite(progress)||progress<0||progress>1)return false;
  const bool dropped=previous>=0&&sample<previous;
  previous=sample;
  if(dropped){++epoch;terminal_epoch=0;active_seen=false;return true;}
  if(progress<1&&!done)active_seen=true;
  // Adaptive completion may have fewer than the configured128 samples.
  if(active_seen&&sample>0&&progress==1&&done)terminal_epoch=epoch;
  return true;
 }
};
inline bool default_completion_terminal(std::uint64_t epoch,std::uint64_t terminal_epoch,int sample,double progress,bool done)noexcept {
 return epoch>0&&terminal_epoch==epoch&&sample>0&&progress==1&&done;
}
inline bool default_completion_matches(std::uint64_t generation,std::uint64_t epoch,std::uint64_t terminal_epoch,int sample,double progress,bool done,std::uint64_t wanted_generation,std::uint64_t wanted_epoch)noexcept {
 return generation==wanted_generation&&generation>0&&epoch>=wanted_epoch&&default_completion_terminal(epoch,terminal_epoch,sample,progress,done);
}
inline bool default_draw_matches(std::uint64_t generation,std::uint64_t epoch,std::uint64_t draw_generation,std::uint64_t draw_epoch)noexcept {
 return generation>0&&epoch>0&&draw_generation==generation&&draw_epoch==epoch;
}
}
