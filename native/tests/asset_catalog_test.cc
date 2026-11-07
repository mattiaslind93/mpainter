#include "AssetCatalog.hh"
#include "AssetCategories.hh"
#include "NativeMaterialImageDependencies.hh"
#include <set>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;assert(dir.isValid());bts::AssetCatalog catalog(dir.path());
int materials=0,images=0;std::set<QUuid> ids;
for(const auto &asset:catalog.assets()){
 assert(ids.insert(asset.id).second);assert(!asset.smart_material);assert(!asset.material_preset);
 if(asset.search_terms.contains("polyhaven")&&asset.material){
  ++materials;assert(asset.scope==bts::AssetScope::Builtin);assert(bts::is_native_material_asset(asset));
  assert(bts::asset_in_category(asset,"Materials")&&!bts::asset_in_category(asset,"Material presets"));
  assert(!bts::native_material_bundle_preview(asset.bytes).isEmpty());
  assert(asset.fill_preset&&asset.fill_preset->mode==0&&asset.fill_preset->native_material);
  bts::validate_native_material_dependencies(asset,catalog.assets());
 }else if(asset.search_terms.contains("internal:polyhaven-texture")){
  ++images;assert(asset.bytes.isEmpty()&&!asset.texture_files.empty());
  assert(!bts::asset_in_category(asset,"All")&&!bts::asset_in_category(asset,"Textures"));
 }else{assert(!asset.material);assert(!bts::asset_in_category(asset,"Materials"));assert(asset.bytes.isEmpty()||asset.smart_mask||asset.brush_preset);}
}
assert(materials==47&&images>100);
int generators=0;for(const auto &asset:catalog.assets())if(bts::asset_in_category(asset,"Mask generators")){++generators;assert(asset.smart_mask);assert(asset.filename.endsWith(".btsmask"));}assert(generators==10);
for(const auto &filename:{"material.btsnative.blend","material.btsuniform","CraftyPack.btsnative.blend","procedural.builtin"}){
 bts::StudioAsset imported;imported.filename=filename;imported.usages={"material"};
 assert(!bts::asset_in_category(imported,"Material presets"));
 assert(bts::asset_in_category(imported,"Materials"));
 assert(!bts::asset_in_category(imported,"Textures"));
 imported.search_terms={"builtin:standard-material"};
 assert(!bts::asset_in_category(imported,"Material presets"));
}
// A material the project copied in on apply stays off every shelf but Project:
// AssetCategories.hh shelf_shows_asset.
{bts::StudioAsset applied;applied.filename="material.btsnative.blend";applied.usages={"material"};applied.scope=bts::AssetScope::Project;
 assert(bts::asset_in_category(applied,"Materials")&&bts::asset_in_category(applied,"All")&&bts::asset_in_category(applied,"Project"));
 assert(bts::shelf_shows_asset(applied,"Project"));
 assert(!bts::shelf_shows_asset(applied,"Materials")&&!bts::shelf_shows_asset(applied,"All"));
 applied.scope=bts::AssetScope::User;
 assert(bts::shelf_shows_asset(applied,"Materials")&&bts::shelf_shows_asset(applied,"All"));
 bts::StudioAsset image;image.filename="image.png";image.usages={"texture"};image.scope=bts::AssetScope::Project;
 assert(bts::shelf_shows_asset(image,"Textures")&&bts::shelf_shows_asset(image,"All"));}
// Shelf rows with no resource kind behind them are greyed out, not hidden.
{assert(bts::unsupported_shelf_categories().size()==6);
 for(const auto &name:bts::unsupported_shelf_categories())assert(bts::shelf_category_names().contains(name)&&!bts::shelf_category_supported(name));
 for(const auto &name:{"All","Project","Alphas","Grunges","Procedurals","Textures","Brushes","Materials","Material presets","Mask generators","Environments","Flattened layers","Baked"})assert(bts::shelf_category_supported(name));}
bts::StudioAsset ordinary;ordinary.filename="image.png";ordinary.usages={"texture"};assert(!bts::asset_in_category(ordinary,"Material presets"));
bts::StudioAsset layered;layered.usages={"smartmaterial"};assert(!bts::asset_in_category(layered,"Material presets")&&!bts::asset_in_category(layered,"Materials"));
catalog.load_user_library();
assert(!catalog.assets().empty());
bts::StudioAsset item;item.name="User texture";item.filename="test.png";item.scope=bts::AssetScope::User;item.usages={"texture"};item.bytes="test-owned-by-user";const auto id=item.id;catalog.add(item);
bts::AssetCatalog reopened(dir.path());reopened.load_user_library();bool found=false;for(const auto &asset:reopened.assets())if(asset.id==id){found=true;assert(asset.bytes==item.bytes);}assert(found);return 0;}
