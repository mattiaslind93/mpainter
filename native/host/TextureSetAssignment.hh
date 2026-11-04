#pragma once
#include "TextureDocument.hh"
#include <algorithm>
#include <stdexcept>
namespace bts {
class TextureSetAssignment {
 public:
  using State=std::vector<QUuid>;
  explicit TextureSetAssignment(const std::vector<TextureSet> &sets,const std::vector<std::uint32_t> &order={}):sets_(sets){auto add=[&](const auto &set){destinations_.push_back(set.material_session_uid);current_.push_back(set.id);};if(order.empty()){for(const auto &set:sets_)if(!set.disabled)add(set);}else for(auto uid:order){const auto found=std::find_if(sets_.begin(),sets_.end(),[&](const auto &s){return s.material_session_uid==uid&&!s.disabled;});if(found==sets_.end())throw std::invalid_argument("Mesh material has no active Texture Set");add(*found);}}
  const State &state() const{return current_;}
  const std::vector<std::uint32_t> &destinations() const{return destinations_;}
  bool can_undo() const{return !undo_.empty();}bool can_redo() const{return !redo_.empty();}
  void assign(std::size_t row,const QUuid &id){if(row>=current_.size()||std::none_of(sets_.begin(),sets_.end(),[&](const auto &s){return s.id==id;}))throw std::invalid_argument("Unknown Texture Set assignment");if(current_[row]==id)return;undo_.push_back(current_);redo_.clear();auto previous=std::find(current_.begin(),current_.end(),id);if(previous!=current_.end())*previous=current_[row];current_[row]=id;}
  void undo(){if(!can_undo())return;redo_.push_back(current_);current_=std::move(undo_.back());undo_.pop_back();}
  void redo(){if(!can_redo())return;undo_.push_back(current_);current_=std::move(redo_.back());redo_.pop_back();}
  std::vector<TextureSet> result() const {auto result=sets_;std::set<std::uint32_t> unused;for(const auto &set:sets_)unused.insert(set.material_session_uid);for(auto uid:destinations_)unused.erase(uid);for(auto &set:result){auto found=std::find(current_.begin(),current_.end(),set.id);set.disabled=found==current_.end();if(set.disabled){auto free=unused.find(set.material_session_uid);if(free==unused.end())free=std::find_if(unused.begin(),unused.end(),[&](auto uid){const auto source=std::find_if(sets_.begin(),sets_.end(),[&](const auto &s){return s.material_session_uid==uid;});return source->tiles==set.tiles;});if(free==unused.end())throw std::invalid_argument("No compatible unused material binding");set.material_session_uid=*free;unused.erase(free);}else{const auto row=std::size_t(found-current_.begin());const auto target=std::find_if(sets_.begin(),sets_.end(),[&](const auto &s){return s.material_session_uid==destinations_[row];});if(target->tiles!=set.tiles)throw std::invalid_argument("Assigned Texture Sets must use the destination UV tile numbers");set.material_session_uid=destinations_[row];}set.projection_geometry.reset();}return result;}
 private:
  std::vector<TextureSet> sets_;std::vector<std::uint32_t> destinations_;State current_;std::vector<State> undo_,redo_;
};
}
