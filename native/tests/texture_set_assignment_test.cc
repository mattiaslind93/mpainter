// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureSetAssignment.hh"
#include <cassert>
#include <iostream>
int main()
{
  using namespace bts;
  TextureSet old, sphere, fresh;
  old.name = "CubeSurface";
  old.material_session_uid = 10;
  old.disabled = true;
  sphere.name = "SphereSurface";
  sphere.material_session_uid = 20;
  fresh.name = "CompanionRenamed";
  fresh.material_session_uid = 30;
  const std::vector<TextureSet> sets{old, sphere, fresh};
  TextureSetAssignment state(sets);
  assert(state.state() == TextureSetAssignment::State({sphere.id, fresh.id}));
  state.assign(1, old.id);
  assert(state.can_undo() && !state.can_redo());
  auto result = state.result();
  assert(result[0].id == old.id && result[0].name == old.name && !result[0].disabled &&
         result[0].material_session_uid == 30);
  assert(result[2].disabled && result[2].material_session_uid == 10);
  assert(result[1].material_session_uid == 20 && !result[1].disabled);
  state.undo();
  assert(state.can_redo() && !state.can_undo());
  assert(state.result()[0].disabled);
  state.redo();
  assert(state.result()[0].material_session_uid == 30);
  state.assign(0, old.id);
  assert(state.state() == TextureSetAssignment::State({old.id, sphere.id}));
  auto mismatch = sets;
  mismatch[0].tiles = {1002};
  TextureSetAssignment invalid(mismatch);
  invalid.assign(1, old.id);
  bool rejected = false;
  try {
    invalid.result();
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(sets[0].disabled && sets[0].material_session_uid == 10);
  std::cout << "Disabled restoration, active swap, local Undo/Redo and UV-tile rejection passed\n";
}
