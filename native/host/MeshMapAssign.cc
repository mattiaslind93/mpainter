#include "MeshMapImport.hh"
namespace bts {
std::vector<std::string> assign_utility_maps(TextureDocument &document,
                                             const MeshMapImports &imports)
{
  auto ignored = imports.ignored;
  for (const auto &source : imports.maps) {
    const auto found = std::find_if(
        document.texture_sets().begin(), document.texture_sets().end(), [&](const auto &set) {
          return set.name == source.name.set_name;
        });
    if (found == document.texture_sets().end() || !found->tiles.contains(source.name.tile)) {
      ignored.push_back("Ignored utility map without matching Texture Set / UV tile: " +
                        source.name.resource_name);
      continue;
    }
    auto &set = document.set(found->material_session_uid);
    auto &map = set.mesh_maps.at(source.name.role);
    if (!map)
      map = MeshMap{};
    map->name = source.name.resource_name;
    map->tiles[source.name.tile] = source.plane;
  }
  return ignored;
}
}  // namespace bts
