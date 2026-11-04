#pragma once
#include "AdjustmentPaintSource.hh"
#include <span>
#include <vector>
namespace bts {
// Correlate delayed GPU refusal with only an uninterrupted adjustment gesture
// tail. Opaque history identities are compared, never dereferenced here.
class AdjustmentPaintTransactions {
 struct Commit{QUuid snapshot,document,set,layer;std::uint64_t revision;std::uintptr_t before;};
 std::vector<Commit> commits_;
 public:
 void record(const QUuid &snapshot,const QUuid &document,const QUuid &set,const QUuid &layer,std::uint64_t revision,const void *before){
  if(!commits_.empty()&&(commits_.back().document!=document||commits_.back().revision+1!=revision||commits_.back().set!=set||commits_.back().layer!=layer))commits_.clear();
  commits_.push_back({snapshot,document,set,layer,revision,reinterpret_cast<std::uintptr_t>(before)});if(commits_.size()>128)commits_.erase(commits_.begin());
 }
 std::optional<std::size_t> rollback(const QUuid &snapshot,const QUuid &document,std::uint64_t revision,std::span<const std::uintptr_t> history)const{
  if(commits_.empty()||commits_.back().document!=document||commits_.back().revision!=revision)return {};
  auto first=std::find_if(commits_.begin(),commits_.end(),[&](const auto &c){return c.snapshot==snapshot&&c.document==document;});if(first==commits_.end())return {};
  const auto count=std::size_t(commits_.end()-first);if(history.size()<count)return {};
  const auto begin=history.size()-count;for(std::size_t i=0;i<count;++i)if(history[begin+i]!=first[i].before)return {};
  return begin;
 }
 void clear(){commits_.clear();}
};
}
