// SPDX-License-Identifier: GPL-3.0-or-later
#include "OpenPbrUsd.hh"
#include "OpenPbrParameters.hh"
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <pxr/usd/sdf/zipFile.h>
#include <QXmlStreamReader>
#include <MaterialXCore/Document.h>
#include <MaterialXFormat/XmlIo.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdMtlx/reader.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <stdexcept>
namespace bts {
void package_openpbr_usd_files(const QString &layer,
                               const QString &package,
                               const QStringList &files,
                               const std::atomic_bool &cancel)
{
  const QFileInfo root(layer), destination(package);
  const QDir directory = root.absoluteDir();
  const auto canonical = directory.canonicalPath();
  if (!root.isAbsolute() || !root.isFile() || canonical.isEmpty() ||
      destination.absoluteDir().canonicalPath() != canonical || destination.suffix() != "usdz")
    throw std::invalid_argument("USDZ requires an existing sibling USD layer");
  QStringList manifest{root.fileName()};
  manifest.append(files);
  QStringList unique;
  QSet<QString> names;
  for (const auto &name : manifest) {
    if (name.isEmpty() || QDir::isAbsolutePath(name) || name.contains('\\') ||
        QDir::cleanPath(name) != name || name == ".." || name.startsWith("../"))
      throw std::invalid_argument("Unsafe USDZ package entry");
    const QFileInfo input(directory.filePath(name));
    if (!input.isFile() || !input.canonicalFilePath().startsWith(canonical + "/"))
      throw std::invalid_argument("USDZ source is missing or outside its export directory: " +
                                  name.toStdString());
    if (!names.contains(name)) {
      names.insert(name);
      unique.append(name);
    }
  }
  if (cancel)
    return;
  auto writer = pxr::SdfZipFileWriter::CreateNew(package.toStdString());
  if (!writer)
    throw std::runtime_error("Cannot create USDZ archive");
  struct Guard {
    pxr::SdfZipFileWriter &writer;
    ~Guard()
    {
      if (writer)
        writer.Discard();
    }
  } guard{writer};
  // All exported paths are already relative and manifest-owned. Preserve .mtlx
  // bytes: the generic USD localizer cannot rewrite its read-only file format.
  for (const auto &name : unique) {
    if (cancel)
      return;
    if (writer.AddFile(directory.filePath(name).toStdString(), name.toStdString()).empty())
      throw std::runtime_error("Cannot add USDZ entry: " + name.toStdString());
  }
  if (!writer.Save())
    throw std::runtime_error("Cannot save USDZ archive");
}
QByteArray openpbr_usd_document(const QByteArray &materialx, const QString &filename)
{
  if (filename.isEmpty() || QFileInfo(filename).fileName() != filename ||
      !filename.endsWith(".mtlx") || filename.contains(QChar(0)) || filename.contains('\\'))
    throw std::invalid_argument("USD requires a sibling MaterialX filename");
  QXmlStreamReader xml(materialx);
  while (!xml.atEnd())
    xml.readNext();
  if (xml.hasError())
    throw std::invalid_argument("Invalid MaterialX XML for USD transport");
  auto document = MaterialX::createDocument();
  MaterialX::readFromXmlString(document, materialx.toStdString());
  auto definition = document->getNodeDef("ND_BTS_OpenPBR_1_1_1");
  auto surface = document->getNode("StudioSurface");
  auto source_material = document->getNode("StudioMaterial");
  if (!definition || definition->getVersionString() != "1.1.1" || !surface || !source_material ||
      surface->getNodeDefString() != definition->getName())
    throw std::invalid_argument("USD transport requires the pinned OpenPBR material");
  document->setSourceUri(filename.toStdString());
  // UsdMtlxRead traverses materials through looks. The synthetic look remains in
  // this in-memory translation only, without changing the distributed .mtlx.
  auto look = document->addLook("BTSExportLook");
  look->addMaterialAssign("BTSExportAssignment", "StudioMaterial")->setGeom("/");
  auto stage = pxr::UsdStage::CreateInMemory();
  pxr::UsdMtlxRead(document, stage);
  const pxr::SdfPath root("/MaterialX"), material_path("/MaterialX/Materials/StudioMaterial");
  auto material = pxr::UsdShadeMaterial(stage->GetPrimAtPath(material_path));
  if (!material || !material.GetSurfaceOutput(pxr::TfToken("mtlx")))
    throw std::runtime_error("USD did not translate the OpenPBR material");
  material.GetPrim()
      .CreateAttribute(pxr::TfToken("bts:openpbrLicense"), pxr::SdfValueTypeNames->Asset, true)
      .Set(pxr::SdfAssetPath("OpenPBR-LICENSE.txt"));
  auto shader = pxr::UsdShadeShader(
      stage->GetPrimAtPath(material_path.AppendChild(pxr::TfToken("StudioSurface"))));
  pxr::TfToken identifier;
  if (!shader || !shader.GetSourceAssetSubIdentifier(&identifier, pxr::TfToken("mtlx")) ||
      identifier != pxr::TfToken("ND_BTS_OpenPBR_1_1_1"))
    throw std::runtime_error("USD lost the pinned OpenPBR definition");
  material.GetPrim()
      .CreateAttribute(
          pxr::TfToken("bts:openpbrTransportVersion"), pxr::SdfValueTypeNames->Int, true)
      .Set(2);
  material.GetPrim().SetDisplayName(source_material->getAttribute("uiname"));
  for (const auto &parameter : openpbr_parameters)
    if (parameter.type == OpenPbrType::Color) {
      material.GetInput(pxr::TfToken(parameter.id))
          .GetAttr()
          .SetColorSpace(pxr::TfToken("acescg"));
      shader.GetInput(pxr::TfToken(parameter.id)).GetAttr().SetColorSpace(pxr::TfToken("acescg"));
    }
  const auto setting = [&](const char *name, float value) {
    material.GetPrim()
        .CreateAttribute(pxr::TfToken(name), pxr::SdfValueTypeNames->Float, true)
        .Set(value);
  };
  if (auto bump = document->getNode("StudioBump"))
    setting("bts:bumpScale", bump->getInput("scale")->getValue()->asA<float>());
  if (auto displacement = document->getNode("StudioDisplacement")) {
    setting("bts:displacementScale", displacement->getInput("scale")->getValue()->asA<float>());
    setting(
        "bts:displacementMidlevel",
        document->getNode("StudioDisplacementHeight")->getInput("in2")->getValue()->asA<float>());
    material.GetPrim()
        .CreateAttribute(
            pxr::TfToken("bts:displacementEnabled"), pxr::SdfValueTypeNames->Bool, true)
        .Set(bool(source_material->getInput("displacementshader")));
  }
  // Keep the source asset relative to the output USD layer, even if the SDK's
  // discovery resolver has resolved a same-named document elsewhere.
  shader.SetSourceAsset(pxr::SdfAssetPath(filename.toStdString()), pxr::TfToken("mtlx"));
  // UsdMtlxRead 26.03 drops image-node colorspace. Preserve it as standard USD
  // colorSpace metadata on the filename input, and on color outputs.
  for (const auto &node : document->getNodes())
    if (node->getCategory() == "image") {
      const auto space = node->getColorSpace();
      if (space.empty())
        throw std::invalid_argument("USD image bindings require an explicit color space");
      bool found = false;
      for (const auto &prim : stage->Traverse())
        if (prim.GetName() == pxr::TfToken(node->getName()) && prim.IsA<pxr::UsdShadeShader>()) {
          pxr::UsdShadeShader image(prim);
          auto input = image.GetInput(pxr::TfToken("file"));
          if (!input)
            throw std::runtime_error("USD image lost its filename");
          input.GetAttr().SetColorSpace(pxr::TfToken(space));
          if (node->getType() == "color3")
            image.GetOutput(pxr::TfToken("out")).GetAttr().SetColorSpace(pxr::TfToken(space));
          found = true;
        }
      if (!found)
        throw std::runtime_error("USD image binding was not translated");
    }
  stage->SetDefaultPrim(stage->GetPrimAtPath(root));
  stage->GetRootLayer()->SetDocumentation(
      "OpenPBR 1.1.1 material library; reference definition/license in sibling MaterialX/OpenPBR-LICENSE.txt. Not a viewport BSDF conformance claim.");
  std::string result;
  if (!stage->GetRootLayer()->ExportToString(&result))
    throw std::runtime_error("Cannot serialize the USD OpenPBR material");
  return QByteArray::fromStdString(result);
}
}  // namespace bts
