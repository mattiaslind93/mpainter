#include "BlendAssetLibrary.hh"
#include "BlendLibrary.hh"
#include "BlendLibraryIndex.hh"
#include "TextureMemory.hh"
#include <QDirIterator>
#include <QFileInfo>
#include "AS_asset_catalog.hh"
#include "AS_asset_catalog_tree.hh"
namespace bts {
BlendAssetLibrary inspect_blend_asset_library(const QString &selected,std::stop_token stop,std::function<void(const QString &)> progress){
 const QFileInfo directory(selected);const bool single=directory.isFile()&&is_blend_library(selected.toStdString());if(!directory.isAbsolute()||(!directory.isDir()&&!single)||!directory.isReadable())throw std::invalid_argument("Choose an explicit readable Asset Library folder or .blend file");
 BlendAssetLibrary result;result.root=single?directory.absolutePath():directory.canonicalFilePath();const auto check=[&]{if(stop.stop_requested())throw std::runtime_error("Asset Library scan cancelled");};
 QStringList files,definitions;if(single){files.append(directory.canonicalFilePath());const auto catalog=QDir(result.root).filePath("blender_assets.cats.txt");if(QFileInfo(catalog).isFile())definitions.append(catalog);}QDirIterator paths(result.root,QDir::Files|QDir::NoSymLinks,QDirIterator::Subdirectories);
 while(!single&&paths.hasNext()){check();const auto path=paths.next();if(QFileInfo(path).fileName()=="blender_assets.cats.txt")definitions.append(path);else if(is_blend_library(path.toStdString()))files.append(path);}
 files.sort(Qt::CaseInsensitive);definitions.sort(Qt::CaseInsensitive);
 blender::asset_system::AssetCatalogService catalogs(result.root.toStdString(),blender::asset_system::AssetCatalogService::read_only_tag{});
 // Only explicitly discovered files: the native recursive loader can otherwise
 // walk a linked directory outside the folder the artist selected.
 for(const auto &file:definitions){check();catalogs.load_from_disk(file.toStdString());}
 catalogs.catalog_tree()->foreach_item([&](const blender::asset_system::AssetCatalogTreeItem &catalog){result.catalogs.insert(QString::fromStdString(catalog.get_catalog_id().str()),QString::fromStdString(catalog.catalog_path().str()));});
 std::uint64_t memory=0;
 for(const auto &file:files){check();++result.files;if(progress)progress(QString("Reading %1/%2 — %3").arg(result.files).arg(files.size()).arg(QDir(result.root).relativeFilePath(file)));
  try{for(auto entry:BlendLibraryIndex{}.read(file,stop)){check();const auto path=result.catalogs.value(entry.catalog_id);memory+=entry.preview.sizeInBytes()+entry.name.size()*2+entry.description.size()*2+sizeof(BlendLibraryMaterial);result.materials.push_back({file,QDir(result.root).relativeFilePath(file),path,std::move(entry)});}}
  catch(const std::length_error &){throw;}
  catch(const std::exception &error){check();result.errors.append(QDir(result.root).relativeFilePath(file)+": "+QString::fromUtf8(error.what()));}
 }
 return result;
}
}
