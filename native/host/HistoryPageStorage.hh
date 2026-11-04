#include <QStandardPaths>
#pragma once
#include "HistoryMemory.hh"
#include <utility>
#include <stdexcept>
#include <cstdio>
#include <cerrno>
#include <QDir>
#include <QTemporaryFile>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <vector>
#if defined(Q_OS_UNIX)
# include <sys/mman.h>
# include <unistd.h>
# include <fcntl.h>
#endif
namespace bts {
namespace history_pages {
using Pixel=std::array<float,4>;
constexpr std::size_t pixels_per_page=64*64,page_bytes=pixels_per_page*sizeof(Pixel);
// Allocation granularity, not a history/memory limit. Additional sparse arenas
// are created as needed; no stroke is discarded when an arena fills.
constexpr std::size_t arena_pages=1024,arena_bytes=arena_pages*page_bytes;
struct Arena {
 QTemporaryFile file;
 uchar *mapping=nullptr;
 std::size_t used=0;
 std::vector<std::size_t> free_slots;
 int descriptor=-1;
 Arena():file([]{
  const auto root=qEnvironmentVariable("BTS_RECOVERY_DIR",QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/history");
  const auto folder=root+"/history-pages";
  if(!QDir().mkpath(folder))throw std::runtime_error("Cannot create history page cache: "+folder.toStdString());
  return folder+"/pages-XXXXXX.raw";
 }()) {
  if(!file.open()||!file.resize(arena_bytes))throw std::runtime_error("Cannot allocate sparse history cache: "+file.errorString().toStdString());
  free_slots.reserve(arena_pages);
  descriptor=file.handle();
#if defined(__linux__)
  const int reserved=posix_fallocate(descriptor,0,arena_bytes);
  if(reserved)throw std::runtime_error("Cannot reserve history cache disk space: "+std::string(std::strerror(reserved)));
#endif
  mapping=file.map(0,arena_bytes);
  if(!mapping)throw std::runtime_error("Cannot map history cache: "+file.errorString().toStdString());
  descriptor=file.handle();
 }
 ~Arena(){if(mapping)file.unmap(mapping);}
 std::uint64_t resident(std::size_t bytes)const {
#if defined(__linux__)
  const auto system_page=std::size_t(sysconf(_SC_PAGESIZE));
  std::vector<unsigned char> state((bytes+system_page-1)/system_page);
  if(!state.empty()&&mincore(mapping,bytes,state.data())==0){std::uint64_t result=0;for(auto value:state)if(value&1)result+=system_page;return result;}
#endif
  return bytes;
 }
 void spill(std::size_t bytes)const {
#if defined(Q_OS_UNIX)
  if(bytes&&msync(mapping,bytes,MS_SYNC)!=0)throw std::runtime_error("Cannot flush history cache to disk: "+std::string(std::strerror(errno)));
  // Virtual addresses remain valid for spans and concurrent immutable readers.
  // A later read faults the exact saved bytes back in, without raster replay.
  if(bytes)madvise(mapping,bytes,MADV_DONTNEED);
# if defined(__linux__)
  if(bytes)posix_fadvise(descriptor,0,bytes,POSIX_FADV_DONTNEED);
# endif
#endif
 }
};
struct State {
 std::mutex mutex;
 std::vector<std::weak_ptr<Arena>> arenas;
 std::shared_ptr<Arena> current;
 std::future<HistorySpillResult> job;
 std::optional<HistorySpillResult> completed;
 std::atomic<std::uint64_t> live_pages{0};
 std::chrono::steady_clock::time_point last_scan{};
 ~State(){if(job.valid())job.wait();}
};
inline State &state(){static State value;return value;}
inline std::pair<std::shared_ptr<Arena>,Pixel *> allocate(){
 auto &s=state();std::lock_guard lock(s.mutex);
 const auto available=[](const std::shared_ptr<Arena> &arena){return arena&&(!arena->free_slots.empty()||arena->used<arena_pages);};
 if(!available(s.current)){
  for(const auto &weak:s.arenas)if(auto candidate=weak.lock();available(candidate)){s.current=std::move(candidate);break;}
  if(!available(s.current)){s.current=std::make_shared<Arena>();std::erase_if(s.arenas,[](const auto &item){return item.expired();});s.arenas.push_back(s.current);}
 }
 auto arena=s.current;std::size_t slot;
 if(!arena->free_slots.empty()){slot=arena->free_slots.back();arena->free_slots.pop_back();}else slot=arena->used++;
 auto *data=reinterpret_cast<Pixel *>(arena->mapping+slot*page_bytes);++s.live_pages;
 return {std::move(arena),data};
}
inline std::vector<std::pair<std::shared_ptr<Arena>,std::size_t>> snapshot(){
 auto &s=state();std::lock_guard lock(s.mutex);std::vector<std::pair<std::shared_ptr<Arena>,std::size_t>> result;
 for(const auto &weak:s.arenas)if(auto arena=weak.lock())result.emplace_back(arena,arena->used*page_bytes);
 return result;
}
inline bool request_spill(std::uint64_t budget){
 auto &s=state();std::lock_guard lock(s.mutex);
 if(s.job.valid()){
  if(s.job.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
  s.completed=s.job.get();
 }
 if(s.live_pages.load()*page_bytes<=budget)return false;
 const auto now=std::chrono::steady_clock::now();
 if(budget&&now-s.last_scan<std::chrono::seconds(1))return false;
 s.last_scan=now;
 // No document/pixel evaluation on the caller. A worker flushes old arenas;
 // shared COW pages and every history/path reference keep the same addresses.
 try{s.job=std::async(std::launch::async,[budget]{
  HistorySpillResult result;result.budget=budget;
  try{
   const auto arenas=snapshot();result.allocated=state().live_pages.load()*page_bytes;
   for(const auto &[arena,bytes]:arenas)result.resident_before+=arena->resident(bytes);
   auto resident=result.resident_before;
   for(const auto &[arena,bytes]:arenas){if(resident<=budget)break;const auto before=arena->resident(bytes);arena->spill(bytes);result.written+=before;const auto after=arena->resident(bytes);const auto freed=before>after?before-after:0;resident=resident>freed?resident-freed:0;}
   for(const auto &[arena,bytes]:arenas)result.resident_after+=arena->resident(bytes);
  }catch(const std::exception &error){result.error=error.what();}
  if(result.written||!result.error.empty())std::fprintf(stderr,"BTS_HISTORY_SPILL allocated=%llu resident_before=%llu resident_after=%llu written=%llu budget=%llu lost_operations=0 error=%s\n",(unsigned long long)result.allocated,(unsigned long long)result.resident_before,(unsigned long long)result.resident_after,(unsigned long long)result.written,(unsigned long long)budget,result.error.c_str());
  return result;
 });}catch(const std::exception &error){HistorySpillResult result;result.budget=budget;result.error=std::string("Cannot start history cache worker: ")+error.what();s.completed=std::move(result);}
 return true;
}
inline std::optional<HistorySpillResult> collect(){
 auto &s=state();std::lock_guard lock(s.mutex);
 if(s.job.valid()&&s.job.wait_for(std::chrono::seconds(0))==std::future_status::ready)s.completed=s.job.get();
 return std::exchange(s.completed,std::nullopt);
}
}
// Contiguous page payload with stable addresses. COW still lives at Page level;
// copying a Page copies exact floats into a new slice, never aliases writes.
class HistoryPageStorage {
 using Pixel=history_pages::Pixel;
 std::shared_ptr<history_pages::Arena> arena_;
 Pixel *pixels_=nullptr;
 public:
 HistoryPageStorage(){auto allocation=history_pages::allocate();arena_=std::move(allocation.first);pixels_=allocation.second;std::fill_n(pixels_,history_pages::pixels_per_page,Pixel{});}
 HistoryPageStorage(const HistoryPageStorage &)=delete;
 ~HistoryPageStorage(){
  auto &s=history_pages::state();std::lock_guard lock(s.mutex);
  arena_->free_slots.push_back((reinterpret_cast<uchar *>(pixels_)-arena_->mapping)/history_pages::page_bytes);--s.live_pages;
 }
 HistoryPageStorage &operator=(const HistoryPageStorage &other){std::copy_n(other.pixels_,history_pages::pixels_per_page,pixels_);return *this;}
 Pixel &operator[](std::size_t index){return pixels_[index];}
 const Pixel &operator[](std::size_t index)const{return pixels_[index];}
 Pixel *begin(){return pixels_;} Pixel *end(){return pixels_+history_pages::pixels_per_page;}
 const Pixel *begin()const{return pixels_;} const Pixel *end()const{return pixels_+history_pages::pixels_per_page;}
 std::size_t size()const{return history_pages::pixels_per_page;}
 Pixel *data(){return pixels_;} const Pixel *data()const{return pixels_;}
};
}
