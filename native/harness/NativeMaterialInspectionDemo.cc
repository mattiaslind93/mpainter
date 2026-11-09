// SPDX-License-Identifier: GPL-3.0-or-later
#include "NativeMaterialInspectionDemo.hh"
#include "NativeMaterialPanelTest.hh"
#include "BlenderHost.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialGpuLibrary.hh"
#include "NativeMaterialFragmentImages.hh"
#include "BuiltinStandardMaterials.hh"
#include "NativeMaterialApply.hh"
#include "MaterialLayeringSourcesTest.hh"
#include "SourceCpuRegression.hh"
#include "../tests/NativeMaterialMappingGraphTest.hh"
#include "../tests/NativeHeightPolicyGraphTest.hh"
#include "../tests/NativeMaterialFragmentNormalGraphTest.hh"
#include "../tests/NativeMaterialSourceContextGraphTest.hh"
#include "../tests/NativeFragmentGeometryTest.hh"
#include "../tests/NativeFragmentViewContextTest.hh"
#include "../tests/NativeFragmentBakedAoContextTest.hh"
#include "../tests/NativeWorldNormalSurfaceContractTest.hh"
#include "../tests/NativeFragmentRetainedImageContextTest.hh"
#include "../tests/NativeFragmentImageProjectionTest.hh"
#include "../tests/NativeNormalImageAvailabilityTest.hh"
#include "../tests/NativeFragmentCoordinateNormalTest.hh"
#include "../tests/NativeMaterialTiledSourceTest.hh"
#include "../tests/NativeMaterialTiledStorageTest.hh"
#include "../tests/NativeMaterialTiledProjectTest.hh"
#include "../tests/NativeWorldNormalPlanTest.hh"
#include "../tests/NativeWorldNormalShaderTest.hh"
#include "../tests/NativeWorldNormalUvTest.hh"
#include "../tests/NativeEmptyWorkspaceColorTest.hh"
#include "../tests/MaterialLayeringTopologyTest.hh"
#include "../tests/MaterialLayeringPanelTest.hh"
#include "../tests/ShaderControllerTargetTest.hh"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <set>
#include "BKE_idprop.hh"
#include "ResourceIO.hh"
#include "../tests/NativeMaterialFragmentNormalMapFixture.hh"

namespace bts {
namespace {
QJsonObject world_normal_context_metadata(const std::shared_ptr<NativeMaterialGraph> &graph)
{
  if (!graph)
    throw std::runtime_error("Native world context metadata requires the detached source graph");
  const auto before = native_material_description_json(graph->description);
  const bool preserved = graph->original_inputs_preserved;
  NativeMaterialGpuLibrary library;
  library.install("cpu-world-context", "cpu-world-context", graph);
  QString state;
  switch (library.world_normal_context_state("cpu-world-context")) {
    case NativeWorldNormalContextState::Unsupported:
      state = "unsupported";
      break;
    case NativeWorldNormalContextState::PendingGenerated:
      state = "pending_generated";
      break;
    case NativeWorldNormalContextState::PendingView:
      state = "pending_view";
      break;
    case NativeWorldNormalContextState::PendingBakedAo:
      state = "pending_baked_ao";
      break;
    case NativeWorldNormalContextState::PendingImages:
      state = "pending_images";
      break;
    case NativeWorldNormalContextState::Ready:
      state = "ready";
      break;
  }
  const bool image_object_matrices = library.world_normal_requires_images("cpu-world-context") ?
                                         NativeFragmentImageContext::capture(
                                             {graph, graph->world_normal_tree})
                                             .requires_object_matrices() :
                                         false;
  const auto statistics = library.statistics();
  if (statistics.programs || statistics.world_programs ||
      before != native_material_description_json(graph->description) ||
      preserved != graph->original_inputs_preserved)
    throw std::runtime_error("CPU world context metadata changed source or created a program");
  return {{"surface_contract",
           QString::fromUtf8(
               native_world_normal_surface_name(graph->world_normal_surface_contract).data())},
          {"source_displacement_linked", graph->world_normal_source_displacement},
          {"surface_contract_warning",
           graph->description.warnings.contains(
               QString::fromUtf8(native_world_normal_surface_warning().data()))},
          {"perturbed", library.world_normal_perturbed("cpu-world-context")},
          {"context_state", state},
          {"error", library.world_normal_error("cpu-world-context")},
          {"requires_generated", library.world_normal_requires_generated("cpu-world-context")},
          {"requires_view", library.world_normal_requires_view("cpu-world-context")},
          {"requires_baked_ao", library.world_normal_requires_baked_ao("cpu-world-context")},
          {"requires_images", library.world_normal_requires_images("cpu-world-context")},
          {"requires_image_object_matrices", image_object_matrices},
          {"source_original_preserved", graph->original_inputs_preserved},
          {"programs", qint64(statistics.programs)},
          {"world_programs", qint64(statistics.world_programs)}};
}
QJsonObject verify_recovery_normal_geometry(BlenderHost &host, const QString &path)
{
  using namespace blender;
  const QFileInfo before(path);
  ReportList reports{};
  BKE_reports_init(&reports, RPT_STORE);
  const auto release_reports = qScopeGuard([&] { BKE_reports_free(&reports); });
  BlendFileReadReport report{};
  report.reports = &reports;
  std::unique_ptr<BlendFileData, decltype(&BLO_blendfiledata_free)> file(
      BLO_read_from_file(path.toUtf8().constData(), BLO_READ_SKIP_USERDEF, &report),
      BLO_blendfiledata_free);
  if (!file || !file->main || file->main->scenes.is_empty() || file->main->materials.is_empty())
    throw std::runtime_error("Recovery geometry fixture could not be loaded");
  auto *scene = static_cast<Scene *>(file->main->scenes.first);
  auto *material = static_cast<Material *>(file->main->materials.first);
  auto *context = CTX_create();
  const auto release_context = qScopeGuard([&] {
    BKE_scene_free_depsgraph_hash(scene);
    CTX_free(context);
  });
  BKE_scene_set_background(file->main, scene);
  DEG_relations_tag_update(file->main);
  CTX_data_main_set(context, file->main);
  CTX_data_scene_set(context, scene);
  CTX_data_ensure_evaluated_depsgraph(context);
  NativeFragmentGeometryRequest request;
  request.material_uid = material->id.session_uid;
  request.tiles = {1001};
  request.target_uv = {NativeFragmentUvKind::DefaultRender, {}};
  request.uv_requests = {request.target_uv};
  const std::atomic_bool cancel = false;
  const auto source = capture_native_fragment_geometry(context, nullptr, {}, request, cancel);
  const auto job = NativeWorldNormalGeometryJob::start(source);
  NativeWorldNormalGeometryJob::pool().waitForDone();
  if (!job->ready.load(std::memory_order_acquire) || !job->error.empty() || !job->geometry)
    throw std::runtime_error("Recovery tangent geometry rejected: " + job->error);
  std::size_t triangles = 0, samples = 0;
  for (std::size_t stream = 0; stream < job->geometry->streams().size(); ++stream) {
    const auto vertices = job->geometry->vertices(stream);
    triangles += vertices.size() / 3;
    const auto &owner = job->geometry->objects()[job->geometry->streams()[stream].object];
    const float4x4 inverse(owner.world_to_object.data());
    const float3x3 matrix(inverse);
    std::array<float, 9> transform;
    std::copy_n(&matrix[0][0], 9, transform.begin());
    for (std::size_t i = 0; i < vertices.size(); i += 3) {
      const std::array corners{vertices[i], vertices[i + 1], vertices[i + 2]};
      const auto [t, n] = native_fragment_normal_map_test::interpolate(
          corners, {.2, .3, .5}, transform);
      for (bool front : {false, true}) {
        const auto normal = native_fragment_normal_map_test::expected(
            t,
            n,
            {.2, .7, 1},
            .65,
            front,
            bool(job->geometry->object_infos(job->geometry->streams()[stream].object).flag &
                 OBJECT_NEGATIVE_SCALE),
            false);
        double length = 0;
        for (double x : normal) {
          if (!std::isfinite(x))
            throw std::runtime_error("Recovery CPU Normal evaluation is nonfinite");
          length += x * x;
        }
        if (std::abs(length - 1) > 1e-6)
          throw std::runtime_error("Recovery CPU Normal evaluation is not unit length");
        ++samples;
      }
    }
  }
  // Restore the actual saved layer metadata and prepare only resources used by
  // those layers. The 81-entry Shelf is checked as metadata, without hydration.
  auto *properties = IDP_GetProperties(&scene->id);
  auto *json = properties ? IDP_GetPropertyFromGroup(properties, "bts_document") : nullptr;
  if (!json || json->type != IDP_STRING)
    throw std::runtime_error("Recovery document metadata missing");
  const auto metadata = QJsonDocument::fromJson(QByteArray(IDP_string_get(json))).object();
  auto *sets = IDP_GetPropertyFromGroup(properties, "bts_sets");
  std::vector<TextureSetBinding> bindings;
  for (const auto &entry : metadata["texture_sets"].toArray()) {
    const QUuid id(entry.toObject()["id"].toString());
    auto *ref = sets ? IDP_GetPropertyFromGroup(
                           sets, id.toString(QUuid::WithoutBraces).toUtf8().constData()) :
                       nullptr;
    if (!ref || ref->type != IDP_ID)
      throw std::runtime_error("Recovery set reference missing");
    auto *mat = IDP_ID_get(ref);
    bindings.push_back({id, mat->session_uid, mat->name + 2});
  }
  auto document = TextureDocument::from_json(metadata, bindings);
  const auto resources = read_project_assets(*scene);
  QJsonArray materials;
  for (const auto &set : document.texture_sets())
    for (const auto &layer : set.layers)
      if (layer.fill.native_material) {
        const auto resource = std::find_if(
            resources.begin(), resources.end(), [&](const auto &asset) {
              return asset.id.toString(QUuid::WithoutBraces).toStdString() ==
                     layer.fill.material_resource;
            });
        if (resource == resources.end())
          throw std::runtime_error("Recovery native resource missing");
        auto graph = host.prepare_material_resource(
                             *resource, std::make_shared<std::atomic_bool>(false), resources)
                         .get();
        const auto state = world_normal_context_metadata(graph);
        if (!graph->world_normal_tree || !graph->world_normal_error.isEmpty())
          throw std::runtime_error("Recovery native Normal graph unavailable: " +
                                   graph->world_normal_error.toStdString());
        const std::array ids{layer.id};
        const auto plan = native_world_normal_plan(set, ids);
        if (layer.visible && !plan.eligible)
          throw std::runtime_error(
              "Recovery NativeWorldNormalPlan rejected active native material");
        materials.append(QJsonObject{{"material", layer.fill.native_material->material},
                                     {"plan_eligible", plan.eligible},
                                     {"context", state},
                                     {"graph_error", graph->world_normal_error}});
      }
  int shelf_plans = 0;

  const QFileInfo after(path);
  if (before.size() != after.size() || before.lastModified() != after.lastModified())
    throw std::runtime_error("Recovery inspection modified its source");
  std::printf(
      "BTS_RECOVERY_NORMAL_GEOMETRY tangent_accepted=1 triangles=%zu cpu_normal_samples=%zu shelf_plans=%d\n",
      triangles,
      samples,
      shelf_plans);
  return {{"path", path},
          {"material", QString::fromUtf8(material->id.name + 2)},
          {"triangles", qint64(triangles)},
          {"cpu_normal_samples", qint64(samples)},
          {"recovery_materials", materials},
          {"shelf_metadata_plans", shelf_plans},
          {"tangent_geometry_accepted", true},
          {"source_unchanged", true},
          {"gpu_required", false}};
}
}  // namespace
int run_native_material_inspection_demo(BlenderHost &host, const QString &directory)
{
  if (qEnvironmentVariableIsSet("BTS_NATIVE_DEGENERATE_TEST")) {
    QDir().mkpath(directory);
    QJsonObject proof{
        {"geometry", verify_native_fragment_geometry()},
        {"normal_plan", verify_native_world_normal_plan()},
        {"normal_map_cpu", native_fragment_normal_map_test::verify_metadata()},
        {"recovery",
         verify_recovery_normal_geometry(host, qEnvironmentVariable("BTS_NATIVE_RECOVERY_TEST"))}};
    QFile output(directory + "/degenerate-fix.json");
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot save degenerate geometry proof");
    output.write(QJsonDocument(proof).toJson());
    return 0;
  }
  run_ordinary_color_source_cpu_regression(directory + "/ordinary-color-source");
  run_spsm_color_lowering_cpu_regression();
  const auto empty_workspace_color = verify_empty_workspace_color_cpu();
  auto panel = verify_native_material_panel(directory);
  panel["empty_workspace_color"] = empty_workspace_color;
  panel["upstream_mapping_graph"] = verify_native_upstream_mapping_graph();
  panel["height_policy_graph"] = verify_native_height_policy_graph();
  panel["fragment_normal_graph"] = verify_native_fragment_normal_graph();
  panel["native_source_context_graph"] = verify_native_material_source_context_graph();
  panel["fragment_geometry"] = verify_native_fragment_geometry();
  panel["fragment_view_context"] = verify_native_fragment_view_context();
  panel["fragment_baked_ao_context"] = verify_native_fragment_baked_ao_context();
  panel["world_normal_surface_contract"] = verify_native_world_normal_surface_contract();
  panel["fragment_retained_image_context"] = verify_native_fragment_retained_image_context();
  panel["fragment_image_projection"] = verify_native_fragment_image_projection();
  panel["normal_image_availability"] = verify_native_normal_image_availability();
  panel["fragment_coordinate_normal"] = verify_native_fragment_coordinate_normal();
  panel["native_world_normal_plan"] = verify_native_world_normal_plan();
  panel["native_world_normal_shader"] = verify_native_world_normal_shader();
  panel["native_world_normal_uv"] = verify_native_world_normal_uv();
  panel["material_layering_topology"] = verify_material_layering_topology_cpu();
  panel["material_layering_panel"] = test_material_layering_panel();
  panel["shader_controller_layering_targets"] = test_shader_controller_layering_targets();
  panel["material_layering_source_pixels"] = test_material_layering_source_pixels();
  panel["tiled_image_sources"] = verify_native_material_tiled_sources();
  panel["tiled_image_storage"] = verify_native_material_tiled_storage();
  panel["tiled_image_project"] = verify_native_material_tiled_project();
  if (!qEnvironmentVariableIsEmpty("BTS_NATIVE_RECOVERY_TEST"))
    panel["recovery_normal_geometry"] = verify_recovery_normal_geometry(
        host, qEnvironmentVariable("BTS_NATIVE_RECOVERY_TEST"));
  if (qEnvironmentVariableIsEmpty("BTS_NATIVE_MATERIAL_CASES")) {
    QFile output(directory + "/native-material-panel.json");
    if (!output.open(QIODevice::WriteOnly))
      throw std::runtime_error("Cannot save native parameter UI proof");
    output.write(QJsonDocument(panel).toJson());
    return 0;
  }
  QFile input(qEnvironmentVariable("BTS_NATIVE_MATERIAL_CASES"));
  if (!input.open(QIODevice::ReadOnly))
    throw std::runtime_error(
        "Set BTS_NATIVE_MATERIAL_CASES to the explicit test-library manifest");
  const auto cases = QJsonDocument::fromJson(input.readAll()).array();
  if (cases.isEmpty())
    throw std::runtime_error("No material inspection cases");
  QJsonArray results;
  std::set<QString> scanned;
  QJsonArray indexed_files;
  for (const auto &entry : cases) {
    const auto test = entry.toObject();
    const QString path = test["file"].toString(), name = test["material"].toString();
    const QFileInfo before(path);
    QElapsedTimer timer;
    timer.start();
    if (scanned.insert(path).second) {
      auto library =
          host.inspect_asset_library(path, std::make_shared<std::stop_source>(), {}).get();
      bool found = false;
      for (const auto &entry : library.materials) {
        if (entry.file != QFileInfo(path).canonicalFilePath())
          throw std::runtime_error("Single-file library scan included another source");
        found |= entry.material.name == name;
      }
      if (library.files != 1 || !found)
        throw std::runtime_error("Single .blend library scan omitted selected material");
      indexed_files.append(QJsonObject{{"file", path},
                                       {"source_files", library.files},
                                       {"materials", qint64(library.materials.size())}});
    }
    auto result =
        host.inspect_material_graph(path, name, std::make_shared<std::atomic_bool>(false)).get();
    const double ms = timer.nsecsElapsed() / 1e6;
    auto json = native_material_description_json(result);
    if (native_material_description_json(read_native_material_description(json)) != json)
      throw std::runtime_error("Native parameter JSON roundtrip changed data");
    const QFileInfo after(path);
    if (before.size() != after.size() || before.lastModified() != after.lastModified())
      throw std::runtime_error("Read-only source changed during material inspection");
    timer.restart();
    const auto graph =
        host.prepare_material_graph(path, name, std::make_shared<std::atomic_bool>(false)).get();
    const double preparation_ms = timer.nsecsElapsed() / 1e6;
    QJsonArray channels;
    for (const auto &channel : graph->channels)
      channels.append(QJsonObject{{"channel", channel.channel},
                                  {"prepared", channel.tree != nullptr},
                                  {"error", channel.error}});
    json["channels"] = channels;
    json["original_inputs_preserved"] = graph->original_inputs_preserved;
    json["preparation_ms"] = preparation_ms;
    json["parameter_buffer_entries"] = qint64(graph->parameters.size());
    timer.restart();
    auto prepared =
        host.import_native_material(path, name, std::make_shared<std::atomic_bool>(false)).get();
    if (!prepared.native_graph || prepared.material.bytes.isEmpty())
      throw std::runtime_error("Native import did not return its detached source resource");
    auto restored_asset = asset_metadata_read(asset_metadata(prepared.material));
    restored_asset.bytes = prepared.material.bytes;
    restored_asset.name = "Renamed Shelf material";
    auto hydrated = host.prepare_material_resource(restored_asset,
                                                   std::make_shared<std::atomic_bool>(false),
                                                   prepared.textures)
                        .get();
    auto source = native_material_description_json(prepared.native_graph->description),
         restored = native_material_description_json(hydrated->description);
    source.remove("warnings");
    restored.remove("warnings");
    if (source != restored)
      throw std::runtime_error("Host native resource hydration changed source parameters");
    json["native_resource_pipeline"] = QJsonObject{
        {"supported_channels", QJsonArray::fromStringList(prepared.material.search_terms)},
        {"warnings",
         QJsonArray::fromStringList(prepared.material.fill_preset->native_material->warnings)},
        {"external_image_resources", qint64(prepared.textures.size())},
        {"bytes", prepared.material.bytes.size()},
        {"worker_import_and_hydrate_ms", timer.nsecsElapsed() / 1e6},
        {"renamed_source_preserved", true},
        {"shared_encoded_bytes",
         restored_asset.bytes.constData() == prepared.material.bytes.constData()}};
    json["world_normal_context"] = QJsonObject{
        {"imported", world_normal_context_metadata(prepared.native_graph)},
        {"hydrated", world_normal_context_metadata(hydrated)}};
    json["source"] = path;
    json["milliseconds"] = ms;
    json["source_unchanged"] = true;
    results.append(json);
    std::printf("BTS_NATIVE_MATERIAL_INSPECT material=%s parameters=%zu nodes=%lld ms=%.3f\n",
                qPrintable(name),
                result.parameters.size(),
                static_cast<long long>(result.node_types.size()),
                ms);
    std::fflush(stdout);
  }
  bool cancelled = false;
  try {
    const auto first = cases.first().toObject();
    host.inspect_material_graph(first["file"].toString(),
                                first["material"].toString(),
                                std::make_shared<std::atomic_bool>(true))
        .get();
  }
  catch (const std::exception &) {
    cancelled = true;
  }
  if (!cancelled)
    throw std::runtime_error("Cancelled inspection was published");
  const auto first = cases.first().toObject();
  bool native_cancel = false;
  try {
    host.import_native_material(first["file"].toString(),
                                first["material"].toString(),
                                std::make_shared<std::atomic_bool>(true))
        .get();
  }
  catch (const std::exception &) {
    native_cancel = true;
  }
  if (!native_cancel)
    throw std::runtime_error("Cancelled native import was published");
  QDir().mkpath(directory);
  QFile output(directory + "/native-material-inspection.json");
  if (!output.open(QIODevice::WriteOnly))
    throw std::runtime_error("Cannot write material inspection proof");
  output.write(QJsonDocument(QJsonObject{{"panel", panel},
                                         {"cases", results},
                                         {"cancelled_request_rejected", cancelled},
                                         {"gpu_required", false},
                                         {"single_file_libraries", indexed_files}})
                   .toJson());
  return 0;
}
}  // namespace bts
