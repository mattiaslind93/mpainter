#include "ResourceAvailability.hh"
#pragma once
#include "AssetCatalog.hh"
#include "ImageSequence.hh"
#include <atomic>
namespace bts {
struct ResourceChange {StudioAsset previous,current;};
struct ResourceChanges {std::vector<ResourceChange> updates;QStringList messages;};
// Worker-only, immutable catalog input. Checks actual bytes, including all UDIM members;
// neither a same-size overwrite nor an atomic file replacement can evade detection.
inline ResourceChanges scan_resources(const std::vector<StudioAsset> &assets,const std::atomic<bool> &cancel){
 ResourceChanges result;qsizetype updated_bytes=0;
 for(const auto &asset:assets){
  if(cancel.load())break;
  if(asset.scope==AssetScope::Builtin||asset.path.isEmpty())continue;
  try{
   if(!QFileInfo(asset.path).isAbsolute())throw std::runtime_error("Original source path is not absolute");
   if(!asset.texture_files.empty()){
    const auto source=reference_image_resource(asset.path,asset.texture_files.front().tile!=0,&cancel);auto next=asset;next.texture_files=source.files;next.bytes.clear();next.filename=source.filename;next.alpha.reset();next.smart_mask.reset();if(next.hash()!=asset.hash())result.updates.push_back({asset,std::move(next)});continue;
   }
   const auto source=read_image_resource(asset.path,is_image_sequence(asset.bytes));
   if(source.bytes==asset.bytes)continue;
   updated_bytes=resource_sum(updated_bytes,source.bytes.size());
   auto next=asset;next.bytes=source.bytes;next.filename=source.filename;next.alpha.reset();next.smart_mask.reset();
   result.updates.push_back({asset,std::move(next)});
  }catch(const std::exception &e){result.messages.append(asset.name+": "+QString::fromUtf8(e.what())+"; retained imported version.");}
 }
 return result;
}
}
