// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QJsonObject>
#include "NativeCyclesImageCache.hh"
#include "NativeCyclesImages.hh"
#include "NativeCyclesPublication.hh"
#include "NativeGpuImages.hh"
#include "BKE_main.hh"
#include "BKE_idtype.hh"
#include "BKE_material.hh"
#include "DNA_material_types.h"
#include "BLI_listbase.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "IMB_colormanagement.hh"
#include "DNA_genfile.h"
#include "CLG_log.h"
#include <iostream>
#include <future>
#include <cstdlib>
#include <new>
namespace {
thread_local bool fail_allocations = false;
}
void *operator new(std::size_t size)
{
  if (fail_allocations)
    throw std::bad_alloc();
  if (auto *p = std::malloc(size ? size : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept
{
  std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
  std::free(p);
}
namespace {
using namespace blender;
void require(bool v, const char *message)
{
  if (!v)
    throw std::runtime_error(message);
}
void reserve_entries(std::vector<bts::NativeCyclesCacheEntry> &entries)
{
  entries.reserve(2);
}
struct MainOwner {
  Main *main = BKE_main_new();
  ~MainOwner()
  {
    for (Image &image : main->images)
      for (auto &target : image.runtime->gputexture)
        for (auto &eye : target)
          eye = nullptr;
    BKE_main_free(main);
  }
};
ImBuf *buffer(float start)
{
  auto *b = IMB_allocImBuf(2, 2, ImBufFlags::FloatData);
  b->channels = 4;
  for (int i = 0; i < 16; ++i)
    b->float_data_for_write()[i] = start + i * .125f;
  IMB_colormanagement_assign_float_colorspace(b, "Non-Color");
  return b;
}
void run()
{
  MainOwner state;
  const float zero[4] = {};
  auto *image = bts::native_generated_image(
      state.main, 2, 2, "BTS cache test", 32, 1, IMA_GENTYPE_BLANK, zero, false, true, false);
  require(image && image->type == IMA_TYPE_UV_TEST, "Native generated descriptor changed");
  auto *sentinel = reinterpret_cast<gpu::Texture *>(std::uintptr_t(0x123400));
  image->runtime->gputexture[TEXTARGET_2D][0] = sentinel;
  const auto flags = image->runtime->gpuflag;
  auto *a = buffer(-.5f);
  ImBuf *previous = nullptr;
  image->runtime->gpuflag = 0;
  require(!BKE_image_publish_hosted_gpu_buffer(image, 1001, a, &previous) && !previous,
          "Ordinary unmarked GPU image was accepted");
  image->runtime->gpuflag = flags;
  require(BKE_image_publish_hosted_gpu_buffer(image, 1001, a, &previous) && !previous,
          "Initial cache publish failed");
  IMB_freeImBuf(a);
  require(image->runtime->gputexture[TEXTARGET_2D][0] == sentinel &&
              image->runtime->gpuflag == flags,
          "CPU publish changed GPU state");
  ImageUser user{};
  user.tile = 1001;
  auto *held = BKE_image_acquire_ibuf(image, &user, nullptr);
  require(held && held->float_data()[0] == -.5f, "Initial immutable CPU buffer differs");
  auto *b = buffer(2.f);
  require(BKE_image_publish_hosted_gpu_buffer(image, 1001, b, &previous) && previous == held,
          "Replacement did not retain old buffer");
  IMB_freeImBuf(b);
  require(held->float_data()[0] == -.5f, "Held old reader was mutated");
  auto *fresh = BKE_image_acquire_ibuf(image, &user, nullptr);
  require(fresh && fresh != held && fresh->float_data()[0] == 2.f &&
              fresh->float_data()[15] == 3.875f,
          "HDR replacement changed precision");
  require((fresh->userflags & IB_PERSISTENT) && !(fresh->userflags & IB_BITMAPDIRTY) &&
              fresh->colorspace_is_data(),
          "Derived cache flags/color changed");
  BKE_image_release_ibuf(image, fresh, nullptr);
  BKE_image_release_ibuf(image, held, nullptr);
  std::async(std::launch::async, [previous] { IMB_freeImBuf(previous); }).get();
  auto *invalid = buffer(5.f);
  invalid->x = 1;
  require(!BKE_image_publish_hosted_gpu_buffer(image, 1001, invalid, &previous) && !previous,
          "Bad shape mutated native cache");
  invalid->x = 2;
  IMB_freeImBuf(invalid);
  auto *tiled = bts::native_generated_image(
      state.main, 2, 2, "BTS tiled cache test", 32, 1, IMA_GENTYPE_BLANK, zero, false, true, true);
  static_cast<ImageTile *>(tiled->tiles.first)->tile_number = 1002;
  tiled->runtime->gputexture[TEXTARGET_2D_ARRAY][0] = sentinel;
  auto *tile = buffer(.25f);
  require(!BKE_image_publish_hosted_gpu_buffer(tiled, 1001, tile, &previous) && !previous,
          "Missing1001 aliased first actual tile1002");
  require(BKE_image_publish_hosted_gpu_buffer(tiled, 1002, tile, &previous),
          "Actual non1001 tile failed");
  IMB_freeImBuf(tile);
  user.tile = 1002;
  fresh = BKE_image_acquire_ibuf(tiled, &user, nullptr);
  require(fresh && fresh->float_data()[0] == .25f, "Actual tile cache key differs");
  BKE_image_release_ibuf(tiled, fresh, nullptr);
  // Exercise the exact production owner loop. Its only old-ref storage is the
  // already prepared entry, including a publisher that mutates then rejects.
  auto *next = buffer(4.f);
  std::array<bts::NativeCyclesCacheEntry, 1> one{{{1, 1001, next, nullptr}}};
  const std::array<std::uint32_t, 1> ids{1};
  bool attempted = false;
  int notifications = 0;
  bts::publish_native_cycles_cache(
      one,
      ids,
      attempted,
      [&](auto, int number, ImBuf *value, ImBuf **old) {
        const bool ok = BKE_image_publish_hosted_gpu_buffer(image, number, value, old);
        if (ok)
          fail_allocations = true;
        return ok;
      },
      [&](auto) { ++notifications; });
  fail_allocations = false;
  require(attempted && notifications == 1 && one[0].previous,
          "Owner bookkeeping allocated after publication");
  IMB_freeImBuf(next);
  std::async(std::launch::async, [old = one[0].previous] { IMB_freeImBuf(old); }).get();
  std::vector<bts::NativeCyclesCacheEntry> denied;
  bool denied_before = false;
  void (*volatile prepare)(std::vector<bts::NativeCyclesCacheEntry> &) = reserve_entries;
  fail_allocations = true;
  try {
    prepare(denied);
  }
  catch (const std::bad_alloc &) {
    denied_before = true;
  }
  fail_allocations = false;
  require(denied_before, "Prepublication resource failure not exercised");
  auto *first = buffer(6.f), *second = buffer(7.f);
  std::array<bts::NativeCyclesCacheEntry, 2> partial{
      {{1, 1001, first, nullptr}, {2, 1002, second, nullptr}}};
  const std::array<std::uint32_t, 2> both{1, 2};
  attempted = false;
  notifications = 0;
  bool rejected = false;
  int calls = 0;
  try {
    bts::publish_native_cycles_cache(
        partial,
        both,
        attempted,
        [&](auto id, int number, ImBuf *value, ImBuf **old) {
          if (++calls == 2)
            return false;
          return BKE_image_publish_hosted_gpu_buffer(id == 1 ? image : tiled, number, value, old);
        },
        [&](auto) { ++notifications; });
  }
  catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected && attempted && notifications == 0 && partial[0].previous &&
              !partial[1].previous,
          "Partial publication advertised completion or lost old keeper");
  std::async(std::launch::async, [old = partial[0].previous] { IMB_freeImBuf(old); }).get();
  partial[0].previous = nullptr;
  attempted = false;
  bts::publish_native_cycles_cache(
      partial,
      both,
      attempted,
      [&](auto id, int number, ImBuf *value, ImBuf **old) {
        return BKE_image_publish_hosted_gpu_buffer(id == 1 ? image : tiled, number, value, old);
      },
      [&](auto) { ++notifications; });
  require(attempted && notifications == 2, "Same-generation retry did not complete both images");
  for (auto *target : {image, tiled}) {
    user.tile = target == image ? 1001 : 1002;
    fresh = BKE_image_acquire_ibuf(target, &user, nullptr);
    require(fresh && fresh->float_data()[0] == (target == image ? 6.f : 7.f),
            "Retried cache generation differs");
    BKE_image_release_ibuf(target, fresh, nullptr);
  }
  std::async(std::launch::async, [a = partial[0].previous, b = partial[1].previous] {
    if (a)
      IMB_freeImBuf(a);
    if (b)
      IMB_freeImBuf(b);
  }).get();
  IMB_freeImBuf(first);
  IMB_freeImBuf(second);
  // Exercise the actual coordinator's unmet-entry/error gate without a GL
  // context or a large allocation. A live required Image with no GPU storage
  // must not disappear from the pending entry requirement or report ready.
  auto *missing = bts::native_generated_image(state.main,
                                              2,
                                              2,
                                              "BTS missing GPU test",
                                              32,
                                              1,
                                              IMA_GENTYPE_BLANK,
                                              zero,
                                              false,
                                              true,
                                              false);
  auto *material = BKE_material_add(state.main, "BTS missing GPU owner");
  require(material, "Missing GPU fixture has no live material owner");
  bts::NativeCyclesImages coordinator;
  coordinator.changed(*missing, material->id.session_uid, 0);
  bool reported = false;
  int warnings = 0;
  auto update = [&](bool active) {
    return bts::native_cycles_recorded_error_boundary(
        reported,
        [&] { return coordinator.update(*state.main, active); },
        [&] { return coordinator.error(); },
        [&](std::string_view) noexcept { ++warnings; });
  };
  require(!update(true) && !coordinator.error().empty() && !coordinator.pending() && warnings == 1,
          "Missing required GPU storage was not deferred with an actionable error");
  for (int i = 0; i < 3; ++i)
    require(!update(true) && !coordinator.pending() && warnings == 1,
            "Same failed generation retried, warned repeatedly or falsely became ready");
  require(update(false), "Painting remained blocked by a failed Rendering entry");
  coordinator.changed(*missing, material->id.session_uid, 0);
  require(coordinator.error().empty(), "A new GPU generation did not clear the paused error");
  require(!update(true) && !coordinator.error().empty() && warnings == 2,
          "Explicit reentry/new-generation retry did not revalidate missing storage");
  coordinator.reset();
  require(coordinator.error().empty(), "Reset retained a stale paused error");
  std::cout
      << "CYCLES_CACHE_CPU_PASS held-reader immutable; UV_TEST+TILED exact key; signed/HDR; persistent/data/clean; coordinator missingGPU paused/no-spin/retry; fake-GPU sentinel unchanged (actual GPU proof separate)\n";
}
}  // namespace
int main()
{
  try {
    CLG_init();
    blender::DNA_sdna_current_init();
    blender::BKE_idtype_init();
    blender::IMB_init();
    run();
    blender::IMB_exit();
    CLG_exit();
    return 0;
  }
  catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
