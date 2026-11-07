#pragma once
#include "AssetCatalog.hh"
namespace bts {
inline QStringList shelf_category_names(){return {"All","Project","Alphas","Grunges","Procedurals","Textures","Hard Surfaces","Skin","Filters","Brushes","Particles","Tools","Materials","Material presets","Mask generators","Environments","Color profiles","Flattened layers","Baked"};}
// Shelf rows with nothing behind them yet. No builtin resource carries these
// usages, Import offers only Alpha, Texture, Environment, Material preset,
// Mask generator and Brush, and no code path tags an asset with them, so the
// grid for one of these can only ever come up empty. They stay in the list as
// the shape of the shelf to come, greyed out rather than silently misleading.
inline QStringList unsupported_shelf_categories(){return {"Hard Surfaces","Skin","Filters","Particles","Tools","Color profiles"};}
inline bool shelf_category_supported(const QString &category){return !unsupported_shelf_categories().contains(category);}
inline bool is_hidden_shelf_asset(const StudioAsset &asset){
  return asset.search_terms.contains("internal:polyhaven-texture")||asset.search_terms.contains("internal:smart-mask-textures")||
         asset.search_terms.contains("internal:folder-material-texture")||
         asset.search_terms.contains("builtin:standard-material")||
         asset.search_terms.contains("builtin:standard-smart-material");
}
inline bool asset_in_category(const StudioAsset &asset,const QString &category){
  if(is_hidden_shelf_asset(asset))return false;
  if(asset.search_terms.contains("internal:flattened-normal")||(asset.search_terms.contains("flattened")&&asset.filename=="flattened_3.btsseq"))return false; // Retain old project pixels, hide the internal Normal resource.
  if(category=="Materials")return material_shelf(asset)==MaterialShelf::Materials;
  if(category=="Material presets")return material_shelf(asset)==MaterialShelf::Presets;
  if(category=="Flattened layers")return asset.search_terms.contains("flattened");
  if(category=="Baked")return false;
  if(category=="Skin")return asset.usages.contains("skin")||asset.search_terms.contains("skin")||asset.search_terms.contains("category:skin");
  if(category=="All")return true;if(category=="Project")return asset.scope==AssetScope::Project;
  const QStringList usages{"","","alpha","grunge","procedural","texture","hardsurface","skin","filter","brush","particle","tool","material","smartmaterial","smartmask","environment","colorprofile"};const int index=shelf_category_names().indexOf(category);return index>=2&&asset.usages.contains(usages[index]);
}
// Applying a material copies it into the project so that the saved file carries
// it: AssetCatalog::retain_project. The copy is not a live instance of the
// library asset and nothing edits it on its own — it is the project's own
// record of what was applied — so listed beside the original under Materials or
// All it reads as a duplicate of the very thing the artist just dropped. The
// project owns it, so Project is the one shelf that shows it.
inline bool shelf_shows_asset(const StudioAsset &asset,const QString &category){
  if(!asset_in_category(asset,category))return false;
  if(category=="Project"||asset.scope!=AssetScope::Project)return true;
  return material_shelf(asset)==MaterialShelf::None;
}
}
