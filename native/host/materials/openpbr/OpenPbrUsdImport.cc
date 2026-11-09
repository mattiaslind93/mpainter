#include "ResourceAvailability.hh"
#include "OpenPbrUsdImport.hh"
#include <QFileInfo>
#include <pxr/base/gf/vec3f.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/utils.h>
#include <pxr/usd/usdMtlx/utils.h>
#include <pxr/usd/sdf/propertySpec.h>
#include <cmath>
#include <stdexcept>
namespace bts {
namespace {
[[noreturn]] void fail(const pxr::UsdAttribute &attribute, const char *message)
{
  throw std::invalid_argument(attribute.GetPath().GetString() + ": " + message);
}
pxr::UsdAttribute source(const pxr::UsdShadeInput &input)
{
  if (!input)
    throw std::invalid_argument("Missing OpenPBR USD input");
  const auto values = pxr::UsdShadeUtils::GetValueProducingAttributes(input);
  if (values.size() != 1)
    fail(input.GetAttr(), "Expected one authored value or supported image source");
  return values.front();
}
float scalar(const pxr::UsdShadeInput &input)
{
  auto attribute = source(input);
  float value;
  if (!attribute.Get(&value) || !std::isfinite(value))
    fail(attribute, "Expected a finite float");
  return value;
}
OpenPbrUsdImage image(const pxr::UsdAttribute &output, bool color)
{
  pxr::UsdShadeShader shader(output.GetPrim());
  pxr::TfToken id;
  if (!shader || !shader.GetIdAttr().Get(&id) ||
      id != pxr::TfToken(color ? "ND_image_color3" : "ND_image_float"))
    fail(output, "Unsupported OpenPBR image/arithmetic network");
  auto file = shader.GetInput(pxr::TfToken("file"));
  pxr::SdfAssetPath asset;
  if (!file || !file.Get(&asset) || asset.GetAssetPath().empty())
    fail(output, "Image filename is missing");
  const auto space = file.GetAttr().GetColorSpace();
  if (color && space != pxr::TfToken("acescg"))
    fail(file.GetAttr(),
         "Only explicit ACEScg color images are supported by this transport reader");
  OpenPbrUsdImage result;
  result.binding = {QString::fromStdString(asset.GetAssetPath()),
                    color ? OpenPbrImageSpace::AcesCg : OpenPbrImageSpace::Raw};
  for (const auto &spec : file.GetAttr().GetPropertyStack())
    if (spec->HasDefaultValue()) {
      result.authoring_layer = QString::fromStdString(spec->GetLayer()->GetIdentifier());
      break;
    }
  if (result.authoring_layer.isEmpty())
    fail(file.GetAttr(), "Image has no authoring layer");
  return result;
}
}  // namespace
std::vector<OpenPbrUsdProfile> read_openpbr_usd_profiles(const QString &path)
{
  if (!QFileInfo(path).isAbsolute() || !QFileInfo(path).isFile())
    throw std::invalid_argument("Choose an existing absolute USD path");
  auto stage = pxr::UsdStage::Open(path.toStdString());
  if (!stage)
    throw std::invalid_argument("Cannot open USD material profiles");
  std::vector<OpenPbrUsdProfile> result;
  for (const auto &prim : stage->Traverse()) {
    if (!prim.IsA<pxr::UsdShadeMaterial>())
      continue;
    pxr::UsdShadeMaterial material(prim);
    auto surface = material.ComputeSurfaceSource(pxr::TfToken("mtlx"));
    if (!surface)
      continue;
    pxr::TfToken definition;
    surface.GetSourceAssetSubIdentifier(&definition, pxr::TfToken("mtlx"));
    if (definition != pxr::TfToken("ND_BTS_OpenPBR_1_1_1"))
      continue;

    std::string inherited_space;
    pxr::SdfAssetPath definition_asset;
    if (surface.GetSourceAsset(&definition_asset, pxr::TfToken("mtlx")) &&
        !definition_asset.GetResolvedPath().empty())
    {
      if (auto document = pxr::UsdMtlxGetDocument(definition_asset.GetResolvedPath()))
        inherited_space = document->getColorSpace();
    }
    OpenPbrUsdProfile profile;
    profile.material_path = QString::fromStdString(prim.GetPath().GetString());
    profile.name = QString::fromStdString(
        prim.GetDisplayName().empty() ? prim.GetName().GetString() : prim.GetDisplayName());
    for (const auto &parameter : openpbr_parameters) {
      const auto attribute = source(surface.GetInput(pxr::TfToken(parameter.id)));
      if (pxr::UsdShadeUtils::GetType(attribute.GetName()) == pxr::UsdShadeAttributeType::Output) {
        if (parameter.type == OpenPbrType::Boolean)
          fail(attribute, "Boolean OpenPBR image input is unsupported");
        profile.images[parameter.id] = image(attribute, parameter.type == OpenPbrType::Color);
        continue;
      }
      OpenPbrParameters::Value value{};
      if (parameter.type == OpenPbrType::Color) {
        pxr::GfVec3f color;
        if ((attribute.GetColorSpace() != pxr::TfToken("acescg") &&
             !(attribute.GetColorSpace().IsEmpty() && inherited_space == "acescg")) ||
            !attribute.Get(&color))
          fail(attribute, "Expected an explicitly tagged ACEScg color");
        value = {color[0], color[1], color[2]};
      }
      else if (parameter.type == OpenPbrType::Boolean) {
        bool flag;
        if (!attribute.Get(&flag))
          fail(attribute, "Expected a boolean");
        value[0] = flag ? 1 : 0;
      }
      else {
        float number;
        if (!attribute.Get(&number) || !std::isfinite(number))
          fail(attribute, "Expected a finite float");
        value[0] = number;
      }
      profile.parameters.set(parameter.id, value);
    }
    if (auto normal = surface.GetInput(pxr::TfToken("geometry_normal"));
        normal && !pxr::UsdShadeUtils::GetValueProducingAttributes(normal).empty())
    {
      const auto attribute = source(normal);
      pxr::UsdShadeShader bump(attribute.GetPrim());
      pxr::TfToken id;
      if (!bump)
        fail(attribute, "Expected a scalar Bump shader");
      bump.GetIdAttr().Get(&id);
      if (id != pxr::TfToken("ND_bump_vector3"))
        fail(attribute, "Only scalar Bump geometry-normal input is supported");
      profile.images["bump"] = image(source(bump.GetInput(pxr::TfToken("height"))), false);
      profile.bump_scale = scalar(bump.GetInput(pxr::TfToken("scale")));
    }
    auto displacement = material.ComputeDisplacementSource(pxr::TfToken("mtlx"));
    profile.displacement_enabled = bool(displacement);
    if (displacement) {
      profile.displacement_scale = scalar(displacement.GetInput(pxr::TfToken("scale")));
      const auto attribute = source(displacement.GetInput(pxr::TfToken("displacement")));
      pxr::UsdShadeShader subtract(attribute.GetPrim());
      pxr::TfToken id;
      if (!subtract)
        fail(attribute, "Expected a Displacement subtract shader");
      subtract.GetIdAttr().Get(&id);
      if (id != pxr::TfToken("ND_subtract_float"))
        fail(attribute, "Expected Displacement minus Midlevel");
      profile.displacement_midlevel = scalar(subtract.GetInput(pxr::TfToken("in2")));
      profile.images["displacement"] = image(source(subtract.GetInput(pxr::TfToken("in1"))),
                                             false);
    }
    if (!displacement && prim.GetAttribute(pxr::TfToken("bts:displacementEnabled"))) {
      pxr::UsdShadeShader subtract(stage->GetPrimAtPath(
          prim.GetPath().AppendPath(pxr::SdfPath("NodeGraphs/StudioDisplacementHeight"))));
      if (subtract)
        profile.images["displacement"] = image(source(subtract.GetInput(pxr::TfToken("in1"))),
                                               false);
    }
    for (const auto &[name, target] :
         {std::pair{"bts:bumpScale", &profile.bump_scale},
          std::pair{"bts:displacementScale", &profile.displacement_scale},
          std::pair{"bts:displacementMidlevel", &profile.displacement_midlevel}})
      if (auto attribute = prim.GetAttribute(pxr::TfToken(name));
          attribute && (!attribute.Get(target) || !std::isfinite(*target)))
        fail(attribute, "Expected a finite geometry setting");
    if (auto enabled = prim.GetAttribute(pxr::TfToken("bts:displacementEnabled")); enabled) {
      bool value;
      if (!enabled.Get(&value) || value != bool(displacement))
        fail(enabled, "Displacement-enable metadata disagrees with the connected material");
      profile.displacement_enabled = value;
    }
    if (profile.bump_scale < 0 || profile.displacement_scale < 0 ||
        profile.displacement_midlevel < 0 || profile.displacement_midlevel > 1)
      throw std::invalid_argument("Invalid OpenPBR USD geometry settings");
    profile.parameters.validate();
    result.push_back(std::move(profile));
  }
  return result;
}
}  // namespace bts
