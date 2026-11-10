// SPDX-License-Identifier: GPL-3.0-or-later
// Real EXR originals + shared decoder, without GL or a CPU mask compositor.
// The independent mask formula describes the exported R/alpha1 payload itself.
QJsonObject verify_material_parameter_package()
{
  using namespace blender;
  using Buffer = std::unique_ptr<ImBuf, decltype(&IMB_freeImBuf)>;
  QTemporaryDir directory;
  assert(directory.isValid());
  const auto originals_path = directory.filePath("originals");
  assert(QDir().mkdir(originals_path));
  const auto encode = [&](int tile,
                          const QString &name,
                          int width,
                          int height,
                          const std::vector<FillTexture::Pixel> &pixels) {
    auto *image = reinterpret_cast<Image *>(
        BKE_id_new_nomain(ID_IM, "Parameter original encoder"));
    assert(image);
    const auto retire = qScopeGuard([&] { BKE_id_free(nullptr, &image->id); });
    describe_source(*image, width, height);
    image->flag |= IMA_HIGH_BITDEPTH;
    FillTexture source;
    source.asset = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    source.hash = std::string(64, 'a');
    source.name = name.toStdString();
    source.raw = true;
    source.width = width;
    source.height = height;
    source.pixels = std::make_shared<const std::vector<FillTexture::Pixel>>(pixels);
    Buffer header(material_layer_source_pixel_view(source), IMB_freeImBuf);
    assert(BKE_image_assign_hosted_immutable_source(image, 1001, header.get()) &&
           BKE_image_memorypack(image));
    const auto *part = static_cast<const ImagePackedFile *>(image->packedfiles.first);
    assert(part && part->packedfile && part->packedfile->size > 0);
    MaterialParameterFile file;
    file.bytes = QByteArray(static_cast<const char *>(part->packedfile->data),
                            part->packedfile->size);
    file.descriptor = {
        tile,
        name,
        QDir(originals_path).filePath(name),
        QString::fromLatin1(
            QCryptographicHash::hash(file.bytes, QCryptographicHash::Sha256).toHex()),
        file.bytes.size()};
    QFile disk(file.descriptor.path);
    assert(disk.open(QIODevice::WriteOnly) && disk.write(file.bytes) == file.bytes.size());
    return file;
  };
  const auto asset_from = [](const MaterialParameterOriginal &original,
                             const FillTexture &descriptor) {
    StudioAsset asset;
    asset.id = original.asset;
    asset.name = QString::fromStdString(descriptor.name);
    asset.scope = AssetScope::Project;
    asset.usages = {"texture"};
    for (const auto &file : original.files)
      asset.texture_files.push_back(file.descriptor);
    return asset;
  };
  int decoder_calls = 0;
  const auto decode =
      [&](const FillTexture &descriptor, const MaterialParameterOriginal &original, bool mask) {
        ++decoder_calls;
        assert(!mask || descriptor.raw);
        return decode_fill_texture(
            asset_from(original, descriptor), descriptor.raw, descriptor.source_space);
      };
  const auto processor = [](const FillTexture &source) {
    return QString::fromStdString(material_layer_source_color_transform(source).processor);
  };
  MaterialLayerParameters value;
  value.name = "Four portable materials";
  value.tiles = {{1001, {128, 128}}, {1002, {128, 128}}};
  value.shader.ao_intensity = .75f;
  value.shader.normal_intensity = .5f;
  // Start in the actual factory context; the New regression below restores its global change with
  // RAII.
  std::unique_ptr<Main, decltype(&BKE_main_free)> context(BKE_main_new(), BKE_main_free);
  IMB_colormanagement_working_space_init_default(context.get());
  value.working_space = QString::fromUtf8(context->colorspace.scene_linear_name);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      value.scene_linear_to_xyz[r * 3 + c] = context->colorspace.scene_linear_to_xyz[c][r];
  auto *native_material = BKE_material_add(context.get(), "Parameter native target");
  auto &tree = *native_material->nodetree;
  auto *surface = bke::node_add_node(nullptr, tree, UString("ShaderNodeBsdfPrincipled")),
       *output = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputMaterial"));
  const auto socket = [](bNode &node, const char *name, bool input) -> bNodeSocket & {
    auto *found = bke::node_find_socket(node, input ? SOCK_IN : SOCK_OUT, UString(name));
    assert(found);
    return *found;
  };
  BKE_ntree_update_after_single_tree_change(*context, tree);
  bke::node_add_link(
      tree, *surface, socket(*surface, "BSDF", false), *output, socket(*output, "Surface", true));
  BKE_ntree_update_after_single_tree_change(*context, tree);
  // Exercise actual New ACEScg conversion, not a matrix approximation. Restore
  // the process setting on every exit; live Main/document IDs remain untouched.
  const std::string previous_working_space = IMB_colormanagement_working_space_get();
  const auto restore_working_space = qScopeGuard(
      [&] { IMB_colormanagement_working_space_set_from_name(previous_working_space.c_str()); });
  require_material_parameter_surface(*context, *native_material);
  auto &radius = *static_cast<bNodeSocketValueVector *>(
      socket(*surface, "Subsurface Radius", true).default_value);
  const std::array<float, 3> factory_radius{radius.value[0], radius.value[1], radius.value[2]};
  NewProjectProfile::initialize_working_space(*context);
  assert(
      std::string(IMB_colormanagement_working_space_get()) == "ACEScg" &&
      (factory_radius != std::array<float, 3>{radius.value[0], radius.value[1], radius.value[2]}));
  value.working_space = QString::fromUtf8(context->colorspace.scene_linear_name);
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      value.scene_linear_to_xyz[r * 3 + c] = context->colorspace.scene_linear_to_xyz[c][r];
  const auto native_gate = [&](const TextureSet &target) {
    assert(target.material_session_uid == native_material->id.session_uid);
    require_material_parameter_surface(*context, *native_material);
  };
  require_material_parameter_surface(*context, *native_material);
  MaterialParameterOriginal raw;
  raw.asset = QUuid::createUuid();
  for (int tile : {1001, 1002})
    raw.files.push_back(
        encode(tile,
               QString("original.%1.exr").arg(tile),
               2,
               2,
               std::vector<FillTexture::Pixel>(4, {tile == 1001 ? -7.25f : 70000.f, 2, 3, 0})));
  FillTexture raw_descriptor;
  raw_descriptor.name = "Signed HDR data";
  raw_descriptor.raw = true;
  const auto raw_source = decode(raw_descriptor, raw, false);
  assert(raw_source->tiles &&
         raw_source->tiles->at(1001)->pixels->front() == FillTexture::Pixel({-7.25f, 2, 3, 0}) &&
         raw_source->tiles->at(1002)->pixels->front()[0] == 70000.f);
  // One encoded file is used with both raw and Color interpretations: no second
  // physical payload is permitted just because the resource UUID/type differs.
  MaterialParameterOriginal color;
  color.asset = QUuid::createUuid();
  color.files.push_back(raw.files.front());
  color.files.front().descriptor.tile = 0;
  FillTexture color_descriptor;
  color_descriptor.name = "Same encoding AP1";
  color_descriptor.source_space = "ACEScg";
  const auto color_source = decode(color_descriptor, color, false);
  value.originals = {raw, color};
  value.materials[0].textures[0] = color_source;
  value.color_processors[0] = processor(*color_source);
  for (int channel : {1, 2, 4, 5})
    value.materials[1].textures[channel] = raw_source;
  for (int input = 0; input < 4; ++input) {
    auto &material = value.materials[input];
    material.name = "Portable input " + std::to_string(input);
    material.values.base_color = {input / 8.f, .25f, .5f, 1};
    material.values.metallic = input / 4.f;
    material.values.roughness = (4 - input) / 4.f;
    material.values.ambient_occlusion = .875f;
    if (input != 1) {
      material.tiling = {1.5, .75};
      material.offset = {.125, -.125};
      material.rotation = 15 * input;
    }
  }
  const auto opacity = [](int stack, int tile, int x, int y) {
    switch (stack) {
      case 0:
        return .25f + float(x % 2) / 8 + float(tile - 1001) / 8;
      case 1:
        return float((x + 2 * y + tile) % 7) / 8;
      default:
        return float((3 * x + y + tile) % 5) / 8;
    }
  };
  for (int stack = 0; stack < 3; ++stack) {
    MaterialParameterOriginal original;
    original.asset = QUuid::createUuid();
    for (int tile : {1001, 1002}) {
      std::vector<FillTexture::Pixel> pixels(128 * 128);
      for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
          pixels[y * 128 + x] = {opacity(stack, tile, x, y), 0, 0, 1};
      original.files.push_back(
          encode(tile, QString("opacity%1.%2.exr").arg(stack + 1).arg(tile), 128, 128, pixels));
    }
    FillTexture descriptor;
    descriptor.name = "Evaluated opacity " + std::to_string(stack + 1);
    descriptor.raw = true;
    value.masks[stack] = decode(descriptor, original, true);
    value.originals.push_back(std::move(original));
  }
  const auto original_metadata = material_layer_parameters_json(value);
  const auto initial_paths = QDir(originals_path).entryList(QDir::Files);
  assert(initial_paths.size() == 8);
  std::atomic_bool cancel = false;
  const auto package = directory.filePath("parameters");
  write_material_layer_parameter_package(package, value, cancel);
  assert(QDir(package + "/payloads").entryList(QDir::Files).size() == 8 &&
         material_layer_parameters_json(value) == original_metadata);
  // The reader must not depend on original paths, shared in-memory source bytes,
  // or the directory in which the package was originally written.
  assert(QDir(originals_path).removeRecursively());
  const auto moved = directory.filePath("relocated");
  assert(QDir().rename(package, moved));
  const auto portable = read_material_layer_parameter_package(moved, cancel);
  assert(material_layer_parameters_json(portable) == original_metadata);
  for (const auto &original : portable.originals)
    for (const auto &file : original.files)
      assert(file.bytes.isEmpty() && file.descriptor.path.startsWith(moved + '/'));
  SceneSnapshot scene;
  MaterialSummary material;
  material.session_id = native_material->id.session_uid;
  material.name = "Target";
  material.uniform_editable = true;
  scene.materials.push_back(material);
  ImportSummary imported;
  imported.materials.push_back({material.session_id, "Target"});
  imported.materials.back().tiles = {1001, 1002};
  auto *unrelated_native = BKE_material_add(context.get(), "Unrelated parameter target");
  auto unrelated_summary = material;
  unrelated_summary.session_id = unrelated_native->id.session_uid;
  unrelated_summary.name = "Unrelated target";
  scene.materials.push_back(unrelated_summary);
  imported.materials.push_back({unrelated_summary.session_id, unrelated_summary.name});
  imported.materials.back().tiles = {1001, 1002};
  NewProjectOptions options;
  options.resolution = DocumentResolution::Pixels128;
  options.uv_workflow = UvWorkflow::PreserveTiles;
  TextureDocument document(options, imported, scene);
  auto &target = document.set(material.session_id);
  TextureDocument::add_user_channel(target, 0);
  const auto before = document.to_json();
  const auto reconstruct = [&](const MaterialLayerParameters &parameters) {
    return reconstruct_material_layer_parameters(target,
                                                 parameters,
                                                 value.working_space,
                                                 value.scene_linear_to_xyz,
                                                 decode,
                                                 processor,
                                                 native_gate);
  };
  auto result = reconstruct(portable);
  assert((document.to_json() == before && result.material_layering.enabled &&
          result.shader_parameters == value.shader &&
          result.material_layering.mask_slots == std::array<int, 3>({1, 2, 3})));
  for (int input = 0; input < 4; ++input)
    assert(layer_material_json(result.material_layering.materials[input]) ==
           layer_material_json(value.materials[input]));
  // The document-level shader library must adopt parameters independently.
  const auto document_proof = verify_material_layer_parameter_document(
      document, material.session_id, portable, decode, processor, native_gate);
  auto colliding_document = document;
  auto local_library = colliding_document.shader_instances();
  local_library.create(local_library.assignments.at(target.id), "Imported materials");
  colliding_document.set_shader_instances(std::move(local_library));
  auto disabled_document_parameters = portable;
  disabled_document_parameters.enabled = false;
  const auto disabled_document_proof = verify_material_layer_parameter_document(
      colliding_document,
      material.session_id,
      disabled_document_parameters,
      decode,
      processor,
      native_gate);
  assert(document.to_json() == before &&
         disabled_document_proof["fresh_local_shader_instance"].toBool());
  const int before_missing_target = decoder_calls;
  bool missing_target_rejected = false;
  try {
    reconstruct_material_layer_document(document,
                                        0,
                                        portable,
                                        value.working_space,
                                        value.scene_linear_to_xyz,
                                        decode,
                                        processor,
                                        native_gate);
  }
  catch (const std::invalid_argument &) {
    missing_target_rejected = true;
  }
  assert(missing_target_rejected && decoder_calls == before_missing_target &&
         document.to_json() == before);
  // The product worker runs the real reader/shared decoder, with the actual
  // native surface gate BEFORE dispatch. These owner hooks deliberately do not
  // claim GPU publication; the Import GUI actor covers that separate boundary.
  QJsonObject import_job_proof;
  {
    MaterialParameterImportTarget admitted;
    admitted.project = document.id();
    admitted.set = target.id;
    admitted.uid = material.session_id;
    admitted.revision = 7;
    admitted.working_space = value.working_space;
    admitted.scene_linear_to_xyz = value.scene_linear_to_xyz;
    admitted.signature = document.edit_state();
    bool current = true, busy = true, deny = false;
    int gates = 0, ready = 0, claims = 0, releases = 0, publishes = 0;
    MaterialParameterImportHooks hooks;
    hooks.current = [&](const auto &identity) {
      return current && identity.project == document.id() && identity.set == target.id &&
             identity.revision == 7;
    };
    hooks.validate = [&](const auto &) {
      ++gates;
      native_gate(target);
      if (deny)
        throw std::invalid_argument("Actual native target rejected before Import dispatch");
    };
    hooks.busy = [&] {
      ++ready;
      return busy;
    };
    hooks.claim = [&](const auto &token) {
      assert(!token.isNull());
      ++claims;
      return true;
    };
    hooks.release = [&](const auto &token) {
      assert(!token.isNull());
      ++releases;
    };
    hooks.publish = [&](auto &candidate, const auto &token) {
      assert(!token.isNull() && candidate.document &&
             candidate.target.signature == admitted.signature);
      assert((candidate.document->set(material.session_id).material_layering.mask_slots ==
                  std::array<int, 3>({1, 2, 3}) &&
              candidate.document->set(material.session_id).shader_parameters == value.shader &&
              candidate.originals.size() == portable.originals.size()));
      ++publishes;
      return true;
    };
    MaterialLayerParameterImportJob job(std::move(hooks));
    const auto until = [&](auto condition) {
      QElapsedTimer deadline;
      deadline.start();
      while (!condition() && deadline.elapsed() < 30000) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
        QThread::msleep(1);
      }
      assert(condition());
    };
    deny = true;
    bool rejected_before_worker = false;
    try {
      job.start(admitted, document, "/absent-parameter-package");
    }
    catch (const std::invalid_argument &) {
      rejected_before_worker = true;
    }
    assert(rejected_before_worker && !job.pending() && gates == 1 && claims == 0);
    deny = false;
    // A newly requested Import must fail owner admission when Save is already
    // pending, before worker dispatch; it must never join Save's mutual wait.
    current = false;
    bool unavailable_rejected = false;
    try {
      job.start(admitted, document, moved);
    }
    catch (const std::runtime_error &) {
      unavailable_rejected = true;
    }
    assert(unavailable_rejected && !job.pending() && gates == 1 && ready == 0 && claims == 0 &&
           publishes == 0);
    current = true;
    job.start(admitted, document, moved);
    job.cancel();
    until([&] { return !job.pending(); });
    assert(job.status().cancelled && job.status().error.isEmpty() && claims == 0 &&
           publishes == 0 && document.to_json() == before);
    job.start(admitted, document, moved);
    until([&] { return ready > 0 || !job.pending(); });
    assert(job.pending() && ready > 0 && claims == 0 && job.status().error.isEmpty());
    job.cancel();
    until([&] { return !job.pending(); });
    assert(job.status().cancelled && job.status().error.isEmpty() && publishes == 0 &&
           document.to_json() == before);
    ready = 0;
    job.start(admitted, document, moved);
    until([&] { return ready > 0 || !job.pending(); });
    assert(job.pending() && ready > 0);
    current = false;
    until([&] { return !job.pending(); });
    assert(job.status().cancelled && claims == 0 && publishes == 0 &&
           document.to_json() == before);
    current = true;
    busy = false;
    job.start(admitted, document, moved);
    until([&] { return !job.pending(); });
    assert(!job.status().cancelled && job.status().error.isEmpty() && claims == 1 &&
           releases == 1 && publishes == 1 && document.to_json() == before);
    job.shutdown();
    import_job_proof = {{"unavailable_owner_rejected_before_worker", true},
                        {"real_reader_decoder_candidate", true},
                        {"native_gate_before_worker", true},
                        {"cancel_worker_and_ready_busy_result", true},
                        {"stale_ready_result_retired", true},
                        {"owner_lease_released_once", true},
                        {"live_document_unchanged", true},
                        {"GPU_publication_tested_here", false}};
  }
  {
    MaterialInspection preparation;
    const auto first = QUuid::createUuid(), second = QUuid::createUuid();
    const auto source = portable.masks[0];
    assert(!preparation.prepare_texture_resource(source));
    assert(
        !preparation.claim_texture_preparation(first));  // Active legacy upload cannot be stolen.
    preparation.cancel_texture_preparation();
    assert(preparation.claim_texture_preparation(first));
    assert(preparation.claim_texture_preparation(first));
    assert(!preparation.claim_texture_preparation(second));
    assert(!preparation.prepare_texture_resources(first, {source}));
    preparation.cancel_texture_preparation();
    assert(!preparation.claim_texture_preparation(second) &&
           !preparation.prepare_texture_resources({source}));
    assert(!preparation.cancel_texture_preparation(second) &&
           preparation.cancel_texture_preparation(first));
    assert(preparation.claim_texture_preparation(second));
    assert(!preparation.cancel_texture_preparation(first));
    assert(preparation.claim_texture_preparation(second) &&
           preparation.cancel_texture_preparation(second));
    import_job_proof["active_legacy_and_stale_token_isolation"] = true;
  }
  QJsonObject import_catalog_proof;
  {
    auto incoming = asset_from(portable.originals.front(), raw_descriptor);
    incoming.filename = portable.originals.front().files.front().descriptor.filename;
    incoming.path = portable.originals.front().files.front().descriptor.path;
    const auto state = [](const AssetCatalog &catalog) {
      QJsonArray rows;
      for (const auto &asset : catalog.assets())
        rows.append(QJsonObject{{"metadata", asset_metadata(asset)},
                                {"scope", int(asset.scope)},
                                {"revision", qint64(asset.revision)}});
      return rows;
    };
    int rejected = 0;
    for (int kind = 0; kind < 5; ++kind) {
      QTemporaryDir library;
      assert(library.isValid());
      AssetCatalog catalog(library.path());
      auto existing = incoming;
      if (kind == 0)
        existing.scope = AssetScope::Builtin;
      if (kind == 1)
        existing.scope = AssetScope::User;
      if (kind == 2) {
        existing.usages = {"alpha"};
        existing.texture_files.clear();
        QFile file(incoming.texture_files.front().path);
        assert(file.open(QIODevice::ReadOnly));
        existing.bytes = file.readAll();
        assert(!existing.bytes.isEmpty());
      }
      if (kind == 3) {
        existing.texture_files.clear();
        for (const auto &member : portable.originals.back().files)
          existing.texture_files.push_back(member.descriptor);
        assert(existing.hash() != incoming.hash());
      }
      if (kind == 4)
        existing.name = "Another descriptor name";
      catalog.add(existing);
      const auto before_catalog = state(catalog);
      QFile user_file(library.filePath("library.json"));
      QByteArray before_user;
      if (user_file.exists()) {
        assert(user_file.open(QIODevice::ReadOnly));
        before_user = user_file.readAll();
        user_file.close();
      }
      QString reason;
      try {
        prepare_material_parameter_catalog(catalog, std::array{incoming});
      }
      catch (const std::invalid_argument &error) {
        reason = QString::fromUtf8(error.what());
      }
      const QString expected_reason =
          kind < 3 ?
              "Parameter source identity collides with a resource outside this project's textures" :
          kind == 3 ? "Parameter source identity collides with a different original" :
                      "Parameter source identity collides with a different recorded source name";
      assert(reason == expected_reason && state(catalog) == before_catalog);
      if (!before_user.isEmpty()) {
        assert(user_file.open(QIODevice::ReadOnly) && user_file.readAll() == before_user);
        user_file.close();
      }
      else
        assert(!user_file.exists());
      ++rejected;
    }
    AssetCatalog catalog(QString{});
    auto stale = incoming;
    stale.path = "/absent-old-parameter-source.exr";
    for (auto &file : stale.texture_files)
      file.path = "/absent-old-parameter-" + file.filename;
    stale.search_terms = {"retain-existing-tag"};
    catalog.add(stale);
    const auto before_catalog = state(catalog);
    const auto prepared = prepare_material_parameter_catalog(catalog, std::array{incoming});
    assert(state(catalog) == before_catalog);
    const auto project = prepared.project_assets();
    assert(project.size() == 1 && project.front().id == incoming.id &&
           project.front().hash() == incoming.hash() &&
           project.front().texture_files == incoming.texture_files &&
           project.front().path == incoming.path &&
           project.front().search_terms == stale.search_terms);
    // Exercise the actual native Save resource boundary with the refreshed disk
    // descriptors, not merely a metadata serialization or a mocked persistence.
    std::unique_ptr<Main, decltype(&BKE_main_free)> packing(BKE_main_new(), BKE_main_free);
    auto *scene = BKE_scene_add(packing.get(), "Imported catalog persistence");
    pack_project_assets(*packing, *scene, project);
    const auto reopened = read_project_assets(*scene);
    assert(reopened.size() == 1 && reopened.front().id == incoming.id &&
           reopened.front().hash() == incoming.hash() &&
           reopened.front().texture_files.size() == incoming.texture_files.size());
    for (std::size_t i = 0; i < incoming.texture_files.size(); ++i) {
      const auto &actual = reopened.front().texture_files[i],
                 &expected = incoming.texture_files[i];
      assert(actual.tile == expected.tile && actual.sha256 == expected.sha256 &&
             actual.size == expected.size);
    }
    import_catalog_proof = {{"wrong_scope_usage_hash_name_rejections", rejected},
                            {"live_catalog_unchanged", true},
                            {"existing_Project_original_once", true},
                            {"incoming_verified_paths_refreshed", true},
                            {"unrelated_metadata_retained", true},
                            {"actual_native_resource_pack_read", true},
                            {"User_library_unchanged", true}};
  }
  int samples = 0;
  for (int stack = 0; stack < 3; ++stack) {
    const auto root = result.material_layering.mask_roots[stack];
    const auto &layer = TextureDocument::layer(result,
                                               result.material_layering.mask_selections[stack]);
    const int slot = result.material_layering.mask_slots[stack];
    assert(layer.parent == root && layer.kind == LayerKind::Fill && layer.fill.mode == 6 &&
           result.users[slot].precision == ChannelPrecision::L32F);
    const auto &mask = fill_channel_texture(layer.fill, 6 + slot);
    for (int tile : {1001, 1002})
      for (const auto p : {std::array<int, 2>{0, 0}, {63, 41}, {127, 127}}) {
        const auto &pixel = mask->tiles->at(tile)->pixels->at(p[1] * 128 + p[0]);
        assert(pixel == FillTexture::Pixel({opacity(stack, tile, p[0], p[1]), 0, 0, 1}));
        ++samples;
      }
  }
  // Evaluated masks are same-size transport, not a new filtered Fill source.
  // Exercise the effective mapping: the earlier payload checks alone cannot
  // detect an extra quarter-pixel HQ filter during native composition.
  int transported = 0;
  for (int stack = 0; stack < 3; ++stack) {
    const auto &layer = TextureDocument::layer(result,
                                               result.material_layering.mask_selections[stack]);
    const int slot = result.material_layering.mask_slots[stack];
    auto mapping = layer.fill;
    mapping.texture = fill_channel_texture(layer.fill, 6 + slot);
    assert(mapping.filtering == 2);
    for (int tile : {1001, 1002})
      for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
          const auto actual = fill_tile_match_sample(mapping, x, y, 128, 128, tile);
          assert(actual == FillTexture::Pixel({opacity(stack, tile, x, y), 0, 0, 1}));
          ++transported;
        }
  }
  {
    const auto &layer = TextureDocument::layer(result,
                                               result.material_layering.mask_selections[0]);
    auto old_hq = layer.fill;
    old_hq.texture = fill_channel_texture(layer.fill, 6 + result.material_layering.mask_slots[0]);
    old_hq.filtering = 0;
    const auto filtered = fill_tile_match_sample(old_hq, 1, 1, 128, 128, 1001);
    assert(opacity(0, 1001, 1, 1) == .375f && filtered[0] == .34375f);
  }
  int composed = 0;
  for (int stack = 0; stack < 3; ++stack) {
    auto selected = result;
    select_material_substack(selected, stack);
    const int slot = selected.material_layering.mask_slots[stack];
    for (int tile : {1001, 1002})
      for (const auto point : {std::array<int, 2>{1, 1}, {2, 1}, {63, 41}}) {
        const auto actual = TextureDocument::composite_user(
            selected, slot, point[0], point[1], tile);
        assert(actual[0] == opacity(stack, tile, point[0], point[1]) && actual[1] == 1);
        ++composed;
      }
  }
  auto disabled = portable;
  disabled.enabled = false;
  const auto retained = reconstruct(disabled);
  assert(!retained.material_layering.enabled && retained.material_layering.allocated() &&
         retained.material_layering.active_mask == -1);
  int rejected = 0;
  const auto reject = [&](const auto &operation) {
    bool failed = false;
    try {
      operation();
    }
    catch (const std::exception &) {
      failed = true;
    }
    assert(failed && document.to_json() == before);
    ++rejected;
  };
  // Same production sampler admission, with actual native NodeTexImage storage;
  // no capture/GL call is made by either the positive or corruption checks.
  auto *mask_sample = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexImage"));
  assert(mask_sample && mask_sample->storage);
  require_material_parameter_mask_sampling(*mask_sample);
  auto &mask_settings = *static_cast<NodeTexImage *>(mask_sample->storage);
  const float saved_location = mask_settings.base.tex_mapping.loc[0],
              saved_matrix = mask_settings.base.tex_mapping.mat[3][0];
  const int saved_frames = mask_settings.iuser.frames;
  const auto before_sampling_decoder_calls = decoder_calls;
  mask_settings.base.tex_mapping.loc[0] = .125f;
  mask_settings.base.tex_mapping.mat[3][0] = .125f;
  reject([&] { require_material_parameter_mask_sampling(*mask_sample); });
  mask_settings.base.tex_mapping.loc[0] = saved_location;
  mask_settings.base.tex_mapping.mat[3][0] = saved_matrix;
  mask_settings.iuser.frames = 101;
  reject([&] { require_material_parameter_mask_sampling(*mask_sample); });
  mask_settings.iuser.frames = saved_frames;
  require_material_parameter_mask_sampling(*mask_sample);
  assert(decoder_calls == before_sampling_decoder_calls && !GPU_context_active_get());
  bke::node_remove_node(context.get(), tree, *mask_sample, true);
  reject([&] { write_material_layer_parameter_package(moved, value, cancel); });
  cancel = true;
  const auto cancelled = directory.filePath("cancelled");
  reject([&] { write_material_layer_parameter_package(cancelled, value, cancel); });
  assert(!QFileInfo::exists(cancelled) &&
         QDir(directory.path())
             .entryList({".bts-parameters-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
             .isEmpty());
  cancel = false;
  reject([&] {
    reconstruct_material_layer_parameters(target,
                                          portable,
                                          "another working space",
                                          value.scene_linear_to_xyz,
                                          decode,
                                          processor,
                                          native_gate);
  });
  auto matrix = value.scene_linear_to_xyz;
  matrix[0] += .01;
  reject([&] {
    reconstruct_material_layer_parameters(
        target, portable, value.working_space, matrix, decode, processor, native_gate);
  });
  reject([&] {
    reconstruct_material_layer_parameters(
        target,
        portable,
        value.working_space,
        value.scene_linear_to_xyz,
        decode,
        [](const FillTexture &) { return QString("different OCIO processor"); },
        native_gate);
  });
  for (int kind = 0; kind < 4; ++kind) {
    auto incompatible = target;
    if (kind == 0)
      incompatible.ao_channel = true;
    else if (kind == 1)
      incompatible.displacement.channel = true;
    else if (kind == 2)
      configure_material_layering(incompatible, true);
    else
      incompatible.users[0].scattering = true;
    reject([&] {
      reconstruct_material_layer_parameters(incompatible,
                                            portable,
                                            value.working_space,
                                            value.scene_linear_to_xyz,
                                            decode,
                                            processor,
                                            native_gate);
    });
  }
  // Native rejection must precede the decoder, not merely check model names.
  const auto reject_native = [&] {
    const int calls = decoder_calls;
    reject([&] { reconstruct(portable); });
    assert(decoder_calls == calls);
  };
  auto &alpha = *static_cast<bNodeSocketValueFloat *>(
      socket(*surface, "Alpha", true).default_value);
  const float prior_alpha = alpha.value;
  alpha.value = .5f;
  reject_native();
  alpha.value = prior_alpha;
  auto &emission = *static_cast<bNodeSocketValueFloat *>(
      socket(*surface, "Emission Strength", true).default_value);
  const float prior_emission = emission.value;
  emission.value = 1;
  reject_native();
  emission.value = prior_emission;
  auto &sss = *static_cast<bNodeSocketValueFloat *>(
      socket(*surface, "Subsurface Weight", true).default_value);
  const float prior_sss = sss.value;
  sss.value = .5f;
  reject_native();
  sss.value = prior_sss;
  auto &specular = *static_cast<bNodeSocketValueFloat *>(
      socket(*surface, "Specular IOR Level", true).default_value);
  const float prior_specular = specular.value;
  specular.value = 0;
  reject_native();
  specular.value = prior_specular;
  const float prior_radius = radius.value[0];
  radius.value[0] += .125f;
  reject_native();
  radius.value[0] = prior_radius;
  auto *external = bke::node_add_node(nullptr, tree, UString("ShaderNodeValue"));
  auto &link = bke::node_add_link(
      tree, *external, socket(*external, "Value", false), *surface, socket(*surface, "IOR", true));
  reject_native();
  bke::node_remove_link(&tree, link);
  bke::node_remove_node(context.get(), tree, *external, true);
  require_material_parameter_surface(*context, *native_material);
  reject([&] {
    reconstruct_material_layer_parameters(
        target, portable, value.working_space, value.scene_linear_to_xyz, decode, processor, {});
  });
  const auto manifest_path = moved + "/parameters.json";
  QFile manifest(manifest_path);
  assert(manifest.open(QIODevice::ReadOnly));
  const auto manifest_bytes = manifest.readAll();
  manifest.close();
  const auto restore_manifest = [&](const QByteArray &bytes) {
    QFile file(manifest_path);
    assert(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size());
  };
  // Untrusted manifest paths are checked independently of decoder behavior,
  // including Windows drive/UNC forms even when read on Linux.
  for (const QString path : {"../outside.exr",
                             "C:/outside.exr",
                             "C:outside.exr",
                             "//server/share.exr",
                             "\\\\server\\share.exr"})
  {
    auto json = QJsonDocument::fromJson(manifest_bytes).object();
    auto originals = json["originals"].toArray();
    auto original = originals[0].toObject();
    auto files = original["files"].toArray();
    auto file = files[0].toObject();
    file["path"] = path;
    files[0] = file;
    original["files"] = files;
    originals[0] = original;
    json["originals"] = originals;
    restore_manifest(QJsonDocument(json).toJson());
    reject([&] { read_material_layer_parameter_package(moved, cancel); });
  }
  restore_manifest(manifest_bytes);
  const auto payload = portable.originals.front().files.front().descriptor.path;
  QFile file(payload);
  assert(file.open(QIODevice::ReadOnly));
  const auto bytes = file.readAll();
  file.close();
  const auto restore_payload = [&](const QByteArray &data) {
    QFile f(payload);
    assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size());
  };
  restore_payload(bytes.left(bytes.size() - 1));
  reject([&] { read_material_layer_parameter_package(moved, cancel); });
  auto corrupt = bytes;
  corrupt[corrupt.size() / 2] = char(corrupt.at(corrupt.size() / 2) ^ 1);
  restore_payload(corrupt);
  reject([&] { read_material_layer_parameter_package(moved, cancel); });
  assert(QFile::remove(payload));
  reject([&] { read_material_layer_parameter_package(moved, cancel); });
  const auto outside = directory.filePath("outside.exr");
  {
    QFile f(outside);
    assert(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
  }
  assert(QFile::link(outside, payload));
  reject([&] { read_material_layer_parameter_package(moved, cancel); });
  assert(QFile::remove(payload));
  restore_payload(bytes);
  assert(material_layer_parameters_json(read_material_layer_parameter_package(moved, cancel)) ==
             original_metadata &&
         document.to_json() == before);
  // A self-consistent, decodable file with illegal mask data must also fail;
  // checksum validation is not permission to publish arbitrary raw mask pixels.
  assert(QDir().mkdir(originals_path));
  auto bad_mask = portable;
  const auto wanted = bad_mask.masks[2]->asset;
  auto bad_original = std::find_if(
      bad_mask.originals.begin(), bad_mask.originals.end(), [&](const auto &original) {
        return original.asset.toString(QUuid::WithoutBraces).toStdString() == wanted;
      });
  assert(bad_original != bad_mask.originals.end());
  bad_original->files[0] = encode(1001,
                                  "invalid-opacity.1001.exr",
                                  128,
                                  128,
                                  std::vector<FillTexture::Pixel>(128 * 128, {1.25f, 0, 0, 1}));
  bad_mask.masks[2] = decode(*bad_mask.masks[2], *bad_original, true);
  const auto bad_package = directory.filePath("invalid-mask");
  write_material_layer_parameter_package(bad_package, bad_mask, cancel);
  reject([&] { reconstruct(read_material_layer_parameter_package(bad_package, cancel)); });
  return {{"import_catalog", import_catalog_proof},
          {"import_worker_lifecycle", import_job_proof},
          {"document_reconstruction", document_proof},
          {"disabled_document_reconstruction", disabled_document_proof},
          {"missing_target_rejected_before_decode", true},
          {"native_mask_sampling_rejections", 2},
          {"original_hidden_relocated", true},
          {"unique_encoded_payloads", 8},
          {"same_encoding_raw_color_shared", true},
          {"mask_formula_texels", samples},
          {"evaluated_mask_transport_texels", transported},
          {"evaluated_mask_nearest_transport", true},
          {"old_HQ_checker_differs", true},
          {"evaluated_mask_composite_user_texels", composed},
          {"four_inputs_shader_srt_preserved", true},
          {"local_mask_slots_remapped", true},
          {"disabled_sources_retained", true},
          {"working_space_processor_guards", true},
          {"native_surface_rejections_before_decode", 6},
          {"actual_New_ACEScg_canonical_defaults", true},
          {"corruption_cancel_rejections", rejected},
          {"no_live_document_mutation", true}};
}
