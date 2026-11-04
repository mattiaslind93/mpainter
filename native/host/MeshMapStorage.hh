#include <QStandardPaths>
#pragma once
#include "ResourceBudget.hh"
#include <QDir>
#include <QTemporaryFile>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
#if defined(Q_OS_UNIX)
# include <sys/mman.h>
# include <unistd.h>
# include <fcntl.h>
#endif
namespace bts {
// Every immutable bake result owns an exact disk snapshot. History copies only
// share MeshMapPixels; mapping a snapshot does not keep its pages in RAM.
class MeshMapStorage {
 QTemporaryFile file_;
 uchar *mapping_=nullptr;
 std::vector<float> fallback_;
 std::size_t size_=0;
 public:
 explicit MeshMapStorage(std::vector<float> pixels):size_(pixels.size()){
  try{
   const auto bytes=resource_product(size_,sizeof(float));
   if(bytes>std::uint64_t(std::numeric_limits<qint64>::max()))throw std::length_error("Baked map exceeds the cache file representation");
   const auto root=qEnvironmentVariable("BTS_RECOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/history")+"/mesh-maps";
   if(!QDir().mkpath(root))throw std::runtime_error("Cannot create baked map cache");
   file_.setFileTemplate(root+"/map-XXXXXX.raw");
   if(!file_.open())throw std::runtime_error(file_.errorString().toStdString());
   const char *source=reinterpret_cast<const char *>(pixels.data());
   for(std::uint64_t offset=0;offset<bytes;){
    const auto count=file_.write(source+offset,qint64(std::min<std::uint64_t>(4*1024*1024,bytes-offset)));
    if(count<=0)throw std::runtime_error(file_.errorString().toStdString());
    offset+=std::uint64_t(count);
   }
   if(!file_.flush())throw std::runtime_error(file_.errorString().toStdString());
#if defined(Q_OS_UNIX)
   if(fsync(file_.handle())!=0)throw std::runtime_error(std::strerror(errno));
#endif
   mapping_=file_.map(0,qint64(bytes));
   if(!mapping_)throw std::runtime_error(file_.errorString().toStdString());
#if defined(__linux__)
   // Writes are complete before readers receive the mapping. The kernel can
   // discard clean cache pages and fault back only the texels later requested.
   posix_fadvise(file_.handle(),0,off_t(bytes),POSIX_FADV_DONTNEED);
#endif
  }catch(const std::exception &error){
   // Cache availability must never turn a completed bake into a failed job.
   // Preserve its existing allocation; no extra history/pixel copy is made.
   std::fprintf(stderr,"BTS_BAKE_CACHE_WARNING keeping completed map in RAM: %s\n",error.what());
   if(!file_.fileName().isEmpty())file_.remove();
   fallback_=std::move(pixels);
  }
 }
 ~MeshMapStorage(){if(mapping_)file_.unmap(mapping_);}
 MeshMapStorage(const MeshMapStorage &)=delete;
 MeshMapStorage &operator=(const MeshMapStorage &)=delete;
 const float *data()const{return mapping_?reinterpret_cast<const float *>(mapping_):fallback_.data();}
 const float *begin()const{return data();}
 const float *end()const{return data()+size_;}
 const float &operator[](std::size_t index)const{return data()[index];}
 std::size_t size()const{return size_;}
 bool empty()const{return size_==0;}
 bool disk_backed()const{return mapping_!=nullptr;}
 friend bool operator==(const MeshMapStorage &a,const std::vector<float> &b){return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin());}
};
}
