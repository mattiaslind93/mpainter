// SPDX-License-Identifier: GPL-3.0-or-later
#include "CompositeMemory.hh"
#include <cassert>
int main()
{
  bts::TextureSet set;
  set.texture_resolution = 2048;
  set.tiles = {1001, 1002};
  set.ao_channel = true;
  set.displacement.allocated = true;
  for (auto &user : set.users)
    user.present = true;
  const auto original = bts::complete_composite_channels;
  auto remaining = original;
  bts::CompositeChannels seen{};
  int batches = 0;
  const auto budget = 768ULL * 1024 * 1024;
  assert(bts::composite_peak_bytes(set, true, original) > budget);
  while (std::any_of(remaining.begin(), remaining.end(), [](bool v) { return v; })) {
    const auto batch = bts::composite_batch(set, true, remaining, budget);
    assert(bts::composite_peak_bytes(set, true, batch.channels) <= budget);
    assert(batch.channels != bts::CompositeChannels{});
    for (int c = 0; c < 17; ++c)
      if (batch.channels[c]) {
        assert(!seen[c] && remaining[c]);
        seen[c] = true;
      }
    remaining = batch.remaining;
    assert(++batches < 18);
  }
  assert(seen == original && batches > 1);
  bts::CompositeChannels normal{};
  normal[6] = true;
  const auto dependencies = bts::composite_dependencies(set, true, normal);
  assert(dependencies[3] && dependencies[4] && dependencies[6]);
  const auto no_room = bts::composite_batch(set, true, original, 1);
  assert(no_room.channels != bts::CompositeChannels{} && no_room.remaining != original);
  const auto full = bts::composite_batch(
      set, true, original, bts::composite_peak_bytes(set, true, original));
  assert(full.channels == original && full.remaining == bts::CompositeChannels{});
  return 0;
}
