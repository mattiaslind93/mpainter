#pragma once
#include "ResourceBudget.hh"
#include <memory>
#include <type_traits>
#include <utility>
namespace bts {
// Construct metadata and reserve before queuing GPU edits or moving the live
// document. push_back({move(document),...}) can otherwise lose the document
// when vector growth or entry metadata construction throws.
template<class History>auto prepare_document_commit(History &undo,std::uint32_t uid){
 using Entry=typename History::value_type;static_assert(std::is_nothrow_move_constructible_v<Entry>);
 Entry prepared{nullptr,uid};
 if(undo.size()==undo.capacity()){
  const auto count=resource_sum(undo.size(),1);
  if(count>undo.max_size())throw std::length_error("Undo metadata exceeds vector index storage");
  auto growth=count;if(undo.capacity()&&undo.capacity()<=undo.max_size()/2)growth=std::max<std::uint64_t>(count,undo.capacity()*2);
  // Keep amortized vector growth without making optional spare capacity a
  // reason to reject an edit whose exact required metadata still fits.
  if(growth>count&&resource_product(growth,sizeof(Entry))<=ResourceBudget{}.ram()){
   try{undo.reserve(growth);return prepared;}catch(const std::bad_alloc &){}
  }
  require_texture_ram(resource_product(count,sizeof(Entry)),"Undo entry metadata allocation");undo.reserve(count);
 }
 return prepared;
}
// Call only after all throwing candidate/queue preparation. The publication
// hook is restricted to noexcept swaps of already prepared state.
template<class Document,class History,class Publish>
void publish_document_commit(std::unique_ptr<Document> &document,std::unique_ptr<Document> &candidate,History &undo,History &redo,typename History::value_type &&prepared,Publish &&publish){
 static_assert(std::is_nothrow_invocable_v<Publish>);
 if(!candidate||prepared.state||undo.size()==undo.capacity())throw std::logic_error("Document commit was not prepared");
 prepared.state=std::move(document);undo.push_back(std::move(prepared));redo.clear();
 std::forward<Publish>(publish)();document=std::move(candidate);
}
}
