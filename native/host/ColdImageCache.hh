#pragma once
#include "ColdImBuf.hh"
#include "BKE_image.hh"
#include "DNA_image_types.h"
#include <vector>
#include <set>
namespace bts {
// Generated/dirty image caches have no authoritative encoded source. Keep their
// cache header and exact bytes; never silently regenerate the initial color.
class ColdImageCache {
  struct Buffer {
    std::unique_ptr<blender::ImBuf, decltype(&blender::IMB_freeImBuf)> value{
        nullptr, blender::IMB_freeImBuf};
    ColdImBuf disk;
  };
  std::vector<std::unique_ptr<Buffer>> buffers_;

 public:
  void spill(blender::Image &image)
  {
    using namespace blender;
    if (!BKE_image_has_loaded_ibuf(&image))
      return;
    for (auto &tile : image.tiles) {
      ImageUser user{};
      BKE_imageuser_default(&user);
      user.tile = tile.tile_number;
      auto *ibuf = BKE_image_acquire_ibuf(&image, &user, nullptr);
      if (!ibuf)
        continue;
      bool known = false;
      for (const auto &b : buffers_)
        known |= b->value.get() == ibuf;
      if (!known) {
        auto b = std::make_unique<Buffer>();
        IMB_refImBuf(ibuf);
        b->value.reset(ibuf);
        buffers_.push_back(std::move(b));
      }
      BKE_image_release_ibuf(&image, ibuf, nullptr);
    }
    BKE_image_free_gputextures(&image);
    for (auto &b : buffers_)
      b->disk.spill(*b->value);
  }
  void restore()
  {
    for (auto &b : buffers_)
      b->disk.restore(*b->value);
    buffers_.clear();
  }
};
}  // namespace bts
