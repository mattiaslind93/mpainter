#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
class QTemporaryDir;
namespace bts {
struct UsdImportOptions {
  std::string scope="/";
  // Keys are absolute prim paths plus '#' plus the variant set name.
  std::map<std::string,std::string> variants;
  int subdivision=0;
  double frame=0;
  bool explicit_frame=false;
  bool operator==(const UsdImportOptions &) const = default;
};
struct UsdVariantInfo {std::string key,selected;std::vector<std::string> choices;};
struct UsdSceneInfo {
  std::vector<std::string> scopes;
  std::vector<UsdVariantInfo> variants;
  bool animated=false,subdividable=false;
  double first_frame=0,last_frame=0,meters_per_unit=1;
  int meshes=0;
};
struct FrozenUsdScene {
  std::unique_ptr<QTemporaryDir> directory;
  std::string path,population_mask;
  UsdSceneInfo info;
  FrozenUsdScene();~FrozenUsdScene();
};
bool is_usd_scene(const std::string &);
UsdSceneInfo inspect_usd_scene(const std::string &,const UsdImportOptions & = {});
// A separate anonymous session layer applies variants. Static sampled values
// replace time samples in a private flattened copy; the original stays untouched.
std::unique_ptr<FrozenUsdScene> freeze_usd_scene(const std::string &,const UsdImportOptions &);
}
