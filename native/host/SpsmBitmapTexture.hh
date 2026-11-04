#pragma once
#include "FillTexture.hh"
#include "../spsm/bitmaps/SpsmBitmap.hh"
#include <mutex>
namespace bts {
// These are explicit caller policies, never translations of source ordinals.
enum class SpsmByteOrder {Unknown,LittleEndian,BigEndian};
enum class SpsmRowOrder {Unknown,FirstRowIsV0,FirstRowIsV1};
enum class SpsmAlpha {Unknown,Straight,DataComponents};
struct SpsmBitmapPolicy {
 SpsmByteOrder bytes=SpsmByteOrder::Unknown;
 SpsmRowOrder rows=SpsmRowOrder::Unknown;
 SpsmAlpha alpha=SpsmAlpha::Unknown;
 bool raw=true;
 std::string input_space; // Required known OCIO name/interop ID for color.
 bool operator==(const SpsmBitmapPolicy &) const=default;
};
struct SpsmBitmapTexture:std::enable_shared_from_this<SpsmBitmapTexture> {
 FillTexture texture;
 std::shared_ptr<const FillTexture> binding()const{return {shared_from_this(),&texture};}
 spsm::ImageResource source;
 spsm::bitmap::RawImage original; // Original immutable header, encoded and raw owners.
 SpsmBitmapPolicy policy;
 std::string encoded_sha256; // Exact record provenance; native hash also binds interpretation/pixels.
 std::vector<std::string> warnings;
};
// CPU worker only. Color management must already be initialized, as for
// decode_fill_texture. The v1 integer format is deliberately unsupported.
std::shared_ptr<const SpsmBitmapTexture> interpret_spsm_bitmap(
 spsm::bitmap::RawImage,const spsm::ImageResource &,const SpsmBitmapPolicy &,spsm::ReadOptions={});
class SpsmBitmapTextureCache {
 std::mutex mutex_;
 std::map<std::string,std::weak_ptr<const SpsmBitmapTexture>> entries_;
 public:
 // Serializes misses so repeated/concurrent bindings share decoded pixels.
 // Weak entries retain no image allocation; live FillTexture descriptors retain
 // the source owners through their aliasing immutable pixel pointer.
 std::shared_ptr<const SpsmBitmapTexture> load(
  const spsm::bitmap::Descriptor &,const SpsmBitmapPolicy &,spsm::ReadOptions={});
};
}
