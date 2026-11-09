// SPDX-License-Identifier: GPL-3.0-or-later
#include "TextureExport.hh"
#include "OpenPbrTexturePackage.hh"
#include "OpenPbrMaterialX.hh"
#include "OpenPbrUsdImport.hh"
#include "OpenPbrUsdResources.hh"
#include "OpenPbrMaterialImport.hh"
#include "OpenPbrUsd.hh"
#include "FillTextureIO.hh"
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/Util.h>
#include <MaterialXFormat/XmlIo.h>
#include <OpenImageIO/imageio.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <stdexcept>
namespace bts {
QJsonObject verify_openpbr_texture_export(const QString &directory)
{
  const auto require = [](bool value, const char *message) {
    if (!value)
      throw std::runtime_error(message);
  };
  require(QDir().mkpath(directory), "Cannot create own MaterialX fixture directory");
  ExportRequest request;
  request.directory = directory.toStdString();
  request.mesh = "FixtureMesh.obj";
  request.format = ExportFormat::Exr32;
  request.naming_convention = ExportNamingConvention::OpenPbr;
  request.openpbr_materialx = true;
  TextureSet set;
  set.name = "Set A";
  set.material_session_uid = 37;
  set.uniform_editable = true;
  set.texture_resolution = 128;
  set.tiles = {1001, 1002};
  set.precision[0] = ChannelPrecision::RGB32F;
  set.precision[1] = set.precision[2] = set.precision[4] = ChannelPrecision::L32F;
  set.displacement.channel = set.displacement.allocated = true;
  set.displacement.midlevel = 0;
  set.displacement.strength = 1;
  set.shader_parameters.openpbr.emplace();
  set.layers.resize(1);
  auto &layer = set.layers.front();
  layer.kind = LayerKind::Fill;
  layer.values.base_color = {.25f, .5f, .75f, 1};
  layer.values.metallic = .25f;
  layer.values.roughness = .5f;
  layer.values.bump = -.125f;
  layer.displacement.enabled = true;
  layer.displacement.value = -1.f / 1024;
  ExportTile first;
  first.set = set;
  first.number = 1001;
  first.resolution = 128;
  first.bump_distance = .03125f;
  first.normal.resize(128 * 128 * 4);
  for (std::size_t i = 0; i < first.normal.size(); i += 4) {
    first.normal[i] = first.normal[i + 1] = .5f;
    first.normal[i + 2] = first.normal[i + 3] = 1;
  }
  request.tiles.push_back(first);
  auto second = first;
  second.number = 1002;
  second.set.layers.front().values.base_color = {.5f, .25f, .125f, 1};
  second.set.layers.front().values.bump = .25f;
  second.set.layers.front().displacement.value = 2.f / 1024;
  request.tiles.push_back(second);
  const auto files = export_file_list(request);
  require(files.size() == 10, "MaterialX fixture channel count differs");
  std::atomic_bool cancel = false;
  write_texture_export(request, cancel, [](int, const std::string &) {});
  int samples = 0;
  QJsonArray paths;
  for (const auto &file : files) {
    require(file.channel != 3 && file.channel != 6, "Normal image escaped export policy");
    const auto path = QDir(directory).filePath(QString::fromStdString(file.name));
    auto input = OIIO::ImageInput::open(path.toStdString());
    require(bool(input), "MaterialX fixture image is missing");
    const auto spec = input->spec();
    require(spec.format == OIIO::TypeDesc::FLOAT && spec.nchannels == (file.channel == 0 ? 3 : 1),
            "MaterialX package did not preserve scalar FP32 storage");
    std::vector<float> pixels(std::size_t(spec.width) * spec.height * spec.nchannels);
    require(input->read_image(0, 0, 0, spec.nchannels, OIIO::TypeDesc::FLOAT, pixels.data()),
            "Cannot read MaterialX EXR fixture");
    const auto index = (std::size_t(64) * spec.width + 64) * spec.nchannels;
    const auto &tile = request.tiles.at(file.tile == 1001 ? 0 : 1);
    const auto &values = tile.set.layers.front();
    const float expected = file.channel == 0 ? values.values.base_color[0] :
                           file.channel == 1 ? .25f :
                           file.channel == 2 ? .5f :
                           file.channel == 4 ? values.values.bump :
                                               values.displacement.value;
    require(std::abs(pixels[index] - expected) < 1e-7,
            "MaterialX image changed signed/raw/scene-linear pixel data");
    ++samples;
    paths.append(QString::fromStdString(file.name));
  }
  const auto metadata = openpbr_texture_package_files(request);
  require(metadata.size() == 3, "MaterialX fixture metadata plan differs");
  QFile xml(QDir(directory).filePath(QString::fromStdString(metadata.front())));
  require(xml.open(QIODevice::ReadOnly), "MaterialX package document missing");
  const auto bytes = xml.readAll();
  namespace mx = MaterialX;
  auto doc = mx::createDocument();
  mx::readFromXmlString(doc, bytes.toStdString());
  auto library = mx::createDocument();
  auto library_base = QDir(QCoreApplication::applicationDirPath()).filePath("lib/materialx");
  if (!QDir(library_base + "/libraries").exists())
    library_base = QDir(QCoreApplication::applicationDirPath()).filePath("../lib/materialx");
  if (!QDir(library_base + "/libraries").exists())
    library_base = qEnvironmentVariable("BTS_MATERIALX_BASE", library_base);
  mx::loadLibraries({"libraries"}, mx::FileSearchPath(library_base.toStdString()), library);
  doc->importLibrary(library);
  std::string message;
  if (!doc->validate(&message))
    throw std::runtime_error(message);
  auto surface = doc->getNode("StudioSurface");
  require(bool(surface) && surface->getNodeDef()->getVersionString() == "1.1.1",
          "Wrong exported MaterialX definition");
  auto color = surface->getInput("base_color")->getConnectedNode();
  require(color->getColorSpace() == "acescg" &&
              color->getInput("file")->getValueString() == "Set_A_base_color_<UDIM>.exr",
          "Wrong color/UDIM binding in package");
  auto bump = surface->getInput("geometry_normal")->getConnectedNode();
  require(bump && bump->getInput("scale")->getValue()->asA<float>() == .03125f,
          "MaterialX lost snapshotted native Bump Distance");
  require(bump && bump->getCategory() == "bump" &&
              bump->getInput("height")->getConnectedNode()->getColorSpace() == "raw",
          "Bump is not a raw scalar binding");
  auto displacement = doc->getNode("StudioDisplacementHeight");
  require(displacement && displacement->getInput("in2")->getValue()->asA<float>() == 0,
          "Signed displacement midlevel changed");
  QFile license(QDir(directory).filePath("OpenPBR-LICENSE.txt"));
  require(license.open(QIODevice::ReadOnly) && license.readAll() == openpbr_materialx_license(),
          "MaterialX package license missing or changed");
  auto invalid = request;
  invalid.format = ExportFormat::Png16;
  bool rejected = false;
  try {
    export_file_list(invalid);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Lossy MaterialX height format was accepted");
  invalid = request;
  invalid.tiles.front().set.shader_parameters.openpbr.reset();
  rejected = false;
  try {
    export_file_list(invalid);
  }
  catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "Unassigned MaterialX set was accepted");
  QFile usd(QDir(directory).filePath(QString::fromStdString(metadata[1])));
  require(usd.open(QIODevice::ReadOnly) && usd.readAll().contains("ND_BTS_OpenPBR_1_1_1"),
          "USD material library missing its reference definition");
  const auto profiles = read_openpbr_usd_profiles(usd.fileName());
  require(!profiles.empty() && profiles.front().images.size() == 5 &&
              profiles.front().displacement_midlevel == 0,
          "USD exported profile metadata did not roundtrip");
  QStringList manifest;
  for (const auto &file : files)
    manifest.append(QString::fromStdString(file.name));
  for (const auto &file : metadata)
    manifest.append(QString::fromStdString(file));
  const auto package = QDir(directory).filePath("actual-images.usdz");
  package_openpbr_usd_files(usd.fileName(), package, manifest, cancel);
  const auto packed_profiles = read_openpbr_usd_profiles(package);
  require(!packed_profiles.empty(), "Cannot inspect actual packaged EXR bindings");
  int decoded_samples = 0;
  std::shared_ptr<const FillTexture> shared_color;
  for (const auto *parameter :
       {"base_color", "base_metalness", "specular_roughness", "bump", "displacement"})
  {
    const auto resource = prepare_openpbr_usd_resource(
        packed_profiles.front().images.at(parameter),
        {1001, 1002},
        QDir(directory).filePath("source-cache"),
        cancel);
    const bool raw = std::string_view(parameter) != "base_color";
    auto decoded = decode_fill_texture(resource, raw, raw ? "" : "ACEScg");
    require(decoded->tiles && decoded->tiles->size() == 2 && decoded->raw == raw,
            "USD package did not decode both typed tiles");
    for (int tile : {1001, 1002}) {
      const auto pixel = decoded->tiles->at(tile)->sample(.5, .5, false, !raw);
      const float expected = std::string_view(parameter) == "base_color" ?
                                 (tile == 1001 ? .25f : .5f) :
                             std::string_view(parameter) == "base_metalness" ?
                                 .25f :
                             std::string_view(parameter) == "specular_roughness" ?
                                 .5f :
                             std::string_view(parameter) == "bump" ?
                                 (tile == 1001 ? -.125f : .25f) :
                                 (tile == 1001 ? -1.f / 1024 : 2.f / 1024);
      require(std::abs(pixel[0] - expected) < 1e-7,
              "Decoded USD package changed signed/raw/ACEScg texels");
      ++decoded_samples;
    }
    if (!raw) {
      shared_color = decoded;
      auto again = decode_fill_texture(resource, false, "ACEScg");
      require(again->pixels == shared_color->pixels,
              "Repeated USD resource decode did not share pixel storage");
    }
  }
  const auto scene_path = QDir(directory).filePath("material-scene.usda");
  auto scene = pxr::UsdStage::CreateNew(scene_path.toStdString());
  auto world = scene->DefinePrim(pxr::SdfPath("/World"), pxr::TfToken("Xform"));
  scene->SetDefaultPrim(world);
  pxr::UsdGeomSetStageMetersPerUnit(scene, 1);
  auto mesh = pxr::UsdGeomMesh::Define(scene, pxr::SdfPath("/World/Plane"));
  mesh.CreateSubdivisionSchemeAttr().Set(pxr::TfToken("none"));
  mesh.CreatePointsAttr().Set(pxr::VtVec3fArray{{-1, -1, 0},
                                                {0, -1, 0},
                                                {0, 1, 0},
                                                {-1, 1, 0},
                                                {0, -1, 0},
                                                {1, -1, 0},
                                                {1, 1, 0},
                                                {0, 1, 0}});
  mesh.CreateFaceVertexCountsAttr().Set(pxr::VtIntArray{4, 4});
  mesh.CreateFaceVertexIndicesAttr().Set(pxr::VtIntArray{0, 1, 2, 3, 4, 5, 6, 7});
  pxr::UsdGeomPrimvarsAPI(mesh)
      .CreatePrimvar(
          pxr::TfToken("st"), pxr::SdfValueTypeNames->TexCoord2fArray, pxr::TfToken("faceVarying"))
      .Set(pxr::VtVec2fArray{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {1, 0}, {2, 0}, {2, 1}, {1, 1}});
  auto bound = pxr::UsdShadeMaterial::Define(scene, pxr::SdfPath("/Looks/Set_A"));
  require(bound.GetPrim().GetReferences().AddReference(
              metadata[1], pxr::SdfPath("/MaterialX/Materials/StudioMaterial")),
          "Cannot bind import fixture material");
  auto preview = pxr::UsdShadeShader::Define(scene, pxr::SdfPath("/Looks/Set_A/Preview"));
  preview.CreateIdAttr().Set(pxr::TfToken("UsdPreviewSurface"));
  auto preview_color = pxr::UsdShadeShader::Define(scene, pxr::SdfPath("/Looks/Set_A/Color"));
  preview_color.CreateIdAttr().Set(pxr::TfToken("UsdUVTexture"));
  preview_color.CreateInput(pxr::TfToken("file"), pxr::SdfValueTypeNames->Asset)
      .Set(pxr::SdfAssetPath("Set_A_base_color_<UDIM>.exr"));
  preview_color.CreateInput(pxr::TfToken("sourceColorSpace"), pxr::SdfValueTypeNames->Token)
      .Set(pxr::TfToken("raw"));
  preview_color.CreateOutput(pxr::TfToken("rgb"), pxr::SdfValueTypeNames->Float3);
  preview.CreateInput(pxr::TfToken("diffuseColor"), pxr::SdfValueTypeNames->Color3f)
      .ConnectToSource(preview_color.ConnectableAPI(), pxr::TfToken("rgb"));
  preview.CreateOutput(pxr::TfToken("surface"), pxr::SdfValueTypeNames->Token);
  bound.CreateSurfaceOutput().ConnectToSource(preview.ConnectableAPI(), pxr::TfToken("surface"));
  pxr::UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim()).Bind(bound);
  require(scene->GetRootLayer()->Save(), "Cannot write import fixture scene");
  scene.Reset();
  package_openpbr_usd_files(
      scene_path, QDir(directory).filePath("material-scene.usdz"), manifest, cancel);

  const auto image_profiles = read_openpbr_usd_profiles(scene_path);
  require(image_profiles.size() == 1, "Import fixture profile count differs");
  OpenPbrMaterialImportRequest adoption;
  adoption.document = QUuid::createUuid();
  adoption.material_uid = set.material_session_uid;
  adoption.uv_tiles = true;
  adoption.source = scene_path;
  adoption.material_path = image_profiles.front().material_path;
  adoption.cache = QDir(directory).filePath("adoption-cache");
  adoption.tiles = set.tiles;
  adoption.expected = image_profiles.front().parameters;
  const auto prepared = prepare_openpbr_material(adoption, cancel, [](const QString &) {});
  auto adopted = set;
  const auto original_layer = adopted.layers.front().id;
  apply_openpbr_import_fill(adopted, prepared);
  require(adopted.layers.size() == 2 && adopted.layers.back().id == original_layer,
          "Material adoption replaced an existing layer");
  const auto &source_fill = adopted.layers.front();
  require(source_fill.values.base_color[0] == 1 && source_fill.values.base_color[1] == 1 &&
              source_fill.values.base_color[2] == 1,
          "Image-bound color was multiplied by the disconnected MaterialX default");
  require(source_fill.kind == LayerKind::Fill && source_fill.fill.mode == 6 &&
              !source_fill.channels[3] && adopted.uniform_editable,
          "Material adoption lost UV workflow or exposed internal Normal");
  for (int channel : {0, 1, 2, 4, 14})
    require(fill_channel_texture(source_fill.fill, channel) &&
                !source_fill.fill.alpha_mask[channel],
            "Imported image output consumed file alpha");
  require(adopted.displacement.strength == 1 && adopted.displacement.midlevel == 0 &&
              adopted.displacement.precision == ChannelPrecision::L32F,
          "Material adoption changed signed displacement settings");
  std::atomic_bool cancelled = true;
  rejected = false;
  try {
    prepare_openpbr_material(adoption, cancelled, [](const QString &) {});
  }
  catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Cancelled material import decoded resources");
  return {{"structural_image_adoption", true},
          {"cancelled_image_adoption", true},
          {"import_scene_fixture", "material-scene.usda"},
          {"bump_distance_snapshot", .03125},
          {"packaged_exr_decoded_samples", decoded_samples},
          {"shared_decode_storage", true},
          {"usd_profile_metadata_read", true},
          {"usd_material_library", true},
          {"files", paths},
          {"exr32_pixel_samples", samples},
          {"scalar_bump_displacement", true},
          {"udim_tiles", 2},
          {"materialx_111_semantically_valid", true},
          {"color_acescg_data_raw", true},
          {"normal_image_count", 0},
          {"license_exact", true},
          {"invalid_combinations_rejected", true}};
}
}  // namespace bts
