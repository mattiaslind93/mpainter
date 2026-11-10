// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialObjectTable.hh"
#include <cassert>
#include <limits>
using namespace bts;
int main()
{
  NativeMaterialObject a, b;
  a.owner = 0xffffffffu;
  a.random = .25;
  b.owner = 0x1000001u;
  b.color = {.2, .3, .4, 1};
  NativeMaterialObjectTable table({a, b});
  assert(table.size() == 2 && table.rows().size() == 24);
  assert(std::bit_cast<std::uint32_t>(table.rows()[0][0]) == b.owner &&
         std::bit_cast<std::uint32_t>(table.rows()[12][0]) == a.owner);
  assert(table.rows()[11] == b.color && table.rows()[12][1] == a.random);
  NativeMaterialObjectTable reordered({b, a});
  assert(reordered.hash() == table.hash());
  b.color[0] = .8;
  assert(NativeMaterialObjectTable({a, b}).hash() != table.hash());
  const auto rejects = [](auto objects) {
    try {
      NativeMaterialObjectTable value(objects);
      return false;
    }
    catch (const std::invalid_argument &) {
      return true;
    }
  };
  assert(rejects(std::vector<NativeMaterialObject>{a, a}));
  a.owner = 0;
  assert(rejects(std::vector<NativeMaterialObject>{a}));
  a.owner = 1;
  a.world_to_object[0] = std::numeric_limits<float>::infinity();
  assert(rejects(std::vector<NativeMaterialObject>{a}));
  {
    TextureRamQueryScope memory([]() -> std::uint64_t { return 0; });
    bool denied = false;
    try {
      NativeMaterialObjectTable value({b});
    }
    catch (const std::length_error &) {
      denied = true;
    }
    assert(!denied);
  }
  NativeMaterialObjectTable empty({});
  assert(empty.size() == 0);
}
