// SPDX-License-Identifier: GPL-3.0-or-later
#include "ResourceAvailability.hh"
#include "UsdScene.hh"
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/nodeGraph.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/udimUtils.h>
#include <pxr/usd/sdf/propertySpec.h>
namespace bts {
namespace {
pxr::UsdStageRefPtr open(const std::string &path, const UsdImportOptions &options)
{
  QFileInfo file(QString::fromStdString(path));
  if (!file.isAbsolute() || !file.isFile() || file.size() <= 0 || !is_usd_scene(path))
    throw std::invalid_argument("USD source must be an absolute readable USD file");

  if (options.subdivision < 0 || options.subdivision > 11 || !std::isfinite(options.frame))
    throw std::invalid_argument("Invalid USD frame or native subdivision level (0–11)");
  auto layer = pxr::SdfLayer::FindOrOpen(path);
  if (!layer)
    throw std::runtime_error("Cannot open USD root layer");
  auto stage = pxr::UsdStage::Open(layer, pxr::SdfLayer::CreateAnonymous("bts-selection"));
  if (!stage)
    throw std::runtime_error("Cannot compose USD scene");
  stage->SetEditTarget(stage->GetSessionLayer());
  for (const auto &[key, value] : options.variants) {
    const auto split = key.find('#');
    if (split == std::string::npos)
      throw std::invalid_argument("Invalid USD variant key");
    auto prim = stage->GetPrimAtPath(pxr::SdfPath(key.substr(0, split)));
    if (!prim)
      throw std::invalid_argument("USD variant prim no longer exists");
    auto variant = prim.GetVariantSet(key.substr(split + 1));
    const auto names = variant.GetVariantNames();
    if (std::find(names.begin(), names.end(), value) == names.end() ||
        !variant.SetVariantSelection(value))
      throw std::invalid_argument("USD modeling variant no longer exists");
  }
  const pxr::SdfPath scope(options.scope);
  if (options.scope.empty() || !scope.IsAbsolutePath() ||
      (!scope.IsAbsoluteRootPath() && !scope.IsPrimPath()) ||
      (!scope.IsAbsoluteRootPath() && !stage->GetPrimAtPath(scope)))
    throw std::invalid_argument("USD modeling scope no longer exists");
  return stage;
}
UsdSceneInfo information(const pxr::UsdStageRefPtr &stage, const UsdImportOptions &options)
{
  UsdSceneInfo info;
  info.scopes.push_back("/");
  info.first_frame = stage->GetStartTimeCode();
  info.last_frame = stage->GetEndTimeCode();
  info.meters_per_unit = pxr::UsdGeomGetStageMetersPerUnit(stage);
  const pxr::SdfPath scope(options.scope);
  int prims = 0;
  for (auto prim : stage->Traverse()) {
    ++prims;
    const auto type = prim.GetTypeName();
    if (type == pxr::TfToken("Xform") || type == pxr::TfToken("Scope") ||
        prim.IsA<pxr::UsdGeomMesh>())
      info.scopes.push_back(prim.GetPath().GetString());
    const bool selected = prim.GetPath().HasPrefix(scope);
    if (selected || scope.HasPrefix(prim.GetPath()))
      for (auto attribute : prim.GetAttributes())
        info.animated |= attribute.ValueMightBeTimeVarying();
    if (!selected)
      continue;
    for (const auto &name : prim.GetVariantSets().GetNames()) {
      auto variants = prim.GetVariantSet(name);
      info.variants.push_back({prim.GetPath().GetString() + "#" + name,
                               variants.GetVariantSelection(),
                               variants.GetVariantNames()});
    }
    if (prim.IsA<pxr::UsdGeomMesh>()) {
      ++info.meshes;
      pxr::TfToken scheme;
      pxr::UsdGeomMesh(prim).GetSubdivisionSchemeAttr().Get(&scheme);
      info.subdividable |= scheme != pxr::UsdGeomTokens->none;
    }
    for (auto attribute : prim.GetAttributes())
      info.animated |= attribute.ValueMightBeTimeVarying();
  }
  return info;
}
}  // namespace
bool is_usd_scene(const std::string &path)
{
  const auto suffix = QFileInfo(QString::fromStdString(path)).suffix().toLower();
  return suffix == "usd" || suffix == "usda" || suffix == "usdc" || suffix == "usdz";
}
FrozenUsdScene::FrozenUsdScene() = default;
FrozenUsdScene::~FrozenUsdScene() = default;
UsdSceneInfo inspect_usd_scene(const std::string &path, const UsdImportOptions &options)
{
  return information(open(path, options), options);
}
std::unique_ptr<FrozenUsdScene> freeze_usd_scene(const std::string &path,
                                                 const UsdImportOptions &options)
{
  auto source = open(path, options);
  auto result = std::make_unique<FrozenUsdScene>();
  result->info = information(source, options);
  if (!result->info.meshes)
    throw std::runtime_error("Selected USD scope contains no polygon meshes");
  auto stage = pxr::UsdStage::Open(source->Flatten());
  if (!stage)
    throw std::runtime_error("Cannot flatten composed USD scene");
  // Flatten cannot resolve literal <UDIM> identifiers as ordinary files. Anchor
  // them against the original property stack before losing its authoring layers.
  for (auto prim : source->Traverse())
    for (auto attribute : prim.GetAttributes()) {
      pxr::SdfAssetPath asset;
      if (!attribute.Get(&asset) ||
          !pxr::UsdShadeUdimUtils::IsUdimIdentifier(asset.GetAssetPath()))
        continue;
      for (const auto &spec : attribute.GetPropertyStack())
        if (spec->HasDefaultValue()) {
          const auto anchored = pxr::UsdShadeUdimUtils::ResolveUdimPath(asset.GetAssetPath(),
                                                                        spec->GetLayer());
          if (!anchored.empty())
            stage->GetAttributeAtPath(attribute.GetPath()).Set(pxr::SdfAssetPath(anchored));
          break;
        }
    }
  if (options.scope != "/") {
    result->population_mask = options.scope;
    for (auto prim : stage->Traverse())
      if (prim.IsA<pxr::UsdShadeMaterial>() || prim.IsA<pxr::UsdShadeNodeGraph>() ||
          prim.IsA<pxr::UsdShadeShader>())
        result->population_mask += ';' + prim.GetPath().GetString();
  }
  const double time = options.explicit_frame ? options.frame : result->info.first_frame;
  std::size_t total_samples = 0;
  for (auto prim : stage->Traverse())
    for (auto attribute : prim.GetAttributes()) {
      std::vector<double> samples;
      attribute.GetTimeSamples(&samples);
      total_samples = resource_sum(total_samples, samples.size());
      if (samples.empty())
        continue;
      pxr::VtValue value;
      const bool valid = attribute.Get(&value, pxr::UsdTimeCode(time));
      for (double sample : samples)
        attribute.ClearAtTime(pxr::UsdTimeCode(sample));
      if (valid && !attribute.Set(value))
        throw std::runtime_error("Cannot freeze USD animation sample");
    }
  const auto cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                     "/mPainter/import-cache";
  if (!QDir().mkpath(cache))
    throw std::runtime_error("Cannot create private USD import cache");
  result->directory = std::make_unique<QTemporaryDir>(cache + "/usd-XXXXXX");
  if (!result->directory->isValid())
    throw std::runtime_error("Cannot create USD staging directory");
  result->path = (result->directory->path() + "/sample.usdc").toStdString();
  if (!stage->GetRootLayer()->Export(result->path))
    throw std::runtime_error("Cannot write frozen USD import scene");
  return result;
}
}  // namespace bts
