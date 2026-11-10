// SPDX-License-Identifier: GPL-3.0-or-later
// The retained native expression executes first. The independent oracle applies
// Add/Multiply to each original owner/domain coordinate, THEN authored POINT.
QJsonObject vector_math_report;
{
  QJsonArray cases;
  std::uint64_t exact_identity = 0, analytic = 0, reset_checks = 0;
  double maximum = 0;
  for (const char *basis : {"UV", "Generated", "Object"})
    for (int operation : {NODE_VECTOR_MATH_ADD, NODE_VECTOR_MATH_MULTIPLY}) {
      auto adapted = native_mapping_test::vector_math_fixture(basis, operation),
           plain = native_mapping_test::vector_math_fixture(basis, operation);
      const auto retained = adapted.description;
      const auto metadata = native_material_description_json(retained);
      for (auto &p : plain.description.parameters) {
        p.mapping = false;
        p.editable = false;
      }
      std::atomic_bool cancel = false;
      auto graph = std::make_shared<NativeMaterialGraph>(
               std::move(adapted.owner), adapted.material, retained, cancel),
           original = std::make_shared<NativeMaterialGraph>(
               std::move(plain.owner), plain.material, plain.description, cancel);
      NativeMaterialGpuLibrary library, reference;
      library.install("vector-a", "retained-vector-math", graph);
      library.alias("vector-a", "vector-b");
      reference.install("plain-vector", "unadapted-vector-math", original);
      for (int channel : {0, 2})
        for (int tile : {1001, 1002, 1011}) {
          context.tile = tile;
          library.render(
              "vector-a", channel, retained, target.get(), context, {0, 0, width, height});
          auto *actual = pixels();
          require(actual, "Vector Math identity readback absent");
          const auto release = qScopeGuard([&] { MEM_delete(actual); });
          reference.render("plain-vector",
                           channel,
                           plain.description,
                           target.get(),
                           context,
                           {0, 0, width, height});
          auto *expected = pixels();
          require(expected, "Unadapted Vector Math readback absent");
          const auto release_original = qScopeGuard([&] { MEM_delete(expected); });
          for (int i = 0; i < width * height * 4; ++i) {
            require(actual[i] == expected[i], "Extra POINT identity changed retained Vector Math");
            ++exact_identity;
          }
        }
      const auto compilations = GPU_pass_global_compilation_count();
      auto first = retained, second = retained;
      first.parameters[0].mapping_transform = {{-.4, .3, -.2}, {.31, -.23, .47}, {-1.5, .75, 0}};
      first.parameters[1].mapping_transform = {{.2, -.1, .8}, {-.2, .4, -.1}, {.4, -.5, 1.3}};
      second.parameters[0].mapping_transform = {{.1, -.2, .3}, {-.14, .26, -.32}, {.7, 1.2, -.6}};
      require(library.signature("vector-a", 2, second) ==
                  library.signature("vector-a", 2, retained),
              "Vector Math extra transform crossed independent inputs");
      const auto operand = operation == NODE_VECTOR_MATH_ADD ?
                               native_mapping_test::vector_math_addend :
                               native_mapping_test::vector_math_factor;
      for (int pass = 0; pass < 4; ++pass)
        for (int channel : {0, 2})
          for (int tile : {1001, 1002, 1011}) {
            context.tile = tile;
            const auto &instance = pass % 2 ? second : first;
            library.render(pass % 2 ? "vector-b" : "vector-a",
                           channel,
                           instance,
                           target.get(),
                           context,
                           {0, 0, width, height});
            auto *actual = pixels();
            require(actual, "Vector Math authored readback absent");
            const auto release = qScopeGuard([&] { MEM_delete(actual); });
            for (int y = 0; y < height; ++y)
              for (int x = 0; x < width; ++x) {
                std::array<double, 3> value{float((x + .5) / width) + (tile - 1001) % 10,
                                            float((y + .5) / height) + (tile - 1001) / 10,
                                            0};
                if (std::string_view(basis) != "UV") {
                  const auto &owner = x < width / 2 ? mapping_owner_a : mapping_owner_b;
                  for (int r = 0; r < 3; ++r) {
                    float local = owner.world_to_object[12 + r];
                    for (int c = 0; c < 3; ++c)
                      local += owner.world_to_object[c * 4 + r] *
                               points[y * width + x].position[c];
                    value[r] = std::string_view(basis) == "Generated" ?
                                   (local - owner.bounds_min[r]) /
                                       std::max(1e-20f,
                                                owner.bounds_max[r] - owner.bounds_min[r]) :
                                   local;
                  }
                }
                for (int c = 0; c < 3; ++c)
                  value[c] = operation == NODE_VECTOR_MATH_ADD ? value[c] + operand[c] :
                                                                 value[c] * operand[c];
                value = affine(instance.parameters[channel == 0 ? 0 : 1].mapping_transform, value);
                for (int c = 0; c < 4; ++c) {
                  const double expected = !points[y * width + x].valid ? 0 :
                                          c == 3                       ? 1 :
                                          channel == 0                 ? value[c] :
                                                                         value[0],
                               error = std::abs(actual[(y * width + x) * 4 + c] - expected);
                  maximum = std::max(maximum, error);
                  require(
                      std::isfinite(actual[(y * width + x) * 4 + c]) && error < 3e-5,
                      "Retained Vector Math then POINT differs from independent analytic oracle");
                  ++analytic;
                }
              }
          }
      // The very same program/alias returns exactly to unadapted source output.
      for (int channel : {0, 2})
        for (int tile : {1001, 1002, 1011}) {
          context.tile = tile;
          library.render(
              "vector-a", channel, retained, target.get(), context, {0, 0, width, height});
          auto *actual = pixels();
          require(actual, "Vector Math reset readback absent");
          const auto release = qScopeGuard([&] { MEM_delete(actual); });
          reference.render("plain-vector",
                           channel,
                           plain.description,
                           target.get(),
                           context,
                           {0, 0, width, height});
          auto *expected = pixels();
          require(expected, "Vector Math reset reference absent");
          const auto release_original = qScopeGuard([&] { MEM_delete(expected); });
          for (int i = 0; i < width * height * 4; ++i) {
            require(actual[i] == expected[i],
                    "Vector Math reset did not restore exact source output");
            ++reset_checks;
          }
        }
      require(GPU_pass_global_compilation_count() == compilations &&
                  library.statistics().sources == 1 && library.statistics().resources == 2 &&
                  library.statistics().programs == 2,
              "Vector Math instance edits recompiled/duplicated the retained source");
      require(graph->original_inputs_preserved &&
                  native_material_description_json(retained) == metadata,
              "Vector Math runtime changed original source metadata");
      cases.append(
          QJsonObject{{"basis", basis},
                      {"operation", operation == NODE_VECTOR_MATH_ADD ? "Add" : "Multiply"},
                      {"instances", 2},
                      {"owners", 2},
                      {"independent_inputs", 2},
                      {"tiles", QJsonArray{1001, 1002, 1011}},
                      {"linked_operand_retained", true}});
    }
  // Actual same-byte source hydration and raw-Height sidecars must use the
  // same layout domain as install and preview aliases, in both capability modes.
  const auto color_context = native_mapping_test::vector_math_test_color_context();
  auto archived = std::async(std::launch::async, [color_context] {
                    return native_mapping_test::archived_vector_math_fixture(color_context);
                  }).get();
  NativeMaterialGpuLibrary restored;
  restored.install("restored-old", archived.legacy.hash().toStdString(), archived.legacy_graph);
  restored.install("restored-new", archived.current.hash().toStdString(), archived.current_graph);
  restored.register_content_alias("restored-old", "same-packed-preview");
  restored.register_content_alias("restored-new", "same-packed-preview");
  bool mismatched = false;
  try {
    restored.stage_height("restored-old", std::move(*archived.cross_layout_height));
  }
  catch (const std::invalid_argument &) {
    mismatched = true;
  }
  require(mismatched, "Height lookup admitted the other capability layout");
  restored.prepare_height("restored-old", std::move(*archived.legacy_height));
  restored.prepare_height("restored-new", std::move(*archived.current_height));
  require(restored.height_ready("restored-old") && restored.height_ready("restored-new") &&
              restored.statistics().sources == 2,
          "Actual Height lookup did not retain both immutable layouts");
  std::uint64_t height_components = 0;
  double height_maximum = 0;
  for (bool old : {false, true})
    for (int tile : {1001, 1002, 1011}) {
      auto instance = *(old ? archived.legacy : archived.current).fill_preset->native_material;
      instance.height_policy = NativeHeightPolicy::RawAuthoredFloatV1;
      if (!old)
        instance.parameters[1].mapping_transform.offset[0] = .2;
      context.tile = tile;
      restored.render(old ? "restored-old" : "restored-new",
                      4,
                      instance,
                      target.get(),
                      context,
                      {0, 0, width, height});
      auto *actual = pixels();
      require(actual, "Restored Vector Math Height readback missing");
      const auto release = qScopeGuard([&] { MEM_delete(actual); });
      for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
          for (int c = 0; c < 4; ++c) {
            const double expected = !points[y * width + x].valid ? 0 :
                                    c == 3                       ? 1 :
                                             float((x + .5) / width) + (tile - 1001) % 10 +
                                                 native_mapping_test::vector_math_addend[0] +
                                                 (old ? 0 : .2),
                         error = std::abs(actual[(y * width + x) * 4 + c] - expected);
            height_maximum = std::max(height_maximum, error);
            require(std::isfinite(actual[(y * width + x) * 4 + c]) && error < 3e-5,
                    "Current/legacy archive Height differs from independent scalar oracle");
            ++height_components;
          }
    }
  require(archived.original_hidden, "Packed Vector Math test did not hide its source");
  vector_math_report = {{"height_lookup_layouts", 2},
                        {"cross_layout_height_rejected", mismatched},
                        {"hidden_source_archive_hydration", true},
                        {"height_components", qint64(height_components)},
                        {"height_maximum_error", height_maximum},
                        {"cases", cases},
                        {"exact_identity_components", qint64(exact_identity)},
                        {"analytic_components", qint64(analytic)},
                        {"exact_reset_components", qint64(reset_checks)},
                        {"maximum_error", maximum},
                        {"fixed_bound", 3e-5},
                        {"shader_recompiles", 0}};
}
