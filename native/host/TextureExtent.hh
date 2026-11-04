#pragma once
#include "ResourceAvailability.hh"

namespace bts {
// Sparse page keys contain two 16-bit page coordinates, each covering 64 texels.
// This is an authoring representation limit, independent of a GPU allocation.
inline constexpr int sparse_texture_page_side=64;
inline constexpr int sparse_texture_max_side=sparse_texture_page_side*(1<<16);
inline bool valid_sparse_texture_extent(int value,int minimum=1,bool power_of_two=false){
 return value>0&&value>=minimum&&value<=sparse_texture_max_side&&(!power_of_two||!(value&(value-1)));
}
inline void require_sparse_texture_extent(int value,const char *purpose,int minimum=1,bool power_of_two=false){
 if(!valid_sparse_texture_extent(value,minimum,power_of_two))throw std::invalid_argument(std::string(purpose)+": extent is outside the sparse page-key representation");
}
inline std::uint64_t texture_texel_count(int width,int height){
 require_sparse_texture_extent(width,"Texture width");require_sparse_texture_extent(height,"Texture height");return resource_product(width,height);
}
inline std::uint64_t texture_buffer_bytes(int width,int height,std::uint64_t bytes_per_texel){
 return resource_product(texture_texel_count(width,height),bytes_per_texel);
}
inline std::uint64_t sparse_texture_page_count(int width,int height){
 require_sparse_texture_extent(width,"Sparse texture width");require_sparse_texture_extent(height,"Sparse texture height");
 const auto columns=(std::uint64_t(width)+sparse_texture_page_side-1)/sparse_texture_page_side;
 const auto rows=(std::uint64_t(height)+sparse_texture_page_side-1)/sparse_texture_page_side;
 return resource_product(columns,rows);
}
inline std::uint64_t sparse_texture_page_count(int side){return sparse_texture_page_count(side,side);}
inline void require_gpu_texture_extent(int width,int height,const char *purpose){
 require_sparse_texture_extent(width,purpose);require_sparse_texture_extent(height,purpose);
 const auto maximum=texture_gpu_max_size.load(std::memory_order_relaxed);
 if(maximum>0&&(width>maximum||height>maximum))throw std::length_error(std::string(purpose)+": "+std::to_string(width)+" x "+std::to_string(height)+" exceeds current GPU texture dimension "+std::to_string(maximum));
 // Unknown telemetry is not an invented limit; the allocator checks the device.
}
}
