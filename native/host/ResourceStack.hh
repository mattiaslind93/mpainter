#pragma once
#include "ResourceAvailability.hh"
#if defined(__linux__) || defined(__APPLE__)
# include <pthread.h>
#endif
#if defined(_MSC_VER)
# include <intrin.h>
#endif
namespace bts {
struct ResourceStackRange {std::uintptr_t low=0,high=0;};
inline ResourceStackRange resource_stack_range(){
#if defined(__linux__)
 pthread_attr_t attr;if(pthread_getattr_np(pthread_self(),&attr))return {};void *base=nullptr;std::size_t bytes=0;
 const auto error=pthread_attr_getstack(&attr,&base,&bytes);pthread_attr_destroy(&attr);if(!error)return {reinterpret_cast<std::uintptr_t>(base),reinterpret_cast<std::uintptr_t>(base)+bytes};
#elif defined(__APPLE__)
 const auto high=reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));const auto bytes=pthread_get_stacksize_np(pthread_self());return {high-bytes,high};
#elif defined(_WIN32)
 ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);return {low,high};
#endif
 return {};
}
// ASan may move an address-taken local to its separate fake-stack allocation.
// Measure a real machine frame in a non-inlined function, not an instrumented
// C++ object's address. The returned number is never dereferenced.
#if defined(_MSC_VER)
__declspec(noinline) inline std::uintptr_t resource_stack_pointer(){return reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress());}
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline)) inline std::uintptr_t resource_stack_pointer(){return reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));}
#else
inline std::uintptr_t resource_stack_pointer(){return 0;}
#endif
// These supported platforms have descending stacks. Unlike an arbitrary tree
// depth cap, this guards the actual current thread's remaining stack memory.
inline void require_resource_stack(std::uint64_t frame,const char *purpose){
 static thread_local const auto range=resource_stack_range();const auto at=resource_stack_pointer();
 if(!range.low||at<range.low||at>range.high)throw std::runtime_error(std::string(purpose)+": cannot query current thread stack capacity");
 const auto remaining=at-range.low;if(frame>remaining/2)throw std::length_error(std::string(purpose)+" needs stack "+resource_mebibytes(frame)+"; available "+resource_mebibytes(remaining)+" (half reserved for callers)");
}
}
