#pragma once
#include <array>
namespace bts {
// Raw source capture enables ordinary source channels independently of their
// artist toggles. A native Fill must retain its authored Normal flag: internal
// capture must neither invent an artist Normal nor conceal an invalid one.
inline std::array<bool, 5> layer_source_channels(std::array<bool, 5> authored, bool native_fill)
{
  const bool normal = authored[3];
  authored.fill(true);
  if (native_fill)
    authored[3] = normal;
  return authored;
}
}  // namespace bts
