#pragma once
#include <QString>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <map>
namespace blender {struct Image;struct Main;struct ImBuf;}
namespace bts {
// An immutable graph-space ImBuf, independent of the material Main that first
// decoded it. Image caches retain references, not copies of its pixel storage.
class ColdImBuf;
class NativeMaterialImageResource {
 public:
  explicit NativeMaterialImageResource(blender::ImBuf *,QString key,QString encoded_sha256={});
  ~NativeMaterialImageResource();
  NativeMaterialImageResource(const NativeMaterialImageResource &)=delete;
  blender::ImBuf *buffer() const;
  void spill();
  const QString &key() const {return key_;}
  const QString &encoded_sha256()const{return encoded_sha256_;}
 private:
  blender::ImBuf *buffer_;
  mutable std::unique_ptr<ColdImBuf> cold_;
  QString key_;
  QString encoded_sha256_;
};
struct NativeMaterialImagePoolStatistics {std::uint64_t decodes=0;std::size_t live_resources=0;};
// The graph owns this complete immutable mapping, never the source Main/cache.
class NativeMaterialImageTileSet {
 std::map<int,std::shared_ptr<NativeMaterialImageResource>> tiles_;
 QString key_;
 public:
 NativeMaterialImageTileSet(std::map<int,std::shared_ptr<NativeMaterialImageResource>>,QString);
 const auto &tiles()const{return tiles_;}
 const QString &key()const{return key_;}
};
NativeMaterialImagePoolStatistics native_material_image_pool_statistics();
std::shared_ptr<NativeMaterialImageResource> prepare_shared_native_image(
    const blender::Main &,blender::Image &,const std::atomic_bool &,bool &reused);
std::shared_ptr<const NativeMaterialImageTileSet> prepare_shared_native_tiled_image(
    const blender::Main &,const blender::Image &,const std::atomic_bool &,bool &reused);
QString native_material_encoded_image_sha256(const blender::Main &,const blender::Image &,int tile,const std::atomic_bool &);
}
