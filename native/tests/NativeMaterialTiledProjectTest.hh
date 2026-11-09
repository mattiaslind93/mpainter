// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "NativeMaterialTiledSourceTest.hh"
#include "../host/NativeMaterialApply.hh"
#include "../host/ProjectIO.hh"
#include "../host/FillTextureIO.hh"
#include "../host/ResourceIO.hh"
#include "BKE_idprop.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"
#include "BLO_readfile.hh"
#include "DNA_material_types.h"
#include "DNA_scene_types.h"

namespace bts::native_tiled_project_test {
using native_tiled_source_test::require;
inline QJsonObject portable_metadata(const StudioAsset &asset)
{
  auto result = asset_metadata(asset);
  if (result.contains("texture_files")) {
    auto files = result["texture_files"].toArray();
    for (int i = 0; i < files.size(); ++i) {
      auto file = files[i].toObject();
      file.remove("path");
      files[i] = file;
    }
    result["texture_files"] = files;
  }
  return result;
}
inline const blender::IDProperty *property(const blender::IDProperty *root,
                                           const std::string &key,
                                           int type)
{
  const auto *result = root ? blender::IDP_GetPropertyFromGroup(root, key) : nullptr;
  require(result && result->type == type,
          "Native TILED cold project property missing or wrong type");
  return result;
}
}  // namespace bts::native_tiled_project_test
namespace bts {
inline QJsonObject verify_native_material_tiled_project()
{
  using namespace native_tiled_project_test;
  require(QCoreApplication::instance() &&
              QThread::currentThread() == QCoreApplication::instance()->thread(),
          "Native TILED project fixture requires initialized CPU owner");
  require(!blender::GPU_context_active_get(),
          "Native TILED project fixture entered with GPU context");
  const auto compilations = blender::GPU_pass_global_compilation_count();
  QTemporaryDir directory;
  require(directory.isValid(), "Cannot create native TILED project fixture directory");
  std::atomic_bool cancel = false;
  const bool had_cache = qEnvironmentVariableIsSet("BTS_RESOURCE_CACHE");
  const auto previous_cache = qgetenv("BTS_RESOURCE_CACHE");
  const auto restore_cache = qScopeGuard([&] {
    if (had_cache)
      qputenv("BTS_RESOURCE_CACHE", previous_cache);
    else
      qunsetenv("BTS_RESOURCE_CACHE");
  });
  const auto writer_cache = directory.filePath("writer-cache"),
             cold_cache = directory.filePath("cold-cache");
  require(QDir().mkpath(writer_cache) && qputenv("BTS_RESOURCE_CACHE", writer_cache.toUtf8()),
          "Cannot isolate native project writer cache");
  const auto proof = native_tiled_source_test::work([&] {
    using namespace blender;
    using namespace native_tiled_source_test;
    write(directory.path());
    const auto project_path = directory.filePath("native-tiled-cold.blend");
    QJsonObject expected_document, expected_controls, expected_description,
        expected_native_metadata, expected_carrier_metadata;
    QJsonArray expected_catalog, expected_tiles;
    QString native_hash, carrier_hash, image_name;
    std::string source_hash;
    QUuid native_id, carrier_id, set_id, layer_id;
    std::string material_name;
    std::weak_ptr<NativeMaterialGraph> retired_graph;
    std::weak_ptr<const NativeMaterialImageTileSet> retired_set;
    std::vector<std::weak_ptr<NativeMaterialImageResource>> retired_pixels;
    bool writer_Main_destroyed = false;
    {
      auto source = graph(directory.path(), "Non-Color", cancel);
      prepare_native_material_images(*source, cancel);
      auto decoded = inventory(*source);
      expected_controls = state(*source, false);
      expected_description = native_material_description_json(source->description);
      retired_graph = source;
      retired_set = decoded;
      for (const auto &[tile, owner] : decoded->tiles())
        retired_pixels.push_back(owner);
      const auto archive = archive_native_material(
          *source, directory.filePath("native-source.blend"), cancel, true);
      require(archive.textures.size() == 1 && version(archive.bytes) == 3,
              "Cold project source did not use native v3 and one aggregate");
      auto asset = native_material_asset(
          "Native TILED cold project", archive.bytes, archive.description);
      asset.scope = AssetScope::Project;
      for (const auto &channel : source->channels)
        if (channel.tree)
          asset.search_terms.append("native-channel:" + QString::number(channel.channel));
      auto carrier = archive.textures.front();
      carrier.scope = AssetScope::Project;
      native_id = asset.id;
      carrier_id = carrier.id;
      native_hash = asset.hash();
      source_hash = native_material_source_hash(asset.bytes);
      carrier_hash = carrier.hash();
      const auto refs = native_material_bundle_image_manifest(asset.bytes);
      require(refs.size() == 1 && !native_material_bundle_provenance(asset.bytes),
              "Original v3 source invented variant lineage");
      image_name = refs.front().image;
      expected_tiles = texture_sources_json(refs.front().tiles, false);
      tiled_reference(refs.front(), carrier);
      expected_native_metadata = portable_metadata(asset);
      expected_carrier_metadata = portable_metadata(carrier);
      expected_catalog = {asset_metadata(asset), asset_metadata(carrier)};
      std::unique_ptr<Main, decltype(&BKE_main_free)> writer(BKE_main_new(), BKE_main_free);
      auto *scene = BKE_scene_add(writer.get(), "Native TILED cold project");
      auto *material = BKE_material_add(writer.get(), "Native TILED target");
      material_name = material->id.name + 2;
      SceneSnapshot snapshot;
      MaterialSummary summary;
      summary.session_id = material->id.session_uid;
      summary.name = material_name;
      summary.uniform_editable = true;
      snapshot.materials = {summary};
      ImportSummary imported;
      imported.materials = {{material->id.session_uid, material_name}};
      NewProjectOptions options;
      options.uv_workflow = UvWorkflow::PreserveTiles;
      TextureDocument document(options, imported, snapshot);
      auto &set = document.set(material->id.session_uid);
      set.tiles = {numbers.begin(), numbers.end()};
      set.texture_resolution = 128;
      set.texture_height = 256;
      set.size_linked = false;
      set.tile_resolutions[1002] = 256;
      set_id = set.id;
      layer_id = apply_native_material_asset(set, asset);
      auto &layer = TextureDocument::layer(set, layer_id);
      layer.fill.rotation = .375;
      layer.fill.offset = {.125, -.25};
      require(layer.fill.native_material &&
                  layer.fill.material_resource ==
                      native_id.toString(QUuid::WithoutBraces).toStdString() &&
                  !layer.channels[3],
              "Real native Fill application lost UUID or hidden Normal contract");
      expected_document = document.to_json();
      ProjectResourceImages packed;
      pack_project_assets(*writer, *scene, {asset, carrier}, &packed);
      require(packed.by_key.size() == 5,
              "Project pack did not retain native blob plus four aggregate members");
      write_project(*writer, *scene, document, project_path.toStdString());
      require(QFileInfo(project_path).size() > 0 && state(*source, false) == expected_controls,
              "Project writer failed or mutated native source controls");
      writer.reset();
      writer_Main_destroyed = true;
    }
    require(writer_Main_destroyed && retired_graph.expired() && retired_set.expired() &&
                retired_pixels.size() == 4 &&
                std::all_of(retired_pixels.begin(),
                            retired_pixels.end(),
                            [](const auto &owner) { return owner.expired(); }),
            "Cold project retained a writer graph, set or decoded pixel owner");
    QStringList hidden;
    const auto restore = qScopeGuard([&] {
      for (const auto &name : hidden)
        QFile::rename(name + ".hidden", name);
    });
    for (int tile : numbers) {
      const auto name = path(directory.path(), tile);
      require(QFile::rename(name, name + ".hidden"),
              "Cannot hide original native TILED project source");
      hidden.append(name);
    }
    for (const auto &name : hidden)
      require(!QFileInfo::exists(name),
              "Original tile remained available before cold project read");
    require(!GPU_context_active_get(), "Project writer acquired GPU context");
    require(!QFileInfo::exists(cold_cache) && QDir().mkpath(cold_cache) &&
                qputenv("BTS_RESOURCE_CACHE", cold_cache.toUtf8()),
            "Cannot select a fresh empty owned cold resource cache");
    require(QDir(cold_cache).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
            "Cold resource cache was not empty before reader");
    ReportList reports{};
    BKE_reports_init(&reports, RPT_STORE);
    const auto release_reports = qScopeGuard([&] { BKE_reports_free(&reports); });
    BlendFileReadReport report{};
    report.reports = &reports;
    std::unique_ptr<BlendFileData, decltype(&BLO_blendfiledata_free)> cold(
        BLO_read_from_file(project_path.toUtf8().constData(), BLO_READ_SKIP_USERDEF, &report),
        BLO_blendfiledata_free);
    require(cold && cold->main && !cold->main->scenes.is_empty(),
            "Actual native TILED project cold .blend read failed");
    auto *scene = static_cast<Scene *>(cold->main->scenes.first);
    const auto *root = IDP_GetProperties(&scene->id);
    const auto *saved = property(root, "bts_document", IDP_STRING);
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(QByteArray(IDP_string_get(saved)), &error);
    require(error.error == QJsonParseError::NoError && json.object() == expected_document,
            "Saved native Fill document metadata changed");
    const auto catalog = QJsonDocument::fromJson(
        QByteArray(IDP_string_get(property(root, "bts_resources", IDP_STRING))));
    require(catalog.object()["resources"].toArray() == expected_catalog,
            "Actual saved native catalog metadata changed");
    const auto *sets = property(root, "bts_sets", IDP_GROUP);
    auto *material = IDP_ID_get(
        property(sets, set_id.toString(QUuid::WithoutBraces).toStdString(), IDP_ID));
    require(material && GS(material->name) == ID_MA && material->session_uid &&
                std::string(material->name + 2) == material_name,
            "Cold project lost actual native target material reference");
    bool owned = false;
    for (const auto &entry : cold->main->materials)
      owned |= &entry.id == material;
    require(owned, "Cold target material reference is outside cold Main");
    ProjectResourceImages resource_images;
    const auto assets = read_project_assets(*scene, &resource_images);
    require(assets.size() == 2 && resource_images.by_key.size() == 5,
            "Cold actual resource reader lost native/aggregate catalog entries");
    const auto native = std::find_if(assets.begin(),
                                     assets.end(),
                                     [&](const auto &asset) { return asset.id == native_id; }),
               carrier = std::find_if(assets.begin(), assets.end(), [&](const auto &asset) {
                 return asset.id == carrier_id;
               });
    require(native != assets.end() && carrier != assets.end() && native->hash() == native_hash &&
                carrier->hash() == carrier_hash &&
                portable_metadata(*native) == expected_native_metadata &&
                portable_metadata(*carrier) == expected_carrier_metadata,
            "Cold native/aggregate UUID, metadata or encoded hash changed");
    require(
        resource_images.by_key.contains(native_id.toString(QUuid::WithoutBraces).toStdString()),
        "Cold native resource lacks actual packed Image reference");
    for (int tile : numbers)
      require(
          resource_images.by_key.contains(carrier_id.toString(QUuid::WithoutBraces).toStdString() +
                                          "_" + std::to_string(tile)),
          "Cold aggregate member lacks packed Image reference");
    for (const auto &file : carrier->texture_files)
      require(
          file.path.startsWith(cold_cache + "/") && QFileInfo(file.path).size() == file.size &&
              hash_texture_file(file.path) == file.sha256,
          "Actual cold resource reader did not reconstruct encoded tile into empty owned cache");
    const auto refs = native_material_bundle_image_manifest(native->bytes);
    require(version(native->bytes) == 3 &&
                native_material_source_hash(native->bytes) == source_hash &&
                !native_material_bundle_provenance(native->bytes) && refs.size() == 1 &&
                refs.front().image == image_name && refs.front().sha256 == carrier_hash &&
                texture_sources_json(refs.front().tiles, false) == expected_tiles,
            "Cold v3 source manifest/hash/original lineage changed");
    tiled_reference(refs.front(), *carrier);
    const std::array<TextureSetBinding, 1> bindings{
        {{set_id, material->session_uid, material->name + 2}}};
    auto document = TextureDocument::from_json(json.object(), bindings);
    resolve_fill_textures(document, assets);
    require(document.to_json() == expected_document,
            "Cold native Fill document roundtrip changed metadata");
    auto &set = document.set(material->session_uid);
    auto &layer = TextureDocument::layer(set, layer_id);
    require(layer.fill.material_resource ==
                    native_id.toString(QUuid::WithoutBraces).toStdString() &&
                layer.fill.native_material &&
                native_material_description_json(*layer.fill.native_material) ==
                    expected_description &&
                !layer.channels[3],
            "Cold native Fill instance UUID/description or hidden Normal changed");
    auto hydrated = hydrate_native_material_resource(*native, cancel, assets);
    require(state(*hydrated, false) == expected_controls &&
                native_material_description_json(hydrated->description) == expected_description &&
                image(*hydrated).source == IMA_SRC_TILED,
            "Cold project worker changed native source kind/controls/description");
    const auto pixels = inventory(*hydrated);
    std::size_t components = 0;
    for (std::size_t i = 0; i < numbers.size(); ++i) {
      const auto &member = pixels->tiles().at(numbers[i]);
      const auto *buffer = member->buffer();
      require(buffer && buffer->float_data() && buffer->x == dimensions[i][0] &&
                  buffer->y == dimensions[i][1] &&
                  member->encoded_sha256() == refs.front().tiles[i].sha256,
              "Cold project worker lost tile dimensions or encoded identity");
      for (int y = 0; y < buffer->y; ++y)
        for (int x = 0; x < buffer->x; ++x) {
          const auto expected = pixel(int(i), x, y);
          for (int c = 0; c < 4; ++c) {
            require(buffer->float_data()[4 * (y * buffer->x + x) + c] == expected[c],
                    "Cold project native signed/HDR/zero-alpha pixels changed");
            ++components;
          }
        }
    }
    require(!BKE_image_has_loaded_ibuf(&image(*hydrated)) && !GPU_context_active_get(),
            "Cold worker populated original BKE cache or GPU context");
    return QJsonObject{{"actual_project_write", true},
                       {"actual_cold_BLO_read", true},
                       {"writer_Main_and_source_graph_destroyed", true},
                       {"decoded_set_expired_before_read", true},
                       {"decoded_pixel_owners_expired_before_read", 4},
                       {"original_files_hidden", 4},
                       {"cold_encoded_cache_initially_empty", true},
                       {"packed_members_reconstructed_in_fresh_cache", 4},
                       {"catalog_assets", 2},
                       {"native_v3_assets", 1},
                       {"aggregate_carriers", 1},
                       {"packed_resource_ID_references", 5},
                       {"sparse_tiles", QJsonArray{1001, 1002, 1011, 2000}},
                       {"mixed_source_rectangles_preserved", true},
                       {"all_pixel_components_exact", qint64(components)},
                       {"signed_HDR_zero_alpha_preserved", true},
                       {"saved_document_and_catalog_IDProperties_exact", true},
                       {"actual_saved_material_ID_owned_by_cold_Main", true},
                       {"native_Fill_UUID_description_mapping_exact", true},
                       {"native_and_carrier_hash_manifest_exact", true},
                       {"original_variant_lineage_absence_preserved", true},
                       {"source_controls_kind_tile_labels_exact", true},
                       {"worker_hydrate_from_actual_cold_catalog", true},
                       {"original_BKE_image_cache_unpopulated", true},
                       {"PendingProject_WM_Open_tested", false},
                       {"render_tested", false}};
  });
  require(!blender::GPU_context_active_get() &&
              blender::GPU_pass_global_compilation_count() == compilations,
          "Native TILED project CPU fixture created GPU context/program");
  return {{"complete", true},
          {"cold_project", proof},
          {"GPU_context_created", false},
          {"GPU_compilations", 0}};
}
}  // namespace bts
