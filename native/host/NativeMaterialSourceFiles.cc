#include <QStandardPaths>
#include "NativeMaterialSourceFiles.hh"
#include "NativeMaterialImageSourceReference.hh"
#include "NativeMaterialImageSource.hh"
#include "TextureMemory.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include <QCoreApplication>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <set>
namespace bts {
namespace {
void require_worker(const std::atomic_bool &cancel){
 if(auto *app=QCoreApplication::instance();app&&QThread::currentThread()==app->thread())throw std::logic_error("Native source files require a worker");
 if(cancel.load())throw std::runtime_error("Native source files cancelled");
}
}
QString materialize_native_material_archive(const StudioAsset &asset,const std::atomic_bool &cancel)
{
  require_worker(cancel);
  require_texture_ram(std::uint64_t(asset.bytes.size()),"Native material source archive");
  const auto root=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");
  const auto folder=QDir(root).filePath(asset.hash());
  if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create native material source cache");
  const auto path=QDir(folder).filePath("material.btsnative.blend");
  QFile previous(path);
  if(previous.open(QIODevice::ReadOnly)&&previous.size()==asset.bytes.size()){
    bool equal=true;constexpr qsizetype chunk=4*1024*1024;
    for(qsizetype offset=0;offset<asset.bytes.size();offset+=chunk){require_worker(cancel);const auto count=std::min(chunk,asset.bytes.size()-offset);const auto bytes=previous.read(count);if(bytes.size()!=count||!std::equal(bytes.begin(),bytes.end(),asset.bytes.constData()+offset)){equal=false;break;}}
    if(equal)return path;
  }
  QSaveFile output(path);
  if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot cache native material source");
  constexpr qsizetype chunk=4*1024*1024;
  for(qsizetype offset=0;offset<asset.bytes.size();offset+=chunk){
    if(cancel.load())throw std::runtime_error("Native material preparation cancelled");
    const auto size=std::min(chunk,asset.bytes.size()-offset);
    if(output.write(asset.bytes.constData()+offset,size)!=size)throw std::runtime_error("Native material cache write failed");
  }
  if(!output.commit())throw std::runtime_error("Cannot commit native material cache");
  return path;
}
QString materialize_native_material_image(const NativeMaterialImageReference &image,const std::vector<StudioAsset> &textures,const std::atomic_bool &cancel){
  require_worker(cancel);image.validate();
  if(image.source==NativeMaterialImageSourceKind::Tiled){
    const auto found=std::find_if(textures.begin(),textures.end(),[&](const auto &asset){return asset.usages.contains("texture")&&asset.hash()==image.sha256;});if(found==textures.end())throw std::runtime_error((image.image+": native tiled resource is missing").toStdString());
    auto metadata=found->texture_files;for(auto &tile:metadata)tile.path.clear();if(metadata!=image.tiles)throw std::invalid_argument("Native tiled resource manifest differs from its exact reference");
    const auto cache=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");const auto folder=QDir(cache).filePath("native-tiled/"+image.sha256);if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create native tiled source cache");
    for(const auto &tile:found->texture_files){
      require_worker(cancel);auto file=tile;file.tile=0;StudioAsset view;view.usages={"texture"};view.filename=tile.filename;view.texture_files={file};const NativeMaterialImageReference reference{image.image+" tile "+QString::number(tile.tile),tile.sha256,tile.filename};const auto source=materialize_native_material_image(reference,{view},cancel);const auto destination=QDir(folder).filePath(tile.filename);
      if(QFileInfo(destination).size()!=tile.size||hash_texture_file(destination,&cancel)!=tile.sha256){QFile input(source);QSaveFile output(destination);if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot materialize native source tile");QCryptographicHash digest(QCryptographicHash::Sha256);qint64 total=0;while(!input.atEnd()){require_worker(cancel);const auto bytes=input.read(4*1024*1024);if(bytes.isEmpty()&&input.error()!=QFile::NoError)throw std::runtime_error("Native source tile read failed");digest.addData(bytes);total+=bytes.size();if(output.write(bytes)!=bytes.size())throw std::runtime_error("Native source tile write failed");}if(total!=tile.size||QString::fromLatin1(digest.result().toHex())!=tile.sha256||!output.commit())throw std::runtime_error("Native source tile changed during materialization");}
    }
    require_worker(cancel);return QDir(folder).absoluteFilePath(image.filename);
  }
  const StudioAsset *found=nullptr;
  for(const auto &texture:textures)if(texture.usages.contains("texture")&&texture.hash()==image.sha256){found=&texture;break;}
  if(!found)throw std::runtime_error((image.image+": native source texture is missing from the resource snapshot").toStdString());
  QString source;
  if(found->texture_files.size()==1&&found->texture_files.front().tile==0)source=found->texture_files.front().path;
  else if(!found->bytes.isEmpty()){
    const auto cache=qEnvironmentVariable("BTS_RESOURCE_CACHE",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/resources");
    const auto folder=QDir(cache).filePath(image.sha256);if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create native source cache");
    source=QDir(folder).filePath(image.filename);
    if(!QFileInfo(source).isFile()||hash_texture_file(source,&cancel)!=image.sha256){
      QSaveFile output(source);if(!output.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot restore native source image");
      constexpr qsizetype chunk=4*1024*1024;
      for(qsizetype offset=0;offset<found->bytes.size();offset+=chunk){require_worker(cancel);const auto count=std::min(chunk,found->bytes.size()-offset);if(output.write(found->bytes.constData()+offset,count)!=count)throw std::runtime_error("Cannot write native source image");}
      if(!output.commit())throw std::runtime_error("Cannot commit native source image");
    }
  }
  if(source.isEmpty()||!QFileInfo(source).isFile()||hash_texture_file(source,&cancel)!=image.sha256)throw std::runtime_error((image.image+": native source resource missing or checksum mismatch ("+image.sha256+")").toStdString());
  return QFileInfo(source).absoluteFilePath();
}
void bind_native_material_source_images(blender::Main &main,const NativeMaterialBundle &bundle,const std::vector<StudioAsset> &textures,const std::atomic_bool &cancel){
  require_worker(cancel);
  struct Binding {blender::Image *image;QByteArray path;};std::vector<Binding> pending;std::set<QString> names;
  for(const auto &image:bundle.images){
    image.validate();if(!names.insert(image.image).second)throw std::invalid_argument("Duplicate native image source binding");blender::Image *target=nullptr;for(auto &candidate:main.images)if(image.image==QString::fromUtf8(candidate.id.name+2)){if(target)throw std::invalid_argument("Native image source name is ambiguous");target=&candidate;}if(!target)throw std::runtime_error((image.image+": native archive image binding was not found").toStdString());
    if(image.source==NativeMaterialImageSourceKind::Tiled){if(target->type!=blender::IMA_TYPE_IMAGE||target->flag&blender::IMA_USE_VIEWS)throw std::invalid_argument("Native TILED binding requires an ordinary single-view source");const auto numbers=native_material_tiled_image_numbers(*target);std::vector<int> expected;for(const auto &tile:image.tiles)expected.push_back(tile.tile);if(numbers!=expected||QFileInfo(native_material_tiled_image_pattern(main,*target)).fileName()!=image.filename)throw std::invalid_argument("Native tiled target pattern/tile table differs from the immutable reference");}
    else if(target->source!=blender::IMA_SRC_FILE)throw std::invalid_argument("Native FILE reference cannot replace a different image source kind");
    const auto source=materialize_native_material_image(image,textures,cancel);const auto path=QFileInfo(source).absoluteFilePath().toUtf8();if(path.size()>=qsizetype(sizeof(target->filepath)))throw std::length_error((QString("Native source image path needs %1 UTF-8 bytes; Image.filepath stores at most %2 bytes plus NUL").arg(path.size()).arg(sizeof(target->filepath)-1)).toStdString());pending.push_back({target,path});
  }
  require_worker(cancel);
  // Every source and target is valid before any path/base is published. The
  // marker restorer itself validates all markers before its nonthrowing commit.
  restore_missing_native_image_references(main);
  for(const auto &binding:pending)blender::STRNCPY(binding.image->filepath,binding.path.constData());
}
}
