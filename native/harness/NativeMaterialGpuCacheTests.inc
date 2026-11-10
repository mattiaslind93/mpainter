// SPDX-License-Identifier: GPL-3.0-or-later
// Deliberately identical node graphs with different external parameter layouts.
// Without a hosted pass key, the second program reuses the first binding index.
QJsonArray binding_cases;
for (int binding = 0; binding < 2; ++binding) {
  auto *binding_tree = bke::node_tree_add_tree(
      main, "Native binding cache regression", "ShaderNodeTree");
  binding_tree->flag |= NTREE_IS_GPU_SHADER_INTERNAL;
  auto *attribute = bke::node_add_node(nullptr, *binding_tree, UString("ShaderNodeAttribute"));
  const std::string name = "BTS_Param0";
  STRNCPY(static_cast<NodeShaderAttribute *>(attribute->storage)->name, name.c_str());
  auto *aov = bke::node_add_node(nullptr, *binding_tree, UString("ShaderNodeOutputAOV"));
  STRNCPY(static_cast<NodeShaderOutputAOV *>(aov->storage)->name, "BTS_Binding");
  bke::node_add_link(*binding_tree,
                     *attribute,
                     socket(*attribute, "Fac", false),
                     *aov,
                     socket(*aov, "Value", true));
  bke::node_add_node(nullptr, *binding_tree, UString("ShaderNodeOutputMaterial"));
  BKE_ntree_update_after_single_tree_change(*main, *binding_tree);
  Compilation layout{binding_tree, {}, 2};
  layout.parameter_slots = {std::size_t(binding)};
  auto *program = compile_native_material(layout);
  std::unique_ptr<GPUMaterial, decltype(&GPU_material_free_single)> release_program(
      program, GPU_material_free_single);
  require(layout.error.empty() && program && GPU_material_status(program) == GPU_MAT_SUCCESS,
          "Binding layout shader failed");
  auto *binding_shader = GPU_material_get_shader(program);
  const float values[8]{.25, .25, .25, 1, .75, .75, .75, 1};
  auto *buffer = GPU_storagebuf_create_ex(
      sizeof(values), values, GPU_USAGE_DYNAMIC, "Native binding layout oracle");
  std::unique_ptr<gpu::StorageBuf, decltype(&GPU_storagebuf_free)> release_buffer(
      buffer, GPU_storagebuf_free);
  GPU_shader_bind(binding_shader);
  GPU_storagebuf_bind(buffer, 0);
  if (auto *uniform = GPU_material_uniform_buffer_get(program))
    GPU_uniformbuf_bind(uniform, GPU_shader_get_ubo_binding(binding_shader, "node_tree"));
  const auto unbind = qScopeGuard([&] {
    GPU_texture_image_unbind(image);
    GPU_storagebuf_unbind(buffer);
    if (auto *uniform = GPU_material_uniform_buffer_get(program))
      GPU_uniformbuf_unbind(uniform);
    GPU_shader_unbind();
  });
  GPU_texture_image_bind(image, 0);
  GPU_shader_uniform_1i(binding_shader, "selected_aov", int(BLI_hash_string("BTS_Binding")));
  GPU_shader_uniform_1i(binding_shader, "scalar_aov", 1);
  GPU_compute_dispatch(binding_shader, 1, 1, 1);
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE);
  auto *sample = static_cast<float *>(GPU_texture_read(image, GPU_DATA_FLOAT, 0));
  require(sample, "Binding layout oracle read failed");
  const auto free_sample = qScopeGuard([&] { MEM_delete(sample); });
  const float expected = binding ? .75f : .25f;
  std::printf("BTS_NATIVE_BINDING_ORACLE binding=%d actual=%.9g expected=%.9g key=%llu\n",
              binding,
              sample[0],
              expected,
              static_cast<unsigned long long>(GPU_material_uuid_get(program)));
  std::fflush(stdout);
  for (int i = 0; i < 64; ++i)
    require(std::abs(sample[i * 4] - expected) < 1e-6, "Native binding cache alias reproduced");
  binding_cases.append(
      QJsonObject{{"binding", binding},
                  {"value", sample[0]},
                  {"cache_key", QString::number(GPU_material_uuid_get(program))}});
}
require(binding_cases[0].toObject()["cache_key"] != binding_cases[1].toObject()["cache_key"],
        "Native binding layouts shared a pass namespace");
