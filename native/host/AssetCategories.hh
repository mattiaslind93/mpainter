#pragma once
#include "AssetCatalog.hh"
namespace bts {
inline QStringList shelf_category_names(){return {"All","Project","Alphas","Grunges","Procedurals","Textures","Hard Surfaces","Skin","Filters","Brushes","Particles","Tools","Materials","Material presets","Mask generators","Environments","Color profiles","Flattened layers","Baked"};}
inline bool is_hidden_shelf_asset(const StudioAsset &asset){
  return asset.search_terms.contains("internal:polyhaven-texture")||asset.search_terms.contains("internal:smart-mask-textures")||
         asset.search_terms.contains("internal:folder-material-texture")||
         asset.search_terms.contains("builtin:standard-material")||
         asset.search_terms.contains("builtin:standard-smart-material");
}
inline bool asset_in_category(const StudioAsset &asset,const QString &category){
  if(is_hidden_shelf_asset(asset))return false;
  if(asset.search_terms.contains("internal:flattened-normal")||(asset.search_terms.contains("flattened")&&asset.filename=="flattened_3.btsseq"))return false; // Retain old project pixels, hide the internal Normal resource.
  // Imported Blender shaders and channel presets are reusable fill presets too.
  if(category=="Material presets"&&asset.usages.contains("material")&&
     (asset.filename.endsWith(".btsnative.blend")||asset.filename.endsWith(".btsuniform")))return true;
  if(category=="Flattened layers")return asset.search_terms.contains("flattened");
  if(category=="Baked")return false;
  if(category=="Skin")return asset.usages.contains("skin")||asset.search_terms.contains("skin")||asset.search_terms.contains("category:skin");
  if(category=="All")return true;if(category=="Project")return asset.scope==AssetScope::Project;
  const QStringList usages{"","","alpha","grunge","procedural","texture","hardsurface","skin","filter","brush","particle","tool","material","smartmaterial","smartmask","environment","colorprofile"};const int index=shelf_category_names().indexOf(category);return index>=2&&asset.usages.contains(usages[index]);
}
}
