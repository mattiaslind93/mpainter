// Test-only: compile with the exact reviewed Cycles blender/image.cpp flags.
#include "MaterialLayeringCyclesSourceProbe.hh"
#include "blender/image.h"
#include "scene/image_loader.h"
#include "util/colorspace.h"
#include "util/image_metadata.h"
#include "util/progress.h"
#include "BKE_image.hh"
#include <atomic>
#include <stdexcept>
#include <thread>
namespace bts {
struct MaterialLayeringCyclesSourceProbe::Impl {std::unique_ptr<ccl::BlenderImageLoader> loader;};
MaterialLayeringCyclesSourceProbe::MaterialLayeringCyclesSourceProbe(blender::Image &image,int tile):impl_(std::make_unique<Impl>()){
 blender::ImageUser user{};blender::BKE_imageuser_default(&user);
 impl_->loader=std::make_unique<ccl::BlenderImageLoader>(&image,&user,1,tile,true);
}
MaterialLayeringCyclesSourceProbe::~MaterialLayeringCyclesSourceProbe()=default;
MaterialLayerSourceSamples MaterialLayeringCyclesSourceProbe::read(){
 if(!impl_||!impl_->loader)throw std::logic_error("Cycles source probe has retired");
 ccl::ImageMetaData metadata;ccl::Progress progress;std::atomic<int> load_failures=0,tx_failures=0;
 const ccl::ImageLoaderParams params{false,false,{},ccl::u_colorspace_data,ccl::IMAGE_ALPHA_CHANNEL_PACKED,load_failures,tx_failures};
 if(!impl_->loader->load_metadata(metadata,params,progress))throw std::runtime_error("Cycles source metadata failed");
 metadata.finalize(ccl::IMAGE_ALPHA_CHANNEL_PACKED);
 if(metadata.width<1||metadata.height<1||metadata.channels!=4||metadata.type!=ccl::IMAGE_DATA_TYPE_FLOAT4)throw std::runtime_error("Cycles source probe requires actual float4 metadata");
 MaterialLayerSourceSamples result;result.width=int(metadata.width);result.height=int(metadata.height);result.data=metadata.colorspace==ccl::u_colorspace_data;
 result.pixels.resize(std::size_t(result.width)*result.height);
 if(!impl_->loader->load_pixels(metadata,result.pixels.data()))throw std::runtime_error("Cycles source pixels failed");
 return result;
}
bool MaterialLayeringCyclesSourceProbe::immutable_snapshot()const{return impl_&&impl_->loader&&impl_->loader->hosted_source_snapshot;}
void MaterialLayeringCyclesSourceProbe::release_on_worker(){
 std::thread worker([owner=std::move(impl_)]()mutable{owner.reset();});worker.join();
}
}
