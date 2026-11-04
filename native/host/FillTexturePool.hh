#pragma once
#include "FillTexture.hh"
#include <cstring>
#include <mutex>
namespace bts {
// Shared immutable decoded storage for original images and self-contained
// presets. The source hash includes encoded format/bitdepth; interpretation and
// dimensions distinguish decoded uses. Weak entries retain no pixel memory.
inline std::shared_ptr<const std::vector<FillTexture::Pixel>> intern_fill_texture_pixels(
 const FillTexture &texture,std::shared_ptr<const std::vector<FillTexture::Pixel>> pixels)
{
 if(!pixels||pixels->size()!=std::size_t(texture.width)*texture.height)throw std::invalid_argument("Invalid shared texture pixels");
 const auto key=texture.storage_identity()+":"+std::to_string(texture.raw)+":"+texture.source_space+":"+(texture.raw?std::string{}:texture.input_space)+":"+std::to_string(texture.width)+":"+std::to_string(texture.height);
 static std::mutex mutex;static std::map<std::string,std::weak_ptr<const std::vector<FillTexture::Pixel>>> pool;
 std::lock_guard lock(mutex);
 if(const auto found=pool.find(key);found!=pool.end())if(auto shared=found->second.lock()){
  // An archive may not spoof an existing source hash to substitute different
  // pixels. Compare only on reuse; first-time image decoding adds no extra scan.
  if(shared!=pixels&&(shared->size()!=pixels->size()||std::memcmp(shared->data(),pixels->data(),pixels->size()*sizeof(FillTexture::Pixel))))throw std::invalid_argument("Texture source identity has conflicting decoded pixels");
  return shared;
 }
 if(pool.size()>4096)std::erase_if(pool,[](const auto &entry){return entry.second.expired();});
 pool[key]=pixels;return pixels;
}
// A stored preset may name the same source with another per-use asset UUID.
// Rebind its descriptor without copying pixels or conflating source revisions.
inline std::shared_ptr<const FillTexture> bind_fill_texture_descriptor(const FillTexture &expected,const FillTexture &image){
 if(expected.hash!=image.hash||expected.raw!=image.raw||expected.source_space!=image.source_space||expected.width!=image.width||expected.height!=image.height||bool(expected.tiles)!=bool(image.tiles))throw std::invalid_argument("Texture source descriptor changed");
 auto result=std::make_shared<FillTexture>(expected);result->pixels=image.pixels;result->input_space=image.input_space;result->storage_key=image.storage_key;
 if(expected.tiles){if(expected.tiles->size()!=image.tiles->size())throw std::invalid_argument("Texture source tile count changed");auto tiles=std::make_shared<std::map<int,std::shared_ptr<const FillTexture>>>();for(const auto &[tile,member]:*expected.tiles){const auto found=image.tiles->find(tile);if(found==image.tiles->end())throw std::invalid_argument("Texture source tile missing");tiles->emplace(tile,bind_fill_texture_descriptor(*member,*found->second));}result->tiles=std::move(tiles);result->pixels=result->tiles->begin()->second->pixels;}
 result->validate();return result;
}

}
