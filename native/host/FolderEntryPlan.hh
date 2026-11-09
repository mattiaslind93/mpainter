#pragma once
#include "TextureDocument.hh"
#include <QVarLengthArray>
namespace bts {
struct FolderEntry {
  int first = 0, folder = 0;
};
// Validated postorder: entry events in reverse consumption order. O(layers),
// inline for ordinary stacks; no resource/RAM queries on the per-pixel path.
inline QVarLengthArray<FolderEntry, 16> folder_entry_plan(const std::vector<TextureLayer> &layers)
{
  QVarLengthArray<int, 16> open;
  QVarLengthArray<FolderEntry, 16> events;
  for (int i = int(layers.size()) - 1; i >= 0; --i) {
    while (!open.empty() && layers[open.back()].depth >= layers[i].depth) {
      events.push_back({i + 1, open.back()});
      open.pop_back();
    }
    if (layers[i].kind == LayerKind::Folder)
      open.push_back(i);
  }
  while (!open.empty()) {
    events.push_back({0, open.back()});
    open.pop_back();
  }
  return events;
}
}  // namespace bts
