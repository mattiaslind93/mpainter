#pragma once
#include "NativeMaterialGpuLibrary.hh"
#include "GpuThumbnail.hh"
#include "MaterialSpherePreview.hh"
#include <cstring>
#include <numbers>
namespace bts {
// One small native channel dispatch/fenced packet per step. Uses the already
// uploaded graph/programs; no document readback, second source decode or renderer.
class NativeMaterialResourcePreview {
 using Texture=std::unique_ptr<blender::gpu::Texture,decltype(&blender::GPU_texture_free)>;
 Texture surface_{nullptr,blender::GPU_texture_free},target_{nullptr,blender::GPU_texture_free},ao_{nullptr,blender::GPU_texture_free};
 NativeMaterialGpuContext context_;
 std::unique_ptr<GpuThumbnail> packet_;
 FillProjection mapping_;std::size_t next_=0;std::vector<int> channels_;
 QImage image_;
 public:
 bool step(NativeMaterialGpuLibrary &library,const std::string &key,const NativeMaterialDescription &description){
  using namespace blender;constexpr int side=ThumbnailMetadata::side;
  if(!target_){
   // Sphere parameterized over UV tile 1001. Its object, view, generated and
   // geometry inputs are real finite sphere attributes, independent of a project.
   constexpr int stride=sizeof(FillSurfacePoint)/sizeof(std::uint32_t),texels=(stride+3)/4;
   std::vector<std::uint32_t> packed(side*side*texels*4);
   for(int y=0;y<side;++y)for(int x=0;x<side;++x){const double longitude=((x+.5)/side-.5)*2*std::numbers::pi,latitude=((y+.5)/side-.5)*std::numbers::pi;FillSurfacePoint point;point.valid=true;point.match_object=1;point.position=point.normal={float(std::sin(longitude)*std::cos(latitude)),float(std::sin(latitude)),float(std::cos(longitude)*std::cos(latitude))};point.footprint=2.f/side;std::memcpy(packed.data()+std::size_t(y*side+x)*stride,&point,sizeof(point));}
   surface_.reset(GPU_texture_create_2d_array("Native resource sphere surface",side*texels,side,1,1,gpu::TextureFormat::UINT_32_32_32_32,GPU_TEXTURE_USAGE_SHADER_READ,nullptr));
   target_.reset(GPU_texture_create_2d("Native resource channel preview",side,side,1,gpu::TextureFormat::SFLOAT_32_32_32_32,GPU_TEXTURE_USAGE_GENERAL,nullptr));
   ao_.reset(GPU_texture_create_2d("Convex resource sphere AO",1,1,1,gpu::TextureFormat::SFLOAT_32,GPU_TEXTURE_USAGE_SHADER_READ,nullptr));
   if(!surface_||!target_||!ao_)throw std::bad_alloc();GPU_texture_update(surface_.get(),GPU_DATA_UINT,packed.data());const float white=1;GPU_texture_update(ao_.get(),GPU_DATA_FLOAT,&white);
   context_=library.default_context(key);context_.surface=surface_.get();context_.surface_size={side,side};context_.surface_stride=stride;context_.ambient_occlusion=ao_.get();NativeMaterialObject object;object.owner=1;object.bounds_min={-1,-1,-1};object.bounds_max={1,1,1};context_.objects=std::make_shared<NativeMaterialObjectTable>(std::vector<NativeMaterialObject>{object});
   for(int channel:library.effective_channels(key,description))if(channel>=0&&channel<=2)channels_.push_back(channel);
   if(std::find(channels_.begin(),channels_.end(),0)==channels_.end())throw std::runtime_error("Native resource preview requires evaluated Color");return false;
  }
  if(packet_){packet_->poll();const auto result=packet_->get();if(!result)return false;auto texture=std::make_shared<FillTexture>();texture->width=texture->height=side;auto pixels=std::make_shared<std::vector<FillTexture::Pixel>>(side*side);for(int y=0;y<side;++y)for(int x=0;x<side;++x)(*pixels)[y*side+x]=result->pixels[(side-y-1)*side+x];texture->pixels=std::move(pixels);fill_channel_texture(mapping_,channels_[next_++])=std::move(texture);packet_.reset();return false;}
  if(next_<channels_.size()){library.render(key,channels_[next_],description,target_.get(),context_,{0,0,side,side});packet_=std::make_unique<GpuThumbnail>();packet_->update({},{{target_.get(),side,side}},next_+1);return false;}
  MaterialValues base;base.base_color={1,1,1,1};image_=material_sphere_preview(base,&mapping_,64);return true;
 }
 const QImage &image()const{return image_;}
};
}
