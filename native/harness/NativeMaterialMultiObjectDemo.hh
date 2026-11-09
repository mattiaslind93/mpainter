#include <QCoreApplication>
#pragma once
#include <QtCore>
#include <QScopeGuard>
#include "BlenderHost.hh"
#include "NativeMaterialSourceLoader.hh"
#include "NativeMaterialApply.hh"
#include "NativeMaterialGraph.hh"
#include "NativeMaterialFill.hh"
#include "BKE_lib_id.hh"
#include "BKE_attribute.hh"
#include "BKE_main.hh"
#include "BKE_blendfile.hh"
#include "BKE_material.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh.h"
#include "BKE_object.hh"
#include "BKE_node.hh"
#include "BKE_node_tree_update.hh"
#include "BKE_report.hh"
#include "BKE_global.hh"
#include "BLI_listbase.h"
#include "BLI_math_matrix.h"
#include "BLI_ustring.hh"
#include "BLO_writefile.hh"
#include "DNA_material_types.h"
#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_node_types.h"
#include "GPU_pass.hh"
#include "IMB_colormanagement.hh"
#include <future>
#include <map>
namespace bts {
namespace native_object_demo {
inline void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}
inline blender::bNodeSocket &socket(blender::bNode &node, const char *name, bool input)
{
  for (auto &s : input ? node.inputs : node.outputs)
    if (std::string_view(s.identifier) == name)
      return s;
  throw std::runtime_error("Native context fixture socket missing");
}
inline void fixture(const QString &path, bool capture_bump = false)
{
  using namespace blender;
  using bke::blendfile::PartialWriteContext;
  std::unique_ptr<Main, decltype(&BKE_main_free)> main(BKE_main_new(), BKE_main_free);
  IMB_colormanagement_working_space_init_default(main.get());
  std::vector<ID *> ids;
  for (const auto *coordinate :
       {"Generated", "Object", "UV", "Reference", "Instancer", "ReferenceUV", "UnusedReference"})
  {
    const auto name = std::string("AW_") + coordinate;
    auto *material = BKE_material_add(main.get(), name.c_str());
    auto &tree = *material->nodetree;
    auto *coords = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexCoord"));
    const bool unused = std::string_view(coordinate) == "UnusedReference";
    auto *shader = bke::node_add_node(
        nullptr, tree, UString(unused ? "ShaderNodeBsdfPrincipled" : "ShaderNodeEmission"));
    auto *output = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputMaterial"));
    const bool reference = unused || std::string_view(coordinate).starts_with("Reference");
    if (reference) {
      auto *object = BKE_object_add_only_object(
          main.get(), OB_EMPTY, "Explicit coordinate reference");
      coords->id = &object->id;
      id_us_plus(coords->id);
    }
    if (std::string_view(coordinate) == "Instancer")
      coords->custom1 |= 1;
    const char *source = (unused || std::string_view(coordinate) == "Reference") ? "Object" :
                         std::string_view(coordinate) == "Instancer"             ? "Generated" :
                         std::string_view(coordinate) == "ReferenceUV"           ? "UV" :
                                                                                   coordinate;
    bke::node_add_link(tree,
                       *coords,
                       socket(*coords, source, false),
                       *shader,
                       socket(*shader, unused ? "Normal" : "Color", true));
    bke::node_add_link(tree,
                       *shader,
                       socket(*shader, unused ? "BSDF" : "Emission", false),
                       *output,
                       socket(*output, "Surface", true));
    BKE_ntree_update_after_single_tree_change(*main, tree);
    ids.push_back(&material->id);
  }
  if (capture_bump) {
    auto *material = BKE_material_add(main.get(), "AW_BumpUnavailable");
    auto &tree = *material->nodetree;
    auto *coords = bke::node_add_node(nullptr, tree, UString("ShaderNodeTexCoord"));
    auto *bump = bke::node_add_node(nullptr, tree, UString("ShaderNodeBump"));
    auto *pbr = bke::node_add_node(nullptr, tree, UString("ShaderNodeBsdfPrincipled"));
    auto *output = bke::node_add_node(nullptr, tree, UString("ShaderNodeOutputMaterial"));
    const float base_color[4]{-1.f, .1f, 2.f, 1};
    std::copy_n(base_color,
                4,
                static_cast<bNodeSocketValueRGBA *>(socket(*pbr, "Base Color", true).default_value)
                    ->value);
    bke::node_add_link(
        tree, *coords, socket(*coords, "Generated", false), *bump, socket(*bump, "Height", true));
    bke::node_add_link(
        tree, *bump, socket(*bump, "Normal", false), *pbr, socket(*pbr, "Normal", true));
    bke::node_add_link(
        tree, *pbr, socket(*pbr, "BSDF", false), *output, socket(*output, "Surface", true));
    BKE_ntree_update_after_single_tree_change(*main, tree);
    ids.push_back(&material->id);
  }
  auto *material = BKE_material_add(main.get(), "AW_SharedSurface");
  for (int part = 0; part < 2; ++part) {
    const float x0 = part ? 4 : 0, y0 = part ? 2 : 0, dx = part ? 3 : 2, dy = part ? 2 : 1;
    auto *mesh = BKE_mesh_add(main.get(), part ? "AW_Mesh_B" : "AW_Mesh_A");
    auto *object = BKE_object_add_only_object(
        main.get(), OB_MESH, part ? "AW_Object_B" : "AW_Object_A");
    object->data = &mesh->id;
    auto *geometry = BKE_mesh_new_nomain(4, 0, 2, 8);
    auto positions = geometry->vert_positions_for_write();
    positions[0] = {x0, y0, 0};
    positions[1] = {x0 + dx, y0, 0};
    positions[2] = {x0 + dx, y0 + dy, 0};
    positions[3] = {x0, y0 + dy, 0};
    auto offsets = geometry->face_offsets_for_write();
    offsets[0] = 0;
    offsets[1] = 4;
    offsets[2] = 8;
    auto corners = geometry->corner_verts_for_write();
    auto uv = geometry->attributes_for_write().lookup_or_add_for_write_only_span<float2>(
        "UVMap", bke::AttrDomain::Corner);
    const float left = part ? .55f : .05f, right = part ? .95f : .45f;
    for (int tile = 0; tile < 2; ++tile) {
      for (int corner = 0; corner < 4; ++corner) {
        corners[tile * 4 + corner] = corner;
        uv.span[tile * 4 + corner] = {tile + ((corner == 1 || corner == 2) ? right : left),
                                      corner >= 2 ? .9f : .1f};
      }
    }
    uv.finish();
    geometry->uv_maps_active_set("UVMap");
    geometry->uv_maps_default_set("UVMap");
    geometry->tag_topology_changed();
    bke::mesh_calc_edges(*geometry, false, false);
    BKE_mesh_nomain_to_mesh(geometry, mesh, object);
    mesh->texspace_flag = 0;
    mesh->texspace_location[0] = x0 + dx / 2;
    mesh->texspace_location[1] = y0 + dy / 2;
    mesh->texspace_location[2] = 0;
    mesh->texspace_size[0] = dx / 2;
    mesh->texspace_size[1] = dy / 2;
    mesh->texspace_size[2] = part ? 2 : 1;
    float transform[4][4];
    unit_m4(transform);
    transform[0][0] = part ? .5f : 2;
    transform[1][1] = part ? 2 : 3;
    transform[2][2] = part ? 3 : 1;
    transform[3][0] = part ? -4 : 3;
    transform[3][1] = part ? 5 : -1;
    transform[3][2] = part ? -2 : 2;
    BKE_object_apply_mat4(object, transform, false, false);
    BKE_object_material_assign(main.get(), object, material, 1, BKE_MAT_ASSIGN_OBDATA);
    ids.push_back(&object->id);
  }
  PartialWriteContext output(*main);
  for (auto *id : ids)
    output.id_add(id,
                  {PartialWriteContext::ADD_DEPENDENCIES | PartialWriteContext::MAKE_LOCAL |
                   PartialWriteContext::SET_FAKE_USER});
  require(output.is_valid(), "Native object fixture partial write is invalid");
  ReportList reports{};
  BKE_reports_init(&reports, RPT_STORE);
  const auto cleanup = qScopeGuard([&] { BKE_reports_free(&reports); });
  require(
      output.write(path.toUtf8().constData(), G_FILE_COMPRESS, BLO_WRITE_PATH_REMAP_NONE, reports),
      "Cannot write native multi-object fixture");
}
}  // namespace native_object_demo
inline QJsonObject verify_native_multi_object_fill(BlenderHost &host,
                                                   const std::function<void()> &frame)
{
  using namespace native_object_demo;
  const auto output = qEnvironmentVariable("BTS_NATIVE_MATERIAL_GPU_TEST"),
             library = output + "/native-context-fixture.blend";
  std::async(std::launch::async, [&] { fixture(library); }).get();
  // These source options cannot use owner-local coordinates as a substitute.
  for (const auto *name : {"AW_Reference", "AW_Instancer"}) {
    bool denied = false;
    try {
      host.import_native_material(library, name, std::make_shared<std::atomic_bool>(false)).get();
    }
    catch (const std::exception &error) {
      const std::string message = error.what();
      denied = message.find("reference-object") != message.npos ||
               message.find("From Instancer") != message.npos;
    }
    require(denied, "Unsupported source coordinate context was silently replaced");
  }
  auto unused_reference = host.import_native_material(library,
                                                      "AW_UnusedReference",
                                                      std::make_shared<std::atomic_bool>(false))
                              .get();
  require(bool(unused_reference.native_graph),
          "Unused reference-object Normal graph prevented native Color loading");
  require(!native_material_channel_supported(unused_reference.material, 4),
          "Unsupported native Bump extraction was advertised as available");
  auto reference_uv = host.import_native_material(
                              library, "AW_ReferenceUV", std::make_shared<std::atomic_bool>(false))
                          .get();
  require(bool(reference_uv.native_graph),
          "Unused Object reference prevented native UV channel loading");
  QJsonArray cases;
  std::uint64_t checked = 0;
  double maximum = 0;
  for (const auto *coordinate : {"Generated", "Object", "UV"}) {
    auto prepared = host.import_native_material(library,
                                                QString("AW_") + coordinate,
                                                std::make_shared<std::atomic_bool>(false))
                        .get();
    QString failure;
    NativeMaterialSourceLoader loader(host, nullptr, [] {}, [](const QString &) {});
    const auto failed = [&](const QString &error) { failure = error; };
    NewProjectOptions options;
    options.resolution = DocumentResolution::Pixels128;
    options.uv_workflow = UvWorkflow::PreserveTiles;
    options.blend.objects = {"AW_Object_A", "AW_Object_B"};
    const auto environment = qEnvironmentVariable(
        "BTS_ENVIRONMENT",
        QCoreApplication::applicationDirPath() +
            "/5.2/datafiles/studiolights/world/default-studio.hdr");
    const auto imported = host.new_project(
        library.toStdString(), environment.toStdString(), &options);
    auto scene = host.scene_snapshot();
    require(scene.mesh_objects == 2 && scene.materials.size() == 1,
            "Native context fixture did not preserve its shared material");
    TextureDocument document(options, imported, scene);
    const auto material = scene.materials.front().session_id;
    auto &set = document.set(material);
    require(set.tiles == std::set<int>({1001, 1002}),
            "Native context fixture lost its two UV tiles");
    set.precision[0] = ChannelPrecision::RGB32F;
    AssetCatalog catalog(output + "/context-catalog-" + coordinate);
    catalog.add(prepared.material);
    const auto retained = catalog.retain_project(prepared.material);
    const auto resource = *std::find_if(catalog.assets().begin(),
                                        catalog.assets().end(),
                                        [&](const auto &asset) { return asset.id == retained; });
    const auto drain = [&] {
      QElapsedTimer clock;
      clock.start();
      do {
        frame();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (clock.elapsed() > 120000)
          throw std::runtime_error("Native context live-fill work did not drain");
      } while (loader.pending() || host.texture_updates_pending());
      require(failure.isEmpty(), qPrintable(failure));
    };
    loader.prepared(prepared, [](const auto &) {}, failed);
    drain();
    host.alias_native_material(prepared.material.id, resource.id);
    host.validate_native_material_target(resource.id, set);
    const auto bottom = apply_native_material_asset(set, resource),
               top = apply_native_material_asset(set, resource);
    TextureDocument::layer(set, top).opacity[0] = .5f;
    for (auto id : {bottom, top}) {
      auto &layer = TextureDocument::layer(set, id);
      for (int c = 1; c < 5; ++c)
        layer.channels[c] = false;
      layer.ao_enabled = false;
      require(!layer.channels[3] && !layer.displacement.enabled,
              "Native fill exposed Normal or enabled displacement");
    }
    require(TextureDocument::layer(set, bottom).fill.native_material ==
                TextureDocument::layer(set, top).fill.native_material,
            "Fresh native instances duplicated their descriptor");
    std::map<int, std::vector<std::array<int, 2>>> coordinates;
    std::map<int, std::vector<std::array<float, 3>>> expected;
    std::array<std::uint64_t, 2> per_object{};
    for (int tile : {1001, 1002})
      for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
          const float u = (x + .5f) / 128, v = (y + .5f) / 128;
          const int part = u > .5f;
          const float left = part ? .55f : .05f;
          if (u < left + .01f || u > left + .39f || v < .11f || v > .89f)
            continue;  // Native RE_bake_pixels_populate offsets triangle raster coordinates by
          // -(0.5 + 0.001, 0.5 + 0.002); surface interpolation thus samples this
          // biased UV position. Texture Coordinate UV itself uses the texel center.
          const float local_u = (u + .001f / 128 - left) / .4f,
                      local_v = (v + .002f / 128 - .1f) / .8f;
          std::array<float, 3> value = std::string_view(coordinate) == "Generated" ?
                                           std::array<float, 3>{local_u, local_v, .5f} :
                                       std::string_view(coordinate) == "Object" ?
                                           std::array<float, 3>{
                                               (part ? 4 : 0) + (part ? 3 : 2) * local_u,
                                               (part ? 2 : 0) + (part ? 2 : 1) * local_v,
                                               0} :
                                           std::array<float, 3>{u + tile - 1001, v, 0};
          std::array<float, 3> converted{};
          for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
              converted[r] += prepared.native_graph->description.color_to_aces[c * 3 + r] *
                              value[c];
          coordinates[tile].push_back({x, y});
          expected[tile].push_back(converted);
          ++per_object[part];
        }
    const auto check = [&](float gain) {
      auto &current = document.set(document.texture_sets().front().material_session_uid);
      host.set_selected_material(current.material_session_uid);
      host.apply_texture_set(current, 128);
      drain();
      for (int tile : {1001, 1002}) {
        const auto values = host.diagnostic_gpu_pixels(
            current.material_session_uid, 0, coordinates.at(tile), tile);
        require(values.size() == expected.at(tile).size(),
                "Native context GPU probe lost samples");
        for (std::size_t i = 0; i < values.size(); ++i) {
          require(values[i][3] > .999f, "Native owner context lost covered texels");
          for (int c = 0; c < 3; ++c) {
            const double error = std::abs(values[i][c] - gain * expected.at(tile)[i][c]);
            maximum = std::max(maximum, error);
            if (error > 3e-5)
              throw std::runtime_error(
                  (QString(
                       "Native %1 owner/texel mismatch tile=%2 x=%3 y=%4 channel=%5 actual=%6 expected=%7")
                       .arg(coordinate)
                       .arg(tile)
                       .arg(coordinates.at(tile)[i][0])
                       .arg(coordinates.at(tile)[i][1])
                       .arg(c)
                       .arg(values[i][c], 0, 'g', 9)
                       .arg(gain * expected.at(tile)[i][c], 0, 'g', 9))
                      .toStdString());
          }
          ++checked;
        }
      }
    };
    check(1);
    auto original = document;
    const auto compilations = blender::GPU_pass_global_compilation_count();
    const auto edit = [&](const QUuid &id, float gain) {
      auto &fill = TextureDocument::layer(document.set(material), id).fill;
      auto copy = std::make_shared<NativeMaterialDescription>(*fill.native_material);
      bool found = false;
      for (auto &parameter : copy->parameters)
        if (parameter.identifier == "Strength") {
          parameter.value[0] = gain;
          found = true;
        }
      require(found, "Native live source has no Strength input");
      fill.native_material = std::move(copy);
    };
    edit(bottom, .25f);
    check(.625f);
    require(TextureDocument::layer(document.set(material), top).fill.native_material ==
                TextureDocument::layer(original.set(material), top).fill.native_material,
            "Editing one native instance changed the other");
    edit(top, .75f);
    check(.5f);
    require(blender::GPU_pass_global_compilation_count() == compilations,
            "Native per-instance parameter edit recompiled shaders");
    auto changed = document;
    document = original;
    check(1);
    document = changed;
    check(.5f);
    const auto path = output + "/native-" + coordinate + "-two-objects.blend";
    host.begin_save_project(document, path.toStdString(), catalog.project_assets());
    QElapsedTimer save;
    save.start();
    while (host.save_project_pending()) {
      frame();
      host.poll_save_project();
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
      require(save.elapsed() < 120000, "Native context project save did not drain");
    }
    require(QFile::rename(library, library + ".hidden"),
            "Cannot hide private native fixture before reopen");
    const auto restore = qScopeGuard([&] { QFile::rename(library + ".hidden", library); });
    document = host.open_project(path.toStdString());
    const auto resources = host.project_resources();
    const auto source = std::find_if(resources.begin(), resources.end(), [&](const auto &asset) {
      return asset.id == resource.id;
    });
    require(source != resources.end(), "Native packed context source is missing");
    loader.ensure(*source, resources, [](const auto &) {}, failed);
    drain();
    check(.5f);
    auto &restored = document.set(document.texture_sets().front().material_session_uid);
    require(TextureDocument::layer(restored, bottom).fill.native_material->parameters.size() ==
                TextureDocument::layer(changed.set(material), bottom)
                    .fill.native_material->parameters.size(),
            "Native parameter interface changed on reopen");
    require(
        native_material_fill_json(TextureDocument::layer(restored, bottom).fill) ==
                native_material_fill_json(
                    TextureDocument::layer(changed.set(material), bottom).fill) &&
            native_material_fill_json(TextureDocument::layer(restored, top).fill) ==
                native_material_fill_json(TextureDocument::layer(changed.set(material), top).fill),
        "Native instance parameters changed on reopen");
    cases.append(QJsonObject{{"coordinate", coordinate},
                             {"objects", 2},
                             {"tiles", QJsonArray{1001, 1002}},
                             {"owner_a_samples", qint64(per_object[0])},
                             {"owner_b_samples", qint64(per_object[1])},
                             {"live_instance_edits", true},
                             {"snapshot_undo_redo", true},
                             {"packed_source_free_reopen", true},
                             {"parameter_metadata_exact", true},
                             {"shader_recompiles", 0}});
  }
  return {{"cases", cases},
          {"covered_texel_checks", qint64(checked)},
          {"maximum_error", maximum},
          {"reference_object_rejected", true},
          {"instancer_rejected", true},
          {"unused_object_reference_uv_allowed", true},
          {"unused_normal_reference_color_allowed", true},
          {"gpu_storage_samples", true},
          {"cpu_composition", false}};
}
}  // namespace bts
