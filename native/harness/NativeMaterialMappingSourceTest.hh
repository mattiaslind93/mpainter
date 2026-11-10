// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialGraph.hh"
#include "FillProjection.hh"
#include "BKE_main.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "BLI_ustring.hh"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "NOD_shader.h"
#include "GPU_pass.hh"
#include "GPU_state.hh"
#include "MEM_guardedalloc.h"
#include <QScopeGuard>
#include <future>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
namespace bts {
// Test-only independent reference: replace one edge in a detached source copy
// by a known constant coordinate, without changing any production source link.
inline QJsonObject verify_native_material_mapping_source(
    const std::shared_ptr<NativeMaterialGraph> &source, blender::gpu::Texture *target)
{
  using namespace blender;
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  const auto selected = std::find_if(
      source->description.parameters.begin(),
      source->description.parameters.end(),
      [](const auto &p) { return p.mapping_transform_supported() && p.source_socket == "UV"; });
  require(selected != source->description.parameters.end(),
          "Actual Mapping test needs a direct UV input");
  const auto index = std::size_t(selected - source->description.parameters.begin());
  const auto original = native_material_description_json(source->description);
  const auto width = GPU_texture_width(target), height = GPU_texture_height(target);
  NativeMaterialGpuLibrary library;
  library.install("actual-source", "actual-source", source);
  FillSurfacePoint point{};
  point.valid = true;
  point.position = {.2, .4, 1};
  point.normal = {0, 0, 1};
  const auto word_count = (sizeof(point) + 15) / 16;
  std::array<std::uint32_t, word_count * 4> packed{};
  std::memcpy(packed.data(), &point, sizeof(point));
  auto *surface = GPU_texture_create_2d_array("Mapping actual source context",
                                              word_count,
                                              1,
                                              1,
                                              1,
                                              gpu::TextureFormat::UINT_32_32_32_32,
                                              GPU_TEXTURE_USAGE_GENERAL,
                                              nullptr);
  require(surface, "Mapping source surface allocation failed");
  const auto release_surface = qScopeGuard([&] { GPU_texture_free(surface); });
  GPU_texture_update(surface, GPU_DATA_UINT, packed.data());
  NativeMaterialGpuContext context;
  context.surface = surface;
  context.surface_size = {1, 1};
  context.surface_stride = sizeof(point) / 4;
  context.color_to_aces = source->color_to_aces;
  context.map_u = {0, 0, .375};
  context.map_v = {0, 0, .625};
  NativeMaterialMapping transform;
  transform.offset = {.125, -.25, 0};
  transform.scale = {.5, 1.5, 1};
  QJsonArray cases;
  std::uint64_t components = 0, changed_components = 0;
  double maximum = 0;
  std::vector<float> identity_values;
  std::size_t compared = 0;
  for (bool authored : {false, true}) {
    const std::array<float, 3> coordinate = authored ? std::array<float, 3>{.3125, .6875, 0} :
                                                       std::array<float, 3>{.375, .625, 0};
    auto reference =
        std::async(std::launch::async, [source, index, coordinate] {
          NativeMaterialGraph::MainOwner owner(BKE_main_new(), BKE_main_free);
          owner->colorspace = source->main->colorspace;
          auto *material = reinterpret_cast<Material *>(
              BKE_id_copy_ex(owner.get(), &source->material->id, nullptr, 0));
          if (!material || !material->nodetree)
            throw std::runtime_error("Cannot copy actual Mapping reference source");
          auto &tree = *material->nodetree;
          const auto find = [](bNode &node, const char *id, bool in) -> bNodeSocket & {
            for (auto &socket : in ? node.inputs : node.outputs)
              if (STREQ(socket.identifier, id))
                return socket;
            throw std::runtime_error("Actual Mapping reference socket missing");
          };
          auto *surface = ntreeShaderOutputNode(&tree, SHD_OUTPUT_EEVEE);
          auto *link = surface ? find(*surface, "Surface", true).link : nullptr;
          auto *top = link ? link->fromnode : nullptr;
          if (!top || !STREQ(top->idname, "ShaderNodeGroup"))
            throw std::runtime_error("Actual Mapping top group missing");
          auto &input = find(
              *top, source->description.parameters[index].identifier.toUtf8().constData(), true);
          if (!input.link)
            throw std::runtime_error("Actual Mapping source edge missing");
          bke::node_remove_link(&tree, *input.link);
          auto *constant = bke::node_add_node(nullptr, tree, UString("ShaderNodeCombineXYZ"));
          if (!constant)
            throw std::runtime_error("Cannot create independent source coordinate");
          for (int c = 0; c < 3; ++c)
            static_cast<bNodeSocketValueFloat *>(find(*constant,
                                                      c == 0 ? "X" :
                                                      c == 1 ? "Y" :
                                                               "Z",
                                                      true)
                                                     .default_value)
                ->value = coordinate[c];
          bke::node_add_link(tree, *constant, find(*constant, "Vector", false), *top, input);
          BKE_ntree_update_after_single_tree_change(*owner, tree);
          auto description = source->description;
          description.parameters[index].mapping = false;
          std::atomic_bool cancel = false;
          auto result = std::make_shared<NativeMaterialGraph>(
              std::move(owner), material, std::move(description), cancel);
          result->color_to_aces = source->color_to_aces;
          result->color_from_aces = source->color_from_aces;
          return result;
        }).get();
    NativeMaterialGpuLibrary oracle;
    oracle.install("independent-coordinate", "independent-coordinate", reference);
    auto instance = source->description;
    if (authored)
      instance.parameters[index].mapping_transform = transform;
    for (int channel : {0, 1, 2, 14}) {
      library.render("actual-source", channel, instance, target, context, {0, 0, width, height});
      auto *actual = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
      require(actual, "Actual Mapping source readback missing");
      const auto free_actual = qScopeGuard([&] { MEM_delete(actual); });
      oracle.render("independent-coordinate",
                    channel,
                    reference->description,
                    target,
                    context,
                    {0, 0, width, height});
      auto *expected = static_cast<float *>(GPU_texture_read(target, GPU_DATA_FLOAT, 0));
      require(expected, "Independent Mapping source readback missing");
      const auto free_expected = qScopeGuard([&] { MEM_delete(expected); });
      for (int i = 0; i < width * height * 4; ++i) {
        const double error = std::abs(actual[i] - expected[i]);
        maximum = std::max(maximum, error);
        if (!authored)
          identity_values.push_back(actual[i]);
        else {
          if (actual[i] != identity_values.at(compared))
            ++changed_components;
          ++compared;
        }
        require(std::isfinite(actual[i]) && (authored ? error < 3e-5 : actual[i] == expected[i]),
                "Actual linked Mapping differs from independent original-source coordinate");
        ++components;
      }
    }
    cases.append(QJsonObject{
        {"authored", authored},
        {"source_uv", QJsonArray{.375, .625, 0}},
        {"independent_mapped_uv", QJsonArray{coordinate[0], coordinate[1], coordinate[2]}},
        {"rotation", 0},
        {"exact_identity", !authored}});
  }
  require(changed_components > 0,
          "Actual source Mapping oracle did not witness an authored change");
  const auto compilations = GPU_pass_global_compilation_count();
  for (int pass = 0; pass < 4; ++pass) {
    auto instance = source->description;
    if (pass % 2)
      instance.parameters[index].mapping_transform = transform;
    for (int channel : {0, 1, 2, 14})
      library.render("actual-source", channel, instance, target, context, {0, 0, width, height});
  }
  require(GPU_pass_global_compilation_count() == compilations,
          "Actual Mapping edits recompiled source programs");
  require(source->original_inputs_preserved &&
              native_material_description_json(source->description) == original,
          "Actual Mapping test changed source defaults or links");
  return {{"input", selected->identifier},
          {"source_basis", "UV"},
          {"cases", cases},
          {"components", qint64(components)},
          {"changed_components", qint64(changed_components)},
          {"maximum_error", maximum},
          {"shader_recompiles", 0},
          {"source_defaults_preserved", true},
          {"independent_detached_edge_reference", true}};
}
}  // namespace bts
