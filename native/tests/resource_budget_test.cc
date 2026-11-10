// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#include "ResourceStack.hh"
#include "StrokeSpacing.hh"
#include <cassert>
#include <iostream>
using namespace bts;
static std::uint64_t memory()
{
  return 32ULL << 30;
}
int main()
{
  TextureRamQueryScope query(memory);
  texture_vram_query_valid = true;
  texture_free_vram = 16ULL << 30;
  const ResourceAvailability memory;
  assert(memory.ram() == 32ULL << 30);
  assert(memory.vram() == 16ULL << 30);
  require_resource_stack(1024, "Stack fixture");
  bool failed = false;
  try {
    require_resource_stack(UINT64_MAX, "Stack fixture");
  }
  catch (const std::length_error &) {
    failed = true;
  }
  assert(failed);
  texture_free_vram = 0;
  assert(ResourceAvailability{}.vram() == 0);
  texture_vram_query_valid = false;
  assert(!ResourceAvailability{}.vram_known);
  failed = false;
  try {
    resource_product(UINT64_MAX, 2);
  }
  catch (const std::overflow_error &) {
    failed = true;
  }
  assert(failed);
  failed = false;
  try {
    resource_sum(UINT64_MAX, 1);
  }
  catch (const std::overflow_error &) {
    failed = true;
  }
  assert(failed);
  StrokeSpacing spacing;
  std::size_t dabs = 0;
  spacing.reset({0, 0}, 1);
  spacing.advance({20000, 0}, [&](auto) {
    ++dabs;
    return 1.;
  });
  assert(dabs == 20000);
  spacing.reset({0, 0}, 1e-10);
  dabs = 0;
  spacing.advance({1e-9, 0}, [&](auto) {
    ++dabs;
    return 1e-10;
  });
  assert(dabs == 10);
  std::cout
      << "Dynamic RAM/VRAM, exhausted VRAM, unknown driver telemetry and checked arithmetic passed\n";
}
