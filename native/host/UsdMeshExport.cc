// SPDX-License-Identifier: GPL-3.0-or-later
#include "UsdMeshExport.hh"
#include "SceneSnapshot.hh"
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <stdexcept>
#include <pxr/usd/usd/stage.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUtils/usdzPackage.h>
#include "BKE_layer.hh"
#include "BKE_main.hh"
#include "BKE_material.hh"
#include "BKE_report.hh"
#include "BKE_scene.hh"
#include "BLI_listbase.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_scene_types.h"
#include "DEG_depsgraph.hh"
#include "DEG_depsgraph_build.hh"
#include "DEG_depsgraph_query.hh"
#include "NOD_defaults.hh"
#include "WM_types.hh"
#include "usd.hh"
#include "intern/usd_utils.hh"
// Pinned Blender's native worker entry; avoids the WM job/global-render wrapper.
namespace blender::io::usd {
pxr::UsdStageRefPtr export_to_stage(const USDExportParams &, Depsgraph *, const char *);
}
namespace bts {
void write_native_usd_mesh(blender::Main &main,
                           blender::Depsgraph &graph,
                           const std::string &path,
                           bool package,
                           bool triangulate,
                           const std::map<std::string, std::string> &occlusion,
                           const std::map<std::string, std::string> &material_libraries)
{
  using namespace blender;
  for (Material &material : main.materials)
    if (!material.nodetree || !material.nodetree->nodes.first) {
      MaterialValues values;
      values.base_color = {material.r, material.g, material.b, material.alpha};
      values.roughness = material.roughness;
      values.metallic = material.metallic;
      nodes::node_tree_shader_default(nullptr, &main, &material.id);
      write_material_values(main, material, values);
      DEG_id_tag_update_ex(&main, &material.id, ID_RECALC_SHADING);
    }
  DEG_relations_tag_update(&main);
  BKE_scene_graph_update_tagged(&graph, &main);
  ReportList reports{};
  BKE_reports_init(&reports, RPT_STORE | RPT_PRINT_HANDLED_BY_OWNER);
  struct Cleanup {
    ReportList &list;
    ~Cleanup()
    {
      BKE_reports_free(&list);
    }
  } cleanup{reports};
  wmJobWorkerStatus status{};
  status.reports = &reports;
  io::usd::USDExportParams params{};
  params.worker_status = &status;
  params.export_lights = false;
  params.export_cameras = false;
  params.export_curves = false;
  params.export_points = false;
  params.export_volumes = false;
  params.export_hair = false;
  params.export_armatures = false;
  params.export_shapekeys = false;
  params.convert_world_material = false;
  params.generate_materialx_network = false;
  params.export_custom_properties = false;
  params.export_subdiv = io::usd::SubdivExportMode::Tessellate;
  params.root_prim_path = "/BTS";
  params.triangulate_meshes = triangulate;
  params.convert_scene_units = io::usd::SceneUnits::Custom;
  params.custom_meters_per_unit = DEG_get_input_scene(&graph)->unit.scale_length;
  const QFileInfo destination(QString::fromStdString(path));
  QTemporaryDir temporary(destination.absolutePath() + "/.bts-usd-export-XXXXXX");
  if (!temporary.isValid())
    throw std::runtime_error("Cannot create private USD export directory");
  const auto layer_path =
      package ? temporary.filePath(destination.completeBaseName() + ".usdc").toStdString() : path;
  auto stage = io::usd::export_to_stage(params, &graph, layer_path.c_str());
  if (!stage)
    throw std::runtime_error("Native Blender USD stage export failed");
  for (const auto &[material, path] : occlusion) {
    const auto material_path = pxr::SdfPath("/BTS/_materials/" +
                                            io::usd::make_safe_name(material, true));
    auto root = stage->GetPrimAtPath(material_path);
    if (!root)
      throw std::runtime_error("USD occlusion material binding is missing");
    pxr::UsdShadeShader surface;
    for (const auto &prim : pxr::UsdPrimRange(root))
      if (prim.IsA<pxr::UsdShadeShader>()) {
        pxr::TfToken id;
        pxr::UsdShadeShader(prim).GetIdAttr().Get(&id);
        if (id == pxr::TfToken("UsdPreviewSurface")) {
          surface = pxr::UsdShadeShader(prim);
          break;
        }
      }
    if (!surface)
      throw std::runtime_error("USD material has no PreviewSurface");
    auto uv = pxr::UsdShadeShader::Define(stage,
                                          material_path.AppendChild(pxr::TfToken("__BTS_AoUv")));
    uv.CreateIdAttr().Set(pxr::TfToken("UsdPrimvarReader_float2"));
    uv.CreateInput(pxr::TfToken("varname"), pxr::SdfValueTypeNames->Token).Set(pxr::TfToken("st"));
    uv.CreateOutput(pxr::TfToken("result"), pxr::SdfValueTypeNames->Float2);
    auto texture = pxr::UsdShadeShader::Define(
        stage, material_path.AppendChild(pxr::TfToken("__BTS_Occlusion")));
    texture.CreateIdAttr().Set(pxr::TfToken("UsdUVTexture"));
    const auto relative = QDir(QFileInfo(QString::fromStdString(layer_path)).absolutePath())
                              .relativeFilePath(QString::fromStdString(path));
    texture.CreateInput(pxr::TfToken("file"), pxr::SdfValueTypeNames->Asset)
        .Set(pxr::SdfAssetPath(relative.toStdString()));
    texture.CreateInput(pxr::TfToken("sourceColorSpace"), pxr::SdfValueTypeNames->Token)
        .Set(pxr::TfToken("raw"));
    texture.CreateInput(pxr::TfToken("st"), pxr::SdfValueTypeNames->Float2)
        .ConnectToSource(uv.ConnectableAPI(), pxr::TfToken("result"));
    texture.CreateOutput(pxr::TfToken("r"), pxr::SdfValueTypeNames->Float);
    surface.CreateInput(pxr::TfToken("occlusion"), pxr::SdfValueTypeNames->Float)
        .ConnectToSource(texture.ConnectableAPI(), pxr::TfToken("r"));
  }
  // Reference the exact MaterialX library on the material that is already bound
  // to the source mesh. Keep the native universal PreviewSurface as a fallback.
  for (const auto &[name, library] : material_libraries) {
    const auto path = pxr::SdfPath("/BTS/_materials/" + io::usd::make_safe_name(name, true));
    auto prim = stage->GetPrimAtPath(path);
    if (!prim)
      throw std::runtime_error("USD OpenPBR target material is missing");
    const auto relative = QDir(QFileInfo(QString::fromStdString(layer_path)).absolutePath())
                              .relativeFilePath(QString::fromStdString(library));
    if (!prim.GetReferences().AddReference(relative.toStdString(),
                                           pxr::SdfPath("/MaterialX/Materials/StudioMaterial")))
      throw std::runtime_error("Cannot bind USD OpenPBR material library");
    pxr::UsdShadeMaterial material(prim);
    auto output = material.GetSurfaceOutput(pxr::TfToken("mtlx"));
    if (!output || !output.HasConnectedSource())
      throw std::runtime_error("USD OpenPBR material has no connected surface");
  }
  // The native tiled-image writer leaves absolute <UDIM> patterns. Anchor
  // image assets to the loose layer just like ordinary single-tile outputs.
  for (const auto &prim : stage->Traverse())
    if (prim.IsA<pxr::UsdShadeShader>()) {
      auto file = pxr::UsdShadeShader(prim).GetInput(pxr::TfToken("file"));
      pxr::SdfAssetPath asset;
      if (file && file.GetTypeName() == pxr::SdfValueTypeNames->Asset &&
          file.GetAttr().Get(&asset) &&
          QDir::isAbsolutePath(QString::fromStdString(asset.GetAssetPath())))
      {
        const QDir directory(QFileInfo(QString::fromStdString(layer_path)).absolutePath());
        file.Set(pxr::SdfAssetPath(
            directory.relativeFilePath(QString::fromStdString(asset.GetAssetPath()))
                .toStdString()));
      }
    }
  // PreviewSurface consumes RGB normals; keep the unused alpha canonical as
  // specified for normal textures, including when transparent padding is used.
  for (const auto &prim : stage->Traverse())
    if (prim.GetName() == pxr::TfToken("__BTS_UsdNormal") && prim.IsA<pxr::UsdShadeShader>()) {
      pxr::UsdShadeShader shader(prim);
      pxr::GfVec4f scale, bias;
      if (shader.GetInput(pxr::TfToken("scale")).GetAttr().Get(&scale) &&
          shader.GetInput(pxr::TfToken("bias")).GetAttr().Get(&bias))
      {
        scale[3] = 1;
        bias[3] = 0;
        shader.GetInput(pxr::TfToken("scale")).Set(scale);
        shader.GetInput(pxr::TfToken("bias")).Set(bias);
      }
    }
  if (!stage->GetRootLayer()->Save() || BKE_reports_contain(&reports, RPT_ERROR))
    throw std::runtime_error("Native Blender USD stage save failed");
  stage.Reset();
  if (package)
    package_native_usd(layer_path, path);
  if (!destination.exists() || !destination.size())
    throw std::runtime_error("USD export did not produce a complete file");
}
void package_native_usd(const std::string &layer, const std::string &package)
{
  if (!pxr::UsdUtilsCreateNewUsdzPackage(pxr::SdfAssetPath(layer), package))
    throw std::runtime_error("Native OpenUSD package export failed");
}
QJsonObject inspect_exported_usd(const std::string &path)
{
  auto stage = pxr::UsdStage::Open(path);
  if (!stage)
    throw std::runtime_error("Cannot reopen native USD export");
  QJsonArray meshes, materials, assets, shaders;
  int points = 0, faces = 0, triangles = 0, preview_shaders = 0;
  for (const auto &prim : stage->Traverse()) {
    if (prim.IsA<pxr::UsdGeomMesh>()) {
      pxr::UsdGeomMesh mesh(prim);
      pxr::VtVec3fArray positions;
      pxr::VtIntArray counts, indices;
      mesh.GetPointsAttr().Get(&positions);
      mesh.GetFaceVertexCountsAttr().Get(&counts);
      mesh.GetFaceVertexIndicesAttr().Get(&indices);
      const auto st = pxr::UsdGeomPrimvarsAPI(prim).GetPrimvar(pxr::TfToken("st"));
      pxr::VtVec2fArray uv;
      if (st)
        st.Get(&uv);
      auto material = pxr::UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
      points += positions.size();
      faces += counts.size();
      for (int n : counts)
        triangles += n - 2;
      meshes.append(QJsonObject{
          {"path", QString::fromStdString(prim.GetPath().GetString())},
          {"points", int(positions.size())},
          {"faces", int(counts.size())},
          {"uvs", int(uv.size())},
          {"corners", int(indices.size())},
          {"material",
           material ? QString::fromStdString(material.GetPrim().GetName().GetString()) :
                      QString()}});
    }
    if (prim.IsA<pxr::UsdShadeMaterial>())
      materials.append(QString::fromStdString(prim.GetName().GetString()));
    if (prim.IsA<pxr::UsdShadeShader>()) {
      QJsonObject inputs;
      pxr::UsdShadeShader shader(prim);
      pxr::TfToken id;
      shader.GetIdAttr().Get(&id);
      if (id == pxr::TfToken("UsdPreviewSurface"))
        ++preview_shaders;
      for (auto input : shader.GetInputs()) {
        pxr::SdfAssetPath asset;
        QJsonObject data{
            {"type", QString::fromStdString(input.GetTypeName().GetAsToken().GetString())}};
        pxr::SdfPathVector paths;
        input.GetAttr().GetConnections(&paths);
        QJsonArray connections;
        for (const auto &path : paths)
          connections.append(QString::fromStdString(path.GetString()));
        data["connections"] = connections;
        const auto type = input.GetTypeName().GetAsToken();
        if (type == pxr::TfToken("asset") && input.GetAttr().Get(&asset) &&
            !asset.GetAssetPath().empty())
        {
          assets.append(QString::fromStdString(asset.GetAssetPath()));
          data["value"] = QString::fromStdString(asset.GetAssetPath());
          data["resolved"] = QString::fromStdString(asset.GetResolvedPath());
        }
        else if (type == pxr::TfToken("token")) {
          pxr::TfToken value;
          if (input.GetAttr().Get(&value))
            data["value"] = QString::fromStdString(value.GetString());
        }
        else if (type == pxr::TfToken("string")) {
          std::string value;
          if (input.GetAttr().Get(&value))
            data["value"] = QString::fromStdString(value);
        }
        else if (type == pxr::TfToken("float4")) {
          pxr::GfVec4f value;
          if (input.GetAttr().Get(&value))
            data["value"] = QJsonArray{value[0], value[1], value[2], value[3]};
        }
        inputs[QString::fromStdString(input.GetBaseName().GetString())] = data;
      }
      shaders.append(QJsonObject{{"path", QString::fromStdString(prim.GetPath().GetString())},
                                 {"id", QString::fromStdString(id.GetString())},
                                 {"inputs", inputs}});
    }
  }
  return {{"meshes", meshes},
          {"materials", materials},
          {"texture_assets", assets},
          {"shaders", shaders},
          {"points", points},
          {"faces", faces},
          {"triangles", triangles},
          {"preview_shaders", preview_shaders},
          {"meters_per_unit", pxr::UsdGeomGetStageMetersPerUnit(stage)},
          {"up_axis", QString::fromStdString(pxr::UsdGeomGetStageUpAxis(stage).GetString())},
          {"default_prim", bool(stage->GetDefaultPrim())}};
}
}  // namespace bts
