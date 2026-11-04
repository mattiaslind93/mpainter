#pragma once
#include "NativeMaterialAsset.hh"
#include "ResourceIO.hh"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include "BKE_main.hh"
#include "BKE_scene.hh"
namespace bts {
// Explicit worker diagnostic: the real project resource pack/read boundary,
// with the private encoded-source cache removed before restoration.
inline std::vector<StudioAsset> verify_native_bundle_project_resources(
    const StudioAsset &material,const std::vector<StudioAsset> &textures,QJsonObject &proof)
{
 using namespace blender;
 std::unique_ptr<Main,decltype(&BKE_main_free)> main(BKE_main_new(),BKE_main_free);auto *scene=BKE_scene_add(main.get(),"Native bundle resources");std::vector<StudioAsset> resources{material};resources.front().scope=AssetScope::Project;for(auto asset:textures){asset.scope=AssetScope::Project;resources.push_back(std::move(asset));}
 pack_project_assets(*main,*scene,resources);
 const auto root=QFileInfo(qEnvironmentVariable("BTS_RESOURCE_CACHE")).absoluteFilePath();if(!root.contains("/bts/build/"))throw std::runtime_error("Native bundle pack test requires a private build cache");
 for(const auto &asset:textures)for(const auto &file:asset.texture_files){const auto path=QFileInfo(file.path).absoluteFilePath();if(!path.startsWith(root+"/"))throw std::runtime_error("Refusing to remove a source outside the private resource cache");if(!QFile::remove(path))throw std::runtime_error("Cannot clear native bundle test source cache");}
 auto restored=read_project_assets(*scene);if(restored.size()!=resources.size())throw std::runtime_error("Native bundle project pack lost resources");for(const auto &asset:restored){const auto before=std::find_if(resources.begin(),resources.end(),[&](const auto &source){return source.id==asset.id;});if(before==resources.end()||before->hash()!=asset.hash())throw std::runtime_error("Native bundle project resource hash changed");}
 proof["project_resource_pack_roundtrip"]=true;proof["original_snapshot_cache_removed"]=true;proof["restored_resources"]=qint64(restored.size());return restored;
}
}
