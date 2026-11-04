#include "AssetCatalog.hh"
#include "AssetCategories.hh"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
int main(int argc,char **argv){QCoreApplication app(argc,argv);QTemporaryDir dir;assert(dir.isValid());bts::AssetCatalog catalog(dir.path());
for(const auto &asset:catalog.assets()){assert(!asset.material);assert(!asset.smart_material);assert(!asset.material_preset);assert(!bts::asset_in_category(asset,"Materials"));assert(asset.bytes.isEmpty()||asset.smart_mask);}
catalog.load_user_library();
assert(!catalog.assets().empty());
bts::StudioAsset item;item.name="User texture";item.filename="test.png";item.scope=bts::AssetScope::User;item.usages={"texture"};item.bytes="test-owned-by-user";const auto id=item.id;catalog.add(item);
bts::AssetCatalog reopened(dir.path());reopened.load_user_library();bool found=false;for(const auto &asset:reopened.assets())if(asset.id==id){found=true;assert(asset.bytes==item.bytes);}assert(found);return 0;}
