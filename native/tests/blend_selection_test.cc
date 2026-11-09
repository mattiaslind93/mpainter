// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlendLibrary.hh"
#include <cassert>
#include <cstdio>

int main()
{
  using namespace bts;
  BlendMeshSelection selection{{"Body", "Öga"}, {"Character", "Props"}};
  selection.validate(true);
  assert(BlendMeshSelection::from_json(selection.to_json()) == selection);
  assert(BlendMeshSelection::from_json(BlendMeshSelection{}.to_json()).empty());
  const auto rejects = [](const auto &operation) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
  };
  rejects([] { BlendMeshSelection{}.validate(true); });
  auto duplicate = selection;
  duplicate.objects.push_back("Body");
  rejects([&] { duplicate.validate(); });
  auto invalid = selection;
  invalid.objects.push_back(std::string("bad\0name", 8));
  rejects([&] { invalid.validate(); });
  invalid.objects.back() = std::string(1, char(0xff));
  rejects([&] { invalid.validate(); });
  auto data = selection.to_json();
  data["objects"] = QJsonArray{42};
  rejects([&] { BlendMeshSelection::from_json(data); });
  data = selection.to_json();
  data["unknown"] = true;
  rejects([&] { BlendMeshSelection::from_json(data); });
  BlendMeshSelection large;
  for (int i = 0; i < 4097; ++i) large.objects.push_back("Object " + std::to_string(i));
  large.validate();assert(BlendMeshSelection::from_json(large.to_json())==large);
  {TextureRamQueryScope low(+[]()->std::uint64_t{return 0;});bool denied=false;try{BlendMeshSelection::from_json(large.to_json());}catch(const std::length_error &){denied=true;}assert(!denied);}
  std::puts("Blender library selection: exact Unicode roundtrip, empty/default and invalid selection cases PASS");
}
