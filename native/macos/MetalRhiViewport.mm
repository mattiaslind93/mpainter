#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include "MetalRhiViewport.hh"
#include "GPU_platform.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"
#include "mtl_context.hh"
#include "mtl_texture.hh"
#include <stdexcept>
namespace bts {
struct MetalRhiViewport::Impl {std::unique_ptr<QRhi> rhi;};
MetalRhiViewport::MetalRhiViewport():impl_(std::make_unique<Impl>()){
 using namespace blender;
 if(GPU_backend_get_type()!=GPU_BACKEND_METAL)throw std::logic_error("RHI bridge requires Metal");
 auto *context=gpu::MTLContext::get();QRhiMetalInitParams params;QRhiMetalNativeHandles native;
 native.dev=reinterpret_cast<MTLDevice *>(context->device);
 native.cmdQueue=reinterpret_cast<MTLCommandQueue *>(context->queue);
 impl_->rhi.reset(QRhi::create(QRhi::Metal,&params,{},&native));
 if(!impl_->rhi)throw std::runtime_error("Cannot create offscreen Qt Metal RHI");
}
MetalRhiViewport::~MetalRhiViewport()=default;
QImage MetalRhiViewport::capture(blender::gpu::Texture *source){
 using namespace blender;
 if(!source||GPU_texture_format(source)!=gpu::TextureFormat::UNORM_8_8_8_8||GPU_texture_is_array(source))
  throw std::invalid_argument("RHI viewport expects a 2D RGBA8 texture");
 // Queue order is the synchronization contract: submit the producer before Qt
 // submits its copy on that exact same MTLCommandQueue. No GPU_finish here.
 auto *native=static_cast<gpu::MTLTexture *>(source);
 auto handle=native->get_metal_handle_base();GPU_flush();
 GPU_texture_ref(source);
 std::unique_ptr<gpu::Texture,decltype(&GPU_texture_free)> lease(source,GPU_texture_free);
 const QSize size(GPU_texture_width(source),GPU_texture_height(source));
 auto &rhi=*impl_->rhi;
 std::unique_ptr<QRhiTexture> imported(rhi.newTexture(QRhiTexture::RGBA8,size,1,QRhiTexture::UsedAsTransferSource));
 if(!imported->createFrom({quint64(handle),0}))throw std::runtime_error("Cannot import Blender Metal texture into Qt RHI");
 std::unique_ptr<QRhiTexture> target(rhi.newTexture(QRhiTexture::RGBA8,size,1,QRhiTexture::UsedAsTransferSource));
 if(!target->create())throw std::runtime_error("Cannot allocate RHI presentation target");
 QRhiCommandBuffer *commands=nullptr;
 if(rhi.beginOffscreenFrame(&commands)!=QRhi::FrameOpSuccess)throw std::runtime_error("Cannot begin offscreen RHI frame");
 QRhiReadbackResult readback;bool complete=false;readback.completed=[&]{complete=true;};
 auto *updates=rhi.nextResourceUpdateBatch();updates->copyTexture(target.get(),imported.get());
 updates->readBackTexture(QRhiReadbackDescription(target.get()),&readback);commands->resourceUpdate(updates);
 if(rhi.endOffscreenFrame()!=QRhi::FrameOpSuccess||!complete||readback.pixelSize!=size||readback.data.size()!=size.width()*size.height()*4)
  throw std::runtime_error("RHI offscreen capture failed");
 return QImage(reinterpret_cast<const uchar *>(readback.data.constData()),size.width(),size.height(),QImage::Format_RGBA8888).copy();
}
}
