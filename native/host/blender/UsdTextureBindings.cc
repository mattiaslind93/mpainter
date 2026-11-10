// SPDX-License-Identifier: GPL-3.0-or-later
#include "UsdTextureBindings.hh"
#include "TextureExport.hh"
#include <QDir>
#include <QFileInfo>
#include <map>
#include <stdexcept>
#include "BKE_image.hh"
#include "BKE_main.hh"
#include "BKE_lib_id.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DEG_depsgraph.hh"
#include "NOD_defaults.hh"
#include "SceneSnapshot.hh"
namespace bts {
std::vector<UsdTextureSetExport> usd_texture_exports(const ExportRequest &request)
{
  std::map<std::uint32_t, UsdTextureSetExport> sets;
  for (const auto &file : export_file_list(request)) {
    const int role = file.channel == 0 ? 0 :
                     file.channel == 1 ? 1 :
                     file.channel == 2 ? 2 :
                     file.channel == 7 ? 3 :
                                         -1;
    if (role < 0)
      throw std::runtime_error("USD export requires color, metallic, roughness and occlusion");
    auto &set = sets[file.material_uid];
    set.material_uid = file.material_uid;
    set.tiles.insert(file.tile);
    const auto path = QDir(QString::fromStdString(request.directory))
                          .absoluteFilePath(QString::fromStdString(file.asset_path))
                          .toStdString();
    if (!set.files[role].empty() && set.files[role] != path)
      throw std::runtime_error("USD tile outputs must share one UDIM pattern per map");
    set.files[role] = path;
  }
  std::vector<UsdTextureSetExport> result;
  for (auto &[uid, set] : sets) {
    for (const auto &file : set.files)
      if (file.empty())
        throw std::runtime_error("USD material is missing a required PBR map");
    result.push_back(std::move(set));
  }
  return result;
}
namespace {
using namespace blender;
bNodeSocket &socket(bNode &node, bool out, const char *name)
{
  for (bNodeSocket &value : out ? node.outputs : node.inputs)
    if (STREQ(value.identifier, name) || STREQ(value.name, name))
      return value;
  throw std::runtime_error(std::string("Missing USD material socket: ") + name);
}
Image *image(Main &main, const UsdTextureSetExport &set, int role)
{
  const auto &pattern = set.files[role];
  QString first = QString::fromStdString(pattern);
  first.replace("<UDIM>", QString::number(*set.tiles.begin()));
  auto *image = BKE_image_load(&main, first.toUtf8().constData());
  if (!image)
    throw std::runtime_error("Cannot load exported USD texture");
  STRNCPY(image->colorspace_settings.name,
          role == 0 ? (set.scene_linear_color ? "ACEScg" : "sRGB") : "Non-Color");
  image->alpha_mode = IMA_ALPHA_CHANNEL_PACKED;
  if (pattern.find("<UDIM>") != std::string::npos) {
    image->source = IMA_SRC_TILED;
    STRNCPY(image->filepath, pattern.c_str());
    if (auto *initial = BKE_image_get_tile(image, 1001); initial && *set.tiles.begin() != 1001)
      BKE_image_reassign_tile(image, initial, *set.tiles.begin());
    for (int tile : set.tiles)
      if (!BKE_image_get_tile(image, tile))
        BKE_image_add_tile(image, tile, nullptr);
  }
  return image;
}
}  // namespace
void attach_native_usd_textures(blender::Main &main,
                                blender::Material &material,
                                const UsdTextureSetExport &set)
{
  using namespace blender;
  if (set.tiles.empty())
    throw std::runtime_error("USD texture set has no UV tile");
  nodes::node_tree_shader_default(nullptr, &main, &material.id);
  auto &tree = *material.nodetree;
  auto *principled = native_principled_surface(material);
  if (!principled)
    throw std::runtime_error("USD material has no Principled surface");
  if (set.scene_linear_color)
    write_material_values(main, material, MaterialValues{});
  const std::array<const char *, 3> names{
      "__BTS_UsdColor", "__BTS_UsdMetallic", "__BTS_UsdRoughness"},
      inputs{"Base Color", "Metallic", "Roughness"};
  for (int role = 0; role < 3; ++role) {
    if (set.files[role].empty())
      continue;
    auto *texture = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexImage"));
    if (!texture)
      throw std::runtime_error("Cannot create USD texture node");
    STRNCPY(texture->name, names[role]);
    texture->id = &image(main, set, role)->id;
    id_us_plus(texture->id);
    bke::node_add_link(tree,
                       *texture,
                       socket(*texture, true, "Color"),
                       *principled,
                       socket(*principled, false, inputs[role]));
  }
  BKE_ntree_update_tag_all(&tree);
  BKE_ntree_update_after_single_tree_change(main, tree);
  DEG_id_tag_update_ex(&main, &material.id, ID_RECALC_SHADING | ID_RECALC_SYNC_TO_EVAL);
}
}  // namespace bts
