#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#if defined(__linux__)
#  include <sys/stat.h>
#endif
#include "NativeMaterialImageResource.hh"
#include "NativeMaterialImageSource.hh"
#include "NativeMaterialDecodedImage.hh"
#include "BKE_image.hh"
#include "BKE_image_hosted.hh"
#include "BKE_main.hh"
#include "BLI_listbase.h"
#include "BLI_path_utils.hh"
#include "DNA_image_types.h"
#include "DNA_packedFile_types.h"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "MEM_guardedalloc.h"

namespace bts {
using namespace blender;
namespace {
struct Entry {
  std::weak_ptr<NativeMaterialImageResource> ready;
  std::shared_future<std::shared_ptr<NativeMaterialImageResource>> loading;
};
std::mutex pool_mutex,hash_mutex;
std::map<QString,Entry> pool;
std::map<QString,std::weak_ptr<const NativeMaterialImageTileSet>> tile_sets;
struct SourceStamp {
  QString path;
  qint64 size=0,modified=0,changed=0;
  quint64 device=0,inode=0;
  quint32 modified_ns=0,changed_ns=0;
  bool operator==(const SourceStamp &)const=default;
  bool cacheable()const{return inode&&changed_ns;}
};
struct FileHash {SourceStamp stamp;QString digest;};
std::map<QString,FileHash> file_hashes;
SourceStamp source_stamp(const QString &path,int descriptor=-1)
{
  const QFileInfo info(path);
  if(!info.isFile()||!info.isReadable()||info.size()<=0)throw std::runtime_error("Native source file is missing or unreadable");
  SourceStamp stamp;stamp.path=info.canonicalFilePath();stamp.size=info.size();
  stamp.modified=info.lastModified().toMSecsSinceEpoch();stamp.changed=info.metadataChangeTime().toMSecsSinceEpoch();
#if defined(__linux__)
  struct stat status{};
  if((descriptor>=0 ? ::fstat(descriptor,&status) : ::stat(QFile::encodeName(stamp.path).constData(),&status))!=0)
    throw std::runtime_error("Cannot stat native source image");
  if(!S_ISREG(status.st_mode)||status.st_size<=0)throw std::runtime_error("Native source image is not a nonempty file");
  stamp.size=status.st_size;stamp.device=status.st_dev;stamp.inode=status.st_ino;
  stamp.modified=status.st_mtim.tv_sec;stamp.modified_ns=status.st_mtim.tv_nsec;
  stamp.changed=status.st_ctim.tv_sec;stamp.changed_ns=status.st_ctim.tv_nsec;
#else
  (void)descriptor;
#endif
  return stamp;
}
std::atomic_uint64_t decodes{0};
struct SourceIdentity {QString key,digest;};
SourceIdentity source_identity(const Main &main,const Image &image,int tile,const std::atomic_bool &cancel)
{
  QString digest;const auto selected=native_material_encoded_image_source(main,image,tile);
  if(const auto *packed=selected.packed){
    QCryptographicHash hash(QCryptographicHash::Sha256);for(qint64 offset=0;offset<packed->size;offset+=4*1024*1024){if(cancel.load())throw std::runtime_error("Native packed source hashing cancelled");hash.addData(QByteArrayView(static_cast<const char *>(packed->data)+offset,std::min<qint64>(4*1024*1024,packed->size-offset)));}digest=QString::fromLatin1(hash.result().toHex());
  }else{
    const auto encoded_path=selected.path.toUtf8();const char *path=encoded_path.constData();
    try{const auto stamp=source_stamp(QString::fromUtf8(path));
    // Device/inode and ns ctime invalidate replacement and same-size rewrites
    // even when an application restores mtime. Coarse/unsupported stamps rehash.
    std::lock_guard lock(hash_mutex);
    if(const auto found=file_hashes.find(stamp.path);stamp.cacheable()&&found!=file_hashes.end()&&found->second.stamp==stamp)digest=found->second.digest;
    else{
      QFile file(stamp.path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot hash native source image");
      if(source_stamp(stamp.path,file.handle())!=stamp)throw std::runtime_error("Native source image changed before hashing");
      QCryptographicHash hash(QCryptographicHash::Sha256);
      while(!file.atEnd()){
        if(cancel.load())throw std::runtime_error("Native source image loading cancelled");
        const auto bytes=file.read(4*1024*1024);
        if(bytes.isEmpty()&&file.error()!=QFile::NoError)throw std::runtime_error("Native source image read failed");
        hash.addData(bytes);
      }
      if(source_stamp(stamp.path,file.handle())!=stamp||source_stamp(stamp.path)!=stamp)throw std::runtime_error("Native source image changed while hashing");
      digest=QString::fromLatin1(hash.result().toHex());
      if(stamp.cacheable())file_hashes[stamp.path]={stamp,digest};
    }}catch(const std::exception &error){throw std::runtime_error(native_material_image_source_error(image,path,tile?std::string("tile ")+std::to_string(tile)+": "+error.what():std::string(error.what())));}
  }
  // The decoder uses explicit named source/graph spaces and preserves data/alpha
  // policy. Never reuse old BKE buffers that were decoded into a process-global
  // role merely because their detached Main also claimed this graph target.
  return {QString::fromUtf8(QJsonDocument(QJsonArray{"explicit-native-image",native_material_decode_policy_version,
    "source-declared-data-unchanged","image-alpha-preserved",
    digest,QString::fromUtf8(image.colorspace_settings.name),QString::fromUtf8(main.colorspace.scene_linear_name),
    int(image.alpha_mode),int(image.flag&IMA_HIGH_BITDEPTH)}).toJson(QJsonDocument::Compact)),digest};
}
}
NativeMaterialImageResource::NativeMaterialImageResource(ImBuf *buffer,QString key,QString digest):buffer_(buffer),key_(std::move(key)),encoded_sha256_(std::move(digest)){}
NativeMaterialImageResource::~NativeMaterialImageResource(){IMB_freeImBuf(buffer_);}
NativeMaterialImagePoolStatistics native_material_image_pool_statistics(){std::lock_guard lock(pool_mutex);NativeMaterialImagePoolStatistics result;result.decodes=decodes.load();for(const auto &[key,entry]:pool)result.live_resources+=!entry.ready.expired();return result;}
static std::shared_ptr<NativeMaterialImageResource> prepare_resource(
    const Main &main,const Image &image,int tile,const std::atomic_bool &cancel,bool &reused)
{
  if(cancel.load())throw std::runtime_error("Native source image loading cancelled");
  const auto identity=source_identity(main,image,tile,cancel);const auto &key=identity.key;using Result=std::shared_ptr<NativeMaterialImageResource>;std::shared_future<Result> pending;std::shared_ptr<std::promise<Result>> producer;Result result;
  {
    std::lock_guard lock(pool_mutex);std::erase_if(pool,[](const auto &entry){return entry.second.ready.expired()&&!entry.second.loading.valid();});auto &entry=pool[key];result=entry.ready.lock();
    if(result)reused=true;
    else if(entry.loading.valid()){pending=entry.loading;reused=true;}
    else{producer=std::make_shared<std::promise<Result>>();pending=producer->get_future().share();entry.loading=pending;reused=false;}
  }
  if(producer){
    try{++decodes;auto decoded=decode_native_material_image_tile(main,image,tile,cancel);if(source_identity(main,image,tile,cancel).key!=key)throw std::runtime_error("Native source image changed while decoding");result=std::make_shared<NativeMaterialImageResource>(decoded.buffer.get(),key,identity.digest);decoded.buffer.release();producer->set_value(result);std::lock_guard lock(pool_mutex);pool[key].ready=result;pool[key].loading={};}
    catch(...){producer->set_exception(std::current_exception());std::lock_guard lock(pool_mutex);pool[key].loading={};throw;}
  }
  if(!result){while(pending.wait_for(std::chrono::milliseconds(10))!=std::future_status::ready)if(cancel.load())throw std::runtime_error("Native source image loading cancelled");result=pending.get();}
  if(cancel.load())throw std::runtime_error("Native source image loading cancelled");
  return result;
}
std::shared_ptr<NativeMaterialImageResource> prepare_shared_native_image(const Main &main,Image &image,const std::atomic_bool &cancel,bool &reused){
 if(image.source!=IMA_SRC_FILE||image.type!=IMA_TYPE_IMAGE)throw std::invalid_argument("Shared native image preparation requires a single-file image");auto result=prepare_resource(main,image,0,cancel,reused);if(!BKE_image_assign_hosted_file_buffer(&image,result->buffer()))throw std::logic_error("Shared native buffer cannot replace a live/frame-specific image");return result;
}
NativeMaterialImageTileSet::NativeMaterialImageTileSet(std::map<int,std::shared_ptr<NativeMaterialImageResource>> tiles,QString key):tiles_(std::move(tiles)),key_(std::move(key)){
 if(tiles_.empty()||key_.isEmpty())throw std::invalid_argument("Native tiled source inventory is empty");for(const auto &[number,owner]:tiles_)if(number<1001||number>IMA_UDIM_MAX||!owner||!owner->buffer()||owner->buffer()->x<=0||owner->buffer()->y<=0)throw std::invalid_argument("Invalid native tiled source inventory member");
}
std::shared_ptr<const NativeMaterialImageTileSet> prepare_shared_native_tiled_image(const Main &main,const Image &image,const std::atomic_bool &cancel,bool &reused){
 const auto numbers=native_material_tiled_image_numbers(image);std::map<int,std::shared_ptr<NativeMaterialImageResource>> members;QJsonArray identity;reused=true;
 for(int number:numbers){bool shared=false;auto owner=prepare_resource(main,image,number,cancel,shared);reused&=shared;identity.append(QJsonArray{number,owner->key()});members.emplace(number,std::move(owner));}
 if(cancel.load())throw std::runtime_error("Native tiled source preparation cancelled");
 // Recheck the complete source closure before exposing any inventory.
 if(native_material_tiled_image_numbers(image)!=numbers)throw std::runtime_error("Native source tile table changed during preparation");for(const auto &[number,owner]:members)if(source_identity(main,image,number,cancel).key!=owner->key())throw std::runtime_error("Native source tile changed during preparation");
 const auto key=QString::fromUtf8(QJsonDocument(QJsonArray{"explicit-native-tile-set",identity}).toJson(QJsonDocument::Compact));std::lock_guard lock(pool_mutex);std::erase_if(tile_sets,[](const auto &entry){return entry.second.expired();});if(auto existing=tile_sets[key].lock()){reused=true;return existing;}auto result=std::make_shared<const NativeMaterialImageTileSet>(std::move(members),key);tile_sets[key]=result;return result;
}

QString native_material_encoded_image_sha256(const Main &main,const Image &image,int tile,const std::atomic_bool &cancel){return source_identity(main,image,tile,cancel).digest;}

}
