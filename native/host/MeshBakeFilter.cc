#include "MeshBakeFilter.hh"
#include "IMB_imbuf.hh"
#include "IMB_imbuf_types.hh"
#include "MEM_guardedalloc.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
namespace bts {
namespace {
using Buffer=std::unique_ptr<blender::ImBuf,decltype(&blender::IMB_freeImBuf)>;
Buffer buffer(int width,int height){
  Buffer result(blender::IMB_allocImBuf(width,height,blender::ImBufFlags::FloatData),blender::IMB_freeImBuf);
  if(!result||!result->float_data())throw std::runtime_error("Cannot allocate bake filtering buffer");return result;
}
// Push/pull diffusion uses Blender's associated-alpha box pyramid and bilinear reconstruction.
// Only previously empty texels are filled. Authored island texels are never blurred.
void diffuse(blender::ImBuf &image){
  std::vector<Buffer> levels;
  const blender::ImBuf *previous=&image;
  while(previous->x>1||previous->y>1){
    Buffer next(blender::IMB_scale_into_new(previous,std::max(1,previous->x/2),std::max(1,previous->y/2),blender::IMBScaleFilter::Box),blender::IMB_freeImBuf);
    if(!next)throw std::runtime_error("Cannot allocate bake diffusion pyramid");previous=next.get();levels.push_back(std::move(next));
  }
  for(int level=int(levels.size())-1;level>=0;--level){
    auto &target=level==0?image:*levels[level-1];
    Buffer up(blender::IMB_scale_into_new(levels[level].get(),target.x,target.y,blender::IMBScaleFilter::Bilinear),blender::IMB_freeImBuf);
    if(!up)throw std::runtime_error("Cannot reconstruct bake diffusion pyramid");
    auto *pixels=target.float_data_for_write();const auto *coarse=up->float_data();
    for(std::size_t i=0;i<std::size_t(target.x)*target.y*4;i+=4)if(pixels[i+3]==0&&coarse[i+3]>1e-12f){for(int c=0;c<3;++c)pixels[i+c]=coarse[i+c]/coarse[i+3];pixels[i+3]=1;}
  }
}
}
std::vector<float> filter_mesh_bake(std::vector<float> pixels,int source_width,int source_height,int width,int height,int dilation,bool diffusion,int role){
  if(pixels.size()!=std::size_t(source_width)*source_height*4)throw std::invalid_argument("Invalid supersampled bake buffer");
  auto image=buffer(width,height);
  if(source_width==width&&source_height==height)std::copy(pixels.begin(),pixels.end(),image->float_data_for_write());
  else blender::IMB_scale_box(pixels.data(),{source_width,source_height},4,image->float_data_for_write(),{width,height},true);
  pixels.clear();pixels.shrink_to_fit();
  auto *data=image->float_data_for_write();
  if(role==0||role==1)for(std::size_t i=0;i<std::size_t(width)*height*4;i+=4){const float alpha=data[i+3];if(alpha<=0)continue;float vector[3],length=0;for(int c=0;c<3;++c){vector[c]=data[i+c]/alpha*2-1;length+=vector[c]*vector[c];}length=std::sqrt(length);if(length>1e-8f)for(int c=0;c<3;++c)data[i+c]=(vector[c]/length*.5f+.5f)*alpha;}
  if(dilation){std::unique_ptr<char[],decltype([](char *p){MEM_delete(p);})> mask(MEM_new_array_uninitialized<char>(std::size_t(width)*height,__func__));for(std::size_t i=0;i<std::size_t(width)*height;++i)mask[i]=data[i*4+3]>0?FILTER_MASK_USED:0;blender::IMB_filter_extend(image.get(),mask.get(),dilation);}
  if(diffusion)diffuse(*image);
  return {image->float_data(),image->float_data()+std::size_t(width)*height*4};
}
}
