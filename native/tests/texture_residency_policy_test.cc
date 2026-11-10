// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureResidencyPolicy.hh"
#include <cassert>
#include <iostream>
using namespace bts;
int main()
{
  assert(native_texture_storage_bytes(1, 2, 2, 0, 2, 1, 16) == 64);
  assert(native_texture_storage_bytes(1, 8, 3, 0, 3, 4, 16) == 720);
  assert(native_texture_storage_bytes(2, 512, 256, 2, 2, 10, 16) == 5592416);
  assert(native_texture_storage_bytes(3, 4, 4, 4, 1, 3, 4) == 292);
  bool overflow = false;
  try {
    native_texture_storage_bytes(3, INT32_MAX, INT32_MAX, INT32_MAX, 1, 1, 16);
  }
  catch (const std::overflow_error &) {
    overflow = true;
  }
  assert(overflow);
  assert(coherent_vram_report(1, 0) && !coherent_vram_report(0, 0) && !coherent_vram_report(1, 2));
  assert(available_texture_storage(128 * 1024, 64 * 1024) == 64ULL * 1024 * 1024);
  assert(available_texture_storage(0, 0, 1234) == 1234);
  assert(available_texture_storage(1024, 0, 999, 4321) == 4321);
  AuthoringCacheBudget cache;
  assert(cache.update(1, 100, 200, true, 0) == 300);
  assert(cache.update(1, 100, 200, true, 0) == 300);
  assert(cache.update(2, 100, 0, true, 999) == 100);
  assert(cache.update(3, 100, 0, false, 999) == 1099);
  for (int size : {4096, 8192})
    for (unsigned bpp : {1, 2, 3, 4, 6, 8, 12, 16}) {
      ResidencyRegion region{1, {{size, size}, {size, size / 2}}, bpp, 0, 1};
      for (std::uint64_t free : {0, 1, 1048576}) {
        texture_free_vram = free;
        texture_vram_query_valid = true;
        const auto plan = plan_full_resolution({region});
        assert(plan.size() == 1 && plan[0].mip == 0 &&
               plan[0].bytes == residency_bytes(region, 0));
      }
    }
  bool invalid = false;
  try {
    ResidencyRegion region{1, {{8192, 8192}}, 16};
    plan_full_resolution({region, region});
  }
  catch (const std::invalid_argument &) {
    invalid = true;
  }
  assert(invalid);
  std::cout
      << "Full-resolution 4K/8K UDIM residency retains every texel at zero available VRAM; actual free memory and reusable ownership accounting pass\n";
}
