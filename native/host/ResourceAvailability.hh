#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach/mach.h>
#endif
namespace bts {
// Device memory is published by the GPU owner; workers never query a GL context.
inline std::atomic<std::uint64_t> texture_free_vram{0};
inline std::atomic_bool texture_vram_query_valid{false};
inline std::atomic<std::uint64_t> texture_vram_query_generation{0};
inline std::atomic<int> texture_gpu_max_size{0};
inline std::atomic_bool texture_memory_refresh_needed{false};
using TextureRamQuery = std::uint64_t (*)();
inline thread_local TextureRamQuery texture_ram_query = nullptr;
struct TextureRamQueryScope {
  TextureRamQuery previous;
  explicit TextureRamQueryScope(TextureRamQuery query) : previous(texture_ram_query)
  {
    texture_ram_query = query;
  }
  ~TextureRamQueryScope()
  {
    texture_ram_query = previous;
  }
  TextureRamQueryScope(const TextureRamQueryScope &) = delete;
  TextureRamQueryScope &operator=(const TextureRamQueryScope &) = delete;
};
inline std::uint64_t resource_sum(std::uint64_t a, std::uint64_t b)
{
  if (b > std::numeric_limits<std::uint64_t>::max() - a)
    throw std::overflow_error("Resource size addition overflows 64-bit storage");
  return a + b;
}
inline std::uint64_t resource_product(std::uint64_t n, std::uint64_t stride)
{
  if (stride && n > std::numeric_limits<std::uint64_t>::max() / stride)
    throw std::overflow_error("Resource size multiplication overflows 64-bit storage");
  return n * stride;
}
inline std::uint64_t texture_available_ram_bytes()
{
  if (texture_ram_query)
    return texture_ram_query();
  std::uint64_t available = 0;
#if defined(__linux__)
  std::ifstream info("/proc/meminfo");
  std::string key, tail;
  std::uint64_t value;
  while (info >> key >> value) {
    std::getline(info, tail);
    if (key == "MemAvailable:") {
      available = resource_product(value, 1024);
      break;
    }
  }
  std::ifstream limit("/sys/fs/cgroup/memory.max"), used("/sys/fs/cgroup/memory.current");
  std::uint64_t maximum, current;
  if (limit >> maximum && used >> current)
    available = std::min(available, maximum > current ? maximum - current : 0);
#elif defined(_WIN32)
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (GlobalMemoryStatusEx(&status))
    available = status.ullAvailPhys;
#elif defined(__APPLE__)
  vm_statistics64_data_t stats{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_size_t page = 0;
  const auto host = mach_host_self();
  if (host_page_size(host, &page) == KERN_SUCCESS &&
      host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) ==
          KERN_SUCCESS)
    available = resource_product(std::uint64_t(stats.free_count) + stats.inactive_count, page);
  mach_port_deallocate(mach_task_self(), host);
#endif
  return available;
}
inline std::string resource_mebibytes(std::uint64_t bytes)
{
  return std::to_string(bytes / 1048576) + " MiB (" + std::to_string(bytes) + " bytes)";
}
struct ResourceAvailability {
  std::uint64_t available_ram = texture_available_ram_bytes();
  bool vram_known = texture_vram_query_valid.load(std::memory_order_relaxed);
  std::uint64_t available_vram = texture_free_vram.load(std::memory_order_relaxed);
  // Availability informs streaming and cache eviction, never admission.
  std::uint64_t ram() const
  {
    return available_ram;
  }
  std::uint64_t vram() const
  {
    return available_vram;
  }
};
}  // namespace bts
