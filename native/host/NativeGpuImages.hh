#pragma once
#include "BKE_image.hh"
#include "BKE_image_gpu_override.hh"
#include "BKE_image_partial_update.hh"
#include "BKE_lib_id.hh"
#include "BLI_listbase.h"
#include "BLI_string.h"
#include "DNA_image_types.h"
#include "GPU_texture.hh"
#include "GPU_texture_mip_range.hh"
#include "IMB_colormanagement.hh"
#include "ChannelPrecision.hh"
#include "Performance.hh"
#include "TextureMemory.hh"
#include "TextureResidencyPolicy.hh"
#include "GPU_capabilities.hh"
#include "GPU_platform.hh"
#include <algorithm>
#include <string_view>
#include <vector>
namespace bts {
// Private adapter for this pinned Blender's ImageRuntime (not saved DNA).
// Ordinary Blender still generates/loads these descriptors normally. BTS owns
// their GPU outputs, so allocating/uploading zero CPU ImBufs serves no purpose.
inline constexpr short native_generated_gpu_bit=1<<8;
static_assert((native_generated_gpu_bit&blender::IMA_GPU_MIPMAP_COMPLETE)==0);
inline bool native_gpu_image(const blender::Image *image){return image&&image->runtime&&(image->runtime->gpuflag&native_generated_gpu_bit);}
inline blender::Image *native_generated_image(blender::Main *main,unsigned w,unsigned h,const char *name,int depth,int floating,short type,const float color[4],bool stereo,bool data,bool tiled){
 using namespace blender;
 // These two paths intentionally upload canonical CPU bake/ownership data.
 const std::string_view label(name);
 if(stereo||label.starts_with("BTS Mesh Map Inspection")||label.starts_with("BTS Geometry")){auto *img=BKE_image_add_generated(main,w,h,name,depth,floating,type,color,stereo,data,tiled);if(img&&floating)img->flag|=IMA_HIGH_BITDEPTH;return img;}
 auto *image=reinterpret_cast<Image *>(BKE_id_new(main,ID_IM,name));if(!image)return nullptr;
 image->source=tiled?IMA_SRC_TILED:IMA_SRC_GENERATED;image->type=tiled?IMA_TYPE_IMAGE:IMA_TYPE_UV_TEST;
 image->runtime->gpuflag|=native_generated_gpu_bit;
 auto *tile=static_cast<ImageTile *>(image->tiles.first);tile->gen_x=w;tile->gen_y=h;tile->gen_depth=depth;tile->gen_type=eImageGenType(type);tile->gen_flag|=IMA_GEN_FLOAT|IMA_GEN_TILE;std::copy_n(color,4,tile->gen_color);
 if(data)STRNCPY(image->colorspace_settings.name,IMB_colormanagement_role_colorspace_name_get(COLOR_ROLE_DATA));
 return image;
}
inline bool native_fill_tile(blender::Image *image,blender::ImageTile *tile){if(!native_gpu_image(image))return blender::BKE_image_fill_tile(image,tile);tile->gen_flag|=blender::IMA_GEN_FLOAT|blender::IMA_GEN_TILE;return true;}
inline bool native_scale_image(blender::Image *image,int w,int h,blender::ImageUser *user){
 using namespace blender;if(!native_gpu_image(image))return BKE_image_scale(image,w,h,user);
 auto *tile=BKE_image_get_tile(image,user?user->tile:0);if(!tile)return false;
 // Runtime targets are recreated by the GPU owner, never CPU-resampled.
 BKE_image_free_buffers(image);tile->gen_x=w;tile->gen_y=h;return true;
}
inline void native_image_precision(blender::Image *image,ChannelPrecision precision){
 if(!native_gpu_image(image))return;
 if(precision==ChannelPrecision::L32F||precision==ChannelPrecision::RGB32F)image->flag|=blender::IMA_HIGH_BITDEPTH;else image->flag&=~blender::IMA_HIGH_BITDEPTH;
}
inline void prepare_native_gpu_image(blender::Image *image){
 using namespace blender;using namespace bke::image::partial_update;
 if(!native_gpu_image(image))return;performance::Scope timing("native_gpu_target_allocate");
 const bool tiled=image->source==IMA_SRC_TILED;const int target=tiled?TEXTARGET_2D_ARRAY:TEXTARGET_2D;
 const auto format=image->flag&IMA_HIGH_BITDEPTH?gpu::TextureFormat::SFLOAT_32_32_32_32:gpu::TextureFormat::SFLOAT_16_16_16_16;
 auto **slot=&image->runtime->gputexture[target][0];if(*slot&&GPU_texture_format(*slot)==format)return;
 int width=1,height=1,count=0,last=1001;for(ImageTile &tile:image->tiles){width=std::max(width,tile.gen_x);height=std::max(height,tile.gen_y);if(tile.gen_x>1||tile.gen_y>1)++count;last=std::max(last,tile.tile_number);}
 count=std::max(1,count);
 // A 1x1 tile is a nonresident presentation descriptor; it owns no array layer.
 // Allocate only from a fresh driver report. A stale per-frame snapshot can
 // otherwise admit several full UDIM images against the same free bytes.
 int total_kb=0,free_kb=0;
 if(!GPU_type_matches(GPU_DEVICE_SOFTWARE,GPU_OS_ANY,GPU_DRIVER_ANY)&&GPU_mem_stats_supported())GPU_mem_stats_get(&total_kb,&free_kb);
 const bool known=coherent_vram_report(total_kb,free_kb);
 const auto available=known?std::uint64_t(free_kb)*1024:texture_available_ram_bytes();
 const auto required=(std::uint64_t(width)*height*count*(format==gpu::TextureFormat::SFLOAT_32_32_32_32?16:8)*4+2)/3;

 auto *texture=tiled?GPU_texture_create_2d_array(image->id.name+2,width,height,count,9999,format,GPU_TEXTURE_USAGE_GENERAL,nullptr):GPU_texture_create_2d(image->id.name+2,width,height,9999,format,GPU_TEXTURE_USAGE_GENERAL,nullptr);
 if(!texture)throw std::bad_alloc();if(*slot)BKE_image_free_gputextures(image);*slot=texture;const float empty[4]={};GPU_texture_clear(texture,GPU_DATA_FLOAT,empty);GPU_texture_filter_mode(texture,true);GPU_texture_anisotropic_filter(texture,true);GPU_texture_mipmap_range(texture,0,0);
 if(tiled){const int size=last-1000;std::vector<float> mapping(std::size_t(size)*8);for(int i=0;i<size;++i)mapping[i*4]=-1;
  int layer=0;for(ImageTile &tile:image->tiles){const bool resident=tile.gen_x>1||tile.gen_y>1;tile.runtime.tilearray_layer=resident?layer++:0;tile.runtime.tilearray_offset[0]=tile.runtime.tilearray_offset[1]=0;tile.runtime.tilearray_size[0]=tile.gen_x;tile.runtime.tilearray_size[1]=tile.gen_y;const int index=tile.tile_number-1001;mapping[index*4]=resident?tile.runtime.tilearray_layer:-1;mapping[(size+index)*4+2]=float(tile.gen_x)/width;mapping[(size+index)*4+3]=float(tile.gen_y)/height;}
  auto *map=GPU_texture_create_1d_array("BTS native UDIM descriptors",size,2,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_SHADER_READ,mapping.data());if(!map){GPU_texture_free(texture);*slot=nullptr;throw std::bad_alloc();}image->runtime->gputexture[TEXTARGET_TILE_MAPPING][0]=map;GPU_texture_mipmap_mode(map,false,false);
 }
 image->runtime->gpu_pass=image->runtime->gpu_layer=image->runtime->gpu_view=0;
 if(!image->runtime->partial_update_user)image->runtime->partial_update_user=BKE_image_partial_update_create(image);
 BKE_image_gpu_override_updated(image);
 std::printf("BTS_GPU_EMPTY_TARGET image=%s width=%d height=%d tiles=%d bytes_per_texel=%d cpu_pixels=0\n",image->id.name+2,width,height,count,format==gpu::TextureFormat::SFLOAT_32_32_32_32?16:8);
}
}
