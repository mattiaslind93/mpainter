// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeWorldNormalShader.hh"
#include "TextureDocument.hh"
#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_ustring.hh"
#include "DNA_image_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "GPU_context.hh"
#include "GPU_pass.hh"
#include <QJsonArray>
#include <QJsonObject>
#include <QScopeGuard>
#include <memory>
#include <stdexcept>

namespace bts {
// Called by the existing CPU native-material driver, after node registration.
// Deliberately builds fresh Main IDs: copying Main can clear embedded NO_MAIN
// and would miss the actual constructor ownership regression.
inline QJsonObject verify_native_world_normal_shader()
{
  using namespace blender;
  const auto require = [](bool ok, const char *reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  require(!GPU_context_active_get(), "World shader metadata test requires no GPU context");
  const auto programs = GPU_pass_global_compilation_count();
  QJsonArray cases;
  for (const auto &tiles : {std::set<int>{1001}, std::set<int>{1002, 1011, 1012}}) {
    std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
    auto *material = BKE_material_add(main.get(), "Fresh native world shader");
    require(material, "Cannot create world shader test material");
    // material_init_data already creates its embedded shader tree. Replacing
    // that pointer would leak the genuine fresh tree and defeat this fixture.
    auto *tree = material->nodetree;
    require(tree && material->nodetree == tree && (tree->id.flag & ID_FLAG_EMBEDDED_DATA) &&
                (tree->id.tag & ID_TAG_NO_MAIN),
            "Fixture did not construct a fresh embedded NO_MAIN tree");
    auto *legacy = bke::node_add_node(nullptr, *tree, UString("ShaderNodeBump"));
    auto *surface = bke::node_add_node(nullptr, *tree, UString("ShaderNodeBsdfPrincipled"));
    auto *output = bke::node_add_node(nullptr, *tree, UString("ShaderNodeOutputMaterial"));
    require(legacy && surface && output, "Cannot create world shader test nodes");
    const auto socket = [&](bNode &node, bool out, const char *name) -> bNodeSocket & {
      auto *s = bke::node_find_socket(node, out ? SOCK_OUT : SOCK_IN, UString(name));
      require(s, "World shader fixture socket missing");
      return *s;
    };
    bke::node_add_link(*tree,
                       *legacy,
                       socket(*legacy, true, "Normal"),
                       *surface,
                       socket(*surface, false, "Normal"));
    bke::node_add_link(*tree,
                       *surface,
                       socket(*surface, true, "BSDF"),
                       *output,
                       socket(*output, false, "Surface"));
    BKE_ntree_update_after_single_tree_change(*main, *tree);
    TextureSet set;
    set.material_session_uid = material->id.session_uid;
    set.tiles = tiles;
    set.texture_resolution = 128;
    set.texture_height = 64;
    if (tiles.size() > 1)
      set.tile_resolutions[1011] = 32;
    std::array<Image *, 2> images{};
    bNode *enabled = nullptr;
    install_native_world_normal_shader(*main, *tree, set, images, enabled, *legacy, *surface);
    require(images[0] && images[1] && images[0] != images[1] && enabled,
            "World shader installation missing handles");
    require(tree->id.tag & ID_TAG_NO_MAIN, "Installation changed embedded ownership flags");
    const auto handles = images;
    const auto *enable_handle = enabled;
    const auto node_count = tree->nodes.count(), image_count = main->images.count();
    // Restore/adoption finds the persistent IDs; it must not allocate a new pair.
    images = {};
    enabled = nullptr;
    install_native_world_normal_shader(*main, *tree, set, images, enabled, *legacy, *surface);
    require(images == handles && enabled == enable_handle && tree->nodes.count() == node_count &&
                main->images.count() == image_count,
            "World shader reinstallation was not idempotent");
    auto &enable_socket = socket(*enabled, true, "Value");
    auto &value = static_cast<bNodeSocketValueFloat *>(enable_socket.default_value)->value;
    require(value == 0, "New world shader was enabled before publication");
    value = 1;
    validate_native_world_normal_shader(*main, *tree, set, images, *legacy, *surface);
    value = 0;
    int rejected = 0;
    const auto reject = [&] {
      bool failed = false;
      try {
        validate_native_world_normal_shader(*main, *tree, set, images, *legacy, *surface);
      }
      catch (const std::invalid_argument &) {
        failed = true;
      }
      require(failed, "Tampered world shader metadata was accepted");
      ++rejected;
    };
    {
      const auto old = images[0]->alpha_mode;
      const auto restore = qScopeGuard([&] { images[0]->alpha_mode = old; });
      images[0]->alpha_mode = IMA_ALPHA_PREMUL;
      reject();
    }
    {
      const auto old = images[0]->id.tag;
      const auto restore = qScopeGuard([&] { images[0]->id.tag = old; });
      images[0]->id.tag |= ID_TAG_NO_MAIN;
      reject();
    }
    {
      auto *old = tree->owner_id;
      const auto restore = qScopeGuard([&] { tree->owner_id = old; });
      tree->owner_id = nullptr;
      reject();
    }
    {
      const auto old = set.id;
      const auto restore = qScopeGuard([&] { set.id = old; });
      set.id = QUuid::createUuid();
      reject();
    }
    {
      auto *alias = bke::node_add_node(nullptr, *tree, UString("ShaderNodeTexImage"));
      require(alias, "Cannot create image-alias negative");
      alias->id = &images[0]->id;
      id_us_plus(alias->id);
      const auto restore = qScopeGuard(
          [&] { bke::node_remove_node(main.get(), *tree, *alias, true); });
      reject();
    }
    {
      const auto restore = qScopeGuard([&] { value = 0; });
      value = .5f;
      reject();
    }
    BKE_ntree_update_after_single_tree_change(*main, *tree);
    validate_native_world_normal_shader(*main, *tree, set, images, *legacy, *surface);
    for (const auto *image : images) {
      require(image->runtime && !image->runtime->cache,
              "World shader builder allocated CPU image buffers");
      for (const auto &target : image->runtime->gputexture)
        for (auto *texture : target)
          require(!texture, "World shader builder allocated GPU storage");
    }
    cases.append(QJsonObject{{"tiles", int(tiles.size())},
                             {"fresh_embedded_no_main", true},
                             {"idempotent", true},
                             {"negative_checks", rejected},
                             {"cpu_image_buffers", 0},
                             {"gpu_targets", 0}});
  }
  require(!GPU_context_active_get() && GPU_pass_global_compilation_count() == programs,
          "World shader metadata test created GPU state");
  return {{"cases", cases}, {"gpu_required", false}, {"gpu_compilations", 0}};
}
}  // namespace bts
