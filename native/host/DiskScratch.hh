#pragma once
#include "ResourceAvailability.hh"
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <type_traits>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
# include <sys/mman.h>
# include <unistd.h>
# include <fcntl.h>
# include <cerrno>
#endif
namespace bts {
// Scratch arrays keep their exact element layout and full dimensions. File-backed
// pages are reclaimable by the OS under actual RAM pressure; no configured cap,
// downsampling, or eager anonymous copy is involved. Copies remain independent.
template<class T> struct DiskScratchAllocator {
 using value_type=T;
 using is_always_equal=std::true_type;
 using propagate_on_container_move_assignment=std::true_type;
 DiskScratchAllocator()=default;
 template<class U> DiskScratchAllocator(const DiskScratchAllocator<U>&){}
 T *allocate(std::size_t count){
  if(count>std::numeric_limits<std::size_t>::max()/sizeof(T))throw std::bad_array_new_length();
#if defined(__unix__) || defined(__APPLE__)
  const auto bytes=count*sizeof(T);if(!bytes)return nullptr;
  static_assert(alignof(T)<=4096,"Scratch mapping alignment exceeds the page size");
  std::error_code error;std::filesystem::path root;
  if(const auto *configured=std::getenv("BTS_RECOVERY_DIR"))root=configured;
  else root=std::filesystem::temp_directory_path(error);
  root/="scratch";std::filesystem::create_directories(root,error);
  void *mapping=MAP_FAILED;
  if(!error&&bytes<=std::uint64_t(std::numeric_limits<off_t>::max())){
   auto name=(root/"scratch-XXXXXX.raw").string();
   // mkstemp requires the six X characters at the end.
   name.resize(name.size()-4);std::vector<char> path(name.begin(),name.end());path.push_back(0);
   const int descriptor=::mkstemp(path.data());
   if(descriptor>=0){
    ::unlink(path.data());
    bool stored=::ftruncate(descriptor,off_t(bytes))==0;
# if defined(__linux__)
    // Reserve backing storage up front: a sparse successful ftruncate alone
    // could otherwise deliver SIGBUS later if the volume filled during baking.
    if(stored){const int reservation=::posix_fallocate(descriptor,0,off_t(bytes));stored=reservation==0||reservation==EOPNOTSUPP||reservation==ENOSYS;}
# endif
    if(stored)mapping=::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,descriptor,0);
    ::close(descriptor);
   }
  }
  // Removing/closing the temporary file leaves the shared mapping valid until
  // deallocation; there are no stale recovery files after cancellation/crashes.
  if(mapping==MAP_FAILED){
   std::fprintf(stderr,"BTS_SCRATCH_WARNING disk cache unavailable; using anonymous virtual storage\n");
   mapping=::mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  }
  if(mapping==MAP_FAILED)throw std::bad_alloc();
  ::madvise(mapping,bytes,MADV_SEQUENTIAL);
  return static_cast<T *>(mapping);
#else
  return std::allocator<T>{}.allocate(count);
#endif
 }
 void deallocate(T *pointer,std::size_t count)noexcept{
#if defined(__unix__) || defined(__APPLE__)
  if(pointer)::munmap(pointer,count*sizeof(T));
#else
  std::allocator<T>{}.deallocate(pointer,count);
#endif
 }
 template<class U>bool operator==(const DiskScratchAllocator<U>&)const noexcept{return true;}
};
template<class T>class ScratchVector:public std::vector<T,DiskScratchAllocator<T>>{
 using Base=std::vector<T,DiskScratchAllocator<T>>;
 public:
 using Base::Base;using Base::operator=;
 ScratchVector()=default;
 ScratchVector(const ScratchVector&)=default;ScratchVector(ScratchVector&&)noexcept=default;
 ScratchVector&operator=(const ScratchVector&)=default;ScratchVector&operator=(ScratchVector&&)noexcept=default;
 ScratchVector(const std::vector<T>&source):Base(source.begin(),source.end()){}
 ScratchVector&operator=(const std::vector<T>&source){this->assign(source.begin(),source.end());return *this;}
};
}
